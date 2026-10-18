// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The in-game binding capture's seam (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md
// §14.3, P7).
//
// The generated Controls menu arms a capture with a `rbbz-settings/capture/<token>/-` write and
// enters the listening dialog. The state filter (src/input/remap.cpp) feeds this module one raw
// frame per poll; the first new press after every contributing input has been released is the
// captured source. The module owns no device: keys and mouse arrive from the window's event
// latches and the pad from the physical-driver sample, so a capture can observe a device whose
// support is off without reopening that device's normal path.
//
// Two rules are the reason this is a state machine rather than a callback:
//
//   * **the press that opened the page cannot be captured** - the session waits for a neutral
//     frame (nothing held) before it listens, so the Confirm press, a held stick or a click that
//     started the dialog all release first;
//   * **a wrong-kind source gives feedback and keeps listening** - the plan's R11 and §14.3: a
//     `key:` pressed into the Controller table is not stored, not applied, and not a silent
//     nothing; the overlay says which kind the table takes, and the session waits for neutral
//     again. The press still starts the hold timer, so holding it cancels the dialog even when
//     none of the connected devices can be bound here - which is the only way out of a page
//     whose controller is not plugged in.
//
// SDK-free and device-free: the tests drive the rules with plain frames, and the live filter
// supplies SDL's own state.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "profile/remap.h"

namespace rb_blitz::input::capture {

// What an armed capture is for. `token` is the `input.*` row that will receive the source;
// `allowed` is the row's table kind (General Mapping takes all three, the two gameplay tables take
// their own kind only - plan R11).
struct CaptureTarget {
  std::string token;
  std::vector<profile::remap::SourceKind> allowed;
};

// One poll of the raw devices. Everything is unfiltered by the device gates on purpose: a
// binding may be prepared for a device that is disabled for normal play.
struct Observation {
  bool focused = false;
  bool any_key_down = false;
  // The key-down events since the previous observation, by the SDL scancode name the profile
  // spells (`space`, `f1`, `w`).
  std::vector<std::string> new_keys;
  uint32_t mouse_buttons = 0;
  profile::remap::PadState pad;
  // Whether a candidate source is still physically held, answered from the device levels the
  // caller already samples (`PadSourceHeld`/`MouseSourceHeld` and the keyboard's own latch).
  //
  // A capture completes when its candidate is RELEASED and cancels when it is held past
  // `HoldToCancel()`: that is the dialog's unambiguous cancel gesture, and it is why Back and
  // Escape stay bindable - a tap of either binds it, a long hold of it cancels (plan §14.3). A
  // caller that has no device levels leaves this empty, and a press is then captured on the frame
  // it arrives, which is what a device that only reports edges allows.
  std::function<bool(const profile::remap::Source&)> still_held;
};

struct Result {
  std::string token;
  profile::remap::Source source;
};

// One observation's answer: a captured source, or "leave the dialog" (a held candidate, or a
// timeout).
struct Tick {
  std::optional<Result> captured;
  bool leave = false;
  // The leave was the player's own cancel gesture, not the timeout: same effect on the dialog,
  // reported apart so a run can tell a cancel from an abandoned page.
  bool cancelled = false;
};

// How long a candidate has to be held before the capture reads it as "cancel" instead of a
// binding. Long enough that no deliberate tap reaches it, short enough that a player who wants
// out is not waiting on the timeout.
constexpr auto kHoldToCancel = std::chrono::milliseconds(700);

// Arms a capture. Re-arming replaces the previous one; the press that opened the page is ignored
// until every input is released.
void Arm(const CaptureTarget& target);
void Cancel();
bool Armed();

// Consumes one raw frame. Only the armed session is evaluated; unarmed calls are free.
Tick Observe(const Observation& observation);

// The sentence the help overlay shows while the dialog is open: the live listening state, a
// wrong-kind message, or a save failure the caller posted with `PostStatusMessage`. Empty when
// idle.
std::string StatusText();

// A one-off sentence shown until the next Arm/Cancel: a refused source ("that is a pad control;
// this table takes keys and mouse") or a failed save ("the profile could not be written").
// Spelled PostStatusMessage rather than PostMessage: <winuser.h> defines the latter as a macro,
// and a name that silently grows an A suffix is not one to build a message channel on.
void PostStatusMessage(std::string message);

// The kind list as the dialog speaks it ("a pad control", "a keyboard key or mouse button").
std::string AllowedKindText(const std::vector<profile::remap::SourceKind>& allowed);

// Whether one captured pad source is still down, from the same primitives `Observe` presses with.
// The live filter's `still_held` oracle is built from this and the two below, so the hold rule and
// the press rule can never disagree about what "still held" means.
bool PadSourceHeld(const profile::remap::PadState& pad, std::string_view name);
bool MouseSourceHeld(uint32_t mouse_buttons, std::string_view name);

}  // namespace rb_blitz::input::capture
