// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace rb_blitz::shell_music {

// Host-only preparation with a bounded handoff. Taking a result never waits for
// I/O; publishing devices and changing the live pool belong to the caller.
template <typename T>
class MenuMusicQueue {
 public:
  ~MenuMusicQueue() { Stop(); }

  void Start(size_t begin, size_t end,
             std::function<std::optional<T>(size_t)> prepare) {
    std::lock_guard lock(mutex_);
    if (started_ || stopped_) return;
    started_ = true;
    worker_ = std::jthread([this, begin, end, prepare = std::move(prepare)] {
      for (size_t i = begin; i < end; ++i) {
        {
          std::unique_lock lock(mutex_);
          wake_.wait(lock, [this] { return stopped_ || ready_.size() < 2; });
          if (stopped_) break;
        }
        auto result = prepare(i);
        std::lock_guard lock(mutex_);
        if (stopped_) break;
        if (result) ready_.push_back(std::move(*result));
      }
      std::lock_guard lock(mutex_);
      finished_ = true;
    });
  }

  std::optional<T> Take() {
    std::lock_guard lock(mutex_);
    if (stopped_ || ready_.empty()) return std::nullopt;
    T result = std::move(ready_.front());
    ready_.pop_front();
    wake_.notify_one();
    return result;
  }

  // Nonblocking, including while a mount is in progress. The worker owns only
  // host data, so it can finish that mount without accessing a closing runtime.
  void Stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    wake_.notify_all();
  }

  bool Finished() const {
    std::lock_guard lock(mutex_);
    return finished_ && ready_.empty();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<T> ready_;
  bool started_ = false, stopped_ = false, finished_ = false;
  // Destroyed first: joins before the mutex, queue and condition variable die.
  std::jthread worker_;
};

}  // namespace rb_blitz::shell_music
