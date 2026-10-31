// SPDX-License-Identifier: GPL-2.0-only
//
// The `input.*` token grammar. See src/profile/input_rows.h for what a row is.

#include "profile/input_rows.h"

#include <map>
#include "profile/music_bindings.h"

namespace rb_blitz::profile {
namespace {

constexpr std::string_view kPrefix = "input.";
constexpr std::string_view kGeneralPrefix = "input.general.";
constexpr std::string_view kControllerPrefix = "input.controller.";
constexpr std::string_view kKeyboardMousePrefix = "input.keyboard_mouse.";

std::string Join(std::string_view prefix, std::string_view name) {
  std::string out(prefix);
  out += name;
  return out;
}

// Case-folded copy for the display-name tables: a profile written by hand may spell a key in any
// case, and the row must read the same either way.
std::string LowerAscii(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  return out;
}

// The pad controls and stick directions as a row draws them. One line, so the short spellings a
// pad itself prints win over the settings label's prose ("LB", "L3", "D-pad Right").
std::string PadDisplayName(std::string_view name) {
  static const std::map<std::string_view, std::string_view, std::less<>> kNames = {
      {"a", "A"},
      {"b", "B"},
      {"x", "X"},
      {"y", "Y"},
      {"left_shoulder", "LB"},
      {"right_shoulder", "RB"},
      {"left_trigger", "LT"},
      {"right_trigger", "RT"},
      {"back", "Back"},
      {"start", "Start"},
      {"left_thumb", "L3"},
      {"right_thumb", "R3"},
      {"dpad_up", "D-pad Up"},
      {"dpad_down", "D-pad Down"},
      {"dpad_left", "D-pad Left"},
      {"dpad_right", "D-pad Right"},
      {"lstick_up", "L-Stick Up"},
      {"lstick_down", "L-Stick Down"},
      {"lstick_left", "L-Stick Left"},
      {"lstick_right", "L-Stick Right"},
      {"rstick_up", "R-Stick Up"},
      {"rstick_down", "R-Stick Down"},
      {"rstick_left", "R-Stick Left"},
      {"rstick_right", "R-Stick Right"},
  };
  const auto found = kNames.find(name);
  return found == kNames.end() ? std::string(name) : std::string(found->second);
}

// The keys a row shows with their own word: SDL's scancode names are already readable ("F1",
// "Semicolon"), but the names that collide with a direction or a menu action read better spelled
// out ("Right" -> "Arrow Right", "Return" -> "Enter").
std::string KeyDisplayName(std::string_view name) {
  static const std::map<std::string, std::string> kNames = {
      {"return", "Enter"},      {"enter", "Enter"},       {"escape", "Escape"},
      {"backspace", "Backspace"}, {"space", "Space"},     {"tab", "Tab"},
      {"delete", "Delete"},     {"insert", "Insert"},     {"home", "Home"},
      {"end", "End"},           {"pageup", "Page Up"},    {"pagedown", "Page Down"},
      {"up", "Arrow Up"},       {"down", "Arrow Down"},   {"left", "Arrow Left"},
      {"right", "Arrow Right"}, {"left shift", "L Shift"}, {"right shift", "R Shift"},
      {"left ctrl", "L Ctrl"},  {"right ctrl", "R Ctrl"}, {"left alt", "L Alt"},
      {"right alt", "R Alt"},
      {"mediatracknext", "Media Next"}, {"mediatrackprevious", "Media Prev"},
      {"mediaplaypause", "Media Play"},
  };
  const std::string lower = LowerAscii(name);
  if (const auto found = kNames.find(lower); found != kNames.end()) {
    return found->second;
  }
  // A letter or digit key: one upper-case character reads better in a row.
  if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') {
    return std::string(1, static_cast<char>(name[0] - 'a' + 'A'));
  }
  return std::string(name);
}

std::string MouseDisplayName(std::string_view name) {
  if (name == "left") return "Left Click";
  if (name == "right") return "Right Click";
  if (name == "middle") return "Middle Click";
  if (name == "x1") return "Mouse 4";
  if (name == "x2") return "Mouse 5";
  if (name == remap::kMouseWheelUp) return "Scroll Up";
  if (name == remap::kMouseWheelDown) return "Scroll Down";
  return std::string(name);
}

}  // namespace

