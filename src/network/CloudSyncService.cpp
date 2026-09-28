#include "CloudSyncService.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
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
constexpr int SYNC_TASK_STACK = 8192;  // HTTPS + JSON + per-book Epub metadata load
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

// Per-book values probed from the reader's on-SD cache at export time. A
// negative progressPercent means no readable position; a non-positive
// totalPages means no page-count estimate; an empty isbn means the EPUB carried
// no ISBN identifier. Each may be absent per book.
struct ExportBookStats {
  int progressPercent = -1;
  int totalPages = -1;
  std::string isbn;
};

struct ExportSink {
  std::string* out;
  bool overflow = false;
  bool firstDay = true;
  // Optional probe supplying a book's whole-book reading percent (0..100) and
  // page-count estimate. The host export tests leave it null, in which case no
  // progress/pages fields are emitted. Results are memoized per path: the same
  // book appears once per day it was read, and each probe opens the book's cache
  // on the SD card.
  void (*readBookStats)(const std::string& path, ExportBookStats& stats) = nullptr;
  std::string lastProbedPath;
  ExportBookStats lastProbedStats;
};

// Serializes one day (schema v3) into the sink. The server ingest RPC requires
// days[].date, days[].books[].title/author/activeSeconds; v3 adds the optional
// per-book "progress" percent and "pages" estimate when the book's cache
// carries a position.
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
    if (sink->readBookStats) {
      if (book.path.empty() || book.path != sink->lastProbedPath) {
        sink->readBookStats(book.path, sink->lastProbedStats);
        sink->lastProbedPath = book.path;
      }
      const ExportBookStats& stats = sink->lastProbedStats;
      if (!stats.isbn.empty()) {
        out += ",\"isbn\":";
        appendEscaped(out, stats.isbn);
      }
      if (stats.progressPercent >= 0) {
        out += ",\"progress\":";
        appendInt64(out, stats.progressPercent);
      }
      if (stats.totalPages > 0) {
        out += ",\"pages\":";
        appendInt64(out, stats.totalPages);
      }
    }
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

// Device-side probe for the export. Fills the whole-book reading percent
// (0..100) and a whole-book page estimate for an EPUB from its on-SD cache, or
// leaves both negative when the book has nothing to report: never opened, cache
// cleared, or a non-EPUB format (TXT/XTC keep progress in their own formats and
// are not exported yet).
//
// The EPUB reader paginates one spine item at a time, so it has no exact
// whole-book page count. The estimate scales the current chapter's page count by
// the book's byte size over the chapter's byte size — the same byte-weighted
// density the reader already uses for whole-book progress. It is layout
// dependent (font, margins, orientation), so it is only an estimate.
// Normalizes a dc:identifier to a canonical ISBN (digits only; an ISBN-10 check
// digit may be X), or returns an empty string when it carries no ISBN.
std::string normalizeIsbn(const std::string& raw) {
  std::string s;
  s.reserve(raw.size());
  for (const char c : raw) {
    if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  const std::string prefix = "urn:isbn:";
  if (s.rfind(prefix, 0) == 0) s.erase(0, prefix.size());
  if (s.size() != 10 && s.size() != 13) return {};
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c >= '0' && c <= '9') continue;
    if (i == 9 && s.size() == 10 && c == 'x') continue;  // ISBN-10 check digit
    return {};
  }
  return s;
}

