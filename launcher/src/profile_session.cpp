// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the launcher's write path (launcher/src/profile_session.h, B4).

#include "profile_session.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace rb_blitz::launcher {
namespace {

namespace fs = std::filesystem;

// What the marker says about itself, for the person who finds it in the install folder rather
// than in the launcher. Only its existence matters to this code (D2).
constexpr const char* kMarkerText =
    "Portable mode: the launcher keeps its settings in launcher.toml beside this file.\n"
    "Delete this file to keep them in the app data folder again.\n";

bool FileHasBytes(const fs::path& path, const std::string& text) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str() == text;
}

bool WriteMarker(const fs::path& marker, std::string* error) {
  std::ofstream stream(marker, std::ios::binary | std::ios::trunc);
  if (!stream) {
    *error = "cannot create " + marker.string();
    return false;
  }
  stream << kMarkerText;
  if (!stream) {
    *error = "cannot create " + marker.string();
    return false;
  }
  return true;
}

bool RemoveMarker(const fs::path& marker, std::string* error) {
  std::error_code code;
  if (fs::remove(marker, code)) {
    return true;
  }
  if (code) {
    *error = "cannot remove " + marker.string() + ": " + code.message();
    return false;
  }
  return true;  // it was not there, which is the state that was asked for
}

// The profile is "portable" when the file is the one beside the launcher - whether the older
// marker put it there or the user chose that folder. It is a fact about the path, not a flag,
// so there is only ever one of it.
bool IsPortablePath(const fs::path& path, const fs::path& executable_dir) {
  if (path.empty() || executable_dir.empty()) {
    return false;
  }
  return path == executable_dir / kProfileFileName;
}

}  // namespace

ProfileSession::ProfileSession(ProfilePathInputs inputs, ProfileLoadResult load)
    : inputs_(std::move(inputs)), load_(std::move(load)) {
  path_from_override_ =
      !inputs_.command_line_value.empty() || !inputs_.environment_value.empty();
  path_ = ResolveProfilePath(inputs_);
  portable_ = IsPortablePath(path_, inputs_.executable_dir);
  saved_ = load_.profile;
}

std::string ProfileSession::Refusal() const {
  // LoadProfile already names the path and the line, so this adds nothing to it. The one refusal
  // that is the launcher's own rather than the file's is safe mode's window size, which is not a
  // refusal to save the profile at all: it is the one write safe mode skips.
  return load_.usable() ? std::string{} : load_.error;
}

void ProfileSession::SetSafeMode(bool on) {
  safe_mode_ = on;
  // Set once, when safe mode is turned on: a file that did not parse can be replaced by a save,
  // and a file that did parse cannot (there is nothing to recover from).
  replacing_ = on && !load_.usable();
}

std::string ProfileSession::SafeModeNote() const {
  if (!safe_mode_) {
    return {};
  }
  if (replacing_) {
    return "Safe mode: the settings file could not be read, and Save will replace it.";
  }
  return "Safe mode: the launcher's own keys and window size are the defaults, not the file's.";
}

std::string OverrideNoteText(std::string_view launcher_value, std::string_view game_value) {
  return "The game's own rb_blitz.toml sets this to " + std::string(game_value) +
         " and is applied after the launcher's profile, so the game will use " +
         std::string(game_value) + ", not " + std::string(launcher_value) + ".";
}

bool ProfileSession::Dirty() const {
  if (!CanSave() || path_.empty()) {
    return false;
  }
  std::string text;
  std::string error;
  if (!ComposeProfile(load_.profile, &text, &error)) {
    return false;
  }
  if (FileHasBytes(path_, text)) {
    return false;  // the file already says exactly this
  }
  if (!fs::exists(path_)) {
    // No file yet. There is something to save only where the profile differs from what was
    // loaded - otherwise a launcher nobody touched would report unsaved changes on first run,
    // and (A1) creating the file for no reason is exactly what the geometry rule avoids. Both
    // documents are composed from the same template, so equal models give equal text.
    std::string loaded_text;
    if (!ComposeProfile(saved_, &loaded_text, &error)) {
      return false;
    }
    return loaded_text != text;
  }
  return true;
}

bool ProfileSession::has_file() const {
  std::error_code code;
  return !path_.empty() && fs::exists(path_, code);
}

void ProfileSession::SetSetting(std::string_view key, std::string value,
                                std::string_view default_text, ValueStyle style) {
  if (value == default_text) {
    // The file records what differs from the compiled defaults, so returning to the default is
    // recorded by dropping the key rather than by writing the default out.
    load_.profile.EraseSetting(std::string(key));
    return;
  }
  if (style == ValueStyle::kBare) {
    load_.profile.Set(key, std::move(value));
  } else {
    load_.profile.SetString(key, std::move(value));
  }
}

