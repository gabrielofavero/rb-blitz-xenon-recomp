// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Turning the settings schema into the shell's row list (docs/plans/launcher-plan.md A1).
//
// The generated table (launcher/out/generated/settings_table.h, P0.2) is the only source
// of tabs, groups and rows. This module reads it and produces a flat, ordered shape the
// shell can draw, so the shell holds no per-setting knowledge and adding a row is a
// settings.toml edit and nothing else.
//
// Dependency-free on purpose - no ImGui, no SDL: the layout is a data question, so it can
// be tested (E1) and dumped (--dump-layout) without a window.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "settings_table.h"

namespace rb_blitz::launcher {

// One line in a tab. Exactly one pointer is set: a setting the ring can land on, or the
// single line an unavailable group shows instead of rows (D14).
struct LayoutRow {
  const settings::Setting* setting = nullptr;
  const settings::Group* note_group = nullptr;
};

// A group's rows, in the table's order. `unavailable` means no rows are drawn: the group
// is its name plus one line, never a disabled widget that looks like a setting (D14).
struct LayoutGroup {
  const settings::Group* group = nullptr;
  std::vector<LayoutRow> rows;
  bool unavailable = false;
};

struct TabLayout {
  settings::Tab tab = settings::Tab::kGeneral;
  std::vector<LayoutGroup> groups;
  std::size_t row_count = 0;   // focusable settings across every group
  std::size_t note_count = 0;  // groups rendered as a name and one line
};

// Every tab, in the table's own order, because the tab list is data (R9): adding a Tab to
// the schema is what adds a tab to the launcher.
std::vector<settings::Tab> TabOrder();

// The tab's groups and rows. A group marked unavailable, and a group with no rows at all,
// both become one note row carrying the group's own text.
TabLayout BuildTabLayout(settings::Tab tab);

std::vector<TabLayout> BuildLayout();

// The one row with this key, or nullptr. The General tab (B1) uses it to find the row a
// picked path belongs to, and B7 will use it to turn a profile into the game's argv.
const settings::Setting* FindSetting(std::string_view key);

// The one line a group with nothing to draw shows. A group declared unavailable without a
// note still says something honest rather than rendering an empty heading.
std::string_view GroupNoteText(const settings::Group& group);

// A stable, human-readable rendering of the layout: the evidence --dump-layout prints
// that every schema row reaches a tab and every row carries a tooltip.
std::string DescribeLayout(const std::vector<TabLayout>& layout);

}  // namespace rb_blitz::launcher
