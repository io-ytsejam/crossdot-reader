#include "CloudSyncService.h"

#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <string>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>
#else
#error "Cloud sync currently requires the wolfSSL SecureHttpClient path."
#endif

#include "CloudSyncPolicy.h"
#include "CloudSyncStore.h"
#include "ReadingTime.h"
#include "StatisticsStore.h"

namespace {

// Upload-cap against pile-ups on a slow link. A full 90-day history across a
// few books a day is far under this; the cap exists so a pathological payload
// can never OOM the export buffer.
constexpr size_t MAX_EXPORT_BYTES = 256 * 1024;
constexpr int SYNC_TASK_STACK = 4096;  // HTTPS + JSON: network-size stack
constexpr int MAX_WRITES = 6;          // Content-Length + JSON string + headers

// Lightweight JSON string escaper/emitter for the export body, avoiding
// ArduinoJson's per-TU serializer bloat in a network hot path.
void appendEscaped(std::string& out, const std::string& value) {
  out += '"';
  for (const unsigned char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void appendUInt(std::string& out, uint32_t value) {
  char buf[12];
  int n = snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(value));
  if (n > 0) out.append(buf, static_cast<size_t>(n));
}

void appendInt64(std::string& out, int64_t value) {
  char buf[24];
  int n = snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
  if (n > 0) out.append(buf, static_cast<size_t>(n));
}

struct ExportSink {
  std::string* out;
  bool overflow = false;
};

// Serializes one day (schema v2, matching the web export) into the sink. The
// server ingest RPC only requires days[].date, days[].books[].title/author/
// activeSeconds; the extra fields are ignored but kept for parity.
bool writeDay(void* ctx, const DailyReadingStatistics& day) {
  auto* sink = static_cast<ExportSink*>(ctx);
  std::string& out = *sink->out;
  if (out.size() > MAX_EXPORT_BYTES) {  // cheap pre-check before each book
    sink->overflow = true;
    return false;
  }
  out += "{\"date\":";
  appendEscaped(out, day.date);
  out += ",\"totalSeconds\":";
  appendUInt(out, day.totalSeconds);
  out += day.goalMet ? ",\"goalMet\":true,\"books\":[" : ",\"goalMet\":false,\"books\":[";
  bool first = true;
  for (const auto& book : day.books) {
    if (out.size() > MAX_EXPORT_BYTES) {
      sink->overflow = true;
      return false;
    }
    if (!first) out += ',';
    first = false;
    out += "{\"title\":";
    appendEscaped(out, book.title);
    out += ",\"author\":";
    appendEscaped(out, book.author);
    out += ",\"activeSeconds\":";
    appendUInt(out, book.activeSeconds);
    out += ",\"lastReadAt\":";
    appendInt64(out, book.lastReadAt);
    out += ",\"path\":";
    appendEscaped(out, book.path);
    out += ",\"coverBmpPath\":";
    appendEscaped(out, book.coverBmpPath);
    out += '}';
  }
  out += "]}";
  if (out.size() > MAX_EXPORT_BYTES) {
    sink->overflow = true;
    return false;
  }
  return true;
}

// "12 min ago" style, matching StatisticsActivity phrasing conventions without
// depending on it.
std::string formatAge(const int64_t seconds) {
  if (seconds < 60) return "just now";
  if (seconds < 3600) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld min", static_cast<long long>(seconds / 60));
    return buf;
  }
  if (seconds < 86400) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld h", static_cast<long long>(seconds / 3600));
    return buf;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld d", static_cast<long long>(seconds / 86400));
  return buf;
}

}  // namespace

CloudSyncService& CloudSyncService::getInstance() {
  static CloudSyncService instance;
  return instance;
}

bool CloudSyncService::isConfigured() const {
  return CLOUD_SYNC_STORE.isEnabled() && CloudSyncPolicy::validServerUrl(CLOUD_SYNC_STORE.getServerUrl()) &&
         CloudSyncPolicy::validToken(CLOUD_SYNC_STORE.getToken());
}

void CloudSyncService::markDataChanged() { dataChanged.store(true, std::memory_order_release); }

std::string CloudSyncService::getStatusLabel() const {
  const Status s = getStatus();
  switch (s) {
    case Status::Disabled:
      // Feature off, disabled at runtime, or config incomplete — one label for
      // all three; the settings screen shows what is missing.
      return std::string(tr(STR_CLOUD_SYNC_DISABLED));
    case Status::Syncing:
      return std::string(tr(STR_CLOUD_SYNC_SYNCING));
    case Status::Synced: {
      const int64_t last = CLOUD_SYNC_STORE.getLastSyncedAt();
      int64_t now = 0;
      if (last > 0 && StatisticsStore::getEpochSeconds(now)) {
        if (now - last >= 0 && now - last < 60) return std::string(tr(STR_CLOUD_SYNC_JUST_NOW));
        return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED)) + " " + formatAge(now - last);
      }
      return std::string(tr(STR_CLOUD_SYNC_JUST_NOW));
    }
    case Status::Failed:
      return std::string(tr(STR_CLOUD_SYNC_FAILED));
    case Status::Idle: {
      const int64_t last = CLOUD_SYNC_STORE.getLastSyncedAt();
      int64_t now = 0;
      if (last > 0 && StatisticsStore::getEpochSeconds(now) && now >= last) {
        return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED)) + " " + formatAge(now - last);
      }
      return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED)) + " " + tr(STR_CLOUD_SYNC_NEVER);
    }
  }
  return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED));
}

