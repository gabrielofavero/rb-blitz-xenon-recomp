// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's profile as a *source of cvars* for the game (docs/plans/finished/2026-09-27-launcher-plan.md D3,
// rank 4). The launcher's own "Launch Game" passes every row it moved off its default as
// `--<key>=<value>` (rank 1, launcher/src/game_launch.cpp), so those runs need nothing here.
// This module is for the other half of R4: the game started on its own - the Start-menu shortcut,
// or the exe itself - which used to see `rb_blitz.toml` and the compiled defaults and none of the
// launcher's rows at all (launcher-plan.md §11 finding 6).
//
// Only the game compiles this: it needs the cvar registry, and the launcher deliberately does not
// link the runtime (D10). The reader is shared with the pad remap, which reads the same file for
// its `[remap]` table (src/input/remap.cpp).
//
// Nothing here logs. The one call site that matters runs in `OnConfigurePaths`, which is *before*
// the SDK initializes logging, so a line written there goes nowhere; the caller reports the
// outcome from `OnPostInitLogging` instead, with LogProfileApplyReport below.
//
// SDK-using on purpose, unlike the profile modules beside it: this is the game's half.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "launcher/profile.h"

namespace rb_blitz::launcher {

// The launcher's profile, resolved the way the launcher resolves it - an explicit
// `--launcher_profile`, then the environment, then the settings-folder pointer, then the portable
// marker and the app data folder. The order itself lives in ResolveProfilePath, so this cannot
// disagree with the window the file was written from.
//
// nullopt when there is nowhere to look, and also when the file is there but does not parse: a
// profile this build cannot read is not a reason to refuse to start the game (the launcher is
// where the user is told about it), it just leaves the compiled defaults in place. When `why` is
// not null it receives a reason worth reporting (a file that did not parse, or could not be read)
// and is cleared otherwise.
std::optional<Profile> ReadLauncherProfile(std::string* why = nullptr);

// What one application of the profile did, so the caller can report it once logging is up.
struct ProfileApplyReport {
  bool found = false;                // a profile was found and read
  std::size_t rows = 0;              // rows the profile carried
  std::size_t applied = 0;           // ...of those, the registry took
  std::size_t rows_ignored = 0;      // ...and of those, the retired rows the allowlist left out
  std::vector<std::string> refused;  // "key = value" rows the build does not have, or refused
  std::string unreadable;            // the profile was there but this build could not read it
};

// Applies the profile's `[settings]` table to the cvar registry at the *config* rank, which is
// the rank the game's own `rb_blitz.toml` uses. Call it before the SDK loads that file
// (OnConfigurePaths is such a point): a later config write wins, so the game's own file still
// outranks the launcher at equal rank, and a command-line flag - applied before any of this -
// outranks both. A row the registry does not know, or a value the flag refuses, is collected in
// the report rather than being fatal.
//
// The rows applied are the profile's own, except under `profile_controller_rows_only` (P11b, on by
// default): then only the controller rows and the launcher-only host rows are
// (`settings::ProfileRowsTheGameApplies`, contracts §8) and everything else in the table is inert
// preservation data. A row with an in-game settings row of its own must not be in that set: it is
// the store's, and applying the profile's copy would make the in-game row read as externally
// configured. That switch - not this function - decides when the launcher stops being a source of
// game preferences; see docs/settings/menu.md (part 3, Controller Settings).
ProfileApplyReport ApplyLauncherProfileSettings();

// Writes the report as log lines. Call it once logging is initialized (OnPostInitLogging); before
// that a line would go nowhere.
void LogProfileApplyReport(const ProfileApplyReport& report);

}  // namespace rb_blitz::launcher
