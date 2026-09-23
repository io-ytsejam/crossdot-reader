#pragma once

#include <WebServer.h>

#include <memory>

#include "StatisticsImportParser.h"
#include "StatisticsSessionAuth.h"

// Handles POST /api/statistics/import: the counterpart to the statistics export
// (GET /api/statistics). It reuses the same one-time pairing token
// (StatisticsSessionAuth), streams the raw JSON body straight into a
// StatisticsImportParser, and merges each parsed book into the reading-statistics
// store as its object closes — so a payload spanning many days merges in
// constant memory (no whole-document buffer), matching OtaUpdater's streaming
// pattern. Mirrors WebDAVHandler's use of the RequestHandler raw-upload
// interface to receive the body chunk-by-chunk.
class StatisticsImportHandler : public RequestHandler {
 public:
  explicit StatisticsImportHandler(const StatisticsSessionAuth& sessionAuth);

  bool canHandle(WebServer& server, HTTPMethod method, const String& uri) override;
  bool canRaw(WebServer& server, const String& uri) override;
  void raw(WebServer& server, const String& uri, HTTPRaw& raw) override;
  bool handle(WebServer& server, HTTPMethod method, const String& uri) override;

 private:
  void beginImport(WebServer& server);
  void finishImport(bool aborted);
  void respond(WebServer& server);

  const StatisticsSessionAuth& sessionAuth;
  std::unique_ptr<StatisticsImportParser> importer;  // allocated per request; null when idle
  bool authorized = false;
  bool ok = false;
  size_t booksMerged = 0;

  static void onBookSink(void* ctx, const StatisticsImportBook& book);
};