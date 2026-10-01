// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the B-009 MOGG key path (src/hooks/crypto_keytable.h). No SDK,
// no game image, no boot: the logic under test is pure host code, so this runs in
// milliseconds. It is the oldest of the project's host test targets (docs/backlog.md
// §1); what the two import hooks decide - where a key comes from, and which key an
// AES-CBC call runs under - is covered here too, so only the kernel calls themselves
// still need a boot.
//
// The plaintext keyset itself is intentionally not copied here - it would be a
// second vendored copy of external key material (docs/rb3-references.md §9), and
// no in-repo source can independently confirm those bytes. What is pinned instead
// is the structure the bug depended on: four distinct entries, the guest's
// version -> entry mapping, and the fact that installing by key id would select a
// different key than installing by buffer offset.

#include "check.h"

#include "hooks/crypto_keytable.h"

#include <cstring>

namespace {

using namespace rb_blitz;
using namespace rb_blitz::crypto;
using namespace rb_blitz::test;

static_assert(kKeysetEntryCount == 4, "the obscured table must hold four keys");
static_assert(kKeysetTableSize == kKeysetEntryCount * kKeySize, "table must be key-sized entries");

// 0x823DE070 ByteGrinder::GetEncMethod, reimplemented from the disassembly so the
// tests state the guest contract rather than restate a header constant.
constexpr uint32_t EncMethodForVersion(uint32_t version) {
  switch (version) {
    case 12:
    case 13:
      return 0;
    case 14:
      return 1;
    case 15:
      return 2;
    case 16:
      return 3;
    default:
      return 0;
  }
}

// What the guest does for a MOGG stream: HvDecrypt installs with the constant key
// id 0 and the buffer `0x8280C568 + GetEncMethod(version) * 16`, so the version
// travels in the pointer, not in the key id.
constexpr uint32_t InstallBufferForVersion(uint32_t version) {
  return kKeysetTableAddress + EncMethodForVersion(version) * kKeySize;
}

// What the host hook installs for that call.
constexpr const uint8_t* KeyInstalledForVersion(uint32_t version) {
  return PlaintextKeyAtOffset(InstallBufferForVersion(version) - kKeysetTableAddress);
}

void TestSlotFromBiasedKeyId() {
  BeginCase("the guest wrapper's 0xE0 bias maps key ids 0xE0..0xE7 onto slots 0..7");
  for (uint32_t slot = 0; slot < kKeySlots; ++slot) {
    const KeySlotSelection selection = SelectKeySlot(kKeyIdBias + slot);
    CHECK_EQ(selection.slot, slot);
    CHECK_FALSE(selection.clamped);
  }
}

void TestSlotFromBareKeyId() {
  BeginCase("bare key ids 0..7 name the same slots");
  for (uint32_t slot = 0; slot < kKeySlots; ++slot) {
    const KeySlotSelection selection = SelectKeySlot(slot);
    CHECK_EQ(selection.slot, slot);
    CHECK_FALSE(selection.clamped);
  }
}

void TestSlotClamping() {
  BeginCase("an id with no slot of its own falls back to slot 0 and reports it");
  const uint32_t unowned_ids[] = {kKeySlots, kKeyIdBias + kKeySlots, 0x100, 0xFFFF, 0xFFFFFFFF};
  for (uint32_t key_id : unowned_ids) {
    const KeySlotSelection selection = SelectKeySlot(key_id);
    CHECK_EQ(selection.slot, 0);
    CHECK_TRUE(selection.clamped);
  }
}

void TestKeysetEntryAddresses() {
  BeginCase("every entry of the obscured table is recognised by address");
  for (uint32_t entry = 0; entry < kKeysetEntryCount; ++entry) {
    CHECK_TRUE(IsKeysetTableAddress(kKeysetTableAddress + entry * kKeySize));
  }
}

void TestNonKeysetAddresses() {
  BeginCase("addresses outside the table are not treated as table entries");
  CHECK_FALSE(IsKeysetTableAddress(kKeysetTableAddress - 1));
  CHECK_FALSE(IsKeysetTableAddress(kKeysetTableAddress - kKeySize));
  CHECK_FALSE(IsKeysetTableAddress(kKeysetTableAddress + kKeysetTableSize));
  CHECK_FALSE(IsKeysetTableAddress(kKeysetTableAddress + kKeysetTableSize - 1));
  CHECK_FALSE(IsKeysetTableAddress(0x82768C88));  // an unrelated guest buffer
  CHECK_FALSE(IsKeysetTableAddress(0));
}

void TestPlaintextLookupIndexesByEntryOffset() {
  BeginCase("plaintext lookup indexes the table by buffer offset");
  for (uint32_t entry = 0; entry < kKeysetEntryCount; ++entry) {
    const uint8_t* key = PlaintextKeyAtOffset(entry * kKeySize);
    CHECK_MEM_EQ(key, kPlaintextKeyTable + entry * kKeySize, kKeySize);
    CHECK_EQ(static_cast<uint64_t>(key - kPlaintextKeyTable), entry * kKeySize);
  }
}

void TestTableHoldsFourDistinctKeys() {
  BeginCase("the plaintext table holds four distinct, non-zero keys");
  for (uint32_t entry = 0; entry < kKeysetEntryCount; ++entry) {
    const uint8_t* key = PlaintextKeyAtOffset(entry * kKeySize);
    bool non_zero = false;
    for (uint32_t byte = 0; byte < kKeySize; ++byte) {
      non_zero = non_zero || (key[byte] != 0);
    }
    CHECK_TRUE(non_zero);
    for (uint32_t other = entry + 1; other < kKeysetEntryCount; ++other) {
      // Four separate keys are the whole point of the table: if any two entries
      // matched, the version -> entry mapping could not select a distinct key and
      // the B-009 regression below could not be observed.
      CHECK_FALSE(std::memcmp(key, PlaintextKeyAtOffset(other * kKeySize), kKeySize) == 0);
    }
  }
}

void TestVersionChoosesTableEntry() {
  BeginCase("the MOGG version selects the table entry (0x823DE070)");
  const struct {
    uint32_t version;
    uint32_t entry;
  } expected[] = {{12, 0}, {13, 0}, {14, 1}, {15, 2}, {16, 3}};

  for (const auto& want : expected) {
    const uint32_t buffer = InstallBufferForVersion(want.version);
    CHECK_EQ((buffer - kKeysetTableAddress) / kKeySize, want.entry);
    CHECK_TRUE(IsKeysetTableAddress(buffer));
  }

  // Versions outside the window fall back to entry 0 rather than failing.
  CHECK_EQ(EncMethodForVersion(0), 0);
  CHECK_EQ(EncMethodForVersion(11), 0);
  CHECK_EQ(EncMethodForVersion(17), 0);
}

void TestKeySourceIsDecidedInOrder() {
  BeginCase("the obscured table wins over the general guest-pointer arm");
  // The table lives high in the guest address space, so it is also a plausible
  // pointer. The hook's first arm has to win or HvDecrypt's own call would be
  // treated as "install these bytes verbatim" and the table would install itself.
  for (uint32_t entry = 0; entry < kKeysetEntryCount; ++entry) {
    const uint32_t buffer = kKeysetTableAddress + entry * kKeySize;
    CHECK_TRUE(IsPlausibleGuestPointer(buffer));
    CHECK_TRUE(SelectKeySource(buffer) == KeySource::kKeysetTable);
  }
  // One byte either side of the table is not an entry, so it falls through to the
  // pointer arm rather than being read as a key.
  CHECK_TRUE(SelectKeySource(kKeysetTableAddress - 1) == KeySource::kGuestBuffer);
  CHECK_TRUE(SelectKeySource(kKeysetTableAddress + kKeysetTableSize) == KeySource::kGuestBuffer);
}

void TestKeySourceForPlainGuestBuffers() {
  BeginCase("a guest buffer that is not the table is used verbatim");
  const uint32_t buffers[] = {0x10000, 0x82076598, 0xFFFEFFF0};
  for (uint32_t buffer : buffers) {
    CHECK_TRUE(IsPlausibleGuestPointer(buffer));
    CHECK_TRUE(SelectKeySource(buffer) == KeySource::kGuestBuffer);
  }
}

void TestKeySourceRefusesAddressesItCannotRead() {
  BeginCase("the null page and the top of the address space are not read");
  // The bounds are inclusive on both ends: an address is usable when a whole key
  // fits above it. The guest has never produced either shape - the wrapper rejects
  // a NULL buffer itself - so this is a refusal, not a path the title takes.
  CHECK_FALSE(IsPlausibleGuestPointer(0));
  CHECK_FALSE(IsPlausibleGuestPointer(0xFFFF));
  CHECK_FALSE(IsPlausibleGuestPointer(0xFFFEFFF1));
  CHECK_FALSE(IsPlausibleGuestPointer(0xFFFFFFFF));

  const uint32_t unusable[] = {0, 0xFFFF, 0xFFFEFFF1, 0xFFFFFFFF};
  for (uint32_t buffer : unusable) {
    CHECK_TRUE(SelectKeySource(buffer) == KeySource::kUnusable);
  }
}

void TestCbcKeySelection() {
  BeginCase("AES-CBC runs under the named slot, the last key, or nothing");
  // The guest's own order: install, then encrypt. A keyed slot is used as it is.
  CHECK_TRUE(SelectCbcKey(true, true) == CbcKeySelection::kSlotKeyed);
  CHECK_TRUE(SelectCbcKey(true, false) == CbcKeySelection::kSlotKeyed);
  // A slot that was never keyed borrows the most recent key...
  CHECK_TRUE(SelectCbcKey(false, true) == CbcKeySelection::kReuseLast);
  // ...and with no key installed anywhere the call is reported and still run.
  CHECK_TRUE(SelectCbcKey(false, false) == CbcKeySelection::kUnkeyed);
}

void TestBugWouldInstallDifferentKey() {
  BeginCase("B-009 regression: installing by key id would select a different key");
  // Before the fix the host resolved the key from the key id, which is a constant
  // 0xE0 for ByteGrinder::HvDecrypt, so slot 0 always received entry 0 and MOGG
  // streams of versions 14..16 used the wrong key.
  const uint8_t* const by_key_id = PlaintextKeyAtOffset(0);
  for (uint32_t version = 12; version <= 16; ++version) {
    const uint8_t* const by_buffer_offset = KeyInstalledForVersion(version);
    if (EncMethodForVersion(version) == 0) {
      CHECK_MEM_EQ(by_buffer_offset, by_key_id, kKeySize);
    } else {
      CHECK_FALSE(std::memcmp(by_buffer_offset, by_key_id, kKeySize) == 0);
    }
  }
}

}  // namespace

int main() {
  TestSlotFromBiasedKeyId();
  TestSlotFromBareKeyId();
  TestSlotClamping();
  TestKeysetEntryAddresses();
  TestNonKeysetAddresses();
  TestPlaintextLookupIndexesByEntryOffset();
  TestTableHoldsFourDistinctKeys();
  TestVersionChoosesTableEntry();
  TestKeySourceIsDecidedInOrder();
  TestKeySourceForPlainGuestBuffers();
  TestKeySourceRefusesAddressesItCannotRead();
  TestCbcKeySelection();
  TestBugWouldInstallDifferentKey();
  return rb_blitz::test::Finish();
}
