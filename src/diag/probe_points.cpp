// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The probe points that exist today: the four named functions of the MOGG
// (music) decryption path, in the order the engine walks them.
//
// This is what a "named path" means in src/diag/probe.h - a handful of guest
// functions that are already named with recorded evidence
// (docs/symbols.md "Audio (MOGG) decryption path") and that the guest reaches in
// a known order when it opens a music stream:
//
//     ByteGrinder_GetEncMethod  -> which key table entry this stream's version uses
//     ByteGrinder_HvDecrypt     -> installs that key, decrypts the header mask
//     VorbisReader_SetupCypher  -> derives the CTR key from the mask
//     VorbisReader_CheckHmxHeader -> the header parse that drives all three
//
// Why these four and not the menu: a probe point can only be placed on a
// function we have already identified, and the menu's functions have no names
// yet - finding them is the flow-mapping work the probe exists to serve
// (docs/plans/customization-plan.md S2). These four are the only guest path this
// project has proved end to end, so they are the demonstration that the
// instrument works, and adding the next point is one line.
//
// Each override is a pass-through: it records and then calls
// `__imp__<name>`, which is the original body (ultimate.cpp does the same with
// the same reasoning). With `probe_trace` off the only cost is the call
// indirection the project's hooks already carry everywhere.

#include <atomic>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "diag/probe.h"

REX_EXTERN(__imp__ByteGrinder_GetEncMethod);
REX_EXTERN(__imp__ByteGrinder_HvDecrypt);
REX_EXTERN(__imp__VorbisReader_SetupCypher);
REX_EXTERN(__imp__VorbisReader_CheckHmxHeader);
// 0x823DE070: MOGG version -> key-table index selector.
REX_HOOK_RAW(ByteGrinder_GetEncMethod) {
  RBBLITZ_PROBE(0x823DE070, "mogg.enc_method");
  __imp__ByteGrinder_GetEncMethod(ctx, base);
}

// 0x823DE0C0: installs the version's key, then decrypts the 16-byte header mask.
REX_HOOK_RAW(ByteGrinder_HvDecrypt) {
  RBBLITZ_PROBE(0x823DE0C0, "mogg.hv_decrypt");
  __imp__ByteGrinder_HvDecrypt(ctx, base);
}

// 0x82768AD0: stream key = GrindArray(keychain) ^ mask, then CTR start.
REX_HOOK_RAW(VorbisReader_SetupCypher) {
  RBBLITZ_PROBE(0x82768AD0, "mogg.setup_cypher");
  __imp__VorbisReader_SetupCypher(ctx, base);
}

// 0x82768C88: the MOGG header parser, the only caller of ByteGrinder_HvDecrypt.
REX_HOOK_RAW(VorbisReader_CheckHmxHeader) {
  RBBLITZ_PROBE(0x82768C88, "mogg.header");
  __imp__VorbisReader_CheckHmxHeader(ctx, base);
}

// ---------------------------------------------------------------------------
// P2 spike: the exact-render-resolution experiment (docs/plans/in-game-settings-
// resolution-findings.md).
//
// 0x823F3698 is where the title decides its own screen size: it stores a height in
// the object's +68 (720, or 540 for the half-resolution mode) and derives the width
// into +64 as height*16/9 (1280) or height*4/3 (960). Nothing upstream of this reads
// the video mode the SDK reports, which is why `video_mode_width/height` changes the
// window and the letterbox but never the picture (measured, P2).
//
// The spike overwrites those two fields with 1920x1080 and reports what the title had
// chosen, so one boot answers whether the guest's size decision is the lever the
// findings doc claims it is. Off by default; the log line is the evidence either way.
//
// This is an instrument, not a feature: it is deliberately not wired to a user setting,
// and P4 replaces or deletes it when the render path lands.
REX_EXTERN(__imp__sub_823F3698);

