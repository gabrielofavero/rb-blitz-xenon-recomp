---
status: in-progress
milestone: 5
last_updated: 2026-09-20
---

# Milestone 5 — Complete one song

## Entry state

- Milestone 4 is **working but not closed**: menus navigate, bundled content
  enumerates, a song can be selected and played. Open there, and inherited here:
  the ARK/HDR offset audit, pad-driven verification, and save restart /
  corrupt-writable-data behaviour. Detail: "Milestone 4 close-out" in
  [docs/bringup-log.md](../docs/bringup-log.md).
- **Knowledge folded in (2026-09-19):**
  - **Already playable:** songs load, the highway/notes/HUD/3D background render,
    and the author has played full songs through 3–4 times. That was an
    observation rather than a recorded acceptance run, and none of it was
    scripted; both changed on 2026-09-20 — see "Acceptance / exit criteria"
    below.
  - **Content quirks:** bundled content enumerates from the game-data root
    (`XamContentCreateEnumerator: added 2 items` — the `songcache:` /
    `globaloptions:` content devices); `game:\Content\0000000000000000` does not
    exist, so the aggregate DLC enumerator returns 0 items (expected offline).
  - **Storage:** the title writes into the writable root (here
    `C:\Users\gabri\Documents\rb_blitz`):
    `B13EBABEBABEBABE\5841122D\00000001\{globaloptions,songcache}` plus 328-byte
    headers, and rewrites its shader cache
    `cache\shaders\shareable\5841122D.rtv.d3d12.xpso` at process exit. All of it
    is the SDK content path; no project code is involved.
  - **Input mapping:** `XInput Controller #1` (VendorID `0x0B05`, ProductID
    `0x1B4C`) is enumerated, but every verified run was driven by keyboard
    injection (`scripts/drive_ui.ps1`): `space`/`semicolon`→A, `backspace`→B,
    `l`→X, `p`→Y, `enter`→Start, `z`/`tab`→Back, arrows (+Shift = D-pad),
    WASD = sticks. Pad lane controls are unverified.
  - **The named song is recorded (2026-09-20):** "These Days" (Foo Fighters), song
    list row 3 of `Random Song` / `One Week` / `These Days` / `Death on Two Legs`.
    The list cursor is moved by the **left stick** (`lstick_up` / `lstick_down`),
    one row per press, clamped at both ends; the arrow keys are bound to the right
    stick and do nothing here.
  - **The MOGG key entry is selected by buffer offset, not by key id.** If audio
    regresses for one stream but not another, read "B-009 follow-up" in
    [docs/bringup-log.md](../docs/bringup-log.md) first.
  - **Audio frames are produced from the title onward**
    (`XAudioRegisterRenderDriverClient` + `XAudioSubmitRenderDriverFrame`), so M5
    starts from "frames are produced" and only has to prove they are audible.

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
  against the known plaintext keyset without running the game. The RB3 port's
  audit names "verified once and forgotten" as its worst failure mode — this is
  the cheapest durable guard we can buy.
  **Done 2026-09-19:** the pure half (id → slot, table-address bounds, version →
  entry, plaintext lookup) now lives in `src/hooks/crypto_keytable.h` and is
  covered by `tests/crypto_keytable_tests.cpp` under `ctest`; see "First host unit
  tests" in `docs/bringup-log.md`. What is still untested is everything that needs
  a run: the guest buffers, the AES forwarding, and whether music actually plays.
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
- **Working (2026-09-19)** — but only after B-011: picking a song trapped on
  `0x82783CD8` until 15 missed indirect-call targets were registered
  (`0x827EC038` plus 14 adjuster-thunk holes). Registered targets and the pattern
  scan that found them: [docs/bringup-log.md](../docs/bringup-log.md) B-011.
  Expect one more of these for each newly reached gameplay path — the fault text
  is `[FATAL] Call to invalid or unregistered function at guest address 0x…`.

### 2. Graphics

- Verify the Xenos pipeline draws the note highway, notes, HUD, and essential
  background objects.
- **Solved 2026-09-19 (B-010): the note highway and 3D background render.** They
  were invisible because their textures use guest format 35 (`k_32_32_32_32`) with
  `num_format = 0` — 0.32 fixed point, which has no samplable host format — so
  `CreateTexture` returned `nullptr` and the alpha-blended geometry sampled
  `(0,0,0,0)`. The SDK patch
  [patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch](../patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch)
  converts the words to floats on the CPU; see
  [docs/bringup-log.md](../docs/bringup-log.md) B-010. Two limits remain: 32-bit
  **integer** textures (`num_format = 1`) still fail to create, and one converted
  texture must fit a single 2 MiB upload page.
- Compare failing draws against a Xenia capture; prefer correcting reusable
  Xenos behavior over game-specific rendering hacks.

### 3. Audio

- Verify audio voices, sample formats, streaming, clocks, and pause/resume.
- **Status (2026-09-19): audible, not measured.** Title music and in-song MOGG
  streams decrypt (`guest XeKeys: key 0xE0 -> slot 0 (obscured table entry +0x30
  -> plaintext key 3)`) and the surviving log contains one clean start/stop pair,
  `XMPSetPlaybackController(0,1)` at 22:18:35 → `(0,0)` at 22:22:39. Voice counts,
  sample formats, pause/resume and the `audio-verify` methodology above are all
  still unchecked — "I heard it" is not yet evidence.
- Measure audio/gameplay drift across a full song; defer fine calibration unless
  drift makes play impossible.

### 4. Stability

- **Status (2026-09-20): the launch → results path is recorded and passing; the
  measurements are not.** Three clean-process runs and one replay run are captured
  in `out/m5-acceptance/`. Frame pacing and input polling are still unmeasured
  (a vanilla log prints no fps or pacing line), results-to-song-select was only
  observed under `-Replay`, and pause/resume is untouched.
- Verify frame pacing and input polling are stable for a complete run.
- Reach results, return to song select, and play again without leaked state or
  a crash. **Observed working** (2026-09-20, `-Replay`): the results screen returns
  to the song list of the same process, the same row is reselected, and the second
  song reaches its own results screen with no `[FATAL]` and no leaked state.

## Acceptance / exit criteria

- [x] One named bundled song passes the full launch → results path **three
      times in a row** from a clean process. **Met** (2026-09-20):
      [scripts/acceptance_song.ps1](../scripts/acceptance_song.ps1) launches the
      title, drives the offline route and asserts each transition on screenshots
      plus the run's own log. Three consecutive runs passed — playback envelopes
      315 / 317 / 317 s, a results screen naming `THESE DAYS`, no `[FATAL]`, a
      clean window close (`out/m5-acceptance/summary.json`) — and `-Replay` played
      the song a second time inside one process. Run it with
      `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance_song.ps1`
      (≈ 20 minutes for three runs).

## Handoff to Milestone 6

Entry state of [`06-reproducible-release.md`](./06-reproducible-release.md) was
seeded on 2026-09-19 with what M6 already inherits (acceptance is scripted as of
2026-09-20 for one song, but there is no fingerprint/log-identity enforcement and
no `toolchain.md`). What is still owed once this milestone finishes:

- the chosen song used for the acceptance test: **"These Days" (song list row
  3)**;
- any known graphics/audio quirks that must be documented for the release;
- the exact smoke-test route used: launch → A → A → A → main menu `PLAY` → A →
  song list → left stick to row 3 → A → how-to-play card → A → gameplay →
  results → `CONTINUE`.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
