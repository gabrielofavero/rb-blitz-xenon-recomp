// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — the path file and the native settings location (contracts §4.1/§5.5).
//
// `paths.toml` lives beside the installed rb_blitz.exe, one per installation, so its
// discovery never depends on the save directory it selects. Paths are TOML literal strings
// (Windows backslashes need no escaping); a relative value resolves against the directory
// containing the file, never the process working directory.
//
// The native settings file is a sibling of the legacy Controller Settings profile, derived once from
// ResolveProfilePath so the game never starts reading preferences out of the save folder
// when user_data_root changes (§4.1 mapping table):
//
//   <dir>/launcher.toml        -> <dir>/settings.toml
//   <dir>/<stem>.toml          -> <dir>/<stem>.game-settings.toml

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "profile/profile_path.h"

namespace rb_blitz::settings {

inline constexpr const char* kPathFileName = "paths.toml";
inline constexpr int kPathSchemaVersion = 1;

struct PathEntry {
  std::string dlc_root;       // empty = <game_data_root>/dlc
  std::string user_data_root; // empty = the platform Documents default
};

struct PathFileResult {
  enum class Status : uint8_t { kOk, kMissing, kMalformed } status = Status::kMissing;
  int32_t schema_version = kPathSchemaVersion;
  PathEntry paths;
  std::string error;
};

// Caller-supplied environment keeps the installed writable layout testable
// without changing a process's HOME/XDG variables. Empty fields use HOME.
struct NativePathEnvironment {
  std::filesystem::path home;
  std::filesystem::path xdg_data_home;
  std::filesystem::path xdg_config_home;
  std::filesystem::path xdg_cache_home;
  std::filesystem::path xdg_state_home;
};

struct NativeUserRoots {
  std::filesystem::path data;
  std::filesystem::path config;
  std::filesystem::path cache;
  std::filesystem::path state;
};

// Installed, per-user native roots. Does not create folders. Windows keeps its
// existing SDK/installer locations; on Windows this returns empty paths.
NativeUserRoots ResolveNativeUserRoots(const NativePathEnvironment& environment);
NativeUserRoots NativeUserRootsFromEnvironment();

// Windows and explicit portable mode: <executable_dir>/paths.toml.
// Installed Linux/macOS: per-user native config root/paths.toml.
std::filesystem::path ResolvePathFile(const std::filesystem::path& executable_dir);

// Reads the path file. A missing file is not an error and yields the defaults (both empty).
// A malformed file, or one whose schema_version this build does not know, is reported and
// never reinterpreted.
PathFileResult LoadPathFile(const std::filesystem::path& file);

// Writes the path file transactionally: a flushed temporary sibling that atomically replaces
// the destination, keeping the old bytes on failure. Unknown keys and comments survive, and a
// value reset to its default (empty) drops the key rather than writing an empty string.
bool SavePathFile(const std::filesystem::path& file, const PathEntry& entry, std::string* error);

// Resolves one stored path value: empty -> `fallback`; relative -> resolved against the
// directory containing `file`; absolute -> itself.
std::filesystem::path ResolvePathValue(const std::string& value, const std::filesystem::path& file,
                                       const std::filesystem::path& fallback);

// The native settings file that belongs to a resolved legacy profile path (§4.1).
std::filesystem::path SettingsFileForProfile(const std::filesystem::path& legacy_profile);

// Resolves the settings file from Controller Settings-environment inputs, adapting the existing
// profile locator (explicit --launcher_profile / RBBLITZ_CONTROLLER_SETTINGS_PROFILE / settings_dir.txt
// pointer / portable marker / %APPDATA%). Empty only when there is nowhere to keep settings.
std::filesystem::path ResolveSettingsFile(const rb_blitz::profile::ProfilePathInputs& inputs);

}  // namespace rb_blitz::settings
