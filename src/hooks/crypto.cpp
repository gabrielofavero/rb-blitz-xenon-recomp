// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Guest addresses:
//   0x827F6BA4   import thunk -> __imp__XeKeysSetKey (xboxkrnl.exe ordinal)
//   0x827F6BB4   import thunk -> __imp__XeKeysAesCbc
//   0x82727218   guest XeKeysSetKey wrapper: returns 0x585 for index >= 8, 0x57 for
//                a NULL buffer and 0x18 for size != 16, otherwise calls the
//                import with (index + 0xE0, buffer, 16)
//   0x827272B0   guest XeKeysAesCbc wrapper: same 0xE0 id bias, hardcodes a NULL
//                feed and the decrypt direction, forwards r4..r6
//   0x823DE070   MOGG version -> key index: 12/13 -> 0, 14 -> 1, 15 -> 2,
//                16 -> 3, anything else -> 0 (ByteGrinder::GetEncMethod)
//   0x823DE0C0   ByteGrinder::HvDecrypt(in, out, version): installs the key for
//                this version, then AES-128-ECB-decrypts the 16 bytes at `in` into
//                `out`. It derives the 16-byte stream mask, it does not decrypt
//                audio.
//   0x82768C88   VorbisReader::CheckHmxHeader, the MOGG header parser and the only
//                caller: reads version/nonce/magic/key index out of the HMX header
//                and calls HvDecrypt once per music stream
//   0x82768AD0   VorbisReader::setupCypher(version): CTR key = GrindArray(keychain
//                key) ^ stream mask, then ctr_start(nonce)
//   0x8280C568   64-byte .data key table: four *obscured* AES-128 keys
//
// Observed failure:
//   Music never plays while sound effects do. The only crypto imports the guest
//   ever reaches are logged once, ~16 s into boot, and both are no-ops:
//     [krnl] __imp__XeKeysSetKey STUB
//     [krnl] __imp__XeKeysAesCbc STUB
//   That pair is the first MOGG stream being opened (the title music). The header
//   parse gets noise back instead of the stream mask, so the CTR key is wrong, the
//   vorbis decoder sees garbage and the stream is dropped, while sound effects
//   never touch this path and play normally. Harmonix titles of this era keep all
//   music in MOGG streams and obfuscate only the audio, so music alone is silent.
//
// Intended behaviour:
//   XeKeysSetKey is what applies KEY_OBFUSCATION_KEY, a per-console value held in
//   hardware seals. The recompiled runtime has no such seal, so with the stub in
//   place the table bytes are never de-obfuscated and the derived mask is garbage.
//   The de-obfuscated material is a platform constant rather than per-console data,
//   so it is installed directly here: any XeKeysSetKey whose buffer points into
//   0x8280C568 hands the AES engine the known plaintext key for that slot, and
//   AES-CBC is forwarded to the runtime's real XeCryptAesCbc, so
//     XeKeysSetKey(0xE0, &table[index * 16], 16);
//     XeKeysAesCbc(0xE0, in, 16, out, NULL, decrypt);
//   yields AES-128-ECB(keytable[index], in) -> out. One CBC block with a null feed
//   is exactly an ECB decrypt, which is what HvDecrypt does.
//
// Evidence:
//   - The recompiled runtime ships both imports as REX_EXPORT_STUB no-ops
//     (rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_crypt.cpp:710,717) while the
//     underlying AES entry points (XeCryptAesKey/XeCryptAesCbc) are real.
//   - The guest wrapper validates index < 8, size == 16 and adds 0xE0, so the
//     kernel sees key ids 0xE0..0xE7 for the four table entries.
//   - The 64 bytes at 0x8280C568 are byte-identical to the table retail Rock
//     Band 3 keeps at 0x82C76258 (freeqaz/rb3-xenon,
//     tools/oss-xbox-build/rb3dx_port_audit.json, .data span "clean"), and the
//     RB3DX port replaces exactly those bytes with the plaintext keyset below
//     (same audit, span "rb3dx"). The obfuscation and its inverse are therefore
//     title-independent.
//   - Those same 64 bytes are the engine constant gHvKeyGreen
//     (freeqaz/rb3 src/system/synth/ByteGrinder.cpp) used with the identical
//     version -> index mapping, and they are the NewKeyset that band3_recomp
//     installs for the "MOGG AES key path" at 0x82c76258. RB3DX additionally
//     ports the same two imports (PORT_XEKEYSSETKEY_STUB, PORT_XEKEYSAESCBC_STUB)
//     and nothing else crypto related, so three independent ports apply this same
//     replacement.
//   - The guest code at 0x82768C88 maps one-to-one onto RB3's
//     VorbisReader::CheckHmxHeader (freeqaz/rb3 src/system/synth/VorbisReader.cpp):
//     the 60000-byte header buffer, the version window 0xC..0x10, the nonce, the
//     two 64-bit magics, the 16-byte block read twice, the `% 6 + 6` key index and
//     the `TheSynth->mGrinder.HvDecrypt(stuff, mKeyMask, version)` call are all
//     present in the same order. 0x82768AD0 is that file's setupCypher, which
//     mixes the mask into the CTR key (gKey[i] ^= mKeyMask[i]) and starts the
//     stream cipher. So the mask this hook returns is the only kernel-side input
//     to MOGG decryption; the keychain, the grinder and the CTR cipher are all
//     guest software that runs on data already inside the image.
//   - The plaintext keyset does not appear anywhere in the Blitz image in any
//     form or permutation, and the 0x180-byte keychain blob the guest passes
//     around at 0x82076598 is byte-identical to the published RB3 one, so only
//     the obscured table needs substituting.
//
// Milestone: 4 (see docs/bringup-log.md B-009).

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

