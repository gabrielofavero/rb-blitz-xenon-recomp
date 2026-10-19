// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the source gates (src/input/source_gates.h). The rules are data in, data out:
// no device is opened here and no guest is called.

#include "input/source_gates.h"

#include <optional>
#include <vector>

namespace rb_blitz::input {
namespace {

using profile::gameplay_remap::Action;
using profile::gameplay_remap::Table;
using profile::remap::Source;
using profile::remap::SourceKind;

}  // namespace

SourceDevice DeviceOf(SourceKind kind) {
  switch (kind) {
    case SourceKind::kPad:
      return SourceDevice::kPad;
    case SourceKind::kKey:
      return SourceDevice::kKeyboard;
    case SourceKind::kMouse:
      return SourceDevice::kMouse;
  }
  return SourceDevice::kPad;
}

uint32_t MouseMaskFor(std::string_view name) {
  if (name == "left") return mouse_button::kLeft;
  if (name == "middle") return mouse_button::kMiddle;
  if (name == "right") return mouse_button::kRight;
  if (name == "x1") return mouse_button::kX1;
  if (name == "x2") return mouse_button::kX2;
  return 0;
}

bool SourceHeld(const PhysicalInput& input, const SourceGates& gates, const Source& source) {
  // An unfocused window plays nothing: the OS gives the keyboard to somebody else and a pad source
  // would otherwise keep firing behind an alt-tab.
  if (!input.window_focused) {
    return false;
  }
  switch (source.kind) {
    case SourceKind::kPad:
      // The physical pad's own state, never the merged one. A `pad:` name is either one of the 16
      // controls or a stick direction; the vocabularies are disjoint, so order cannot matter.
      return profile::remap::PadSourceHeld(input.pad, source.name);
    case SourceKind::kKey:
      return gates.keyboard && input.KeyDown(source.name);
    case SourceKind::kMouse: {
      if (!gates.mouse) {
        return false;
      }
      const uint32_t mask = MouseMaskFor(source.name);
      return mask != 0 && (input.mouse_buttons & mask) != 0;
    }
  }
  return false;
}

const Source* HeldSource(const PhysicalInput& input, const SourceGates& gates, const Table& table,
                         Action action) {
  const std::vector<Source>* sources = table.Effective(action);
  if (sources == nullptr) {
    return nullptr;
  }
  for (const Source& source : *sources) {
    // A source of the other input type never fires this table, whatever path put it there.
    if (!profile::gameplay_remap::AcceptsSource(table.kind(), source)) {
      continue;
    }
    if (SourceHeld(input, gates, source)) {
      return &source;
    }
  }
  return nullptr;
}

bool ActionHeld(const PhysicalInput& input, const SourceGates& gates, const Table& table,
                Action action) {
  return HeldSource(input, gates, table, action) != nullptr;
}

bool TableActive(GameplayRouting routing, profile::gameplay_remap::TableKind kind, bool gameplay) {
  if (!gameplay) {
    return false;
  }
  return kind == profile::gameplay_remap::TableKind::kController ? routing.controller
                                                                 : routing.keyboard_mouse;
}

bool GeneralSourceAllowed(const SourceGates& gates, bool gameplay, bool keyboard_mouse_mapping,
                          bool controller_mapping, SourceKind kind) {
  if (kind == SourceKind::kPad) {
    return false;
  }
  if (kind == SourceKind::kKey && !gates.keyboard) {
    return false;
  }
  if (kind == SourceKind::kMouse && !gates.mouse) {
    return false;
  }
  if (!gameplay) {
    // In a menu the native pointer owns the mouse, and the keyboard drives the UI through General
    // Mapping exactly as it always has.
    return kind == SourceKind::kKey;
  }
  // Playing: while either direct table is on, gameplay input is its business and General
  // Mapping's keyboard/mouse emulation stops at the menu (R4). With both off, General Mapping
  // feeds the native preset as before.
  return !(keyboard_mouse_mapping || controller_mapping);
}

ActionHold::Edge ActionHold::Observe(bool held_now) {
  Edge edge;
  if (!armed) {
    // A boundary crossed while the source was down. Its release arms the hold; while it stays
    // down nothing is pressed, so the new context cannot inherit the old one's key.
    if (!held_now) {
      armed = true;
    }
    return edge;
  }
  if (held_now == held) {
    return edge;
  }
  held = held_now;
  if (held_now) {
    edge.pressed = true;
  } else {
    edge.released = true;
  }
  return edge;
}

uint16_t ControllerOwnedButtons(const Table& table) {
  uint16_t bits = 0;
  for (const Action action : profile::gameplay_remap::Actions()) {
    const std::vector<Source>* sources = table.Effective(action);
    if (sources == nullptr) {
      continue;
    }
    for (const Source& source : *sources) {
      if (source.kind != SourceKind::kPad) {
        continue;
      }
      // A stick direction is not a control (src/profile/remap.h): it has no bit, and the
      // boundary suppression is what stops the preset acting on it.
      const std::optional<profile::remap::Target> target =
          profile::remap::ParseTarget(source.name);
      if (target.has_value()) {
        bits = static_cast<uint16_t>(bits | profile::remap::TargetBit(*target));
      }
    }
  }
  return bits;
}

GameplayPadOverlay GameplayOverlay(bool controller_active, bool keyboard_mouse_active,
                                   bool pause_held, uint16_t controller_buttons) {
  GameplayPadOverlay overlay;
  if (controller_active) {
    // The preset is bypassed, so its controls must not reach the guest as pad input either -
    // and Start is the pause path, which is part of what "bypassed" covers.
    overlay.clear_buttons =
        static_cast<uint16_t>(controller_buttons | profile::remap::TargetBit(
                                                        profile::remap::Target::kStart));
  }
  if ((controller_active || keyboard_mouse_active) && pause_held) {
    overlay.set_buttons = profile::remap::TargetBit(profile::remap::Target::kStart);
  }
  return overlay;
}

void ActionEdges::Observe(Action action, bool held_now) {
  Slot& slot = slots_[kIndex(action)];
  bool& held = held_[kIndex(action)];
  if (held_now == held) {
    return;
  }
  held = held_now;
  if (held_now) {
    slot.press = true;
  } else {
    slot.release = true;
  }
}

std::size_t ActionEdges::Drain(Action action, bool* out, std::size_t capacity) {
  Slot& slot = slots_[kIndex(action)];
  std::size_t written = 0;
  // Press first: a tap queued as press+release must reach the guest in that order.
  if (slot.press && written < capacity) {
    out[written++] = true;
    slot.press = false;
  }
  if (slot.release && written < capacity) {
    out[written++] = false;
    slot.release = false;
  }
  return written;
}

bool ActionEdges::Pending(Action action) const {
  const Slot& slot = slots_[kIndex(action)];
  return slot.press || slot.release;
}

bool ActionEdges::Held(Action action) const { return held_[kIndex(action)]; }

void ActionEdges::Clear() {
  slots_.fill(Slot{});
  held_.fill(false);
}

// ---------------------------------------------------------------------------------------------
// P8: the Controls recovery path (plan §14.4/§17.2).
// ---------------------------------------------------------------------------------------------

bool RecoveryNeeded(const SourceGates& gates, const bool pad_connected) {
  return !gates.keyboard && !gates.mouse && !pad_connected;
}

bool RecoveryHold::Observe(const bool needed, const bool key_down,
                           const std::chrono::steady_clock::time_point now) {
  if (!needed) {
    // The route exists (or exists again): nothing to recover, and the next need starts over.
    holding_ = false;
    done_ = false;
    was_down_ = key_down;
    return false;
  }
  const bool rising = key_down && !was_down_;
  was_down_ = key_down;
  if (!key_down) {
    // Any release starts over: a fresh attempt needs a fresh hold, and a recovery that fired
    // cannot fire again until the key comes up.
    holding_ = false;
    done_ = false;
    return false;
  }
  if (rising) {
    holding_ = true;
    since_ = now;
    return false;
  }
  if (holding_ && !done_ && now - since_ >= kRecoveryHold) {
    done_ = true;
    return true;
  }
  return false;
}

RecoveryOutcome RecoverKeyboard(settings::InputBindingStore* store, std::string* why) {
  if (store == nullptr) {
    if (why != nullptr) {
      *why = "the Controls store is not installed in this build";
    }
    return RecoveryOutcome::kNoStore;
  }
  std::string error;
  // The token is the row grammar's own spelling (src/profile/input_rows.h): the recovery edit is
  // the same row the Controls menu's Keyboard switch writes, so a boot can tell them apart only by
  // its log line, never by where the value landed.
  if (!store->StageBool("input.keyboard_support", true, &error)) {
    if (why != nullptr) {
      *why = error.empty() ? "the store refused the switch" : error;
    }
    return RecoveryOutcome::kRefused;
  }
  if (!store->Commit(&error)) {
    if (why != nullptr) {
      *why = error.empty() ? "the save failed" : error;
    }
    return RecoveryOutcome::kSaveFailed;
  }
  return RecoveryOutcome::kRearmed;
}

}  // namespace rb_blitz::input
