// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cstdint>

namespace rb_blitz::audio_stream {
inline constexpr uint32_t kBlockBytes = 16384;
inline constexpr uint32_t kReadBytes = 128 * 1024;
inline constexpr uint32_t kReadBufferCaller = 0x827687A0;
inline constexpr uint32_t kReadFileCaller = 0x827687C4;
inline constexpr uint32_t kArkReadAsync = 0x82384C88;

inline bool ValidRing(uint32_t blocks, uint32_t writer, uint32_t cursor) {
  return blocks >= 4 && blocks <= 1024 && !(blocks & 1) && writer < blocks &&
         cursor < blocks * kBlockBytes;
}

// Retail checks only distance == half the ring. If a long frame skips that
// block, the copy must still happen while the cursor is beyond that threshold.
inline bool MissedRefill(uint32_t blocks, uint32_t writer, uint32_t cursor) {
  return ValidRing(blocks, writer, cursor) &&
         (cursor / kBlockBytes + blocks - writer) % blocks > blocks / 2;
}

inline bool NeedsDecodedAudio(uint32_t blocks, uint32_t written, uint32_t cursor,
                              uint32_t sample_rate) {
  if (!sample_rate || !ValidRing(blocks, written % std::max(blocks, 1u), cursor)) return false;
  const uint32_t capacity = blocks * kBlockBytes;
  const uint32_t ahead = (written % blocks * kBlockBytes + capacity - cursor) % capacity;
  // Half a second of mono int16 PCM, bounded by half the physical ring.
  return ahead < std::min(capacity / 2, sample_rate);
}

// Couple the larger read to the exact buffer reserved for it. A different
// reader, file implementation or call site retains the original request.
struct PendingRead {
  uint32_t buffer = 0;
  uint32_t Consume(uint32_t caller, uint32_t destination, uint32_t requested) {
    if (caller != kReadFileCaller) return requested;
    const bool matches = buffer && destination == buffer && requested == kBlockBytes;
    buffer = 0;
    return matches ? kReadBytes : requested;
  }
};
}  // namespace rb_blitz::audio_stream
