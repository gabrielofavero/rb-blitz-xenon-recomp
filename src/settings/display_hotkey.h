// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <rex/input_keys.h>

namespace rb_blitz::settings {

// SDL maps both Enter keys to kReturn. Consume the complete chord, including
// repeats and release after Alt went up, before it can activate a guest menu.
class DisplayHotkey {
 public:
  enum class Action { kNone, kToggle, kTrial };
  void Reset() { enter_held_ = trial_held_ = false; }

  Action Handle(rex::InputEvent& event) {
    using Type = rex::InputEvent::Type;
    if (event.type == Type::kFocusLost || event.type == Type::kFocusGained) {
      Reset();
      return Action::kNone;
    }
    if (event.type != Type::kKeyDown && event.type != Type::kKeyUp) return Action::kNone;
    bool* held = nullptr;
    Action action = Action::kNone;
    const auto modifiers = static_cast<unsigned>(event.modifiers);
    const auto alt = static_cast<unsigned>(rex::KeyModifiers::kAlt);
    const auto ctrl = static_cast<unsigned>(rex::KeyModifiers::kCtrl);
    if (event.key == rex::VirtualKey::kReturn) {
      held = &enter_held_;
      if (modifiers & alt) action = Action::kToggle;
    } else if (event.key == rex::VirtualKey::kF11) {
      held = &trial_held_;
      if ((modifiers & (alt | ctrl)) == (alt | ctrl)) action = Action::kTrial;
    } else return Action::kNone;
    if (*held) {
      event.handled = true;
      if (event.type == Type::kKeyUp) *held = false;
      return Action::kNone;
    }
    if (event.type == Type::kKeyDown && action != Action::kNone) {
      event.handled = true;
      *held = true;
      return event.repeat ? Action::kNone : action;
    }
    return Action::kNone;
  }

  bool ShouldToggle(rex::InputEvent& event) { return Handle(event) == Action::kToggle; }

 private:
  bool enter_held_ = false;
  bool trial_held_ = false;
};

}  // namespace rb_blitz::settings
