// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string_view>
#include <utility>
#include <vector>
#include "profile/gameplay_remap.h"

namespace rb_blitz::input::touch {
using Action = profile::gameplay_remap::Action;
using Actions = std::array<bool, profile::gameplay_remap::kActionCount>;
// SDL reserves these IDs for finger events synthesized from a mouse or pen.
inline bool IsDirectTouch(uint64_t device) {
  return device != UINT64_MAX && device != UINT64_MAX - 1;
}

struct Rect {
  float x = 0, y = 0, width = 0, height = 0;
  bool Contains(float px, float py) const;
  bool operator==(const Rect&) const = default;
};
struct Layout {
  float width = 0, height = 0;
  Rect pause;
  bool operator==(const Layout&) const = default;
};
Layout MakeLayout(float width, float height, Rect safe_area);

enum class Phase { kDown, kMove, kUp, kCancel };
struct FingerEvent {
  uint64_t device = 0, finger = 0;
  Phase phase = Phase::kDown;
  float x = 0, y = 0;  // normalized full-screen coordinates
  uint64_t timestamp_ns = 0;
};

struct PointerResult { bool consumed = false, deploy = false; };
// The native lane artwork is a target for both mouse and direct touch. A press
// owns its original bounds until release; moving off or losing readiness cancels.
class PowerupTarget {
 public:
  void SetTarget(Rect bounds, bool available);
  PointerResult Observe(const FingerEvent& event);  // window coordinates in points
  void Cancel();
 private:
  struct Press { Rect bounds; float x, y; bool moved = false; };
  Rect bounds_;
  bool available_ = false;
  std::map<std::pair<uint64_t, uint64_t>, Press> presses_;
};

enum class GameplayKind { kPress, kSwipe, kRelease, kCancel };
struct GameplayEvent {
  GameplayKind kind;
  uint64_t token = 0;
  Action action = Action::kFirstNote;
  bool swiped = false;
  uint64_t timestamp_ns = 0;
};

// Only new contacts in the current sampling interval count. Captured contacts
// keep swallowing their synthetic mouse stream through a sampling boundary.
class CalibrationTaps {
 public:
  void Cancel() { target_ = 0; down_ = captured_ = false; pending_.clear(); }
  void SetTarget(uint32_t target) {
    if (target_ != target) { pending_.clear(); target_ = target; }
  }
  bool Observe(Phase phase, uint64_t timestamp) {
    if (phase == Phase::kDown) {
      if (down_) return captured_;
      down_ = true;
      captured_ = target_ != 0;
      if (captured_ && pending_.size() < 64) pending_.push_back(timestamp);
    }
    const bool consumed = captured_;
    if (phase == Phase::kUp || phase == Phase::kCancel) down_ = captured_ = false;
    return consumed;
  }
  std::vector<uint64_t> Drain(uint32_t target) {
    if (target != target_) return {};
    std::vector<uint64_t> result;
    result.swap(pending_);
    return result;
  }
 private:
  uint32_t target_ = 0;
  bool down_ = false, captured_ = false;
  std::vector<uint64_t> pending_;
};

// Notes strike immediately. Each touch keeps an identity through its release so
// only the wrong press belonging to a swipe can be forgiven by the guest seam.
class Gestures {
 public:
  void SetLayout(Layout layout);
  void SetActive(bool active);
  void SetPowerupAvailable(bool available);
  void SetInvertedLaneSwipe(bool inverted) { inverted_lane_swipe_ = inverted; }
  void DeployPowerup();
  void Cancel();
  void Observe(const FingerEvent& event);
  std::vector<GameplayEvent> DrainGameplay();
  Actions Sample();  // Queued pulses with a neutral poll between repeated presses.

 private:
  struct Finger {
    float x, y, travel = 0;
    uint64_t token = 0;
    bool pause = false, moved = false, swiped = false;
    Rect button;
  };
  void Emit(Action action);
  uint64_t next_token_ = 0;
  std::vector<GameplayEvent> gameplay_;
  Layout layout_;
  bool active_ = false;
  bool inverted_lane_swipe_ = true;
  bool pause_requested_ = false;
  bool powerup_available_ = false;
  std::map<std::pair<uint64_t, uint64_t>, Finger> fingers_;
  std::array<uint8_t, profile::gameplay_remap::kActionCount> pending_{};
  std::array<uint8_t, profile::gameplay_remap::kActionCount> held_polls_{};
};

// Acceleration includes gravity, in m/s^2. Tip the screen toward face-up
// briefly to deploy; return to the playing angle before another deployment.
// Pitch uses the screen normal, independent of portrait/landscape orientation.
class TiltDetector {
 public:
  bool Observe(uint64_t timestamp_ns, float x, float y, float z);
  void Reset();
 private:
  float pitch_ = 0, baseline_ = 0;
  uint64_t last_ = 0, cooldown_ = 0, settled_since_ = 0, movement_since_ = 0;
  bool initialized_ = false, calibrated_ = false, armed_ = true;
};

// A light flick in any direction, with a quiet sample and one second to rearm.
class ShakeDetector {
 public:
  bool Observe(uint64_t timestamp_ns, float x, float y, float z);
  void Reset();
 private:
  std::array<float, 3> gravity_{};
  uint64_t last_ = 0, cooldown_ = 0;
  bool armed_ = true;
};

enum class MotionMode { kNone, kTilt, kShake };
MotionMode ParseMotionMode(std::string_view token);
class MotionDetector {
 public:
  bool Observe(MotionMode mode, uint64_t timestamp_ns, float x, float y, float z);
  void Reset();
 private:
  MotionMode mode_ = MotionMode::kNone;
  TiltDetector tilt_;
  ShakeDetector shake_;
};

// Only SDL's touch-generated mouse stream is swallowed. Its release remains
// swallowed after Pause opens a menu, so one gesture cannot also select a row.
class MouseGate {
 public:
  void Capture() { down_ = true; }
  // Raw contacts can arrive before multiple synthesized mouse gestures drain.
  void MenuBegin(bool captured) {
    if (menu_pending_.size() == 32) menu_pending_.pop_front();
    menu_pending_.push_back(captured);
  }
  bool Consume(bool is_touch, Phase phase, bool gameplay);
 private:
  bool down_ = false;
  bool menu_ = false;
  std::deque<bool> menu_pending_;
};
}  // namespace rb_blitz::input::touch
