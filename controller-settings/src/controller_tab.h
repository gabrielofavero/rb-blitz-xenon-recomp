// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The button-mapping block on the Controller tab (docs/plans/finished/2026-09-27-launcher-plan.md D16).
//
// Every control a 360 pad has is listed whether or not it is bound, because the list is the
// answer to "what can I rebind?" - a list that only showed the differences would leave the user
// guessing. A row is either *stock* (no `[remap]` entry, so the pad reports it exactly as the
// SDK mapped it) or bound to a list of sources.
//
// *Assign* listens for three seconds and adds the first input it sees, from any device at once:
// the point of listening is not to interrogate the user about which pad they mean, and a
// single-player game has nothing to disambiguate. Adding rather than replacing is what makes
// "several inputs, one button" work - press the pad button, press Assign again, press the key -
// and *Reset* is how a row goes back to being stock.
//
// There is no input-source setting to pair with it: the listen watches every connected pad, the
// keyboard and the mouse in the same frame, which is the flexible half of the choice the plan
// left open.
//
// The reading is polled rather than event-driven - Controller Settings' event pump already belongs to
// ImGui - so SDL is asked what is down each frame and a capture is a *rising edge* over the
// previous frame. That is also what keeps the click that started the listen from assigning
// itself: the mouse button is already down when the listen begins, so it has no edge left.
//
// The pads it reads are Controller Settings' open ones (A3's PadRegistry): SDL only reports a pad's
// state through a handle somebody opened, so a capture that opened its own would be reading a
// different pad from the one the focus ring is following.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "profile/profile.h"
#include "profile/remap.h"
#include "nav.h"
#include "pad_source.h"
#include "profile_session.h"
#include "schema_view.h"

namespace rb_blitz::profile {

// The controls listed: every target the grammar knows (`remap::Targets()`), which is every control
// a 360 pad has except Guide - that one is a platform button, and the project never delivers it to
// the guest. Spelled as a constant here so the ring size below is a compile-time number rather
// than something the shell has to ask for at run time, and tied to the vocabulary by the
// static_assert in controller_tab.cpp so the two cannot drift.
inline constexpr std::size_t kControlRows = remap::kTargetCount;

class ControllerTab {
 public:
  // How long a listen lasts. Long enough for a deliberate press and short enough that a
  // mis-click does not lock the panel, and the countdown is on screen the whole time.
  static constexpr double kListenSeconds = 3.0;

  // The block's flat focus rows, in draw order: one row per control (Assign and Reset side by
  // side), the "Reset all bindings" row, then the two Gameplay Mapping switches
  // (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md §4).
  static constexpr std::size_t kResetAllRow = kControlRows * 2;
  static constexpr std::size_t kControllerSwitchRow = kResetAllRow + 1;
  static constexpr std::size_t kKeyboardMouseSwitchRow = kResetAllRow + 2;
  static constexpr std::size_t kRowCount = kKeyboardMouseSwitchRow + 1;

  // The number of ring options each block row takes, which is what the shell sizes its ring from.
  // The block walks the same list to place its entries, so the count cannot drift from the draw
  // (shell.cpp's one-place rule for every block).
  static std::vector<std::size_t> FocusRows();

  // `pads` is Controller Settings' open pads (A3). A capture reads them through the registry rather
  // than opening anything itself: SDL only reads a pad somebody has opened, so the registry is
  // what makes a listen able to see a pad press at all.
  explicit ControllerTab(const PadRegistry& pads) : pads_(pads) {}

  // Draws the block. `first_row` is the ring index of the first control's Assign button.
  void Draw(std::size_t first_row, FocusModel& ring, ProfileSession& session, NavAction action);

  // True while a listen is running. The shell uses it to keep Escape from quitting the
  // Controller Settings while the user is waiting for the countdown.
  bool Listening() const { return listening_.has_value(); }

  // What the bottom bar says about one of the block's rows, in the ring's own order (A2). The
  // schema's rows carry their own tooltip; these are buttons, so their sentence is built here
  // from the control the row belongs to - `row / 2` is the control, `row % 2` is which of its two
  // buttons it is, in the same arithmetic the draw loop uses.
  static std::string HelpText(std::size_t row);

 private:
  using Source = remap::Source;

  // One button, ring-reachable, disabled when `enabled` is false.
  bool Item(std::size_t index, FocusModel& ring, NavAction action, const char* label,
            bool enabled);

  // A Gameplay Mapping switch: its label, then the ring-reachable On/Off button. Returns true
  // when the user flipped it, in which case the caller writes the new value into the profile.
  bool SwitchRow(std::size_t index, FocusModel& ring, NavAction action, const char* label,
                 bool on);

  void BeginListening(remap::Target target);
  // Adds the captured input to the control's binding, which is how "several inputs, one button"
  // is expressed: Assign never replaces, so pressing it again adds another source.
  void CommitCapture(ProfileSession& session, const Source& source);
  // The first input that went down since the previous frame, if any. Reads every connected pad,
  // then the keyboard, then the mouse: a pad press is the one a listen is usually about, so it
  // wins a tie.
  std::optional<Source> CapturedInput() const;
  // Records what is down now, which is what the next frame's rising edges are measured against.
  void SnapshotInput();

  struct PadSnapshot {
    SDL_JoystickID id = 0;
    std::vector<bool> buttons;  // indexed by SDL_GamepadButton
    bool left_trigger = false;
    bool right_trigger = false;
  };

  // Controller Settings' open pads (A3). Not owned: the shell holds the registry, because the focus
  // ring needs the same pads the capture does.
  const PadRegistry& pads_;

  // The control being rebound, and when the listen runs out.
  std::optional<remap::Target> listening_;
  double listen_until_ = 0.0;

  // The previous frame's input, which is the half of "rising edge" that is not SDL's.
  std::vector<bool> key_down_;
  std::vector<PadSnapshot> pads_snapshot_;
  uint32_t mouse_down_ = 0;

  // The last thing the block did, so a capture, a reset and a listen that found nothing all
  // say so rather than leaving the user to guess.
  std::string status_;
};

}  // namespace rb_blitz::profile