bool IsInputToken(const std::string_view token) {
  return token.starts_with(kPrefix);
}

InputRowToken ParseInputRowToken(const std::string_view token) {
  InputRowToken out;
  if (token == "input.reset.music") return {InputRowKind::kResetMusic, {}};
  if (token.starts_with("input.music.")) {
    const auto name = token.substr(12);
    if (music::ActionIndex(name)) return {InputRowKind::kMusicBinding, std::string(name)};
    return {};
  }
  if (token == "input.keyboard_support") {
    out.kind = InputRowKind::kKeyboardSupport;
    return out;
  }
  if (token == "input.mouse_support") {
    out.kind = InputRowKind::kMouseSupport;
    return out;
  }
  if (token == "input.reset.general") {
    out.kind = InputRowKind::kResetGeneral;
    return out;
  }
  if (token == "input.reset.controller") {
    out.kind = InputRowKind::kResetController;
    return out;
  }
  if (token == "input.reset.keyboard_mouse") {
    out.kind = InputRowKind::kResetKeyboardMouse;
    return out;
  }
  if (token.starts_with(kGeneralPrefix)) {
    const std::string_view name = token.substr(kGeneralPrefix.size());
    if (const auto target = remap::ParseTarget(name); target.has_value()) {
      out.kind = InputRowKind::kGeneralBinding;
      out.name = std::string(name);
    }
    return out;
  }
  if (token.starts_with(kControllerPrefix)) {
    const std::string_view name = token.substr(kControllerPrefix.size());
    if (const auto action = gameplay_remap::ParseAction(name); action.has_value()) {
      out.kind = InputRowKind::kControllerBinding;
      out.name = std::string(name);
    }
    return out;
  }
  if (token.starts_with(kKeyboardMousePrefix)) {
    const std::string_view name = token.substr(kKeyboardMousePrefix.size());
    if (const auto action = gameplay_remap::ParseAction(name); action.has_value()) {
      out.kind = InputRowKind::kKeyboardMouseBinding;
      out.name = std::string(name);
    }
    return out;
  }
  return out;
}

std::string_view InputSwitchToken(const InputRowKind kind) {
  switch (kind) {
    case InputRowKind::kKeyboardSupport: return "input.keyboard_support";
    case InputRowKind::kMouseSupport: return "input.mouse_support";
    default: return {};
  }
}

std::string GeneralBindingToken(const remap::Target target) {
  return Join(kGeneralPrefix, remap::TargetName(target));
}

std::string GameplayBindingToken(const gameplay_remap::TableKind kind,
                                 const gameplay_remap::Action action) {
  return Join(kind == gameplay_remap::TableKind::kController ? kControllerPrefix
                                                             : kKeyboardMousePrefix,
              gameplay_remap::ActionName(action));
}

std::string ResetAllToken(const InputRowKind kind) {
  switch (kind) {
    case InputRowKind::kResetGeneral: return "input.reset.general";
    case InputRowKind::kResetController: return "input.reset.controller";
    case InputRowKind::kResetKeyboardMouse: return "input.reset.keyboard_mouse";
    default: return {};
  }
}

std::optional<gameplay_remap::TableKind> BindingTable(const InputRowKind kind) {
  switch (kind) {
    case InputRowKind::kControllerBinding: return gameplay_remap::TableKind::kController;
    case InputRowKind::kKeyboardMouseBinding:
      return gameplay_remap::TableKind::kKeyboardMouse;
    default: return std::nullopt;
  }
}

