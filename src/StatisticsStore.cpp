#include "StatisticsStore.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cstdlib>

#include "CrossPointSettings.h"

namespace {
constexpr const char* MODULE = "STS";

bool isStatisticsFilename(const char* name) {
  if (!name) return false;
  const std::string value(name);
  if (value.size() != 15 || value.substr(10) != ".json") return false;
  return value[4] == '-' && value[7] == '-';
}

bool dayNumberFromFilename(const char* name, int64_t& dayNumber) {
  if (!isStatisticsFilename(name)) return false;
  const int year = atoi(std::string(name, 4).c_str());
  const unsigned month = static_cast<unsigned>(atoi(std::string(name + 5, 2).c_str()));
  const unsigned day = static_cast<unsigned>(atoi(std::string(name + 8, 2).c_str()));
  if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  dayNumber = ReadingTime::daysFromCivil(year, month, day);
  return true;
}

// Longest run of goal-met days ending today — or ending yesterday while today
// is still in progress. days are newest-first; a missing entry is a day with
// no statistics file (device off, or goal missed), which ends the streak.
// Bounded by RETENTION_DAYS, since older files are pruned.
int currentStreak(const std::vector<DailyReadingStatistics>& days, const int64_t today) {
  int64_t expected = today;
  if (days.empty() || days.front().dayNumber != today || !days.front().goalMet) {
    // Today has not reached the goal yet (no reading, or still reading): an
    // in-progress day must not zero a streak that is still alive.
    expected = today - 1;
  }

  int streak = 0;
  for (const auto& day : days) {
    if (day.dayNumber > expected) continue;  // today, still in progress
    if (day.dayNumber < expected || !day.goalMet) break;
    ++streak;
    --expected;
  }
  return streak;
}
}  // namespace

StatisticsStore& StatisticsStore::getInstance() {
  static StatisticsStore instance;
  return instance;
}

bool StatisticsStore::getLocalEpoch(int64_t& localEpoch, int32_t* utcOffsetSeconds) {
  Rtc::DateTime utc;
  if (!halClock.getDateTime(utc, true)) return false;

  const uint8_t offsetSetting = std::min<uint8_t>(SETTINGS.clockUtcOffsetQ, 104);
  const int offsetQuarterHours = static_cast<int>(offsetSetting) - 48;
  if (utcOffsetSeconds) *utcOffsetSeconds = offsetQuarterHours * 15 * 60;
  localEpoch = ReadingTime::localEpochFromUtc(utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second,
                                              offsetQuarterHours);
  return true;
}

std::string StatisticsStore::filePathForDay(const int64_t dayNumber) {
  return std::string(DIRECTORY) + "/" + ReadingTime::dateString(dayNumber) + ".json";
}

bool StatisticsStore::beginReading(const std::string& path, const std::string& title, const std::string& author,
                                   const std::string& coverBmpPath) {
  endReading();

  int64_t localEpoch = 0;
  if (!getLocalEpoch(localEpoch, &activeUtcOffsetSeconds)) {
    LOG_INF(MODULE, "Reading statistics disabled: no valid RTC time");
    return false;
  }

  activeBookPath = path;
  activeBookTitle = title;
  activeBookAuthor = author;
  activeBookCoverBmpPath = coverBmpPath;
  accumulator.begin(localEpoch, millis());
  return true;
}

void StatisticsStore::recordPageTurn() {
  if (accumulator.isRunning()) accumulator.mark(millis());
}

