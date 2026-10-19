// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 of the gameplay-mapping plan: the guest adapter that turns a gameplay table's held action
// into the title's own gameplay dispatch
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md, D4/D5; evidence in §10).
//
// Two halves, and the split is the whole design:
//
//   * the **input filter** (src/input/remap.cpp) samples the physical layer once per guest poll,
//     evaluates the two tables against it (src/input/source_gates.h) and *publishes* the result
//     here. It also receives the pad overlay it must apply to the state it is about to deliver -
//     the physical controls the Controller table owns, and the Start bit a held Pause raises;
//
//   * the **guest seam** (this file) consumes that publication. The title's own
//     JoypadController update (`sub_82443A30`, ~30 ms while a song runs) is the only place a
//     guest function is called, because it is the guest's thread and its own frame cadence
//     (plan D4: window callbacks only publish input).
//
// The dispatch itself is the title's validated action boundary, not a reimplementation of it:
// `sub_824364B0(controller, kBMAction, arg)` on a press and `sub_824367E0(controller, kBMAction)`
// on a release are the same calls the native preset's producers make, so note judgement,
// calibration, lane animation and the power-up rules stay the game's own (plan §10.2).
//
// The native preset is bypassed only while the **Controller** table is active, and only at that
// boundary: every native producer (button, key message, symbol message, the controller's own
// held-action repeat) reaches `sub_824364B0`/`sub_824367E0`, so one suppression there covers all
// four (plan §10.4 O1) without blanking pad state the menus, the pause screen, calibration and
// the system still need. Pause is the exception that proves the rule: Start is not a
// `kBMAction` at all (§10.4 O10), so it travels as the pad overlay above instead.
//
// No guest function is called from the filter, and no table is evaluated on the guest seam:
// that is what keeps one press one press (plan D5) - the seam never re-reads the devices, it
// drains the edges the poll produced.

#pragma once

#include <array>
#include <cstdint>

#include "profile/gameplay_remap.h"

namespace rb_blitz::gameplay_input {

// One poll of the physical layer, as the input filter composed it.
struct Poll {
  // A live song (`GameScreenActive()`): the only context a gameplay table may act in.
  bool gameplay = false;
  // The two routing selectors, already resolved against that context.
  bool controller_active = false;
  bool keyboard_mouse_active = false;
  // The pad controls the Controller table's sources name, as X_INPUT_GAMEPAD bits.
  uint16_t controller_buttons = 0;
  // The union of both active tables' holds, one per action (plan D5's action boundary).
  std::array<bool, profile::gameplay_remap::kActionCount> held{};
};

// The pad bits the filter must remove from - and add to - the state it is about to deliver.
// Same shape as `input::GameplayPadOverlay`, which is what composes it.
struct PadOverlay {
  uint16_t clear_buttons = 0;
  uint16_t set_buttons = 0;
};

// Publishes one poll: queues the action edges the consumer will dispatch and returns the overlay
// for the pad state of the same poll. Thread-safe; called on the guest's input thread.
PadOverlay PublishPoll(const Poll& poll);

// A boundary the tables must not carry held state across (focus loss, screen change, an
// unplugged pad, a flipped switch): queued edges are dropped, and the seam releases every action
// the guest currently believes is down, so a held track action cannot keep repeating across the
// boundary (plan D5).
void NoteBoundary();

// Whether the native preset's producers must be suppressed right now. True exactly while the
// Controller table is routing gameplay input; read by the boundary hooks on the guest thread.
bool NativeActionsSuppressed();

}  // namespace rb_blitz::gameplay_input
