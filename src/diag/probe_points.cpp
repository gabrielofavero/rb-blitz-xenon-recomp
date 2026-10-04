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

#include <rex/hook.h>

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
