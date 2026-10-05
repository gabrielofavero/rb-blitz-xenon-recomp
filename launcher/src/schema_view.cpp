// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the schema-to-layout view (launcher/src/schema_view.h, A1).

#include "schema_view.h"

#include <cctype>
#include <iterator>
#include <string>
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

std::string DisplayTabName(settings::Tab tab) {
  std::string name(settings::TabName(tab));
  if (!name.empty()) {
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  }
  return name;
}

bool SettingVisible(const settings::Setting& setting, const RowEnvironment& environment) {
  if (setting.visible == "multi_monitor") {
    return environment.multiple_monitors;
  }
  return true;
}

std::string_view GroupNoteText(const settings::Group& group) {
  return OrPlaceholder(group.note, "Nothing in this group is available in this build yet.");
}

TabLayout BuildTabLayout(settings::Tab tab, const RowEnvironment& environment) {
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
      if (setting.tab != tab || setting.group != group.name) {
        continue;
      }
      // A row the environment hides is not part of the layout at all. It is filtered here
      // rather than skipped by each caller, because the ring that decides what is focusable
      // and the loop that draws are two different pieces of code: the only way they cannot
      // disagree is for the row to be gone before either of them sees it.
      if (!SettingVisible(setting, environment)) {
        ++layout.hidden_count;
        continue;
      }
      entry.rows.push_back(LayoutRow{&setting, nullptr});
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

std::vector<TabLayout> BuildLayout(const RowEnvironment& environment) {
  std::vector<TabLayout> layout;
  for (const settings::Tab tab : TabOrder()) {
    layout.push_back(BuildTabLayout(tab, environment));
  }
  return layout;
}

const settings::Setting* FindSetting(std::string_view key) {
  for (const settings::Setting& setting : settings::kSettings) {
    if (setting.key == key) {
      return &setting;
    }
  }
  return nullptr;
}

std::vector<std::string_view> SettingChoices(const settings::Setting& setting) {
  std::vector<std::string_view> choices;
  const std::string_view text = setting.choices;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t comma = text.find(',', start);
    const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
    choices.push_back(text.substr(start, end - start));
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return choices;
}

std::size_t FocusEntriesFor(const settings::Setting& setting) {
  if (setting.kind != settings::Kind::kEnum) {
    return 1;
  }
  const std::size_t count = SettingChoices(setting).size();
  return count == 0 ? 1 : count;
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
    out += " unavailable, ";
    out += std::to_string(tab.hidden_count);
    out += " hidden by rule\n";
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
