#include "CloudSyncService.h"

#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CloudSyncPolicy.h"
#include "CloudSyncResponse.h"
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
  bool firstDay = true;
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
  if (!sink->firstDay) out += ',';
  sink->firstDay = false;
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

// Precise elapsed-time string for the "how long ago" hint: "5 min", "2 h 15
// min", "1 d 3 h". Only called with seconds >= 60 — anything fresher is the
// dedicated "Synced just now" label.
std::string formatAge(const int64_t seconds) {
  if (seconds < 3600) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld min", static_cast<long long>(seconds / 60));
    return buf;
  }
  if (seconds < 86400) {
    const int64_t hours = seconds / 3600;
    const int64_t minutes = (seconds % 3600) / 60;
    if (minutes > 0) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%lld h %lld min", static_cast<long long>(hours), static_cast<long long>(minutes));
      return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld h", static_cast<long long>(hours));
    return buf;
  }
  const int64_t days = seconds / 86400;
  const int64_t hours = (seconds % 86400) / 3600;
  if (hours > 0) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%lld d %lld h", static_cast<long long>(days), static_cast<long long>(hours));
    return buf;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld d", static_cast<long long>(days));
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
  auto* service = static_cast<CloudSyncService*>(param);
  service->runLoop();
  service->completed.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void CloudSyncService::start(bool waitForCompletion) {
  static TaskHandle_t task = nullptr;
  if (task) return;
  if (!isConfigured()) {
    status.store(Status::Disabled, std::memory_order_release);
    // Loud, secret-free reason so a silent no-op never looks like a heisenbug.
    std::string cause;
    if (!CLOUD_SYNC_STORE.isEnabled()) {
      cause = "cloud sync is disabled";
    } else if (CLOUD_SYNC_STORE.getServerUrl().empty() ||
               !CloudSyncPolicy::validServerUrl(CLOUD_SYNC_STORE.getServerUrl())) {
      cause = "server URL missing or invalid";
    } else if (CLOUD_SYNC_STORE.getToken().empty() || !CloudSyncPolicy::validToken(CLOUD_SYNC_STORE.getToken())) {
      cause = "device token missing or invalid";
    }
    LOG_ERR("CSYNC", "Boot sync not started: %s", cause.empty() ? "not configured" : cause.c_str());
    return;
  }
  completed.store(false, std::memory_order_release);
  if (xTaskCreate(&taskTrampoline, "CloudSync", SYNC_TASK_STACK, this, 1, &task) != pdPASS) {
    LOG_ERR("CSYNC", "Failed to create cloud sync task");
    status.store(Status::Failed, std::memory_order_release);
    completed.store(true, std::memory_order_release);
    task = nullptr;
  }
  while (waitForCompletion && !completed.load(std::memory_order_acquire)) {
    delay(10);  // Yield to Wi-Fi, TLS and the idle task; no UI allocations yet.
  }
}

void CloudSyncService::runLoop() {
  // Boot-only sync: wait (bounded) for Wi-Fi the device joins at boot, then
  // perform exactly one upload and stop. No periodic polling or in-session
  // retries — that is the point of the feature (easy on hardware and battery).
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (!isConfigured()) {
      status.store(Status::Disabled, std::memory_order_release);
      return;
    }
    if (static_cast<int32_t>(millis() - start) >= static_cast<int32_t>(BOOT_WIFI_TIMEOUT_MS)) {
      LOG_ERR("CSYNC", "No Wi-Fi within the boot window; skipping sync this session");
      status.store(Status::Failed, std::memory_order_release);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  status.store(Status::Syncing, std::memory_order_release);
  const bool ok = upload();
  LOG_INF("CSYNC", "Heap after upload: free=%u min=%u largest=%u stackRemaining=%u", ESP.getFreeHeap(),
          ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr));
  status.store(ok ? Status::Synced : Status::Failed, std::memory_order_release);
  LOG_INF("CSYNC", "Boot sync finished: %s", ok ? "ok" : "failed");
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

  // perform() consumes the response while dispatching ON_DATA. Allocate the
  // bounded collector once on the heap; 4 KiB would exhaust this task's stack.
  auto response = makeUniqueNoThrow<CloudSyncResponse>();
  if (!response) {
    LOG_ERR("CSYNC", "Response collector allocation failed");
    return false;
  }
  esp_http_client_config_t config = {};
  config.user_data = response.get();
  config.event_handler = [](esp_http_client_event_t* event) -> esp_err_t {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
      // ON_DATA also carries decoded chunked bodies; do not skip those events.
      static_cast<CloudSyncResponse*>(event->user_data)
          ->append(static_cast<const char*>(event->data), static_cast<size_t>(event->data_len));
    }
    return ESP_OK;
  };
  config.url = url.c_str();
  config.timeout_ms = 60000;
  // Verify TLS against the bundled CA roots (this build has esp-tls
  // CONFIG_ESP_TLS_INSECURE off, so an unverified handshake can't even be set
  // up). This is the same verified path HttpDownloader uses.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  // Never auto-follow a redirect: a 30x could move the bearer credential to a
  // different host. A redirect therefore surfaces as a non-200 failure.
  config.disable_auto_redirect = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("CSYNC", "HTTP client init failed");
    return false;
  }
  const std::string bearer = std::string("Bearer ") + CLOUD_SYNC_STORE.getToken();
  esp_http_client_set_header(client, "Authorization", bearer.c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));

  LOG_INF("CSYNC", "Uploading statistics to %s", url.c_str());
  const esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    LOG_ERR("CSYNC", "Upload transport failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }
  const int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    LOG_ERR("CSYNC", "Server returned HTTP %d", status);
    esp_http_client_cleanup(client);
    return false;
  }
  esp_http_client_cleanup(client);
  if (!response->confirmsIngest()) {
    // Never log an untrusted response body; it may echo credentials.
    LOG_ERR("CSYNC", "Server confirmation missing, invalid or oversized");
    return false;
  }
  // Without a valid RTC we have no trustworthy timestamp to persist; the sync
  // itself still counts as done, but "last synced" stays unset ("Never").
  if (clockValid && nowEpoch > 0) {
    CLOUD_SYNC_STORE.setLastSyncedAt(nowEpoch);
  }
  LOG_INF("CSYNC", "Statistics synced");
  return true;
}