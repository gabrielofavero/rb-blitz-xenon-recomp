// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The Controller tab's block layout (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md
// §4, P4): which rows exist under each combination of the two gameplay switches, where each one
// starts in the flat focus ring, what the bottom bar calls it, and how the binding value column is
// measured and crossed. Dependency-free - no ImGui, no SDL, no window - because the ring arithmetic
// and the strike arithmetic are the two things a screenshot cannot prove.

#include "check.h"

#include "binding_text.h"
#include "controller_block.h"
#include "schema_view.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace {

using rb_blitz::profile::BlockRowAt;
using rb_blitz::profile::BlockRowInfo;
using rb_blitz::profile::BlockRowKind;
using rb_blitz::profile::BlockRowStart;
using rb_blitz::profile::BlockSwitches;
using rb_blitz::profile::BlockHelpText;
using rb_blitz::profile::BlockTraceName;
using rb_blitz::profile::BuildControllerBlock;
using rb_blitz::profile::ControllerBlock;
using rb_blitz::profile::GameplayMappingHelp;
using rb_blitz::profile::GeneralMappingHelp;
using rb_blitz::profile::Profile;
using rb_blitz::profile::ReadBlockSwitches;
using rb_blitz::profile::SettingBool;

using rb_blitz::profile::binding_text::Layout;
using rb_blitz::profile::binding_text::Span;
using rb_blitz::profile::binding_text::Wrap;

namespace binding_text = rb_blitz::profile::binding_text;
namespace settings = rb_blitz::profile::settings;

// check.h compares integers, so strings get their own reporter that prints both sides.
void CheckString(const char* file, int line, const std::string_view actual,
                 const std::string_view expected, const char* actual_expr,
                 const char* expected_expr) {
  ++rb_blitz::test::g_checks;
  if (actual == expected) {
    return;
  }
  rb_blitz::test::Fail(file, line,
                       std::string(actual_expr) + " == " + expected_expr + " failed\n         actual   " +
                           std::string(actual) + "\n         expected " + std::string(expected));
}

#define CHECK_STRING(actual, expected) CheckString(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

// The test's font: ten units per character. The separator is two characters, so 20.
float MeasureTen(std::string_view text) {
  return static_cast<float>(text.size()) * 10.0f;
}

void CheckBlockIsCoherent(const ControllerBlock& block) {
  CHECK_EQ(block.row_count, block.row_options.size());
  std::size_t total = 0;
  for (std::size_t row = 0; row < block.row_count; ++row) {
    const BlockRowInfo info = BlockRowAt(block, row);
    CHECK_TRUE(info.valid);
    // The flat ring has no gaps: a row begins where the one before it ended, and every option of
    // a row resolves back to it (what the bottom bar and the focus trace rely on).
    CHECK_EQ(BlockRowStart(block, row), total);
    for (std::size_t option = 0; option < block.row_options[row]; ++option) {
      CHECK_EQ(rb_blitz::profile::BlockRowForEntry(block, total + option), row);
    }
    total += block.row_options[row];
    CHECK_FALSE(BlockTraceName(block, row).empty());
    CHECK_FALSE(BlockHelpText(block, row).empty());
  }
  CHECK_EQ(BlockRowStart(block, block.row_count), total);
  // Every row's trace name is unique, so a focus trace cannot name two rows the same.
  for (std::size_t left = 0; left < block.row_count; ++left) {
    for (std::size_t right = left + 1; right < block.row_count; ++right) {
      CHECK_FALSE(BlockTraceName(block, left) == BlockTraceName(block, right));
    }
  }
  // A row past the end is nobody's.
  CHECK_FALSE(BlockRowAt(block, block.row_count).valid);
  CHECK_TRUE(rb_blitz::profile::BlockRowForEntry(block, total) == rb_blitz::profile::kNoBlockRow);
}

