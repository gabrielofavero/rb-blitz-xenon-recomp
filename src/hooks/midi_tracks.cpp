// SPDX-License-Identifier: GPL-2.0-only
// Blitz does not consume the optional LIPSYNC1..4 MIDI tracks carried by some
// converted songs. Its first metadata pass nevertheless accepts every track,
// collects bracketed text as unknown events and overflows DataArray's signed
// 16-bit length. Can't Hold Us reaches 0x8000 entries on LIPSYNC1, then requests
// 0xFFFC0008 bytes and indexes below the size-class allocator's table.
//
// Extend MidiParser::ShouldReadTrack's rejection policy, so OnTrackName uses
// its own MidiReader::SkipCurrentTrack (seek to the MTrk end and notify track
// end). No asset is rewritten, no event from a playable track is discarded,
// and no loader completion or cleanup is fabricated. Independent of Load Guard.
// Evidence: docs/plans/in-progress/2026-10-13-load-guard.md, part 4.
#include <rex/hook.h>

REX_EXTERN(__imp__sub_82438880);

REX_HOOK_RAW(sub_82438880) {
  // r4 is an interned track-name Symbol, valid for this invocation. Read no
  // more than the exact spellings require, stopping at their first mismatch.
  const auto name = ctx.r4.u32;
  bool optional_lipsync = name != 0;
  constexpr char prefix[] = "LIPSYNC";
  for (unsigned i = 0; optional_lipsync && i < sizeof(prefix) - 1; ++i)
    optional_lipsync = base[name + i] == static_cast<uint8_t>(prefix[i]);
  unsigned part = 0;
  if (optional_lipsync) {
    part = base[name + 7];
    optional_lipsync = part >= '1' && part <= '4' && base[name + 8] == 0;
  }
  __imp__sub_82438880(ctx, base);
  if (optional_lipsync && ctx.r3.u32) {
    REXLOG_INFO("midi_tracks: skipping optional LIPSYNC{} through native track rejection", part - '0');
    ctx.r3.u64 = 0;
  }
}