// Real AES entry points exported by rexruntime (rex::kernel::xboxkrnl).
// Declared through the SDK macro so the parameter types match the runtime's
// definitions exactly.
REX_EXTERN(__imp__XeCryptAesKey);
REX_EXTERN(__imp__XeCryptAesCbc);

namespace {

// Guest .data table holding the obscured key material.
constexpr uint32_t kKeysetTableAddress = 0x8280C568;
constexpr uint32_t kKeysetTableSize = 0x40;

// The guest wrapper biases every key index by 0xE0 and rejects index >= 8.
constexpr uint32_t kKeyIdBias = 0xE0;
constexpr uint32_t kKeySlots = 8;

constexpr uint32_t kAesStateSize = 0x160;  // XECRYPT_AES_STATE
constexpr uint32_t kKeySize = 0x10;        // AES-128
constexpr uint32_t kFeedSize = 0x10;       // CBC chaining block

// How many calls to dump in full. Music streams, so logging every call would
// drown the log; the first few are enough to prove the path works.
constexpr uint32_t kVerboseCalls = 8;

// De-obfuscated key material: what XeKeysSetKey would install after the console
// applied KEY_OBFUSCATION_KEY to the table above. Identical to the RB3 "green"
// keyset / band3_recomp NewKeyset.
constexpr uint8_t kDeobfuscatedKeyTable[kKeysetTableSize] = {
    0x01, 0x22, 0x00, 0x38, 0xD2, 0x01, 0x78, 0x8B, 0xDD, 0xCD, 0xD0, 0xF0, 0xFE, 0x3E, 0x24, 0x7F,
    0x51, 0x73, 0xAD, 0xE5, 0xB3, 0x99, 0xB8, 0x61, 0x58, 0x1A, 0xF9, 0xB8, 0x1E, 0xA7, 0xBE, 0xBF,
    0xC6, 0x22, 0x94, 0x30, 0xD8, 0x3C, 0x84, 0x14, 0x08, 0x73, 0x7C, 0xF2, 0x23, 0xF6, 0xEB, 0x5A,
    0x02, 0x1A, 0x83, 0xF3, 0x97, 0xE9, 0xD4, 0xB8, 0x06, 0x74, 0x14, 0x6B, 0x30, 0x4C, 0x00, 0x91};

// Guest buffers, allocated on first use (heap is not up during static init).
uint32_t g_aes_state = 0;  // kKeySlots * XECRYPT_AES_STATE
uint32_t g_key_table = 0;  // kKeySlots * 16 plaintext key bytes
uint32_t g_feed = 0;       // kFeedSize, kept zeroed

uint32_t g_last_keyed_slot = 0;
bool g_has_keyed_slot = false;
bool g_slot_keyed[kKeySlots] = {};
bool g_warned_unkeyed[kKeySlots] = {};
bool g_warned_lookup = false;
bool g_warned_fallback = false;
bool g_header_logged = false;
uint32_t g_calls = 0;

// Guest address -> host pointer, using the same translation the runtime applies
// when marshalling guest arguments (and the same one recompiled code uses).
uint8_t* GuestToHost(uint8_t* base, uint32_t guest_address) {
  return rex::memory::GuestPtr<uint8_t*>(base, guest_address);
}

bool EnsureGuestBuffers(uint8_t* base) {
  if (g_aes_state != 0) {
    return true;
  }
  auto* memory = rex::system::kernel_memory();
  if (memory == nullptr) {
    REXLOG_WARN("guest XeKeys: kernel memory not available yet, ignoring the call");
    return false;
  }
  g_aes_state = memory->SystemHeapAlloc(kKeySlots * kAesStateSize, 16);
  g_key_table = memory->SystemHeapAlloc(kKeySlots * kKeySize, 16);
  g_feed = memory->SystemHeapAlloc(kFeedSize, 16);

  std::memset(GuestToHost(base, g_aes_state), 0, kKeySlots * kAesStateSize);
  std::memcpy(GuestToHost(base, g_key_table), kDeobfuscatedKeyTable, kKeysetTableSize);
  std::memset(GuestToHost(base, g_feed), 0, kFeedSize);

  REXLOG_INFO("guest XeKeys: aes states 0x{:08X}, key table 0x{:08X} ({} slots), feed 0x{:08X}",
              g_aes_state, g_key_table, static_cast<uint32_t>(kKeySlots), g_feed);
  return true;
}

uint32_t KeySlotForId(uint32_t key_id) {
  uint32_t slot = (key_id >= kKeyIdBias) ? (key_id - kKeyIdBias) : key_id;
  if (slot >= kKeySlots) {
    if (!g_warned_lookup) {
      g_warned_lookup = true;
      REXLOG_WARN("guest XeKeys: unexpected key id 0x{:X}, using slot 0", key_id);
    }
    slot = 0;
  }
  return slot;
}

// True when the guest key buffer is one of the four obscured table entries.
bool IsKeysetTableAddress(uint32_t guest_address) {
  if (guest_address < kKeysetTableAddress) {
    return false;
  }
  return (guest_address - kKeysetTableAddress) + kKeySize <= kKeysetTableSize;
}

bool IsPlausibleGuestPointer(uint32_t guest_address) {
  return guest_address >= 0x10000 && guest_address <= 0xFFFF0000 - kKeySize;
}

std::string HexDump(const uint8_t* data, uint32_t size) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(size * 2);
  for (uint32_t i = 0; i < size; ++i) {
    out.push_back(kHex[data[i] >> 4]);
    out.push_back(kHex[data[i] & 0xF]);
  }
  return out;
}

