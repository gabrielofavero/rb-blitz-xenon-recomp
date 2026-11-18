// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <rex/hid/input_system.h>
#include <rex/hid/title_input.h>

namespace rb_blitz::input {
struct ExtraButtons {
  uint8_t available = 0;
  uint8_t held = 0;
};

// Configure after HID initialization and clear after guest input workers stop.
// The HID module owns the service; no private driver pointers are retained.
void ConfigureTitleInput(rex::hid::ITitleInput* input);
bool SystemUIActive();
bool SamplePhysicalPad(rex::hid::IInputSystem::PadReading* out_state,
                       bool for_host_ui = false, ExtraButtons* extras = nullptr);
}  // namespace rb_blitz::input