bool StatisticsStore::appendSession(const ReadingTime::DayFragment& fragment, const std::string& path,
                                    const std::string& title, const std::string& author,
                                    const std::string& coverBmpPath, const int32_t utcOffsetSeconds) {
  if (fragment.activeSeconds == 0) return true;

  Storage.ensureDirectoryExists(DIRECTORY);
  const std::string filePath = filePathForDay(fragment.dayNumber);
  JsonDocument doc;
  if (Storage.exists(filePath.c_str()) && !PersistableStoreBase::readDocFromFile(filePath.c_str(), doc)) {
    LOG_ERR(MODULE, "Could not load statistics file: %s", filePath.c_str());
    return false;
  }

  doc["date"] = ReadingTime::dateString(fragment.dayNumber);
  JsonArray sessions = doc["sessions"].to<JsonArray>();
  JsonObject session = sessions.add<JsonObject>();
  session["path"] = path;
  session["title"] = title;
  session["author"] = author;
  session["coverBmpPath"] = coverBmpPath;
  session["start"] = fragment.firstActiveAt - utcOffsetSeconds;
  session["end"] = fragment.lastActiveAt - utcOffsetSeconds;
  session["offsetMinutes"] = utcOffsetSeconds / 60;
  session["seconds"] = fragment.activeSeconds;
  return PersistableStoreBase::writeDocToFile(filePath.c_str(), doc);
}

void StatisticsStore::endReading() {
  if (!accumulator.isRunning()) return;

  const auto& fragments = accumulator.finish(millis());
  for (const auto& fragment : fragments) {
    if (!appendSession(fragment, activeBookPath, activeBookTitle, activeBookAuthor, activeBookCoverBmpPath,
                       activeUtcOffsetSeconds)) {
      LOG_ERR(MODULE, "Failed to persist reading session for %s", activeBookPath.c_str());
    }
  }

  if (!fragments.empty()) pruneOldFiles(fragments.back().dayNumber);
  activeBookPath.clear();
  activeBookTitle.clear();
  activeBookAuthor.clear();
  activeBookCoverBmpPath.clear();
  activeUtcOffsetSeconds = 0;
}

DailyReadingStatistics StatisticsStore::loadDay(const int64_t dayNumber) {
  DailyReadingStatistics result;
  result.dayNumber = dayNumber;
  result.date = ReadingTime::dateString(dayNumber);

  JsonDocument doc;
  const std::string filePath = filePathForDay(dayNumber);
  if (!Storage.exists(filePath.c_str())) return result;
  if (!PersistableStoreBase::readDocFromFile(filePath.c_str(), doc)) return result;

  for (const JsonObjectConst session : doc["sessions"].as<JsonArrayConst>()) {
    const char* pathValue = session["path"] | "";
    if (pathValue[0] == '\0') continue;
    const char* titleValue = session["title"] | "";
    const char* authorValue = session["author"] | "";
    const char* coverBmpPathValue = session["coverBmpPath"] | "";
    const uint32_t seconds = session["seconds"] | 0U;
    const int64_t lastReadAt = session["end"] | static_cast<int64_t>(0);

    auto book = std::find_if(result.books.begin(), result.books.end(),
                             [pathValue](const BookReadingStatistics& value) { return value.path == pathValue; });
    if (book == result.books.end()) {
      result.books.push_back({pathValue, titleValue, authorValue, coverBmpPathValue, seconds, lastReadAt});
    } else {
      book->activeSeconds += seconds;
      book->lastReadAt = std::max(book->lastReadAt, lastReadAt);
      if (book->title.empty() && titleValue[0] != '\0') book->title = titleValue;
      if (book->author.empty() && authorValue[0] != '\0') book->author = authorValue;
      if (book->coverBmpPath.empty() && coverBmpPathValue[0] != '\0') book->coverBmpPath = coverBmpPathValue;
    }
    result.totalSeconds += seconds;
  }

  result.goalMet = ReadingTime::dailyGoalMet(result.totalSeconds);
  std::sort(result.books.begin(), result.books.end(),
            [](const auto& left, const auto& right) { return left.lastReadAt > right.lastReadAt; });
  return result;
}

