# Known issues and standing limitations

Enduring facts only. The **chronology** — what was tried, in what order, and what
the logs said — lives in [bringup-log.md](bringup-log.md); this file is the index
of things that are still true and will still be true next session.

Issue IDs (`B-00N`) are allocated in `bringup-log.md`. A merged or disproved issue
is removed from this file, not from the log.

## Open

| Issue | What it is | Where |
| --- | --- | --- |
| **B-006 (class, not closed)** | Functions found only through an indirect call — no PDATA, no static `bl`, or a hole inside a run of 8-byte adjuster thunks — are missing from the register table and appear at runtime as `[FATAL] Call to invalid or unregistered function at guest address 0x…`. Fix = add `[functions."0x…"]` (no `size`/`end`) to [config/functions.toml](../config/functions.toml), re-run codegen, rebuild. **24 addresses registered so far; 15 of them on 2026-09-19** — one comparator whose address is taken in data, plus a scan for the thunk-table hole pattern (14 hits). Each newly reached UI or gameplay path can still expose another one. | [bringup-log.md](bringup-log.md) |
| **B-009 (resolved, then corrected)** | Music was silent because the `xboxkrnl` `XeKeysSetKey`/`XeKeysAesCbc` layer is a `REX_EXPORT_STUB` no-op in ReXGlue; [src/hooks/crypto.cpp](../src/hooks/crypto.cpp) intercepts both imports and forwards to `XeCryptAesKey`/`XeCryptAesCbc`. Corrected on 2026-09-19: the guest installs with a **constant key id** (`0xE0`) and carries the MOGG version in the *buffer offset* (`&table[GetEncMethod(version) * 16]`), so the plaintext entry selected by the offset is what must land in the slot. Music plays. That selection rule is now pinned by host unit tests ([tests/crypto_keytable_tests.cpp](../tests/crypto_keytable_tests.cpp)) that need no boot. | [bringup-log.md](bringup-log.md) B-009 |
| **B-010 (fix in place; two standing limits)** | 32-bit guest texture formats (`k_32`, `k_32_32`, `k_32_32_32_32`) with `num_format = 0` are 0.32 fixed point — they have no samplable host format, and without [patch 0002](../patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch) they sample `(0,0,0,0)` and make alpha-blended 3D invisible (this is what hid the note highway). The patch converts them on the CPU into an upload-pool staging buffer. Still true: `num_format = 1` (integer) textures fail to create, and one converted texture plus its mips must fit a single 2 MiB upload page (largest seen so far: 256 KB). | [bringup-log.md](bringup-log.md) B-010 |
| **Frame pacing and input polling have four standing limits** | (1) **One long stall per song survives tracing on**: the accepted vsync-on run has a single 1174.9 ms `VdSwap` gap (263.6 ms in `XE_SWAP`), the vsync-off run 254.3 / 245.8 ms; no threshold asserts on the maximum, and the 250 ms poll-gap bound is the one the tail actually approaches (142.1 ms and 243.5 ms). (2) **Injected keys depend on the game window holding the foreground**: `rexglue-sdk/src/input/mnk/mnk_input_driver.cpp` builds the pad state from the window's `WM_KEYDOWN`/`WM_KEYUP` and clears it on focus loss, so a press stolen by another window never reaches the guest. The driver samples the foreground across the whole hold and repeats an unclean press, and [scripts/audit_pacing_input.ps1](../scripts/audit_pacing_input.ps1) discards rather than asserts those, but the numbers are only as good as a machine left alone. (3) **A press shorter than a poll gap can be invisible**: of the three probes realized below 25 ms, two were seen and one fell between polls — that is how a polled pad behaves rather than a defect, and a human press is an order of magnitude longer. (4) **vsync off is host-load dependent**: the recorded run measures 68.3 fps where an earlier run of the same route (before the press-delivery change) measured 62.9 fps, so an uncapped frame rate is a property of that hour's machine as much as of the guest. | [bringup-log.md](bringup-log.md) "Milestone 5 close-out" |
| **Most booted behaviour is still verified by hand; host test coverage is four targets** | Fixes are still "verified once and forgotten" wherever proving them needs a boot. [tests/](../tests) covers the pure host code in four targets — `rb_blitz_crypto_keytable_tests` (the B-009 key path through [src/hooks/crypto_keytable.h](../src/hooks/crypto_keytable.h)), `rb_blitz_payload_overlay_tests`, `rb_blitz_path_policy_tests` (the writable-root override decision of [src/fs/path_policy.h](../src/fs/path_policy.h), added 2026-09-20 after the overrides turned out to be silently discarded) and `rb_blitz_fingerprint_tests` (the parser and verdicts behind [src/util/game_fingerprint.h](../src/util/game_fingerprint.h)), 522 checks in total, all under `ctest` in the configured build tree. Three scripted acceptance runs cover the offline UI route and storage: [scripts/acceptance_launches.ps1](../scripts/acceptance_launches.ps1) stops at the title screen, [scripts/acceptance_song.ps1](../scripts/acceptance_song.ps1) drives launch → a named song → results (and, with `-Replay`, a second song), and [scripts/acceptance_persistence.ps1](../scripts/acceptance_persistence.ps1) runs the five storage cases against an isolated `--user_data_root`; a fourth pair measures the two things no route check can see — [scripts/measure_pacing_input.ps1](../scripts/measure_pacing_input.ps1) plays one named song with timed injected presses while SDK patch 0004 traces frames and polls, and [scripts/audit_pacing_input.ps1](../scripts/audit_pacing_input.ps1) turns the resulting ~74 MB log into a 17-check verdict (its `-SelfTest` synthesizes a good and a broken run and asserts the verdicts differ); all of them keep the screenshots and the log of every run. Everything they do not visit still needs a boot: every hook in [src/hooks/](../src/hooks) that touches the SDK, pause/resume, and the UI paths outside the offline song loop. Pad input is the one item verified by hand instead of scripted — the author exercised navigation, accept/back, pause and lane controls on a controller on 2026-09-20 (see [bringup-log.md](bringup-log.md)), while every scripted route injects keys. | [build-and-run.md](build-and-run.md) §3–§4, [rb3-references.md](rb3-references.md) §7.3 |
| **`longjmp_address` / `setjmp_address` unset** | The 0.10 SDK supports both at `[entrypoint]` level and `band3_recomp` sets them; [rb_blitz_manifest.toml](../rb_blitz_manifest.toml) sets neither, so guest `longjmp`/`setjmp` has no host bridge. | [rb3-references.md](rb3-references.md) §8 |
| **Hooks carry no "why is this disabled" record** | A hook that early-returns is a bug unless it is documented as a deliberate deviation. Same failure class as the RB3 port's "faithful behaviour switched off" audit. | [rb3-references.md](rb3-references.md) §7.3, §9 |
| **Codegen `GapFill` candidate fix (not applied)** | `rexglue-sdk/src/codegen/phase_gapfill.cpp` `splitRegionOnTerminators` should also split on `bctr` when the following word is a known function entry. Upstream fix, plan fix-order #6. | [symbols.md](symbols.md) |
| **Local SDK patches are applied inside the SDK working tree** | Four patches: `rexglue-sdk/src/system/xthread.cpp` (pinned SDK `c94f5ebd`) logs the "Too few processor cores - scheduling will be wonky" warning **once per process** instead of on every thread creation and CPU migration, which flooded the log (~580 KB per boot) — [patches/rexglue-sdk/0001-xthread-log-once-core-count-warning.patch](../patches/rexglue-sdk/0001-xthread-log-once-core-count-warning.patch); the 32-bit fixed-point texture support of B-010 — [patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch](../patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch); and the `xboxkrnl_io.cpp` tracing gap — [patches/rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch](../patches/rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch): `NtWriteFile_entry` and `NtReadFileScatter_entry` had no `REXKRNL_IMPORT_TRACE`/`REXKRNL_IMPORT_RESULT` call at all (unlike `NtReadFile_entry`), so a trace-level log listed every guest read and **no** guest write — indistinguishable from "the title never writes" — and the write trace is what makes the persistence evidence in [bringup-log.md](bringup-log.md) possible; and the frame/poll tracing gap — [patches/rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch](../patches/rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch): a vanilla log prints **no** fps, frame-time or poll-rate line at all, so the frame pacing and input polling claims of [bringup-log.md](bringup-log.md) had no possible source; it adds `[VdSwap]` (the guest's frame submission), `[XE_SWAP]` (the packet that reached the host as a present) and `[XamInputGetState]` (one poll, with the button word the guest was handed) behind the same noisy-trace gate the other traces use. All four are re-applied after a checkout by `scripts/apply_sdk_patches.ps1`. The submodule therefore always reports ` M` on those files; that is expected, not drift. `.gitmodules` marks the submodule `submodule.rexglue-sdk.ignore = dirty` so the parent repository stays clean, and `scripts/apply_sdk_patches.ps1` audits the SDK work tree (exit 1 on any edit the patch set does not account for) to compensate for what that hides. Upstream candidates. | [patches/README.md](../patches/README.md) |
| **SDK symlinks are flattened — `lzxd.c` can break the build** | 16 tracked symlink entries under `rexglue-sdk/thirdparty/` (15 `libmspack/cabextract/mspack/*`, 1 o1heap `CLAUDE.md`) exist as regular files here: the machine cannot create symlinks (not elevated, Developer Mode off) and the submodule repos inherit `core.symlinks=false` from the system gitconfig. The SDK **compiles** `cabextract/mspack/lzxd.c`, so a checkout that writes link text instead of source (29 bytes vs 30,796) fails to compile. Setting `core.symlinks true` in the submodules is *not* a fix — checkout then fails outright (`Function not implemented`, exit 255, file missing). `scripts/repair_flat_symlinks.ps1` re-expands the files (idempotent) and marks them `--skip-worktree`. | [bringup-log.md](bringup-log.md) B-001, [patches/README.md](../patches/README.md) |
| **9 SDK submodules are uninitialised** | `catch2`, `glslang`, `moltenvk`, `spirv-headers`, `spirv-tools`, `vulkan-headers`, `vulkan-loader`, `vulkan-memory-allocator` and `simde/test/munit` are absent; the D3D12 build path used so far does not need them (milestones 1–3 configured and built without them). If a configure starts asking for one: `git -C rexglue-sdk submodule update --init --recursive thirdparty/<name>`. | `git -C rexglue-sdk submodule status` |

## Accepted (by design, not bugs)

- Offline services stay honest `STUB`s — `XamVoiceSetMicArrayIdleUsers`,
  `XeKeysSetKey`, `XeKeysAesCbc` are **not** faked to success (B-009 is the one
  deliberate exception, and it is a real implementation, not a fake).
- 32-bit **integer** textures (`num_format = 1`) deliberately fail to create
  instead of being sampled as if they were normalized data: a wrong texture is
  harder to notice than a missing one. B-010's conversion covers
  `num_format = 0` (fixed point) only.
- A **corrupt settings payload is loaded as-is.** With
  `B13EBABEBABEBABE\5841122D\00000001\globaloptions\globaloptions` overwritten by
  `0xAB`, the title still reaches the offline menus, logs nothing about the
  content, and neither rejects nor rewrites the file (2026-09-20,
  `scripts/acceptance_persistence.ps1 -Only corrupt-payload`). A corrupt content
  **header** (`…\Headers\00000001\globaloptions.header`) is treated differently:
  the title recreates the 1024-byte default payload instead of reading it. Both
  stay non-fatal and leave the other content files alone. Nothing here is a
  bring-up shortcut — it is the title's own validation, which we neither add to
  nor emulate.
- `update:\gen\patch_xbox.hdr` → `0xc000000f`: no title update in this dump, and
  request that retail Blitz makes on every boot. (With an Ultimate payload
  installed this request is re-pointed at the merged `d:` view; see
  [ultimate-compat.md](ultimate-compat.md).)
- Three key/voice `STUB` warnings at boot.
- Online features (Rock Central sign-in, achievements, leaderboards, challenges,
  store/DLC enumeration) stay unavailable. This is **permanent, not a bring-up
  shortcut**: Blitz ships its own offline mode, so restoring online behaviour was
  never required for a playable port, and unblocking it is the community mod's job
  rather than ours. Our obligation is the opposite direction — the community's
  **Rock Band Blitz Ultimate** payload must keep working by drag-and-drop. What that
  means, which hazards an Ultimate payload brings (`update:`/TU5, overlay
  fingerprinting, replaced UI data), and what we refuse to ship are in
  [ultimate-compat.md](ultimate-compat.md).
- The project is **GPL-2.0-only**, deliberately not "or later": GPL-2.0 is what
  keeps us compatible with the GPL-2.0 Rock Band 3 recompilation project we adapt
  code from. Do not relicense, and do not add GPL-3.0-only or otherwise
  incompatible third-party code ([README.md](../README.md)).
- Borrowed code must carry provenance. Anything adapted from another project
  keeps its original license, says so in a header comment, and gets a row in the
  provenance table in [rb3-references.md](rb3-references.md) §9.
- The **build** refuses a `game/` that is not the fingerprinted dump, but a
  **runtime** game-data root that differs is only logged, never fatal: an Ultimate
  payload is a supported content variant chosen at launch
  (`--game_data_root`, `--ultimate_mode`), and refusing to start would turn
  compatibility into a bug report. `-DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON` relaxes
  the build half for the same case — [build-and-run.md](build-and-run.md) §3,
  [ultimate-compat.md](ultimate-compat.md).
