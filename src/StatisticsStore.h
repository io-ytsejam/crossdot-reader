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
  int64_t dayNumber = 0;  // Local-time civil day; enables gap-aware streak math.
  uint32_t totalSeconds = 0;
  bool goalMet = false;  // totalSeconds >= ReadingTime::DAILY_GOAL_SECONDS
  std::vector<BookReadingStatistics> books;
};

struct ReadingStatisticsHistory {
  bool clockAvailable = false;
  std::string todayDate;
  int64_t todayDayNumber = 0;  // Local civil day "now"; enables relative day labels.
  int currentStreak = 0;       // Consecutive goal-met days ending today (or yesterday).
  std::vector<DailyReadingStatistics> days;
};

struct ReadingStatisticsVisitResult {
  bool clockAvailable = false;
  bool completed = true;
  int64_t todayDayNumber = 0;
  int currentStreak = 0;
};

// One book's aggregate reading time for one civil day, as produced by the
// statistics import parser. Carries the day it belongs to inline so the import
// can stream an export document and commit each parsed book as soon as its
// object closes, without buffering the whole payload. Fixed-size fields mirror
// the export document (schema v2); a book without an SD path (v1 export) has an
// empty `path` and is keyed by title+author on merge.
struct StatisticsImportBook {
  int64_t dayNumber = 0;  // Local-time civil day; derives the per-day file.
  uint32_t activeSeconds = 0;
  int64_t lastReadAt = 0;  // UTC epoch; matches the export's session "end".
  char date[16] = {};      // "YYYY-MM-DD" verbatim from the payload.
  char path[256] = {};
  char title[256] = {};
  char author[128] = {};
  char coverBmpPath[256] = {};
};

class StatisticsStore {
 public:
  using DayVisitor = bool (*)(void* context, const DailyReadingStatistics& day);

  static StatisticsStore& getInstance();

  StatisticsStore(const StatisticsStore&) = delete;
  StatisticsStore& operator=(const StatisticsStore&) = delete;

  bool beginReading(const std::string& path, const std::string& title, const std::string& author = {},
                    const std::string& coverBmpPath = {});
  void recordPageTurn();
  void endReading();

  // Fraction of the daily reading goal reached so far (0..1), including the
  // in-progress session. Reads a cached value only, so it is cheap and safe to
  // call from the render path (never touches the SD card).
  float todayGoalProgress() const;

  ReadingStatisticsHistory getHistory();
  ReadingStatisticsVisitResult visitHistory(void* context, DayVisitor visitor);
  void updateBookPath(const std::string& oldPath, const std::string& newPath);

  // Merge one imported book's aggregate reading time for a single day into the
  // persisted store. Called by the import stream as each book object closes;
  // books never share between two days, so the day is carried inline. Books are
  // keyed by `book.path` when present, otherwise by title+author (imports of
  // the path-less v1 export). Repeated imports of the same data accumulate
  // rather than overwrite. Returns false only on an SD read/write failure.
  bool mergeImportedBook(const StatisticsImportBook& book);

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
  // Daily-goal cache for the status-bar ring. baselineDayNumber is the local day
  // at session open; baselineSeconds is that day's persisted total, and
  // todayActiveSecondsSnapshot is the total shown for the day the session is
  // currently recording. Only the main task writes it; the render task reads
  // the uint32.
  int64_t baselineDayNumber = 0;
  uint32_t baselineSeconds = 0;
  uint32_t todayActiveSecondsSnapshot = 0;

  static bool getLocalEpoch(int64_t& localEpoch, int32_t* utcOffsetSeconds = nullptr);
  static std::string filePathForDay(int64_t dayNumber);
  static bool appendSession(const ReadingTime::DayFragment& fragment, const std::string& path, const std::string& title,
                            const std::string& author, const std::string& coverBmpPath, int32_t utcOffsetSeconds);
  static DailyReadingStatistics loadDay(int64_t dayNumber);
  void pruneOldFiles(int64_t todayDayNumber);
  void refreshTodaySnapshot();
};

#define READING_STATISTICS StatisticsStore::getInstance()
