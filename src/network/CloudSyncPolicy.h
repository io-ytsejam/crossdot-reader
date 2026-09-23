#pragma once

#include <cstdint>
#include <string_view>

// Pure policy shared by the reader service and host-side tests. Never follow an
// HTTP redirect while carrying the bearer credential.
namespace CloudSyncPolicy {
constexpr uint32_t INTERVAL_MS = 30U * 60U * 1000U;
constexpr uint32_t RETRY_MS = 5U * 60U * 1000U;

inline bool validServerUrl(std::string_view url) {
  constexpr std::string_view scheme = "https://";
  if (!url.starts_with(scheme) || url.size() > 160) return false;
  url.remove_prefix(scheme.size());
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

inline bool due(uint32_t now, uint32_t lastAttempt, bool dirty, bool failed) {
  if (lastAttempt == 0) return true;
  const uint32_t elapsed = now - lastAttempt;  // unsigned wraparound is intentional
  if (failed) return elapsed >= RETRY_MS;
  return dirty || elapsed >= INTERVAL_MS;
}
}  // namespace CloudSyncPolicy
