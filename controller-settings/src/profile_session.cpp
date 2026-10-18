// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of Controller Settings' write path (controller-settings/src/profile_session.h, B4).

#include "profile_session.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace rb_blitz::profile {
namespace {

namespace fs = std::filesystem;

// What the marker says about itself, for the person who finds it in the install folder rather
// than in Controller Settings. Only its existence matters to this code (D2).
constexpr const char* kMarkerText =
    "Portable mode: Controller Settings keeps its settings in launcher.toml beside this file.\n"
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

// The profile is "portable" when the file is the one beside Controller Settings - whether the older
// marker put it there or the user chose that folder. It is a fact about the path, not a flag,
// so there is only ever one of it.
bool IsPortablePath(const fs::path& path, const fs::path& executable_dir) {
  if (path.empty() || executable_dir.empty()) {
    return false;
  }
  return path == executable_dir / kProfileFileName;
}

// --- P8's merge -----------------------------------------------------------
//
// The rule a save follows when the file changed while the window was open: the document on disk is
// the base, and this session contributes only the keys it actually changed. Everything else is the
// other writer's to decide, which is what makes "the game (or a hand edit) touched the file while
// Controller Settings was up" end with both writers' work in the file instead of one of them
// silently losing it.
//
// What counts as "changed" is a comparison against what this session *loaded* (`base`), not
// against the defaults: a key the user never opened the editor on is not an edit, whether or not
// its value happens to differ from the compiled default.

// One table's rows, keyed by name with the first occurrence winning - the same row a lookup finds
// (`Profile::FindSetting`), so the merge and the panel cannot disagree about which one is real.
std::map<std::string, ProfileSetting> IndexRows(const std::vector<ProfileSetting>& rows) {
  std::map<std::string, ProfileSetting> indexed;
  for (const ProfileSetting& row : rows) {
    indexed.emplace(row.key, row);
  }
  return indexed;
}

bool SameRow(const ProfileSetting& left, const ProfileSetting& right) {
  return left.value == right.value && left.style == right.style;
}

// Writes `row` into `rows`: the first entry with that key is replaced in place - so the row keeps
// its position, and with it any comment on the same line - and a key the document does not have is
// appended at the end of the table.
void SetRow(std::vector<ProfileSetting>* rows, const ProfileSetting& row) {
  for (ProfileSetting& candidate : *rows) {
    if (candidate.key == row.key) {
      candidate = row;
      return;
    }
  }
  rows->push_back(row);
}

void RemoveRow(std::vector<ProfileSetting>* rows, std::string_view key) {
  for (auto it = rows->begin(); it != rows->end();) {
    it = it->key == key ? rows->erase(it) : it + 1;
  }
}

// Applies the session's edits to one table: the keys it changed or added, then the keys it removed.
// A key it never touched is left exactly as `target` has it.
void MergeRows(const std::vector<ProfileSetting>& base_rows,
               const std::vector<ProfileSetting>& model_rows,
               std::vector<ProfileSetting>* target) {
  const std::map<std::string, ProfileSetting> base = IndexRows(base_rows);
  const std::map<std::string, ProfileSetting> model = IndexRows(model_rows);
  for (const auto& [key, row] : model) {
    const auto was = base.find(key);
    if (was != base.end() && SameRow(was->second, row)) {
      continue;  // not this session's edit
    }
    SetRow(target, row);
  }
  for (const auto& [key, row] : base) {
    if (model.count(key) != 0) {
      continue;  // still in the model: either untouched, or written above
    }
    RemoveRow(target, key);
  }
}

// `fresh` supplies the document and every key this session did not touch; `base` is what the
// session loaded and `model` is what it holds now, so the difference between those two is exactly
// the work this window is responsible for.
Profile MergeEdits(const Profile& fresh, const Profile& base, const Profile& model) {
  Profile merged = fresh;
  if (model.launcher_version != base.launcher_version) {
    merged.launcher_version = model.launcher_version;
  }
  if (model.portable != base.portable) {
    merged.portable = model.portable;
  }
  if (model.window_width != base.window_width) {
    merged.window_width = model.window_width;
  }
  if (model.window_height != base.window_height) {
    merged.window_height = model.window_height;
  }
  if (model.target != base.target) {
    merged.target = model.target;
  }
  if (model.game_dir != base.game_dir) {
    merged.game_dir = model.game_dir;
  }
  if (model.user_data_dir != base.user_data_dir) {
    merged.user_data_dir = model.user_data_dir;
  }
  if (model.dlc_dir != base.dlc_dir) {
    merged.dlc_dir = model.dlc_dir;
  }
  if (model.update_declined_version != base.update_declined_version) {
    merged.update_declined_version = model.update_declined_version;
  }
  MergeRows(base.settings, model.settings, &merged.settings);
  MergeRows(base.remap, model.remap, &merged.remap);
  // The two gameplay tables are their own tables for the same reason `[remap]` is: a rebase must
  // carry this window's edits to one action without touching the other writer's work in the same
  // file (plan D6 - "rebased saves" is part of P1's storage contract).
  MergeRows(base.gameplay_remap_controller, model.gameplay_remap_controller,
            &merged.gameplay_remap_controller);
  MergeRows(base.gameplay_remap_keyboard_mouse, model.gameplay_remap_keyboard_mouse,
            &merged.gameplay_remap_keyboard_mouse);
  MergeRows(base.nav, model.nav, &merged.nav);
  return merged;
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
  // that is Controller Settings' own rather than the file's is safe mode's window size, which is not a
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
  return "Safe mode: the window opens at the default size rather than the file's.";
}

std::string OverrideNoteText(std::string_view profile_value, std::string_view game_value) {
  return "The game's own rb_blitz.toml sets this to " + std::string(game_value) +
         " and is applied after Controller Settings' profile, so the game will use " +
         std::string(game_value) + ", not " + std::string(profile_value) + ".";
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
    // loaded - otherwise Controller Settings nobody touched would report unsaved changes on first run,
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

  // P8: the document on disk is the base, not this session's snapshot of it.
  RebaseOntoDisk();

  // Asked before the write, because afterwards the answer is always "no difference".
  const bool would_change = Dirty();
  // The bytes the write is about to leave behind, so the session's belief about the file can be
  // updated with what it really wrote rather than with what it thought it would write.
  std::string text;
  std::string error;
  const bool composed = ComposeProfile(load_.profile, &text, &error);
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
  if (composed) {
    NoteWritten(text);
  }
  return outcome;
}

SaveOutcome ProfileSession::SaveWindowGeometry(int width, int height) {
  SaveOutcome outcome;
  if (safe_mode_) {
    // The window opened at the default size (main()), so the size it ends at is not something the
    // user chose - and a switch for reading a profile Controller Settings cannot use must not be the
    // thing that writes to it. Nothing to do is not a failure.
    return outcome;
  }
  // Whether there is anything to write is asked before the rebase and against the size the file
  // last had, because a window the user never resized must not create a profile (A1's rule).
  const bool changed = (width > 0 && width != saved_.window_width) ||
                       (height > 0 && height != saved_.window_height);
  if (!changed) {
    // A Controller Settings nobody resized leaves the file exactly as it is, created or not (A1's rule).
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

  std::string text;
  std::string error;
  // The same rule as Save, and it matters most here: this write starts on every window close, so
  // it is the one most likely to land on a file somebody else has edited since Controller Settings opened.
  RebaseOntoDisk();
  // The new size is applied *after* the rebase, because the rebase replaces what this session
  // believes the file holds - and the size the user just dragged the window to is the one thing
  // this write exists to record.
  if (width > 0) {
    saved_.window_width = width;
  }
  if (height > 0) {
    saved_.window_height = height;
  }
  // What is written is the profile as the file last had it - `saved_` already carries the new
  // size - so an edit the user did not save cannot be written by the back door.
  const bool composed = ComposeProfile(saved_, &text, &error);
  if (!SaveProfile(path_, saved_, &error)) {
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }
  outcome.wrote = true;
  // The model keeps the size too, so the panel's own save cannot write the old one back.
  load_.profile.window_width = saved_.window_width;
  load_.profile.window_height = saved_.window_height;
  if (composed) {
    NoteWritten(text);
  }
  return outcome;
}

void ProfileSession::RebaseOntoDisk() {
  if (path_.empty()) {
    return;
  }
  std::error_code code;
  if (!fs::exists(path_, code) || code) {
    return;  // nothing on disk to rebase onto: a save creates the file from the model
  }
  ProfileLoadResult fresh = LoadProfile(path_);
  if (fresh.status != ProfileStatus::kOk) {
    // A file this build cannot read is not one to merge with. It is left exactly as it is: the
    // save is refused further up (CanSave) unless safe mode asked to replace it, and in that case
    // the replacement is the whole model rather than a merge with a document nobody understood.
    return;
  }
  if (fresh.profile.source_text == saved_.source_text) {
    return;  // the file is the one this session believes it is: nothing changed under us
  }
  // The document changed under us. It becomes the base, and this session's edits are re-applied on
  // top of it - so `saved_` (the file as last had it) is the fresh document, and the model holds
  // the fresh document plus the keys this window changed.
  //
  // `saved_` is what the comparison above is against, and not the model, on purpose: an import
  // (`ImportFrom`) adopts a document the disk has never held, and a session that had just imported
  // one must not have its import mistaken for somebody else's edit and merged away.
  load_.profile = MergeEdits(fresh.profile, saved_, load_.profile);
  saved_ = std::move(fresh.profile);
}

void ProfileSession::NoteWritten(const std::string& written) {
  // The file now holds exactly these bytes, so this is what the model is based on and what the
  // next rebase compares the disk against. Without it a session would read its own output back as
  // an edit by somebody else - harmless when nothing changed, but wrong the moment a second writer
  // is involved.
  load_.profile.source_text = written;
  saved_.source_text = written;
}

SaveOutcome ProfileSession::SaveUpdateDeclined(const std::string& version) {
  SaveOutcome outcome;
  if (version.empty() || version == saved_.update_declined_version) {
    // Nothing to record, or the file already says it: Controller Settings asked about a release it has
    // already declined does not touch the file.
    return outcome;
  }
  if (safe_mode_ && !load_.usable()) {
    // A file that did not parse, in the mode that can replace it: answering an update prompt is
    // not the user asking for that, so the file is left exactly as it is and the question is
    // simply asked again next time.
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

  // `saved_` is the profile as the file last had it, so a setting the user changed and did not
  // save is not written by the back door (the same rule the geometry write follows). The file is
  // rebased first, for the same reason the geometry write is: an answer recorded here must not
  // revert what another writer put in the document.
  RebaseOntoDisk();
  saved_.update_declined_version = version;
  std::string text;
  std::string error;
  const bool composed = ComposeProfile(saved_, &text, &error);
  if (!SaveProfile(path_, saved_, &error)) {
    outcome.ok = false;
    outcome.error = error;
    return outcome;
  }
  outcome.wrote = true;
  // The model keeps it too, so the panel's own save cannot write the old answer back.
  load_.profile.update_declined_version = version;
  if (composed) {
    NoteWritten(text);
  }
  return outcome;
}

SaveOutcome ProfileSession::ExportTo(const fs::path& target) const {  SaveOutcome outcome;
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
  // fill in Controller Settings, not a way to change where it saves.
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
  // ...and so are the two gameplay tables (P1 of the gameplay-mapping plan): a reset that left a
  // custom scheme behind would keep playing it.
  if (!profile.gameplay_remap_controller.empty() ||
      !profile.gameplay_remap_keyboard_mouse.empty()) {
    plan.launch.push_back("every gameplay binding, back to the schemes they ship with");
  }
  // ...and Controller Settings' own keys are one too (A5): a reset that left the ring bound to a key
  // the user was trying to get rid of would be the same mistake.
  if (!profile.nav.empty()) {
    plan.launch.push_back("every Controller Settings key binding, back to the keys it ships with");
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
  profile.gameplay_remap_controller.clear();
  profile.gameplay_remap_keyboard_mouse.clear();
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
    // Controller Settings at a file it refuses to write.
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
        "RBBLITZ_CONTROLLER_SETTINGS_PROFILE, so where the settings live cannot be changed here.";
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

RowOverride ProfileSession::OverrideFor(std::string_view key, std::string_view profile_value,
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
  if (profile_value != compiled_default) {
    // Controller Settings passes this row on the command line, and rank 1 outranks rank 3.
    return result;
  }
  if (found->second == profile_value) {
    return result;  // both files say the same thing, so there is nothing to warn about
  }
  result.overridden = true;
  result.game_value = found->second;
  return result;
}

}  // namespace rb_blitz::profile
