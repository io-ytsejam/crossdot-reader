#pragma once

#include <cstdint>

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  enum class Mode : uint8_t { Wait, Try };

  explicit RenderLock();
  explicit RenderLock(Mode mode);
  explicit RenderLock(Activity&);  // unused for now, but keep for compatibility
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  [[nodiscard]] bool locked() const { return isLocked; }
  void unlock();
  static bool peek();
};
