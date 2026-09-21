#include "StatisticsActivity.h"

#include <Bitmap.h>
#include <HalStorage.h>

#include <algorithm>
#include <cmath>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "ReadingTime.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

void StatisticsActivity::enrichSelectedDayBooks() {
  if (selectedDayIndex < 0 || selectedDayIndex >= static_cast<int>(history.days.size())) return;
  if (selectedDayIndex < static_cast<int>(enrichedDays.size()) && enrichedDays[selectedDayIndex]) return;

  auto& books = history.days[selectedDayIndex].books;
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  for (auto& book : books) {
    if (!Storage.exists(book.path.c_str())) continue;

    auto recent = std::find_if(recentBooks.begin(), recentBooks.end(),
                               [&book](const RecentBook& value) { return value.path == book.path; });
    const RecentBook metadata = recent != recentBooks.end() ? *recent : RECENT_BOOKS.getDataFromBook(book.path);
    if (!metadata.title.empty()) book.title = metadata.title;
    if (!metadata.author.empty()) book.author = metadata.author;
    if (!metadata.coverBmpPath.empty()) book.coverBmpPath = metadata.coverBmpPath;
  }
  if (selectedDayIndex < static_cast<int>(enrichedDays.size())) enrichedDays[selectedDayIndex] = true;
}

void StatisticsActivity::focusSelectedDayBooks() {
  if (selectedDayIndex < 0 || selectedDayIndex >= static_cast<int>(history.days.size())) return;
  enrichSelectedDayBooks();
  if (history.days[selectedDayIndex].books.empty()) return;

  focus = Focus::Book;
  selectedBookIndex = 0;
  ensureSelectionVisible(historyHeight());
  requestUpdate();
}

void StatisticsActivity::activateSelectedBook() {
  if (selectedDayIndex < 0 || selectedDayIndex >= static_cast<int>(history.days.size())) return;
  const auto& books = history.days[selectedDayIndex].books;
  if (selectedBookIndex < 0 || selectedBookIndex >= static_cast<int>(books.size())) return;
  onSelectBook(books[selectedBookIndex].path);
}

int StatisticsActivity::dayHeaderHeight() const {
  return std::max(46, UITheme::getInstance().getMetrics().listRowHeight);
}

int StatisticsActivity::bookRowHeight() const {
  return std::max(88, UITheme::getInstance().getMetrics().listWithSubtitleRowHeight + 28);
}

int StatisticsActivity::dayGap() const { return UITheme::getInstance().getMetrics().verticalSpacing; }

int StatisticsActivity::dayHeight(const int dayIndex) const {
  return dayHeaderHeight() + static_cast<int>(history.days[dayIndex].books.size()) * bookRowHeight();
}

int StatisticsActivity::dayTop(const int dayIndex) const {
  int top = 0;
  for (int index = 0; index < dayIndex; ++index) top += dayHeight(index) + dayGap();
  return top;
}

int StatisticsActivity::totalHistoryHeight() const {
  int height = 0;
  for (int index = 0; index < static_cast<int>(history.days.size()); ++index) {
    height += dayHeight(index);
    if (index + 1 < static_cast<int>(history.days.size())) height += dayGap();
  }
  return height;
}

int StatisticsActivity::contentTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
}

int StatisticsActivity::contentBottom() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
}

bool StatisticsActivity::streakBannerVisible() const { return history.clockAvailable && history.currentStreak > 0; }

int StatisticsActivity::streakBannerHeight() const {
  // Text line plus vertical breathing room above and below.
  return renderer.getLineHeight(UI_10_FONT_ID) + 16;
}

int StatisticsActivity::historyTop() const { return contentTop() + (streakBannerVisible() ? streakBannerHeight() : 0); }

int StatisticsActivity::historyHeight() const { return contentBottom() - historyTop(); }

void StatisticsActivity::ensureSelectionVisible(const int contentHeight) {
  if (history.days.empty()) {
    scrollOffset = 0;
    return;
  }

  int targetTop = dayTop(selectedDayIndex);
  int targetHeight = dayHeaderHeight();
  if (focus == Focus::Book) {
    targetTop += dayHeaderHeight() + selectedBookIndex * bookRowHeight();
    targetHeight = bookRowHeight();
  }

  if (targetTop < scrollOffset) {
    scrollOffset = targetTop;
  } else if (targetTop + targetHeight > scrollOffset + contentHeight) {
    scrollOffset = targetTop + targetHeight - contentHeight;
  }
  scrollOffset = std::clamp(scrollOffset, 0, std::max(0, totalHistoryHeight() - contentHeight));
}

