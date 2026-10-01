// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the decidable half of the Rock Band Blitz Ultimate compatibility
// layer (src/hooks/ultimate_plan.h): which --ultimate_mode value and which payload on
// disk make the layer touch anything, where the payload is looked for, which
// directory entries make a payload, and whether the content-device slot may be
// rewritten. No SDK, no game image, no boot: the questions are path arithmetic, a
// byte comparison and a truth table.
//
// What is pinned here is the layer's promise that vanilla Blitz is the default: mode
// 0 touches nothing even with a payload on disk, auto mode patches only when there is
// something to patch for, and the content-device data patch runs only from the exact
// retail bytes - so an image that already carries the mod's edit, or one with a
// different layout, is left alone (docs/ultimate-compat.md, src/hooks/ultimate.cpp).

#include "check.h"

#include "hooks/ultimate_plan.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace rb_blitz::test;
using namespace rb_blitz::ultimate;

namespace fs = std::filesystem;

namespace {

// A scratch tree holding a game root and the payload directory the default spelling
// names, so HasPayload() answers about real directory entries rather than a spelling.
//
//   <temp>/rb_blitz-ultimate-plan/game                 the stand-in for game_data_root
//   <temp>/rb_blitz-ultimate-plan/game/ultimate/gen    the default payload root
struct Scratch {
  fs::path base;
  fs::path game;
  fs::path payload;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-ultimate-plan";
    fs::remove_all(base, ec);
    game = base / "game";
    payload = game / "ultimate";
    fs::create_directories(payload, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }

  // Writes a file of `size` bytes, creating its parents.
  static void Write(const fs::path& path, std::size_t size = 16) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << std::string(size, '\0');
  }
};

}  // namespace

