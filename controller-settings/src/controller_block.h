// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The Controller tab's block of rows: one visible-layout model for the tab that draws it and
// the shell that sizes its focus ring from it
// (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md §4, P4).
//
// The block is *not* fixed any more. Both gameplay tables can be hidden by their own switch, so
// the row list depends on two `[settings]` values - and the draw and the ring have to agree about
// it or the ring lands on a row nobody drew (the plan's "hidden rows are never focusable"). This
// module is that one arithmetic: `BuildControllerBlock` answers the row indices and the option
// count of every row, `BlockRowAt` maps a row back to what it is, and the two text helpers name
// it for the bottom bar and the focus trace.
//
// Dependency-free on purpose - no ImGui, no SDL - so the layout and the naming are unit-tested
// (tests/controller_settings_block_tests.cpp) and a headless dump can describe the block without
// a window.
//
// The row order is the plan's §4, top to bottom:
//
//   General Mapping
//     [?]                                   general-help
//     one row per pad control               control/<target> (Assign, Reset)
//     Reset all bindings                    general-reset-all
//   Gameplay Mapping
//     [?]                                   gameplay-help
//     Controller            [On|Off]        controller-switch
//     [six action rows + Reset all]         only while Controller is on
//     Keyboard + Mouse      [On|Off]        keyboard-mouse-switch
//     [six action rows + Reset all]         only while Keyboard + Mouse is on
//
// The switches themselves are always rows, whether or not the table under them is visible: a
// switch that hid itself would be a table nobody can bring back.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "profile/gameplay_remap.h"
#include "profile/profile.h"
#include "profile/remap.h"

namespace rb_blitz::profile {

// The two gameplay switches, as the block reads them. They are `[settings]` rows rather than
// profile booleans (plan D6), so Controller Settings, the game's profile apply and a hand edit
// meet in one place.
struct BlockSwitches {
  bool controller = false;      // gameplay_mapping_controller, off by default
  bool keyboard_mouse = true;   // gameplay_mapping_keyboard_mouse, on by default
};

// The keys and the compiled defaults, in one place: the schema row, the block and the runtime
// resolver in src/input/remap.cpp all have to agree, and a test pins them against each other.
inline constexpr const char* kGameplayControllerKey = "gameplay_mapping_controller";
inline constexpr const char* kGameplayKeyboardMouseKey = "gameplay_mapping_keyboard_mouse";
inline constexpr bool kGameplayControllerDefault = false;
inline constexpr bool kGameplayKeyboardMouseDefault = true;

// The two device gates the Keyboard + Mouse table's strike-through reads. The Controller table
// has no device gate: a pad has no switch of its own (plan R1).
inline constexpr const char* kKeyboardSupportKey = "keyboard_support";
inline constexpr const char* kMouseSupportKey = "mouse_ui_nav";
inline constexpr bool kKeyboardSupportDefault = true;
inline constexpr bool kMouseSupportDefault = true;

// A `[settings]` boolean, or its compiled default when the profile has no row for it (a row is
// written only when it differs from the default).
bool SettingBool(const Profile& profile, std::string_view key, bool fallback);

// The two switches as the profile records them, with their compiled defaults for the rows it
// does not mention.
BlockSwitches ReadBlockSwitches(const Profile& profile);

// No block row: the value a hidden table's indices carry, and what the shell uses when a rebuild
// has no particular row to focus.
inline constexpr std::size_t kNoBlockRow = static_cast<std::size_t>(-1);

// What a block row is. The index means the pad control for kControl and the gameplay action for
// kAction; `table` names which gameplay table an action row belongs to.
enum class BlockRowKind : uint8_t {
  kGeneralHelp,
  kControl,
  kGeneralResetAll,
  kGameplayHelp,
  kControllerSwitch,
  kKeyboardMouseSwitch,
  kAction,
  kActionResetAll,
};

struct BlockRowInfo {
  bool valid = false;
  BlockRowKind kind = BlockRowKind::kGeneralHelp;
  std::size_t index = 0;
  gameplay_remap::TableKind table = gameplay_remap::TableKind::kController;
};

// The whole visible block: where each named row starts, how many there are, and how many options
// each takes in the focus ring (two for a control or action - Assign and Reset - and one for
// everything else).
struct ControllerBlock {
  BlockSwitches switches;

  std::size_t general_help = 0;
  std::size_t general_table = 0;  // first control row; remap::kTargetCount follow
  std::size_t general_reset_all = 0;
  std::size_t gameplay_help = 0;
  std::size_t controller_switch = 0;
  std::size_t controller_table = kNoBlockRow;  // first action row while the switch is on
  std::size_t controller_reset_all = kNoBlockRow;
  std::size_t keyboard_mouse_switch = 0;
  std::size_t keyboard_mouse_table = kNoBlockRow;
  std::size_t keyboard_mouse_reset_all = kNoBlockRow;

  std::size_t row_count = 0;
  std::vector<std::size_t> row_options;  // one entry per row, in draw order

  bool has_controller_table() const { return switches.controller; }
  bool has_keyboard_mouse_table() const { return switches.keyboard_mouse; }
};

ControllerBlock BuildControllerBlock(BlockSwitches switches);

// What a row is. A row out of range answers with `valid == false`, which a caller can treat as
// "nothing to say" rather than reading an index it does not have.
BlockRowInfo BlockRowAt(const ControllerBlock& block, std::size_t row);

// The flat ring index of the row's first option: the sum of the option counts of the rows before
// it. The ring is flat (nav.h), so a row that follows a two-option row does not start one entry
// after it - this is the arithmetic that keeps the draw and the ring in step.
std::size_t BlockRowStart(const ControllerBlock& block, std::size_t row);

// The row a flat ring entry belongs to - the inverse of BlockRowStart, for the bottom bar and the
// focus trace, which are handed an entry. `kNoBlockRow` for an entry past the end.
std::size_t BlockRowForEntry(const ControllerBlock& block, std::size_t entry);

// The row's name for the focus trace: no spaces, so the harness can parse it like a schema key
// (`block:control/a`, `block:controller/action/first_note`, `block:keyboard-mouse-switch`).
std::string BlockTraceName(const ControllerBlock& block, std::size_t row);

// The row's sentence for the bottom bar (A2/D7), built from the control or action the row owns.
std::string BlockHelpText(const ControllerBlock& block, std::size_t row);

// The two `?` pages (§4). Plain sentences, so the modal and a test read the same text.
std::string_view GeneralMappingHelp();
std::string_view GameplayMappingHelp();

}  // namespace rb_blitz::profile
