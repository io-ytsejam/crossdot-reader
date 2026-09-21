#pragma once

#include <I18n.h>
#include <activities/Activity.h>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "StatisticsStore.h"
#include "activities/RenderLock.h"
#include "util/ButtonNavigator.h"

class StatisticsActivity final : public Activity {
 private:
  enum class Focus { Day, Book };

  ReadingStatisticsHistory history;
  Focus focus = Focus::Day;
  int selectedDayIndex = 0;
  int selectedBookIndex = 0;
  int scrollOffset = 0;
  std::vector<bool> enrichedDays;

  void enrichSelectedDayBooks();
  void focusSelectedDayBooks();
  void activateSelectedBook();
  void handleTouch(int contentTop, int contentHeight);
  void ensureSelectionVisible(int contentHeight);

  int dayHeaderHeight() const;
  int bookRowHeight() const;
  int dayGap() const;
  int dayHeight(int dayIndex) const;
  int dayTop(int dayIndex) const;
  int totalHistoryHeight() const;

  void renderHistory(int contentTop, int contentBottom);
  void drawDayHeader(const DailyReadingStatistics& day, int y, int height, bool selected);
  void drawBookRow(const BookReadingStatistics& book, int y, int height, bool selected);
  void drawGoalRing(int cx, int cy, int radius, int stroke, float fraction) const;

 public:
  explicit StatisticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Statistics", renderer, mappedInput) {}

  void render(RenderLock&&) override;
  void loop() override;
  void onEnter() override;
};
