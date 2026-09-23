#include "StatisticsImportParser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Copy a possibly non-null-terminated token into a fixed buffer, capping at the
// buffer size and always null-terminating. The streaming parser passes raw
// token spans, which are not guaranteed to be terminated.
void safeCopy(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

StatisticsImportParser::StatisticsImportParser(const StatisticsImportParser::BookSink sink, void* const sinkCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      sink(sink),
      sinkCtx(sinkCtx) {
  reset();
}

void StatisticsImportParser::reset() {
  parser.reset();
  position = Position::PRE_ROOT;
  currentKey = Key::NONE;
  error = false;
  currentDayNumber = 0;
  dayParsed = false;
  currentDate[0] = '\0';
  booksParsed = 0;
  book = StatisticsImportBook{};
}

void StatisticsImportParser::feed(const char* data, const size_t len) { parser.feed(data, len); }

void StatisticsImportParser::onKey(const char* key, const size_t len) {
  switch (position) {
    case Position::ROOT:
      currentKey = (len == 4 && memcmp(key, "days", 4) == 0) ? Key::DAYS : Key::NONE;
      break;
    case Position::IN_DAY_OBJECT:
      if (len == 4 && memcmp(key, "date", 4) == 0)
        currentKey = Key::DATE;
      else if (len == 5 && memcmp(key, "books", 5) == 0)
        currentKey = Key::BOOKS;
      else
        currentKey = Key::NONE;
      break;
    case Position::IN_BOOK_OBJECT:
      if (len == 5 && memcmp(key, "title", 5) == 0)
        currentKey = Key::TITLE;
      else if (len == 6 && memcmp(key, "author", 6) == 0)
        currentKey = Key::AUTHOR;
      else if (len == 13 && memcmp(key, "activeSeconds", 13) == 0)
        currentKey = Key::ACTIVE_SECONDS;
      else if (len == 10 && memcmp(key, "lastReadAt", 10) == 0)
        currentKey = Key::LAST_READ_AT;
      else if (len == 4 && memcmp(key, "path", 4) == 0)
        currentKey = Key::PATH;
      else if (len == 12 && memcmp(key, "coverBmpPath", 12) == 0)
        currentKey = Key::COVER_BMP_PATH;
      else
        currentKey = Key::NONE;
      break;
    default:
      currentKey = Key::NONE;
      break;
  }
}

void StatisticsImportParser::onArrayStart() {
  if (position == Position::ROOT && currentKey == Key::DAYS) {
    position = Position::IN_DAYS_ARRAY;
  } else if (position == Position::IN_DAY_OBJECT && currentKey == Key::BOOKS) {
    position = Position::IN_BOOKS_ARRAY;
  }
  currentKey = Key::NONE;
}

void StatisticsImportParser::onObjectStart() {
  switch (position) {
    case Position::PRE_ROOT:
      position = Position::ROOT;
      break;
    case Position::IN_DAYS_ARRAY:
      position = Position::IN_DAY_OBJECT;
      dayParsed = false;
      currentDayNumber = 0;
      currentDate[0] = '\0';
      break;
    case Position::IN_BOOKS_ARRAY:
      position = Position::IN_BOOK_OBJECT;
      book = StatisticsImportBook{};
      break;
    default:
      break;
  }
  currentKey = Key::NONE;
}

void StatisticsImportParser::onObjectEnd() {
  switch (position) {
    case Position::IN_BOOK_OBJECT:
      emitBook();
      position = Position::IN_BOOKS_ARRAY;
      break;
    case Position::IN_DAY_OBJECT:
      position = Position::IN_DAYS_ARRAY;
      break;
    case Position::ROOT:
      position = Position::PRE_ROOT;
      break;
    default:
      break;
  }
  currentKey = Key::NONE;
}

void StatisticsImportParser::onArrayEnd() {
  switch (position) {
    case Position::IN_BOOKS_ARRAY:
      position = Position::IN_DAY_OBJECT;
      break;
    case Position::IN_DAYS_ARRAY:
      position = Position::ROOT;
      break;
    default:
      break;
  }
  currentKey = Key::NONE;
}

void StatisticsImportParser::onString(const char* value, const size_t len) {
  switch (currentKey) {
    case Key::DATE:
      if (position == Position::IN_DAY_OBJECT) parseDate(value, len);
      break;
    case Key::TITLE:
      safeCopy(book.title, sizeof(book.title), value, len);
      break;
    case Key::AUTHOR:
      safeCopy(book.author, sizeof(book.author), value, len);
      break;
    case Key::PATH:
      safeCopy(book.path, sizeof(book.path), value, len);
      break;
    case Key::COVER_BMP_PATH:
      safeCopy(book.coverBmpPath, sizeof(book.coverBmpPath), value, len);
      break;
    default:
      break;
  }
  currentKey = Key::NONE;
}

void StatisticsImportParser::onNumber(const char* value, const size_t len) {
  if (position == Position::IN_BOOK_OBJECT) {
    char buf[24];
    const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, value, n);
    buf[n] = '\0';
    if (currentKey == Key::ACTIVE_SECONDS) {
      const long long parsed = strtoll(buf, nullptr, 10);
      if (parsed < 0) {
        error = true;
      } else {
        book.activeSeconds = static_cast<uint32_t>(parsed);
      }
    } else if (currentKey == Key::LAST_READ_AT) {
      book.lastReadAt = strtoll(buf, nullptr, 10);
    }
  }
  currentKey = Key::NONE;
}

void StatisticsImportParser::parseDate(const char* value, const size_t len) {
  if (len < 10) {
    error = true;
    return;
  }
  char buf[16];
  const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, value, n);
  buf[n] = '\0';

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (sscanf(buf, "%4d-%2u-%2u", &year, &month, &day) != 3) {
    error = true;
    return;
  }
  if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) {
    error = true;
    return;
  }
  currentDayNumber = ReadingTime::daysFromCivil(year, month, day);
  dayParsed = true;
  safeCopy(currentDate, sizeof(currentDate), buf, n);
}

