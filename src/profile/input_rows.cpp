// SPDX-License-Identifier: GPL-2.0-only
//
// The `input.*` token grammar. See src/profile/input_rows.h for what a row is.

#include "profile/input_rows.h"

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

}  // namespace

bool IsInputToken(const std::string_view token) {
  return token.starts_with(kPrefix);
}

InputRowToken ParseInputRowToken(const std::string_view token) {
  InputRowToken out;
  if (token == "input.keyboard_support") {
    out.kind = InputRowKind::kKeyboardSupport;
    return out;
  }
  if (token == "input.mouse_support") {
    out.kind = InputRowKind::kMouseSupport;
    return out;
  }
  if (token == "input.custom_controller") {
    out.kind = InputRowKind::kCustomController;
    return out;
  }
  if (token == "input.custom_keyboard_mouse") {
    out.kind = InputRowKind::kCustomKeyboardMouse;
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
    case InputRowKind::kCustomController: return "input.custom_controller";
    case InputRowKind::kCustomKeyboardMouse: return "input.custom_keyboard_mouse";
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

std::string BindingRowLabel(const InputRowToken& token) {  switch (token.kind) {
    case InputRowKind::kGeneralBinding: {
      const auto target = remap::ParseTarget(token.name);
      return target.has_value() ? std::string(remap::TargetLabel(*target)) : token.name;
    }
    case InputRowKind::kControllerBinding:
    case InputRowKind::kKeyboardMouseBinding: {
      const auto action = gameplay_remap::ParseAction(token.name);
      return action.has_value() ? std::string(gameplay_remap::ActionLabel(*action)) : token.name;
    }
    default: return {};
  }
}

}  // namespace rb_blitz::profile
