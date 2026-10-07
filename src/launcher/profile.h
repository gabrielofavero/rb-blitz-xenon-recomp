// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher profile (docs/plans/launcher-plan.md §4.2, D2): the one file that carries
// what the launcher saves, read by the game itself so a launcher launch and a
// double-click see the same settings (D3).
//
// Two properties drive the design:
//
//   * A save must not destroy what this version does not understand. The file is patched
//     line by line - only the keys this module owns are rewritten, and only when their
//     value actually changed - so comments, unknown keys and unknown tables survive and
//     a load-then-save with no edits is byte-for-byte the same file.
//   * A file that does not parse is reported, never repaired. The file on disk is left
//     alone and the caller refuses to save over it until the user resolves it (D2:
//     "never lose a hand-edited file").
//
// SDK-free on purpose: the game executable and the launcher both compile this, and the
// tests run it without booting anything.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::launcher {

// Bumped only when a change would make an older launcher misread a newer file. A file
// whose version this build does not know is refused rather than reinterpreted.
inline constexpr int kProfileSchemaVersion = 1;

// The three launch targets (D4): common is the retail game, demo its XBLA trial, ultimate
// mounts the Ultimate payload. Stored as a word, so a hand-edited file reads.
enum class LaunchTarget { kCommon, kDemo, kUltimate };

std::string_view LaunchTargetName(LaunchTarget target);
std::optional<LaunchTarget> ParseLaunchTarget(std::string_view text);

// Whether a value is written as a bare token (a number or a bool), a TOML basic string or
// a TOML literal string. Recorded per setting so an unchanged value is written back
// exactly as it was found.
enum class ValueStyle { kBare, kBasic, kLiteral };

// One `[settings]` entry: a cvar name and the value as the file spells it.
struct ProfileSetting {
  std::string key;
  std::string value;  // unquoted
  ValueStyle style = ValueStyle::kBare;
};

// The profile. Only the fields of Contract 2 are modelled; every other byte of the file
// lives in `source_text` and is preserved by the writer.
struct Profile {
  int launcher_version = 1;
  // The file's own record of portable mode. The marker beside the executable is what
  // actually selects the location (D2); this field only says what the file was told.
  bool portable = false;
  // The launcher window's own geometry, in logical points - the same unit SDL reports a
  // window in once the display's content scale is divided out. main() multiplies it by that
  // scale to size the window, so the number means the same thing on a 100% and a 300% display.
  int window_width = 1280;
  int window_height = 840;
  LaunchTarget target = LaunchTarget::kUltimate;
  std::string game_dir;
  std::string user_data_dir;  // empty = the game's own default
  std::string dlc_dir;        // empty = the game's own default
  std::vector<ProfileSetting> settings;  // file order
  // The `[remap]` table, one row per pad control the user rebound and nothing for the ones they
  // did not. The game reads it to rewrite the pad state the guest asks for (src/launcher/remap.h):
  // a table is not a scalar, so it is not one of the `[settings]` keys.
  std::vector<ProfileSetting> remap;  // file order
  // The `[nav]` table (A5): one row per launcher action the user rebound and nothing for the ones
  // they left alone, which is why a profile nobody has rebound has no `[nav]` table at all. Only
  // the launcher reads it - the game's profile reader wants `[remap]` - and it is the one table
  // whose contents can lock a user out of the window that would fix them, which is what
  // `--safe-mode` exists for.
  std::vector<ProfileSetting> nav;  // file order
  // The `[update]` table (D19): the release version the user answered "no" to, and nothing
  // when they have never been asked. It is remembered per version rather than as "the user
  // said no once", which is what makes the launcher ask again when a release newer than the
  // declined one arrives - and never again for the same one. Like `[nav]`, the table exists
  // only while it has something in it, so a profile that has never met an update does not
  // grow an empty one.
  std::string update_declined_version;

  // The document as read, so a save can patch it. Empty when there was no file, in which
  // case a save renders a fresh one. Treat as read-only.
  std::string source_text;

  ProfileSetting* FindSetting(std::string_view key);
  const ProfileSetting* FindSetting(std::string_view key) const;
  // A bare value: an int, a bool, an enum word.
  void Set(std::string_view key, std::string value);
  // A quoted string value, escaped as a TOML basic string on write.
  void SetString(std::string_view key, std::string value);
  // Removes the entry, which drops the key from `[settings]` on the next save. This is
  // what B4 uses to return a setting to its compiled default.
  void EraseSetting(std::string_view key);
};

enum class ProfileStatus {
  kOk,           // read, and usable
  kMissingFile,  // no file yet: the defaults, and a save creates one
  kMalformed,    // the file is broken: left untouched, and saving is refused
};

struct ProfileLoadResult {
  ProfileStatus status = ProfileStatus::kMissingFile;
  std::string error;  // the reason, when status is kMalformed
  Profile profile;    // the defaults when status is kMissingFile

  bool usable() const { return status != ProfileStatus::kMalformed; }
};

// Reads the profile. A missing file is not an error. A malformed file is reported in
// `error`, `usable()` is false, and nothing is written anywhere - so a caller that
// checks `usable()` cannot save over a file it did not understand.
ProfileLoadResult LoadProfile(const std::filesystem::path& path);

// The exact bytes a save would write, without writing them: the document patched with the
// keys this module owns, everything else in `source_text` byte-for-byte as it was. This is
// what lets a caller answer "would saving change anything?" before touching the file, and
// what an export copies. Returns false and fills `error` when `source_text` does not parse.
bool ComposeProfile(const Profile& profile, std::string* text, std::string* error);

// Writes the profile, patching the keys it owns and leaving the rest of the document
// alone. The path comes from ResolveProfilePath, which is what keeps the default out of
// the install folder (D2). Returns false and fills `error` when it cannot write.
//
// The model is authoritative and the file is not re-read, so saving the same Profile
// twice writes the same bytes; `source_text` is not updated by a save. A save whose bytes
// would be identical to what is already on disk does not open the file at all, so the
// mtime of an unchanged profile stays the moment it really last changed.
bool SaveProfile(const std::filesystem::path& path, const Profile& profile, std::string* error);

// Renders a fresh document, for a first run. Omits `[settings]` when there are none.
std::string RenderProfile(const Profile& profile);

// TOML basic-string escaping, used for the string values this module writes and for
// reading them back. `\uXXXX` is not decoded on read (it is kept verbatim) - the profile
// writes no such escape.
std::string QuoteTomlString(std::string_view text);
std::string UnquoteTomlString(std::string_view text);

}  // namespace rb_blitz::launcher
