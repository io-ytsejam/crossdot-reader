#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "src/ReadingTime.h"
#include "src/StatisticsImportParser.h"

namespace {

struct SinkContext {
  std::vector<StatisticsImportBook> books;
};

void collectBook(void* ctx, const StatisticsImportBook& book) { static_cast<SinkContext*>(ctx)->books.push_back(book); }

std::vector<StatisticsImportBook> parseDocument(const char* json) {
  SinkContext ctx;
  StatisticsImportParser parser(collectBook, &ctx);
  parser.feed(json, strlen(json));
  return ctx.books;
}

// A representative lossless (schema v2) export spanning two days.
constexpr const char* kSchemaV2 =
    "{"
    "\"schemaVersion\":2,\"days\":["
    "{"
    "\"date\":\"2026-09-21\",\"totalSeconds\":1820,\"goalMet\":true,\"books\":["
    "{"
    "\"title\":\"Example Book\",\"author\":\"Example Author\",\"activeSeconds\":1820,"
    "\"lastReadAt\":1789945200,\"path\":\"/books/example.epub\","
    "\"coverBmpPath\":\"/books/example.bmp\""
    "}"
    "]"
    "},"
    "{"
    "\"date\":\"2026-09-20\",\"totalSeconds\":600,\"goalMet\":true,\"books\":["
    "{"
    "\"title\":\"Alpha\",\"author\":\"A\",\"activeSeconds\":300,"
    "\"lastReadAt\":1789858800,\"path\":\"/books/alpha.epub\",\"coverBmpPath\":\"\""
    "},"
    "{"
    "\"title\":\"Beta\",\"author\":\"B\",\"activeSeconds\":300,"
    "\"lastReadAt\":1789859100,\"path\":\"/books/beta.epub\","
    "\"coverBmpPath\":\"/books/beta.bmp\""
    "}"
    "]"
    "}"
    "],"
    "\"clockAvailable\":true,\"today\":\"2026-09-21\",\"currentStreak\":2"
    "}";

TEST(StatisticsImportParser, EmitsBooksWithPathAndDayContext) {
  const auto books = parseDocument(kSchemaV2);

  ASSERT_EQ(books.size(), 3u);

  const int64_t day21 = ReadingTime::daysFromCivil(2026, 9, 21);
  const int64_t day20 = ReadingTime::daysFromCivil(2026, 9, 20);

  // Day 2026-09-21, first book.
  EXPECT_EQ(books[0].dayNumber, day21);
  EXPECT_STREQ(books[0].date, "2026-09-21");
  EXPECT_STREQ(books[0].title, "Example Book");
  EXPECT_STREQ(books[0].author, "Example Author");
  EXPECT_EQ(books[0].activeSeconds, 1820u);
  EXPECT_EQ(books[0].lastReadAt, 1789945200);
  EXPECT_STREQ(books[0].path, "/books/example.epub");
  EXPECT_STREQ(books[0].coverBmpPath, "/books/example.bmp");

  // Day 2026-09-20, two books share the day context.
  EXPECT_EQ(books[1].dayNumber, day20);
  EXPECT_STREQ(books[1].date, "2026-09-20");
  EXPECT_STREQ(books[1].title, "Alpha");
  EXPECT_EQ(books[1].activeSeconds, 300u);
  EXPECT_STREQ(books[1].path, "/books/alpha.epub");
  EXPECT_STREQ(books[1].coverBmpPath, "");

  EXPECT_EQ(books[2].dayNumber, day20);
  EXPECT_STREQ(books[2].title, "Beta");
  EXPECT_EQ(books[2].activeSeconds, 300u);
  EXPECT_EQ(books[2].lastReadAt, 1789859100);
  EXPECT_STREQ(books[2].path, "/books/beta.epub");
  EXPECT_STREQ(books[2].coverBmpPath, "/books/beta.bmp");
}

TEST(StatisticsImportParser, PathlessV1ShapeEmitsEmptyPath) {
  const char* v1 =
      "{"
      "\"schemaVersion\":1,\"days\":["
      "{\"date\":\"2026-09-21\",\"totalSeconds\":900,\"goalMet\":true,\"books\":["
      "{\"title\":\"Old Book\",\"author\":\"Old Author\",\"activeSeconds\":900,\"lastReadAt\":1700000000}"
      "]"
      "}"
      "],"
      "\"clockAvailable\":true,\"today\":\"2026-09-21\",\"currentStreak\":1"
      "}";

  const auto books = parseDocument(v1);

  ASSERT_EQ(books.size(), 1u);
  EXPECT_STREQ(books[0].title, "Old Book");
  EXPECT_EQ(books[0].activeSeconds, 900u);
  EXPECT_EQ(books[0].lastReadAt, 1700000000);
  // No path/cover fields in v1: the store keys these by title+author.
  EXPECT_STREQ(books[0].path, "");
  EXPECT_STREQ(books[0].coverBmpPath, "");
}

TEST(StatisticsImportParser, ChunkedFeedingMatchesWholeDocument) {
  SinkContext chunkedCtx;
  StatisticsImportParser parser(collectBook, &chunkedCtx);
  const size_t len = strlen(kSchemaV2);
  for (size_t i = 0; i < len; i += 3) {
    const size_t remaining = len - i;
    parser.feed(kSchemaV2 + i, remaining < 3 ? remaining : 3);
  }
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(parser.bookCount(), 3u);

  const auto whole = parseDocument(kSchemaV2);
  ASSERT_EQ(chunkedCtx.books.size(), whole.size());
  for (size_t i = 0; i < whole.size(); ++i) {
    EXPECT_EQ(chunkedCtx.books[i].dayNumber, whole[i].dayNumber);
    EXPECT_STREQ(chunkedCtx.books[i].date, whole[i].date);
    EXPECT_STREQ(chunkedCtx.books[i].title, whole[i].title);
    EXPECT_STREQ(chunkedCtx.books[i].path, whole[i].path);
    EXPECT_EQ(chunkedCtx.books[i].activeSeconds, whole[i].activeSeconds);
    EXPECT_EQ(chunkedCtx.books[i].lastReadAt, whole[i].lastReadAt);
  }
}

TEST(StatisticsImportParser, BytewiseFeedingMatchesWholeDocument) {
  SinkContext bytewiseCtx;
  StatisticsImportParser parser(collectBook, &bytewiseCtx);
  const size_t len = strlen(kSchemaV2);
  for (size_t i = 0; i < len; ++i) {
    parser.feed(kSchemaV2 + i, 1);
  }
  EXPECT_FALSE(parser.hasError());
  ASSERT_EQ(bytewiseCtx.books.size(), 3u);
  EXPECT_STREQ(bytewiseCtx.books[2].title, "Beta");
}

TEST(StatisticsImportParser, EmptyDaysArrayEmitsNothing) {
  const auto books = parseDocument("{\"schemaVersion\":2,\"days\":[],\"clockAvailable\":true}");
  EXPECT_TRUE(books.empty());
}

TEST(StatisticsImportParser, DayWithoutDateFlagsErrorAndSkips) {
  // A book object under a day that never declared a date cannot be placed.
  const char* json =
      "{\"days\":["
      "{\"totalSeconds\":900,\"books\":[{\"title\":\"Orphan\",\"activeSeconds\":900,\"lastReadAt\":1}]}"
      "]}";
  SinkContext ctx;
  StatisticsImportParser parser(collectBook, &ctx);
  parser.feed(json, strlen(json));
  EXPECT_TRUE(parser.hasError());
  EXPECT_TRUE(ctx.books.empty());
}

TEST(StatisticsImportParser, NegativeSecondsFlagsError) {
  const char* json =
      "{\"days\":["
      "{\"date\":\"2026-09-21\",\"books\":[{\"title\":\"N\",\"activeSeconds\":-5,\"lastReadAt\":0}]}"
      "]}";
  SinkContext ctx;
  StatisticsImportParser parser(collectBook, &ctx);
  parser.feed(json, strlen(json));
  // Corrupt negative time flags the document (client gets a 400 at the end);
  // the record is still emitted as a zero-seconds no-op the store ignores.
  EXPECT_TRUE(parser.hasError());
  ASSERT_EQ(ctx.books.size(), 1u);
  EXPECT_EQ(ctx.books[0].activeSeconds, 0u);
}

TEST(StatisticsImportParser, InvalidDateFlagsError) {
  const char* json =
      "{\"days\":["
      "{\"date\":\"2026-13-99\",\"books\":[{\"title\":\"N\",\"activeSeconds\":5,\"lastReadAt\":0}]}"
      "]}";
  SinkContext ctx;
  StatisticsImportParser parser(collectBook, &ctx);
  parser.feed(json, strlen(json));
  EXPECT_TRUE(parser.hasError());
}

TEST(StatisticsImportParser, ResetClearsCountersAndState) {
  SinkContext ctx;
  StatisticsImportParser parser(collectBook, &ctx);

  parser.feed(kSchemaV2, strlen(kSchemaV2));
  EXPECT_EQ(parser.bookCount(), 3u);
  EXPECT_FALSE(parser.hasError());

  ctx.books.clear();
  parser.reset();
  EXPECT_EQ(parser.bookCount(), 0u);
  parser.feed(kSchemaV2, strlen(kSchemaV2));
  EXPECT_EQ(parser.bookCount(), 3u);
  EXPECT_EQ(ctx.books.size(), 3u);
}

TEST(StatisticsImportParser, ZeroSecondsStillEmittedForStoreToDecide) {
  const char* json =
      "{\"days\":["
      "{\"date\":\"2026-09-20\",\"books\":[{\"title\":\"Zero\",\"activeSeconds\":0,\"lastReadAt\":5}]}"
      "]}";
  const auto books = parseDocument(json);
  ASSERT_EQ(books.size(), 1u);
  EXPECT_EQ(books[0].activeSeconds, 0u);
  EXPECT_STREQ(books[0].title, "Zero");
}

}  // namespace