void TestBlockLayout() {
  rb_blitz::test::BeginCase("the block's rows follow the two gameplay switches");

  // Both tables hidden: help, the 16 controls, Reset all, the gameplay help and the two
  // switches. The switches stay rows even when their table is gone, or nothing could bring it
  // back.
  const ControllerBlock none = BuildControllerBlock(BlockSwitches{false, false});
  CHECK_EQ(none.row_count, 21);
  CHECK_TRUE(none.general_help == 0);
  CHECK_TRUE(none.general_table == 1);
  CHECK_TRUE(none.general_reset_all == 17);
  CHECK_TRUE(none.gameplay_help == 18);
  CHECK_TRUE(none.controller_switch == 19);
  CHECK_TRUE(none.controller_table == rb_blitz::profile::kNoBlockRow);
  CHECK_TRUE(none.keyboard_mouse_switch == 20);
  CHECK_TRUE(none.keyboard_mouse_table == rb_blitz::profile::kNoBlockRow);
  CheckBlockIsCoherent(none);

  // The shipping defaults: Keyboard + Mouse on, Controller off.
  const ControllerBlock defaults = BuildControllerBlock(BlockSwitches{false, true});
  CHECK_EQ(defaults.row_count, 28);  // 21 + six actions + their Reset all
  CHECK_TRUE(defaults.controller_table == rb_blitz::profile::kNoBlockRow);
  CHECK_TRUE(defaults.keyboard_mouse_table == 21);
  CHECK_TRUE(defaults.keyboard_mouse_reset_all == 27);
  CheckBlockIsCoherent(defaults);
  CHECK_STRING(BlockTraceName(defaults, defaults.general_table), "block:control/a");
  CHECK_STRING(BlockTraceName(defaults, defaults.general_help), "block:general-help");
  CHECK_STRING(BlockTraceName(defaults, defaults.general_reset_all), "block:general-reset-all");
  CHECK_STRING(BlockTraceName(defaults, defaults.gameplay_help), "block:gameplay-help");
  CHECK_STRING(BlockTraceName(defaults, defaults.controller_switch), "block:controller-switch");
  CHECK_STRING(BlockTraceName(defaults, defaults.keyboard_mouse_switch),
               "block:keyboard-mouse-switch");
  CHECK_STRING(BlockTraceName(defaults, defaults.keyboard_mouse_table),
               "block:keyboard-mouse/action/first_note");
  CHECK_STRING(BlockTraceName(defaults, defaults.keyboard_mouse_reset_all),
               "block:keyboard-mouse/reset-all");
  // A hidden table leaves no row for the ring to land on: the last row before the switch is the
  // gameplay help, and the next row is the Keyboard + Mouse switch.
  CHECK_FALSE(BlockRowAt(defaults, defaults.keyboard_mouse_table - 1).kind ==
              BlockRowKind::kAction);
  CHECK_TRUE(BlockRowAt(defaults, defaults.keyboard_mouse_table - 1).kind ==
             BlockRowKind::kKeyboardMouseSwitch);
  CHECK_TRUE(BlockRowAt(defaults, defaults.keyboard_mouse_table).kind == BlockRowKind::kAction);
  CHECK_TRUE(BlockRowAt(defaults, defaults.keyboard_mouse_table).table ==
             rb_blitz::profile::gameplay_remap::TableKind::kKeyboardMouse);

  // Both on: the Controller table appears between its switch and the Keyboard + Mouse switch.
  const ControllerBlock both = BuildControllerBlock(BlockSwitches{true, true});
  CHECK_EQ(both.row_count, 35);
  CHECK_TRUE(both.controller_table == 20);
  CHECK_TRUE(both.controller_reset_all == 26);
  CHECK_TRUE(both.keyboard_mouse_switch == 27);
  CHECK_TRUE(both.keyboard_mouse_table == 28);
  CheckBlockIsCoherent(both);
  CHECK_STRING(BlockTraceName(both, both.controller_table), "block:controller/action/first_note");
  CHECK_STRING(BlockTraceName(both, both.controller_reset_all), "block:controller/reset-all");
  CHECK_TRUE(BlockRowAt(both, both.controller_table).table ==
             rb_blitz::profile::gameplay_remap::TableKind::kController);

  // Controller only: its table is there and the Keyboard + Mouse table is not.
  const ControllerBlock controller = BuildControllerBlock(BlockSwitches{true, false});
  CHECK_EQ(controller.row_count, 28);
  CHECK_TRUE(controller.keyboard_mouse_table == rb_blitz::profile::kNoBlockRow);
  CheckBlockIsCoherent(controller);
  CHECK_STRING(BlockTraceName(controller, controller.keyboard_mouse_switch),
               "block:keyboard-mouse-switch");

  // The hidden rows are not focusable: every action row of a table exists only while its switch
  // is on, and the ring is sized from these options, so a hidden table contributes nothing.
  std::size_t option_count = 0;
  for (const std::size_t options : defaults.row_options) {
    option_count += options;
  }
  CHECK_EQ(option_count, BlockRowStart(defaults, defaults.row_count));
}

