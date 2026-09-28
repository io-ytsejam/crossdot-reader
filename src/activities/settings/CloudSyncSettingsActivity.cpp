#include "CloudSyncSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CloudSyncStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CloudSyncPolicy.h"
#include "network/CloudSyncService.h"

namespace {
constexpr int MENU_ITEMS = 3;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_CLOUD_SYNC_ENABLE, StrId::STR_CLOUD_SYNC_SERVER_URL,
                                     StrId::STR_CLOUD_SYNC_TOKEN};
}  // namespace

void CloudSyncSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  requestUpdate();
}

void CloudSyncSettingsActivity::onExit() { Activity::onExit(); }

void CloudSyncSettingsActivity::loop() {
  auto activateSelected = [this] { handleSelection(); };

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  int touchSel = static_cast<int>(selectedIndex);
  const auto listTouch = handleListTouch(touchSel, MENU_ITEMS, contentTop, contentHeight, false);
  if (listTouch != ListTouchResult::None) {
    selectedIndex = static_cast<size_t>(touchSel);
    if (listTouch == ListTouchResult::Activated) activateSelected();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void CloudSyncSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // Enable toggle — require a valid URL + token before the feature can turn
    // on, so the task never starts half-configured.
    const bool next = !CLOUD_SYNC_STORE.isEnabled();
    if (next && !(CloudSyncPolicy::validToken(CLOUD_SYNC_STORE.getToken()) &&
                  CloudSyncPolicy::validServerUrl(CLOUD_SYNC_STORE.getServerUrl()))) {
      return;
    }
    CLOUD_SYNC_STORE.setEnabled(next);
    requestUpdate();
  } else if (selectedIndex == 1) {
    // Server URL
    const std::string currentUrl = CLOUD_SYNC_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CLOUD_SYNC_SERVER_URL),
                                                                   prefillUrl, 160, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://" || kb.text.empty()) ? "" : kb.text;
                               CLOUD_SYNC_STORE.setServerUrl(urlToSave);
                             }
                           });
  } else if (selectedIndex == 2) {
    // Device token
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CLOUD_SYNC_TOKEN),
                                                CLOUD_SYNC_STORE.getToken(), 96, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            CLOUD_SYNC_STORE.setToken(kb.text);
          }
        });
  }
}

void CloudSyncSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLOUD_SYNC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEMS),
      static_cast<int>(selectedIndex), [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr,
      nullptr,
      [this](int index) {
        if (index == 0) {
          return CLOUD_SYNC_STORE.isEnabled() ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
        } else if (index == 1) {
          const std::string url = CLOUD_SYNC_STORE.getServerUrl();
          return url.empty() ? std::string(tr(STR_NOT_SET)) : url;
        } else if (index == 2) {
          return CLOUD_SYNC_STORE.getToken().empty() ? std::string(tr(STR_NOT_SET)) : std::string("••••••••");
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);

  // Live status footer
  const Rect statusBounds{0, contentTop + contentHeight - metrics.menuRowHeight, pageWidth, metrics.menuRowHeight};
  const auto statusText = CLOUD_SYNC_SERVICE.getStatusLabel();
  renderer.drawCenteredText(SMALL_FONT_ID, statusBounds.y, statusText.c_str(), true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}