SaveOutcome ProfileSession::Save() {
  SaveOutcome outcome;
  if (!CanSave()) {
    outcome.ok = false;
    outcome.error = Refusal();
    return outcome;
  }
  if (path_.empty()) {
    outcome.ok = false;
    outcome.error = "nowhere to save the settings: no profile path could be resolved";
    return outcome;
  }

  // Asked before the write, because afterwards the answer is always "no difference".
  const bool would_change = Dirty();
  std::string error;
  if (!SaveProfile(path_, load_.profile, &error)) {
    outcome.ok = false;
    outcome.wrote = false;  // nothing reached the file
    outcome.error = error;
    return outcome;
  }
  outcome.wrote = would_change;
  // The file now says what the model says, which is what the exit-time geometry write starts
  // from.
  saved_ = load_.profile;
  return outcome;
}

SaveOutcome ProfileSession::SaveWindowGeometry(int width, int height) {
  SaveOutcome outcome;
  if (safe_mode_) {
    // The window opened at the default size (main()), so the size it ends at is not something the
    // user chose - and a switch for reading a profile the launcher cannot use must not be the
    // thing that writes to it. Nothing to do is not a failure.
    return outcome;
  }
  bool changed = false;
  if (width > 0 && width != saved_.window_width) {
    saved_.window_width = width;
    changed = true;
  }
  if (height > 0 && height != saved_.window_height) {
    saved_.window_height = height;
    changed = true;
  }
  if (!changed) {
    // A launcher nobody resized leaves the file exactly as it is, created or not (A1's rule).
    return outcome;
  }
  if (!CanSave()) {
    outcome.ok = false;
    outcome.error = Refusal();
    return outcome;
  }
  if (path_.empty()) {
    outcome.ok = false;
    outcome.error = "nowhere to save the settings: no profile path could be resolved";
    return outcome;
  }

  std::string error;
  // What is written is the profile as the file last had it - `saved_` already carries the new
  // size - so an edit the user did not save cannot be written by the back door.
  if (!SaveProfile(path_, saved_, &error)) {
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }
  outcome.wrote = true;
  // The model keeps the size too, so the panel's own save cannot write the old one back.
  load_.profile.window_width = saved_.window_width;
  load_.profile.window_height = saved_.window_height;
  return outcome;
}

SaveOutcome ProfileSession::ExportTo(const fs::path& target) const {
  SaveOutcome outcome;
  if (!CanSave()) {
    outcome.ok = false;
    outcome.error = Refusal();
    return outcome;
  }

  std::string text;
  std::string error;
  if (!ComposeProfile(load_.profile, &text, &error)) {
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }
  outcome.wrote = !FileHasBytes(target, text);
  if (!SaveProfile(target, load_.profile, &error)) {
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }
  return outcome;
}

bool ProfileSession::ImportFrom(const fs::path& source, std::string* error) {
  ProfileLoadResult loaded = LoadProfile(source);
  if (!loaded.usable()) {
    *error = loaded.error;
    return false;
  }
  // The imported document replaces the session's, so its unknown keys and comments become the
  // ones a save preserves. The file this session writes does not move: an import is a way to
  // fill in the launcher, not a way to change where it saves.
  load_ = std::move(loaded);
  return true;
}

ResetPlan ProfileSession::WhatResetWouldRemove() const {
  const Profile defaults;
  const Profile& profile = load_.profile;

  ResetPlan plan;
  for (const ProfileSetting& setting : profile.settings) {
    plan.settings.push_back(setting.key);
  }
  if (profile.target != defaults.target) {
    plan.launch.push_back("the launch target");
  }
  if (profile.game_dir != defaults.game_dir) {
    plan.launch.push_back("the game folder override");
  }
  if (profile.user_data_dir != defaults.user_data_dir) {
    plan.launch.push_back("the save location");
  }
  if (profile.dlc_dir != defaults.dlc_dir) {
    plan.launch.push_back("the DLC location");
  }
  // A remap is a change the user made, so a reset that left it behind would be a reset that
  // did not reset the pad.
  if (!profile.remap.empty()) {
    plan.launch.push_back("every button binding, back to the pad as it came");
  }
  // ...and the launcher's own keys are one too (A5): a reset that left the ring bound to a key
  // the user was trying to get rid of would be the same mistake.
  if (!profile.nav.empty()) {
    plan.launch.push_back("every launcher key binding, back to the keys it ships with");
  }
  return plan;
}

