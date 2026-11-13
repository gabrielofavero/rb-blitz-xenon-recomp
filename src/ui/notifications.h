// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace rb_blitz::ui::notifications {

enum class Level { kInfo, kWarning, kError };
struct Notice {
  std::string key;
  Level level;
  std::string title;
  std::string description;
};

// SDK-free, shared by guest/input/network producers and the presentation thread.
// Time starts on presentation, so startup notices get their full reading time.
class Queue {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr float kEnterSeconds = 0.25f;
  static constexpr float kExitSeconds = 0.35f;
  static constexpr size_t kCapacity = 16;
  struct Frame {
    Notice notice;
    float visibility;
  };

  void Clear() {
    std::lock_guard lock(mutex_);
    pending_.clear();
    active_.reset();
    recent_.clear();
  }

  void Post(Notice notice, Clock::time_point now = Clock::now()) {
    if (notice.key.empty() || notice.title.empty() || notice.description.empty()) return;
    std::lock_guard lock(mutex_);
    if (active_ && active_->notice.key == notice.key) {
      if (!Same(active_->notice, notice) || active_->dismissed) {
        active_ = Active{std::move(notice), now, {}};
      }
      return;
    }
    for (auto& pending : pending_) {
      if (pending.key == notice.key) {
        pending = std::move(notice);
        return;
      }
    }
    // Repeated autosave/transport failures must not flood the screen.
    while (!recent_.empty() && now - recent_.front().when >= std::chrono::seconds(30))
      recent_.pop_front();
    for (const auto& recent : recent_) {
      if (Same(recent.notice, notice)) return;
    }
    if (pending_.size() == kCapacity) {
      auto lowest = std::min_element(pending_.begin(), pending_.end(),
          [](const Notice& a, const Notice& b) { return a.level < b.level; });
      if (lowest->level > notice.level) return;
      pending_.erase(lowest);
    }
    pending_.push_back(std::move(notice));
  }

  void Dismiss(const std::string& key, Clock::time_point now = Clock::now()) {
    std::lock_guard lock(mutex_);
    std::erase_if(pending_, [&](const Notice& n) { return n.key == key; });
    // A resolved condition/new attempt may report the same failure again immediately.
    std::erase_if(recent_, [&](const Recent& r) { return r.notice.key == key; });
    if (active_ && active_->notice.key == key && !active_->dismissed)
      active_->dismissed = now;
  }

  std::optional<Frame> Tick(Clock::time_point now = Clock::now()) {
    std::lock_guard lock(mutex_);
    if (active_ && Visibility(*active_, now) < 0.0f) {
      if (!active_->dismissed) recent_.push_back({active_->notice, now});
      if (recent_.size() > 32) recent_.pop_front();
      active_.reset();
    }
    if (!active_ && !pending_.empty()) {
      // Errors go first; preserve FIFO order among notices of the same severity.
      auto next = std::max_element(pending_.begin(), pending_.end(),
          [](const Notice& a, const Notice& b) { return a.level < b.level; });
      active_ = Active{std::move(*next), now, {}};
      pending_.erase(next);
    }
    if (!active_) return std::nullopt;
    return Frame{active_->notice, Visibility(*active_, now)};
  }

 private:
  struct Active {
    Notice notice;
    Clock::time_point since;
    std::optional<Clock::time_point> dismissed;
  };
  struct Recent { Notice notice; Clock::time_point when; };
  static bool Same(const Notice& a, const Notice& b) {
    return a.key == b.key && a.level == b.level && a.title == b.title &&
           a.description == b.description;
  }
  static float Visibility(const Active& active, Clock::time_point now) {
    const float age = std::chrono::duration<float>(now - active.since).count();
    const float duration = active.notice.level == Level::kError ? 10.0f :
                           active.notice.level == Level::kWarning ? 8.0f : 6.0f;
    float remaining = duration - age;
    if (active.dismissed)
      remaining = std::min(remaining, kExitSeconds -
          std::chrono::duration<float>(now - *active.dismissed).count());
    if (remaining <= 0.0f) return -1.0f;
    const float linear = std::clamp(std::min(age / kEnterSeconds,
                                           remaining / kExitSeconds), 0.0f, 1.0f);
    return linear * linear * (3.0f - 2.0f * linear);
  }
  std::mutex mutex_;
  std::deque<Notice> pending_;
  std::optional<Active> active_;
  std::deque<Recent> recent_;
};

inline Queue& SharedQueue() {
  static Queue queue;
  return queue;
}
inline void Post(std::string key, Level level, std::string title, std::string description) {
  SharedQueue().Post({std::move(key), level, std::move(title), std::move(description)});
}
inline void Dismiss(const std::string& key) { SharedQueue().Dismiss(key); }

}  // namespace rb_blitz::ui::notifications