// Device-side probe for the export. Fills the EPUB's ISBN (when its metadata
// carries one), the whole-book reading percent (0..100) and a whole-book page
// estimate, leaving them empty/negative when unavailable: a non-EPUB format
// (TXT/XTC keep progress in their own formats and are not exported yet), a book
// without a metadata cache, or no saved position.
//
// The EPUB reader paginates one spine item at a time, so it has no exact
// whole-book page count. The estimate scales the current chapter's page count by
// the book's byte size over the chapter's byte size — the same byte-weighted
// density the reader already uses for whole-book progress. It is layout
// dependent (font, margins, orientation), so it is only an estimate.
void readEpubBookStats(const std::string& path, ExportBookStats& stats) {
  stats = ExportBookStats{};
  if (!FsHelpers::hasEpubExtension(path)) {
    return;
  }
  Epub epub(path, "/.crosspoint");

  // The identifier and spine sizes come from the book's metadata cache. Loading
  // without buildIfMissing keeps this read-only: a book without a cache simply
  // has nothing to export. runLoop's TLS phase never overlaps these SD reads.
  if (!epub.load(false, true)) {
    return;
  }
  stats.isbn = normalizeIsbn(epub.getIdentifier());

  // progress.bin is written by the reader on every page turn (6 bytes: spine
  // index, section page, section page count — little-endian u16s).
  uint8_t data[6];
  int read = 0;
  {
    HalFile file;
    if (!Storage.openFileForRead("CSYNC", epub.getCachePath() + "/progress.bin", file)) {
      return;
    }
    read = file.read(data, sizeof(data));
  }
  if (read != static_cast<int>(sizeof(data))) {
    return;
  }
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t pageCount = 0;
  memcpy(&spine, data + 0, sizeof(spine));
  memcpy(&page, data + 2, sizeof(page));
  memcpy(&pageCount, data + 4, sizeof(pageCount));
  if (pageCount == 0 || spine >= epub.getSpineItemsCount()) {
    return;
  }

  const float fraction = pageCount > 1 ? std::min(page / static_cast<float>(pageCount - 1), 1.0f) : 0.5f;
  const float percent = epub.calculateProgress(spine, fraction) * 100.0f;
  if (!(percent >= 0.0f && percent <= 100.0f)) {  // also rejects NaN
    return;
  }
  stats.progressPercent = static_cast<int>(percent + 0.5f);

  // Whole-book page estimate from the current chapter's page density.
  const size_t bookSize = epub.getBookSize();
  if (bookSize == 0) {
    return;
  }
  const size_t chapterStart = spine >= 1 ? epub.getCumulativeSpineItemSize(spine - 1) : 0;
  const size_t chapterEnd = epub.getCumulativeSpineItemSize(spine);
  if (chapterEnd <= chapterStart) {
    return;
  }
  const double pagesPerByte = static_cast<double>(pageCount) / static_cast<double>(chapterEnd - chapterStart);
  const int totalPages = static_cast<int>(static_cast<double>(bookSize) * pagesPerByte + 0.5);
  if (totalPages > 0) {
    stats.totalPages = totalPages;
  }
}

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
  const int64_t last = CLOUD_SYNC_STORE.getLastSyncedAt();
  int64_t now = 0;
  const bool haveClock = StatisticsStore::getEpochSeconds(now);

  switch (s) {
    case Status::Syncing:
      return std::string(tr(STR_CLOUD_SYNC_SYNCING));
    case Status::Failed:
      return std::string(tr(STR_CLOUD_SYNC_FAILED));
    case Status::Synced:
      if (last > 0 && haveClock) {
        if (now - last >= 0 && now - last < 60) return std::string(tr(STR_CLOUD_SYNC_JUST_NOW));
        return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED)) + " " + formatAge(now - last);
      }
      return std::string(tr(STR_CLOUD_SYNC_JUST_NOW));
    case Status::Idle:
    case Status::Disabled:
      // Before the first attempt of a boot, the status atomic still holds its
      // initial Disabled value even when the feature is fully configured, so
      // never report "off" from it. Configuration and the last successful
      // upload time are the only honest sources here.
      break;
  }

  const auto display = CloudSyncPolicy::resolveStatusDisplay(isConfigured(), last);
  if (display.disabled) return std::string(tr(STR_CLOUD_SYNC_DISABLED));
  if (last > 0 && haveClock && now >= last) {
    return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED)) + " " + formatAge(now - last);
  }
  return std::string(tr(STR_CLOUD_SYNC_LAST_SYNCED)) + " " + tr(STR_CLOUD_SYNC_NEVER);
}

void CloudSyncService::taskTrampoline(void* param) {
  auto* service = static_cast<CloudSyncService*>(param);
  service->runLoop();
  service->completed.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void CloudSyncService::notifyWifiDisconnected(uint8_t reason) {
  if (CloudSyncPolicy::terminalWifiJoinFailure(reason)) {
    wifiJoinFailed.store(true, std::memory_order_release);
  }
}

void CloudSyncService::prepareWifiJoin() {
  wifiJoinFailed.store(false, std::memory_order_release);
  wifiJoinStartedAt.store(millis(), std::memory_order_release);
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
    LOG_ERR("CSYNC", "Reader sync not started: %s", cause.empty() ? "not configured" : cause.c_str());
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
  // Reader-idle sync normally starts with Wi-Fi already connected. Keep the
  // bounded wait as a defensive guard against a disconnect between scheduling
  // the worker and entering this task.
  uint32_t start = wifiJoinStartedAt.load(std::memory_order_acquire);
  if (start == 0) start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (!isConfigured()) {
      status.store(Status::Disabled, std::memory_order_release);
      return;
    }
    if (wifiJoinFailed.load(std::memory_order_acquire)) {
      LOG_ERR("CSYNC", "Saved Wi-Fi unavailable; skipping sync this session");
      status.store(Status::Failed, std::memory_order_release);
      return;
    }
    if (static_cast<int32_t>(millis() - start) >= static_cast<int32_t>(CloudSyncPolicy::BOOT_WIFI_TIMEOUT_MS)) {
      LOG_ERR("CSYNC", "No Wi-Fi within the join window; skipping sync this session");
      status.store(Status::Failed, std::memory_order_release);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  status.store(Status::Syncing, std::memory_order_release);
  const bool ok = upload();
  LOG_INF("CSYNC", "Heap after upload: free=%u min=%u largest=%u stackRemaining=%u", ESP.getFreeHeap(),
          ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), uxTaskGetStackHighWaterMark(nullptr));
  status.store(ok ? Status::Synced : Status::Failed, std::memory_order_release);
  LOG_INF("CSYNC", "Reader sync finished: %s", ok ? "ok" : "failed");
}

bool CloudSyncService::upload() {
  int64_t nowEpoch = 0;
  const bool clockValid = StatisticsStore::getEpochSeconds(nowEpoch);

  ExportSink sink;
  std::string body;
  body.reserve(4096);
  body += "{\"schemaVersion\":3,\"days\":[";
  sink.out = &body;
  sink.readBookStats = &readEpubBookStats;
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
  config.timeout_ms = CloudSyncPolicy::HTTP_TIMEOUT_MS;
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