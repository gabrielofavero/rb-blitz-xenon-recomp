// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the launcher's write path (docs/plans/launcher-plan.md D2, D3; prompt B4):
// the four things B4 has to be able to show, plus the rules they are built on.
//
//   1. a profile with unknown keys and comments round-trips
//   2. a save with nothing to change does not touch the file (its mtime included)
//   3. a file that cannot be written is reported, not crashed into
//   4. a settings folder that cannot be written is reported, and the location stays where it
//      was
//
// No ImGui, no SDL, no game data, no boot: the panel is the only part of B4 that needs a
// window, and everything it decides is here.
//
// Two notes on how the unwritable cases are built. A plain test cannot reach Windows ACLs, so
// "read-only" is the file's own read-only attribute (`std::ofstream` refuses it, which is what
// the launcher meets in the field) and "the install folder cannot be written" is a path whose
// parent is a regular file - the same failure, staged differently. And a read-only *directory*
// is not a thing Windows enforces for creation, so it is not used as a fixture.

#include "check.h"

#include "game_config.h"
#include "launcher/profile.h"
#include "launcher/profile_path.h"
#include "profile_session.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace rb_blitz::launcher;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

void CheckStrEq(const char* file, int line, std::string_view actual, std::string_view expected) {
  ++g_checks;
  if (actual != expected) {
    Fail(file, line, "expected \"" + std::string(expected) + "\", got \"" + std::string(actual) +
                         "\"");
  }
}

void CheckContains(const char* file, int line, std::string_view haystack,
                   std::string_view needle) {
  ++g_checks;
  if (haystack.find(needle) == std::string_view::npos) {
    Fail(file, line, "expected to find \"" + std::string(needle) + "\" in \"" +
                         std::string(haystack) + "\"");
  }
}

