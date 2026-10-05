// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The editable half of a settings row (docs/plans/launcher-plan.md D12, prompt B2).
//
// A1 drew every row read-only and greyed; this is what turns the schema into widgets the user
// can actually change, writing each one straight into the session's profile (B4's writer, then
// its Save). It is deliberately schema-driven: a row's kind picks the widget, its `min`/`max`
// bound a slider, and its `choices` become the radios of an enum - so a new row is still a
// settings.toml edit and nothing else.
//
// What it does *not* own is the wording: whether a change needs a restart is the row's
// `applies`, and the tab draws that.

#pragma once

#include <cstddef>

#include "nav.h"
#include "profile_session.h"
#include "settings_table.h"

namespace rb_blitz::launcher {

// Draws the widget for one row and writes what the user changes into `session`. The row takes
// FocusEntriesFor(setting) ring entries starting at `index`. Returns true when the profile's
// value for the row changed this frame.
//
// Kinds that have nothing honest to edit (path rows, and any kind without an editor) are drawn
// read-only rather than silently ignoring a click (B2).
bool DrawEditableSetting(const settings::Setting& setting, float value_width, std::size_t index,
                         FocusModel& ring, NavAction action, ProfileSession& session);

}  // namespace rb_blitz::launcher
