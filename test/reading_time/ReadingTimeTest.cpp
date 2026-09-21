#include <gtest/gtest.h>

#include <cstdint>

#include "src/ReadingTime.h"

TEST(ReadingTimeCalendar, ConvertsCivilDatesRoundTrip) {
  const auto leapDay = ReadingTime::daysFromCivil(2024, 2, 29);
  const auto date = ReadingTime::civilFromDays(leapDay);
  EXPECT_EQ(date.year, 2024);
  EXPECT_EQ(date.month, 2U);
  EXPECT_EQ(date.day, 29U);
  EXPECT_EQ(ReadingTime::dateString(leapDay), "2024-02-29");
}

TEST(ReadingTimeCalendar, AppliesPositiveAndNegativeUtcOffsetsAcrossDays) {
  const int64_t utcDay = ReadingTime::daysFromCivil(2026, 9, 21);
  const int64_t east = ReadingTime::localEpochFromUtc(2026, 9, 21, 23, 30, 0, 4);
  const int64_t west = ReadingTime::localEpochFromUtc(2026, 9, 21, 1, 30, 0, -8);

  EXPECT_EQ(ReadingTime::floorDay(east), utcDay + 1);
  EXPECT_EQ(ReadingTime::floorDay(west), utcDay - 1);
}

TEST(ReadingTimeAccumulator, CountsOpenAndCloseInterval) {
  ReadingTime::Accumulator accumulator;
  accumulator.begin(ReadingTime::daysFromCivil(2026, 9, 21) * ReadingTime::SECONDS_PER_DAY, 1000);
  const auto& fragments = accumulator.finish(121000);

  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_EQ(fragments[0].activeSeconds, 120U);
}

TEST(ReadingTimeAccumulator, CapsEveryPageIntervalAtFiveMinutes) {
  ReadingTime::Accumulator accumulator;
  accumulator.begin(0, 0);
  accumulator.mark(10 * 60 * 1000);
  const auto& fragments = accumulator.finish(12 * 60 * 1000);

  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_EQ(fragments[0].activeSeconds, 7U * 60U);
  EXPECT_EQ(fragments[0].firstActiveAt, 0);
  EXPECT_EQ(fragments[0].lastActiveAt, 12 * 60);
}

TEST(ReadingTimeAccumulator, SplitsActiveTimeAtLocalMidnight) {
  const int64_t day = ReadingTime::daysFromCivil(2026, 9, 21);
  ReadingTime::Accumulator accumulator;
  accumulator.begin(day * ReadingTime::SECONDS_PER_DAY + 23 * 3600 + 59 * 60, 0);
  const auto& fragments = accumulator.finish(2 * 60 * 1000);

  ASSERT_EQ(fragments.size(), 2U);
  EXPECT_EQ(fragments[0].dayNumber, day);
  EXPECT_EQ(fragments[0].activeSeconds, 60U);
  EXPECT_EQ(fragments[1].dayNumber, day + 1);
  EXPECT_EQ(fragments[1].activeSeconds, 60U);
}

TEST(ReadingTimeAccumulator, HandlesMillisRollover) {
  ReadingTime::Accumulator accumulator;
  accumulator.begin(0, UINT32_MAX - 29999U);
  const auto& fragments = accumulator.finish(30000U);

  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_EQ(fragments[0].activeSeconds, 60U);
}

TEST(ReadingTimeFormatting, UsesReadableUnits) {
  EXPECT_EQ(ReadingTime::formatDuration(0), "0 min");
  EXPECT_EQ(ReadingTime::formatDuration(59), "< 1 min");
  EXPECT_EQ(ReadingTime::formatDuration(4 * 60 + 59), "4 min");
  EXPECT_EQ(ReadingTime::formatDuration(60 * 60), "1 h");
  EXPECT_EQ(ReadingTime::formatDuration(80 * 60), "1 h 20 min");
}
