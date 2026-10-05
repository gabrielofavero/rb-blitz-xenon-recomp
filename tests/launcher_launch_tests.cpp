// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the launch contract (docs/plans/launcher-plan.md §4.3, Contract 3; prompt
// B7): the argv the launcher hands the game, and the two rules that make it safe to print -
// a row is passed only when it differs from the compiled default, and a value that is text is
// quoted.
//
// No ImGui, no SDL, no game data, no boot, and no process: BuildLaunchCommand is pure string
// work, which is what makes `--print-command` the same code path as a real start. GameProcess
// is therefore not exercised here - it is the part that needs an executable to start, and
// scripts/ or a manual run is where it is checked.

#include "check.h"

#include "game_launch.h"
#include "launcher/profile.h"
#include "launcher/profile_path.h"
#include "profile_session.h"
#include "ultimate_state.h"

#include <filesystem>
#include <fstream>
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

struct Scratch {
  fs::path base;
  fs::path install;
  fs::path app_data;
  fs::path game_root;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-launcher-launch";
    fs::remove_all(base, ec);
    install = base / "install with a space";
    app_data = base / "app data";
    game_root = base / "game";
    fs::create_directories(install, ec);
    fs::create_directories(app_data, ec);
    // A real game root: the entry point and the archive folder the detector accepts.
    WriteText(game_root / "default.xex", "the user's own executable");
    // The game's own executable, which has to be beside the launcher for a start to be possible.
    WriteText(install / kGameExecutableName, "not really the game");
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }

  ProfileSession Session() const {
    ProfilePathInputs inputs;
    inputs.executable_dir = install;
    inputs.app_data_dir = app_data;
    return ProfileSession(std::move(inputs), ProfileLoadResult{});
  }

  GameRoots Roots() const {
    GameRoots roots;
    roots.game_root = game_root;
    roots.ultimate_root = game_root / "ultimate";
    roots.game_root_found = true;
    return roots;
  }
};

void TestCommandShape(const Scratch& scratch) {
  BeginCase("Contract 3: the roots, the target and the profile path are always there");

  ProfileSession session = scratch.Session();
  const LaunchCommand command =
      BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kUltimate);

  CHECK_TRUE(command.ok);
  CHECK_STR_EQ(command.executable.filename().string(), kGameExecutableName);
  CHECK_STR_EQ(command.working_directory.string(), scratch.install.string());
  CHECK_CONTAINS(command.arguments, "--game_data_root=\"" + scratch.game_root.string() + "\"");
  // Ultimate is auto mode; common is off, and demo is off *plus* no licence.
  CHECK_CONTAINS(command.arguments, "--ultimate_mode=1");
  CHECK_NOT_CONTAINS(command.arguments, "--license_mask");
  CHECK_CONTAINS(command.arguments, "--launcher_profile=\"" + session.path().string() + "\"");
  // Nothing the user never touched is passed: the compiled defaults decide, and the game's own
  // file outranks the launcher for those rows (D3).
  CHECK_NOT_CONTAINS(command.arguments, "--resolution");
  CHECK_NOT_CONTAINS(command.arguments, "--fullscreen");
  CHECK_NOT_CONTAINS(command.arguments, "--user_data_root");
  CHECK_NOT_CONTAINS(command.arguments, "--dlc_root");
  CHECK_NOT_CONTAINS(command.arguments, "--gpu_backend");

  BeginCase("the demo target is the trial, and the retail target turns Ultimate off");

  const LaunchCommand demo = BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kDemo);
  CHECK_CONTAINS(demo.arguments, "--ultimate_mode=0");
  CHECK_CONTAINS(demo.arguments, "--license_mask=0");

  const LaunchCommand common = BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kCommon);
  CHECK_CONTAINS(common.arguments, "--ultimate_mode=0");
  CHECK_NOT_CONTAINS(common.arguments, "--license_mask");
}

void TestOnlyDifferencesArePassed(const Scratch& scratch) {
  BeginCase("a row is passed only where the profile moved it off the compiled default");

  ProfileSession session = scratch.Session();
  // One of each interesting shape: a bool whose default is true, an enum whose default is
  // "none", an int with a default, and a path.
  session.SetSetting("fullscreen", "false", "true", ValueStyle::kBare);
  session.SetSetting("swap_post_effect", "fxaa", "none", ValueStyle::kBare);
  session.SetSetting("resolution_scale", "2", "1", ValueStyle::kBare);
  session.SetSetting("user_data_root", "D:\\saves here", "", ValueStyle::kBasic);
  // And one written back to its default, which must not appear at all.
  session.SetSetting("vsync", "true", "true", ValueStyle::kBare);

  const LaunchCommand command =
      BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kUltimate);
  CHECK_TRUE(command.ok);
  CHECK_CONTAINS(command.arguments, "--fullscreen=false");
  CHECK_CONTAINS(command.arguments, "--swap_post_effect=fxaa");
  CHECK_CONTAINS(command.arguments, "--resolution_scale=2");
  CHECK_NOT_CONTAINS(command.arguments, "--vsync");

  BeginCase("a path with a space in it is one quoted argument");

  CHECK_CONTAINS(command.arguments, "--user_data_root=\"D:\\saves here\"");

  BeginCase("a launcher-level row is never passed as a flag");

  session.SetSetting("launch.target", "demo", "ultimate", ValueStyle::kBare);
  const LaunchCommand again =
      BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kUltimate);
  CHECK_NOT_CONTAINS(again.arguments, "--launch.target");
}

void TestRefusals(const Scratch& scratch) {
  BeginCase("no game data root: the command is printed, and says why it cannot start");

  ProfileSession session = scratch.Session();
  GameRoots roots = scratch.Roots();
  roots.game_root_found = false;
  const LaunchCommand missing_data = BuildLaunchCommand(session, roots, LaunchTarget::kCommon);
  CHECK_FALSE(missing_data.ok);
  CHECK_CONTAINS(missing_data.error, "no game was found");
  // The command line is still built, because that is the first thing a bug report needs.
  CHECK_CONTAINS(missing_data.arguments, "--game_data_root=");

  BeginCase("no game executable next to the launcher");

  std::error_code ec;
  fs::remove(scratch.install / kGameExecutableName, ec);
  const LaunchCommand missing_exe =
      BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kCommon);
  CHECK_FALSE(missing_exe.ok);
  CHECK_CONTAINS(missing_exe.error, kGameExecutableName);
  WriteText(scratch.install / kGameExecutableName, "not really the game");

  BeginCase("the whole command line begins with the executable, quoted");

  const LaunchCommand command =
      BuildLaunchCommand(session, scratch.Roots(), LaunchTarget::kCommon);
  const std::string line = FormatLaunchCommand(command);
  CHECK_STR_EQ(line.substr(0, 1), "\"");
  // Which matters because CreateProcessW takes this one string: without argv[0] the game would
  // read --game_data_root as its first argument. So the executable is the first token, quoted,
  // and the flags follow it.
  CHECK_CONTAINS(line, "\"" + command.executable.string() + "\" --game_data_root=");
}

}  // namespace

int main() {
  const Scratch scratch;
  TestCommandShape(scratch);
  TestOnlyDifferencesArePassed(scratch);
  TestRefusals(scratch);
  return Finish();
}
