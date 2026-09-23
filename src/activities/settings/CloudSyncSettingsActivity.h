#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Submenu for Cloud Sync (Le Biblioteq) settings.
 * Enables the feature, configures the server URL and device token, and shows
 * the live sync status.
 */
class CloudSyncSettingsActivity final : public Activity {
 public:
  explicit CloudSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CloudSyncSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  size_t selectedIndex = 0;

  void handleSelection();
};