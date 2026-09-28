#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// A single upload of reading statistics to the Le Biblioteq ingest endpoint,
// run once per awake session after a reading page has stayed still for five
// seconds (battery-friendly: no periodic polling or retries). Runs on its own
// FreeRTOS task; the main task intentionally pauses reader controls only while
// the framebuffer is lent to TLS.
class CloudSyncService {
 public:
  enum class Status {
    Disabled,  // feature off or config incomplete
    Idle,      // enabled, waiting for a reader-idle opportunity
    Syncing,   // the upload is in progress
    Synced,    // upload succeeded; lastSyncedAt is fresh
    Failed,    // upload failed or saved Wi-Fi was unavailable
  };

  static CloudSyncService& getInstance();

  CloudSyncService(const CloudSyncService&) = delete;
  CloudSyncService& operator=(const CloudSyncService&) = delete;

  // Main-task accessors for UI rendering. getStatusLabel() returns a tr()-
  // sourced, stack-safe string ready for drawText.
  Status getStatus() const { return status.load(std::memory_order_acquire); }
  bool isComplete() const { return completed.load(std::memory_order_acquire); }
  bool didWifiJoinFail() const { return wifiJoinFailed.load(std::memory_order_acquire); }
  std::string getStatusLabel() const;
  // True when the user has enabled the feature and server+token are present.
  bool isConfigured() const;

  // Start the one-shot HTTPS worker after visible feedback is painted and the
  // framebuffer is temporarily released for TLS.
  void start(bool waitForCompletion = false);

  // Reset join state immediately before the reader-idle WiFi.begin().
  void prepareWifiJoin();

  // Called from the Arduino Wi-Fi event callback. Definitive join failures
  // end the opportunistic attempt instead of waiting for the fallback timeout.
  void notifyWifiDisconnected(uint8_t reason);

 private:
  CloudSyncService() = default;

  void runLoop();
  static void taskTrampoline(void* param);

  // Performs one upload. Returns true on success.
  bool upload();

  // Written only on the background task; read on the main task.
  std::atomic<Status> status{Status::Disabled};
  std::atomic<bool> completed{true};
  std::atomic<bool> wifiJoinFailed{false};
  std::atomic<uint32_t> wifiJoinStartedAt{0};
};

#define CLOUD_SYNC_SERVICE CloudSyncService::getInstance()