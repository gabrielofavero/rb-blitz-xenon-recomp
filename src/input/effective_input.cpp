// SPDX-License-Identifier: GPL-2.0-only
//
// Implementation of src/input/effective_input.h.

#include "input/effective_input.h"

#include <atomic>
#include <mutex>

namespace rb_blitz::input {
namespace {

std::mutex& Mutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<const EffectiveInput>& Slot() {
  static std::shared_ptr<const EffectiveInput> slot = std::make_shared<EffectiveInput>();
  return slot;
}

// -1 = no song has created a controller yet, which reads as preset 0 (the compiled default).
std::atomic<int> g_controller_preset{-1};

}  // namespace

std::shared_ptr<const EffectiveInput> CurrentInput() {
  std::lock_guard lock(Mutex());
  return Slot();
}

void PublishInput(std::shared_ptr<EffectiveInput> input) {
  if (!input) {
    return;
  }
  std::lock_guard lock(Mutex());
  Slot() = std::move(input);
}

std::shared_ptr<EffectiveInput> CopyCurrentInput() {
  std::lock_guard lock(Mutex());
  return std::make_shared<EffectiveInput>(*Slot());
}

void ObserveControllerPreset(const int preset) {
  if (preset < 0 || preset >= profile::gameplay_remap::kControllerPresetCount) {
    return;
  }
  if (g_controller_preset.exchange(preset) == preset) {
    return;
  }
  // Re-stamp what is published now, so the poll after a preset change already evaluates the new
  // preset's sources. A later store publish stamps the same value again.
  std::shared_ptr<EffectiveInput> next = CopyCurrentInput();
  next->controller.SetControllerPreset(preset);
  PublishInput(std::move(next));
}

int ObservedControllerPreset() { return g_controller_preset.load(std::memory_order_relaxed); }

}  // namespace rb_blitz::input