REXCVAR_DEFINE_BOOL(probe_screen_1080p, false, "Diagnostics",
                    "P2 spike: make the title take its own half-resolution (960x540) screen path "
                    "before it computes its size, and report what it chose. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// 0x8251D580 is the title's present: it builds the swap packet and calls VdSwap with
// the front-buffer size. Its r31 is the engine object, and `*(object + 11024) + 8` is
// the Direct3D 9 object the swap reads its size from - so dumping both once is how the
// guest's real back-buffer size is found without a debugger.
REX_EXTERN(__imp__sub_8251D580);

REXCVAR_DEFINE_BOOL(probe_dump_guest_swap, false, "Diagnostics",
                    "P2 spike: dump the guest present object and its Direct3D 9 pointer once, to "
                    "locate the title's real back-buffer size. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

// The guest is big-endian and these two fields are plain ints in its own image.
uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void StoreBe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

// One line per distinct choice, so a boot that calls this repeatedly still leaves a
// readable log.
uint64_t g_last_reported = ~uint64_t(0);

// The swap dump is a one-shot: the first present is enough to locate the structure.
std::atomic<bool> g_swap_dumped{false};

}  // namespace

extern "C" REX_FUNC(sub_823F3698) {
  const uint32_t object = uint32_t(ctx.r3.u32);
  const bool override_on = REXCVAR_GET(probe_screen_1080p);
  if (override_on) {
    // Bit 0 of +796 is the flag this very function reads (0x823F3804) to pick 540 instead
    // of 720; forcing it at entry makes the title take its own half-resolution path rather
    // than being poked after the fact, so anything that path re-creates is re-created.
    uint8_t* memory_in = rex::memory::GuestPtr<uint8_t*>(base, object);
    if (memory_in != nullptr) {
      StoreBe32(memory_in + 796, LoadBe32(memory_in + 796) | 1);
    }
  }
  __imp__sub_823F3698(ctx, base);
  if (object == 0) {
    return;
  }
  uint8_t* memory = rex::memory::GuestPtr<uint8_t*>(base, object);
  if (memory == nullptr) {
    return;
  }
  const uint32_t width = LoadBe32(memory + 64);
  const uint32_t height = LoadBe32(memory + 68);
  const uint64_t report = (uint64_t(width) << 32) | height;
  if (report != g_last_reported) {
    g_last_reported = report;
    REXLOG_INFO("probe/screen: object 0x{:08X} chose {}x{}{}", object, width, height,
                override_on ? " (half-resolution forced)" : "");
  }
}

// Dumps guest memory around a base address as big-endian dwords, so a value found in the
// log can be read back with the same arithmetic the recompiled code uses.
void DumpGuest(const char* tag, uint8_t* base, uint32_t address, uint32_t dwords) {
  uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, address);
  if (p == nullptr) {
    REXLOG_INFO("probe/dump {}: 0x{:08X} is not mapped", tag, address);
    return;
  }
  for (uint32_t i = 0; i < dwords; i += 4) {
    REXLOG_INFO("probe/dump {}: 0x{:08X}  {:08X} {:08X} {:08X} {:08X}", tag, address + i * 4,
                LoadBe32(p + i * 4), LoadBe32(p + i * 4 + 4), LoadBe32(p + i * 4 + 8),
                LoadBe32(p + i * 4 + 12));
  }
}

extern "C" REX_FUNC(sub_8251D580) {
  const bool dump = REXCVAR_GET(probe_dump_guest_swap) && !g_swap_dumped.exchange(true);
  const uint32_t object = uint32_t(ctx.r3.u32);
  __imp__sub_8251D580(ctx, base);
  if (!dump || object == 0) {
    return;
  }
  uint8_t* memory = rex::memory::GuestPtr<uint8_t*>(base, object);
  if (memory == nullptr) {
    return;
  }
  const uint32_t d3d = LoadBe32(memory + 11024);
  REXLOG_INFO("probe/dump: present object 0x{:08X}, d3d 0x{:08X}", object, d3d);
  DumpGuest("object+48", base, object + 48, 8);
  if (d3d != 0) {
    DumpGuest("d3d", base, d3d, 64);
    DumpGuest("d3d+256", base, d3d + 256, 64);
  }
}
