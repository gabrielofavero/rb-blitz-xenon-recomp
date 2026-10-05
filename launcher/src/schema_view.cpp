// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the schema-to-layout view (launcher/src/schema_view.h, A1).

#include "schema_view.h"

#include <iterator>
#include <utility>

namespace rb_blitz::launcher {
namespace {

std::string_view OrPlaceholder(std::string_view text, std::string_view placeholder) {
  return text.empty() ? placeholder : text;
}

}  // namespace

std::vector<settings::Tab> TabOrder() {
  std::vector<settings::Tab> tabs;
  tabs.reserve(std::size(settings::kTabNames));
  for (std::size_t index = 0; index < std::size(settings::kTabNames); ++index) {
    tabs.push_back(static_cast<settings::Tab>(index));
  }
  return tabs;
}

std::string_view GroupNoteText(const settings::Group& group) {
  return OrPlaceholder(group.note, "Nothing in this group is available in this build yet.");
}

TabLayout BuildTabLayout(settings::Tab tab) {
  TabLayout layout;
  layout.tab = tab;
  // kGroups' order is the display order within a tab, and kSettings' order is the order
  // inside a group - both come from the .toml, so the shell never sorts.
  for (const settings::Group& group : settings::kGroups) {
    if (group.tab != tab) {
      continue;
    }
    LayoutGroup entry;
    entry.group = &group;
    for (const settings::Setting& setting : settings::kSettings) {
      if (setting.tab == tab && setting.group == group.name) {
        entry.rows.push_back(LayoutRow{&setting, nullptr});
      }
    }
    // An unavailable group's rows are deliberately not drawn, and a group with no rows is
    // the same thing: a category with nothing in it is not a setting (D14).
    if (group.unavailable || entry.rows.empty()) {
      entry.rows.clear();
      entry.rows.push_back(LayoutRow{nullptr, &group});
      entry.unavailable = true;
      ++layout.note_count;
    } else {
      layout.row_count += entry.rows.size();
    }
    layout.groups.push_back(std::move(entry));
  }
  return layout;
}

std::vector<TabLayout> BuildLayout() {
  std::vector<TabLayout> layout;
  for (const settings::Tab tab : TabOrder()) {
    layout.push_back(BuildTabLayout(tab));
  }
  return layout;
}

std::string DescribeLayout(const std::vector<TabLayout>& layout) {
  std::string out;
  for (const TabLayout& tab : layout) {
    out += "tab ";
    out += settings::TabName(tab.tab);
    out += ": ";
    out += std::to_string(tab.row_count);
    out += " rows, ";
    out += std::to_string(tab.groups.size());
    out += " groups, ";
    out += std::to_string(tab.note_count);
    out += " unavailable\n";
    for (const LayoutGroup& group : tab.groups) {
      out += "  group ";
      out += group.group->name;
      out += group.unavailable ? " (unavailable)\n" : "\n";
      for (const LayoutRow& row : group.rows) {
        if (row.setting == nullptr) {
          out += "    note   ";
          out += GroupNoteText(*row.note_group);
          out += "\n";
          continue;
        }
        const settings::Setting& setting = *row.setting;
        out += "    row    ";
        out += setting.key;
        out += " [";
        out += settings::KindName(setting.kind);
        out += ", ";
        out += settings::AppliesName(setting.applies);
        out += "] ";
        out += setting.label;
        out += " = ";
        out += OrPlaceholder(setting.default_text, "(empty)");
        if (setting.tooltip.empty()) {
          out += "  MISSING-TOOLTIP";
        }
        out += "\n";
      }
    }
  }
  return out;
}

}  // namespace rb_blitz::launcher
