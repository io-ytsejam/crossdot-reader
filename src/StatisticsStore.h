#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReadingTime.h"

struct BookReadingStatistics {
  std::string path;
  std::string title;
  uint32_t activeSeconds = 0;
  int64_t lastReadAt = 0;
};

struct DailyReadingStatistics {
  bool clockAvailable = false;
  std::string date;
  uint32_t totalSeconds = 0;
  std::vector<BookReadingStatistics> books;
};

class StatisticsStore {
 public:
  static StatisticsStore& getInstance();

  StatisticsStore(const StatisticsStore&) = delete;
  StatisticsStore& operator=(const StatisticsStore&) = delete;

  bool beginReading(const std::string& path, const std::string& title);
  void recordPageTurn();
  void endReading();

  DailyReadingStatistics getToday();
  void updateBookPath(const std::string& oldPath, const std::string& newPath);

 private:
  StatisticsStore() = default;

  static constexpr int RETENTION_DAYS = 90;
  static constexpr const char* DIRECTORY = "/.crosspoint/statistics";

  ReadingTime::Accumulator accumulator;
  std::string activeBookPath;
  std::string activeBookTitle;
  int32_t activeUtcOffsetSeconds = 0;
  bool retentionPruned = false;

  static bool getLocalEpoch(int64_t& localEpoch, int32_t* utcOffsetSeconds = nullptr);
  static std::string filePathForDay(int64_t dayNumber);
  static bool appendSession(const ReadingTime::DayFragment& fragment, const std::string& path,
                            const std::string& title, int32_t utcOffsetSeconds);
  void pruneOldFiles(int64_t todayDayNumber);
};

#define READING_STATISTICS StatisticsStore::getInstance()
