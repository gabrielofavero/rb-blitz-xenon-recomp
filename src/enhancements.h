// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The enhancement toggles: one cvar per planned guest-side customization, all
// defaulting off, so an unconfigured boot is the faithful one. This header is
// the whole interface - a feature reads its toggle with REXCVAR_GET, exactly as
// src/input/mouse_ui.cpp reads its own - and src/enhancements.cpp owns the
// registrations and the one boot line that says what each toggle is set to.
//
// The names are the `[enhancements]` table keys in <exe name>.toml with the
// runtime's own prefix: ApplyTomlTable joins nested tables with '_'
// (rexglue-sdk/src/core/cvar.cpp), so `[enhancements] skip_offline_dialog =
// true` sets the cvar `enhancements_skip_offline_dialog`. docs/engine/toggles.md
// is the table, with each toggle's faithful behaviour and the plan prompt that
// will implement it (docs/plans/customization-plan.md, R1-R9).

#pragma once

namespace rb_blitz::enhancements {

// Logs every toggle with its effective value and the source that set it (a
// compiled default, the config file, an environment variable, the command line
// or a runtime write). Called once per boot, after the paths are final and the
// logging is up; it decides nothing and changes no state.
//
// The point is evidence: "the toggle did nothing" and "the toggle was never on"
// look identical in the log otherwise, and every planned enhancement is
// off-by-default on purpose.
void LogToggles();

}  // namespace rb_blitz::enhancements
