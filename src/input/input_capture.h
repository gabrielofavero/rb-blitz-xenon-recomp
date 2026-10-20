// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The in-game binding capture's seam (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md
// §14.3, P7).
//
// The generated Controls menu arms a capture with a `rbbz-settings/capture/<token>/-` write and
// enters the listening dialog. The state filter (src/input/remap.cpp) feeds this module one raw
// frame per poll; the first new press after every contributing input has been released is the
// captured source. The module owns no device: keys and mouse arrive from the window's event
// latches and the pad from the physical-driver sample, so a capture can observe a device whose
// support is off without reopening that device's normal path.
//
// Three rules are the reason this is a state machine rather than a callback:
//
//   * **the press that opened the page cannot be captured** - the session waits for a neutral
//     frame (nothing held) before it listens, so the Confirm press, a held stick or a click that
//     started the dialog all release first;
//   * **the listening window is a countdown** - once everything is released the session counts
//     3, 2, 1 once per second (restructure plan D11). A valid press is captured immediately (the
//     binding decision does not wait for a release), and reaching zero leaves the dialog with
//     the row untouched. There is no hold-to-cancel gesture to learn;
//   * **a wrong-kind source gives feedback and keeps listening** - plan R11: a `key:` pressed
//     into the Controller table is not stored, not applied, and not a silent nothing; the overlay
//     says which kind the table takes, and the count keeps running - a player whose controller
//     is missing waits the countdown out rather than having to know a gesture.
//
// SDK-free and device-free: the tests drive the rules with plain frames, and the live filter
// supplies SDL's own state.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
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
  // The wheel detents since the previous frame: positive up, negative down. A detent is an
  // event, so it is bindable as `mouse:wheel_up` / `mouse:wheel_down` and can never be held:
  // the session captures it on the frame it arrives, without waiting for a release.
  int32_t wheel_detents = 0;
  profile::remap::PadState pad;
};

struct Result {
  std::string token;
  profile::remap::Source source;
};

// One observation's answer: a captured source, or "leave the dialog" (the countdown reached
// zero).
struct Tick {
  std::optional<Result> captured;
  bool leave = false;
};

// The listening window (restructure plan D11): the dialog counts 3, 2, 1 once per second and then
// leaves the row untouched. Long enough to press the input you meant, short enough that a
// forgotten dialog cannot absorb the next menu press.
constexpr auto kCountdown = std::chrono::seconds(3);

// Arms a capture. Re-arming replaces the previous one; the press that opened the page is ignored
// until every input is released.
void Arm(const CaptureTarget& target);
void Cancel();
bool Armed();

// Consumes one raw frame. Only the armed session is evaluated; unarmed calls are free.
Tick Observe(const Observation& observation);

// The sentence the help overlay shows while the dialog is open: the live listening state with
// the countdown's remaining seconds, a wrong-kind message (the count keeps running beside it), or
// a save failure the caller posted with `PostStatusMessage`. Empty when idle.
std::string StatusText();

// A one-off sentence shown until the next Arm/Cancel: a refused source ("that is a pad control;
// this table takes keys and mouse") or a failed save ("the profile could not be written").
// Spelled PostStatusMessage rather than PostMessage: <winuser.h> defines the latter as a macro,
// and a name that silently grows an A suffix is not one to build a message channel on.
void PostStatusMessage(std::string message);

// The kind list as the dialog speaks it ("a pad control", "a keyboard key or mouse button").
std::string AllowedKindText(const std::vector<profile::remap::SourceKind>& allowed);

}  // namespace rb_blitz::input::capture