void ProfileSession::ResetToDefaults() {
  // The window geometry is not a setting the user chose and is deliberately kept: a reset that
  // resized the window would be a surprise, and A1 saves the geometry on its own.
  const Profile defaults;
  Profile& profile = load_.profile;
  profile.target = defaults.target;
  profile.game_dir = defaults.game_dir;
  profile.user_data_dir = defaults.user_data_dir;
  profile.dlc_dir = defaults.dlc_dir;
  // Every recorded setting goes, which is what makes the next save drop every `[settings]` key.
  profile.settings.clear();
  profile.remap.clear();
  profile.nav.clear();
}

ProfileSession::LocationOutcome ProfileSession::SetSettingsDir(const fs::path& dir) {
  LocationOutcome outcome;
  if (dir.empty()) {
    outcome.ok = false;
    outcome.error = "no folder was chosen";
    return outcome;
  }

  // The pointer is what moves the path; the marker beside the executable is the older answer
  // to the same question. Leaving both behind would leave two answers, so a location the user
  // chose supersedes the marker - the folder named here is the folder, marker or not.
  const fs::path marker = inputs_.executable_dir / kPortableMarkerName;

  const fs::path wanted = dir / kProfileFileName;
  if (wanted == path_) {
    // Already there. The marker still goes, so an explicit choice of the folder the marker
    // asked for leaves one answer behind instead of two.
    if (!IsPortable(inputs_.executable_dir)) {
      return outcome;
    }
  }
  if (!CanSave()) {
    // The settings cannot be carried to the new folder, so moving them there would only point
    // the launcher at a file it refuses to write.
    outcome.ok = false;
    outcome.error = Refusal();
    return outcome;
  }
  if (path_from_override_) {
    // Writing the pointer would record a choice that has no effect, and the next run would
    // disagree with this one. Saying so is the honest answer instead.
    outcome.ok = false;
    outcome.error =
        "This process was told which settings file to use, by --launcher_profile or "
        "RBBLITZ_LAUNCHER_PROFILE, so where the settings live cannot be changed here.";
    return outcome;
  }

  const fs::path previous_pointer = ReadSettingsDirPointer(inputs_.app_data_dir);
  const fs::path previous_path = path_;
  const bool previous_portable = portable_;
  const bool previous_field = load_.profile.portable;

  std::string error;
  if (!WriteSettingsDirPointer(inputs_.app_data_dir, dir, &error)) {
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }
  RemoveMarker(marker, &error);

  // D2's order is re-resolved rather than assumed: an override that named the file goes on
  // naming it, and then the answer is that the pointer did not move anything.
  path_ = ResolveProfilePath(inputs_);
  portable_ = IsPortablePath(path_, inputs_.executable_dir);
  load_.profile.portable = portable_;  // the file's own record, which decides nothing (D2)

  // The settings follow the choice instead of the choice looking like it discarded them. The
  // file left behind is not deleted: nothing here removes a settings file.
  if (!path_.empty() && !SaveProfile(path_, load_.profile, &error)) {
    // A folder that cannot be written is not an improvement over the one that worked, so the
    // pointer and the marker are put back exactly as they were and the failure is reported.
    std::string rollback_error;
    WriteSettingsDirPointer(inputs_.app_data_dir, previous_pointer, &rollback_error);
    if (previous_portable && !IsPortable(inputs_.executable_dir)) {
      WriteMarker(marker, &rollback_error);
    }
    path_ = previous_path;
    portable_ = previous_portable;
    load_.profile.portable = previous_field;
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }

  // A move that ended up at the same file - because the override decided it, say - is not a
  // change, and the status line should not claim one.
  outcome.changed = path_ != previous_path || previous_portable != portable_;
  return outcome;
}

void ProfileSession::RefreshGameConfig() const {
  const fs::path path = inputs_.executable_dir / kGameConfigFileName;
  std::error_code code;
  const fs::file_time_type stamp = fs::last_write_time(path, code);
  const bool exists = !code;
  if (game_config_read_ && game_config_.present == exists &&
      (!exists || stamp == game_config_stamp_)) {
    return;
  }
  game_config_ = ReadGameConfig(inputs_.executable_dir);
  game_config_stamp_ = stamp;
  game_config_read_ = true;
}

const GameConfig& ProfileSession::game_config() const {
  RefreshGameConfig();
  return game_config_;
}

RowOverride ProfileSession::OverrideFor(std::string_view key, std::string_view launcher_value,
                                        std::string_view compiled_default) const {
  RowOverride result;
  const GameConfig& config = game_config();
  if (config.unreadable) {
    return result;
  }
  const auto found = config.values.find(std::string(key));
  if (found == config.values.end()) {
    return result;
  }
  if (launcher_value != compiled_default) {
    // The launcher passes this row on the command line, and rank 1 outranks rank 3.
    return result;
  }
  if (found->second == launcher_value) {
    return result;  // both files say the same thing, so there is nothing to warn about
  }
  result.overridden = true;
  result.game_value = found->second;
  return result;
}

}  // namespace rb_blitz::launcher
