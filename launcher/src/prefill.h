// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The first run's prefill (docs/plans/launcher-plan.md D4, §4.2; prompt D4).
//
// A fresh launcher has no profile. Rather than making the user retype what the installer
// already wrote down, the first run reads `install-manifest.toml` beside the launcher and
// seeds the General tab from it - the game folder and the launch target the install's
// Ultimate state implies. Nothing is written here: the profile is left dirty so the user
// confirms it with the panel's Save, which is the only thing that creates the file.
//
// Three cases the caller should not have to think about:
//   * no manifest (a hand install, or an installer older than the manifest) -> the file
//     system, exactly as B1 already detects the game root;
//   * a manifest whose paths are gone -> name them and re-scan the file system;
//   * a profile already on disk (a second launcher install, or an upgrade) -> change nothing.
//
// Dependency-free (no ImGui, no SDL, no SDK) so the three cases are tested from fixture
// manifests rather than from an install.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "launcher/profile.h"

namespace rb_blitz::launcher {

// What installer/src/install.cpp's finalize step writes next to the game and the launcher.
inline constexpr const char* kInstallManifestFileName = "install-manifest.toml";

// install-manifest.toml, as far as the launcher reads it. `present` is false for an install
// made by hand or by an installer that predates the manifest; `parsed` is false for a file
// that is there but is not a manifest this launcher understands - never guessed at.
struct InstallManifest {
  std::filesystem::path path;
  bool present = false;
  bool parsed = false;
  std::string error;            // why it did not parse
  std::string install_dir;      // [install] directory
  std::string game_dir;         // [install] game_directory
  std::string payload_commit;   // [install] payload_commit
  std::string game_source;      // [game_data] source
  bool ultimate_installed = false;  // [game_data] ultimate_installed
};

// Reads <dir>/install-manifest.toml. A missing file is not an error.
InstallManifest ReadInstallManifest(const std::filesystem::path& dir);

// What the file system and the session already know, so this module never touches either:
// B1's detection (ultimate_state.h) and whether a profile is already on disk.
struct PrefillInputs {
  bool has_profile = false;         // launcher.toml already exists: never overwrite it
  bool game_root_found = false;     // DetectGameRoots found a game on disk
  std::string detected_game_dir;    // the game root it found (or the installer's layout)
  bool ultimate_available = false;  // kReady or kAlsoPresent
};

// Where a plan's values came from.
enum class PrefillSource { kNone, kManifest, kFileSystem };

std::string_view PrefillSourceName(PrefillSource source);

// What a first run would put in the profile. Never written by this module; ApplyPrefill only
// edits an in-memory Profile.
struct PrefillPlan {
  InstallManifest manifest;
  bool applies = false;             // a first run with something to prefill
  PrefillSource source = PrefillSource::kNone;
  std::string game_dir;             // the value for the profile's game folder
  LaunchTarget target = LaunchTarget::kUltimate;
  std::vector<std::string> missing;  // manifest paths that are no longer on disk
  bool rescanned = false;            // the fallback re-scanned the file system
  std::string note;                  // one sentence for the person, or empty
};

// Decides what a first run would prefill, reading the manifest beside `launcher_dir`.
PrefillPlan PlanPrefill(const std::filesystem::path& launcher_dir, const PrefillInputs& inputs);

// Applies a plan to a profile. Returns true when something changed. Never writes a file: the
// caller leaves the profile dirty and the user's Save is the confirmation (D4's "write the
// first profile only when the user confirms").
bool ApplyPrefill(Profile& profile, const PrefillPlan& plan);

// The plan as text, for --dump-prefill and a bug report.
std::string DescribePrefill(const PrefillPlan& plan);

}  // namespace rb_blitz::launcher
