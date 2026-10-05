// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pad remap, installed over the SDK's input system.
//
// Why this cannot be a driver: `InputSystem::GetState` merges every device assigned to a user,
// and merging only ever adds input, so a driver can add a button but cannot take one away - and
// a remap that cannot take a button away is not a remap. The seam is therefore the state filter
// the SDK grew for exactly this (patches/rexglue-sdk/0010-input-system-state-filter.patch,
// `InputSystem::SetStateFilter`), which runs once on the merged state, on its way to the guest.
//
// What the filter does with it is wholly in this project: the profile's `[remap]` table is read
// with the same module the launcher writes it with (src/launcher/remap.h), so the two cannot
// disagree about what a binding means, and the physical sources it names are read from SDL -
// which is also where the SDK's own pad state comes from, so no translation is duplicated.
//
// The keyboard is read here and not by the SDK: the SDK's SDL driver maps no keys to pad
// buttons, so `key:` sources are how a keyboard becomes a pad at all.
//
// Known limitation, deliberately not papered over: `GetKeystroke` reports the *pad's* own
// transitions, so a remapped button emits a keystroke for the control the pad has rather than
// the one the guest will see. Nothing here reads keystrokes for selection - the menus are
// polled - so this is a footnote rather than a defect, and fixing it would mean reimplementing
// the SDK's keystroke queue.

#pragma once

#include <rex/runtime.h>

namespace rb_blitz::input {

// Reads the launcher profile's `[remap]` table and, when it binds anything, filters every pad
// state the guest asks for. Installed the same way mouse_ui is - by wrapping
// RuntimeConfig::input_factory - because that is where the input system is created and disposed
// of, and the filter has to be in place before the guest starts polling.
//
// A missing or unreadable profile is not an error: no table means the pad is left alone, which
// is also what a player who never opened the launcher's Controller tab gets.
void InstallPadRemap(rex::RuntimeConfig& config);

}  // namespace rb_blitz::input
