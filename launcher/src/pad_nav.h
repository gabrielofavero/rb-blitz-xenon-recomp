// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pad's half of the input seam (docs/plans/launcher-plan.md A3, D6).
//
// A pad is a device the focus ring never hears about: what it produces is the same NavAction the
// keyboard produces (nav.h), and everything downstream - the ring, the tab strip, the row
// activation, the scroll-follow - is already device-agnostic. This module is the part that
// decides *which* action a pad's state means. There is no SDL in it: the button vocabulary, the
// deadzone and the repeat are rules with numbers in them, and a rule with a number in it is worth
// a test (tests/launcher_pad_nav_tests.cpp). launcher/src/pad_source.cpp is the SDL half - which
// pads are open, what they are called, and reading them into a PadNavState.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "nav.h"

namespace rb_blitz::launcher {

// What a pad can ask the launcher to do, one entry per binding D6 names. The order is the order
// the model resolves two presses in the same frame by, and it reads as the priority a player
// would expect: a direction before a face button, and the right stick's scroll last of all - so a
// stick leaning off-centre can only ever scroll, never starve a real press of its turn.
enum class PadNavButton : std::uint8_t {
  kUp,
  kDown,
  kLeft,
  kRight,
  kConfirm,
  kCancel,
  kTabForward,
  kTabBack,
  kLaunch,
  kScrollUp,
  kScrollDown,
  kCount,
};

inline constexpr std::size_t kPadNavButtonCount = static_cast<std::size_t>(PadNavButton::kCount);

// One frame of a pad's state in the launcher's own vocabulary. A stick that has passed the
// deadzone arrives here as the same thing a D-pad press is: nothing downstream asks which one
// moved, which is what makes the ring work the same on a stick, a D-pad and a keyboard.
struct PadNavState {
  std::array<bool, kPadNavButtonCount> down{};

  bool operator[](PadNavButton button) const {
    return down[static_cast<std::size_t>(button)];
  }
  bool& operator[](PadNavButton button) { return down[static_cast<std::size_t>(button)]; }

  // True when anything at all is down, which is what the shell reads to know the pad is the
  // device that last moved (D6's "the last device to move owns the focus ring") - including the
  // frames where the action it produced was swallowed by a modal.
  bool any() const {
    for (const bool pressed : down) {
      if (pressed) {
        return true;
      }
    }
    return false;
  }
};

// A stick's reading, and the width of the band around the centre that means nothing. SDL reports
// a stick as a signed 16-bit value and a pad at rest does not read exactly zero, so the deadzone
// is the difference between "the ring stays where it was" and "the ring crawls". A quarter of the
// way out is the usual number; it is a constant here rather than a setting because D6's "with a
// deadzone" should be one value a test can hold on to.
inline constexpr int kStickDeadzone = 8000;

// The right stick's scroll band. Wider than the ring's: a scroll is a glance rather than a
// command, and a pad whose right stick does not rest at exactly zero must not walk the list on its
// own while the ring's own deadzone would have called the same lean "nothing".
inline constexpr int kScrollStickDeadzone = 14000;

enum class AxisDirection {
  kNeutral,
  kNegative,
  kPositive,
};

// Which way a stick is pushed, once the deadzone is discounted.
AxisDirection AxisDirectionFor(int value, int deadzone = kStickDeadzone);

// The four face buttons, as the two families name them. SDL's mapping database knows which a pad
// is (`SDL_GetGamepadButtonLabel`), and pad_source turns its answer into this; the text lives
// here so what the bar draws is testable without a pad on the desk.
enum class FaceLabel { kUnknown, kA, kB, kX, kY, kCross, kCircle, kSquare, kTriangle };

std::string_view FaceLabelText(FaceLabel label);

// The families whose shoulder and Start buttons have their own names. SDL's database labels the
// four face buttons and has nothing to say about the rest, so this short table is the launcher's
// own - and it is a table rather than a guess because "press LB to change tab" is wrong on a
// DualSense in a way its owner notices. `kUnknown` is the fallback and is what an unnamed pad
// gets: the letters the 360 controller has, which are also the names the remap grammar uses.
enum class PadFamily { kUnknown, kXbox, kPlayStation, kNintendo };

struct PadButtonNames {
  std::string_view confirm = "A";
  std::string_view cancel = "B";
  std::string_view shoulder_left = "LB";
  std::string_view shoulder_right = "RB";
  std::string_view start = "Start";
};

// What the bar's hint line calls this pad's buttons (A2). `confirm` and `cancel` are the pad's
// own face labels - a DualSense's south button is Cross, not A - and the rest come from the
// family above. A face label the database does not give falls back to the 360 letters.
PadButtonNames ButtonNamesFor(PadFamily family, FaceLabel confirm, FaceLabel cancel);

// The repeat timing: the first press is immediate, a direction held past `kRepeatDelaySeconds`
// fires again, and then every `kRepeatIntervalSeconds` while it is still held. The numbers are
// borrowed from what a text caret does - a pause long enough not to overshoot a row, then steady.
inline constexpr double kRepeatDelaySeconds = 0.45;
inline constexpr double kRepeatIntervalSeconds = 0.12;

// One pad's worth of edge and repeat handling, which is the whole of A3's decision-making. It is
// fed the state of every open pad merged into one (pad_source merges them): two pads both holding
// Down is one Down, and either pad can take over mid-session because neither is "the" pad. A
// button already down when the pad arrived has no edge, so a stick resting against its stop when
// the pad is plugged in does not move the ring until it is released and pushed again - and a pad
// that leaves mid-press simply arrives here as a state with nothing in it, which is what makes
// the release it can no longer send unnecessary.
class PadNavModel {
 public:
  // The action this frame's state asks for, or kNone. At most one action per frame, like the
  // keyboard's: the ring moves a row at a time, and a frame is not the place to catch up on three
  // presses that were missed. `now_seconds` is a monotonic clock the caller owns - SDL's, in the
  // launcher - so the repeat is a rule about time and not about frames, and a test can hand it any
  // clock it likes.
  NavAction Update(const PadNavState& state, double now_seconds);

 private:
  struct Held {
    bool down = false;
    // A press that has not been turned into an action yet. A frame returns one action, so a
    // press that arrives while another is firing waits its turn instead of being dropped - which
    // is what makes a confirm aimed at the row a repeat is running over still land.
    bool pressed = false;
    double due = 0.0;
  };
  std::array<Held, kPadNavButtonCount> held_{};
};

// What one button means. Free rather than a member because the mapping is the binding table D6
// names, and a table is easier to check when it is the only thing in the function.
NavAction ActionForPadButton(PadNavButton button);

}  // namespace rb_blitz::launcher
