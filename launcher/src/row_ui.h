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
#include "profile_session.h"
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

// One widget per schema kind (Contract 1). `value_text` is the value the row shows; the caller
// passes LauncherValueText, which is the compiled default until something overrides it.
void DrawReadOnlyValue(const settings::Setting& setting, float value_width,
                       std::string_view value_text);

// The value the launcher would hand the game for this row: what the profile records, or the
// compiled default it has not overridden. Once there is a writer (B4) this - not the default -
// is what a row means, and it is what the precedence badge below is decided against.
std::string_view LauncherValueText(const ProfileSession& session,
                                   const settings::Setting& setting);

// The precedence badge and its one action (B4, D3): when the game's own `rb_blitz.toml` decides
// a row - because the launcher's value is the compiled default and is therefore passed nowhere -
// the row says which value the game will really use and offers to adopt it. The game's file
// itself is never rewritten: it belongs to the game and to the F4 overlay (D2).
//
// The action is by mouse only. A second focusable target per row would be a change to the focus
// model, and a pad binding for it belongs with A5; the badge says what the value is either way,
// and the row can always be set to match it by hand.
void DrawOverrideBadge(const settings::Setting& setting, ProfileSession& session);

}  // namespace rb_blitz::launcher
