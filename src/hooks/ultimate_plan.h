// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The decidable half of the Rock Band Blitz Ultimate compatibility layer: what
// --ultimate_mode and --ultimate_patches select, where the payload is looked for,
// which files make a directory a payload, and whether the content-device slot still
// holds the retail bytes. Deliberately free of SDK headers and of runtime state, for
// the same reason src/hooks/crypto_keytable.h is - it is host path arithmetic and a
// byte comparison, and tests/ultimate_plan_tests.cpp has to cover it without booting
// the game (docs/rb3-references.md §7.3). src/hooks/ultimate.cpp is the only
// production consumer and holds the guest addresses, the evidence and the hooks;
// docs/ultimate-compat.md has the payload's side.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace rb_blitz::ultimate {

// One bit per edit the mod makes to default.xex, so a single edit can be isolated
// from the command line (--ultimate_patches). The guest addresses are documented in
// docs/ultimate-compat.md.
enum Patch : uint32_t {
  // 0x8205DD74: the 8-byte content device string "UPDATE:\0" -> "D:\0...".
  kPatchContentDevice = 1u << 0,
  // 0x821D0A7C (byte 0x821D0A7F): tail of sub_821D0A18, "li r3,1" -> "li r3,0", i.e.
  // the two entry song blacklist (name, id) table never matches.
  kPatchSongBlacklist = 1u << 1,
  // 0x8236C108: prologue of sub_8236C108, "mflr r12" -> "blr", i.e. Blitz's
  // PlatformMgr::SetDiskError returns immediately instead of latching a disk error
  // and spinning forever. The checksum validator calls it with 3 (failed checksum)
  // for any ark that is not in the retail checksum database, which is exactly what
  // the mod's payload is.
  kPatchDiskError = 1u << 2,
  kPatchAll = (1u << 3) - 1,
};

// The mask is clamped to the bits that exist, so an unknown value from a config file
// cannot leave a bit set that no patch answers.
constexpr uint32_t ClampPatchMask(uint32_t mask) { return mask & kPatchAll; }

constexpr bool HasPatch(uint32_t mask, uint32_t bit) { return (mask & bit) != 0; }

// 0x8205DD74, the content device string slot, and both spellings of its 8 bytes.
inline constexpr uint32_t kContentDeviceSlot = 0x8205DD74;
inline constexpr char kContentDeviceRetail[8] = {'U', 'P', 'D', 'A', 'T', 'E', ':', '\0'};
inline constexpr char kContentDevicePayload[8] = {'D', ':', '\0', '\0', '\0', '\0', '\0', '\0'};

// What the slot holds. The data patch is applied only to the retail spelling: an
// image that already carries the mod's edit is left alone, and so is one that holds
// something else entirely - a different image layout - which is reported rather than
// overwritten.
enum class ContentDeviceState { kRetail, kAlreadyRedirected, kUnrecognized };

inline ContentDeviceState ClassifyContentDeviceSlot(const uint8_t* slot) {
  if (std::memcmp(slot, kContentDevicePayload, sizeof(kContentDevicePayload)) == 0) {
    return ContentDeviceState::kAlreadyRedirected;
  }
  if (std::memcmp(slot, kContentDeviceRetail, sizeof(kContentDeviceRetail)) == 0) {
    return ContentDeviceState::kRetail;
  }
  return ContentDeviceState::kUnrecognized;
}

// The files that make a directory a payload, and the file the overlay hides so that a
// stale payload copy cannot take over the boot.
inline constexpr std::string_view kPayloadHeader = "gen/patch_xbox.hdr";
inline constexpr std::string_view kPayloadArchive = "gen/patch_xbox_0.ark";
inline constexpr std::string_view kEntrypoint = "default.xex";

// A payload is present when the game can find a patch header for it.
inline bool HasPayload(const std::filesystem::path& root) {
  std::error_code ec;
  return std::filesystem::is_regular_file(root / kPayloadHeader, ec) && !ec;
}

// A header without its archive beside it is the pair the game reads as a damaged
// disc, so it is worth a warning even though the header alone still mounts.
inline bool HasPayloadArchive(const std::filesystem::path& root) {
  std::error_code ec;
  return std::filesystem::is_regular_file(root / kPayloadArchive, ec) && !ec;
}

// Empty selects <game_data_root>/ultimate, a relative value resolves against
// game_data_root, and an absolute value is honoured as given. Purely lexical, so the
// result is the configuration's own arithmetic and not a function of the working
// directory. The default keeps the game root's own spelling; the other two are
// normalised, which is what the payload path has always been.
inline std::filesystem::path ResolvePayloadRoot(std::string_view configured,
                                                const std::filesystem::path& game_data_root) {
  if (configured.empty()) {
    return game_data_root / "ultimate";
  }
  std::filesystem::path root{std::string(configured)};
  if (root.is_relative()) {
    root = game_data_root / root;
  }
  return root.lexically_normal();
}

// --ultimate_mode. The cvar's range is 0..2; anything else is read the way the
// switch reads it - 0 or below is off, 1 is auto, everything above is force.
enum class Mode { kOff = 0, kAuto = 1, kForce = 2 };

constexpr Mode ModeFromValue(int32_t value) {
  if (value <= 0) {
    return Mode::kOff;
  }
  if (value == 1) {
    return Mode::kAuto;
  }
  return Mode::kForce;
}

// What the mode and the payload on disk agree to do. Vanilla Blitz is the default:
// with the mode off nothing is touched at all, and auto mode never forces a patch
// onto a game root that has nothing to patch for.
enum class Outcome {
  kOff,                   // mode 0: the retail behaviour, payload or not
  kRetailNoPayload,       // auto mode with nothing on disk: again the retail behaviour
  kForcedWithoutPayload,  // force mode with nothing on disk: patch, but nothing readable
  kPayload,               // a payload is present: patch, and mount it when it is separate
};

constexpr Outcome DecideOutcome(Mode mode, bool has_payload, bool merged) {
  if (mode == Mode::kOff) {
    return Outcome::kOff;
  }
  if (!has_payload && !merged) {
    return mode == Mode::kAuto ? Outcome::kRetailNoPayload : Outcome::kForcedWithoutPayload;
  }
  return Outcome::kPayload;
}

// The overlay is the one deviation with no byte in the mod's image: the payload's
// files are unioned with the game root only when they live in a directory of their
// own. A payload already merged into the game root is already visible.
constexpr bool ShouldMountOverlay(bool has_payload, bool merged) {
  return has_payload && !merged;
}

}  // namespace rb_blitz::ultimate
