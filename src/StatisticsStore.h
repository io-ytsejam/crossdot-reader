#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReadingTime.h"

struct BookReadingStatistics {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  uint32_t activeSeconds = 0;
  int64_t lastReadAt = 0;
};

struct DailyReadingStatistics {
  std::string date;
  uint32_t totalSeconds = 0;
  std::vector<BookReadingStatistics> books;
};

struct ReadingStatisticsHistory {
  bool clockAvailable = false;
  std::string todayDate;
  std::vector<DailyReadingStatistics> days;
};

class StatisticsStore {
 public:
  static StatisticsStore& getInstance();

  StatisticsStore(const StatisticsStore&) = delete;
  StatisticsStore& operator=(const StatisticsStore&) = delete;

  bool beginReading(const std::string& path, const std::string& title, const std::string& author = {},
                    const std::string& coverBmpPath = {});
  void recordPageTurn();
  void endReading();

  ReadingStatisticsHistory getHistory();
  void updateBookPath(const std::string& oldPath, const std::string& newPath);

 private:
  StatisticsStore() = default;

  static constexpr int RETENTION_DAYS = 90;
  static constexpr const char* DIRECTORY = "/.crosspoint/statistics";

  ReadingTime::Accumulator accumulator;
  std::string activeBookPath;
  std::string activeBookTitle;
  std::string activeBookAuthor;
  std::string activeBookCoverBmpPath;
  int32_t activeUtcOffsetSeconds = 0;
  bool retentionPruned = false;

  static bool getLocalEpoch(int64_t& localEpoch, int32_t* utcOffsetSeconds = nullptr);
  static std::string filePathForDay(int64_t dayNumber);
  static bool appendSession(const ReadingTime::DayFragment& fragment, const std::string& path, const std::string& title,
                            const std::string& author, const std::string& coverBmpPath, int32_t utcOffsetSeconds);
  static DailyReadingStatistics loadDay(int64_t dayNumber);
  void pruneOldFiles(int64_t todayDayNumber);
};

#define READING_STATISTICS StatisticsStore::getInstance()
