#include "StatisticsImportHandler.h"

#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <string_view>

#include "StatisticsStore.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr size_t MAX_IMPORT_BYTES = 512 * 1024;  // guard against pathological payloads

}

StatisticsImportHandler::StatisticsImportHandler(const StatisticsSessionAuth& sessionAuth) : sessionAuth(sessionAuth) {}

bool StatisticsImportHandler::canHandle(WebServer& server, HTTPMethod method, const String& uri) {
  (void)server;
  return method == HTTP_POST && uri == "/api/statistics/import";
}

bool StatisticsImportHandler::canRaw(WebServer& server, const String& uri) {
  (void)server;
  return uri == "/api/statistics/import";
}

void StatisticsImportHandler::raw(WebServer& server, const String& uri, HTTPRaw& raw) {
  (void)uri;
  switch (raw.status) {
    case RAW_START:
      beginImport(server);
      break;
    case RAW_WRITE: {
      resetTaskWatchdogIfSubscribed();
      if (!ok || !importer) return;
      importer->feed(reinterpret_cast<const char*>(raw.buf), raw.currentSize);
      if (importer->hasError()) {
        LOG_ERR("WIMP", "Statistics import JSON invalid");
        ok = false;
      }
      break;
    }
    case RAW_END:
      finishImport(false);
      break;
    case RAW_ABORTED:
      finishImport(true);
      break;
  }
}

bool StatisticsImportHandler::handle(WebServer& server, HTTPMethod method, const String& uri) {
  (void)method;
  (void)uri;
  respond(server);
  return true;
}

void StatisticsImportHandler::beginImport(WebServer& server) {
  resetTaskWatchdogIfSubscribed();
  booksMerged = 0;

  const String authorization = server.header("Authorization");
  authorized = sessionAuth.authorize(std::string_view(authorization.c_str(), authorization.length()));
  ok = authorized;
  if (!authorized) {
    LOG_DBG("WIMP", "Statistics import rejected: unauthenticated");
    return;
  }

  // Reject oversized bodies before accepting the stream; the parser merges as it
  // feeds, so anything past this cap is not worth the SD I/O and CPU.
  const int contentLength = server.clientContentLength();
  if (contentLength > static_cast<int>(MAX_IMPORT_BYTES)) {
    LOG_ERR("WIMP", "Statistics import too large: %d bytes", contentLength);
    ok = false;
    return;
  }

  importer = makeUniqueNoThrow<StatisticsImportParser>(&StatisticsImportHandler::onBookSink, this);
  if (!importer) {
    LOG_ERR("WIMP", "OOM: statistics import parser");
    ok = false;
    return;
  }
  LOG_DBG("WIMP", "Statistics import started");
}

void StatisticsImportHandler::finishImport(bool aborted) {
  if (importer && !aborted && ok && importer->hasError()) {
    LOG_ERR("WIMP", "Statistics import JSON invalid");
    ok = false;
  }
  importer.reset();
  if (aborted) {
    ok = false;
    LOG_DBG("WIMP", "Statistics import aborted");
  } else if (ok) {
    LOG_INF("WIMP", "Statistics import complete: %u books merged", static_cast<unsigned>(booksMerged));
  }
}

void StatisticsImportHandler::respond(WebServer& server) {
  server.sendHeader("Cache-Control", "no-store");
  if (!authorized) {
    server.sendHeader("WWW-Authenticate", "Bearer");
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!ok) {
    server.send(400, "application/json", "{\"error\":\"invalid statistics document\"}");
    return;
  }
  char response[64];
  const int length =
      snprintf(response, sizeof(response), "{\"ok\":true,\"booksImported\":%u}", static_cast<unsigned>(booksMerged));
  server.send(200, "application/json", response);
  (void)length;
}

void StatisticsImportHandler::onBookSink(void* ctx, const StatisticsImportBook& book) {
  auto* self = static_cast<StatisticsImportHandler*>(ctx);
  if (book.activeSeconds == 0) return;  // nothing to record; the store treats 0 as a no-op
  // Each merge reads and rewrites the day file, so it can exceed the HTTP read
  // window; keep the watchdog fed on the same cadence as the export writer.
  resetTaskWatchdogIfSubscribed();
  if (!READING_STATISTICS.mergeImportedBook(book)) {
    LOG_ERR("WIMP", "Failed to persist imported statistics for %s", book.date);
    self->ok = false;
    return;
  }
  ++self->booksMerged;
}