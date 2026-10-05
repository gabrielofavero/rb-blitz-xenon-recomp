// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The General tab's two facts (docs/plans/launcher-plan.md D4, D5; prompt B1): where the
// game's data is, and whether the Ultimate payload can be mounted.
//
// Dependency-free on purpose - no ImGui, no SDL, no SDK - so D5's four states can be tested
// from directory fixtures (tests/launcher_ultimate_state_tests.cpp) without booting
// anything. The payload paths and the "is it there" checks come from
// src/hooks/ultimate_plan.h, which is the same header the runtime's own check uses: the
// launcher saying "ready" and the game mounting it cannot drift apart.

#pragma once

#include <filesystem>
#include <string_view>

#include "launcher/profile.h"

namespace rb_blitz::launcher {

// The directories the General tab needs. `ultimate_root` is where the runtime mounts the
// payload (ResolvePayloadRoot's default, <game root>\ultimate), kept here so the tab and
// the tests cannot disagree about it.
struct GameRoots {
  std::filesystem::path game_root;
  std::filesystem::path ultimate_root;
  bool game_root_found = false;
};

// Detection, in order: a non-empty `override_root` (the game's own --game_data_root); then
// `<launcher dir>\game`, which is the installer's layout; then `<launcher dir>` itself. A
// candidate is a game root when it holds `default.xex` or `gen\`. When none matches, the
// installer's layout is returned with `game_root_found == false`, so the tab can say it did
// not find the game instead of pretending.
GameRoots DetectGameRoots(const std::filesystem::path& launcher_dir, std::string_view override_root);

// D5's four states, read off the file system.
enum class UltimateState {
  kReady,        // the payload pair is under <game root>\ultimate
  kAlsoPresent,  // the pair sits at the game root's top level ("merged")
  kMissing,      // neither location has it
  kDamaged,      // a header without its archive: the pair the game reads as a damaged disc
};

UltimateState DetectUltimateState(const std::filesystem::path& game_root);

// True when the payload can be mounted: kReady or kAlsoPresent.
bool UltimateAvailable(UltimateState state);

// D5: never hard-disable. Kept as the single flag the decision says to flip if that ever
// changes, and pinned by the tests. The General tab no longer needs it, though: when the
// payload is not there the Ultimate entry is replaced by *Install Ultimate...* rather than
// being drawn greyed out, so there is nothing left to disable.
bool UltimateSelectable(UltimateState state);

// D5's UI column, verbatim: the line the tab shows for the state. Empty for kReady, which
// needs no annotation.
std::string_view UltimateStateText(UltimateState state);

// The target the launcher will actually use. A stored "Ultimate" with nothing to mount falls
// back to the retail game rather than starting a boot that cannot honour it - D5's "Ultimate
// is selectable but not the default" when the payload is missing. Any explicit choice other
// than Ultimate is untouched.
LaunchTarget FallbackTarget(LaunchTarget wanted, UltimateState state);

}  // namespace rb_blitz::launcher
