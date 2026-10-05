// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pieces every tab's rows are made of (docs/plans/launcher-plan.md A1, B1).
//
// A1's shell draws a read-only value per `kind`; B1's General tab draws its own editors for
// three of its rows. Both need the same label, the same focus behaviour and the same column
// widths, so those live here instead of in either of them - a row looks the same whichever
// prompt owns its value widget.
//
// The header stays free of ImGui so the shell's shape does not depend on the renderer; the
// implementation is ImGui, like the shell it serves.

#pragma once

#include <cstddef>
#include <string_view>

#include "nav.h"
#include "settings_table.h"

namespace rb_blitz::launcher {

// The two columns a row is laid out in, so a tab that draws its own value widget lines up
// with the read-only rows around it.
struct RowColumns {
  float label_width = 0.0f;
  float value_width = 0.0f;
};

RowColumns RowColumnWidths();

// Scopes a row's ImGui ids to its key, so the label and the value widget of one row cannot
// collide with the next row's - or with another tab's copy of the same key.
class RowScope {
 public:
  explicit RowScope(std::string_view key);
  ~RowScope();
  RowScope(const RowScope&) = delete;
  RowScope& operator=(const RowScope&) = delete;
};

// The row's focus target: its label. Clicking it focuses the row and hovering adopts the ring,
// so the mouse and the keyboard agree on one selection (A1, D6). Returns true on click.
bool DrawRowLabel(const settings::Setting& setting, std::size_t index, FocusModel& ring,
                  float label_width);

// One widget per schema kind, showing the compiled default (Contract 1). A1's default for
// every row that has no editor yet.
void DrawReadOnlyValue(const settings::Setting& setting, float value_width);

}  // namespace rb_blitz::launcher
