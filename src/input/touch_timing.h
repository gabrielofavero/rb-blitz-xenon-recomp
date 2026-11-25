// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::input::touch {
// Input older than this belongs to a stall, not a new judgement at resume.
inline constexpr uint64_t kMaxInputAgeNs = 250'000'000;
inline uint64_t InputAgeNs(uint64_t timestamp, uint64_t now) {
  return timestamp && now > timestamp ? now - timestamp : 0;
}
// Both guest song judgement and calibration clocks use milliseconds. Preserve
// their native latency offsets; remove only time spent waiting on the host.
inline double EventTimeMs(double guest_ms, uint64_t timestamp, uint64_t now) {
  return guest_ms - double(InputAgeNs(timestamp, now)) * 1e-6;
}
}  // namespace rb_blitz::input::touch
