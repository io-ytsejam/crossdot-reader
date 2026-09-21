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
  DailyReadingStatistics statistics;
  int selectorIndex = 0;

  std::string bookTitle(int index) const;

 public:
  explicit StatisticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Statistics", renderer, mappedInput) {}

  void render(RenderLock&&) override;
  void loop() override;
  void onEnter() override;
};
