// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The input system this title runs on, and the physical pad it can read before anything is
// merged or synthesized (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md, D1/D2;
// P2).
//
// Two things differ from the SDK's own CreateDefaultInputSystem:
//
//   * the legacy MnK emulation driver is installed only while Keyboard Support is on. Its
//     `mnk_mode`/`keybind_*` cvars synthesize pad state from keys, and that synthesis must not be
//     a keyboard path around the source gates: with the gate off the driver is simply absent, so
//     no key can raise a pad bit. With the gate on (the default) it behaves exactly as before,
//     which is also how every acceptance harness drives the menus. The boot log names the choice.
//
//   * the physical drivers are remembered, so `SamplePhysicalPad` can read a pad's own state
//     without the synthetic devices the SDK merges into what the guest sees. A `pad:` gameplay
//     source must not become active because MnK set the same bit (D1); this is the seam that
//     makes that true, and it needs no SDK patch because InputDriver's own state call is public.
//
// The synthetic connection is *not* here: it is the host-input device src/input/mouse_ui.cpp
// keeps connected while either host gate is open.

#pragma once

#include <memory>

#include <rex/input/input.h>
#include <rex/runtime.h>

namespace rb_blitz::input {

// The SDK's default drivers with the two changes above. Tool mode installs the NOP driver only,
// exactly like the SDK's own default.
std::unique_ptr<rex::system::IInputSystem> CreateTitleInputSystem(bool tool_mode);

// Merges only the physical (non-synthetic) devices' state, across every physical driver the
// current input system owns. Returns false - and leaves `out_state` zeroed - when no physical pad
// is attached. Called from the state filter, so it is one extra poll of the drivers per guest
// poll and no background work; the drivers read their own cached state or the platform's, both of
// which are cheap.
bool SamplePhysicalPad(rex::input::X_INPUT_STATE* out_state);

}  // namespace rb_blitz::input
