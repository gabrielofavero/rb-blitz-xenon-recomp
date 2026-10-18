// SPDX-License-Identifier: GPL-2.0-only
//
// Implementation of src/input/effective_input.h.

#include "input/effective_input.h"

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

}  // namespace rb_blitz::input