void CheckNotContains(const char* file, int line, std::string_view haystack,
                      std::string_view needle) {
  ++g_checks;
  if (haystack.find(needle) != std::string_view::npos) {
    Fail(file, line, "expected not to find \"" + std::string(needle) + "\" in \"" +
                         std::string(haystack) + "\"");
  }
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq(__FILE__, __LINE__, actual, expected)
#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, haystack, needle)
#define CHECK_NOT_CONTAINS(haystack, needle) \
  CheckNotContains(__FILE__, __LINE__, haystack, needle)

void WriteText(const fs::path& path, std::string_view text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

std::string ReadText(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void MakeReadOnly(const fs::path& path, bool read_only) {
  std::error_code ec;
  fs::permissions(path, read_only ? fs::perms::owner_read : fs::perms::owner_all,
                  fs::perm_options::replace, ec);
}

// A session for `install_dir` whose settings live in `app_data`, which is D2's default. The
// portable tests then move it with the marker, exactly as the launcher does.
ProfileSession MakeSession(const fs::path& install_dir, const fs::path& app_data,
                           ProfileLoadResult load = {}) {
  ProfilePathInputs inputs;
  inputs.executable_dir = install_dir;
  inputs.app_data_dir = app_data;
  return ProfileSession(std::move(inputs), std::move(load));
}

// A document with everything a hand-edited file can have: comments, an unknown key, an unknown
// table, and one setting.
constexpr const char* kHandEdited =
    "# my notes, which must survive\n"
    "schema_version = 1\n"
    "future_key = \"a newer launcher wrote this\"\n"
    "\n"
    "[launcher]\n"
    "version = 1\n"
    "portable = false\n"
    "\n"
    "[window]\n"
    "width = 1100\n"
    "height = 720\n"
    "\n"
    "[launch]\n"
    "target = \"ultimate\"\n"
    "game_dir = \"\"\n"
    "user_data_dir = \"\"\n"
    "dlc_dir = \"\"\n"
    "\n"
    "[settings]\n"
    "fullscreen = false   # the user turned it off\n"
    "\n"
    "[future_table]\n"
    "answer = 42\n";

struct Scratch {
  fs::path base;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-launcher-session";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
  }

  ~Scratch() {
    std::error_code ec;
    // A test that leaves a read-only file behind must still clean up.
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(base, ec)) {
      MakeReadOnly(entry.path(), false);
    }
    fs::remove_all(base, ec);
  }

  fs::path Make(std::string_view name) const {
    const fs::path dir = base / name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
  }
};

void TestRoundTrip(Scratch& scratch) {
  BeginCase("D2: a hand-edited file round-trips, comments, unknown keys and unknown tables");

  const fs::path install = scratch.Make("round-trip-install");
  const fs::path app_data = scratch.Make("round-trip-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  WriteText(profile_path, kHandEdited);

  ProfileSession session = MakeSession(install, app_data, LoadProfile(profile_path));
  CHECK_TRUE(session.CanSave());
  CHECK_STR_EQ(session.path().string(), profile_path.string());

  // Nothing was edited, so there is nothing to write - and D2's "never lose a hand-edited
  // file" starts with not rewriting it for no reason.
  CHECK_FALSE(session.Dirty());
  const SaveOutcome untouched = session.Save();
  CHECK_TRUE(untouched.ok);
  CHECK_FALSE(untouched.wrote);
  CHECK_STR_EQ(ReadText(profile_path), kHandEdited);

  BeginCase("B4: a save writes only what changed, and still keeps the rest of the document");

  session.profile().user_data_dir = "D:\\Saves";
  CHECK_TRUE(session.Dirty());

  const SaveOutcome saved = session.Save();
  CHECK_TRUE(saved.ok);
  CHECK_TRUE(saved.wrote);

  const std::string text = ReadText(profile_path);
  CHECK_CONTAINS(text, "# my notes, which must survive");
  CHECK_CONTAINS(text, "future_key = \"a newer launcher wrote this\"");
  CHECK_CONTAINS(text, "[future_table]");
  CHECK_CONTAINS(text, "answer = 42");
  CHECK_CONTAINS(text, "fullscreen = false");
  CHECK_CONTAINS(text, "user_data_dir = \"D:\\\\Saves\"");
  CHECK_FALSE(session.Dirty());

  BeginCase("D2: returning a setting to its compiled default drops the key");

  // The file records what differs from the defaults, so "back to the default" is the absence of
  // the key rather than the default written out (D2's `[settings]` example).
  session.SetSetting("fullscreen", "true", "true", ValueStyle::kBare);
  CHECK_TRUE(session.Dirty());
  const SaveOutcome dropped = session.Save();
  CHECK_TRUE(dropped.ok);
  CHECK_TRUE(dropped.wrote);
  const std::string without = ReadText(profile_path);
  CHECK_NOT_CONTAINS(without, "fullscreen");
  CHECK_CONTAINS(without, "# my notes, which must survive");
  CHECK_CONTAINS(without, "future_key = \"a newer launcher wrote this\"");
  CHECK_CONTAINS(without, "answer = 42");

  BeginCase("D2: a value that differs from the default is written");

  session.SetSetting("fullscreen", "false", "true", ValueStyle::kBare);
  CHECK_TRUE(session.Save().wrote);
  CHECK_CONTAINS(ReadText(profile_path), "fullscreen = false");
}

void TestSaveWithNothingChanged(Scratch& scratch) {
  BeginCase("B4: a save with no changes does not touch the file, mtime included");

  const fs::path install = scratch.Make("mtime-install");
  const fs::path app_data = scratch.Make("mtime-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  WriteText(profile_path, kHandEdited);

  // A timestamp from an hour ago, so "unchanged" is a fact rather than a coincidence of two
  // writes landing in the same tick.
  const auto an_hour_ago = fs::file_time_type::clock::now() - std::chrono::hours(1);
  std::error_code ec;
  fs::last_write_time(profile_path, an_hour_ago, ec);

  ProfileSession session = MakeSession(install, app_data, LoadProfile(profile_path));
  const SaveOutcome outcome = session.Save();
  CHECK_TRUE(outcome.ok);
  CHECK_FALSE(outcome.wrote);
  CHECK_TRUE(fs::last_write_time(profile_path, ec) == an_hour_ago);

  BeginCase("B4: a file that cannot be written is reported, not crashed into");

  session.profile().user_data_dir = "D:\\Saves";
  MakeReadOnly(profile_path, true);
  const SaveOutcome refused = session.Save();
  CHECK_FALSE(refused.ok);
  CHECK_FALSE(refused.wrote);
  CHECK_CONTAINS(refused.error, "cannot write");
  // And the read-only file is exactly as it was: a failed save changes nothing.
  CHECK_STR_EQ(ReadText(profile_path), kHandEdited);
  MakeReadOnly(profile_path, false);

  BeginCase("B4: nothing to change means a read-only file is left alone, and not an error");
  // The state the user asked for is already on disk, so there is no write to refuse - the file
  // stays read-only and the save is still a success.
  session.profile().user_data_dir.clear();
  MakeReadOnly(profile_path, true);
  const SaveOutcome nothing_to_do = session.Save();
  CHECK_TRUE(nothing_to_do.ok);
  CHECK_FALSE(nothing_to_do.wrote);
  CHECK_STR_EQ(ReadText(profile_path), kHandEdited);
  MakeReadOnly(profile_path, false);

  BeginCase("D2: a file that did not parse is never written over");

  const fs::path broken_path = app_data / "rb_blitz" / "broken.toml";
  WriteText(broken_path, "schema_version = \"one\"\n");
  const ProfileLoadResult broken = LoadProfile(broken_path);
  CHECK_FALSE(broken.usable());

  ProfileSession broken_session = MakeSession(install, app_data, broken);
  CHECK_FALSE(broken_session.CanSave());
  CHECK_CONTAINS(broken_session.Refusal(), "schema_version");
  CHECK_FALSE(broken_session.Dirty());
  const SaveOutcome refused_broken = broken_session.Save();
  CHECK_FALSE(refused_broken.ok);
  CHECK_STR_EQ(ReadText(broken_path), "schema_version = \"one\"\n");

  BeginCase("D2: no resolvable path is refused with a reason");

  ProfilePathInputs nowhere;
  nowhere.executable_dir = install;
  ProfileSession nowhere_session(std::move(nowhere), ProfileLoadResult{});
  CHECK_TRUE(nowhere_session.path().empty());
  const SaveOutcome nowhere_outcome = nowhere_session.Save();
  CHECK_FALSE(nowhere_outcome.ok);
  CHECK_CONTAINS(nowhere_outcome.error, "nowhere to save");
}

void TestWindowGeometry(Scratch& scratch) {
  BeginCase("A1/B4: the window size is kept on the way out, and only when it changed");

  const fs::path install = scratch.Make("geometry-install");
  const fs::path app_data = scratch.Make("geometry-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  ProfileSession session = MakeSession(install, app_data);

  // A launcher nobody resized does not create the profile at all (A1's rule, kept).
  const SaveOutcome unchanged = session.SaveWindowGeometry(1280, 840);
  CHECK_TRUE(unchanged.ok);
  CHECK_FALSE(unchanged.wrote);
  CHECK_FALSE(fs::exists(profile_path));

  BeginCase("B4: a resize is written, and a setting that was not saved is not");

  session.profile().user_data_dir = "D:\\Saves";  // a change the user did not save
  const SaveOutcome resized = session.SaveWindowGeometry(1280, 800);
  CHECK_TRUE(resized.ok);
  CHECK_TRUE(resized.wrote);
  const std::string resized_text = ReadText(profile_path);
  CHECK_CONTAINS(resized_text, "width = 1280");
  CHECK_CONTAINS(resized_text, "height = 800");
  CHECK_NOT_CONTAINS(resized_text, "D:\\\\Saves");
  CHECK_FALSE(session.SaveWindowGeometry(1280, 800).wrote);

  BeginCase("B4: the panel's save then writes the setting, and not a stale window size");

  const SaveOutcome saved = session.Save();
  CHECK_TRUE(saved.ok);
  CHECK_TRUE(saved.wrote);
  const std::string saved_text = ReadText(profile_path);
  CHECK_CONTAINS(saved_text, "width = 1280");
  CHECK_CONTAINS(saved_text, "height = 800");
  CHECK_CONTAINS(saved_text, "user_data_dir = \"D:\\\\Saves\"");

  BeginCase("B4: the exit write cannot put an unsaved change in the file by the back door");

  session.profile().user_data_dir = "E:\\Other";
  const SaveOutcome exit_resize = session.SaveWindowGeometry(1024, 768);
  CHECK_TRUE(exit_resize.ok);
  const std::string after_exit = ReadText(profile_path);
  CHECK_CONTAINS(after_exit, "width = 1024");
  CHECK_CONTAINS(after_exit, "user_data_dir = \"D:\\\\Saves\"");
  CHECK_NOT_CONTAINS(after_exit, "E:\\\\Other");
}

void TestWindowGeometryUnwritable(Scratch& scratch) {
  BeginCase("B4: a resize that cannot be written is reported, and the session survives it");

  const fs::path install = scratch.Make("geometry-readonly-install");
  const fs::path app_data = scratch.Make("geometry-readonly-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  WriteText(profile_path, kHandEdited);
  ProfileSession session = MakeSession(install, app_data, LoadProfile(profile_path));

  MakeReadOnly(profile_path, true);
  const SaveOutcome outcome = session.SaveWindowGeometry(1280, 800);
  CHECK_FALSE(outcome.ok);
  CHECK_CONTAINS(outcome.error, "cannot write");
  CHECK_STR_EQ(ReadText(profile_path), kHandEdited);
  MakeReadOnly(profile_path, false);
}

void TestFreshProfile(Scratch& scratch) {
  BeginCase("B4: a profile that has never been saved is not 'unsaved changes'");

  const fs::path install = scratch.Make("fresh-install");
  const fs::path app_data = scratch.Make("fresh-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  ProfileSession session = MakeSession(install, app_data);

  CHECK_TRUE(session.CanSave());
  CHECK_FALSE(session.has_file());
  CHECK_FALSE(session.Dirty());

  BeginCase("B4: changing something in it is, and saving creates the file");

  session.profile().target = LaunchTarget::kDemo;
  CHECK_TRUE(session.Dirty());

  const SaveOutcome saved = session.Save();
  CHECK_TRUE(saved.ok);
  CHECK_TRUE(saved.wrote);
  CHECK_TRUE(session.has_file());
  CHECK_FALSE(session.Dirty());
  const std::string text = ReadText(profile_path);
  CHECK_CONTAINS(text, "target = \"demo\"");
  CHECK_CONTAINS(text, "schema_version = 1");

  BeginCase("B4: a fresh profile edited back to the defaults has nothing to save");

  const fs::path other_app_data = scratch.Make("fresh-appdata-2");
  ProfileSession reverted = MakeSession(scratch.Make("fresh-install-2"), other_app_data);
  reverted.profile().target = LaunchTarget::kCommon;
  CHECK_TRUE(reverted.Dirty());
  reverted.profile().target = LaunchTarget::kUltimate;
  CHECK_FALSE(reverted.Dirty());
  // So a save here would not create a file either (the same rule the geometry write follows).
  CHECK_FALSE(reverted.SaveWindowGeometry(1280, 840).wrote);
  CHECK_FALSE(fs::exists(other_app_data / "rb_blitz" / "launcher.toml"));
}

void TestReset(Scratch& scratch) {
  BeginCase("B4: a reset names exactly what it will remove, before it removes it");

  const fs::path install = scratch.Make("reset-install");
  const fs::path app_data = scratch.Make("reset-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  WriteText(profile_path, kHandEdited);

  ProfileSession session = MakeSession(install, app_data, LoadProfile(profile_path));
  session.profile().target = LaunchTarget::kCommon;
  session.profile().user_data_dir = "D:\\Saves";
  session.profile().dlc_dir = "D:\\Dlc";

  const ResetPlan plan = session.WhatResetWouldRemove();
  CHECK_FALSE(plan.empty());
  CHECK_TRUE(plan.settings.size() == 1);
  CHECK_STR_EQ(plan.settings[0], "fullscreen");
  CHECK_TRUE(plan.launch.size() == 3);
  CHECK_STR_EQ(plan.launch[0], "the launch target");
  CHECK_STR_EQ(plan.launch[1], "the save location");
  CHECK_STR_EQ(plan.launch[2], "the DLC location");

  BeginCase("B4: the reset clears them, and the window's geometry survives it");

  session.profile().window_width = 1280;
  session.ResetToDefaults();
  CHECK_TRUE(session.profile().target == LaunchTarget::kUltimate);
  CHECK_TRUE(session.profile().user_data_dir.empty());
  CHECK_TRUE(session.profile().dlc_dir.empty());
  CHECK_TRUE(session.profile().settings.empty());
  CHECK_TRUE(session.profile().window_width == 1280);
  CHECK_TRUE(session.WhatResetWouldRemove().empty());

  const SaveOutcome saved = session.Save();
  CHECK_TRUE(saved.ok);
  CHECK_TRUE(saved.wrote);
  const std::string text = ReadText(profile_path);
  CHECK_NOT_CONTAINS(text, "fullscreen");
  CHECK_NOT_CONTAINS(text, "user_data_dir = \"D:");
  // The document's other keys are still the user's.
  CHECK_CONTAINS(text, "future_key = \"a newer launcher wrote this\"");
  CHECK_CONTAINS(text, "[future_table]");
}

void TestImportExport(Scratch& scratch) {
  BeginCase("B4: an export writes the same document somewhere else");

  const fs::path install = scratch.Make("export-install");
  const fs::path app_data = scratch.Make("export-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  WriteText(profile_path, kHandEdited);
  ProfileSession session = MakeSession(install, app_data, LoadProfile(profile_path));

  const fs::path exported = scratch.base / "backup" / "launcher.toml";
  const SaveOutcome outcome = session.ExportTo(exported);
  CHECK_TRUE(outcome.ok);
  CHECK_TRUE(outcome.wrote);
  CHECK_STR_EQ(ReadText(exported), ReadText(profile_path));
  // The session's own file is not the export's business.
  CHECK_STR_EQ(ReadText(profile_path), kHandEdited);

  BeginCase("B4: an import adopts a document, and a broken one is refused and kept");

  const fs::path other = scratch.base / "other.toml";
  WriteText(other, "# from another machine\nschema_version = 1\nnew_key = 7\n[settings]\n"
                   "fullscreen = true\n");
  std::string error;
  CHECK_TRUE(session.ImportFrom(other, &error));
  CHECK_TRUE(session.profile().FindSetting("fullscreen") != nullptr);
  CHECK_STR_EQ(session.profile().FindSetting("fullscreen")->value, "true");
  CHECK_TRUE(session.Dirty());
  // Nothing is written until the caller saves, so the document it replaced is still the file.
  CHECK_STR_EQ(ReadText(profile_path), kHandEdited);

  session.Save();
  const std::string adopted = ReadText(profile_path);
  CHECK_CONTAINS(adopted, "# from another machine");
  CHECK_CONTAINS(adopted, "new_key = 7");
  CHECK_NOT_CONTAINS(adopted, "# my notes, which must survive");

  const fs::path broken = scratch.base / "broken-import.toml";
  WriteText(broken, "schema_version = \"one\"\n");
  CHECK_FALSE(session.ImportFrom(broken, &error));
  CHECK_CONTAINS(error, "schema_version");
  // The session is untouched - saving now would still change nothing - and so is the file it
  // was pointed at.
  CHECK_TRUE(session.profile().FindSetting("fullscreen") != nullptr);
  CHECK_FALSE(session.Dirty());
  CHECK_STR_EQ(ReadText(broken), "schema_version = \"one\"\n");
}

void TestSettingsLocation(Scratch& scratch) {
  BeginCase("D2: the settings folder is a choice, and moving it carries the settings over");

  const fs::path install = scratch.Make("location-install");
  const fs::path app_data = scratch.Make("location-appdata");
  const fs::path other = scratch.Make("location-other");
  const fs::path roaming = app_data / "rb_blitz" / "launcher.toml";
  WriteText(roaming, kHandEdited);

  ProfileSession session = MakeSession(install, app_data, LoadProfile(roaming));
  CHECK_FALSE(session.portable());
  CHECK_FALSE(session.path_from_override());
  CHECK_FALSE(session.settings_dir_chosen());
  CHECK_STR_EQ(session.path().string(), roaming.string());

  const ProfileSession::LocationOutcome moved = session.SetSettingsDir(other);
  CHECK_TRUE(moved.ok);
  CHECK_TRUE(moved.changed);
  CHECK_TRUE(session.settings_dir_chosen());
  CHECK_STR_EQ(session.path().string(), (other / kProfileFileName).string());
  // The settings followed the choice rather than being discarded by it: the new file is the
  // document that was in the old one.
  const std::string carried = ReadText(other / kProfileFileName);
  CHECK_CONTAINS(carried, "# my notes, which must survive");
  CHECK_CONTAINS(carried, "future_key = \"a newer launcher wrote this\"");
  CHECK_CONTAINS(carried, "fullscreen = false");
  // And the file they came from is still there: switching location is not deleting.
  CHECK_STR_EQ(ReadText(roaming), kHandEdited);

  BeginCase("D2: the location survives a re-resolve, which is what the next run does");

  ProfilePathInputs inputs;
  inputs.executable_dir = install;
  inputs.app_data_dir = app_data;
  CHECK_STR_EQ(ResolveProfilePath(inputs).string(), (other / kProfileFileName).string());

  BeginCase("B4: the launcher's own folder is a location like any other, and is portable");

  const ProfileSession::LocationOutcome beside = session.SetSettingsDir(install);
  CHECK_TRUE(beside.ok);
  CHECK_TRUE(beside.changed);
  CHECK_TRUE(session.portable());
  CHECK_STR_EQ(session.path().string(), (install / kProfileFileName).string());

  BeginCase("B4: choosing the default folder removes the pointer instead of recording it");

  const ProfileSession::LocationOutcome back = session.SetSettingsDir(app_data / "rb_blitz");
  CHECK_TRUE(back.ok);
  CHECK_TRUE(back.changed);
  CHECK_FALSE(session.settings_dir_chosen());
  CHECK_FALSE(session.portable());
  CHECK_STR_EQ(session.path().string(), roaming.string());
  CHECK_FALSE(fs::exists(app_data / "rb_blitz" / kSettingsDirFileName));

  BeginCase("B4: asking for the folder it is already in changes nothing");

  const ProfileSession::LocationOutcome again = session.SetSettingsDir(app_data / "rb_blitz");
  CHECK_TRUE(again.ok);
  CHECK_FALSE(again.changed);

  BeginCase("B4: a location that cannot be written is reported, and nothing moves");

  const fs::path blocker = scratch.base / "location-blocked";
  WriteText(blocker / "settings", "not a folder");
  const fs::path blocked_dir = blocker / "settings" / "nested";
  ProfileSession blocked = MakeSession(install, app_data, LoadProfile(roaming));

  const ProfileSession::LocationOutcome blocked_move = blocked.SetSettingsDir(blocked_dir);
  CHECK_FALSE(blocked_move.ok);
  CHECK_FALSE(blocked_move.changed);
  CHECK_CONTAINS(blocked_move.error, "cannot write");
  CHECK_FALSE(blocked.settings_dir_chosen());
  CHECK_STR_EQ(blocked.path().string(), roaming.string());

  BeginCase("B4: an override names the file, so the location cannot move out from under it");

  const fs::path named = scratch.base / "named.toml";
  WriteText(named, kHandEdited);
  ProfilePathInputs override_inputs;
  override_inputs.command_line_value = named.string();
  override_inputs.executable_dir = install;
  override_inputs.app_data_dir = app_data;
  ProfileSession pinned(std::move(override_inputs), LoadProfile(named));
  CHECK_TRUE(pinned.path_from_override());

  const ProfileSession::LocationOutcome refused = pinned.SetSettingsDir(other);
  CHECK_FALSE(refused.ok);
  CHECK_FALSE(refused.changed);
  CHECK_CONTAINS(refused.error, "launcher_profile");
  CHECK_FALSE(pinned.settings_dir_chosen());
  CHECK_STR_EQ(pinned.path().string(), named.string());

  BeginCase("D2: the older marker beside the launcher still moves the settings, and a chosen "
            "folder supersedes it");

  WriteText(install / kPortableMarkerName, "");
  // A fresh session: the path is resolved when the session is made, so the marker is what
  // makes it the launcher-folder file - the same thing a user who copies the marker in sees
  // on the next run.
  ProfileSession marked = MakeSession(install, app_data, LoadProfile(roaming));
  CHECK_FALSE(marked.settings_dir_chosen());
  CHECK_STR_EQ(marked.path().string(), (install / kProfileFileName).string());

  const ProfileSession::LocationOutcome chosen = marked.SetSettingsDir(other);
  CHECK_TRUE(chosen.ok);
  CHECK_TRUE(chosen.changed);
  // One answer, not two: the marker is gone and the pointer is what says where the settings are.
  CHECK_FALSE(fs::exists(install / kPortableMarkerName));
  CHECK_STR_EQ(marked.path().string(), (other / kProfileFileName).string());
  CHECK_STR_EQ(ResolveProfilePath(inputs).string(), (other / kProfileFileName).string());
}

void TestGameConfigReader(Scratch& scratch) {
  BeginCase("B4: the game's own file is read as the game writes it");

  const fs::path game_root = scratch.Make("game-config");
  WriteText(game_root / kGameConfigFileName,
            "# Auto-generated cvar configuration\n"
            "fullscreen = false\n"
            "vsync = true\n"
            "video_mode_width = 1600\n"
            "user_data_root = \"D:\\\\Saves\\\\rb_blitz\"   # a # inside is not a comment\n"
            "title = 'literal'\n"
            "channels = [1, 2]\n"
            "\n"
            "[log]\n"
            "levels = 3\n");

  const GameConfig config = ReadGameConfig(game_root);
  CHECK_TRUE(config.present);
  CHECK_FALSE(config.unreadable);
  CHECK_TRUE(config.values.size() == 5);
  CHECK_STR_EQ(config.values.at("fullscreen"), "false");
  CHECK_STR_EQ(config.values.at("video_mode_width"), "1600");
  CHECK_STR_EQ(config.values.at("user_data_root"), "D:\\Saves\\rb_blitz");
  CHECK_STR_EQ(config.values.at("title"), "literal");
  CHECK_TRUE(config.values.find("channels") == config.values.end());
  CHECK_TRUE(config.values.find("levels") == config.values.end());

  BeginCase("B4: a missing file is the ordinary case, not a warning");

  const fs::path bare = scratch.Make("game-config-missing");
  const GameConfig absent = ReadGameConfig(bare);
  CHECK_FALSE(absent.present);
  CHECK_FALSE(absent.unreadable);
  CHECK_TRUE(absent.values.empty());

  BeginCase("B4: a file this reader cannot interpret is named as such, not guessed at");

  const fs::path odd = scratch.Make("game-config-odd");
  WriteText(odd / kGameConfigFileName, "fullscreen false\n");
  const GameConfig odd_config = ReadGameConfig(odd);
  CHECK_TRUE(odd_config.present);
  CHECK_TRUE(odd_config.unreadable);
  CHECK_CONTAINS(odd_config.error, "is not `name = value`");
  CHECK_TRUE(odd_config.values.empty());
}

void TestPrecedence(Scratch& scratch) {
  BeginCase("D3: the game's own file decides a row the launcher does not pass on argv");

  const fs::path install = scratch.Make("precedence-install");
  const fs::path app_data = scratch.Make("precedence-appdata");
  const fs::path profile_path = app_data / "rb_blitz" / "launcher.toml";
  WriteText(profile_path, "# the launcher's own file\nschema_version = 1\n[settings]\nvsync = true\n");
  WriteText(install / kGameConfigFileName,
            "# Auto-generated cvar configuration\n"
            "fullscreen = false\n"
            "vsync = true\n");

  ProfileSession session = MakeSession(install, app_data, LoadProfile(profile_path));

  // The launcher's value is the compiled default, so B7 passes nothing on the command line and
  // rank 3 beats rank 4.
  const RowOverride over = session.OverrideFor("fullscreen", "true", "true");
  CHECK_TRUE(over.overridden);
  CHECK_STR_EQ(over.game_value, "false");

  BeginCase("D3: rank 1 still wins when the launcher does pass the row");

  const RowOverride argv_wins = session.OverrideFor("fullscreen", "false", "true");
  CHECK_FALSE(argv_wins.overridden);

  BeginCase("rows the files agree about, and rows the file does not mention, are not badges");

  CHECK_FALSE(session.OverrideFor("vsync", "true", "true").overridden);
  CHECK_FALSE(session.OverrideFor("safe_area", "0", "0").overridden);

  BeginCase("B4: adopting the game's value writes it to the launcher's own file only");

  const std::string game_text = ReadText(install / kGameConfigFileName);
  session.SetSetting("fullscreen", over.game_value, "true", ValueStyle::kBare);
  CHECK_TRUE(session.Dirty());
  const SaveOutcome saved = session.Save();
  CHECK_TRUE(saved.ok);
  CHECK_TRUE(saved.wrote);
  const std::string launcher_text = ReadText(profile_path);
  CHECK_CONTAINS(launcher_text, "fullscreen = false");
  CHECK_CONTAINS(launcher_text, "vsync = true");
  // The game's file is not the launcher's to write (D2), so it is byte-for-byte what it was.
  CHECK_STR_EQ(ReadText(install / kGameConfigFileName), game_text);
  CHECK_FALSE(session.OverrideFor("fullscreen", "false", "true").overridden);

  BeginCase("B4: the file the reader refuses to interpret badges nothing");

  WriteText(install / kGameConfigFileName, "fullscreen false\n");
  const RowOverride unreadable = session.OverrideFor("fullscreen", "true", "true");
  CHECK_FALSE(unreadable.overridden);
}

}  // namespace

int main() {
  Scratch scratch;
  TestRoundTrip(scratch);
  TestSaveWithNothingChanged(scratch);
  TestWindowGeometry(scratch);
  TestWindowGeometryUnwritable(scratch);
  TestFreshProfile(scratch);
  TestReset(scratch);
  TestImportExport(scratch);
  TestSettingsLocation(scratch);
  TestGameConfigReader(scratch);
  TestPrecedence(scratch);
  return Finish();
}
