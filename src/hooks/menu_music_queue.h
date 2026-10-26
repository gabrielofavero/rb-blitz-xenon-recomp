// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

namespace rb_blitz::shell_music {

// Demand-driven host preparation. No library walk runs ahead of the consumer.
// A request stays deduplicated until its result is consumed, including in-flight
// I/O. Devices are prepared outside the mutex and published by the guest owner.
template <typename T>
class MenuMusicQueue {
 public:
  ~MenuMusicQueue() { Stop(); }

  void Start(std::function<std::optional<T>(size_t)> prepare) {
    std::lock_guard lock(mutex_);
    if (started_ || stopped_) return;
    started_ = true;
    worker_ = std::jthread([this, prepare = std::move(prepare)] {
      for (;;) {
        size_t key;
        {
          std::unique_lock lock(mutex_);
          wake_.wait(lock, [this] {
            return stopped_ || (!paused_ && !requests_.empty() && ready_.size() < 2);
          });
          if (stopped_) break;
          key = requests_.front();
          requests_.pop_front();
        }
        auto result = prepare(key);
        {
          std::lock_guard lock(mutex_);
          if (stopped_) break;
          if (result) ready_.push_back({key, std::move(*result)});
          else outstanding_.erase(key);
        }
      }
      // Retire unpublished devices on this host worker, not on a closing UI
      // thread. All callbacks own their host-only snapshots.
      std::deque<std::pair<size_t, T>> discarded;
      {
        std::lock_guard lock(mutex_);
        discarded.swap(ready_);
        requests_.clear();
        outstanding_.clear();
      }
    });
  }

  bool Request(size_t key, bool priority = false) {
    std::lock_guard lock(mutex_);
    if (!started_ || stopped_ || outstanding_.contains(key) || outstanding_.size() >= 2) return false;
    outstanding_.insert(key);
    if (priority) requests_.push_front(key); else requests_.push_back(key);
    wake_.notify_one();
    return true;
  }

  std::optional<T> Take() {
    std::lock_guard lock(mutex_);
    if (stopped_ || ready_.empty()) return std::nullopt;
    auto item = std::move(ready_.front());
    ready_.pop_front();
    outstanding_.erase(item.first);
    wake_.notify_one();
    return std::move(item.second);
  }

  void Pause(bool paused) {
    std::lock_guard lock(mutex_);
    paused_ = paused;
    wake_.notify_all();
  }
  bool Idle() const {
    std::lock_guard lock(mutex_);
    return outstanding_.empty();
  }
  // Does not join an in-flight mount. The destructor joins before members die.
  void Stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    wake_.notify_all();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<size_t> requests_;
  std::deque<std::pair<size_t, T>> ready_;
  std::set<size_t> outstanding_;
  bool started_ = false, stopped_ = false, paused_ = false;
  std::jthread worker_;
};
}  // namespace rb_blitz::shell_music
