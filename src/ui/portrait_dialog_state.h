// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rb_blitz::ui::portrait_dialog {

inline std::string PlainText(std::string_view text) {
  std::string result;
  bool icon = false;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '<') {
      const auto end = text.find('>', i);
      if (end == text.npos) { if (!icon) result.append(text.substr(i)); break; }
      auto tag = std::string(text.substr(i + 1, end - i - 1));
      for (char& ch : tag) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
      if (tag == "alt" || tag == "alt1") icon = true;
      else if (tag == "/alt" || tag == "/alt1") icon = false;
      i = end + 1;
    } else {
      if (!icon) result += text[i];
      ++i;
    }
  }
  const auto first = result.find_first_not_of(" \t\r\n");
  if (first == result.npos) return {};
  return result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
}

struct Snapshot {
  uint64_t generation = 0;
  uint32_t directory = 0;
  std::string body, caption;
  bool active = false, pending = false;
};

// The guest owns opening, closing and dispatch. The host holds copied strings
// and a generation-tagged intent; no guest pointer is dereferenced by ImGui.
class State {
 public:
  void Reset() {
    std::lock_guard lock(mutex_);
    ++generation_;
    labels_.clear();
    screen_ = panel_ = directory_ = 0;
    open_ = pending_ = eligible_ = false;
  }
  void Open() {
    std::lock_guard lock(mutex_);
    ++generation_;
    open_ = true;
    pending_ = false;
  }
  void Close() {
    std::lock_guard lock(mutex_);
    open_ = pending_ = false;
    ++generation_;
  }
  void Observe(uint32_t screen, uint32_t panel, uint32_t directory, bool eligible) {
    std::lock_guard lock(mutex_);
    if (screen_ != screen || panel_ != panel || directory_ != directory || eligible_ != eligible) {
      ++generation_;
      pending_ = false;
    }
    screen_ = screen; panel_ = panel; directory_ = directory; eligible_ = eligible;
  }
  void Label(uint32_t directory, bool body, std::string_view text) {
    if (!directory || text.size() > 16384) return;
    auto plain = PlainText(text);
    std::lock_guard lock(mutex_);
    if (!labels_.contains(directory) && labels_.size() >= 32) labels_.clear();
    auto& value = body ? labels_[directory].body : labels_[directory].caption;
    if (value != plain) {
      value = std::move(plain);
      if (directory == directory_) { ++generation_; pending_ = false; }
    }
  }
  Snapshot Current() const {
    std::lock_guard lock(mutex_);
    Snapshot result{generation_, directory_, {}, {}, false, pending_};
    const auto found = labels_.find(directory_);
    if (found != labels_.end()) {
      result.body = found->second.body;
      result.caption = found->second.caption;
    }
    result.active = open_ && eligible_ && screen_ && panel_ && directory_ && !result.body.empty();
    if (result.caption.empty()) result.caption = "Continue";
    return result;
  }
  bool Confirm(uint64_t generation) {
    std::lock_guard lock(mutex_);
    if (!open_ || !eligible_ || !screen_ || !panel_ || !directory_ ||
        generation != generation_ || pending_) return false;
    const auto found = labels_.find(directory_);
    if (found == labels_.end() || found->second.body.empty()) return false;
    pending_ = true;
    return true;
  }
  bool TakeConfirm() {
    std::lock_guard lock(mutex_);
    if (!open_ || !eligible_ || !pending_) return false;
    pending_ = false;
    return true;
  }
 private:
  struct Labels { std::string body, caption; };
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, Labels> labels_;
  uint64_t generation_ = 0;
  uint32_t screen_ = 0, panel_ = 0, directory_ = 0;
  bool open_ = false, eligible_ = false, pending_ = false;
};
}  // namespace rb_blitz::ui::portrait_dialog
