#pragma once

#include <cstdint>
#include <string_view>

// Pure validation shared by the reader service and host-side tests. The cloud
// sync runs once per boot (battery-friendly): validation here keeps that single
// attempt from ever starting half-configured. Never follow an HTTP redirect
// while carrying the bearer credential.
namespace CloudSyncPolicy {

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
}  // namespace CloudSyncPolicy