// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the ImGui half of the launcher's own keys (launcher/src/nav_keys.h, A5).

#include "nav_keys.h"

namespace rb_blitz::launcher::nav_keys {
namespace {

// ImGui's own key table is the vocabulary: every named key, which is what makes "any key" bindable
// rather than the thirteen the launcher used to read. The walk is over the enum's named range; the
// aliases in it (the Mod keys, and the legacy names for keys that have one) are either rejected by
// IsBindableName or resolve to the same name twice, which the model treats as one press.
template <typename Fn>
void ForEachNamedKey(Fn&& visit) {
  for (int value = ImGuiKey_NamedKey_BEGIN; value < ImGuiKey_NamedKey_END; ++value) {
    const ImGuiKey key = static_cast<ImGuiKey>(value);
    const char* name = ImGui::GetKeyName(key);
    if (name == nullptr || !nav_bindings::IsBindableName(name)) {
      continue;
    }
    if (!visit(key, name)) {
      return;
    }
  }
}

}  // namespace

ImGuiKey KeyForName(std::string_view name) {
  ImGuiKey found = ImGuiKey_None;
  ForEachNamedKey([&](ImGuiKey key, const char* key_name) {
    if (!nav_bindings::NameMatches(name, key_name)) {
      return true;
    }
    found = key;
    return false;
  });
  return found;
}

std::string NameForKey(ImGuiKey key) {
  if (key == ImGuiKey_None) {
    return {};
  }
  return ImGui::GetKeyName(key);
}

nav_bindings::Presses ReadPresses() {
  const ImGuiIO& io = ImGui::GetIO();
  nav_bindings::Presses presses;
  presses.modifiers.ctrl = io.KeyCtrl;
  presses.modifiers.shift = io.KeyShift;
  presses.modifiers.alt = io.KeyAlt;
  ForEachNamedKey([&](ImGuiKey key, const char* name) {
    // Two questions, because they mean two things: a fresh press, and a press this frame that
    // includes the repeat of a key held down. Which of the two an action may answer to is the
    // action's own rule (nav_bindings::Repeats), not the key's.
    if (ImGui::IsKeyPressed(key, false)) {
      presses.fresh.emplace_back(name);
    }
    if (ImGui::IsKeyPressed(key, true)) {
      presses.repeating.emplace_back(name);
    }
    return true;
  });
  return presses;
}

std::optional<nav_bindings::Trigger> CapturedTrigger() {
  const ImGuiIO& io = ImGui::GetIO();
  std::optional<nav_bindings::Trigger> captured;
  ForEachNamedKey([&](ImGuiKey key, const char* name) {
    if (!ImGui::IsKeyPressed(key, false)) {
      return true;
    }
    nav_bindings::Trigger trigger;
    trigger.key = name;
    trigger.ctrl = io.KeyCtrl;
    trigger.shift = io.KeyShift;
    trigger.alt = io.KeyAlt;
    captured = std::move(trigger);
    return false;
  });
  return captured;
}

}  // namespace rb_blitz::launcher::nav_keys