std::string InputRowSymbol(const std::string_view token) {
  // The canonical symbol drops the wire namespace (`input.`), so `input.general.a` becomes
  // `settings_bind_general_a` - the key the locale and the caption event use.
  std::string_view name = token;
  if (name.starts_with(kPrefix)) {
    name = name.substr(kPrefix.size());
  }
  std::string out = "settings_bind_";
  for (const char c : name) {
    if (c == '.') {
      out.push_back('_');
    } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out;
}

InputRowToken ParseInputRowSymbol(const std::string_view symbol) {
  for (const auto name : music::kActions) {
    const std::string token = "input.music." + std::string(name);
    if (InputRowSymbol(token) == symbol) return ParseInputRowToken(token);
  }
  for (const remap::Target target : remap::Targets()) {
    if (InputRowSymbol(GeneralBindingToken(target)) == symbol) {
      InputRowToken out;
      out.kind = InputRowKind::kGeneralBinding;
      out.name = std::string(remap::TargetName(target));
      return out;
    }
  }
  for (const gameplay_remap::TableKind kind : gameplay_remap::TableKinds()) {
    for (const gameplay_remap::Action action : gameplay_remap::Actions()) {
      if (InputRowSymbol(GameplayBindingToken(kind, action)) == symbol) {
        InputRowToken out;
        out.kind = kind == gameplay_remap::TableKind::kController
                       ? InputRowKind::kControllerBinding
                       : InputRowKind::kKeyboardMouseBinding;
        out.name = std::string(gameplay_remap::ActionName(action));
        return out;
      }
    }
  }
  return {};
}

std::string InterfaceTargetLabel(const remap::Target target) {
  // D1's vocabulary. The three music-library names say what the song list does with the button
  // rather than naming the pad's button, and the two face buttons read as the menu actions they
  // perform - `Confirm`/`Go Back` is also what keeps a player from confusing B with Back/Select.
  switch (target) {
    case remap::Target::kA: return "Confirm";
    case remap::Target::kB: return "Go Back";
    case remap::Target::kY: return "Shortcuts";
    case remap::Target::kBack: return "Sort";
    case remap::Target::kStart: return "Search";
    case remap::Target::kDpadUp: return "Up";
    case remap::Target::kDpadDown: return "Down";
    case remap::Target::kDpadLeft: return "Left";
    case remap::Target::kDpadRight: return "Right";
    case remap::Target::kX:
    case remap::Target::kLeftShoulder:
    case remap::Target::kRightShoulder:
    case remap::Target::kLeftTrigger:
    case remap::Target::kRightTrigger:
    case remap::Target::kLeftThumb:
    case remap::Target::kRightThumb:
      break;
  }
  return std::string(remap::TargetLabel(target));
}

std::string BindingRowLabel(const InputRowToken& token) {  switch (token.kind) {
    case InputRowKind::kMusicBinding: return music::Label(token.name);
    case InputRowKind::kGeneralBinding: {
      const auto target = remap::ParseTarget(token.name);
      return target.has_value() ? InterfaceTargetLabel(*target) : token.name;
    }
    case InputRowKind::kControllerBinding:
    case InputRowKind::kKeyboardMouseBinding: {
      const auto action = gameplay_remap::ParseAction(token.name);
      return action.has_value() ? std::string(gameplay_remap::ActionLabel(*action)) : token.name;
    }
    default: return {};
  }
}

std::string SourceDisplayName(const remap::Source& source) {
  switch (source.kind) {
    case remap::SourceKind::kPad: return PadDisplayName(source.name);
    case remap::SourceKind::kKey: return KeyDisplayName(source.name);
    case remap::SourceKind::kMouse: return MouseDisplayName(source.name);
    case remap::SourceKind::kSdl:
      return std::string(source.name.starts_with("button:") ?
          source.name.substr(7) : source.name);
  }
  return std::string(source.name);
}

std::string BindingListText(const std::vector<remap::Source>& sources) {
  std::string text;
  for (const remap::Source& source : sources) {
    if (!text.empty()) {
      text += ", ";
    }
    text += SourceDisplayName(source);
  }
  return text;
}

std::string BindingRowText(const InputRowToken& token,
                           const std::vector<remap::Source>* sources) {
  const std::string label = BindingRowLabel(token);
  if (label.empty()) {
    return {};
  }
  std::string text = label + ": ";
  if (sources == nullptr || sources->empty()) {
    return text + "(no inputs)";
  }
  return text + BindingListText(*sources);
}

}  // namespace rb_blitz::profile
