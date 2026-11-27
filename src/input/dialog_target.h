// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "input/native_menu.h"
#include <cmath>
#include <mutex>
#include <vector>

namespace rb_blitz::input {
struct DialogTargetContext {
  uint32_t screen = 0, focus = 0, panel = 0, directory = 0, scope = 0;
  bool available = false, error_popup = false;
  uint32_t poll_frame = 0;
  bool SameOwner(const DialogTargetContext& other) const {
    return screen == other.screen && focus == other.focus && panel == other.panel &&
        directory == other.directory && scope == other.scope &&
        available == other.available && error_popup == other.error_popup;
  }
};

// A modal's UI polling and native drawing may use different guest threads.
// Publish drawn button bounds with their owning context; never borrow a
// background menu target or keep a dismissed panel clickable.
class DialogTargetPublisher {
 public:
  void Context(DialogTargetContext context, uint32_t poll_frame) {
    std::lock_guard lock(mutex_);
    if (!context_.SameOwner(context)) targets_.clear();
    context.poll_frame = poll_frame;
    context_ = context;
  }
  DialogTargetContext Snapshot() const {
    std::lock_guard lock(mutex_);
    return context_;
  }
  void Capture(const DialogTargetContext& context, MenuBounds bounds, uint16_t button) {
    std::lock_guard lock(mutex_);
    if (!context.SameOwner(context_) || !context.available || !context.scope || !button ||
        !std::isfinite(bounds.left) || !std::isfinite(bounds.top) ||
        !std::isfinite(bounds.right) || !std::isfinite(bounds.bottom) ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
    if (int32_t(context_.poll_frame - context.poll_frame) > 1 ||
        (!targets_.empty() && int32_t(context.poll_frame - targets_.back().frame) < 0)) return;
    if (!targets_.empty() && targets_.back().frame != context.poll_frame) targets_.clear();
    targets_.push_back({context.screen, context.focus, context.poll_frame, bounds, button, context.scope});
  }
  uint16_t Hit(MenuPoint point, uint32_t screen, uint32_t focus,
               uint32_t scope, uint32_t frame) const {
    std::lock_guard lock(mutex_);
    if (!context_.available || context_.screen != screen || context_.focus != focus ||
        context_.scope != scope) return 0;
    return HitMenuHint(point, targets_, screen, focus, frame, scope);
  }
 private:
  mutable std::mutex mutex_;
  DialogTargetContext context_;
  std::vector<MenuHintTarget> targets_;
};
}
