// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9 - the runtime half of the live apply (src/settings/hot_apply.h owns the seam). This file is
// the only place that knows HOW a `hot` row reaches the running game, and today it knows exactly
// one row:
//
//   * `graphics.vsync` writes the SDK's own `vsync` cvar **by name**, because it is defined in
//     `rexruntime.dll`'s GPU category and this project defines no storage symbol for it. The same
//     call R7's frame pacing already makes, and the reason the row is `hot` at all: with the guest
//     running, that write moved the discovery from 16.66 ms per item to 5.68 ms (docs/dlc.md).
//
// Two constraints from the project, both structural rather than stylistic:
//
//   * **a guest thread must not touch the cvar registry** (docs/plans/song-cache-plan.md §7). The
//     commit that triggers an apply arrives on the guest thread (it is the guest's own
//     `write_file`), so the write itself happens on a short-lived host thread and this function
//     waits for it: the caller must be able to say "applied" only when the write really happened.
//     A join on a string assignment is microseconds, against a commit that already wrote a file;
//     R7 goes async because it is on the *frame* path, and a settings commit is not.
//   * **the registry is queried by name, and the answer is logged.** A write that silently fails
//     (an unknown cvar, a lifecycle that refuses) must be a `kFailed` outcome the menu can act on,
//     which is why `Apply` reads the value back before it reports success.
//
// SDK-dependent on purpose: it is the one file in P9 that must see `rex/cvar.h`. Everything that
// can be decided without the emulator was left in src/settings/hot_apply.cpp.

#pragma once

#include <string_view>

#include "settings/hot_apply.h"
#include "settings/model.h"

namespace rb_blitz::settings {

// The one applier a boot has. Never null; a row it does not know is reported as `kNoApplier` by
// the seam, so an unwired row honestly asks for a restart instead of pretending.
HotApplier* RuntimeHotApplier();

// The stable token -> id lookup the guest channel needs (`graphics.vsync` on the wire). kUnknown
// for a token this build does not describe, so a stale path can never be applied.
SettingId IdForToken(std::string_view token);

// The token a row's live write targets, for the log line: "graphics.vsync" for kVsync, otherwise
// the empty string. Kept here rather than in the schema because it is the *wire* spelling.
std::string_view HotRowToken(SettingId id);

// The rows this build can apply live, by token. Used by the boot's diagnostic line so a log says
// what the hot set is rather than leaving it to be inferred from the schema.
std::string HotApplierSummary();

}  // namespace rb_blitz::settings
