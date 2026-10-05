// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for the launcher profile (src/launcher/profile.{h,cpp}) and its path
// resolution (src/launcher/profile_path.{h,cpp}). No SDK, no boot - see tests/check.h.
//
// What is pinned here is the pair of promises the module makes to the launcher and to
// the game (docs/plans/launcher-plan.md P0.3, D2):
//
//   * the file's location has one order - explicit path, environment, portable marker,
//     roaming app data - and an install folder is never a candidate without the marker;
//   * a save patches the keys it owns and leaves everything else alone, so a
//     load-then-save with no edits is byte-for-byte the same file and a key, a table or
//     a comment this version does not know survives;
//   * a file that does not parse is reported with a reason and left untouched.

#include "check.h"

#include "launcher/profile.h"
#include "launcher/profile_path.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <initializer_list>

namespace {

using namespace rb_blitz::launcher;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

// check.h compares integers, so strings get their own reporter that prints both sides.
void CheckString(const char* file, int line, const std::string& actual,
                 const std::string& expected, const char* actual_expr,
                 const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " == " + expected_expr +
                      " failed\n         actual   " + actual + "\n         expected " +
                      expected);
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

// A scratch tree with the three places the resolution distinguishes:
//
//   <temp>/rb_blitz-launcher-profile/install    the folder the executable lives in
//   <temp>/rb_blitz-launcher-profile/appdata    the roaming app data directory
//   <temp>/rb_blitz-launcher-profile/*.toml     explicit paths, as a user would give one
struct Scratch {
  fs::path base;
  fs::path install;
  fs::path appdata;

  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-launcher-profile";
    fs::remove_all(base, code);
    install = base / "install";
    appdata = base / "appdata";
    fs::create_directories(install, code);
    fs::create_directories(appdata, code);
  }

  ~Scratch() {
    std::error_code code;
    fs::remove_all(base, code);
  }
};

// Lines joined with '\n' and a trailing newline, written out one literal per line so a
// test never depends on the host's line endings.
std::string MakeText(std::initializer_list<const char*> lines) {
  std::string text;
  for (const char* line : lines) {
    text += line;
    text += '\n';
  }
  return text;
}

// A profile that already carries every structural key, two settings, an unknown table and
// two blocks of the same array table. The path is a basic string with escaped
// backslashes, which is how Contract 2 spells a folder.
std::string SampleProfile() {
  return MakeText({
      "# Rock Band Blitz launcher profile, kept by hand.",
      "schema_version = 1",
      "",
      "[launcher]",
      "version  = 1",
      "portable = false",
      "",
      "[window]",
      "width  = 1100",
      "height = 720",
      "",
      "[launch]",
      "target        = \"ultimate\"   # what the installer chose",
      "game_dir      = \"C:\\\\Games\\\\Rock Band Blitz\\\\game\"   # the wizard's folder",
      "user_data_dir = \"\"",
      "dlc_dir       = \"\"",
      "",
      "[settings]",
      "ultimate_mode    = 1",
      "video_mode_width = 1600",
      "",
      "# A table this version does not know.",
      "[future]",
      "thing = \"kept\"   # and its comment",
      "",
      "[[devices.\"030000005e0400008e02000000007801\".bindings]]",
      "from = \"PadA\"",
      "to   = \"PadA\"",
      "[[devices.\"030000005e0400008e02000000007801\".bindings]]",
      "from = \"PadB\"",
      "to   = \"PadB\"",
  });
}

void TestDefaultPath() {
  BeginCase("with no override, the profile is under the roaming app data directory");
  Scratch scratch;
  ProfilePathInputs inputs;
  inputs.executable_dir = scratch.install;
  inputs.app_data_dir = scratch.appdata;

  CHECK_FALSE(IsPortable(inputs.executable_dir));
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.appdata / "rb_blitz" / "launcher.toml");

  // The install folder is never reached without the marker, whatever else is set.
  inputs.executable_dir.clear();
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.appdata / "rb_blitz" / "launcher.toml");
  CHECK_TRUE(ResolveProfilePath(ProfilePathInputs{}) == fs::path());
}

void TestPortableOverride() {
  BeginCase("the portable marker moves the profile beside the executable");
  Scratch scratch;
  ProfilePathInputs inputs;
  inputs.executable_dir = scratch.install;
  inputs.app_data_dir = scratch.appdata;

  WriteFile(scratch.install / "rb_blitz_launcher.portable", "");
  CHECK_TRUE(IsPortable(inputs.executable_dir));
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.install / "launcher.toml");

  // Removing it puts the profile back under the app data directory.
  std::error_code code;
  fs::remove(scratch.install / "rb_blitz_launcher.portable", code);
  CHECK_FALSE(IsPortable(inputs.executable_dir));
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.appdata / "rb_blitz" / "launcher.toml");
}

