// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Rock Band Blitz Ultimate (https://github.com/ultimate-mods-rb/blitz-ultimate)
// compatibility layer.
//
// The mod ships a title-update payload (gen/patch_xbox.hdr, gen/patch_xbox_0.ark)
// plus a default.xex that differs from the retail one in 12 bytes. This layer
// reproduces those bytes host-side and maps the payload into the guest-visible
// tree, so the payload can be dropped next to the retail game data instead of
// replacing it. See docs/ultimate-compat.md for the evidence.

#pragma once

#include <cstdint>
#include <filesystem>

namespace rex {
class Runtime;
}  // namespace rex

namespace rb_blitz::ultimate {

// One bit per edit the mod makes to default.xex, so a single edit can be
// isolated from the command line (--ultimate_patches). The guest addresses are
// documented in docs/ultimate-compat.md.
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

// True when `bit` is active for this run. Configure() sets the mask once, before
// the guest starts, and only guest threads read it afterwards.
bool PatchEnabled(uint32_t bit);

// Resolves the mode from the cvars and the payload on disk, maps the payload's
// files into the guest-visible tree and reapplies the mod's default.xex edits to
// the loaded image.
//
// Call once from RbBlitzApp::OnPostLoadXexImage(): the VFS is up, the XEX is in
// guest memory and the guest has not started yet. Without a payload this logs and
// returns, leaving retail behaviour untouched.
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

}  // namespace rb_blitz::ultimate