void TestSwitches() {
  rb_blitz::test::BeginCase("the switches read [settings] with their compiled defaults");
  Profile profile;
  BlockSwitches defaults = ReadBlockSwitches(profile);
  CHECK_FALSE(defaults.controller);
  CHECK_TRUE(defaults.keyboard_mouse);

  profile.Set("gameplay_mapping_controller", "true");
  profile.Set("gameplay_mapping_keyboard_mouse", "false");
  BlockSwitches explicit_values = ReadBlockSwitches(profile);
  CHECK_TRUE(explicit_values.controller);
  CHECK_FALSE(explicit_values.keyboard_mouse);

  // An explicit `false` is a value, not a missing row.
  profile.Set("gameplay_mapping_keyboard_mouse", "false");
  CHECK_FALSE(SettingBool(profile, "gameplay_mapping_keyboard_mouse", true));
  CHECK_TRUE(SettingBool(profile, "keyboard_support", true));
  profile.Set("keyboard_support", "false");
  CHECK_FALSE(SettingBool(profile, "keyboard_support", true));
}

void TestHelpPages() {
  rb_blitz::test::BeginCase("the two help pages and every row's sentence");
  const std::string_view general = GeneralMappingHelp();
  CHECK_FALSE(general.empty());
  CHECK_TRUE(general.find("General Mapping") != std::string_view::npos);
  CHECK_TRUE(general.find("menus") != std::string_view::npos);
  const std::string_view gameplay = GameplayMappingHelp();
  CHECK_FALSE(gameplay.empty());
  CHECK_TRUE(gameplay.find("Controller") != std::string_view::npos);
  CHECK_TRUE(gameplay.find("Keyboard + Mouse") != std::string_view::npos);
  // The page describes the two tables and the next-launch rule, which is what the switch status
  // line also says - the two must not disagree about when a change applies.
  CHECK_TRUE(gameplay.find("next time the game starts") != std::string_view::npos);

  // Every row of every layout has something to say, and the two `?` rows say what they open.
  for (const BlockSwitches switches :
       {BlockSwitches{false, false}, BlockSwitches{false, true}, BlockSwitches{true, false},
        BlockSwitches{true, true}}) {
    const ControllerBlock block = BuildControllerBlock(switches);
    CHECK_TRUE(BlockHelpText(block, block.general_help).find("General Mapping") !=
               std::string::npos);
    CHECK_TRUE(BlockHelpText(block, block.gameplay_help).find("Gameplay Mapping") !=
               std::string::npos);
    for (std::size_t row = 0; row < block.row_count; ++row) {
      CHECK_FALSE(BlockHelpText(block, row).empty());
    }
  }
}