void TestOverridePrecedence() {
  BeginCase("an explicit path wins over the environment, which wins over the marker");
  Scratch scratch;
  {
    std::error_code code;
    fs::create_directories(scratch.install, code);
  }
  WriteFile(scratch.install / "rb_blitz_launcher.portable", "");

  ProfilePathInputs inputs;
  inputs.executable_dir = scratch.install;
  inputs.app_data_dir = scratch.appdata;
  inputs.environment_value = (scratch.base / "env.toml").string();
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.base / "env.toml");

  inputs.command_line_value = (scratch.base / "cli.toml").string();
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.base / "cli.toml");
}

void TestEnvironmentInputs() {
  BeginCase("the environment helper reads the override and the app data directory");
  Scratch scratch;
  const std::string value = (scratch.base / "from-env.toml").string();
#ifdef _WIN32
  _putenv_s(kProfileEnvVar, value.c_str());
#else
  setenv(kProfileEnvVar, value.c_str(), 1);
#endif

  const ProfilePathInputs inputs = ProfilePathInputsFromEnvironment(scratch.install);
  CHECK_STR_EQ(inputs.environment_value, value);
  CHECK_TRUE(inputs.executable_dir == scratch.install);
  CHECK_TRUE(ResolveProfilePath(inputs) == scratch.base / "from-env.toml");

#ifdef _WIN32
  _putenv_s(kProfileEnvVar, "");
#else
  unsetenv(kProfileEnvVar);
#endif
}

void TestMissingFile() {
  BeginCase("a missing file is not an error and yields the defaults");
  Scratch scratch;
  const fs::path path = scratch.base / "never-written.toml";

  const ProfileLoadResult result = LoadProfile(path);
  CHECK_TRUE(result.status == ProfileStatus::kMissingFile);
  CHECK_TRUE(result.usable());
  CHECK_TRUE(result.error.empty());
  CHECK_EQ(result.profile.window_width, 1100);
  CHECK_EQ(result.profile.window_height, 640);
  CHECK_TRUE(result.profile.target == LaunchTarget::kUltimate);
  CHECK_TRUE(result.profile.settings.empty());
  CHECK_FALSE(fs::exists(path));
}

void TestDamagedFile() {
  BeginCase("a damaged file is reported with a reason and left byte for byte alone");
  Scratch scratch;

  struct Case {
    const char* name;
    std::string text;
    const char* reason;
  };
  const Case cases[] = {
      {"header.toml", MakeText({"schema_version = 1", "[window", "width = 1100"}), "table header"},
      {"string.toml", MakeText({"schema_version = 1", "[launch]", "target = \"common"}),
       "unterminated string"},
      {"duplicate.toml", MakeText({"schema_version = 1", "[window]", "width = 1", "width = 2"}),
       "duplicate key"},
      {"target.toml", MakeText({"schema_version = 1", "[launch]", "target = \"trial\""}),
       "common, demo or ultimate"},
      {"width.toml", MakeText({"schema_version = 1", "[window]", "width = \"wide\""}),
       "window.width is not an integer"},
      {"version.toml", MakeText({"schema_version = 2"}), "schema_version 2"},
      {"noequals.toml", MakeText({"schema_version = 1", "[window]", "width 1100"}),
       "expected `key = value`"},
  };

  for (const Case& test_case : cases) {
    const fs::path path = scratch.base / test_case.name;
    WriteFile(path, test_case.text);

    const ProfileLoadResult result = LoadProfile(path);
    CHECK_TRUE(result.status == ProfileStatus::kMalformed);
    CHECK_FALSE(result.usable());
    CHECK_FALSE(result.error.empty());
    CHECK_TRUE(result.error.find(test_case.reason) != std::string::npos);
    if (result.error.find(test_case.reason) == std::string::npos) {
      std::printf("         %s: %s\n", test_case.name, result.error.c_str());
    }
    // The file is neither repaired nor rewritten.
    CHECK_STR_EQ(ReadFile(path), test_case.text);
  }

  // An empty file is readable, not damaged: there is nothing to parse, so it is all
  // defaults, and a save gives it a body.
  const fs::path empty = scratch.base / "empty.toml";
  WriteFile(empty, "");
  const ProfileLoadResult result = LoadProfile(empty);
  CHECK_TRUE(result.status == ProfileStatus::kOk);
  CHECK_TRUE(result.usable());
}