std::string AsciiColumn(const uint8_t* data, uint32_t size) {
  std::string out;
  out.reserve(size);
  for (uint32_t i = 0; i < size; ++i) {
    out.push_back((data[i] >= 0x20 && data[i] < 0x7F) ? static_cast<char>(data[i]) : '.');
  }
  return out;
}

// Bonus diagnostic: recognises a decrypted container header in the output buffer
// so a log confirms the reveal produced real audio data.
const char* ContainerMagic(const uint8_t* data, uint32_t size) {
  if (size < 4) {
    return nullptr;
  }
  static const char kMagics[][5] = {"MOGG", "OggS", "RIFF", "fLaC", "XMA "};
  for (const char* magic : kMagics) {
    if (std::memcmp(data, magic, 4) == 0) {
      return magic;
    }
  }
  return nullptr;
}

// Loads the plaintext key for `slot` from our table into the AES engine.
void InstallKey(PPCContext& ctx, uint8_t* base, uint32_t slot) {
  ctx.r3.u64 = g_aes_state + slot * kAesStateSize;
  ctx.r4.u64 = g_key_table + slot * kKeySize;
  __imp__XeCryptAesKey(ctx, base);
}

}  // namespace

// XeKeysSetKey(uint32_t key_id, void* key_buffer, uint32_t key_size)
extern "C" REX_FUNC(__imp__XeKeysSetKey) {
  if (!EnsureGuestBuffers(base)) {
    ctx.r3.u64 = 0;
    return;
  }

  const uint32_t key_id = static_cast<uint32_t>(ctx.r3.u64);
  const uint32_t key_buffer = static_cast<uint32_t>(ctx.r4.u64);
  const uint32_t key_size = static_cast<uint32_t>(ctx.r5.u64);
  const uint32_t slot = KeySlotForId(key_id);

  if (IsKeysetTableAddress(key_buffer)) {
    // The guest is installing an entry of its obscured table. The de-obfuscated
    // key for that slot is already in our table, so there is nothing to copy.
    REXLOG_INFO("guest XeKeys: key 0x{:X} -> slot {} (obscured table entry +0x{:X})", key_id,
                slot, key_buffer - kKeysetTableAddress);
  } else if (IsPlausibleGuestPointer(key_buffer)) {
    // A key installed straight from guest memory is used verbatim.
    REXLOG_INFO("guest XeKeys: key 0x{:X} -> slot {} from guest buffer 0x{:08X} (size {})", key_id,
                slot, key_buffer, key_size);
    std::memcpy(GuestToHost(base, g_key_table + slot * kKeySize), GuestToHost(base, key_buffer),
                kKeySize);
  } else {
    REXLOG_WARN(
        "guest XeKeys: key 0x{:X} -> slot {} has unusable buffer 0x{:08X} (size {}), keeping the "
        "known key",
        key_id, slot, key_buffer, key_size);
  }

  InstallKey(ctx, base, slot);

  g_slot_keyed[slot] = true;
  g_last_keyed_slot = slot;
  g_has_keyed_slot = true;

  ctx.r3.u64 = 0;
}

