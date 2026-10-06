// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the launcher's own keys (launcher/src/nav_bindings.h, A5).

#include "nav_bindings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace rb_blitz::launcher::nav_bindings {
namespace {

// The modifier a token names, in the spelling a person is likely to write. "Control" is there
// because that is the word on the key itself on most keyboards.
enum class Modifier { kCtrl, kShift, kAlt };

std::optional<Modifier> ParseModifierToken(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char character : text) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  if (lowered == "ctrl" || lowered == "control") {
    return Modifier::kCtrl;
  }
  if (lowered == "shift") {
    return Modifier::kShift;
  }
  if (lowered == "alt") {
    return Modifier::kAlt;
  }
  return std::nullopt;
}

// A key's identity without the punctuation a person may or may not type: "Down Arrow", "down
// arrow" and "DownArrow" are one key. Only the *lookup* is normalized - what a binding was read
// as is kept exactly as the file spelled it, so a round trip never rewrites a hand-edited row.
std::string Normalize(std::string_view name) {
  std::string normalized;
  normalized.reserve(name.size());
  for (const char character : name) {
    if (character == ' ' || character == '_' || character == '-' || character == '\t') {
      continue;
    }
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return normalized;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

struct ActionInfo {
  NavAction action;
  std::string_view name;
  std::string_view label;
  std::string_view help;
  std::string_view defaults;
};

// One row per action, in the order a frame resolves them by, with the defaults the launcher ships
// with. The order is also the tie-break: a frame that sees both a move and an activation asks for
// the move, which is what the hardcoded source did.
//
// The keys are no longer the profile's to change: an [nav] table is still parsed and round-tripped
// but the launcher always reads these defaults, so a hand-edited file cannot lock a user out of
// the window that would fix it. The scheme is written down in launcher/README.md.
constexpr std::array<ActionInfo, 13> kActionInfo{{
    {NavAction::kNext, "next", "Next row",
     "Moves the ring down one row.", "Tab, DownArrow"},
    {NavAction::kPrevious, "previous", "Previous row",
     "Moves the ring up one row.", "Shift+Tab, UpArrow"},
    {NavAction::kNextOption, "next_option", "Next option",
     "Moves the ring right, inside the focused row: the next choice, button or slider step.",
     "RightArrow"},
    {NavAction::kPreviousOption, "previous_option", "Previous option",
     "Moves the ring left, inside the focused row: the previous choice, button or slider step.",
     "LeftArrow"},
    {NavAction::kFirst, "first", "First row", "Moves the ring to the first row of the tab.",
     "Home"},
    {NavAction::kLast, "last", "Last row", "Moves the ring to the last row of the tab.", "End"},
    {NavAction::kNextTab, "next_tab", "Next tab",
     "Moves to the next tab, wrapping at the last one.", "PageDown"},
    {NavAction::kPreviousTab, "previous_tab", "Previous tab",
     "Moves to the previous tab, wrapping at the first one.", "PageUp"},
    {NavAction::kActivate, "activate", "Activate",
     "Does what the focused row says it does: picks a value, presses a button, or starts the "
     "Ultimate install.",
     "Enter, KeypadEnter, Space"},
    {NavAction::kCancel, "cancel", "Cancel and quit",
     "Closes whatever is up - a confirmation, a key capture - and leaves the launcher when nothing "
     "is. It is the one binding worth leaving alone.",
     "Escape, B"},
    {NavAction::kLaunch, "launch", "Launch the game",
     "Does what *Launch Game* on the bottom bar does: saves, then starts the game.", "Ctrl+Enter"},
    {NavAction::kSave, "save", "Save",
     "Writes the profile, exactly as the bottom bar's *Save* does.", "Ctrl+S"},
    {NavAction::kCopyEffectiveValue, "copy_effective_value", "Copy the game's value",
     "Does what the precedence badge's *Copy the effective value* button does, for the row the ring "
     "is on: when the game's own rb_blitz.toml decides that row, this takes the value the game will "
     "really use into the launcher's profile.",
     "Ctrl+Shift+C"},
}};

const std::vector<Trigger>& DefaultsFor(NavAction action) {
  static const std::vector<std::vector<Trigger>> defaults = [] {
    std::vector<std::vector<Trigger>> all;
    all.reserve(kActionInfo.size());
    for (const ActionInfo& info : kActionInfo) {
      std::vector<Trigger> triggers;
      std::size_t start = 0;
      const std::string_view text = info.defaults;
      while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        if (const std::optional<Trigger> trigger = ParseTrigger(text.substr(start, end - start))) {
          triggers.push_back(*trigger);
        }
        if (comma == std::string_view::npos) {
          break;
        }
        start = comma + 1;
      }
      all.push_back(std::move(triggers));
    }
    return all;
  }();
  const auto found = std::find_if(kActionInfo.begin(), kActionInfo.end(),
                                  [action](const ActionInfo& info) { return info.action == action; });
  if (found == kActionInfo.end()) {
    static const std::vector<Trigger> none;
    return none;
  }
  return defaults[static_cast<std::size_t>(std::distance(kActionInfo.begin(), found))];
}

const ActionInfo* InfoFor(NavAction action) {
  const auto found = std::find_if(kActionInfo.begin(), kActionInfo.end(),
                                  [action](const ActionInfo& info) { return info.action == action; });
  return found == kActionInfo.end() ? nullptr : &*found;
}

bool HasModifier(const Trigger& trigger) {
  return trigger.ctrl || trigger.shift || trigger.alt;
}

// Nothing when any token is not a key this build can read, which is what makes a value either
// wholly understood or kept verbatim - a half-applied list would silently drop the one key a
// newer launcher meant.
std::optional<std::vector<Trigger>> ParseTriggerList(std::string_view text) {
  std::vector<Trigger> triggers;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t comma = text.find(',', start);
    const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
    const std::optional<Trigger> trigger = ParseTrigger(text.substr(start, end - start));
    if (!trigger) {
      return std::nullopt;
    }
    // The same key twice is one press, not two.
    if (std::find(triggers.begin(), triggers.end(), *trigger) == triggers.end()) {
      triggers.push_back(*trigger);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return triggers;
}

// The file's spelling: what a `[nav]` row holds, which is the name nav_keys.h resolves - so a
// round trip through this module cannot change what a key means.
std::string JoinTriggers(const std::vector<Trigger>& triggers) {
  std::string text;
  for (const Trigger& trigger : triggers) {
    if (!text.empty()) {
      text += ", ";
    }
    text += FormatTrigger(trigger);
  }
  return text;
}

// The panel's spelling: the same chords as the words on the key caps.
std::string JoinTriggerLabels(const std::vector<Trigger>& triggers) {
  std::string text;
  for (const Trigger& trigger : triggers) {
    if (!text.empty()) {
      text += ", ";
    }
    text += TriggerLabel(trigger);
  }
  return text;
}

}  // namespace

bool operator==(const Trigger& left, const Trigger& right) {
  return left.ctrl == right.ctrl && left.shift == right.shift && left.alt == right.alt &&
         NameMatches(left.key, right.key);
}

std::optional<Trigger> ParseTrigger(std::string_view text) {
  const std::string_view trimmed = Trim(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  Trigger trigger;
  std::size_t start = 0;
  while (true) {
    const std::size_t plus = trimmed.find('+', start);
    const std::size_t end = plus == std::string_view::npos ? trimmed.size() : plus;
    const std::string_view token = Trim(trimmed.substr(start, end - start));
    if (plus == std::string_view::npos) {
      // The last token is the key. A chord needs one: "Ctrl" on its own is half a binding.
      if (token.empty()) {
        return std::nullopt;
      }
      trigger.key = std::string(token);
      break;
    }
    const std::optional<Modifier> modifier = ParseModifierToken(token);
    if (!modifier) {
      return std::nullopt;
    }
    switch (*modifier) {
      case Modifier::kCtrl:
        trigger.ctrl = true;
        break;
      case Modifier::kShift:
        trigger.shift = true;
        break;
      case Modifier::kAlt:
        trigger.alt = true;
        break;
    }
    start = plus + 1;
  }
  if (!IsBindableName(trigger.key)) {
    return std::nullopt;
  }
  return trigger;
}

std::string FormatTrigger(const Trigger& trigger) {
  std::string text;
  if (trigger.ctrl) {
    text += "Ctrl+";
  }
  if (trigger.shift) {
    text += "Shift+";
  }
  if (trigger.alt) {
    text += "Alt+";
  }
  text += trigger.key;
  return text;
}

std::string_view KeyLabel(std::string_view name) {
  // Only the keys whose ImGui name is not what the key cap says: the arrows, the two page keys and
  // the numeric keypad's Enter. Everything else - "Tab", "Escape", "Space", "S", "F7" - is already
  // the word a user would use, which is why the table is this short.
  struct Label {
    std::string_view name;
    std::string_view label;
  };
  static constexpr std::array<Label, 7> kLabels{{
      {"DownArrow", "Down"},
      {"UpArrow", "Up"},
      {"LeftArrow", "Left"},
      {"RightArrow", "Right"},
      {"PageUp", "Page Up"},
      {"PageDown", "Page Down"},
      {"KeypadEnter", "Keypad Enter"},
  }};
  for (const Label& entry : kLabels) {
    if (NameMatches(entry.name, name)) {
      return entry.label;
    }
  }
  return name;
}

std::string TriggerLabel(const Trigger& trigger) {
  std::string text;
  if (trigger.ctrl) {
    text += "Ctrl+";
  }
  if (trigger.shift) {
    text += "Shift+";
  }
  if (trigger.alt) {
    text += "Alt+";
  }
  text += KeyLabel(trigger.key);
  return text;
}

bool NameMatches(std::string_view left, std::string_view right) {
  return !left.empty() && Normalize(left) == Normalize(right);
}

bool Holds(const std::vector<std::string>& keys, std::string_view name) {
  return std::any_of(keys.begin(), keys.end(),
                     [name](const std::string& key) { return NameMatches(key, name); });
}

bool IsBindableName(std::string_view name) {
  const std::string normalized = Normalize(name);
  if (normalized.empty() || normalized == "none") {
    return false;
  }
  // A modifier is something held *with* a key: it is parsed as one (ParseTrigger) and never as a
  // key, so a binding that spelled one would never fire. ImGui names the four twice over - the
  // generic "Ctrl" and the physical "LeftCtrl"/"RightCtrl" - and either spelling is still a
  // modifier rather than a key. Getting this wrong is not theoretical: a capture that accepted
  // "LeftCtrl" would bind Ctrl+LeftCtrl when the user pressed Ctrl+S.
  constexpr std::string_view kModifiers[] = {"ctrl", "shift", "alt", "super"};
  for (const std::string_view modifier : kModifiers) {
    if (normalized == modifier || normalized == "left" + std::string(modifier) ||
        normalized == "right" + std::string(modifier) || normalized == "mod" + std::string(modifier)) {
      return false;
    }
  }
  // The pad and the mouse are devices with their own source (D6). ImGui names their keys too, so
  // a name is checked rather than trusted: a binding that matched a pad press would be a pad
  // moving the ring with --no-gamepad on.
  if (StartsWith(normalized, "gamepad") || StartsWith(normalized, "mouse")) {
    return false;
  }
  return true;
}

// The one thing that keeps kActionCount and the table from drifting apart: the tab's block sizes
// its ring off the constant, and a row per action is what it draws.
static_assert(kActionInfo.size() == kActionCount, "kActionCount is not the size of the table");

const std::vector<NavAction>& BindableActions() {  static const std::vector<NavAction> actions = [] {
    std::vector<NavAction> all;
    all.reserve(kActionInfo.size());
    for (const ActionInfo& info : kActionInfo) {
      all.push_back(info.action);
    }
    return all;
  }();
  return actions;
}

std::string_view ActionName(NavAction action) {
  const ActionInfo* info = InfoFor(action);
  return info == nullptr ? std::string_view{} : info->name;
}

std::optional<NavAction> ParseAction(std::string_view name) {
  const std::string_view trimmed = Trim(name);
  for (const ActionInfo& info : kActionInfo) {
    if (trimmed == info.name) {
      return info.action;
    }
  }
  return std::nullopt;
}

std::string_view ActionLabel(NavAction action) {
  const ActionInfo* info = InfoFor(action);
  return info == nullptr ? std::string_view{} : info->label;
}

std::string_view ActionHelp(NavAction action) {
  const ActionInfo* info = InfoFor(action);
  return info == nullptr ? std::string_view{} : info->help;
}

std::string DefaultBindingText(NavAction action) {
  return JoinTriggerLabels(DefaultsFor(action));
}

bool Repeats(NavAction action) {
  switch (action) {
    case NavAction::kNext:
    case NavAction::kPrevious:
    case NavAction::kNextOption:
    case NavAction::kPreviousOption:
    case NavAction::kNextTab:
    case NavAction::kPreviousTab:
      return true;
    default:
      // Everything else is a decision: a held Enter would press the same button over and over,
      // and a held Escape would quit a launcher the user was still reading.
      return false;
  }
}

bool IsBarAction(NavAction action) {
  return action == NavAction::kLaunch || action == NavAction::kSave ||
         action == NavAction::kCopyEffectiveValue;
}

Table::Table() {
  for (const ActionInfo& info : kActionInfo) {
    rows_.push_back(Row{info.action, DefaultsFor(info.action)});
  }
}

Table Table::FromRows(const std::vector<ProfileSetting>& rows) {
  Table table;
  for (const ProfileSetting& row : rows) {
    const std::optional<NavAction> action = ParseAction(row.key);
    const std::optional<std::vector<Trigger>> triggers =
        row.value.empty() ? std::optional<std::vector<Trigger>>{std::vector<Trigger>{}}
                          : ParseTriggerList(row.value);
    if (!action || !triggers) {
      table.unknown_.push_back(row);
      continue;
    }
    table.Set(*action, *triggers);
  }
  return table;
}

std::vector<ProfileSetting> Table::ToRows() const {
  std::vector<ProfileSetting> rows;
  rows.reserve(rows_.size() + unknown_.size());
  for (const ActionInfo& info : kActionInfo) {
    if (IsDefault(info.action)) {
      continue;
    }
    rows.push_back(ProfileSetting{std::string(info.name), JoinTriggers(Triggers(info.action)),
                                  ValueStyle::kBasic});
  }
  for (const ProfileSetting& row : unknown_) {
    rows.push_back(row);
  }
  return rows;
}

const std::vector<Trigger>& Table::Triggers(NavAction action) const {
  const auto found = std::find_if(rows_.begin(), rows_.end(),
                                  [action](const Row& row) { return row.action == action; });
  if (found == rows_.end()) {
    static const std::vector<Trigger> none;
    return none;
  }
  return found->triggers;
}

std::string Table::BindingText(NavAction action) const {
  return JoinTriggerLabels(Triggers(action));
}

bool Table::IsDefault(NavAction action) const {
  const std::vector<Trigger>& defaults = DefaultsFor(action);
  const std::vector<Trigger>& triggers = Triggers(action);
  return triggers.size() == defaults.size() &&
         std::equal(triggers.begin(), triggers.end(), defaults.begin());
}

std::size_t Table::ChangedCount() const {
  std::size_t changed = 0;
  for (const ActionInfo& info : kActionInfo) {
    if (!IsDefault(info.action)) {
      ++changed;
    }
  }
  return changed;
}

void Table::Set(NavAction action, std::vector<Trigger> triggers) {
  for (Row& row : rows_) {
    if (row.action == action) {
      row.triggers = std::move(triggers);
      return;
    }
  }
}

void Table::Reset(NavAction action) { Set(action, DefaultsFor(action)); }

void Table::ResetAll() {
  for (Row& row : rows_) {
    row.triggers = DefaultsFor(row.action);
  }
}

NavAction Resolve(const Table& table, const Presses& presses) {
  const auto down = [&presses](const Trigger& trigger, bool repeat) {
    if (Holds(presses.fresh, trigger.key)) {
      return true;
    }
    return repeat && Holds(presses.repeating, trigger.key);
  };
  const auto modifiers_match = [&presses](const Trigger& trigger) {
    return trigger.ctrl == presses.modifiers.ctrl && trigger.shift == presses.modifiers.shift &&
           trigger.alt == presses.modifiers.alt;
  };

  // A chord first, and matched exactly, so Shift+Tab can mean "back" while Tab means "forward".
  // Then a bare key: the key whatever is held with it, which is how Shift+Down has always moved
  // the ring. Within a pass the action order is the tie-break (BindableActions).
  for (const bool chords : {true, false}) {
    for (const NavAction action : BindableActions()) {
      for (const Trigger& trigger : table.Triggers(action)) {
        if (HasModifier(trigger) != chords) {
          continue;
        }
        if (chords && !modifiers_match(trigger)) {
          continue;
        }
        if (down(trigger, Repeats(action))) {
          return action;
        }
      }
    }
  }
  return NavAction::kNone;
}

}  // namespace rb_blitz::launcher::nav_bindings