void TestRoundTrip() {
  BeginCase("a load and save with no edits reproduces the file byte for byte");
  Scratch scratch;
  const fs::path path = scratch.base / "round-trip.toml";
  const std::string source = SampleProfile();
  WriteFile(path, source);

  ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  CHECK_TRUE(loaded.error.empty());
  if (loaded.status != ProfileStatus::kOk) {
    std::printf("         profile error: %s\n", loaded.error.c_str());
    std::fflush(stdout);
  }

  // The values the file spells are the values the model holds.
  CHECK_EQ(loaded.profile.window_width, 1100);
  CHECK_EQ(loaded.profile.launcher_version, 1);
  CHECK_TRUE(loaded.profile.target == LaunchTarget::kUltimate);
  CHECK_STR_EQ(loaded.profile.game_dir, "C:\\Games\\Rock Band Blitz\\game");
  CHECK_EQ(loaded.profile.settings.size(), 2);
  CHECK_STR_EQ(loaded.profile.settings[0].key, "ultimate_mode");
  CHECK_STR_EQ(loaded.profile.settings[0].value, "1");
  CHECK_TRUE(loaded.profile.settings[0].style == ValueStyle::kBare);
  CHECK_STR_EQ(loaded.profile.settings[1].key, "video_mode_width");
  CHECK_STR_EQ(loaded.profile.settings[1].value, "1600");

  std::string error;
  CHECK_TRUE(SaveProfile(path, loaded.profile, &error));
  CHECK_TRUE(error.empty());
  CHECK_STR_EQ(ReadFile(path), source);
}

void TestZeroSettings() {
  BeginCase("a profile with zero settings saves without inventing a [settings] table");
  Scratch scratch;
  const fs::path path = scratch.base / "no-settings.toml";
  const std::string source = MakeText({
      "schema_version = 1",
      "",
      "[launcher]",
      "version = 1",
      "portable = false",
      "",
      "[window]",
      "width = 1100",
      "height = 720",
      "",
      "[launch]",
      "target = \"ultimate\"",
      "game_dir = \"\"",
      "user_data_dir = \"\"",
      "dlc_dir = \"\"",
  });
  WriteFile(path, source);

  const ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  CHECK_TRUE(loaded.profile.settings.empty());

  std::string error;
  CHECK_TRUE(SaveProfile(path, loaded.profile, &error));
  CHECK_STR_EQ(ReadFile(path), source);
  CHECK_TRUE(ReadFile(path).find("[settings]") == std::string::npos);

  // A freshly rendered profile omits the table too, rather than writing an empty one.
  CHECK_TRUE(RenderProfile(Profile{}).find("[settings]") == std::string::npos);
}

void TestEdits() {
  BeginCase("a changed value keeps its line, and a new one joins the [settings] table");
  Scratch scratch;
  const fs::path path = scratch.base / "edits.toml";
  WriteFile(path, MakeText({
                       "schema_version = 1",
                       "[launcher]",
                       "version = 1",
                       "portable = false",
                       "[window]",
                       "width = 1100",
                       "[launch]",
                       "target = \"common\"   # the note to keep",
                       "[settings]",
                       "video_mode_width = 1280",
                       "[future]",
                       "thing = \"kept\"",
                   }));

  ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  Profile profile = loaded.profile;
  profile.window_width = 1600;
  profile.target = LaunchTarget::kUltimate;
  profile.Set("video_mode_width", "1920");   // changed: rewritten in place
  profile.SetString("save_note", "C:\\saves");  // added: appended to the table
  profile.Set("audio_mute", "false");        // added, bare
  profile.EraseSetting("dlc_root");          // not present: a no-op

  std::string error;
  CHECK_TRUE(SaveProfile(path, profile, &error));
  const std::string text = ReadFile(path);

  CHECK_TRUE(text.find("width = 1600") != std::string::npos);
  // The value changed; the comment and the spacing around it did not.
  CHECK_TRUE(text.find("target = \"ultimate\"   # the note to keep") != std::string::npos);
  CHECK_TRUE(text.find("video_mode_width = 1920") != std::string::npos);
  // The new rows sit in [settings], between its last row and the next table.
  CHECK_TRUE(text.find("video_mode_width = 1920\nsave_note = \"C:\\\\saves\"\n"
                       "audio_mute = false\n[future]") != std::string::npos);
  CHECK_TRUE(text.find("[settings]\nvideo_mode_width") != std::string::npos);
  // The table this version does not know was not touched.
  CHECK_TRUE(text.find("thing = \"kept\"") != std::string::npos);

  // And the result reads back.
  const ProfileLoadResult again = LoadProfile(path);
  CHECK_TRUE(again.status == ProfileStatus::kOk);
  CHECK_EQ(again.profile.window_width, 1600);
  CHECK_TRUE(again.profile.target == LaunchTarget::kUltimate);
  CHECK_EQ(again.profile.settings.size(), 3);
  CHECK_STR_EQ(again.profile.settings[0].key, "video_mode_width");
  CHECK_STR_EQ(again.profile.settings[0].value, "1920");
  CHECK_STR_EQ(again.profile.settings[1].key, "save_note");
  CHECK_STR_EQ(again.profile.settings[1].value, "C:\\saves");
  CHECK_TRUE(again.profile.settings[1].style == ValueStyle::kBasic);
  CHECK_STR_EQ(again.profile.settings[2].key, "audio_mute");
}