void StatisticsActivity::drawDayHeader(const DailyReadingStatistics& day, const int y, const int height,
                                       const bool selected) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  if (selected) renderer.fillRoundedRect(x, y + 2, width, height - 4, 10, Color::LightGray);

  // Goal ring on the left edge: a partial arc while the day is in progress,
  // a closed circle once the goal is met. Date shifts right to make room.
  constexpr int kRingRadius = 12;
  constexpr int kRingStroke = 3;
  const int ringCx = x + 12 + kRingRadius;
  const float goalFraction =
      day.goalMet ? 1.0f : static_cast<float>(day.totalSeconds) / static_cast<float>(ReadingTime::DAILY_GOAL_SECONDS);
  drawGoalRing(ringCx, y + height / 2, kRingRadius, kRingStroke, goalFraction);

  const std::string title =
      day.date == history.todayDate ? std::string(tr(STR_STATISTICS_TODAY)) + " (" + day.date + ")" : day.date;
  const std::string duration = ReadingTime::formatDuration(day.totalSeconds);
  const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, duration.c_str());
  const int textX = ringCx + kRingRadius + 10;
  const int textWidth = std::max(20, x + width - valueWidth - 12 - textX);
  const auto visibleTitle = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), textWidth, EpdFontFamily::BOLD);
  const int textY = y + (height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, textX, textY, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x + width - valueWidth - 12, textY, duration.c_str());
}

void StatisticsActivity::drawBookRow(const BookReadingStatistics& book, const int y, const int height,
                                     const bool selected) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowX = metrics.contentSidePadding + 8;
  const int rowWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - 16;
  if (selected) renderer.fillRoundedRect(rowX, y + 2, rowWidth, height - 4, 10, Color::LightGray);

  const int coverHeight = height - 12;
  const int coverWidth = std::max(42, coverHeight * 2 / 3);
  const int coverX = rowX + 6;
  const int coverY = y + 5;
  bool coverRendered = false;
  if (!book.coverBmpPath.empty()) {
    const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, metrics.homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("STS", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, coverX, coverY, coverWidth, coverHeight);
        coverRendered = true;
      }
      file.close();
    }
  }
  if (!coverRendered) {
    renderer.drawIcon(CoverIcon, coverX + (coverWidth - 32) / 2, coverY + (coverHeight - 32) / 2, 32);
  }

  const int textX = coverX + coverWidth + 10;
  const int textWidth = std::max(20, rowX + rowWidth - textX - 8);
  std::string title = book.title;
  if (title.empty()) {
    const size_t slash = book.path.find_last_of('/');
    title = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
  }
  title = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, textX, y + 8, title.c_str(), true, EpdFontFamily::BOLD);

  if (!book.author.empty()) {
    const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, y + 34, author.c_str());
  }
  const auto duration = ReadingTime::formatDuration(book.activeSeconds);
  renderer.drawText(SMALL_FONT_ID, textX, y + height - renderer.getLineHeight(SMALL_FONT_ID) - 9, duration.c_str());
}

// Rasterizes the daily goal ring: a full annulus once fraction reaches 1
// (closed circle = goal achieved), otherwise a clockwise arc from 12 o'clock
// proportional to the day's reading progress. Uses drawPixel, which is
// orientation-aware, so no extra transform handling is needed. The achieved
// case needs no float math at all; the arc case costs one atan2f per annulus
// pixel (software float on the C3), negligible next to the e-ink refresh.
void StatisticsActivity::drawGoalRing(const int cx, const int cy, const int radius, const int stroke,
                                      const float fraction) const {
  if (radius <= 0 || stroke <= 0 || fraction <= 0.0f) return;

  const int innerRadius = std::max(radius - stroke, 0);
  const int outerSq = radius * radius;
  const int innerSq = innerRadius * innerRadius;

  if (fraction >= 1.0f) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const int dSq = dx * dx + dy * dy;
        if (dSq <= outerSq && dSq >= innerSq) renderer.drawPixel(cx + dx, cy + dy, true);
      }
    }
    return;
  }

  constexpr float kTwoPi = 6.2831853f;
  const float sweep = fraction * kTwoPi;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const int dSq = dx * dx + dy * dy;
      if (dSq > outerSq || dSq < innerSq) continue;
      float angle = atan2f(static_cast<float>(dx), static_cast<float>(-dy));  // 0 = top, clockwise
      if (angle < 0.0f) angle += kTwoPi;
      if (angle <= sweep) renderer.drawPixel(cx + dx, cy + dy, true);
    }
  }
}

// One-line summary above the day list: current streak of consecutive days
// with the 10-minute goal met, e.g. "5 day streak". Hidden while the streak
// is zero so an idle device does not advertise an empty state.
void StatisticsActivity::drawStreakBanner(const int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const std::string label = std::to_string(history.currentStreak) + " " + tr(STR_STATISTICS_STREAK);
  const auto visible = renderer.truncatedText(
      UI_10_FONT_ID, label.c_str(), renderer.getScreenWidth() - metrics.contentSidePadding * 2, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 8, visible.c_str(), true, EpdFontFamily::BOLD);
}

