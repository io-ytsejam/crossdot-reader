#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

#include "StatisticsStore.h"

// Streaming deserializer for the reading-statistics export document the device
// publishes over HTTP (see docs/statistics-export-api.md). It drives a
// StreamingJsonParser and emits one StatisticsImportBook each time a book object
// closes, so a large export can be fed chunk-by-chunk and merged as it arrives
// without ever buffering the whole document in RAM. Each emitted book carries
// the civil day it was parsed under.
//
// Accepts both the lossless schema v2 (with SD `path` and `coverBmpPath`, so
// imported books stay tied to on-device files) and the path-less v1 shape (the
// store keys those by title+author). `schemaVersion`, per-day totals, and
// streak fields are intentionally ignored.
class StatisticsImportParser {
 public:
  using BookSink = void (*)(void* ctx, const StatisticsImportBook& book);

  explicit StatisticsImportParser(BookSink sink, void* sinkCtx);
  StatisticsImportParser(const StatisticsImportParser&) = delete;
  StatisticsImportParser& operator=(const StatisticsImportParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  bool hasError() const { return error; }
  size_t bookCount() const { return booksParsed; }

 private:
  enum class Position : uint8_t {
    PRE_ROOT,
    ROOT,           // inside the top-level document object { ... }
    IN_DAYS_ARRAY,  // inside the "days" array
    IN_DAY_OBJECT,  // inside one day { ... }
    IN_BOOKS_ARRAY,
    IN_BOOK_OBJECT,
  };

  enum class Key : uint8_t {
    NONE,
    DAYS,
    DATE,
    BOOKS,
    TITLE,
    AUTHOR,
    ACTIVE_SECONDS,
    LAST_READ_AT,
    PATH,
    COVER_BMP_PATH,
  };

  // StreamingJsonParser already aggregates the string tokens; the constructors
  // are static trampolines into the matching instance methods below.
  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void onKey(const char* key, size_t len);
  void onString(const char* value, size_t len);
  void onNumber(const char* value, size_t len);
  void onObjectStart();
  void onObjectEnd();
  void onArrayStart();
  void onArrayEnd();
  void parseDate(const char* value, size_t len);
  void emitBook();

  StreamingJsonParser parser;
  BookSink sink;
  void* sinkCtx;

  Position position;
  Key currentKey;
  bool error;

  int64_t currentDayNumber;
  bool dayParsed;
  char currentDate[16];
  size_t booksParsed;

  StatisticsImportBook book;  // fields of the book currently being parsed
};