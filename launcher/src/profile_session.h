// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's write path (docs/plans/launcher-plan.md D2, D3; prompt B4).
//
// P0.3's profile module is the format: it patches the keys it owns into the document it was
// given and leaves everything else byte for byte as it was. This is the session on top of it -
// the file's path, what a save would change, whether saving is allowed at all, and the two
// decisions B4 adds on top of "write the model": a reset that can name what it removes, and the
// settings folder, which moves by a pointer file rather than by a setting inside the profile.
//
// The file is the authority for "did anything change": `Dirty` and `Save` make the same byte
// comparison, so a profile that was edited and edited back is not reported as unsaved, and a
// save that would not change a byte does not touch the file (B4's mtime rule).
//
// Dependency-free (no ImGui, no SDL, no SDK) so all four of B4's verify items run as tests
// rather than as a screenshot.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "game_config.h"
#include "launcher/profile.h"
#include "launcher/profile_path.h"

namespace rb_blitz::launcher {

// What a write did. `ok` with `wrote == false` is the ordinary case of nothing to change: the
// file already held exactly these bytes.
struct SaveOutcome {
  bool ok = true;
  bool wrote = false;
  std::string error;
};

// What a *Reset to defaults* would remove, named exactly (B4): the settings keys that would be
// dropped from `[settings]`, and the launch fields that would be cleared, in words. Empty
// means the reset would do nothing, which is worth saying before the user confirms it.
struct ResetPlan {
  std::vector<std::string> settings;  // cvar names, as the file spells them
  std::vector<std::string> launch;    // "the launch target", "the save location", ...

  bool empty() const { return settings.empty() && launch.empty(); }
};

// The game's own file deciding one row (D3 ranks 3 and 4).
struct RowOverride {
  bool overridden = false;
  std::string game_value;
};

// The sentence the precedence badge shows (B4): what the game's own file says, and why it wins.
// Pure text, so the panel and a headless report say exactly the same thing.
std::string OverrideNoteText(std::string_view launcher_value, std::string_view game_value);

class ProfileSession {
 public:
  // `inputs` is D2's resolution order, kept whole: changing the settings folder re-resolves
  // with it, so an override that named the file still names it afterwards. `load` is what was
  // read, kept whole because a file that did not parse must not be written over (D2).
  ProfileSession(ProfilePathInputs inputs, ProfileLoadResult load);

  Profile& profile() { return load_.profile; }
  const Profile& profile() const { return load_.profile; }

  // The file this session writes. It moves when the settings folder changes.
  const std::filesystem::path& path() const { return path_; }

  // True when the profile is the one beside the launcher: either the older marker asked for it
  // or the user chose that folder. The profile's own `portable` field is written to agree.
  bool portable() const { return portable_; }
  // True when argv or the environment named the file, so the settings folder cannot move it -
  // D2's order puts an explicit choice above the pointer, and the UI says so.
  bool path_from_override() const { return path_from_override_; }
  // The directory the portable marker lives in: the launcher's own, which is where the game's
  // executable and its `rb_blitz.toml` are too (D1: both ship in the payload).
  const std::filesystem::path& executable_dir() const { return inputs_.executable_dir; }

  // False when the file on disk did not parse. The session then refuses to write anything,
  // rather than replacing a file it did not understand (D2: never lose a hand-edited file). One
  // case overrides that refusal: A5's safe mode, where replacing the file the launcher could not
  // read *is* the recovery, and `SafeModeNote` is the sentence that says so.
  bool CanSave() const { return load_.usable() || (safe_mode_ && replacing_); }
  // The reason saving is refused, or empty.
  std::string Refusal() const;

  // A5's `--safe-mode`, and the whole of what it means: the launcher starts from the compiled
  // default for its *own* behaviour - the size the window opens at, which `main()` takes from the
  // defaults rather than from the file - because that is the one thing a profile can set that can
  // leave the launcher unusable. The keys are fixed defaults now, so there is nothing there to
  // recover.
  //
  // What it deliberately does *not* do is stop reading or writing the file. The settings rows, the
  // launch target and `[remap]` load and save as they always did, and the only case where safe mode
  // can write something the ordinary path would refuse is a file that did not parse at all: it is
  // replaced rather than patched, `SafeModeNote` says so on the bottom bar before the button is
  // pressed, and it is the recovery for a user who has no other copy of the file to import.
  void SetSafeMode(bool on);
  bool safe_mode() const { return safe_mode_; }
  // The sentence the bottom bar shows while safe mode is on, and empty when it is not.
  std::string SafeModeNote() const;

  // True when a save would change the file. Recomputed rather than remembered, because it is
  // the same comparison Save makes and a byte comparison of a few hundred bytes is cheaper than
  // a change-tracking bug - and because the profile can be edited straight through `profile()`.
  //
  // A profile with no file yet is dirty only once something differs from what was loaded, so a
  // launcher nobody has touched does not claim unsaved changes on first run.
  bool Dirty() const;