void StatisticsActivity::renderHistory(const int contentTop, const int contentBottom) {
  if (history.days.empty()) {
    const int middle = contentTop + (contentBottom - contentTop) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, middle, tr(STR_STATISTICS_EMPTY));
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  for (int dayIndex = 0; dayIndex < static_cast<int>(history.days.size()); ++dayIndex) {
    const int cardTop = contentTop + dayTop(dayIndex) - scrollOffset;
    const int cardHeight = dayHeight(dayIndex);
    if (cardTop >= contentBottom || cardTop + cardHeight <= contentTop) continue;

    drawDayHeader(history.days[dayIndex], cardTop, dayHeaderHeight(),
                  focus == Focus::Day && dayIndex == selectedDayIndex);
    const auto& books = history.days[dayIndex].books;
    for (int bookIndex = 0; bookIndex < static_cast<int>(books.size()); ++bookIndex) {
      const int rowY = cardTop + dayHeaderHeight() + bookIndex * bookRowHeight();
      if (rowY >= contentBottom || rowY + bookRowHeight() <= contentTop) continue;
      drawBookRow(books[bookIndex], rowY, bookRowHeight(),
                  focus == Focus::Book && dayIndex == selectedDayIndex && bookIndex == selectedBookIndex);
    }

    if (dayIndex + 1 < static_cast<int>(history.days.size())) {
      const int separatorY = cardTop + cardHeight + dayGap() / 2;
      if (separatorY >= contentTop && separatorY < contentBottom) {
        renderer.drawLine(metrics.contentSidePadding + 12, separatorY,
                          renderer.getScreenWidth() - metrics.contentSidePadding - 13, separatorY, true);
      }
    }
  }
}

void StatisticsActivity::handleTouch(const int contentTop, const int contentHeight) {
  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || y < contentTop || y >= contentTop + contentHeight) return;
  const int sidePadding = UITheme::getInstance().getMetrics().contentSidePadding;
  if (x < sidePadding || x >= renderer.getScreenWidth() - sidePadding) return;
  const int logicalY = y - contentTop + scrollOffset;
  for (int dayIndex = 0; dayIndex < static_cast<int>(history.days.size()); ++dayIndex) {
    const int top = dayTop(dayIndex);
    if (logicalY < top || logicalY >= top + dayHeight(dayIndex)) continue;

    selectedDayIndex = dayIndex;
    if (logicalY < top + dayHeaderHeight()) {
      focusSelectedDayBooks();
      return;
    }
    const int bookIndex = (logicalY - top - dayHeaderHeight()) / bookRowHeight();
    if (bookIndex >= 0 && bookIndex < static_cast<int>(history.days[dayIndex].books.size())) {
      focus = Focus::Book;
      selectedBookIndex = bookIndex;
      activateSelectedBook();
    }
    return;
  }
}

void StatisticsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_STATISTICS));

  const int top = contentTop();
  const int bottom = contentBottom();
  if (!history.clockAvailable) {
    const int middle = top + (bottom - top) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, middle - 18, tr(STR_STATISTICS_UNAVAILABLE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, middle + 16, tr(STR_STATISTICS_CLOCK_REQUIRED));
  } else {
    if (streakBannerVisible()) drawStreakBanner(top);
    renderHistory(historyTop(), bottom);
  }

  const auto labels =
      mappedInput.mapLabels(focus == Focus::Day ? tr(STR_HOME) : tr(STR_BACK),
                            focus == Focus::Day ? tr(STR_SELECT) : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void StatisticsActivity::loop() {
  const int contentHeight = historyHeight();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (focus == Focus::Book) {
      focus = Focus::Day;
      ensureSelectionVisible(contentHeight);
      requestUpdate();
    } else {
      onGoHome();
    }
    return;
  }

  const bool dayFocus = focus == Focus::Day;
  const int itemCount = dayFocus ? static_cast<int>(history.days.size())
                                 : (selectedDayIndex >= 0 && selectedDayIndex < static_cast<int>(history.days.size())
                                        ? static_cast<int>(history.days[selectedDayIndex].books.size())
                                        : 0);
  if (itemCount == 0) return;
  int& selectedIndex = dayFocus ? selectedDayIndex : selectedBookIndex;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (dayFocus) {
      focusSelectedDayBooks();
    } else {
      activateSelectedBook();
    }
    return;
  }

  handleTouch(historyTop(), contentHeight);

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    if (dayFocus) enrichSelectedDayBooks();
    ensureSelectionVisible(contentHeight);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    if (dayFocus) enrichSelectedDayBooks();
    ensureSelectionVisible(contentHeight);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    if (dayFocus) enrichSelectedDayBooks();
    ensureSelectionVisible(contentHeight);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    if (dayFocus) enrichSelectedDayBooks();
    ensureSelectionVisible(contentHeight);
    requestUpdate();
  }
}

void StatisticsActivity::onEnter() {
  Activity::onEnter();
  history = READING_STATISTICS.getHistory();
  focus = Focus::Day;
  selectedDayIndex = 0;
  selectedBookIndex = 0;
  scrollOffset = 0;
  enrichedDays.assign(history.days.size(), false);
  enrichSelectedDayBooks();
  requestUpdate();
}
