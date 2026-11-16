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
  const float gap = std::max(8.f, height * .025f);
  // The installed HUD's prompt is a 54-unit disc in a 480-unit-high scene.
  // Pause always starts at the safe left edge; never reserve a slot before it.
  const float side = std::max(0.f, std::min({std::max(height * .1125f, 44.f),
      (safe.width - margin * 2 - gap) / 2, safe.height - margin * 2}));
  const float x = safe.x + margin;
  const float y = std::clamp(height * (59.3836f / 480.f) - side / 2,
      safe.y + margin, safe.y + std::max(margin, safe.height - margin - side));
  return {width, height, {x, y, side, side}, {x + side + gap, y, side, side}};
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
void Gestures::SetPowerupAvailable(bool available) { powerup_available_ = available; }
void Gestures::SetLanePowerup(Rect rect) { lane_powerup_ = rect; }
void Gestures::DeployPowerup() {
  if (!active_ || pause_requested_ || !powerup_available_ || gameplay_.size() >= 254) return;
  const auto token = ++next_token_;
  gameplay_.push_back({GameplayKind::kPress, token, Action::kPowerup});
  gameplay_.push_back({GameplayKind::kRelease, token, Action::kPowerup});
}
void Gestures::Cancel() {
  gameplay_.clear();
  gameplay_.push_back({GameplayKind::kCancel});
  active_ = false;
  pause_requested_ = false;
  lane_powerup_ = {};
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
      if (!it->second.pause && !it->second.powerup) gameplay_.push_back({GameplayKind::kRelease, it->second.token,
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
    const Rect button = layout_.powerup.Contains(x, y) ? layout_.powerup : lane_powerup_;
    const auto [it, inserted] = fingers_.try_emplace(key,
        Finger{x, y, 0, ++next_token_, layout_.pause.Contains(x, y),
               powerup_available_ && button.Contains(x, y), false, false,
               layout_.pause.Contains(x, y) ? layout_.pause : button});
    if (inserted && !it->second.pause && !it->second.powerup) {
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
  if (finger.pause || finger.powerup) {
    const auto& rect = finger.button;
    finger.moved |= !rect.Contains(x, y) || finger.travel > kTapSlop;
    if (event.phase == Phase::kUp) {
      const bool pause = finger.pause && !finger.moved;
      const bool powerup = finger.powerup && !finger.moved && powerup_available_;
      fingers_.erase(it);
      if (pause) {
        // Pause owns this frame. Other fingers must not leave notes queued for resume.
        Cancel();
        active_ = true;
        Emit(Action::kPause);
        pause_requested_ = true;
      } else if (powerup) {
        // Use the same native action boundary as a controller's Power-up.
        DeployPowerup();
      }
    }
    return;
  }
  if (!finger.swiped && std::abs(dx) >= kSwipeDistance && std::abs(dx) >= std::abs(dy) * 1.5f) {
    finger.swiped = true;
    const bool left = (dx > 0) == inverted_lane_swipe_;
    const auto action = left ? Action::kMoveLaneLeft : Action::kMoveLaneRight;
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
void ShakeDetector::Reset() { *this = {}; }
bool ShakeDetector::Observe(uint64_t now, float x, float y, float z) {
  const std::array<float, 3> sample{x, y, z};
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { Reset(); return false; }
  if (!last_ || now <= last_ || now - last_ > 250'000'000) {
    Reset(); gravity_ = sample; last_ = now; return false;
  }
  const float dt = float(now - last_) / 1e9f;
  last_ = now;
  std::array<float, 3> linear;
  float magnitude2 = 0;
  // A time-based low pass removes gravity at any device orientation.
  const float alpha = dt / (.3f + dt);
  for (size_t axis = 0; axis < 3; ++axis) {
    gravity_[axis] += alpha * (sample[axis] - gravity_[axis]);
    linear[axis] = sample[axis] - gravity_[axis];
    magnitude2 += linear[axis] * linear[axis];
  }
  // A brisk tilt or flick is enough; no reversal or repeated shaking is needed.
  // Require a quieter sample before rearming so the same motion cannot deploy twice.
  if (magnitude2 < 6.f * 6.f) armed_ = true;
  if (now < cooldown_ || !armed_ || magnitude2 < 12.f * 12.f) return false;
  armed_ = false;
  cooldown_ = now + 1'000'000'000;
  return true;
}
bool MouseGate::Consume(bool is_touch, Phase phase, bool gameplay) {
  if (!is_touch) return false;
  const bool consume = gameplay || down_;
  if (phase == Phase::kDown && consume) down_ = true;
  if (phase == Phase::kUp || phase == Phase::kCancel) down_ = false;
  return consume;
}
}  // namespace rb_blitz::input::touch
