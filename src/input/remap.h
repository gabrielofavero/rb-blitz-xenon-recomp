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
// with the same module Controller Settings writes it with (src/profile/remap.h), so the two cannot
// disagree about what a binding means, and the physical sources it names are read from SDL -
// which is also where the SDK's own pad state comes from, so no translation is duplicated.
//
// The keyboard is read here and not by the SDK: the SDK's SDL driver maps no keys to pad
// buttons, so `key:` sources are how a keyboard becomes a pad at all.
//
// Guide is a separate question from a binding, and this is the path that answers it: the SDK's own
// `guide_button` and `keybind_guide` cvars can raise the guest's Guide bit, and the project never
// delivers it (docs/settings/contracts.md §2.3). The filter is therefore installed
// whether or not a profile was readable, and the state it hands the guest has that bit cleared on
// every input path - profile, no profile, malformed profile, or an explicit legacy cvar.
//
// Known limitation, deliberately not papered over: `GetKeystroke` reports the *pad's* own
// transitions, so a remapped button emits a keystroke for the control the pad has rather than
// the one the guest will see. Nothing here reads keystrokes for selection - the menus are
// polled - so this is a footnote rather than a defect, and fixing it would mean reimplementing
// the SDK's keystroke queue.

#pragma once

#include <string>

#include <rex/runtime.h>

namespace rb_blitz::input {

// The device gates that decide what a physical input may reach (plan D2,
// docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md). Read at poll time from the
// cvars the profile feeds, so the input filter, the menu generator and the settings preview
// observe one effective value per gate rather than three readings of a file.
//
// `keyboard_support` and `mouse_ui_nav` are the only switches left: they are source gates, and off
// means that device drives nothing. Gameplay Mapping's two routing switches
// (`gameplay_mapping_controller`, `gameplay_mapping_keyboard_mouse`) are retired by P2 of
// docs/plans/in-progress/2026-10-19-interface-gameplay-mapping-restructure-plan.md (D3): the
// Mouse & Keyboard table is always active while playing, and the Controller table is active when
// it has a custom row - the native preset drives the pad otherwise (D4). Custom is inferred from
// the table; an old profile key is left in the file and decides nothing.
struct GameplaySwitches {
  bool keyboard_support = true;  // `keyboard_support`
  bool mouse_support = true;     // `mouse_ui_nav` - the existing Mouse Support row
};

GameplaySwitches ReadGameplaySwitches();

// The two source gates as their own names, for the device side: `keyboard_support` (read here)
// and the existing Mouse Support switch (`MouseSupportEnabled` in input/native_menu.h).
bool KeyboardSupportEnabled();

// A boundary the gameplay tables must not carry held state across: the window lost focus, the
// guest UI changed screen, or a transition made input go nowhere. The next poll drops every held
// action instead of delivering a late press or a release the guest never asked for (plan D5).
// Safe from any thread; the poll that sees it invalidates under its own lock.
void InvalidateGameplayInput();

// P8: the Controls recovery path (plan §14.4, §17.2). A profile can leave the menu with no usable
// input at all - both host gates off and no pad - and then ordinary input cannot reach the row
// that would fix it. While (and only while) no route exists, one fixed key (`kRecoveryKey`) held
// for `kRecoveryHold` reopens the keyboard gate through the same store the Controls rows write.
//
// One observation per call; safe from the UI thread and from the guest's state filter, and both
// call it. The filter alone would not do: `InputSystem::GetState` returns "not connected" before
// the filter runs when no device enumerates, which is exactly the recovery state (the synthetic
// host device enumerates only while a host gate is open). The overlay draws every frame, so the
// observation happens there too.
void TickInputRecovery();

// What the help caption should say about recovery right now: the "hold F10" sentence while the
// profile leaves no usable route, a short confirmation for a while after the key reopened it, and
// empty otherwise. Drawn even with Settings Labels off (a navigation problem is not optional help).
std::string RecoveryNotice();

// Reads the profile's `[remap]` table and filters every pad state the guest asks for.
// Installed the same way mouse_ui is - by wrapping RuntimeConfig::input_factory - because that is
// where the input system is created and disposed of, and the filter has to be in place before the
// guest starts polling.
//
// A missing or unreadable profile is not an error: no table means the pad is left alone, which is
// also what a player who never opened Controller Settings gets. The filter is still installed, because
// the Guide prohibition is not conditional on a profile existing.
void InstallPadRemap(rex::RuntimeConfig& config);

}  // namespace rb_blitz::input
