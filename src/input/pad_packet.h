// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::input {

struct PadSample {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0, right_trigger = 0;
  int16_t thumb_lx = 0, thumb_ly = 0, thumb_rx = 0, thumb_ry = 0;
  bool operator==(const PadSample&) const = default;
};

// The guest skips input with an unchanged packet number. Device counters cannot
// describe a merged/remapped pad: an idle physical pad's larger counter can hide
// every mouse press, and keyboard remapping happens after device merging too.
class PadPacket {
 public:
  uint32_t Observe(const PadSample& sample) {
    if (!initialized_ || sample != last_) {
      ++number_;
      last_ = sample;
      initialized_ = true;
    }
    return number_;
  }
 private:
  PadSample last_;
  uint32_t number_ = 0;
  bool initialized_ = false;
};

}  // namespace rb_blitz::input
