// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The per-present entry point of the in-song HUD re-anchor (docs/settings/graphics.md
// part 3 §9). The stock HUD is authored scene data, so the script seam alone cannot move
// it; this pass finds the scene's top-level groups by name after the scene has loaded and
// applies the same tested rule to their local x.

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace rb_blitz::hooks {

// Called once per guest present, after the title's swap packet has been built. Safe to
// call in every screen: it returns immediately unless the boot frame is non-16:9 and the
// rule has a non-zero shift. `ctx` is the present call's context, used only to call the
// guest's own transform-dirty routine after a write.
void HudReanchorPresent(PPCContext& ctx, uint8_t* base);

}  // namespace rb_blitz::hooks
