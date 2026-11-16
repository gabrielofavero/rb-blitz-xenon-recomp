// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <map>
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
  Rect powerup;
  bool operator==(const Layout&) const = default;
};
Layout MakeLayout(float width, float height, Rect safe_area);

enum class Phase { kDown, kMove, kUp, kCancel };
struct FingerEvent {
  uint64_t device = 0, finger = 0;
  Phase phase = Phase::kDown;
  float x = 0, y = 0;  // normalized full-screen coordinates
};

enum class GameplayKind { kPress, kSwipe, kRelease, kCancel };
struct GameplayEvent {
  GameplayKind kind;
  uint64_t token = 0;
  Action action = Action::kFirstNote;
  bool swiped = false;
};

// Notes strike immediately. Each touch keeps an identity through its release so
// only the wrong press belonging to a swipe can be forgiven by the guest seam.
class Gestures {
 public:
  void SetLayout(Layout layout);
  void SetActive(bool active);
  void SetPowerupAvailable(bool available);
  void Cancel();
  void Observe(const FingerEvent& event);
  std::vector<GameplayEvent> DrainGameplay();
  Actions Sample();  // Queued pulses with a neutral poll between repeated presses.

 private:
  struct Finger {
    float x, y, travel = 0;
    uint64_t token = 0;
    bool pause = false, powerup = false, moved = false, swiped = false;
  };
  void Emit(Action action);
  uint64_t next_token_ = 0;
  std::vector<GameplayEvent> gameplay_;
  Layout layout_;
  bool active_ = false;
  bool pause_requested_ = false;
  bool powerup_available_ = false;
  std::map<std::pair<uint64_t, uint64_t>, Finger> fingers_;
  std::array<uint8_t, profile::gameplay_remap::kActionCount> pending_{};
  std::array<uint8_t, profile::gameplay_remap::kActionCount> held_polls_{};
};

// Only SDL's touch-generated mouse stream is swallowed. Its release remains
// swallowed after Pause opens a menu, so one gesture cannot also select a row.
class MouseGate {
 public:
  void Capture() { down_ = true; }
  bool Consume(bool is_touch, Phase phase, bool gameplay);
 private:
  bool down_ = false;
};
}  // namespace rb_blitz::input::touch
