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
  // The installed HUD's prompt is a 54-unit disc in a 480-unit-high scene.
  // Pause always starts at the safe left edge; never reserve a slot before it.
  const bool portrait = width < height;
  const float preferred_side = portrait ? 44.f : std::max(height * .1125f, 44.f);
  const float side = std::max(0.f, std::min({preferred_side,
      safe.width - margin * 2, safe.height - margin * 2}));
  const float x = safe.x + margin;
  const float y = portrait ? safe.y + margin : std::clamp(height * (59.3836f / 480.f) - side / 2,
      safe.y + margin, safe.y + std::max(margin, safe.height - margin - side));
  return {width, height, {x, y, side, side}};
}
Rect LanePowerupSlot(Rect viewport) {
  if (!(viewport.width > 0 && viewport.height > 0)) return {};
  // The ready icon is centered under the active lane. Its proportions are
  // relative to the viewport height so the hit area follows portrait cropping.
  const float width = std::min(viewport.width, viewport.height * .136f);
  return {viewport.x + (viewport.width - width) * .5f,
          viewport.y + viewport.height * .82f, width, viewport.height * .116f};
}
void PowerupTarget::SetTarget(Rect bounds, bool available) {
  bounds_ = bounds;
  available_ = available && bounds.width > 0 && bounds.height > 0;
  if (!available_) for (auto& [key, press] : presses_) press.moved = true;
}
void PowerupTarget::Cancel() { *this = {}; }
PointerResult PowerupTarget::Observe(const FingerEvent& event) {
  const auto key = std::make_pair(event.device, event.finger);
  auto it = presses_.find(key);
  const bool finite = std::isfinite(event.x) && std::isfinite(event.y);
  if (event.phase == Phase::kDown && it == presses_.end()) {
    if (!finite || !available_ || !bounds_.Contains(event.x, event.y) || presses_.size() >= 10) return {};
    presses_.emplace(key, Press{bounds_, event.x, event.y});
    return {true, false};
  }
  if (it == presses_.end()) return {};
  auto& press = it->second;
  press.moved |= !finite || !press.bounds.Contains(event.x, event.y) ||
      std::hypot(event.x - press.x, event.y - press.y) > kTapSlop;
  const bool deploy = event.phase == Phase::kUp && available_ && !press.moved;
  if (event.phase == Phase::kUp || event.phase == Phase::kCancel) presses_.erase(it);
  // Only one release from simultaneous contacts may deploy the current ready state.
  if (deploy) { available_ = false; for (auto& [key, other] : presses_) other.moved = true; }
  return {true, deploy};
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
        Finger{x, y, 0, ++next_token_, layout_.pause.Contains(x, y),
               false, false, layout_.pause});
    if (inserted && !it->second.pause) {
      const auto action = x < layout_.width * .5f ? Action::kFirstNote : Action::kSecondNote;
      Emit(action);
      gameplay_.push_back({GameplayKind::kPress, it->second.token, action, false, event.timestamp_ns});
    }
    return;
  }
  const auto it = fingers_.find(key);
  if (it == fingers_.end()) return;  // A finger held across a boundary cannot rearm.
  auto& finger = it->second;
  const float dx = x - finger.x, dy = y - finger.y;
  finger.travel = std::max(finger.travel, std::hypot(dx, dy));
  if (finger.pause) {
    const auto& rect = finger.button;
    finger.moved |= !rect.Contains(x, y) || finger.travel > kTapSlop;
    if (event.phase == Phase::kUp) {
      const bool pause = finger.pause && !finger.moved;
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
    const bool left = (dx > 0) == inverted_lane_swipe_;
    const auto action = left ? Action::kMoveLaneLeft : Action::kMoveLaneRight;
    Emit(action);
    gameplay_.push_back({GameplayKind::kSwipe, finger.token, action, true, event.timestamp_ns});
  }
  if (event.phase == Phase::kUp) {
    gameplay_.push_back({GameplayKind::kRelease, finger.token,
        finger.x < layout_.width * .5f ? Action::kFirstNote : Action::kSecondNote, finger.swiped,
        event.timestamp_ns});
    fingers_.erase(it);
  }
}
std::vector<GameplayEvent> Gestures::DrainGameplay() {
  std::vector<GameplayEvent> events;
  events.swap(gameplay_);
  // A swipe already recognized while waiting for a guest frame must not also
  // strike a note. Keep the press identity so its swipe/release still reconcile.
  for (auto& event : events) {
    if (event.kind != GameplayKind::kPress) continue;
    event.swiped = std::any_of(events.begin(), events.end(), [&](const auto& other) {
      return other.kind == GameplayKind::kSwipe && other.token == event.token;
    });
  }
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
void TiltDetector::Reset() { *this = {}; }
bool TiltDetector::Observe(uint64_t now, float x, float y, float z) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { Reset(); return false; }
  const float magnitude = std::hypot(std::hypot(x, y), z);
  // A brief jolt can interrupt a valid tilt. Ignore the contaminated sample
  // and let the sample-gap check discard the baseline after sustained movement.
  if (magnitude < 9.80665f * .8f || magnitude > 9.80665f * 1.2f) return false;
  constexpr float kDegrees = 57.2957795f;
  const float pitch = std::atan2(z, std::hypot(x, y)) * kDegrees;
  if (!initialized_ || now <= last_ || now - last_ > 250'000'000) {
    Reset();
    initialized_ = true;
    pitch_ = baseline_ = pitch;
    last_ = settled_since_ = now;
    return false;
  }
  const float dt = float(now - last_) / 1e9f;
  last_ = now;
  pitch_ += dt / (.04f + dt) * (pitch - pitch_);
  const float delta = pitch_ - baseline_;
  if (!calibrated_) {
    if (std::abs(delta) > 3.f) { baseline_ = pitch_; settled_since_ = now; }
    calibrated_ = now - settled_since_ >= 150'000'000;
    return false;
  }
  if (!armed_) {
    // Holding the tipped pose or its rebound must not deploy again. Keep the
    // original playing angle until the device has returned for 150 ms.
    if (std::abs(delta) > 8.f) settled_since_ = now;
    if (now >= cooldown_ && now - settled_since_ >= 150'000'000 && std::abs(delta) <= 8.f) {
      armed_ = true;
      baseline_ = pitch_;
    }
    return false;
  }
  if (delta < 6.f) {
    movement_since_ = 0;
    // Follow gradual grip changes while retaining a reference for a quick tilt.
    baseline_ += dt / (.5f + dt) * delta;
    return false;
  }
  if (!movement_since_) movement_since_ = now;
  const auto elapsed = now - movement_since_;
  if (elapsed > 450'000'000) {
    baseline_ = pitch_;
    movement_since_ = 0;
    return false;
  }
  // A low playing angle leaves less travel before the screen is flat.
  const float threshold = std::clamp((90.f - baseline_) * .7f, 12.f, 22.f);
  if (now < cooldown_ || elapsed < 80'000'000 || delta < threshold) return false;
  armed_ = false;
  movement_since_ = 0;
  settled_since_ = now;
  cooldown_ = now + 1'000'000'000;
  return true;
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
  float magnitude2 = 0;
  const float alpha = dt / (.3f + dt);
  for (size_t axis = 0; axis < 3; ++axis) {
    gravity_[axis] += alpha * (sample[axis] - gravity_[axis]);
    const float linear = sample[axis] - gravity_[axis];
    magnitude2 += linear * linear;
  }
  // Lower activation effort while retaining gravity filtering and rebound protection.
  if (magnitude2 < 4.f * 4.f) armed_ = true;
  if (now < cooldown_ || !armed_ || magnitude2 < 8.f * 8.f) return false;
  armed_ = false;
  cooldown_ = now + 1'000'000'000;
  return true;
}
MotionMode ParseMotionMode(std::string_view token) {
  if (token == "tilt") return MotionMode::kTilt;
  if (token == "shake") return MotionMode::kShake;
  return MotionMode::kNone;
}
void MotionDetector::Reset() { *this = {}; }
bool MotionDetector::Observe(MotionMode mode, uint64_t now, float x, float y, float z) {
  if (mode != mode_) { Reset(); mode_ = mode; }
  if (mode == MotionMode::kTilt) return tilt_.Observe(now, x, y, z);
  if (mode == MotionMode::kShake) return shake_.Observe(now, x, y, z);
  return false;
}
bool MouseGate::Consume(bool is_touch, Phase phase, bool gameplay) {
  if (!is_touch) return false;
  if (phase == Phase::kDown && !menu_pending_.empty()) {
    menu_ = menu_pending_.front();
    menu_pending_.pop_front();
  }
  const bool consume = gameplay || down_ || menu_;
  if (phase == Phase::kDown && (gameplay || down_)) down_ = true;
  if (phase == Phase::kUp || phase == Phase::kCancel) down_ = menu_ = false;
  return consume;
}
}  // namespace rb_blitz::input::touch
