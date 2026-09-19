---
status: not-started
milestone: 5
last_updated: 2026-09-19
---

# Milestone 5 — Complete one song

## Entry state

- Milestone 4 done: menus navigate, bundled content enumerates, a song can be
  selected, input works, saves persist.
- **Knowledge folded in:** *(edit as Milestone 4 finishes — chosen song, input
  mapping, storage format, content quirks.)*

## Reference leads (RB3 mining pass, 2026-09-19)

Detail in [`docs/rb3-references.md`](../docs/rb3-references.md).

- **Our audio fix is independently confirmed.** RB3DX's patch group 1 is the
  *same* fix (the same 64-byte key table, the same `XeKeysSetKey`/`XeKeysAesCbc` →
  `XeCryptAesKey`/`XeCryptAesCbc` swap) applied by byte-patching retail RB3. The
  table address differs (`0x82C76258` vs our `0x8280C568`) but the bytes match.
- **Cross-check our deobfuscation against a second implementation:**
  `freeqaz/rb3` has `scripts/native/decrypt_mogg.py`, and `src/system/synth/` is
  the engine source for `VorbisReader`/`ByteGrinder`/the keychain.
- **"Music plays" must be measured, not eyeballed.** The RB3 native port's
  `audio-verify` skill verifies decoded audio three ways: **identity** (chroma
  cross-correlation against a reference render), **speed** (resample-search for
  pitch/tempo drift) and **distortion** (clip/flat-top detection). Adopt this for
  step 3 so the M5 acceptance test means something stronger than "I heard it".
- **Unit-test the deobfuscation before booting anything.** The key-table
  deobfuscation in `src/hooks/crypto.cpp` is pure host code and can be checked
  against the known plaintext keyset (recorded in `docs/symbols.md`) without
  running the game. The RB3 port's audit names "verified once and forgotten" as
  its worst failure mode — this is the cheapest durable guard we can buy.
- **Before repacking or editing any stream, hook
  `StreamChecksum__ValidateChecksum` → `1`** (band3 `patches.cpp`), or validation
  rejects the edit and we chase a phantom decode bug.
- **Song-list gates to check if a song is missing or plays as a demo:** RB3DX
  group 6 (`BandSongMgr::IsDemo` forced false; band3 hooks `SongMgr__IsDemo` → 0),
  group 7 (`AddSongData` special-song table disabled) and group 9 (song cache
  name). If ours enumerates fewer songs than expected, look here first.
- **If we ever become math-bound**, band3 replaces
  `_Normalize_*`, `_Multiply_Matrix3`, `_acos/_asin/_atan/_atan2/_cos/_floor/`
  `_fmod/_pow/_sin/_tan` with native implementations. Measure before porting.
  Float-argument hooks may need `REX_HOOK_RAW` — `REX_HOOK` mis-maps registers
  for some of them (band3 `math.cpp` documents this).
- **Frame-rate-dependent logic** (e.g. camera shake) is only our problem if we
  uncap the frame rate; band3's `CamShot__Shake` reimplementation is a 24 KB
  hand-transcribed hack it describes as "fragile shitty". Defer.

## Reference points

- `docs/baselines/xenia-canary-80679bc.md`: historical Xenia report reached
  gameplay but lacked note tracks and 3D background. During bring-up, compare
  failing draws against a Xenia capture — especially resolve/readback and
  render-target transitions.

## Steps

### 1. Song load

- Get through song load without timeout, deadlock, or missing-file errors.

### 2. Graphics

- Verify the Xenos pipeline draws the note highway, notes, HUD, and essential
  background objects.
- Compare failing draws against a Xenia capture; prefer correcting reusable
  Xenos behavior over game-specific rendering hacks.

### 3. Audio

- Verify audio voices, sample formats, streaming, clocks, and pause/resume.
- Measure audio/gameplay drift across a full song; defer fine calibration unless
  drift makes play impossible.

### 4. Stability

- Verify frame pacing and input polling are stable for a complete run.
- Reach results, return to song select, and play again without leaked state or
  a crash.

## Acceptance / exit criteria

- [ ] One named bundled song passes the full launch → results path **three
      times in a row** from a clean process.

## Handoff to Milestone 6

Write into `06-reproducible-release.md`:

- the chosen song used for the acceptance test;
- any known graphics/audio quirks that must be documented for the release;
- the exact smoke-test route used.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