// XeKeysAesCbc(uint32_t key_id, void* inp, uint32_t inp_size, void* out,
//              void* feed, uint32_t encrypt)
extern "C" REX_FUNC(__imp__XeKeysAesCbc) {
  if (!EnsureGuestBuffers(base)) {
    ctx.r3.u64 = 0;
    return;
  }

  const uint32_t key_id = static_cast<uint32_t>(ctx.r3.u64);
  const uint32_t inp = static_cast<uint32_t>(ctx.r4.u64);
  const uint32_t inp_size = static_cast<uint32_t>(ctx.r5.u64);
  const uint32_t out = static_cast<uint32_t>(ctx.r6.u64);
  uint32_t feed = static_cast<uint32_t>(ctx.r7.u64);
  const uint32_t encrypt = static_cast<uint32_t>(ctx.r8.u64);
  const uint32_t slot = KeySlotForId(key_id);

  if (!g_slot_keyed[slot] && g_has_keyed_slot) {
    // The guest asked for a slot it never installed a key into; reuse the most
    // recent one instead of running AES with an empty schedule.
    if (!g_warned_fallback) {
      g_warned_fallback = true;
      REXLOG_WARN(
          "guest XeKeys: AES-CBC on key 0x{:X} (slot {}) before XeKeysSetKey, reusing slot {}",
          key_id, slot, g_last_keyed_slot);
    }
    std::memcpy(GuestToHost(base, g_key_table + slot * kKeySize),
                GuestToHost(base, g_key_table + g_last_keyed_slot * kKeySize), kKeySize);
    InstallKey(ctx, base, slot);
    g_slot_keyed[slot] = true;
  }
  if (!g_slot_keyed[slot] && !g_warned_unkeyed[slot]) {
    g_warned_unkeyed[slot] = true;
    REXLOG_WARN("guest XeKeys: AES-CBC on key 0x{:X} (slot {}) before any key was installed",
                key_id, slot);
  }

  // The guest always passes a null feed (IV = 0), but the runtime's CBC
  // implementation dereferences feed and writes the last ciphertext block back
  // into it, so a null feed has to become a real buffer.
  if (feed == 0) {
    std::memset(GuestToHost(base, g_feed), 0, kFeedSize);
    feed = g_feed;
  }

  const bool verbose = g_calls < kVerboseCalls;
  ++g_calls;

  if (verbose) {
    REXLOG_INFO("guest XeKeys: AES-CBC key 0x{:X} slot {} size {} encrypt {} in 0x{:08X} out 0x{:08X}",
                key_id, slot, inp_size, encrypt, inp, out);
  }

  ctx.r3.u64 = g_aes_state + slot * kAesStateSize;
  ctx.r4.u64 = inp;
  ctx.r5.u64 = inp_size;
  ctx.r6.u64 = out;
  ctx.r7.u64 = feed;
  ctx.r8.u64 = encrypt;
  __imp__XeCryptAesCbc(ctx, base);

  if (verbose && IsPlausibleGuestPointer(inp) && IsPlausibleGuestPointer(out)) {
    const uint32_t dump_size = std::min(inp_size, 32u);
    REXLOG_INFO("guest XeKeys:   in  {} |{}|", HexDump(GuestToHost(base, inp), dump_size),
                AsciiColumn(GuestToHost(base, inp), dump_size));
    REXLOG_INFO("guest XeKeys:   out {} |{}|", HexDump(GuestToHost(base, out), dump_size),
                AsciiColumn(GuestToHost(base, out), dump_size));
  }

  // Bonus proof: the boot-time call reveals a 16-byte key rather than a container
  // header, so this normally stays quiet. If a whole MOGG header ever travels
  // through here it shows the key is right.
  if (encrypt == 0 && IsPlausibleGuestPointer(out) && !g_header_logged) {
    if (const char* magic = ContainerMagic(GuestToHost(base, out), std::min(inp_size, 4u))) {
      g_header_logged = true;
      REXLOG_INFO("guest XeKeys: decrypted a {} container header, music key is correct", magic);
    }
  }

  ctx.r3.u64 = 0;
}
