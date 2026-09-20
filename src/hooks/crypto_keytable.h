// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Pure host logic of the MOGG key path (B-009): which slot a guest key id names,
// which guest buffers are entries of the obscured .data key table, and what
// those entries de-obfuscate to. Deliberately free of SDK headers and of runtime
// state so it can be unit-tested without booting the game
// (tests/crypto_keytable_tests.cpp); src/hooks/crypto.cpp is the only production
// consumer and holds the evidence, the guest addresses and the two hooks.
//
// See docs/bringup-log.md B-009 and docs/rb3-references.md §5.

#pragma once

#include <cstdint>

namespace rb_blitz::crypto {

// Guest .data table holding the four obscured AES-128 key entries, one per MOGG
// version class.
inline constexpr uint32_t kKeysetTableAddress = 0x8280C568;
inline constexpr uint32_t kKeysetTableSize = 0x40;

// The guest wrapper biases every key index by 0xE0 and rejects index >= 8.
inline constexpr uint32_t kKeyIdBias = 0xE0;
inline constexpr uint32_t kKeySlots = 8;

inline constexpr uint32_t kKeySize = 0x10;  // AES-128

// How many keys the obscured table holds (four); the guest key id does *not*
// select one of these, the buffer offset does.
inline constexpr uint32_t kKeysetEntryCount = kKeysetTableSize / kKeySize;

// De-obfuscated key material: what XeKeysSetKey would install after the console
// applied KEY_OBFUSCATION_KEY to the table above. Identical to the RB3 "green"
// keyset / band3_recomp NewKeyset. The obscured bytes it replaces live in the
// game image, which is not committed, so this is the only form the keyset can
// take in this repository - and the reason it is pinned by a test.
inline constexpr uint8_t kPlaintextKeyTable[kKeysetTableSize] = {
    0x01, 0x22, 0x00, 0x38, 0xD2, 0x01, 0x78, 0x8B, 0xDD, 0xCD, 0xD0, 0xF0, 0xFE, 0x3E, 0x24, 0x7F,
    0x51, 0x73, 0xAD, 0xE5, 0xB3, 0x99, 0xB8, 0x61, 0x58, 0x1A, 0xF9, 0xB8, 0x1E, 0xA7, 0xBE, 0xBF,
    0xC6, 0x22, 0x94, 0x30, 0xD8, 0x3C, 0x84, 0x14, 0x08, 0x73, 0x7C, 0xF2, 0x23, 0xF6, 0xEB, 0x5A,
    0x02, 0x1A, 0x83, 0xF3, 0x97, 0xE9, 0xD4, 0xB8, 0x06, 0x74, 0x14, 0x6B, 0x30, 0x4C, 0x00, 0x91};

// Where a key id installs. `clamped` is set when the id names no slot of its own
// and slot 0 stands in for it; the caller reports that once.
struct KeySlotSelection {
  uint32_t slot = 0;
  bool clamped = false;
};

constexpr KeySlotSelection SelectKeySlot(uint32_t key_id) {
  const uint32_t biased = (key_id >= kKeyIdBias) ? (key_id - kKeyIdBias) : key_id;
  if (biased >= kKeySlots) {
    return {0, true};
  }
  return {biased, false};
}

// True when the guest key buffer points into the obscured table with a whole key
// left to read, i.e. is one of the four entries the guest computes as
// `0x8280C568 + GetEncMethod(version) * 16`.
constexpr bool IsKeysetTableAddress(uint32_t guest_address) {
  if (guest_address < kKeysetTableAddress) {
    return false;
  }
  return (guest_address - kKeysetTableAddress) + kKeySize <= kKeysetTableSize;
}

// The plaintext key the obscured entry at `table_offset` de-obfuscates to. The
// guest carries the MOGG version in this offset - ByteGrinder::HvDecrypt installs
// with a constant key id and the buffer `&table[GetEncMethod(version) * 16]` - so
// this, not the entry matching the key id, is the key that must land in the slot.
constexpr const uint8_t* PlaintextKeyAtOffset(uint32_t table_offset) {
  return kPlaintextKeyTable + table_offset;
}

}  // namespace rb_blitz::crypto
