// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "settings/display_runtime.h"
#include <rex/ui/ui_system.h>
#include <rex/logging/log_category.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace rb_blitz::settings {

// The diagnostic chord changes only native display state. Timeouts hold weak
// state and a generation, so a queued callback cannot outlive or undo a later trial.
class DisplayTrial {
 public:
  explicit DisplayTrial(rex::ui::IUISystem& ui,
      std::chrono::steady_clock::duration timeout = std::chrono::seconds(15))
      : state_(std::make_shared<State>(ui)), timeout_(timeout),
        worker_([state = state_](std::stop_token stop) {
          std::unique_lock lock(state->mutex);
          while (!stop.stop_requested()) {
            state->wake.wait(lock, stop, [&] {
              return state->closed || (state->active && !state->timeout_queued);
            });
            if (stop.stop_requested() || state->closed) break;
            const auto generation = state->generation;
            const auto deadline = state->deadline;
            if (state->wake.wait_until(lock, stop, deadline, [&] {
                  return state->closed || !state->active || state->generation != generation;
                })) continue;
            if (stop.stop_requested()) break;
            state->timeout_queued = true;
            const std::weak_ptr<State> weak = state;
            lock.unlock();
            state->ui.PostTask([weak, generation] {
              if (const auto current = weak.lock()) {
                std::string error;
                if (!Restore(current, generation, &error))
                  REXLOG_WARN("settings_display: diagnostic trial rollback failed: {}", error);
              }
            });
            lock.lock();
          }
        }) {}
  ~DisplayTrial() { Close(nullptr); }
  DisplayTrial(const DisplayTrial&) = delete;
  DisplayTrial& operator=(const DisplayTrial&) = delete;

  bool Cycle(std::string* error) {
    if (!state_->ui.IsUIThread()) {
      if (error) *error = "display trials require the UI thread";
      return false;
    }
    {
      std::lock_guard lock(state_->mutex);
      if (state_->closed) {
        if (error) *error = "display trial is closed";
        return false;
      }
    }
    const auto current = RuntimeDisplayMode(state_->ui);
    if (current.empty()) {
      if (error) *error = "native display state is unavailable";
      return false;
    }
    const auto next = current == "windowed" ? "borderless" :
                      current == "borderless" ? "fullscreen" : "windowed";
    if (!ApplyTransientDisplay(state_->ui, next, error)) return false;
    {
      std::lock_guard lock(state_->mutex);
      if (!state_->active) state_->original = current;
      state_->active = true;
      state_->timeout_queued = false;
      state_->deadline = std::chrono::steady_clock::now() + timeout_;
      ++state_->generation;
    }
    state_->wake.notify_all();
    return true;
  }

  bool Close(std::string* error) {
    std::string restore;
    {
      std::lock_guard lock(state_->mutex);
      if (state_->closed) return true;
      state_->closed = true;
      ++state_->generation;
      if (state_->active) restore = state_->original;
      state_->active = false;
    }
    worker_.request_stop();
    state_->wake.notify_all();
    if (worker_.joinable()) worker_.join();
    return restore.empty() || ApplyTransientDisplay(state_->ui, restore, error);
  }

  bool active() const {
    std::lock_guard lock(state_->mutex);
    return state_->active;
  }

 private:
  struct State {
    explicit State(rex::ui::IUISystem& value) : ui(value) {}
    rex::ui::IUISystem& ui;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::chrono::steady_clock::time_point deadline;
    uint64_t generation = 0;
    bool active = false, timeout_queued = false, closed = false;
    std::string original;
  };
  static bool Restore(const std::shared_ptr<State>& state, uint64_t generation,
                      std::string* error) {
    std::string original;
    {
      std::lock_guard lock(state->mutex);
      if (state->closed || !state->active || state->generation != generation) return true;
      original = state->original;
    }
    const bool restored = ApplyTransientDisplay(state->ui, original, error);
    if (restored) {
      std::lock_guard lock(state->mutex);
      if (state->generation == generation) state->active = false;
    }
    return restored;
  }
  std::shared_ptr<State> state_;
  std::chrono::steady_clock::duration timeout_;
  std::jthread worker_;
};

}  // namespace rb_blitz::settings
