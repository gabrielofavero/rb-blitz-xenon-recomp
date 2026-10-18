// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the Controller tab's block layout (controller-settings/src/controller_block.h).

#include "controller_block.h"

#include <algorithm>

namespace rb_blitz::profile {
namespace {

const char* TablePath(gameplay_remap::TableKind kind) {
  return kind == gameplay_remap::TableKind::kController ? "controller" : "keyboard-mouse";
}

std::string_view ControlLabel(std::size_t index) {
  const std::vector<remap::Target>& targets = remap::Targets();
  if (index >= targets.size()) {
    return "A control";
  }
  return remap::TargetLabel(targets[index]);
}

std::string_view ActionLabel(std::size_t index) {
  const std::vector<gameplay_remap::Action>& actions = gameplay_remap::Actions();
  if (index >= actions.size()) {
    return "An action";
  }
  return gameplay_remap::ActionLabel(actions[index]);
}

}  // namespace

bool SettingBool(const Profile& profile, std::string_view key, bool fallback) {
  const ProfileSetting* row = profile.FindSetting(key);
  if (row == nullptr) {
    return fallback;
  }
  return row->value == "true";
}

BlockSwitches ReadBlockSwitches(const Profile& profile) {
  BlockSwitches switches;
  switches.controller =
      SettingBool(profile, kGameplayControllerKey, kGameplayControllerDefault);
  switches.keyboard_mouse =
      SettingBool(profile, kGameplayKeyboardMouseKey, kGameplayKeyboardMouseDefault);
  return switches;
}

ControllerBlock BuildControllerBlock(BlockSwitches switches) {
  ControllerBlock block;
  block.switches = switches;
  std::size_t row = 0;

  block.general_help = row++;
  block.general_table = row;
  row += remap::kTargetCount;
  block.general_reset_all = row++;
  block.gameplay_help = row++;
  block.controller_switch = row++;
  if (switches.controller) {
    block.controller_table = row;
    row += gameplay_remap::kActionCount;
    block.controller_reset_all = row++;
  }
  block.keyboard_mouse_switch = row++;
  if (switches.keyboard_mouse) {
    block.keyboard_mouse_table = row;
    row += gameplay_remap::kActionCount;
    block.keyboard_mouse_reset_all = row++;
  }
  block.row_count = row;

  // The ring's own arithmetic: a control or an action is one row with Assign and Reset side by
  // side, everything else is one row with one option. The draw walks the same list, which is what
  // keeps a hidden table out of the ring rather than merely off the screen.
  block.row_options.reserve(row);
  block.row_options.push_back(1);  // general help
  for (std::size_t control = 0; control < remap::kTargetCount; ++control) {
    block.row_options.push_back(2);
  }
  block.row_options.push_back(1);  // Reset all bindings
  block.row_options.push_back(1);  // gameplay help
  block.row_options.push_back(1);  // Controller switch
  if (switches.controller) {
    for (std::size_t action = 0; action < gameplay_remap::kActionCount; ++action) {
      block.row_options.push_back(2);
    }
    block.row_options.push_back(1);  // the controller table's Reset all
  }
  block.row_options.push_back(1);  // Keyboard + Mouse switch
  if (switches.keyboard_mouse) {
    for (std::size_t action = 0; action < gameplay_remap::kActionCount; ++action) {
      block.row_options.push_back(2);
    }
    block.row_options.push_back(1);  // the keyboard/mouse table's Reset all
  }
  return block;
}

BlockRowInfo BlockRowAt(const ControllerBlock& block, std::size_t row) {
  BlockRowInfo info;
  if (row >= block.row_count) {
    return info;
  }
  info.valid = true;
  if (row == block.general_help) {
    info.kind = BlockRowKind::kGeneralHelp;
    return info;
  }
  if (row >= block.general_table && row < block.general_table + remap::kTargetCount) {
    info.kind = BlockRowKind::kControl;
    info.index = row - block.general_table;
    return info;
  }
  if (row == block.general_reset_all) {
    info.kind = BlockRowKind::kGeneralResetAll;
    return info;
  }
  if (row == block.gameplay_help) {
    info.kind = BlockRowKind::kGameplayHelp;
    return info;
  }
  if (row == block.controller_switch) {
    info.kind = BlockRowKind::kControllerSwitch;
    return info;
  }
  if (block.has_controller_table() && row >= block.controller_table &&
      row < block.controller_table + gameplay_remap::kActionCount) {
    info.kind = BlockRowKind::kAction;
    info.index = row - block.controller_table;
    info.table = gameplay_remap::TableKind::kController;
    return info;
  }
  if (block.has_controller_table() && row == block.controller_reset_all) {
    info.kind = BlockRowKind::kActionResetAll;
    info.table = gameplay_remap::TableKind::kController;
    return info;
  }
  if (row == block.keyboard_mouse_switch) {
    info.kind = BlockRowKind::kKeyboardMouseSwitch;
    return info;
  }
  if (block.has_keyboard_mouse_table() && row >= block.keyboard_mouse_table &&
      row < block.keyboard_mouse_table + gameplay_remap::kActionCount) {
    info.kind = BlockRowKind::kAction;
    info.index = row - block.keyboard_mouse_table;
    info.table = gameplay_remap::TableKind::kKeyboardMouse;
    return info;
  }
  if (block.has_keyboard_mouse_table() && row == block.keyboard_mouse_reset_all) {
    info.kind = BlockRowKind::kActionResetAll;
    info.table = gameplay_remap::TableKind::kKeyboardMouse;
    return info;
  }
  info.valid = false;
  return info;
}

std::size_t BlockRowStart(const ControllerBlock& block, std::size_t row) {
  std::size_t start = 0;
  const std::size_t limit = std::min(row, block.row_options.size());
  for (std::size_t index = 0; index < limit; ++index) {
    start += block.row_options[index];
  }
  return start;
}

std::size_t BlockRowForEntry(const ControllerBlock& block, std::size_t entry) {
  std::size_t start = 0;
  for (std::size_t row = 0; row < block.row_options.size(); ++row) {
    const std::size_t options = block.row_options[row] == 0 ? 1 : block.row_options[row];
    if (entry < start + options) {
      return row;
    }
    start += options;
  }
  return kNoBlockRow;
}

std::string BlockTraceName(const ControllerBlock& block, std::size_t row) {
  const BlockRowInfo info = BlockRowAt(block, row);
  if (!info.valid) {
    return "block:unknown";
  }
  switch (info.kind) {
    case BlockRowKind::kGeneralHelp:
      return "block:general-help";
    case BlockRowKind::kControl:
      return "block:control/" + std::string(remap::TargetName(remap::Targets()[info.index]));
    case BlockRowKind::kGeneralResetAll:
      return "block:general-reset-all";
    case BlockRowKind::kGameplayHelp:
      return "block:gameplay-help";
    case BlockRowKind::kControllerSwitch:
      return "block:controller-switch";
    case BlockRowKind::kKeyboardMouseSwitch:
      return "block:keyboard-mouse-switch";
    case BlockRowKind::kAction:
      return std::string("block:") + TablePath(info.table) + "/action/" +
             std::string(gameplay_remap::ActionName(gameplay_remap::Actions()[info.index]));
    case BlockRowKind::kActionResetAll:
      return std::string("block:") + TablePath(info.table) + "/reset-all";
  }
  return "block:unknown";
}

std::string BlockHelpText(const ControllerBlock& block, std::size_t row) {
  const BlockRowInfo info = BlockRowAt(block, row);
  if (!info.valid) {
    return {};
  }
  switch (info.kind) {
    case BlockRowKind::kGeneralHelp:
      return "Opens the General Mapping explanation: what these bindings do in menus and in "
             "play, and how Assign and Reset work.";
    case BlockRowKind::kControl: {
      if (info.index >= remap::kTargetCount) {
        return {};
      }
      return std::string(ControlLabel(info.index)) +
             ": adds the next input it sees - a pad button, a key or a mouse button - to what "
             "already presses it.";
    }
    case BlockRowKind::kGeneralResetAll:
      return "Removes every General Mapping assignment at once, so every control goes back to the "
             "keys it ships with.";
    case BlockRowKind::kGameplayHelp:
      return "Opens the Gameplay Mapping explanation: the two switches, the inputs each table "
             "accepts, and when a change applies.";
    case BlockRowKind::kControllerSwitch:
      return "Controller gameplay mapping: on, the Controller table drives the six gameplay "
             "actions directly and the saved controller preset is bypassed; off, the saved preset "
             "plays exactly as before. The preset itself is kept either way. Applies the next time "
             "the game starts.";
    case BlockRowKind::kKeyboardMouseSwitch:
      return "Keyboard + Mouse gameplay mapping: on, that table drives the six gameplay actions "
             "directly while the controller keeps its saved preset; off, keys and mouse go through "
             "General Mapping. Applies the next time the game starts.";
    case BlockRowKind::kAction: {
      if (info.index >= gameplay_remap::kActionCount) {
        return {};
      }
      const std::string label(ActionLabel(info.index));
      const char* accepted =
          info.table == gameplay_remap::TableKind::kController ? "pad" : "key or mouse";
      return label + ": adds the next " + accepted +
             " input it sees to what already triggers the action in this table.";
    }
    case BlockRowKind::kActionResetAll:
      return info.table == gameplay_remap::TableKind::kController
                 ? "Removes every Controller gameplay assignment at once, putting the six "
                   "actions back to the pad controls the title's preset uses."
                 : "Removes every Keyboard + Mouse gameplay assignment at once, putting the six "
                   "actions back to the title's keyboard preset.";
  }
  return {};
}

std::string_view GeneralMappingHelp() {
  return
      "General Mapping binds inputs to controller buttons. A control can answer to several "
      "inputs: press Assign and then a pad button, a key or a mouse button, and it is added to "
      "what already presses the control.\n"
      "\n"
      "While a gameplay mapping switch is on, these bindings control the menus only. With the "
      "matching gameplay switch off they also feed the game's own controller preset during play, "
      "exactly as they always did.\n"
      "\n"
      "Mouse clicks and the wheel belong to the game's menu pointer while a menu is up; they do "
      "not also press a bound control there.\n"
      "\n"
      "Reset removes your row for one control, putting it back to its own pad button and key. "
      "Reset all bindings does that for every control. Assignments are written by Save, on the "
      "bottom bar.";
}

std::string_view GameplayMappingHelp() {
  return
      "Gameplay Mapping binds inputs straight to the game's six gameplay actions, in two "
      "independent tables.\n"
      "\n"
      "Controller takes pad controls only. While it is on, the pad drives the actions through "
      "this table and the saved controller preset is bypassed; the preset is kept, and turning "
      "the switch off restores it unchanged.\n"
      "\n"
      "Keyboard + Mouse takes keys and mouse buttons only. While it is on, those inputs drive the "
      "actions directly while the controller keeps its saved preset - the two input types work "
      "together.\n"
      "\n"
      "A table hides while its switch is off and keeps its bindings. A key or mouse binding whose "
      "device support is off stays listed but is crossed out, because the game ignores that "
      "device.\n"
      "\n"
      "Pause is a gameplay action: once the game is paused, General Mapping looks after Resume and "
      "Back. Changes apply the next time the game starts.";
}

}  // namespace rb_blitz::profile
