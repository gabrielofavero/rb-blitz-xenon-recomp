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
// replacing it. See docs/guides/ultimate-compat.md for the evidence.
//
// The decidable half - the patch bits, the mode/payload decision, the payload's
// spelling and the content-device slot classification - is in hooks/ultimate_plan.h,
// which is SDK-free and covered by tests/ultimate_plan_tests.cpp without a boot.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "hooks/ultimate_plan.h"
#include "settings/ultimate_adapter.h"

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
//
// This is also where P6's provider adapter runs (the payload is inspected and shape-checked, and
// the provider's own settings file is redirected into user storage as part of the overlay).
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);
// Make the title's patch archive reachable without enabling any Ultimate gameplay patches.
void EnableSettingsArchive(rex::Runtime* runtime);

// --- P6: the provider adapter's half of this boot -------------------------------------------------
//
// `Provider()` is computed once, in Configure(), on the host thread; the guest hooks only read it.
// It is empty when Configure() has not run, when there is no payload to inspect (including the
// merged-payload install, which has no directory of its own to read and no overlay to redirect
// through - see docs/settings/architecture.md (part 5, the Ultimate adapter)), or when the cvar turned the mod
// off.
const settings::ultimate_adapter::ProviderRuntime& Provider();

// Writes the provider's own pairs (`variable -> verbatim value text`, as
// `settings::ultimate_adapter::EncodeProviderValues` spells them) into the provider's settings file
// in user storage, durably. False + `error` when nothing was written. Does not touch the guest: the
// provider's own loader reads the file on the next boot, which is why every provider row is a
// `restart` row (plan §5.5).
bool SaveProvider(const std::map<std::string, std::string>& updates, std::string* error);

}  // namespace rb_blitz::ultimate
