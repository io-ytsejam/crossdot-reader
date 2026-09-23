#include "CloudSyncStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

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
  token = extractPassword(doc, needsResave);  // honours "token_obf", legacy plaintext
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
  {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (serverUrl == value) return true;
    serverUrl = value;
  }
  return saveToFile();
}

bool CloudSyncStore::setToken(const std::string& value) {
  {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (token == value) return true;
    token = value;
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