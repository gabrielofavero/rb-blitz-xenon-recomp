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
//
// The decidable half - the patch bits, the mode/payload decision, the payload's
// spelling and the content-device slot classification - is in hooks/ultimate_plan.h,
// which is SDK-free and covered by tests/ultimate_plan_tests.cpp without a boot.

#pragma once

#include <cstdint>
#include <filesystem>

#include "hooks/ultimate_plan.h"

namespace rex {
class Runtime;
}  // namespace rex

namespace rb_blitz::ultimate {

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
