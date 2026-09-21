#include "StatisticsActivity.h"

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "ReadingTime.h"
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "fontIds.h"

std::string StatisticsActivity::bookTitle(const int index) const {
  const auto& book = statistics.books[index];
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  return slash == std::string::npos ? book.path : book.path.substr(slash + 1);
}

void StatisticsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_STATISTICS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (!statistics.clockAvailable) {
    const int middle = contentTop + (contentBottom - contentTop) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, middle - 18, tr(STR_STATISTICS_UNAVAILABLE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, middle + 16, tr(STR_STATISTICS_CLOCK_REQUIRED));
  } else {
    const std::string total = ReadingTime::formatDuration(statistics.totalSeconds);
    const std::string summary = std::string(tr(STR_STATISTICS_TODAY)) + ": " + total;
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, contentTop, summary.c_str(), true,
                      EpdFontFamily::BOLD);

    const int listTop = contentTop + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
    const int listHeight = contentBottom - listTop;
    if (statistics.books.empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, listTop + 12, tr(STR_STATISTICS_EMPTY));
    } else {
      GUI.drawList(
          renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(statistics.books.size()), selectorIndex,
          [this](int index) { return bookTitle(index); },
          [this](int index) { return ReadingTime::formatDuration(statistics.books[index].activeSeconds); },
          [](int) { return UIIcon::Book; });
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void StatisticsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int itemCount = static_cast<int>(statistics.books.size());
  if (itemCount == 0) return;
  const int summaryHeight = renderer.getLineHeight(UI_12_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems =
      UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true, summaryHeight);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, itemCount);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, itemCount);
    requestUpdate();
  }
}

void StatisticsActivity::onEnter() {
  Activity::onEnter();
  statistics = READING_STATISTICS.getToday();
  selectorIndex = 0;
  requestUpdate();
}
