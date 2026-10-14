// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 host tests for the path file and the legacy migration (src/settings/paths.{h,cpp},
// src/settings/migration.{h,cpp}). No SDK, no boot.
//
// The parent plan §4.3 rules pinned here: a byte-identical backup before the first write;
// import only missing fields, and only the ones whose meaning did not change; translate
// resolution_scale, AA and filtering exactly; keep retired values out; write the path file
// once; and leave a migration interrupted between the two files to be finished, not restarted.

#include "check.h"

#include "profile/profile.h"
#include "profile/profile_path.h"
#include "settings/migration.h"
#include "settings/paths.h"
#include "settings/store.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace rb_blitz::settings;
using namespace rb_blitz::test;

namespace fs = std::filesystem;
using rb_blitz::profile::LaunchTarget;
using rb_blitz::profile::Profile;
using rb_blitz::profile::ProfilePathInputs;

void CheckString(const char* file, int line, const std::string& actual, const std::string& expected,
                 const char* actual_expr, const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " == " + expected_expr +
                      " failed\n         actual   " + actual + "\n         expected " + expected);
}

#define CHECK_STR_EQ(actual, expected) \
  CheckString(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

std::string ReadFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void WriteFile(const fs::path& path, const std::string& text) {
  std::error_code code;
  fs::create_directories(path.parent_path(), code);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct Scratch {
  fs::path base;
  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-settings-migration";
    fs::remove_all(base, code);
    fs::create_directories(base, code);
  }
  ~Scratch() {
    std::error_code code;
    fs::remove_all(base, code);
  }
};

// A Controller Settings profile in the shape the current Controller Settings writes: every [settings] row present.
Profile LegacyProfile() {
  Profile profile;
  profile.target = LaunchTarget::kUltimate;
  profile.user_data_dir = "";
  profile.dlc_dir = "";
  profile.Set("enhancements_dlc_cache", "false");
  profile.Set("dlc_scan_threads", "1");
  profile.Set("enhancements_hide_menu_options", "false");
  profile.Set("enhancements_hidden_menu_options",
              "splash_leaderboard,splash_achievements,splash_dlc");
  profile.Set("enhancements_menu_dlc_songs", "false");
  profile.Set("enhancements_skip_offline_dialog", "true");
  profile.Set("resolution_scale", "1");
  profile.Set("fullscreen", "true");
  profile.Set("monitor", "0");
  profile.Set("vsync", "true");
  profile.Set("gpu_backend", "d3d12");
  profile.Set("swap_post_effect", "none");
  profile.Set("native_2x_msaa", "true");
  profile.Set("anisotropic_override", "3");
  profile.Set("enhancements_rename_mod_settings", "true");
  profile.Set("enhancements_menu_dlc_song_count", "3");
  profile.Set("input_backend", "sdl");
  profile.source_text = "schema_version = 1\n";
  return profile;
}

void TestPathFile() {
  Scratch scratch;
  const fs::path file = scratch.base / "install" / "paths.toml";

  BeginCase("paths: a missing file yields the defaults and is not an error");
  PathFileResult missing = LoadPathFile(file);
  CHECK_TRUE(missing.status == PathFileResult::Status::kMissing);

  BeginCase("paths: literal strings round trip and unknown keys survive");
  WriteFile(file, "schema_version = 1\n\n[paths]\n# a comment\nunknown = 'keep me'\n");
  PathFileResult loaded = LoadPathFile(file);
  CHECK_TRUE(loaded.status == PathFileResult::Status::kOk);
  CHECK_STR_EQ(loaded.paths.dlc_root, "");
  PathEntry entry;
  entry.dlc_root = "D:\\Rock Band DLC";
  entry.user_data_root = "D:\\Rock Band Saves";
  std::string error;
  CHECK_TRUE(SavePathFile(file, entry, &error));
  const std::string text = ReadFile(file);
  CHECK_TRUE(text.find("dlc_root = 'D:\\Rock Band DLC'") != std::string::npos);
  CHECK_TRUE(text.find("unknown = 'keep me'") != std::string::npos);
  const PathFileResult again = LoadPathFile(file);
  CHECK_STR_EQ(again.paths.user_data_root, "D:\\Rock Band Saves");

  BeginCase("paths: a malformed file is reported and left intact");
  const fs::path bad = scratch.base / "bad-paths.toml";
  WriteFile(bad, "schema_version = 9\n");
  const PathFileResult unsupported = LoadPathFile(bad);
  CHECK_TRUE(unsupported.status == PathFileResult::Status::kMalformed);
  CHECK_STR_EQ(ReadFile(bad), "schema_version = 9\n");

  BeginCase("paths: empty resets the key, a relative value resolves beside the file");
  PathEntry cleared;
  CHECK_TRUE(SavePathFile(file, cleared, &error));
  CHECK_TRUE(ReadFile(file).find("dlc_root") == std::string::npos);
  CHECK_STR_EQ(ResolvePathValue("saves", file, fs::path("fallback")).string(),
               (file.parent_path() / "saves").string());
  CHECK_STR_EQ(ResolvePathValue("", file, fs::path("fallback")).string(), "fallback");

  BeginCase("paths: the settings sibling mapping (§4.1)");
  CHECK_STR_EQ(SettingsFileForProfile(scratch.base / "rb_blitz" / "launcher.toml").filename().string(),
               "settings.toml");
  CHECK_STR_EQ(
      SettingsFileForProfile(scratch.base / "rb_blitz" / "custom.toml").filename().string(),
      "custom.game-settings.toml");
  CHECK_TRUE(SettingsFileForProfile(fs::path()).empty());

  BeginCase("paths: the settings folder follows the existing profile locator");
  const fs::path install = scratch.base / "install2";
  const fs::path appdata = scratch.base / "appdata2";
  fs::create_directories(install);
  fs::create_directories(appdata);
  ProfilePathInputs inputs;
  inputs.executable_dir = install;
  inputs.app_data_dir = appdata;
  CHECK_STR_EQ(ResolveSettingsFile(inputs).string(),
               (appdata / "rb_blitz" / "settings.toml").string());
  // A portable marker moves both the profile and its settings sibling beside the executable.
  WriteFile(install / "rb_blitz_controller_settings.portable", "");
  CHECK_STR_EQ(ResolveSettingsFile(inputs).string(), (install / "settings.toml").string());
}

void TestMigrateDefault() {
  Scratch scratch;
  StorePaths paths;
  paths.settings_file = scratch.base / "rb_blitz" / "settings.toml";
  paths.paths_file = scratch.base / "install" / "paths.toml";
  paths.legacy_profile = scratch.base / "rb_blitz" / "launcher.toml";
  WriteFile(paths.legacy_profile, "schema_version = 1\n# the player's file\n");
  Profile legacy = LegacyProfile();
  legacy.source_text = ReadFile(paths.legacy_profile);

  BeginCase("migrate: a default profile imports the target and nothing that is already default");
  Store store(paths);
  store.Load();
  MigrationResult result = Migrate(legacy, store, PathEntry{});
  CHECK_TRUE(result.status == MigrationStatus::kMigrated);
  CHECK_TRUE(result.wrote_settings);
  CHECK_TRUE(result.stats.target_imported);
  // Keep the old target, 4x filtering, explicitly stored 720p resolution and the profile's
  // *Cache DLC Library* choice. That last one is the row whose compiled default became `true` when
  // the in-game row took the setting over from Controller Settings flag (P11b), so a profile that stores
  // `false` differs from the default and is imported like any other choice - which is the point:
  // the player's choice survives the migration.
  CHECK_EQ(result.stats.imported, 4);
  CHECK_EQ(result.stats.translated, 2);
  CHECK_EQ(result.stats.dropped, 4);      // rename + pool count + fullscreen + monitor
  CHECK_EQ(result.stats.preserved, 1);    // input_backend
  CHECK_TRUE(!result.backup.empty());
  CHECK_STR_EQ(ReadFile(result.backup), ReadFile(paths.legacy_profile));
  const DocumentLoad loaded = LoadSettingsDocument(paths.settings_file);
  CHECK_TRUE(loaded.document.values.at(SettingId::kTarget).text == "ultimate");
  CHECK_TRUE(loaded.document.origins.at(SettingId::kTarget) == Origin::kManual);
  CHECK_TRUE(loaded.document.migration_imported);
  // Everything already at its compiled default is deliberately not written.
  CHECK_TRUE(loaded.document.values.count(SettingId::kVsync) == 0);

  BeginCase("migrate: a second run is idempotent");
  Store second(paths);
  second.Load();
  const MigrationResult again = Migrate(legacy, second, PathEntry{});
  CHECK_TRUE(again.status == MigrationStatus::kAlreadyCurrent);
  CHECK_FALSE(again.wrote_settings);
  CHECK_FALSE(again.wrote_paths);
}

void TestMigrateTranslations() {
  Scratch scratch;
  StorePaths paths;
  paths.settings_file = scratch.base / "settings.toml";
  paths.paths_file = scratch.base / "paths.toml";
  paths.legacy_profile = scratch.base / "launcher.toml";

  Profile legacy = LegacyProfile();
  legacy.source_text = "x";
  legacy.Set("resolution_scale", "2");      // 2560x1440, never 1920x1080
  legacy.Set("anisotropic_override", "5");  // 16x, never the value 16
  legacy.Set("swap_post_effect", "fxaa");
  legacy.Set("native_2x_msaa", "true");     // simultaneous native+FXAA: intentional loss
  legacy.Set("enhancements_hide_menu_options", "true");
  legacy.Set("enhancements_hidden_menu_options", "splash_start,splash_exit");
  legacy.Set("enhancements_skip_offline_dialog", "false");
  legacy.Set("dlc_scan_threads", "8");
  legacy.Set("user_data_root", "D:\\Saves");

  BeginCase("migrate: resolution, filtering and AA translate exactly");
  Store store(paths);
  store.Load();
  const MigrationResult result = Migrate(legacy, store, PathEntry{});
  CHECK_TRUE(result.status == MigrationStatus::kMigrated);
  const DocumentLoad loaded = LoadSettingsDocument(paths.settings_file);
  CHECK_EQ(loaded.document.values.at(SettingId::kResolution).width, 2560);
  CHECK_EQ(loaded.document.values.at(SettingId::kResolution).height, 1440);
  CHECK_STR_EQ(loaded.document.values.at(SettingId::kFiltering).text, "16x");
  CHECK_STR_EQ(loaded.document.values.at(SettingId::kAntiAliasing).text, "fxaa");
  CHECK_TRUE(loaded.document.values.at(SettingId::kDlcScanThreads).integer == 8);
  CHECK_TRUE(loaded.document.values.at(SettingId::kHideMainMenu).boolean == true);
  CHECK_TRUE(loaded.document.values.at(SettingId::kHideOfflinePrompts).boolean == false);
  CHECK_EQ(loaded.document.values.at(SettingId::kHiddenMainMenu).list.size(), 2);
  CHECK_STR_EQ(loaded.document.values.at(SettingId::kHiddenMainMenu).list[0], "splash_start");
  bool loss_recorded = false;
  for (const std::string& line : result.diagnostics) {
    if (line.find("native") != std::string::npos && line.find("FXAA") != std::string::npos) {
      loss_recorded = true;
    }
  }
  CHECK_TRUE(loss_recorded);

  BeginCase("migrate: the legacy path fields reach paths.toml");
  const PathFileResult written = LoadPathFile(paths.paths_file);
  CHECK_TRUE(written.status == PathFileResult::Status::kOk);
  CHECK_STR_EQ(written.paths.user_data_root, "D:\\Saves");
  CHECK_TRUE(result.stats.paths_imported && result.wrote_paths);
}

void TestMigrateInterruptionAndIdentity() {
  Scratch scratch;
  StorePaths paths;
  paths.settings_file = scratch.base / "settings.toml";
  paths.paths_file = scratch.base / "install" / "paths.toml";
  paths.legacy_profile = scratch.base / "launcher.toml";

  Profile legacy = LegacyProfile();
  legacy.source_text = "x";
  legacy.Set("user_data_root", "D:\\Saves");

  BeginCase("migrate: settings imported but paths missing resumes without erasing");
  // Simulate the first run having written settings.toml (with the marker) and then failing
  // before paths.toml. The next run must finish the paths, not declare a false first boot.
  WriteFile(paths.settings_file,
            "schema_version = 1\n\n[general]\ntarget = 'ultimate'\n\n[migration]\nimported = true\n");
  Store store(paths);
  store.Load();
  Profile minimal;
  minimal.target = LaunchTarget::kUltimate;
  minimal.Set("user_data_root", "D:\\Saves");
  minimal.source_text = "x";
  const MigrationResult resumed = Migrate(minimal, store, PathEntry{});
  CHECK_TRUE(resumed.status == MigrationStatus::kInterruptedResumed);
  CHECK_TRUE(resumed.wrote_paths);
  CHECK_FALSE(resumed.wrote_settings);
  CHECK_TRUE(resumed.stats.imported == 0);
  CHECK_STR_EQ(LoadPathFile(paths.paths_file).paths.user_data_root, "D:\\Saves");
  // The existing target is not overwritten.
  CHECK_STR_EQ(LoadSettingsDocument(paths.settings_file).document.values.at(SettingId::kTarget).text,
               "ultimate");

  BeginCase("migrate: a profile from another installation does not set the paths");
  StorePaths other;
  other.settings_file = scratch.base / "other" / "settings.toml";
  other.paths_file = scratch.base / "other-install" / "paths.toml";
  other.legacy_profile = scratch.base / "other" / "launcher.toml";
  Store other_store(other);
  other_store.Load();
  MigrationOptions options;
  options.apply_paths = false;
  const MigrationResult applied =
      Migrate(legacy, other_store, PathEntry{}, options);
  CHECK_TRUE(applied.status == MigrationStatus::kMigrated);
  CHECK_FALSE(applied.wrote_paths);
  CHECK_FALSE(fs::exists(other.paths_file));

  BeginCase("migrate: an unreadable legacy profile is reported and writes nothing");
  StorePaths broken;
  broken.settings_file = scratch.base / "broken" / "settings.toml";
  broken.paths_file = scratch.base / "broken" / "paths.toml";
  broken.legacy_profile = scratch.base / "broken" / "launcher.toml";
  Store broken_store(broken);
  broken_store.Load();
  Profile unusable = LegacyProfile();
  unusable.source_text = "not toml at all\n";
  const MigrationResult malformed = Migrate(unusable, broken_store, PathEntry{}, MigrationOptions{true, false});
  CHECK_TRUE(malformed.status == MigrationStatus::kMalformedLegacy);
  CHECK_FALSE(fs::exists(broken.settings_file));
  CHECK_FALSE(fs::exists(broken.paths_file));
}

void TestMigrateEmptyCsvStaysAbsent() {
  Scratch scratch;
  StorePaths paths;
  paths.settings_file = scratch.base / "settings.toml";
  paths.paths_file = scratch.base / "paths.toml";
  paths.legacy_profile = scratch.base / "launcher.toml";
  Profile legacy = LegacyProfile();
  legacy.source_text = "x";
  legacy.Set("enhancements_hidden_menu_options", "");  // old empty meant the default trio
  Store store(paths);
  store.Load();
  BeginCase("migrate: the old empty CSV becomes an absent selection, not an empty one");
  Migrate(legacy, store, PathEntry{});
  const DocumentLoad loaded = LoadSettingsDocument(paths.settings_file);
  CHECK_TRUE(loaded.document.values.count(SettingId::kHiddenMainMenu) == 0);
}

}  // namespace

int main() {
  TestPathFile();
  TestMigrateDefault();
  TestMigrateTranslations();
  TestMigrateInterruptionAndIdentity();
  TestMigrateEmptyCsvStaysAbsent();
  return Finish();
}
