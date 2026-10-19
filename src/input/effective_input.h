// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The one published input configuration the title's poll reads
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14.4, P7).
//
// Until P7 the bindings were read once, when the input system was built (remap.cpp's
// `InstallPadRemap`), and the plan's D7 made that a launch-scoped policy. §14.4 replaces it: a
// binding committed in the in-game Controls menu takes effect when the player resumes, without a
// relaunch. That needs one snapshot both the sampler and the pad rewrite read, swapped atomically
// so a poll can never see half a table:
//
//   * the store (src/input/input_store.cpp) publishes a new snapshot after a durable save;
//   * the state filter copies the shared pointer once at the top of the poll and uses that copy
//     for the whole poll - the rewrite, the gameplay tables and the pad overlay therefore agree
//     about the configuration even if a second commit lands while the poll runs;
//   * the boot publishes the initial snapshot before the input system exists, so the first poll
//     already reads the profile's tables rather than a default.
//
// SDK-free: the tables are the same two modules Controller Settings compiles, and a test can
// publish a snapshot without a window or a boot.

#pragma once

#include <memory>

#include "profile/gameplay_remap.h"
#include "profile/remap.h"

namespace rb_blitz::input {

// The bindings one poll evaluates. The switches and the device gates stay on their cvars (a
// profile row is applied to the registry at config rank at boot and the store applies a live edit
// on a host thread), so this snapshot is about the three binding tables and whether a profile was
// readable at all - the fact that decides whether General Mapping's compiled defaults apply.
struct EffectiveInput {
  profile::remap::Table general;
  profile::gameplay_remap::Table controller{
      profile::gameplay_remap::TableKind::kController};
  profile::gameplay_remap::Table keyboard_mouse{
      profile::gameplay_remap::TableKind::kKeyboardMouse};
  // True when a profile file was read at boot (or created by an in-game save). A machine with no
  // profile keeps the SDK's own reporting, exactly as it did before P7: the game does not invent
  // General Mapping defaults for a file nobody has.
  bool profile_present = false;
};

// The snapshot the current poll must use. Never null: before the boot publishes, an empty
// snapshot with every table on its compiled defaults is returned, so a tool or a test cannot
// dereference nothing.
std::shared_ptr<const EffectiveInput> CurrentInput();

// Replaces the published snapshot. Called on the guest thread by a commit (the write hook's own
// thread) and once at boot; the filter only copies the pointer, so no lock is held across a poll.
void PublishInput(std::shared_ptr<EffectiveInput> input);

// A copy of the current snapshot, for a caller that wants to edit it before publishing.
std::shared_ptr<EffectiveInput> CopyCurrentInput();

}  // namespace rb_blitz::input
