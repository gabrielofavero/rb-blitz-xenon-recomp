// SPDX-License-Identifier: GPL-2.0-only
//
// Implementation of src/input/host_pad.h.

#include "input/host_pad.h"

#include <mutex>

namespace rb_blitz::input {
namespace {

std::mutex& Mutex() {
  static std::mutex mutex;
  return mutex;
}

uint16_t& Bits() {
  static uint16_t bits = 0;
  return bits;
}

int& Frames() {
  static int frames = 0;
  return frames;
}

}  // namespace

void PulseHostPad(const uint16_t bits) {
  std::lock_guard lock(Mutex());
  Bits() = bits;
  Frames() = bits != 0 ? 2 : 0;
}

uint16_t TakeHostPadPulse() {
  std::lock_guard lock(Mutex());
  if (Frames() <= 0) {
    Bits() = 0;
    return 0;
  }
  --Frames();
  const uint16_t bits = Bits();
  if (Frames() == 0) {
    Bits() = 0;
  }
  return bits;
}

}  // namespace rb_blitz::input
