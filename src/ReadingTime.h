#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ReadingTime {

constexpr uint32_t IDLE_CUTOFF_SECONDS = 5 * 60;
constexpr int64_t SECONDS_PER_DAY = 24 * 60 * 60;

struct Date {
  int year = 1970;
  unsigned month = 1;
  unsigned day = 1;
};

// Howard Hinnant's civil-calendar algorithms, shifted to the Unix epoch.
constexpr int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

constexpr Date civilFromDays(int64_t days) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  int year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPrime = (5 * dayOfYear + 2) / 153;
  const unsigned day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
  const unsigned month = monthPrime + (monthPrime < 10 ? 3 : -9);
  year += month <= 2;
  return {year, month, day};
}

// Day of week for a day number, Monday=0 .. Sunday=6. The Unix epoch day 0
// (1970-01-01) was a Thursday. The +7 keeps the modulo correct for negative
// day numbers (dates before 1970).
constexpr int weekdayFromDayNumber(const int64_t dayNumber) { return static_cast<int>(((dayNumber % 7) + 10) % 7); }

inline int64_t floorDay(const int64_t localEpochSeconds) {
  if (localEpochSeconds >= 0) return localEpochSeconds / SECONDS_PER_DAY;
  return (localEpochSeconds - (SECONDS_PER_DAY - 1)) / SECONDS_PER_DAY;
}

inline int64_t localEpochFromUtc(const int year, const unsigned month, const unsigned day, const unsigned hour,
                                 const unsigned minute, const unsigned second, const int offsetQuarterHours) {
  return daysFromCivil(year, month, day) * SECONDS_PER_DAY + static_cast<int64_t>(hour) * 3600 +
         static_cast<int64_t>(minute) * 60 + second + static_cast<int64_t>(offsetQuarterHours) * 15 * 60;
}

inline std::string dateString(const int64_t dayNumber) {
  const Date date = civilFromDays(dayNumber);
  char buffer[11];
  snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", date.year, date.month, date.day);
  return buffer;
}

inline std::string formatDuration(const uint32_t seconds) {
  if (seconds == 0) return "0 min";
  if (seconds < 60) return "< 1 min";
  const uint32_t minutes = seconds / 60;
  if (minutes < 60) return std::to_string(minutes) + " min";
  const uint32_t hours = minutes / 60;
  const uint32_t remainingMinutes = minutes % 60;
  if (remainingMinutes == 0) return std::to_string(hours) + " h";
  return std::to_string(hours) + " h " + std::to_string(remainingMinutes) + " min";
}

// Daily reading goal. A day meets the goal once this much active reading time
// (as counted by Accumulator around manual page turns) is recorded for it.
constexpr uint32_t DAILY_GOAL_SECONDS = 10 * 60;

inline bool dailyGoalMet(const uint32_t activeSeconds) { return activeSeconds >= DAILY_GOAL_SECONDS; }

struct DayFragment {
  int64_t dayNumber = 0;
  int64_t firstActiveAt = 0;
  int64_t lastActiveAt = 0;
  uint32_t activeSeconds = 0;
};

class Accumulator {
 public:
  void begin(const int64_t localEpochSeconds, const uint32_t nowMs) {
    startEpochSeconds = localEpochSeconds;
    startMs = nowMs;
    lastMarkerMs = nowMs;
    fragments.clear();
    running = true;
  }

  void mark(const uint32_t nowMs) {
    if (!running) return;
    const uint32_t elapsedMs = nowMs - lastMarkerMs;
    const uint32_t activeSeconds = std::min(elapsedMs / 1000, IDLE_CUTOFF_SECONDS);
    const int64_t markerEpoch = epochAt(lastMarkerMs);
    addInterval(markerEpoch, markerEpoch + activeSeconds);
    lastMarkerMs = nowMs;
  }

  const std::vector<DayFragment>& finish(const uint32_t nowMs) {
    mark(nowMs);
    running = false;
    return fragments;
  }

  bool isRunning() const { return running; }
  const std::vector<DayFragment>& getFragments() const { return fragments; }

 private:
  int64_t startEpochSeconds = 0;
  uint32_t startMs = 0;
  uint32_t lastMarkerMs = 0;
  bool running = false;
  std::vector<DayFragment> fragments;

  int64_t epochAt(const uint32_t atMs) const {
    return startEpochSeconds + static_cast<int64_t>(static_cast<uint32_t>(atMs - startMs) / 1000);
  }

  void addInterval(int64_t start, const int64_t end) {
    while (start < end) {
      const int64_t dayNumber = floorDay(start);
      const int64_t dayEnd = (dayNumber + 1) * SECONDS_PER_DAY;
      const int64_t fragmentEnd = std::min(end, dayEnd);
      auto it = std::find_if(fragments.begin(), fragments.end(),
                             [dayNumber](const DayFragment& fragment) { return fragment.dayNumber == dayNumber; });
      if (it == fragments.end()) {
        fragments.push_back({dayNumber, start, fragmentEnd, static_cast<uint32_t>(fragmentEnd - start)});
      } else {
        it->firstActiveAt = std::min(it->firstActiveAt, start);
        it->lastActiveAt = std::max(it->lastActiveAt, fragmentEnd);
        it->activeSeconds += static_cast<uint32_t>(fragmentEnd - start);
      }
      start = fragmentEnd;
    }
  }
};

}  // namespace ReadingTime
