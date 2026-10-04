// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Where the launcher profile lives (docs/plans/launcher-plan.md D2). This module is the
// single answer to that question: rb_blitz_launcher.exe writes the file it resolves and
// rb_blitz.exe reads the file it resolves, so the two cannot disagree.
//
// SDK-free on purpose - no rex::, no <windows.h>, no third-party parser: the game and the
// launcher both compile this, and the tests exercise it without booting anything
// (launcher-plan.md P0.3).

#pragma once

#include <filesystem>
#include <string>

namespace rb_blitz::launcher {

// The profile's file name and the marker that puts it beside the executable.
inline constexpr const char* kProfileFileName = "launcher.toml";
inline constexpr const char* kPortableMarkerName = "rb_blitz_launcher.portable";

// The folder under the roaming app data directory that holds the profile.
inline constexpr const char* kProfileDirName = "rb_blitz";

// The environment override, the second step of the resolution order.
inline constexpr const char* kProfileEnvVar = "RBBLITZ_LAUNCHER_PROFILE";

// Everything the resolution reads, so each step can be supplied without a process.
struct ProfilePathInputs {
  // `--launcher_profile=<path>` (the game spells it `launcher_profile`). Highest
  // priority: an explicitly named file is never second-guessed.
  std::string command_line_value;
  // RBBLITZ_LAUNCHER_PROFILE. Above the default, below an explicit path.
  std::string environment_value;
  // The directory holding the running executable: where the portable marker is looked
  // for, and where a portable profile is written.
  std::filesystem::path executable_dir;
  // The user's roaming application data directory (%APPDATA% on Windows). The default
  // profile is app_data_dir / "rb_blitz" / "launcher.toml".
  std::filesystem::path app_data_dir;
};

// D2's order, first match wins:
//   1. command_line_value
//   2. environment_value
//   3. executable_dir / launcher.toml    when executable_dir / rb_blitz_launcher.portable exists
//   4. app_data_dir / rb_blitz / launcher.toml
// Returns an empty path only when app_data_dir is empty and no override was given, which
// is the caller's cue that this process has nowhere to keep settings.
std::filesystem::path ResolveProfilePath(const ProfilePathInputs& inputs);

// True when the portable marker sits beside the executable. This is D2's exception to
// "settings live in %APPDATA%", and the only case in which the launcher writes into the
// install folder - which is why the folder is otherwise never a candidate.
bool IsPortable(const std::filesystem::path& executable_dir);

// Reads kProfileEnvVar and the platform's roaming app data directory, leaving
// command_line_value to the caller, which has the argv.
ProfilePathInputs ProfilePathInputsFromEnvironment(std::filesystem::path executable_dir);

// The roaming application data directory: %APPDATA% on Windows, then $XDG_CONFIG_HOME,
// then $HOME/.config. Empty when the environment names none of them.
std::filesystem::path RoamingAppDataDir();

}  // namespace rb_blitz::launcher
