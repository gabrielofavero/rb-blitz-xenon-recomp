// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the General tab's decidable half (docs/plans/launcher-plan.md D4, D5; prompt
// B1): D5's four Ultimate states from directory fixtures, the launch-target fallback, where
// the game is detected, and the schema's `validate` rules.
//
// No ImGui, no SDL, no game data, no boot - the tab is the only part of B1 that needs a
// window, and everything it decides is here.

#include "check.h"

#include "launcher/profile.h"
#include "path_validate.h"
#include "ultimate_state.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

using namespace rb_blitz::launcher;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

void CheckStrEq(const char* file, int line, std::string_view actual, std::string_view expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  ++g_checks;
  Fail(file, line, "expected \"" + std::string(expected) + "\", got \"" + std::string(actual) +
                       "\"");
}

void CheckContains(const char* file, int line, std::string_view haystack,
                   std::string_view needle) {
  if (haystack.find(needle) != std::string_view::npos) {
    ++g_checks;
    return;
  }
  ++g_checks;
  Fail(file, line, "expected to find \"" + std::string(needle) + "\" in \"" +
                       std::string(haystack) + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq(__FILE__, __LINE__, actual, expected)
#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, haystack, needle)

void WriteText(const fs::path& path, std::string_view text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

// D5's payload pair, in either location: gen\patch_xbox.hdr and gen\patch_xbox_0.ark, with the
// archive optional so "damaged" can be built on purpose.
void WritePayload(const fs::path& root, bool header, bool archive) {
  if (header) {
    WriteText(root / "gen" / "patch_xbox.hdr", "header");
  }
  if (archive) {
    WriteText(root / "gen" / "patch_xbox_0.ark", "archive");
  }
}

struct Scratch {
  fs::path base;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-launcher-general";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }

  fs::path Make(std::string_view name) const {
    const fs::path dir = base / name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
  }
};

void TestUltimateStates(Scratch& scratch) {
  BeginCase("D5: a payload pair under ultimate\\ is ready");

  const fs::path ready = scratch.Make("ready");
  WritePayload(ready / "ultimate", true, true);
  CHECK_TRUE(DetectUltimateState(ready) == UltimateState::kReady);
  CHECK_TRUE(UltimateAvailable(UltimateState::kReady));
  CHECK_TRUE(UltimateSelectable(UltimateState::kReady));
  CHECK_TRUE(UltimateStateText(UltimateState::kReady).empty());

  BeginCase("D5: the same pair at the game root is 'also present', and it is merged");

  const fs::path merged = scratch.Make("merged");
  WritePayload(merged, true, true);
  CHECK_TRUE(DetectUltimateState(merged) == UltimateState::kAlsoPresent);
  CHECK_TRUE(UltimateAvailable(UltimateState::kAlsoPresent));
  CHECK_CONTAINS(UltimateStateText(UltimateState::kAlsoPresent), "merged");

  BeginCase("D5: neither location holding the pair is 'missing'");

  const fs::path empty = scratch.Make("empty");
  CHECK_TRUE(DetectUltimateState(empty) == UltimateState::kMissing);
  CHECK_FALSE(UltimateAvailable(UltimateState::kMissing));
  // R11 as literally read wanted a disabled row; D5 decided against one, so the state must
  // still be selectable whatever the files say.
  CHECK_TRUE(UltimateSelectable(UltimateState::kMissing));
  CHECK_CONTAINS(UltimateStateText(UltimateState::kMissing), "not installed");

  BeginCase("D5: a header without its archive is the damaged pair");

  const fs::path damaged = scratch.Make("damaged");
  WritePayload(damaged / "ultimate", true, false);
  CHECK_TRUE(DetectUltimateState(damaged) == UltimateState::kDamaged);
  CHECK_FALSE(UltimateAvailable(UltimateState::kDamaged));
  CHECK_TRUE(UltimateSelectable(UltimateState::kDamaged));
  CHECK_CONTAINS(UltimateStateText(UltimateState::kDamaged), "patch_xbox_0.ark");

  BeginCase("D5: an archive without a header is not a payload either");

  const fs::path archive_only = scratch.Make("archive-only");
  WritePayload(archive_only / "ultimate", false, true);
  CHECK_TRUE(DetectUltimateState(archive_only) == UltimateState::kMissing);

  BeginCase("D5: a usable ultimate\\ pair wins over a merged copy");

  const fs::path both = scratch.Make("both");
  WritePayload(both / "ultimate", true, true);
  WritePayload(both, true, true);
  CHECK_TRUE(DetectUltimateState(both) == UltimateState::kReady);

  BeginCase("D5: a merged root with a broken ultimate\\ folder is damaged, not ready");

  const fs::path half = scratch.Make("half");
  WritePayload(half, true, true);
  WritePayload(half / "ultimate", true, false);
  // The payload folder is what the runtime mounts, and it cannot mount this one; the merged
  // copy is still there, so the state has to pick the worse of the two rather than the nicer.
  CHECK_TRUE(DetectUltimateState(half) == UltimateState::kDamaged);
}

void TestTargetFallback() {
  BeginCase("D5: Ultimate is only the target while it can be mounted");

  CHECK_TRUE(FallbackTarget(LaunchTarget::kUltimate, UltimateState::kReady) ==
             LaunchTarget::kUltimate);
  CHECK_TRUE(FallbackTarget(LaunchTarget::kUltimate, UltimateState::kAlsoPresent) ==
             LaunchTarget::kUltimate);
  CHECK_TRUE(FallbackTarget(LaunchTarget::kUltimate, UltimateState::kMissing) ==
             LaunchTarget::kCommon);
  CHECK_TRUE(FallbackTarget(LaunchTarget::kUltimate, UltimateState::kDamaged) ==
             LaunchTarget::kCommon);

  BeginCase("an explicit choice that is not Ultimate is never second-guessed");

  CHECK_TRUE(FallbackTarget(LaunchTarget::kCommon, UltimateState::kReady) ==
             LaunchTarget::kCommon);
  CHECK_TRUE(FallbackTarget(LaunchTarget::kDemo, UltimateState::kMissing) == LaunchTarget::kDemo);
}

void TestGameRootDetection(Scratch& scratch) {
  BeginCase("the game is found in the installer's layout, next to the launcher");

  const fs::path install = scratch.Make("install");
  WriteText(install / "game" / "default.xex", "the user's own executable");
  const GameRoots beside = DetectGameRoots(install, {});
  CHECK_TRUE(beside.game_root_found);
  CHECK_STR_EQ(beside.game_root.filename().string(), "game");
  CHECK_STR_EQ(beside.ultimate_root.filename().string(), "ultimate");

  BeginCase("a dump that sits in the launcher's own folder is found too");

  const fs::path dump = scratch.Make("dump");
  WriteText(dump / "gen" / "main_xbox_0.ark", "archive");
  const GameRoots here = DetectGameRoots(dump, {});
  CHECK_TRUE(here.game_root_found);
  CHECK_STR_EQ(here.game_root.string(), dump.string());

  BeginCase("a build tree is found by walking up from the launcher");

  // `out\build\<preset>` is three levels below the checkout, which is where the `game` folder
  // really is during development: this is the case the launcher was reporting as "install
  // Ultimate" before the search went up the tree.
  const fs::path checkout = scratch.Make("checkout");
  WriteText(checkout / "game" / "default.xex", "the user's own executable");
  const fs::path preset = checkout / "out" / "build" / "win-amd64-release";
  std::error_code made;
  fs::create_directories(preset, made);
  const GameRoots above = DetectGameRoots(preset, {});
  CHECK_TRUE(above.game_root_found);
  CHECK_STR_EQ(above.game_root.string(), (checkout / "game").string());

  BeginCase("the nearest game wins: beside the launcher, then up the tree");

  const fs::path root = scratch.Make("nearest");
  WriteText(root / "game" / "default.xex", "further up");
  const fs::path nested = root / "deeper" / "here";
  WriteText(nested / "game" / "default.xex", "beside the launcher");
  const GameRoots nearest = DetectGameRoots(nested, {});
  CHECK_TRUE(nearest.game_root_found);
  CHECK_STR_EQ(nearest.game_root.string(), (nested / "game").string());

  BeginCase("nothing to find is reported, not guessed at");

  const fs::path bare = scratch.Make("bare");
  const GameRoots nothing = DetectGameRoots(bare, {});
  CHECK_FALSE(nothing.game_root_found);
  CHECK_STR_EQ(nothing.game_root.filename().string(), "game");

  BeginCase("--game_data_root is honoured as given");

  const fs::path other = scratch.Make("other");
  WriteText(other / "gen" / "main_xbox_0.ark", "archive");
  const GameRoots overridden = DetectGameRoots(bare, other.string());
  CHECK_TRUE(overridden.game_root_found);
  CHECK_STR_EQ(overridden.game_root.string(), other.string());

  BeginCase("an override with no game in it still says where it looked");

  const GameRoots absent = DetectGameRoots(bare, (bare / "nowhere").string());
  CHECK_FALSE(absent.game_root_found);
  CHECK_STR_EQ(absent.game_root.string(), (bare / "nowhere").string());
}

void TestPathRules(Scratch& scratch) {
  BeginCase("D4: a folder inside the game root is refused, with the reason");

  const fs::path game_root = scratch.Make("game-root");
  const fs::path inside = game_root / "user" / "save";
  std::error_code ec;
  fs::create_directories(inside, ec);

  const PathVerdict inside_verdict =
      ValidatePathValue("inside_game_root:forbid", inside, game_root);
  CHECK_FALSE(inside_verdict.ok);
  CHECK_CONTAINS(inside_verdict.reason, "inside the game data folder");
  CHECK_CONTAINS(inside_verdict.reason, "platform user folder");

  BeginCase("the game root itself is 'inside' too, and a sibling is not");

  CHECK_FALSE(ValidatePathValue("inside_game_root:forbid", game_root, game_root).ok);
  const fs::path sibling = scratch.Make("game-root-sibling");
  CHECK_TRUE(ValidatePathValue("inside_game_root:forbid", sibling, game_root).ok);

  BeginCase("an empty value is the game's own default, and always fine");

  CHECK_TRUE(ValidatePathValue("inside_game_root:forbid|dlc_layout", {}, game_root).ok);

  BeginCase("D4: the DLC root is judged by the layout the runtime encodes");

  const fs::path dlc_ok = scratch.Make("dlc-ok");
  WriteText(dlc_ok / "4E4D07FF" / "00000002" / "package", "CON the rest of the container");
  const PathVerdict accepted = ValidatePathValue("dlc_layout", dlc_ok, game_root);
  CHECK_TRUE(accepted.ok);
  CHECK_CONTAINS(accepted.note, "1 package found");

  BeginCase("a misplaced folder in the structured DLC root is reported, not accepted");

  const fs::path dlc_bad = scratch.Make("dlc-bad");
  WriteText(dlc_bad / "4E4D07FF" / "00000002" / "package", "CON the rest of the container");
  WriteText(dlc_bad / "my downloads" / "readme.txt", "not content");
  const PathVerdict refused = ValidatePathValue("dlc_layout", dlc_bad, game_root);
  CHECK_FALSE(refused.ok);
  CHECK_CONTAINS(refused.reason, "my downloads");
  CHECK_CONTAINS(refused.reason, "8 upper-case hex digits");

  BeginCase("a flat song library of loose containers is accepted, not refused");

  // The runtime reads a folder that is not the <title_id>/<content_type>/<package> layout as
  // a flat library (src/fs/dlc_library.h), so `dlc_layout` must accept a dumped song folder
  // too rather than turn away the shape the runtime is built to read.
  const fs::path songs = scratch.Make("yarg-songs");
  WriteText(songs / "Some Song.con", "CON the rest of the container");
  const PathVerdict flat =
      ValidatePathValue("dlc_layout|inside_game_root:forbid", songs, game_root);
  CHECK_TRUE(flat.ok);
  CHECK_CONTAINS(flat.note, "flat song library");

  BeginCase("an empty or absent DLC folder is fine, and says so");

  const fs::path dlc_empty = scratch.Make("dlc-empty");
  const PathVerdict empty_verdict = ValidatePathValue("dlc_layout", dlc_empty, game_root);
  CHECK_TRUE(empty_verdict.ok);
  CHECK_CONTAINS(empty_verdict.note, "No DLC packages found");

  const PathVerdict absent_verdict =
      ValidatePathValue("dlc_layout", scratch.base / "no-such-folder", game_root);
  CHECK_TRUE(absent_verdict.ok);
  CHECK_CONTAINS(absent_verdict.note, "Not a folder yet");

  BeginCase("the rules compose, and the order is the schema's");

  const fs::path dlc_inside = game_root / "dlc";
  std::error_code ec2;
  fs::create_directories(dlc_inside, ec2);
  const PathVerdict composed =
      ValidatePathValue("dlc_layout|inside_game_root:forbid", dlc_inside, game_root);
  CHECK_FALSE(composed.ok);
  CHECK_CONTAINS(composed.reason, "inside the game data folder");

  BeginCase("a rule this build does not know is refused, not waved through");

  const PathVerdict unknown = ValidatePathValue("exists", scratch.base / "gone", game_root);
  CHECK_FALSE(unknown.ok);
  CHECK_CONTAINS(unknown.reason, "does not exist");

  const PathVerdict nonsense = ValidatePathValue("be_magic", scratch.base, game_root);
  CHECK_FALSE(nonsense.ok);
  CHECK_CONTAINS(nonsense.reason, "does not know the rule 'be_magic'");
}

}  // namespace

int main() {
  Scratch scratch;
  TestUltimateStates(scratch);
  TestTargetFallback();
  TestGameRootDetection(scratch);
  TestPathRules(scratch);
  return Finish();
}
