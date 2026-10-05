// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// What the bottom bar says about the row under the focus ring (docs/plans/launcher-plan.md A2,
// D7): the walk from a ring entry to the row it belongs to, the verb Enter performs on that row,
// and the tooltip as the bar re-wraps it. Dependency-free - the bar itself is ImGui's, but
// everything it decides is a question about the schema, which is what this file checks.

#include "check.h"

#include "schema_view.h"

#include <string>
#include <string_view>
#include <vector>

namespace settings = rb_blitz::launcher::settings;

namespace {

using rb_blitz::launcher::BuildLayout;
using rb_blitz::launcher::FindSetting;
using rb_blitz::launcher::FlattenHelpText;
using rb_blitz::launcher::FocusEntriesFor;
using rb_blitz::launcher::RowActionVerb;
using rb_blitz::launcher::SettingForEntry;
using rb_blitz::launcher::TabLayout;
using rb_blitz::launcher::TabOrder;

const TabLayout& TabOf(settings::Tab tab) {
  static const std::vector<TabLayout> layout = BuildLayout();
  for (const TabLayout& candidate : layout) {
    if (candidate.tab == tab) {
      return candidate;
    }
  }
  return layout.front();
}

void TestEntryNamesTheRow() {
  rb_blitz::test::BeginCase("a ring entry names the row the tab drew");
  // The walk has to agree with the draw, and it does so by counting entries the same way: a row
  // takes one, an enum takes one per choice because each choice is its own radio.
  for (const settings::Tab tab : TabOrder()) {
    const TabLayout& layout = TabOf(tab);
    std::size_t entry = 0;
    std::size_t rows = 0;
    for (const rb_blitz::launcher::LayoutGroup& group : layout.groups) {
      for (const settings::Setting* row : group.rows) {
        ++rows;
        for (std::size_t offset = 0; offset < FocusEntriesFor(*row); ++offset) {
          CHECK_TRUE(SettingForEntry(layout, entry + offset) == row);
        }
        entry += FocusEntriesFor(*row);
      }
    }
    // Every row reached, and the entry after the last one is the block the tab draws itself -
    // which is not a setting, and is what the shell asks the block about (B4's panel, D16's table).
    CHECK_TRUE(rows == layout.row_count);
    CHECK_TRUE(SettingForEntry(layout, entry) == nullptr);
  }
}

void TestEntryPastTheEnd() {
  rb_blitz::test::BeginCase("an entry past the last row belongs to nobody");
  // The tab blocks are the shell's tail, so the walk reports them as "not a setting" rather than
  // wrapping round to the first row - which would have the bar describing the wrong row for every
  // button in the block.
  const TabLayout& general = TabOf(settings::Tab::kGeneral);
  CHECK_TRUE(SettingForEntry(general, 0) != nullptr);
  CHECK_TRUE(SettingForEntry(general, 1000) == nullptr);
  // The first entry of every tab is the first row that tab draws.
  for (const settings::Tab tab : TabOrder()) {
    const TabLayout& layout = TabOf(tab);
    if (!layout.groups.empty() && !layout.groups.front().rows.empty()) {
      CHECK_TRUE(SettingForEntry(layout, 0) == layout.groups.front().rows.front());
    }
  }
}

void TestActionVerb() {
  rb_blitz::test::BeginCase("what Enter does on a row");
  // The verb is half of the hint line, so it is about the widget: a checkbox is toggled, a slider
  // is adjusted, a list of choices is chosen. The launch target is the row a user meets first.
  const settings::Setting* target = FindSetting("launch.target");
  CHECK_TRUE(target != nullptr);
  if (target != nullptr) {
    CHECK_TRUE(RowActionVerb(*target) == "Choose");
  }
  int enums = 0;
  int bools = 0;
  int numbers = 0;
  int text = 0;
  for (const settings::Setting& setting : settings::kSettings) {
    const std::string_view verb = RowActionVerb(setting);
    CHECK_FALSE(verb.empty());
    switch (setting.kind) {
      case settings::Kind::kEnum:
        CHECK_TRUE(verb == "Choose");
        ++enums;
        break;
      case settings::Kind::kBool:
        CHECK_TRUE(verb == "Toggle");
        ++bools;
        break;
      case settings::Kind::kInt:
      case settings::Kind::kFloat:
        CHECK_TRUE(verb == (setting.has_range ? "Adjust" : "Edit"));
        ++numbers;
        break;
      default:
        CHECK_TRUE(verb == "Edit" || verb == "Browse");
        ++text;
        break;
    }
  }
  // Not the counts themselves but the coverage: a schema with no rows of one kind would leave the
  // bar's hint line untested for that kind without this failing.
  CHECK_TRUE(enums > 0);
  CHECK_TRUE(bools > 0);
  CHECK_TRUE(numbers > 0);
  CHECK_TRUE(text > 0);
}

void TestFlatten() {
  rb_blitz::test::BeginCase("a tooltip as one run of text");
  // The line breaks in settings.toml are the file's own wrapping; the bar wraps to the width it
  // has, which is a number the file cannot know. So the breaks go, or a three-line tooltip would
  // be three lines on a bar with room for two - and the third would be drawn over the buttons.
  CHECK_TRUE(FlattenHelpText("one\ntwo") == "one two");
  CHECK_TRUE(FlattenHelpText("one\n  two") == "one two");
  CHECK_TRUE(FlattenHelpText("  one \t two  ") == "one two");
  CHECK_TRUE(FlattenHelpText("one\r\ntwo") == "one two");
  CHECK_TRUE(FlattenHelpText("") == "");
  CHECK_TRUE(FlattenHelpText("\n") == "");
  CHECK_TRUE(FlattenHelpText("single") == "single");
  // And every row's own tooltip survives it as something to read, which is the bar's contract:
  // a row with nothing to say is one the schema build refuses to make (P0.2).
  for (const settings::Setting& setting : settings::kSettings) {
    CHECK_FALSE(FlattenHelpText(setting.tooltip).empty());
  }
}

}  // namespace

// A2 and A3 are two halves of one question - what moves the ring, and what the bar says about
// where it landed - so they share a test binary. The suite is named rather than run from main()
// in this file, because a program has one main and the pad's checks have it.
void RunLauncherHelpChecks() {
  TestEntryNamesTheRow();
  TestEntryPastTheEnd();
  TestActionVerb();
  TestFlatten();
}
