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
  std::size_t row_count = 0;    // focusable settings across every group
  std::size_t note_count = 0;   // groups rendered as a name and one line
  std::size_t hidden_count = 0; // rows the environment kept out of this tab
};

// Every tab, in the table's own order, because the tab list is data (R9): adding a Tab to
// the schema is what adds a tab to the launcher.
std::vector<settings::Tab> TabOrder();

// The tab's name as a person reads it - "General", not the lowercase key the schema spells.
// The strip, the headings and the window title all use this.
std::string DisplayTabName(settings::Tab tab);

// What a row's `visible` rule is decided from. Passed in rather than looked up, so this module
// stays dependency-free and a headless dump can describe another machine's answer.
struct RowEnvironment {
  // False only when the machine is known to have one display: the rule a Monitor row needs,
  // because there is nothing to choose between. A caller that has not looked - --dump-layout, a
  // test - leaves this true, so no rule hides a row by accident.
  bool multiple_monitors = true;
};

// False when a row declares a `visible` rule this environment does not satisfy. A row with no
// rule is always visible.
bool SettingVisible(const settings::Setting& setting, const RowEnvironment& environment);

// The tab's groups and rows, with the rows the environment hides already left out - so a
// caller that draws this layout cannot draw, or focus, a row that is not there. A group marked
// unavailable, and a group with no rows at all, both become one note row carrying the group's
// own text.
TabLayout BuildTabLayout(settings::Tab tab, const RowEnvironment& environment = {});

std::vector<TabLayout> BuildLayout(const RowEnvironment& environment = {});

// The one row with this key, or nullptr. The General tab (B1) uses it to find the row a
// picked path belongs to, and B7 will use it to turn a profile into the game's argv.
const settings::Setting* FindSetting(std::string_view key);

// An enum row's `choices`, split into their own tokens in the schema's order. Empty for a
// kind that has none.
std::vector<std::string_view> SettingChoices(const settings::Setting& setting);

// How many entries a row takes in its tab's focus ring: one per choice for an enum, because
// each choice is drawn as its own radio the ring can land on, and one for anything else.
std::size_t FocusEntriesFor(const settings::Setting& setting);

// The one line a group with nothing to draw shows. A group declared unavailable without a
// note still says something honest rather than rendering an empty heading.
std::string_view GroupNoteText(const settings::Group& group);

// A stable, human-readable rendering of the layout: the evidence --dump-layout prints
// that every schema row reaches a tab and every row carries a tooltip.
std::string DescribeLayout(const std::vector<TabLayout>& layout);

}  // namespace rb_blitz::launcher