void CloudSyncService::taskTrampoline(void* param) {
  static_cast<CloudSyncService*>(param)->runLoop();
  vTaskDelete(nullptr);
}

void CloudSyncService::start() {
  static TaskHandle_t task = nullptr;
  if (task) return;
  if (!isConfigured()) {
    status.store(Status::Disabled, std::memory_order_release);
    return;
  }
  if (xTaskCreate(&taskTrampoline, "CloudSync", SYNC_TASK_STACK, this, 1, &task) != pdPASS) {
    LOG_ERR("CSYNC", "Failed to create cloud sync task");
    task = nullptr;
  }
}

bool CloudSyncService::trySyncIfDue() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!isConfigured()) {
    status.store(Status::Disabled, std::memory_order_release);
    return false;
  }

  const uint32_t now = millis();
  const bool dirty = dataChanged.exchange(false, std::memory_order_acq_rel);
  const bool failed = status.load(std::memory_order_acquire) == Status::Failed;
  if (!CloudSyncPolicy::due(now, lastAttemptMs, dirty, failed)) return false;

  status.store(Status::Syncing, std::memory_order_release);
  const bool ok = upload();
  lastAttemptMs = millis();
  // millis() can theoretically return 0 on the first attempt; lastAttemptMs==0
  // means "never attempted" in CloudSyncPolicy::due, which would make every
  // subsequent poll due immediately.
  if (lastAttemptMs == 0) lastAttemptMs = 1;
  status.store(ok ? Status::Synced : Status::Failed, std::memory_order_release);
  return true;
}

void CloudSyncService::runLoop() {
  // First pass immediately if the device woke due and a chain is waiting;
  // afterwards sleep in short polls and let the policy interval gate. When the
  // feature is off or unconfigured, back off to a slow poll so an idle device
  // is not woken every POLL_MS just to find nothing to do; config changes are
  // picked up on the next slow tick.
  constexpr uint32_t IDLE_POLL_MS = 60000;
  uint32_t nextPoll = millis();
  for (;;) {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextPoll) >= 0) {
      if (WiFi.status() == WL_CONNECTED && isConfigured()) {
        trySyncIfDue();
        nextPoll = millis() + POLL_MS;
      } else {
        status.store(Status::Disabled, std::memory_order_release);
        nextPoll = millis() + IDLE_POLL_MS;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

bool CloudSyncService::upload() {
  int64_t nowEpoch = 0;
  const bool clockValid = StatisticsStore::getEpochSeconds(nowEpoch);

  ExportSink sink;
  std::string body;
  body.reserve(4096);
  body += "{\"schemaVersion\":2,\"days\":[";
  sink.out = &body;
  const auto summary = StatisticsStore::getInstance().visitHistory(&sink, writeDay);
  if (sink.overflow || body.size() > MAX_EXPORT_BYTES) {
    LOG_ERR("CSYNC", "Statistics export exceeded size cap; skipping sync");
    return false;
  }
  body += "]";
  if (summary.clockAvailable) {
    body += ",\"clockAvailable\":true,\"today\":";
    appendEscaped(body, ReadingTime::dateString(summary.todayDayNumber));
    body += ",\"currentStreak\":";
    appendUInt(body, static_cast<uint32_t>(summary.currentStreak));
  } else {
    body += ",\"clockAvailable\":false,\"today\":null,\"currentStreak\":0";
  }
  body += '}';

  if (body.size() > MAX_EXPORT_BYTES) {
    LOG_ERR("CSYNC", "Statistics export exceeded size cap; skipping sync");
    return false;
  }
  LOG_DBG("CSYNC", "[MEM] Export %zu bytes, free=%d", body.size(), ESP.getFreeHeap());

  // Normalize the server URL: accept either an explicit https URL or a bare
  // hostname, then append the ingest path.
  std::string url = CLOUD_SYNC_STORE.getServerUrl();
  if (!url.starts_with("https://")) url = "https://" + url;
  if (url.ends_with('/')) url.pop_back();
  url += "/api/reader/ingest";

  freeink::SecureHttpClient http;
  http.setTimeout(60000);
  http.setFollowRedirects(0);  // never follow: a redirect could move the bearer token
  http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (!http.begin(url)) {
    LOG_ERR("CSYNC", "Bad server URL: %s", url.c_str());
    return false;
  }
  http.addHeader("Authorization", std::string("Bearer ") + CLOUD_SYNC_STORE.getToken());
  http.addHeader("Content-Type", "application/json");

  LOG_INF("CSYNC", "Uploading statistics to %s", url.c_str());
  const int status = http.POST(body);

  if (status < 0) {
    LOG_ERR("CSYNC", "Upload transport failed (status %d)", status);
    return false;
  }
  if (status != 200) {
    LOG_ERR("CSYNC", "Server returned HTTP %d", status);
    return false;
  }
  const std::string response = http.getString();
  if (response.find("\"ok\":true") == std::string::npos) {
    LOG_ERR("CSYNC", "Server did not confirm ingest: %s", response.c_str());
    return false;
  }
  http.end();
  // Without a valid RTC we have no trustworthy timestamp to persist; the sync
  // itself still counts as done, but "last synced" stays unset ("Never").
  if (clockValid && nowEpoch > 0) {
    CLOUD_SYNC_STORE.setLastSyncedAt(nowEpoch);
  }
  LOG_INF("CSYNC", "Statistics synced");
  return true;
}