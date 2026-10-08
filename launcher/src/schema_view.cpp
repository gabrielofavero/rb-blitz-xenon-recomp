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
  // The graphics tab is the audio tab too, and "Graphics" would hide half of what is on it.
  if (tab == settings::Tab::kGraphics) {
    return "Audio / Video";
  }
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
  if (setting.visible == "ultimate_installed") {
    return environment.ultimate_installed;
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
    // A category this build has nothing for is not drawn at all: not a disabled widget, and
    // not a heading with a line of apology under it either - the newer instruction is to
    // reduce the useless text, and a name for something the user cannot act on is exactly
    // that. It is still named in the dump below, so the list of what is owed survives.
    if (group.unavailable) {
      layout.unbuilt.push_back(&group);
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
      entry.rows.push_back(&setting);
    }
    // A group that declared no rows at all is the same thing as one declared unavailable, and
    // the same as one whose only rows were hidden: there is nothing to show.
    if (entry.rows.empty()) {
      layout.unbuilt.push_back(&group);
      continue;
    }
    layout.row_count += entry.rows.size();
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

std::vector<std::size_t> SchemaRowOptions(const TabLayout& tab) {
  std::vector<std::size_t> rows;
  for (const LayoutGroup& group : tab.groups) {
    for (const settings::Setting* row : group.rows) {
      // The launch target is drawn as its choices stacked under the group heading rather than as
      // one line of radios, so each choice is a row of its own - the same shape the General tab
      // draws it with (general_tab.cpp's DrawTargetRows).
      if (row->key == "launch.target") {
        for (std::size_t choice = 0; choice < FocusEntriesFor(*row); ++choice) {
          rows.push_back(1);
        }
      } else {
        rows.push_back(FocusEntriesFor(*row));
      }
    }
  }
  return rows;
}

const settings::Setting* SettingForEntry(const TabLayout& tab, std::size_t entry) {
  for (const LayoutGroup& group : tab.groups) {
    for (const settings::Setting* row : group.rows) {
      const std::size_t entries = FocusEntriesFor(*row);
      if (entry < entries) {
        return row;
      }
      entry -= entries;
    }
  }
  return nullptr;
}

std::string_view RowActionVerb(const settings::Setting& setting) {
  switch (setting.kind) {
    case settings::Kind::kBool:
      return "Toggle";
    case settings::Kind::kInt:
    case settings::Kind::kFloat:
      // A row with a range steps through it; one without is typed like any other text.
      return setting.has_range ? "Adjust" : "Edit";
    case settings::Kind::kEnum:
      return "Choose";
    case settings::Kind::kString:
      return "Edit";
    case settings::Kind::kPathDir:
    case settings::Kind::kPathFile:
      return "Browse";
  }
  return "Activate";
}

std::string FlattenHelpText(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool pending_space = false;
  for (const char character : text) {
    const bool blank = character == ' ' || character == '\t' || character == '\n' ||
                       character == '\r';
    if (blank) {
      // A run of blanks is one space, and a run that ends the text is nothing at all.
      pending_space = !out.empty();
      continue;
    }
    if (pending_space) {
      out += ' ';
      pending_space = false;
    }
    out += character;
  }
  return out;
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
    out += std::to_string(tab.unbuilt.size());
    out += " not in this build, ";
    out += std::to_string(tab.hidden_count);
    out += " hidden by rule\n";
    for (const LayoutGroup& group : tab.groups) {
      out += "  group ";
      out += group.group->name;
      out += "\n";
      for (const settings::Setting* row : group.rows) {
        const settings::Setting& setting = *row;
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
        // An enum's choices, because they are part of what the row can do and one row's are
        // decided by the build rather than by the schema (`choices_from`). Without this the
        // dump cannot answer "is Vulkan offered?" except by opening the window.
        if (setting.kind == settings::Kind::kEnum) {
          out += "  choices: ";
          const std::vector<std::string_view> choices = SettingChoices(setting);
          for (std::size_t index = 0; index < choices.size(); ++index) {
            if (index != 0) {
              out += ", ";
            }
            out += choices[index];
          }
        }
        // A numeric row whose ceiling is a fact about the machine rather than a number in the
        // schema: the dump says so, because "how high can this slider go?" is otherwise only
        // answerable on the machine it is drawn on.
        if (!setting.max_from.empty()) {
          out += "  max_from: ";
          out += setting.max_from;
        }
        if (setting.tooltip.empty()) {
          out += "  MISSING-TOOLTIP";
        }
        out += "\n";
      }
    }
    // The categories this build does not fill: named here so the dump still says what is
    // owed, even though the tab itself shows nothing for them.
    for (const settings::Group* group : tab.unbuilt) {
      out += "  unbuilt ";
      out += group->name;
      out += ": ";
      out += GroupNoteText(*group);
      out += "\n";
    }
  }
  return out;
}

}  // namespace rb_blitz::launcher
