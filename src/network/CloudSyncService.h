#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// A single upload of reading statistics to the Le Biblioteq ingest endpoint,
// run once per boot when the reader is awake and on Wi-Fi (battery-friendly: no
// periodic polling or retries during a session). Runs on its own FreeRTOS task
// so the main loop is never blocked by a slow HTTPS request. Owns no render
// state; the UI polls getStatus()/getStatusLabel() on the render path.
class CloudSyncService {
 public:
  enum class Status {
    Disabled,  // feature off or config incomplete
    Idle,      // enabled, waiting for Wi-Fi at boot
    Syncing,   // the upload is in progress
    Synced,    // upload succeeded; lastSyncedAt is fresh
    Failed,    // upload failed or no Wi-Fi within the boot window
  };

  static CloudSyncService& getInstance();

  CloudSyncService(const CloudSyncService&) = delete;
  CloudSyncService& operator=(const CloudSyncService&) = delete;

  // Main-task accessors for UI rendering. getStatusLabel() returns a tr()-
  // sourced, stack-safe string ready for drawText.
  Status getStatus() const { return status.load(); }
  std::string getStatusLabel() const;
  // True when the user has enabled the feature and server+token are present.
  bool isConfigured() const;

  // At boot, wait before allocating display/fonts/activities: TLS and the UI
  // cannot safely share the C3's heap at their peak. Settings can still start
  // asynchronously. One attempt per boot; Wi-Fi and HTTP waits are bounded.
  void start(bool waitForCompletion = false);

  // How long to keep waiting at boot for Wi-Fi before giving up this session.
  static constexpr uint32_t BOOT_WIFI_TIMEOUT_MS = 60U * 1000U;

 private:
  CloudSyncService() = default;

  void runLoop();
  static void taskTrampoline(void* param);

  // Performs one upload. Returns true on success.
  bool upload();

  // Written only on the background task; read on the main task.
  std::atomic<Status> status{Status::Disabled};
  std::atomic<bool> completed{true};
};

#define CLOUD_SYNC_SERVICE CloudSyncService::getInstance()