#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// Background periodic upload of reading statistics to the Le Biblioteq ingest
// endpoint. Runs on its own FreeRTOS task so the main loop is never blocked by
// a slow HTTPS request. Owns no render state; the UI polls getStatus()/status
// strings on the render path.
class CloudSyncService {
 public:
  enum class Status {
    Disabled,  // feature off or config incomplete
    Idle,      // enabled, nothing due yet
    Syncing,   // an upload is in progress
    Synced,    // last upload succeeded; lastSyncedAt is fresh
    Failed,    // last upload failed; will retry on RETRY_MS
  };

  static CloudSyncService& getInstance();

  CloudSyncService(const CloudSyncService&) = delete;
  CloudSyncService& operator=(const CloudSyncService&) = delete;

  // Called on the main task after a reading session is persisted so an upload
  // is due immediately (not waiting out the interval).
  void markDataChanged();

  // Main-task accessors for UI rendering. getStatusLabel() returns a tr()-
  // sourced, stack-safe string ready for drawText.
  Status getStatus() const { return status.load(); }
  std::string getStatusLabel() const;
  // True when the user has enabled the feature and server+token are present.
  bool isConfigured() const;

  // Starts the background task if the feature is enabled and configured. Safe
  // to call from setup(); no-ops if the task already exists.
  void start();

  // Non-blocking: returns without doing work if a sync is not currently due.
  // Intended for the background task; exposed for diagnostics.
  bool trySyncIfDue();

  static constexpr uint32_t POLL_MS = 10000;

 private:
  CloudSyncService() = default;

  void runLoop();
  static void taskTrampoline(void* param);

  // Performs one upload. Returns true on success.
  bool upload();

  // Runtime policy state (all written only on the background task, read on the
  // main task). Module::markDataChanged is safe from either task: it only sets
  // an atomic under a lock-free flag.
  std::atomic<Status> status{Status::Disabled};
  std::atomic<bool> dataChanged{false};
  uint32_t lastAttemptMs = 0;
};

#define CLOUD_SYNC_SERVICE CloudSyncService::getInstance()