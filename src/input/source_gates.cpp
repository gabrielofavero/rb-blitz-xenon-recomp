// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the source gates (src/input/source_gates.h). The rules are data in, data out:
// no device is opened here and no guest is called.

#include "input/source_gates.h"

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

}  // namespace rb_blitz::input