void StatisticsImportParser::emitBook() {
  if (!dayParsed) {
    error = true;
    return;
  }
  book.dayNumber = currentDayNumber;
  safeCopy(book.date, sizeof(book.date), currentDate, strlen(currentDate));
  ++booksParsed;
  if (sink) sink(sinkCtx, book);
}

// -- SAX callback trampolines ------------------------------------------------

void StatisticsImportParser::sOnKey(void* ctx, const char* key, const size_t len) {
  static_cast<StatisticsImportParser*>(ctx)->onKey(key, len);
}

void StatisticsImportParser::sOnString(void* ctx, const char* value, const size_t len) {
  static_cast<StatisticsImportParser*>(ctx)->onString(value, len);
}

void StatisticsImportParser::sOnNumber(void* ctx, const char* value, const size_t len) {
  static_cast<StatisticsImportParser*>(ctx)->onNumber(value, len);
}

void StatisticsImportParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<StatisticsImportParser*>(ctx)->currentKey = Key::NONE;
}

void StatisticsImportParser::sOnNull(void* ctx) { static_cast<StatisticsImportParser*>(ctx)->currentKey = Key::NONE; }

void StatisticsImportParser::sOnObjectStart(void* ctx) { static_cast<StatisticsImportParser*>(ctx)->onObjectStart(); }

void StatisticsImportParser::sOnObjectEnd(void* ctx) { static_cast<StatisticsImportParser*>(ctx)->onObjectEnd(); }

void StatisticsImportParser::sOnArrayStart(void* ctx) { static_cast<StatisticsImportParser*>(ctx)->onArrayStart(); }

void StatisticsImportParser::sOnArrayEnd(void* ctx) { static_cast<StatisticsImportParser*>(ctx)->onArrayEnd(); }