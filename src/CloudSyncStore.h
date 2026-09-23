#pragma once

#include <PersistableStore.h>

#include <cstdint>
#include <mutex>
#include <string>

// Device credential stays separate from the public settings API. As with saved
// Wi-Fi passwords, SD obfuscation only prevents casual plaintext disclosure;
// anyone with the SD card AND device can recover it. Revoke the token in the app
// if the device or its card is lost.
class CloudSyncStore final : public PersistableStore<CloudSyncStore> {
  friend class PersistableStore<CloudSyncStore>;

 public:
  // Getters take dataMutex and return by value: the sync task reads these
  // while the main task can be writing them, so a torn std::string read is
  // not acceptable. Copies are tiny (config strings).
  bool isEnabled() const;
  std::string getServerUrl() const;
  std::string getToken() const;
  int64_t getLastSyncedAt() const;

  bool setEnabled(bool value);
  bool setServerUrl(const std::string& value);
  bool setToken(const std::string& value);
  void setLastSyncedAt(int64_t value);

  static const char* getFilePath() { return "/.crosspoint/cloud_sync.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  bool enabled = false;
  std::string serverUrl;
  std::string token;
  int64_t lastSyncedAt = 0;

  // Guards the plain-data members above. The cloud sync task reads them on the
  // upload path while the main task (settings UI, loadFromFile) writes them, so
  // std::string members must not be touched unlocked from either side.
  //
  // Lock ordering is always storeMutex -> dataMutex: toJson()/fromJson() run
  // under storeMutex (held by saveToFile/loadFromFile) and only then take
  // dataMutex. Mutators therefore RELEASE dataMutex BEFORE calling
  // saveToFile(), never the reverse order (would deadlock against
  // loadFromFile on the main task).
  mutable std::mutex dataMutex;
};

#define CLOUD_SYNC_STORE CloudSyncStore::getInstance()