  // True when the file this session writes is on disk. "Saved" is only worth saying when there
  // is something to have saved.
  bool has_file() const;

  // D2's write path: a value equal to the compiled default is *dropped* instead of written, so
  // the file records what differs from the defaults and nothing else. `default_text` and the
  // value's style come from the schema row.
  void SetSetting(std::string_view key, std::string value, std::string_view default_text,
                  ValueStyle style);

  // Writes the profile. Unknown keys, comments and tables in the document it was loaded from
  // are patched around rather than rewritten - that is P0.3's module, and this only decides
  // whether to call it.
  SaveOutcome Save();

  // The same document, to a file the user named. The session's own file is not touched.
  SaveOutcome ExportTo(const std::filesystem::path& target) const;

  // A1's geometry persistence, which is the one thing the launcher keeps without being asked.
  // Only a size that actually changed is written (so a launcher nobody resized neither creates
  // the profile nor touches its mtime), and what is written is the profile as the file last had
  // it plus the new size - a setting the user changed and did not save stays unsaved, which is
  // what the panel says about it.
  //
  // In safe mode this is the one write that is skipped: the window opened at the default size, so
  // the size it ends at says nothing about what the user wants remembered, and a switch for
  // *reading* a broken profile must not be the thing that writes to it. `ok` with `wrote` false
  // is what that looks like.
  SaveOutcome SaveWindowGeometry(int width, int height);

  // D19: records the release version the user answered "Not now" to, so the launcher does not
  // ask about it again - in this session or in any later one. A newer release than the recorded
  // one is asked about on its own, which is the whole reason the version is what is written
  // rather than "the user said no once".
  //
  // The same shape as the geometry write: only a version that actually differs is written (a
  // second "Not now" for the same release touches nothing), what is written is the profile as
  // the file last had it plus this, and a file the launcher could not parse is left alone unless
  // safe mode was asked for - the user's answer to an update prompt is not a request to replace
  // a settings file this build could not read.
  SaveOutcome SaveUpdateDeclined(const std::string& version);

  // Adopts another document as this session's profile: a backup, another machine's file, or a
  // hand-edited one. A file that does not parse is refused and nothing changes - the file the
  // user picked is left exactly where it is (B4's "do not delete a file you failed to parse").
  // The file the session writes does not move, and nothing is written until Save() or the
  // caller saves.
  bool ImportFrom(const std::filesystem::path& source, std::string* error);

  ResetPlan WhatResetWouldRemove() const;
  // Back to the compiled defaults: every recorded setting is dropped and the launch fields are
  // cleared. The window's geometry is not a setting and is left alone, so the launcher does not
  // resize itself out from under the user.
  void ResetToDefaults();

  struct LocationOutcome {
    bool ok = true;
    bool changed = false;  // false when the settings were already in the folder asked for
    std::string error;
  };

  // D2's settings location, chosen rather than toggled: `dir` is the folder that should hold
  // `launcher.toml`, recorded in a pointer file in the default folder and re-resolved so an
  // override that named the file still wins.
  //
  // The settings are carried to the new folder instead of being left behind - a location the
  // user chose and then found empty would be a reset, not a move - and the file in the old one
  // is kept: switching where settings live is not the place to delete a settings file. If the
  // new folder cannot be written the pointer is put back exactly as it was and the failure is
  // reported, because "settings in a folder that cannot be written" is worse than "settings
  // where they were".
  LocationOutcome SetSettingsDir(const std::filesystem::path& dir);

  // The folder the profile is in, and whether that is one the user chose.
  std::filesystem::path settings_dir() const { return path_.parent_path(); }
  bool settings_dir_chosen() const { return !ReadSettingsDirPointer(inputs_.app_data_dir).empty(); }

  // The game's own file, re-read when it changes on disk. Badge input only: the launcher never
  // writes it (D2).
  const GameConfig& game_config() const;

  // Whether the game's own file, not the launcher, decides this row. The launcher passes a row
  // on the command line only when its value differs from the compiled default (D3 rank 1);
  // for every other row the game's file outranks the profile at equal rank, because the game
  // applies it second.
  RowOverride OverrideFor(std::string_view key, std::string_view launcher_value,
                          std::string_view compiled_default) const;

 private:
  void RefreshGameConfig() const;

  ProfilePathInputs inputs_;
  std::filesystem::path path_;
  ProfileLoadResult load_;
  // The profile as the file last had it: what the exit-time geometry write starts from, and the
  // reason a change the user did not save cannot be written by the back door.
  Profile saved_;
  bool portable_ = false;
  bool path_from_override_ = false;

  mutable GameConfig game_config_;
  mutable bool game_config_read_ = false;
  mutable std::filesystem::file_time_type game_config_stamp_{};

  // A5's safe mode: the launcher's own behaviour comes from the defaults (main()), and a file
  // that could not be read may be replaced rather than refused.
  bool safe_mode_ = false;
  bool replacing_ = false;
};

}  // namespace rb_blitz::launcher
