// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The P0 gameplay-mapping probe's entry points, called from the production hooks in
// src/hooks/gameplay_input.cpp.
//
// One guest address can only have one hook: `REX_HOOK_RAW(sub_...)` defines the symbol the
// recompiled caller binds to, so two definitions would not link. The production module therefore
// owns the five hooks P3 dispatches through (the controller factory and destructor, the per-frame
// seam and the press/release boundary) and calls these functions from inside them; the probe's
// own cvar-driven logging and experiments live in gameplay_probe.cpp, and the hooks it does not
// share with production (the lookup, the message producers) stay there.
//
// Everything here is diagnostics: each function is a no-op while `probe_gameplay` is off.

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace rb_blitz::diag::gameplay_probe {

// The factory returned a controller. Kept so the probe's fixtures and its tick can address the
// object the native preset is using.
void ControllerCreated(uint32_t controller, uint32_t config, uint32_t vtable);

// The controller is being destroyed; the probe forgets it. Called before the original runs.
void ControllerDestroyed(uint32_t controller);

// One live per-frame visit (`sub_82443A30`) with the probe's own tick counter. Drives the
// injection/challenge/source experiments.
void Frame(PPCContext& ctx, uint8_t* base, uint32_t controller, int tick);

// The action boundary saw a native press/release. Logs it and answers whether the probe's
// `probe_gameplay_block_dispatch` asks for it to be suppressed.
bool PressBoundary(PPCContext& ctx, uint8_t* base);
bool ReleaseBoundary(PPCContext& ctx, uint8_t* base);

}  // namespace rb_blitz::diag::gameplay_probe