int main() {
  BeginCase("the patch bits are the mod's three edits and their union");
  {
    CHECK_EQ(kPatchContentDevice, 1u);
    CHECK_EQ(kPatchSongBlacklist, 2u);
    CHECK_EQ(kPatchDiskError, 4u);
    CHECK_EQ(kPatchAll, 7u);
  }

  BeginCase("the patch mask is clamped to the bits that exist");
  {
    const uint32_t bits[] = {kPatchContentDevice, kPatchSongBlacklist, kPatchDiskError};
    for (const uint32_t bit : bits) {
      CHECK_TRUE(HasPatch(kPatchAll, bit));
      CHECK_TRUE(HasPatch(ClampPatchMask(bit), bit));
      CHECK_FALSE(HasPatch(0u, bit));
    }
    CHECK_EQ(ClampPatchMask(0u), 0u);
    CHECK_EQ(ClampPatchMask(0xFFFFFFFFu), kPatchAll);
    // A bit no patch answers is dropped rather than left set in the mask.
    CHECK_EQ(ClampPatchMask(kPatchAll | (1u << 7)), kPatchAll);
    CHECK_FALSE(HasPatch(ClampPatchMask(0xFFFFFFFFu), 1u << 3));
  }

  BeginCase("--ultimate_mode reads 0/1/2 the way the switch does, and nothing else");
  {
    CHECK_TRUE(ModeFromValue(0) == Mode::kOff);
    CHECK_TRUE(ModeFromValue(1) == Mode::kAuto);
    CHECK_TRUE(ModeFromValue(2) == Mode::kForce);
    // The cvar's range is 0..2, so these cannot arrive from a config file; the mapping
    // is the switch's own, because the range is a guard and not the rule.
    CHECK_TRUE(ModeFromValue(-1) == Mode::kOff);
    CHECK_TRUE(ModeFromValue(3) == Mode::kForce);
    CHECK_TRUE(ModeFromValue(INT32_MAX) == Mode::kForce);
  }

  BeginCase("the mode and the payload on disk decide whether anything is touched");
  {
    // Off is off: a payload on disk changes nothing, which is what "0=off" promises.
    CHECK_TRUE(DecideOutcome(Mode::kOff, true, false) == Outcome::kOff);
    CHECK_TRUE(DecideOutcome(Mode::kOff, false, true) == Outcome::kOff);
    CHECK_TRUE(DecideOutcome(Mode::kOff, true, true) == Outcome::kOff);
    CHECK_TRUE(DecideOutcome(Mode::kOff, false, false) == Outcome::kOff);

    // Auto patches only when there is something to patch for - a payload beside the
    // game data, or one already merged into it.
    CHECK_TRUE(DecideOutcome(Mode::kAuto, false, false) == Outcome::kRetailNoPayload);
    CHECK_TRUE(DecideOutcome(Mode::kAuto, true, false) == Outcome::kPayload);
    CHECK_TRUE(DecideOutcome(Mode::kAuto, false, true) == Outcome::kPayload);
    CHECK_TRUE(DecideOutcome(Mode::kAuto, true, true) == Outcome::kPayload);

    // Force patches anyway and says the payload's content will not be readable.
    CHECK_TRUE(DecideOutcome(Mode::kForce, false, false) == Outcome::kForcedWithoutPayload);
    CHECK_TRUE(DecideOutcome(Mode::kForce, true, false) == Outcome::kPayload);
    CHECK_TRUE(DecideOutcome(Mode::kForce, false, true) == Outcome::kPayload);
    CHECK_TRUE(DecideOutcome(Mode::kForce, true, true) == Outcome::kPayload);
  }

  BeginCase("the overlay is mounted only for a payload in a directory of its own");
  {
    CHECK_TRUE(ShouldMountOverlay(true, false));
    CHECK_FALSE(ShouldMountOverlay(true, true));
    CHECK_FALSE(ShouldMountOverlay(false, true));
    CHECK_FALSE(ShouldMountOverlay(false, false));
  }

  BeginCase("the content-device slot is patched only from its exact retail bytes");
  {
    CHECK_EQ(kContentDeviceSlot, 0x8205DD74u);

    const uint8_t* retail = reinterpret_cast<const uint8_t*>(kContentDeviceRetail);
    const uint8_t* redirected = reinterpret_cast<const uint8_t*>(kContentDevicePayload);
    CHECK_MEM_EQ(retail, reinterpret_cast<const uint8_t*>("UPDATE:\0"), 8);
    CHECK_MEM_EQ(redirected, reinterpret_cast<const uint8_t*>("D:\0\0\0\0\0\0\0"), 8);
    // The two spellings differ, which is what makes the patch idempotent: a second
    // call finds kAlreadyRedirected and stops.
    CHECK_FALSE(std::memcmp(retail, redirected, 8) == 0);

    CHECK_TRUE(ClassifyContentDeviceSlot(retail) == ContentDeviceState::kRetail);
    CHECK_TRUE(ClassifyContentDeviceSlot(redirected) == ContentDeviceState::kAlreadyRedirected);

    // A slot that redirects somewhere else, or holds anything else at all - a
    // different image layout - is reported, not overwritten.
    uint8_t elsewhere[8] = {'U', 'P', 'D', 'A', 'T', 'E', ':', '\0'};
    elsewhere[4] = 'F';
    CHECK_TRUE(ClassifyContentDeviceSlot(elsewhere) == ContentDeviceState::kUnrecognized);

    uint8_t zeros[8] = {};
    CHECK_TRUE(ClassifyContentDeviceSlot(zeros) == ContentDeviceState::kUnrecognized);

    // The comparison is a whole-slot one: the retail spelling with the tail byte
    // changed is no longer the retail spelling.
    uint8_t truncated[8] = {'U', 'P', 'D', 'A', 'T', 'E', ':', 'X'};
    CHECK_TRUE(ClassifyContentDeviceSlot(truncated) == ContentDeviceState::kUnrecognized);
  }

  BeginCase("the payload root resolves against the game root, lexically");
  {
    const fs::path game = "D:/game";
    CHECK_TRUE(ResolvePayloadRoot("", game) == game / "ultimate");
    CHECK_TRUE(ResolvePayloadRoot("ultimate", game) == game / "ultimate");
    CHECK_TRUE(ResolvePayloadRoot("./ultimate", game) == game / "ultimate");
    CHECK_TRUE(ResolvePayloadRoot("a/../ultimate", game) == game / "ultimate");
    CHECK_TRUE(ResolvePayloadRoot("extra/payload", game) == game / "extra" / "payload");
    CHECK_TRUE(ResolvePayloadRoot("../other", game) == game.parent_path() / "other");

    // An absolute value is honoured as given.
    const fs::path absolute = "D:/elsewhere/ultimate";
    CHECK_TRUE(ResolvePayloadRoot(absolute.string(), game) == absolute);

    // A relative value is resolved, never left dangling: the answer is not a function
    // of the working directory, so it cannot depend on where the launcher was started.
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    const fs::path before = ResolvePayloadRoot("extra/payload", game);
    fs::current_path(fs::temp_directory_path(ec), ec);
    const fs::path after = ResolvePayloadRoot("extra/payload", game);
    if (!ec) {
      fs::current_path(cwd, ec);
    }
    CHECK_TRUE(before == after);
    CHECK_TRUE(before == game / "extra" / "payload");

    // A relative configured value is normalised; the default is the game root with
    // "ultimate" appended and nothing else. Both are the arithmetic that has always
    // been there, pinned so that changing either is a deliberate act.
    CHECK_TRUE(ResolvePayloadRoot("./ultimate", "D:/game/.") == fs::path("D:/game/ultimate"));
    CHECK_TRUE(ResolvePayloadRoot("", "D:/game/.") == fs::path("D:/game/./ultimate"));
    CHECK_FALSE(ResolvePayloadRoot("", "D:/game/.") == fs::path("D:/game/ultimate"));
  }

  BeginCase("the payload's spelling is the mod's own");
  {
    CHECK_TRUE(kPayloadHeader == "gen/patch_xbox.hdr");
    CHECK_TRUE(kPayloadArchive == "gen/patch_xbox_0.ark");
    CHECK_TRUE(kEntrypoint == "default.xex");
  }

  BeginCase("a directory is a payload when it holds the patch header");
  {
    Scratch s;
    CHECK_FALSE(HasPayload(s.payload));
    CHECK_FALSE(HasPayload(s.game));
    CHECK_FALSE(HasPayload(s.game / "nowhere"));

    // A directory named like the header is not the header: the game opens a file.
    std::error_code ec;
    fs::create_directories(s.payload / kPayloadHeader, ec);
    CHECK_FALSE(HasPayload(s.payload));
    fs::remove_all(s.payload / kPayloadHeader, ec);

    Scratch::Write(s.payload / kPayloadHeader);
    CHECK_TRUE(HasPayload(s.payload));
    CHECK_FALSE(HasPayload(s.game));

    // The header alone is a payload - one the game reads as a damaged disc, which is
    // why the archive is asked about separately.
    CHECK_FALSE(HasPayloadArchive(s.payload));

    Scratch::Write(s.payload / kPayloadArchive);
    CHECK_TRUE(HasPayloadArchive(s.payload));

    // A payload merged into the game root is found there instead, and is not a
    // separate payload any more.
    Scratch::Write(s.game / kPayloadHeader);
    CHECK_TRUE(HasPayload(s.game));
  }

  return Finish();
}
