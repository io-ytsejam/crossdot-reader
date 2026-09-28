#pragma once

#include <cstdint>
#include <string_view>

// Pure validation shared by the reader service and host-side tests. The cloud
// sync runs once per boot (battery-friendly): validation here keeps that single
// attempt from ever starting half-configured. Never follow an HTTP redirect
// while carrying the bearer credential.
namespace CloudSyncPolicy {

// Total budget from Wi-Fi begin(), including time overlapped with painting the
// boot screen. Cloud sync is opportunistic and must never dominate boot UX.
constexpr uint32_t BOOT_WIFI_TIMEOUT_MS = 8U * 1000U;
constexpr uint32_t READER_IDLE_SYNC_MS = 5U * 1000U;
constexpr uint32_t HTTP_TIMEOUT_MS = 8U * 1000U;

inline bool readerIdleForSync(uint32_t nowMs, uint32_t lastActivityMs, bool currentActivityIsReader, bool readerBusy) {
  return currentActivityIsReader && !readerBusy && static_cast<uint32_t>(nowMs - lastActivityMs) >= READER_IDLE_SYNC_MS;
}

inline bool validServerUrl(std::string_view url) {
  // Accept either an explicit "https://host" or a bare hostname ("host"), since
  // the upload path normalizes a scheme-less host by prepending https://. Any
  // other scheme (notably http://) is rejected: never talk to the reader's
  // bearer endpoint over plaintext.
  if (url.size() > 160) return false;
  constexpr std::string_view scheme = "https://";
  if (url.starts_with(scheme)) {
    url.remove_prefix(scheme.size());
  } else if (url.starts_with("http://")) {
    return false;
  } else if (url.find("://") != std::string_view::npos) {
    return false;
  }
  if (url.ends_with('/')) url.remove_suffix(1);
  if (url.empty() || url.front() == '.' || url.back() == '.') return false;
  for (const char c : url) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
          c == ':'))
      return false;
  }
  return true;
}

inline bool validToken(std::string_view token) {
  constexpr std::string_view prefix = "lb_reader_";
  if (token.size() != prefix.size() + 64 || !token.starts_with(prefix)) return false;
  for (const char c : token.substr(prefix.size())) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// ESP-IDF wifi_err_reason_t values that mean this boot's saved-network join
// cannot recover by waiting longer. Keep transient disconnects (for example
// reason 8 after an intentional disconnect, or beacon timeout 200) retryable.
inline bool terminalWifiJoinFailure(uint8_t reason) {
  return reason == 2 || reason == 15 || (reason >= 201 && reason <= 205) || (reason >= 210 && reason <= 212);
}

// What the UI should report before/outside an attempt.
//
// The service's status is written by the worker, so until the first attempt of
// a boot it still holds its initial value (Disabled). Reporting that raw value
// makes a fully configured device announce "Sync is off" - the exact opposite
// of the truth - which is what users see on Home right after boot. The only
// honest sources are configuration (is the feature actually off?) and the last
// successful upload time, so derive the label from those instead:
//   * not configured  -> Disabled ("off" is accurate)
//   * configured      -> not off; show the last-synced age when one exists
struct StatusDisplay {
  bool disabled;
  int64_t lastSyncedAt;  // 0 when there has never been a successful upload
};

inline StatusDisplay resolveStatusDisplay(bool configured, int64_t lastSyncedAt) { return {!configured, lastSyncedAt}; }
}  // namespace CloudSyncPolicy