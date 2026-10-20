// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the gameplay-mapping vocabulary and tables (src/profile/gameplay_remap.h).

#include "profile/gameplay_remap.h"

#include <algorithm>
#include <array>

namespace rb_blitz::profile::gameplay_remap {
namespace {

// Everything the rest of this file needs to know about one action. One table rather than four
// switches, so an action cannot be half-added: a name with no label, or a label nothing lists.
struct ActionInfo {
  Action action;
  const char* name;
  const char* label;
};

constexpr std::array<ActionInfo, kActionCount> kActionInfo{{
    {Action::kFirstNote, "first_note", "First Note"},
    {Action::kSecondNote, "second_note", "Second Note"},
    {Action::kMoveLaneRight, "move_lane_right", "Move Right"},
    {Action::kMoveLaneLeft, "move_lane_left", "Move Left"},
    {Action::kPowerup, "powerup", "Power-up"},
    {Action::kPause, "pause", "Pause"},
}};

struct TableInfo {
  TableKind kind;
  const char* section;
  const char* label;
};

constexpr std::array<TableInfo, kTableKindCount> kTableInfo{{
    {TableKind::kController, "gameplay_remap_controller", "Controller"},
    // The settings surface's own spelling (P4 of the interface/gameplay restructure plan, D6);
    // the section name above is the profile's and does not move.
    {TableKind::kKeyboardMouse, "gameplay_remap_keyboard_mouse", "Mouse & Keyboard"},
}};

static_assert(kActionInfo.size() == kActionCount,
              "kActionCount must match the action table");
static_assert(kTableInfo.size() == kTableKindCount,
              "kTableKindCount must match the table table");

const ActionInfo* InfoFor(Action action) {
  for (const ActionInfo& info : kActionInfo) {
    if (info.action == action) {
      return &info;
    }
  }
  return nullptr;
}

const TableInfo* InfoFor(TableKind kind) {
  for (const TableInfo& info : kTableInfo) {
    if (info.kind == kind) {
      return &info;
    }
  }
  return nullptr;
}

remap::Source Pad(std::string_view name) {
  return remap::Source{remap::SourceKind::kPad, std::string(name)};
}

remap::Source Key(std::string_view name) {
  return remap::Source{remap::SourceKind::kKey, std::string(name)};
}

remap::Source Mouse(std::string_view name) {
  return remap::Source{remap::SourceKind::kMouse, std::string(name)};
}

using ActionTable = std::array<std::vector<remap::Source>, kActionCount>;

// One retail controller preset (restructure plan §2.1). Preset 0 is the scheme the P0 live arms
// measured; the other three are the shipped alternatives, decoded from
// `config/gen/beatmatch_controller.dtb` on 2026-10-17. Each action names every physical input its
// preset accepts, because a preset treats all four directions of a stick as the same action and
// the bindings have to reproduce that rather than pick one. `Start` is not a `kBMAction` in the
// preset: it is the game-screen Pause path, which the direct table's overlay carries.
const std::array<ActionTable, kControllerPresetCount>& PresetTables() {
  static const std::array<ActionTable, kControllerPresetCount> presets = [] {
    std::array<ActionTable, kControllerPresetCount> all;
    // Preset 0: L-stick/L-stick all and D-pad all strike the first note; A and R-stick the
    // second; the triggers and shoulders move lanes; X powers up.
    all[0][static_cast<std::size_t>(Action::kFirstNote)] = {
        Pad("lstick_up"), Pad("lstick_down"), Pad("lstick_left"), Pad("lstick_right"),
        Pad("dpad_up"),   Pad("dpad_down"),   Pad("dpad_left"),   Pad("dpad_right")};
    all[0][static_cast<std::size_t>(Action::kSecondNote)] = {
        Pad("a"), Pad("rstick_up"), Pad("rstick_down"), Pad("rstick_left"),
        Pad("rstick_right")};
    all[0][static_cast<std::size_t>(Action::kMoveLaneRight)] = {Pad("right_trigger"),
                                                                Pad("right_shoulder")};
    all[0][static_cast<std::size_t>(Action::kMoveLaneLeft)] = {Pad("left_trigger"),
                                                               Pad("left_shoulder")};
    all[0][static_cast<std::size_t>(Action::kPowerup)] = {Pad("x")};
    all[0][static_cast<std::size_t>(Action::kPause)] = {Pad("start")};

    // Preset 1: notes on the shoulders/triggers, lanes on the sticks' horizontal directions.
    all[1][static_cast<std::size_t>(Action::kFirstNote)] = {Pad("left_trigger"),
                                                            Pad("left_shoulder")};
    all[1][static_cast<std::size_t>(Action::kSecondNote)] = {Pad("right_trigger"),
                                                             Pad("right_shoulder")};
    all[1][static_cast<std::size_t>(Action::kMoveLaneRight)] = {
        Pad("lstick_right"), Pad("rstick_right"), Pad("dpad_right")};
    all[1][static_cast<std::size_t>(Action::kMoveLaneLeft)] = {
        Pad("lstick_left"), Pad("rstick_left"), Pad("dpad_left")};
    all[1][static_cast<std::size_t>(Action::kPowerup)] = {Pad("x")};
    all[1][static_cast<std::size_t>(Action::kPause)] = {Pad("start")};

    // Preset 2: one control per action.
    all[2][static_cast<std::size_t>(Action::kFirstNote)] = {Pad("left_shoulder")};
    all[2][static_cast<std::size_t>(Action::kSecondNote)] = {Pad("right_shoulder")};
    all[2][static_cast<std::size_t>(Action::kMoveLaneRight)] = {Pad("right_trigger")};
    all[2][static_cast<std::size_t>(Action::kMoveLaneLeft)] = {Pad("left_trigger")};
    all[2][static_cast<std::size_t>(Action::kPowerup)] = {Pad("x")};
    all[2][static_cast<std::size_t>(Action::kPause)] = {Pad("start")};

    // Preset 3: face buttons for the notes, sticks/D-pad for the lanes, three power-up inputs.
    all[3][static_cast<std::size_t>(Action::kFirstNote)] = {Pad("a")};
    all[3][static_cast<std::size_t>(Action::kSecondNote)] = {Pad("b")};
    all[3][static_cast<std::size_t>(Action::kMoveLaneRight)] = {Pad("lstick_right"),
                                                                Pad("dpad_right")};
    all[3][static_cast<std::size_t>(Action::kMoveLaneLeft)] = {Pad("lstick_left"),
                                                               Pad("dpad_left")};
    all[3][static_cast<std::size_t>(Action::kPowerup)] = {Pad("x"), Pad("left_shoulder"),
                                                          Pad("left_trigger")};
    all[3][static_cast<std::size_t>(Action::kPause)] = {Pad("start")};
    return all;
  }();
  return presets;
}

// The Mouse & Keyboard defaults (restructure plan D12): a note on each hand's home row, the lane
// changes on the arrow keys with a wheel detent each, power-up on Space and the middle click, and
// Pause on Escape. These replace the shipped keyboard preset the earlier package froze; a click
// or a detent during a song is now a binding by default.
std::vector<remap::Source> KeyboardMouseDefault(Action action) {
  switch (action) {
    case Action::kFirstNote:
      return {Key("Q"), Mouse("right")};
    case Action::kSecondNote:
      return {Key("W"), Mouse("left")};
    case Action::kMoveLaneRight:
      return {Key("Right"), Mouse("wheel_up")};
    case Action::kMoveLaneLeft:
      return {Key("Left"), Mouse("wheel_down")};
    case Action::kPowerup:
      return {Key("Space"), Mouse("middle")};
    case Action::kPause:
      return {Key("Escape")};
  }
  return {};
}

const ActionTable& DefaultsFor(const TableKind kind) {
  static const ActionTable keyboard_mouse = [] {
    ActionTable all;
    for (const ActionInfo& info : kActionInfo) {
      all[static_cast<std::size_t>(info.action)] = KeyboardMouseDefault(info.action);
    }
    return all;
  }();
  return keyboard_mouse;
}

// The sentence a refused row is kept with. It names the source that does not belong here and the
// kinds this table does take, because "the row I wrote does nothing" is otherwise
// indistinguishable from a bug.
std::string WrongKindReason(TableKind kind, const remap::Source& source) {
  std::string reason;
  const char* found = "?";
  switch (source.kind) {
    case remap::SourceKind::kPad:
      found = "a pad control";
      break;
    case remap::SourceKind::kKey:
      found = "a key";
      break;
    case remap::SourceKind::kMouse:
      found = "a mouse button";
      break;
  }
  reason += std::string(remap::FormatSource(source)) + " is " + found + ", and ";
  reason += std::string(TableKindSection(kind)) + " accepts ";
  reason += kind == TableKind::kController ? "pad sources only" : "keys and mouse buttons only";
  return reason;
}

}  // namespace

const std::vector<Action>& Actions() {
  static const std::vector<Action> actions = [] {
    std::vector<Action> all;
    all.reserve(kActionInfo.size());
    for (const ActionInfo& info : kActionInfo) {
      all.push_back(info.action);
    }
    return all;
  }();
  return actions;
}

std::string_view ActionName(Action action) {
  const ActionInfo* info = InfoFor(action);
  return info == nullptr ? std::string_view{} : std::string_view(info->name);
}

std::string_view ActionLabel(Action action) {
  const ActionInfo* info = InfoFor(action);
  return info == nullptr ? std::string_view{} : std::string_view(info->label);
}

std::optional<Action> ParseAction(std::string_view name) {
  for (const ActionInfo& info : kActionInfo) {
    if (info.name == name) {
      return info.action;
    }
  }
  return std::nullopt;
}

const std::vector<TableKind>& TableKinds() {
  static const std::vector<TableKind> kinds = [] {
    std::vector<TableKind> all;
    all.reserve(kTableInfo.size());
    for (const TableInfo& info : kTableInfo) {
      all.push_back(info.kind);
    }
    return all;
  }();
  return kinds;
}

std::string_view TableKindSection(TableKind kind) {
  const TableInfo* info = InfoFor(kind);
  return info == nullptr ? std::string_view{} : std::string_view(info->section);
}

std::string_view TableKindLabel(TableKind kind) {
  const TableInfo* info = InfoFor(kind);
  return info == nullptr ? std::string_view{} : std::string_view(info->label);
}

std::optional<TableKind> ParseTableKindSection(std::string_view section) {
  for (const TableInfo& info : kTableInfo) {
    if (info.section == section) {
      return info.kind;
    }
  }
  return std::nullopt;
}

bool AcceptsSourceKind(TableKind kind, remap::SourceKind source_kind) {
  if (kind == TableKind::kController) {
    return source_kind == remap::SourceKind::kPad;
  }
  return source_kind == remap::SourceKind::kKey || source_kind == remap::SourceKind::kMouse;
}

bool AcceptsSource(TableKind kind, const remap::Source& source) {
  return AcceptsSourceKind(kind, source.kind);
}

const std::vector<remap::Source>* DefaultSources(const TableKind kind, const Action action) {
  if (kind == TableKind::kController) {
    return PresetSources(0, action);
  }
  return &DefaultsFor(kind)[static_cast<std::size_t>(action)];
}

const std::vector<remap::Source>* PresetSources(const int preset, const Action action) {
  const int clamped =
      preset >= 0 && preset < kControllerPresetCount ? preset : 0;
  return &PresetTables()[static_cast<std::size_t>(clamped)][static_cast<std::size_t>(action)];
}

std::string PresetLabel(const int preset) {
  const int clamped =
      preset >= 0 && preset < kControllerPresetCount ? preset : 0;
  return "Preset " + std::to_string(clamped + 1);
}

Table Table::FromRows(TableKind kind, const std::vector<ProfileSetting>& rows) {
  Table table(kind);
  for (const ProfileSetting& row : rows) {
    const std::optional<Action> action = ParseAction(row.key);
    if (!action) {
      // A name this build does not know: a newer Controller Settings's action, or a hand-written
      // key. Kept verbatim like any other row this build cannot read, and never applied.
      table.unknown_.push_back(row);
      continue;
    }
    // An empty value is a deliberate "nothing fires this", which is a different fact from a value
    // this build cannot read - the plan's D6 asks for the two to be told apart.
    const std::optional<std::vector<remap::Source>> sources =
        row.value.empty()
            ? std::optional<std::vector<remap::Source>>{std::vector<remap::Source>{}}
            : remap::ParseSourceList(row.value);
    if (!sources) {
      table.rejected_.push_back(
          RejectedRow{row, "the value is not a source list this build understands"});
      continue;
    }
    // The row is understood but belongs to the other table: keep it exactly as written, say why,
    // and apply nothing from it. Half-applying the sources of the right kind would make a
    // half-working row look like a working one.
    const auto outside =
        std::find_if(sources->begin(), sources->end(), [kind](const remap::Source& source) {
          return !AcceptsSource(kind, source);
        });
    if (outside != sources->end()) {
      table.rejected_.push_back(RejectedRow{row, WrongKindReason(kind, *outside)});
      continue;
    }
    table.Set(*action, *sources);
  }
  return table;
}

std::vector<ProfileSetting> Table::ToRows() const {
  std::vector<ProfileSetting> rows;
  rows.reserve(bindings_.size() + unknown_.size() + rejected_.size());
  for (const Action action : Actions()) {
    const std::vector<remap::Source>* sources = Bindings(action);
    if (sources == nullptr) {
      continue;
    }
    // A basic string, always: an empty list and a list are both strings, so the value's style
    // cannot be inferred and does not need to be.
    rows.push_back(ProfileSetting{std::string(ActionName(action)),
                                  remap::FormatSourceList(*sources), ValueStyle::kBasic});
  }
  // Everything kept verbatim, in the order it was read within each list. A row the table refused
  // is written back exactly as it came in, so a save can never turn a row this build will not
  // apply into a different row.
  for (const ProfileSetting& row : unknown_) {
    rows.push_back(row);
  }
  for (const RejectedRow& rejected : rejected_) {
    rows.push_back(rejected.row);
  }
  return rows;
}

const std::vector<remap::Source>* Table::Bindings(Action action) const {
  for (const Binding& binding : bindings_) {
    if (binding.action == action) {
      return &binding.sources;
    }
  }
  return nullptr;
}

const std::vector<remap::Source>* Table::Effective(const Action action) const {
  if (const std::vector<remap::Source>* bound = Bindings(action)) {
    return bound;
  }
  // The Controller table follows the retail preset the player selected (D4); the Mouse & Keyboard
  // table has no preset and answers its compiled default.
  if (kind_ == TableKind::kController) {
    return PresetSources(preset_, action);
  }
  return DefaultSources(kind_, action);
}

void Table::SetControllerPreset(const int preset) {
  preset_ = preset >= 0 && preset < kControllerPresetCount ? preset : 0;
}

bool Table::IsDisabled(Action action) const {
  const std::vector<remap::Source>* sources = Bindings(action);
  return sources != nullptr && sources->empty();
}

void Table::Set(Action action, std::vector<remap::Source> sources) {
  for (Binding& binding : bindings_) {
    if (binding.action == action) {
      binding.sources = std::move(sources);
      return;
    }
  }
  bindings_.push_back(Binding{action, std::move(sources)});
}

void Table::Reset(Action action) {
  bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                 [action](const Binding& binding) {
                                   return binding.action == action;
                                 }),
                  bindings_.end());
}

void Table::ResetAll() { bindings_.clear(); }

std::size_t Table::BoundCount() const { return bindings_.size(); }

std::size_t Table::EffectiveBoundCount() const {
  std::size_t count = 0;
  for (const Action action : Actions()) {
    const std::vector<remap::Source>* sources = Effective(action);
    if (sources != nullptr && !sources->empty()) {
      ++count;
    }
  }
  return count;
}

bool Table::AnySourceSet() const {
  for (const Action action : Actions()) {
    const std::vector<remap::Source>* sources = Effective(action);
    if (sources != nullptr && !sources->empty()) {
      return true;
    }
  }
  return false;
}

}  // namespace rb_blitz::profile::gameplay_remap