ReadingStatisticsHistory StatisticsStore::getHistory() {
  ReadingStatisticsHistory result;
  int64_t localEpoch = 0;
  if (!getLocalEpoch(localEpoch)) return result;

  result.clockAvailable = true;
  const int64_t today = ReadingTime::floorDay(localEpoch);
  result.todayDate = ReadingTime::dateString(today);
  pruneOldFiles(today);

  const int64_t oldestKeptDay = today - (RETENTION_DAYS - 1);
  std::vector<int64_t> dayNumbers;
  for (const String& filename : Storage.listFiles(DIRECTORY, 200)) {
    int64_t dayNumber = 0;
    if (!dayNumberFromFilename(filename.c_str(), dayNumber)) continue;
    if (dayNumber < oldestKeptDay || dayNumber > today) continue;
    dayNumbers.push_back(dayNumber);
  }

  std::sort(dayNumbers.begin(), dayNumbers.end(), [](const int64_t left, const int64_t right) { return left > right; });
  dayNumbers.erase(std::unique(dayNumbers.begin(), dayNumbers.end()), dayNumbers.end());
  result.days.reserve(dayNumbers.size());
  for (const int64_t dayNumber : dayNumbers) {
    auto day = loadDay(dayNumber);
    if (!day.books.empty()) result.days.push_back(std::move(day));
  }
  result.currentStreak = currentStreak(result.days, today);
  return result;
}

ReadingStatisticsVisitResult StatisticsStore::visitHistory(void* context, const DayVisitor visitor) {
  ReadingStatisticsVisitResult result;
  int64_t localEpoch = 0;
  if (!getLocalEpoch(localEpoch)) return result;

  result.clockAvailable = true;
  result.todayDayNumber = ReadingTime::floorDay(localEpoch);
  pruneOldFiles(result.todayDayNumber);

  bool streakActive = true;
  for (int dayOffset = 0; dayOffset < RETENTION_DAYS; ++dayOffset) {
    const auto dayNumber = result.todayDayNumber - dayOffset;
    auto day = loadDay(dayNumber);

    if (streakActive) {
      if (dayOffset == 0 && !day.goalMet) {
        // Today remains eligible until midnight; an unfinished day does not
        // break a streak that ended yesterday.
      } else if (day.goalMet) {
        ++result.currentStreak;
      } else {
        streakActive = false;
      }
    }

    if (!day.books.empty() && visitor && !visitor(context, day)) {
      result.completed = false;
      break;
    }
  }
  return result;
}

void StatisticsStore::updateBookPath(const std::string& oldPath, const std::string& newPath) {
  if (oldPath.empty() || oldPath == newPath) return;
  if (activeBookPath == oldPath) activeBookPath = newPath;

  int64_t localEpoch = 0;
  if (!getLocalEpoch(localEpoch)) return;
  const std::string filePath = filePathForDay(ReadingTime::floorDay(localEpoch));
  if (!Storage.exists(filePath.c_str())) return;

  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(filePath.c_str(), doc)) return;
  bool changed = false;
  for (JsonObject session : doc["sessions"].as<JsonArray>()) {
    const char* path = session["path"] | "";
    if (oldPath == path) {
      session["path"] = newPath;
      changed = true;
    }
  }
  if (changed && !PersistableStoreBase::writeDocToFile(filePath.c_str(), doc)) {
    LOG_ERR(MODULE, "Failed to update moved book path in statistics");
  }
}

void StatisticsStore::pruneOldFiles(const int64_t todayDayNumber) {
  if (retentionPruned) return;
  retentionPruned = true;
  const int64_t oldestKeptDay = todayDayNumber - (RETENTION_DAYS - 1);

  for (const String& filename : Storage.listFiles(DIRECTORY, 200)) {
    int64_t dayNumber = 0;
    if (!dayNumberFromFilename(filename.c_str(), dayNumber) || dayNumber >= oldestKeptDay) continue;
    const std::string path = std::string(DIRECTORY) + "/" + filename.c_str();
    if (!Storage.remove(path.c_str())) {
      LOG_ERR(MODULE, "Failed to prune old statistics file: %s", path.c_str());
    }
  }
}
