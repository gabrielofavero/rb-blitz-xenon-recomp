// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace rb_blitz::input {
struct PortraitPanelState {
  uint32_t directory = 0;
  bool active = false;
  std::string screen;
};

// UI polling and panel drawing can run on different guest threads. Draw-time
// eligibility must use one published snapshot, not the polling thread's locals.
class PortraitPanelPublisher {
 public:
  void Publish(uint32_t directory, bool active, std::string_view screen) {
    std::lock_guard lock(mutex_);
    state_.directory = directory;
    state_.active = active;
    if (state_.screen != screen) state_.screen = screen;
  }
  PortraitPanelState Snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
  }
 private:
  mutable std::mutex mutex_;
  PortraitPanelState state_;
};
}
