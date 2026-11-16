// SPDX-License-Identifier: GPL-2.0-only
#include "input/touch_gestures.h"

#include <algorithm>
#include <cmath>

namespace rb_blitz::input::touch {
namespace {
constexpr float kTapSlop = 12;  // SDL window coordinates (points on iOS).
constexpr float kSwipeDistance = 24;
}
bool Rect::Contains(float px, float py) const {
  return width > 0 && height > 0 && px >= x && py >= y && px < x + width && py < y + height;
}
Layout MakeLayout(float width, float height, Rect safe) {
  if (width <= 0 || height <= 0 || !std::isfinite(width) || !std::isfinite(height)) return {};
  if (safe.width <= 0 || safe.height <= 0) safe = {0, 0, width, height};
  const float margin = std::min(12.0f, height * .025f);
  const float side = std::min({std::clamp(height * .09f, 44.0f, 60.0f),
                              safe.width - margin * 2, safe.height - margin * 2});
  return {width, height, {safe.x + safe.width - margin - side, safe.y + margin, side, side}};
}
void Gestures::SetLayout(Layout layout) {
  if (layout_ == layout) return;
  Cancel();
  layout_ = layout;
}
void Gestures::SetActive(bool active) {
  if (active_ == active) return;
  Cancel();
  active_ = active;
}
void Gestures::Cancel() {
  gameplay_.clear();
  gameplay_.push_back({GameplayKind::kCancel});
  active_ = false;
  pause_requested_ = false;
  fingers_.clear();
  pending_.fill(0);
  held_polls_.fill(0);
}
void Gestures::Emit(Action action) {
  auto& count = pending_[static_cast<size_t>(action)];
  count = std::min<unsigned>(count + 1, 8);
}
void Gestures::Observe(const FingerEvent& event) {
  if (!IsDirectTouch(event.device)) return;
  const auto key = std::make_pair(event.device, event.finger);
  auto cancel_finger = [&] {
    const auto it = fingers_.find(key);
    if (it != fingers_.end()) {
      if (!it->second.pause) gameplay_.push_back({GameplayKind::kRelease, it->second.token,
          it->second.x < layout_.width * .5f ? Action::kFirstNote : Action::kSecondNote, true});
      fingers_.erase(it);
    }
  };
  if (event.phase == Phase::kCancel) { cancel_finger(); return; }
  if (!active_ || pause_requested_ || layout_.width <= 0 || layout_.height <= 0) return;
  if (!std::isfinite(event.x) || !std::isfinite(event.y)) { cancel_finger(); return; }
  if (gameplay_.size() >= 256) { Cancel(); return; }
  const float x = event.x * layout_.width, y = event.y * layout_.height;
  if (event.phase == Phase::kDown) {
    if (fingers_.size() >= 10 || event.x < 0 || event.x > 1 || event.y < 0 || event.y > 1) return;
    const auto [it, inserted] = fingers_.try_emplace(key,
        Finger{x, y, 0, ++next_token_, layout_.pause.Contains(x, y)});
    if (inserted && !it->second.pause) {
      const auto action = x < layout_.width * .5f ? Action::kFirstNote : Action::kSecondNote;
      Emit(action);
      gameplay_.push_back({GameplayKind::kPress, it->second.token, action});
    }
    return;
  }
  const auto it = fingers_.find(key);
  if (it == fingers_.end()) return;  // A finger held across a boundary cannot rearm.
  auto& finger = it->second;
  const float dx = x - finger.x, dy = y - finger.y;
  finger.travel = std::max(finger.travel, std::hypot(dx, dy));
  if (finger.pause) {
    finger.moved |= !layout_.pause.Contains(x, y) || finger.travel > kTapSlop;
    if (event.phase == Phase::kUp) {
      const bool pause = !finger.moved;
      fingers_.erase(it);
      if (pause) {
        // Pause owns this frame. Other fingers must not leave notes queued for resume.
        Cancel();
        active_ = true;
        Emit(Action::kPause);
        pause_requested_ = true;
      }
    }
    return;
  }
  if (!finger.swiped && std::abs(dx) >= kSwipeDistance && std::abs(dx) >= std::abs(dy) * 1.5f) {
    finger.swiped = true;
    const auto action = dx > 0 ? Action::kMoveLaneLeft : Action::kMoveLaneRight;
    Emit(action);
    gameplay_.push_back({GameplayKind::kSwipe, finger.token, action, true});
  }
  if (event.phase == Phase::kUp) {
    gameplay_.push_back({GameplayKind::kRelease, finger.token,
        finger.x < layout_.width * .5f ? Action::kFirstNote : Action::kSecondNote, finger.swiped});
    fingers_.erase(it);
  }
}
std::vector<GameplayEvent> Gestures::DrainGameplay() {
  std::vector<GameplayEvent> events;
  events.swap(gameplay_);
  return events;
}
Actions Gestures::Sample() {
  Actions sample{};
  if (!active_) return sample;
  for (size_t i = 0; i < sample.size(); ++i) {
    if (held_polls_[i]) sample[i] = --held_polls_[i] != 0;
    else if (pending_[i]) {
      --pending_[i];
      // Start travels as pad state, matching the native menu's two-poll
      // button pulse. Notes and swipes use the guest's queued action edges.
      held_polls_[i] = i == static_cast<size_t>(Action::kPause) ? 2 : 1;
      sample[i] = true;
    }
  }
  return sample;
}
bool MouseGate::Consume(bool is_touch, Phase phase, bool gameplay) {
  if (!is_touch) return false;
  const bool consume = gameplay || down_;
  if (phase == Phase::kDown && consume) down_ = true;
  if (phase == Phase::kUp || phase == Phase::kCancel) down_ = false;
  return consume;
}
}  // namespace rb_blitz::input::touch
