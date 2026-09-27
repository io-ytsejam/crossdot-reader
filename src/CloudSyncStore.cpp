#include "CloudSyncStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cctype>

namespace {
// A stray leading/trailing spaceor newline from a web paste breaks token/URL
// validation (and is invisible in a password field). Trim it on save so a
// sloppy copy can never silently disable the feature.
void trimWhitespace(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}
}  // namespace

void CloudSyncStore::toJson(JsonDocument& doc) const {
  // storeMutex is already held by saveToFile(); dataMutex is the inner lock
  // (storeMutex -> dataMutex ordering, see CloudSyncStore.h).
  std::lock_guard<std::mutex> lock(dataMutex);
  doc["enabled"] = enabled;
  doc["serverUrl"] = serverUrl;
  doc["token_obf"] = token.empty() ? "" : obfuscation::obfuscateToBase64(token);
  doc["lastSyncedAt"] = lastSyncedAt;
}

bool CloudSyncStore::fromJson(JsonVariantConst doc) {
  std::lock_guard<std::mutex> lock(dataMutex);
  enabled = doc["enabled"] | false;
  serverUrl = doc["serverUrl"] | "";
  bool needsResave = false;
  {
    // extractPassword is keyed to "password_obf"; the token lives under its
    // own obfuscated key, so decode it here.
    bool ok = false;
    std::string t = obfuscation::deobfuscateFromBase64(doc["token_obf"] | "", &ok);
    if (!ok) {
      t = doc["token"] | "";  // legacy plaintext
      if (!t.empty()) needsResave = true;
    }
    token = t;
  }
  lastSyncedAt = doc["lastSyncedAt"] | static_cast<int64_t>(0);
  if (needsResave) requestResave();
  return true;
}

bool CloudSyncStore::setEnabled(bool value) {
  {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (enabled == value) return true;
    enabled = value;
  }
  // dataMutex released before the SD write (storeMutex) — see lock-order note.
  return saveToFile();
}

bool CloudSyncStore::setServerUrl(const std::string& value) {
  std::string trimmed = value;
  trimWhitespace(trimmed);
  {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (serverUrl == trimmed) return true;
    serverUrl = trimmed;
  }
  return saveToFile();
}

bool CloudSyncStore::setToken(const std::string& value) {
  std::string trimmed = value;
  trimWhitespace(trimmed);
  {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (token == trimmed) return true;
    token = trimmed;
  }
  return saveToFile();
}

void CloudSyncStore::setLastSyncedAt(int64_t value) {
  {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (lastSyncedAt == value) return;
    lastSyncedAt = value;
  }
  saveToFile();
}

bool CloudSyncStore::isEnabled() const {
  std::lock_guard<std::mutex> lock(dataMutex);
  return enabled;
}

std::string CloudSyncStore::getServerUrl() const {
  std::lock_guard<std::mutex> lock(dataMutex);
  return serverUrl;
}

std::string CloudSyncStore::getToken() const {
  std::lock_guard<std::mutex> lock(dataMutex);
  return token;
}

int64_t CloudSyncStore::getLastSyncedAt() const {
  std::lock_guard<std::mutex> lock(dataMutex);
  return lastSyncedAt;
}