void TestMissingSectionAppendedLast() {
  BeginCase("a table the file is missing is added after the rows of the table above it");
  Scratch scratch;
  const fs::path path = scratch.base / "append.toml";
  WriteFile(path, MakeText({
                       "schema_version = 1",
                       "[settings]",
                       "video_mode_width = 1280",
                   }));

  const ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  Profile profile = loaded.profile;
  profile.SetString("save_note", "note");

  std::string error;
  CHECK_TRUE(SaveProfile(path, profile, &error));
  const std::string text = ReadFile(path);

  // The new row stays in [settings] - which is the last table in the file - and the
  // [launcher] that has to be created follows it instead of swallowing it.
  CHECK_TRUE(text.find("video_mode_width = 1280\nsave_note = \"note\"\n[launcher]") !=
             std::string::npos);

  const ProfileLoadResult again = LoadProfile(path);
  CHECK_TRUE(again.status == ProfileStatus::kOk);
  CHECK_EQ(again.profile.launcher_version, 1);
  CHECK_EQ(again.profile.settings.size(), 2);
  CHECK_STR_EQ(again.profile.settings[0].key, "video_mode_width");
  CHECK_STR_EQ(again.profile.settings[1].key, "save_note");
  CHECK_STR_EQ(again.profile.settings[1].value, "note");
}

void TestErase() {
  BeginCase("erasing a setting drops its line and leaves the rest of the file alone");
  Scratch scratch;
  const fs::path path = scratch.base / "erase.toml";
  const std::string source = SampleProfile();
  WriteFile(path, source);

  ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  Profile profile = loaded.profile;
  profile.EraseSetting("video_mode_width");

  std::string error;
  CHECK_TRUE(SaveProfile(path, profile, &error));
  const std::string text = ReadFile(path);

  CHECK_TRUE(text.find("video_mode_width") == std::string::npos);
  CHECK_TRUE(text.find("ultimate_mode    = 1") != std::string::npos);
  // The unknown table, the comments and the array tables were not touched.
  CHECK_TRUE(text.find("[future]") != std::string::npos);
  CHECK_TRUE(text.find("thing = \"kept\"   # and its comment") != std::string::npos);
  CHECK_TRUE(text.find("to   = \"PadB\"") != std::string::npos);

  const ProfileLoadResult again = LoadProfile(path);
  CHECK_TRUE(again.status == ProfileStatus::kOk);
  CHECK_EQ(again.profile.settings.size(), 1);
  CHECK_STR_EQ(again.profile.settings[0].key, "ultimate_mode");
}

void TestRenderProfile() {
  BeginCase("a fresh profile renders every structural key");
  const Profile profile;
  const std::string text = RenderProfile(profile);
  CHECK_TRUE(text.find("schema_version = 1\n") == 0);
  CHECK_TRUE(text.find("[launcher]\nversion = 1\nportable = false") != std::string::npos);
  CHECK_TRUE(text.find("[window]\nwidth = 1100\nheight = 640") != std::string::npos);
  CHECK_TRUE(text.find("[launch]\ntarget = \"ultimate\"") != std::string::npos);

  // A fresh file is loadable, and its values match what was rendered.
  Scratch scratch;
  const fs::path path = scratch.base / "fresh.toml";
  std::string error;
  CHECK_TRUE(SaveProfile(path, profile, &error));
  const ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  CHECK_TRUE(loaded.profile.target == LaunchTarget::kUltimate);
  CHECK_TRUE(loaded.profile.settings.empty());
}

void TestQuoting() {
  BeginCase("a path survives the round trip through a basic string");
  CHECK_STR_EQ(QuoteTomlString("C:\\saves"), "\"C:\\\\saves\"");
  CHECK_STR_EQ(UnquoteTomlString("C:\\\\saves\\n"), "C:\\saves\n");
  // An escape this module never writes is kept rather than guessed at.
  CHECK_STR_EQ(UnquoteTomlString("\\u0041"), "\\u0041");
  CHECK_TRUE(ParseLaunchTarget("demo") == LaunchTarget::kDemo);
  CHECK_FALSE(ParseLaunchTarget("trial").has_value());
}

}  // namespace

int main() {
  TestDefaultPath();
  TestPortableOverride();
  TestOverridePrecedence();
  TestEnvironmentInputs();
  TestMissingFile();
  TestDamagedFile();
  TestRoundTrip();
  TestZeroSettings();
  TestEdits();
  TestMissingSectionAppendedLast();
  TestErase();
  TestRenderProfile();
  TestQuoting();
  return Finish();
}
