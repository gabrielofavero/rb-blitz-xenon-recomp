// SPDX-License-Identifier: GPL-2.0-only
#include "input/title_input.h"

#include <atomic>

namespace rb_blitz::input {
namespace {
std::atomic<rex::hid::ITitleInput*> g_input{nullptr};
}
void ConfigureTitleInput(rex::hid::ITitleInput* input) {
  g_input.store(input, std::memory_order_release);
}
bool SystemUIActive() {
  auto* input = g_input.load(std::memory_order_acquire);
  return input && input->IsUIBlocked();
}
bool SamplePhysicalPad(rex::hid::IInputSystem::PadReading* out_state,
                       bool for_host_ui, ExtraButtons* extras) {
  if (out_state) *out_state = {};
  if (extras) *extras = {};
  auto* input = g_input.load(std::memory_order_acquire);
  if (!input) return false;
  auto state = input->GetPhysicalState(for_host_ui);
  if (!state) return false;
  if (out_state) out_state->state = *state;
  if (extras) {
    const auto buttons = input->GetPhysicalExtraButtons(for_host_ui);
    extras->available = buttons.available;
    extras->held = buttons.held;
  }
  return true;
}
}  // namespace rb_blitz::input