void TestBindingSpans() {
  rb_blitz::test::BeginCase("the value column: every source its own span, the disabled crossed");
  // Two sources that fit on one line: text, separator, text. The separator is not crossed and
  // does not inherit the disabled source's style.
  const std::vector<Span> two = {{"pad:a", true}, {"key:K", false}};
  const Layout one_line = Wrap(two, 1000.0f, MeasureTen);
  CHECK_EQ(one_line.lines.size(), 1);
  CHECK_EQ(one_line.lines[0].segments.size(), 3);
  CHECK_EQ(one_line.widest, 120);
  if (one_line.lines[0].segments.size() == 3) {
    const auto& segments = one_line.lines[0].segments;
    CHECK_FALSE(segments[0].struck);
    CHECK_TRUE(segments[1].separator);
    CHECK_FALSE(segments[1].struck);
    CHECK_TRUE(segments[2].struck);
    CHECK_TRUE(segments[0].x == 0.0f);
    CHECK_TRUE(segments[1].x == 50.0f);
    CHECK_TRUE(segments[2].x == 70.0f);
    CHECK_TRUE(segments[0].width == 50.0f);
    CHECK_TRUE(segments[1].width == 20.0f);
    CHECK_TRUE(segments[2].width == 50.0f);
  }

  // The same two on a narrow line: the second source moves to its own line and the separator is
  // dropped there, so a wrapped row never begins with a comma.
  const Layout two_lines = Wrap(two, 100.0f, MeasureTen);
  CHECK_EQ(two_lines.lines.size(), 2);
  if (two_lines.lines.size() == 2) {
    CHECK_EQ(two_lines.lines[0].segments.size(), 1);
    CHECK_TRUE(two_lines.lines[0].width == 50.0f);
    CHECK_EQ(two_lines.lines[1].segments.size(), 1);
    CHECK_TRUE(two_lines.lines[1].width == 50.0f);
    CHECK_TRUE(two_lines.lines[1].segments[0].struck);
    CHECK_FALSE(two_lines.lines[1].segments[0].separator);
  }
  CHECK_TRUE(two_lines.widest == 50.0f);

  // A source wider than the whole line is split at the last character that fits, and every
  // piece is crossed because the source is disabled on every line.
  const std::vector<Span> long_source = {{"pad:left_trigger", false}};
  const Layout split = Wrap(long_source, 100.0f, MeasureTen);
  CHECK_EQ(split.lines.size(), 2);
  if (split.lines.size() == 2) {
    CHECK_EQ(split.lines[0].segments.size(), 1);
    CHECK_TRUE(split.lines[0].width == 100.0f);
    CHECK_TRUE(split.lines[1].width == 60.0f);
    CHECK_TRUE(split.lines[0].segments[0].struck);
    CHECK_TRUE(split.lines[1].segments[0].struck);
  }
  CHECK_TRUE(split.widest == 100.0f);

  // No sources is no line at all - the table draws an empty cell rather than an empty span.
  const Layout empty = Wrap({}, 100.0f, MeasureTen);
  CHECK_EQ(empty.lines.size(), 0);
  CHECK_TRUE(empty.widest == 0.0f);

  // A character wider than the line still makes progress: a layout can never stall on a glyph.
  const Layout tiny = Wrap({{"abc", true}}, 5.0f, MeasureTen);
  CHECK_EQ(tiny.lines.size(), 3);

  // The strike's thickness is derived from the font, never below one unit (focus_ring.h's rule).
  CHECK_TRUE(std::fabs(binding_text::StrikeThickness(16.0f) - 16.0f / 12.0f) < 0.001f);
  CHECK_TRUE(binding_text::StrikeThickness(4.0f) == 1.0f);
}

void TestDumpNamesTheBlock() {
  rb_blitz::test::BeginCase("--dump-layout's text names the block rows and the two switches");
  const std::vector<rb_blitz::profile::TabLayout> layout = {
      rb_blitz::profile::BuildTabLayout(settings::Tab::kController)};
  const ControllerBlock defaults = BuildControllerBlock(BlockSwitches{false, true});
  const std::string text = rb_blitz::profile::DescribeLayout(layout, &defaults);
  CHECK_TRUE(text.find("tab controller") != std::string::npos);
  // The schema rows are still there, including the keyboard's own gate.
  CHECK_TRUE(text.find("keyboard_support") != std::string::npos);
  CHECK_TRUE(text.find("block  28 rows, Controller off, Keyboard + Mouse on") !=
             std::string::npos);
  CHECK_TRUE(text.find("block:controller-switch (1 option) [off, table hidden]") !=
             std::string::npos);
  CHECK_TRUE(text.find("block:keyboard-mouse-switch (1 option) [on]") != std::string::npos);
  CHECK_TRUE(text.find("block:keyboard-mouse/action/first_note (2 options)") !=
             std::string::npos);
  CHECK_TRUE(text.find("block:keyboard-mouse/reset-all (1 option)") != std::string::npos);
  // The hidden table is absent, row for row.
  CHECK_TRUE(text.find("block:controller/action/first_note") == std::string::npos);

  const ControllerBlock both = BuildControllerBlock(BlockSwitches{true, true});
  const std::string all = rb_blitz::profile::DescribeLayout(layout, &both);
  CHECK_TRUE(all.find("block:controller/action/pause (2 options)") != std::string::npos);
  CHECK_TRUE(all.find("Controller on") != std::string::npos);

  // Without a block the dump is the schema alone - the old behaviour, kept for callers that have
  // no profile to read.
  const std::string schema_only = rb_blitz::profile::DescribeLayout(layout);
  CHECK_TRUE(schema_only.find("block row") == std::string::npos);
}

}  // namespace

// The Controller tab's block is layout and naming, so its checks live in the same binary as the
// focus ring's (tests/controller_settings_pad_nav_tests.cpp's main calls this).
void RunControllerSettingsBlockChecks() {
  TestBlockLayout();
  TestSwitches();
  TestHelpPages();
  TestBindingSpans();
  TestDumpNamesTheBlock();
}
