# Rock Band Blitz static recompilation plan

## Goal

Produce a reproducible native build of the Xbox 360 version of **Rock Band Blitz** with ReXGlue. The first release is a bring-up release: it should reliably launch, reach the offline song flow, and complete one bundled song with usable graphics, audio, controller input, and local persistence.

Correctness fixes, performance work, visual upgrades, broad DLC compatibility, and quality-of-life features are later projects. Restoring or unblocking online behavior is **not** one of them at all: the retail title ships an offline mode, so that work was never needed for a playable port, and it belongs to the community's Rock Band Blitz Ultimate mod rather than here. Our obligation on that front is compatibility — an installed payload dropped next to a vanilla dump must run on the same executable ([docs/ultimate-compat.md](docs/ultimate-compat.md)). During bring-up, make only the smallest change needed to pass the next milestone.

This work assumes contributors use game files dumped from a copy they are authorized to use. Retail binaries, archives, music, keys, and other copyrighted game data must not be committed or redistributed. The project's own source is licensed **GPL-2.0-only** (`LICENSE`), a choice made so that code can be adapted from the GPL-2.0 recompilation projects for the same engine; see `README.md` for what the license permits and what it does not.

## Current baseline

- Title: Rock Band Blitz, Xbox Live Arcade title ID `5841122D`.
- Available entrypoint: `game/default.xex`.
- Available base content: `game/gen/main_xbox.hdr` and `game/gen/main_xbox_0.ark`.
- Available SDK: ReXGlue v0.10.0, commit `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.
- No recompilation project, manifest, generated source, or known-good trace exists yet.
- Historical Xenia testing reaches menus but reports missing gameplay track/background rendering. Treat graphics/readback behavior as a known investigation area, not as a conclusion about ReXGlue.
- The original online services are gone. The bring-up target is deliberately offline and must not depend on Xbox Live or Rock Central — permanently, not just during bring-up. The two content variants we support (a vanilla dump, and a Rock Band Blitz Ultimate payload dropped next to one) and what compatibility with the latter means are defined in [docs/ultimate-compat.md](docs/ultimate-compat.md).

## Definition of “working” for the first release

All of these must pass on a clean Windows AMD64 checkout:

1. The project configures and builds with the pinned SDK and documented prerequisites.
2. Code generation finishes without ignored fatal analysis errors. Any deliberate exception is documented by guest address and reason.
3. The executable finds a user-supplied game dump without requiring files inside the source tree.
4. The game reaches the main menu in offline mode.
5. A normal controller can navigate menus and control gameplay.
6. At least one bundled song can be selected, started, played to its results screen, and replayed.
7. The note highway and required gameplay objects render correctly enough to play.
8. Music and sound effects play without game-breaking stalls or gross loss of synchronization.
9. Required local settings/save data survive a restart.
10. Failure produces a useful log containing the SDK version, game fingerprint, last guest PC/function, and missing import or assertion where applicable.

The first two fields no longer depend on a failure (2026-09-19): every boot logs `boot identity:` with the SDK build stamp and `game data identity:` with the `default.xex` digest, or a `MODIFIED` warning naming the values in [config/game_fingerprints.toml](config/game_fingerprints.toml) — [docs/build-and-run.md](docs/build-and-run.md) §4. The remaining two are logged by the code paths that detect them, as before.

This definition intentionally does not require online services, leaderboards, power-up service behavior, achievements, every song/DLC package, multiplayer, non-Windows hosts, perfect timing, or polished presentation.

It also covers **only the vanilla content variant** as an acceptance condition. Running a Rock Band Blitz Ultimate install as the game-data root has its own acceptance criteria, not a condition of this release: [docs/ultimate-compat.md](docs/ultimate-compat.md). (The install route itself is implemented — see B-012 in [docs/bringup-log.md](docs/bringup-log.md).)

## Architecture

### Ownership boundaries

Keep four layers separate:

| Layer | Owns | Rule |
| --- | --- | --- |
| Game input | Locally dumped XEX/content | Read-only, untracked, fingerprinted |
| ReXGlue SDK | Xbox kernel/runtime, VFS, input, audio, graphics, PPC codegen | Pin a revision; avoid title-specific edits |
| Generated code | Mechanical translation of each guest binary | Regenerate; never hand-edit |
| Project code | Manifest/hints, app policy, native hooks, patches, diagnostics | Reviewable source of all Rock Band Blitz knowledge |

Title-specific behavior belongs in this repository's project layer. Change the SDK only when the behavior is generically wrong for Xbox 360 software and could reasonably be contributed upstream. This prevents a future SDK update from swallowing game fixes.

### Proposed repository layout

```text
.
├── CMakeLists.txt                    # Thin target definition from rexglue init
├── CMakePresets.json                 # Reproducible supported build presets
├── DECOMPILATION_PLAN.md
├── LICENSE                           # Project license: GPL-2.0-only
├── README.md                         # Setup, legal input requirements, build/run
├── rb_blitz_manifest.toml            # Project, entrypoint, module/output mapping
├── config/
│   ├── codegen.toml                  # Stable codegen flags/tuning
│   ├── functions.toml                # Confirmed boundaries, chunks, names
│   ├── jump_tables.toml              # Manual switch-table corrections
│   ├── analysis_hints.toml           # Invalid data ranges, indirect calls, EH hints
│   ├── rexcrt.toml                   # Verified guest CRT address mappings
│   └── midasm_hooks.toml             # Instruction hooks only when unavoidable
├── src/
│   ├── main.cpp                      # REX_DEFINE_APP only
│   ├── rb_blitz_app.h
│   ├── rb_blitz_app.cpp              # ReXApp lifecycle/path policy
│   ├── hooks/                        # Whole-function native overrides by subsystem
│   │   ├── filesystem.cpp
│   │   ├── network.cpp
│   │   ├── platform.cpp
│   │   └── diagnostics.cpp
│   └── patches/                      # Named, justified guest-memory patches
│       ├── patches.h
│       └── patches.cpp
├── metadata/                         # Redistributable project metadata only
├── patches/                          # Local SDK fixes kept as patch files
│   └── rexglue-sdk/                  #   applied by scripts/apply_sdk_patches.ps1
├── scripts/                          # Deterministic configure/build/run helpers
├── tests/                            # Host unit tests (ctest); no SDK, no game image
├── docs/
│   ├── bringup-log.md                # Chronological milestone and blocker log
│   ├── build-and-run.md              # Rebuild / run / log-capture commands
│   ├── ultimate-compat.md            # Vanilla vs Ultimate content-variant policy
│   ├── known-issues.md
│   ├── rb3-references.md             # What the RB3 projects solved on this engine
│   ├── symbols.md                    # Important guest addresses and evidence
│   └── test-matrix.md
├── generated/                        # RexGlue-owned output; do not hand-edit
├── thirdparty/
│   └── rexglue-sdk/                  # Preferred pinned submodule location
├── game/                             # Local dump; ignored except optional README
└── out/                              # Build products, logs, captures; ignored
```

The SDK is currently at `rexglue-sdk/`. Before scaffolding, either move/re-add it as the pinned `thirdparty/rexglue-sdk` submodule above, or deliberately keep the current path and set `REXSDK_DIR` consistently. Do not leave it as an untracked nested Git repository.

### RexGlue integration rules

- Start from `rexglue init`; keep its manifest-first project shape, `ReXApp` subclass, generated `rexglue.cmake`, and CMake codegen target.
- Use the manifest to map the entrypoint and any guest DLLs. Each binary gets its own output directory.
- Use the manifest's `includes` support to split reverse-engineering knowledge by concern. The manifest remains a small index; address-heavy data lives in `config/`.
- Pin the SDK commit. Upgrade it in isolated commits, regenerate everything, and rerun the milestone test matrix.
- Keep local SDK fixes as patch files under `patches/rexglue-sdk/` and apply them with `scripts/apply_sdk_patches.ps1`; never commit inside the submodule, which would point the gitlink at a commit the upstream remote does not have. `.gitmodules` marks the submodule `submodule.rexglue-sdk.ignore = dirty` so an applied patch does not show as a modified submodule, and the applier audits the SDK work tree on every run to catch the edits that setting hides. See [patches/README.md](patches/README.md).
- Run `scripts/repair_flat_symlinks.ps1` on a fresh checkout: 16 tracked SDK symlinks cannot exist on Windows here, and one of them (`libmspack/cabextract/mspack/lzxd.c`) is compiled by the SDK.
- Treat `generated/` as a build artifact even if it is temporarily committed to make early builds practical. No fixes may live only there.
- Keep `src/main.cpp` trivial. Put path configuration in `OnConfigurePaths`, runtime/backend selection in `OnPreSetup`, loaded-image data patches in `OnPostLoadXexImage`, final guest patches in `OnPreLaunchModule`, and diagnostics/cleanup in the other `ReXApp` hooks.
- Prefer fixes in this order:
  1. correct function boundaries, chunks, jump tables, or analysis hints;
  2. use an existing ReXGlue kernel/runtime implementation;
  3. add a whole-function `REX_HOOK` override with the original guest address recorded;
  4. use a small named data/code patch;
  5. use a mid-ASM hook when an entire function cannot safely be replaced;
  6. modify the SDK only for a reusable runtime defect — and then record it as a patch file under `patches/`, not as an uncommitted submodule edit.
- Every hook or patch must state the guest address, observed failure, intended behavior, evidence, and the milestone that requires it.
- Do not use hooks to conceal unknown crashes. First capture the failing guest PC, call chain, registers, and relevant import/log messages.

## Work plan

> Each milestone below has a matching living agent prompt in
> [`prompts/`](prompts/README.md). Run them in order and edit the next prompts
> as new knowledge lands (see the prompts README for the editing loop).

### Milestone 0 — Make the baseline safe and reproducible

- [x] Add a root `.gitignore` for `game/`, `out/`, local logs/captures, user presets, and generated bulk output as decided below.
- [x] Pin ReXGlue v0.10.0 commit `c94f5eb...` as a submodule or document an equally reproducible SDK checkout.
- [x] Record SHA-256 hashes and sizes for the expected XEX/HDR/ARK in a local, non-copyrighted fingerprint file. Decide whether hashes are safe and useful to commit. (Committed as [`config/game_fingerprints.toml`](config/game_fingerprints.toml) and **enforced** since 2026-09-19: codegen refuses to run against a different `default.xex` unless the opt-out is set deliberately, and every boot logs which image it got.)
- [x] Record the title/media ID, executable version, region, and whether the dump includes an update.
- [x] Establish one baseline comparison run in current Xenia Canary and retain its config, log, and milestone observations locally. The commit-safe findings are in [`docs/baselines/xenia-canary-80679bc.md`](docs/baselines/xenia-canary-80679bc.md).
- [x] Generated-code policy: do not commit generated C++; reproduce it from the manifest and pinned SDK. Keep only `generated/rexglue.cmake`, which is required by the generated project scaffold.

Exit criterion: a fresh checkout can acquire the SDK deterministically, reject the wrong game revision by fingerprint, and cannot accidentally commit retail content. **Fingerprint half met** (2026-09-19): the build itself refuses to generate code for an image other than the one the fingerprint file describes ([docs/build-and-run.md](docs/build-and-run.md) §3).

### Milestone 1 — Scaffold and generate

- [x] Build the pinned ReXGlue CLI and run `rexglue init` with project name `rb_blitz`, `game/default.xex`, and `game/` as the game root.
- [x] Keep the generated CMake/ReXApp structure intact; only add the project source directories described above. (The ReXApp subclass is `src/rb_blitz_app.h`; every project source lives under `src/`.)
- [x] Point project presets at the pinned source SDK with `REXSDK_DIR` (or use an installed, exactly pinned package).
- [x] Run codegen once without `--force` and save the complete diagnostics.
- [x] Inventory sections, imports, discovered functions, unresolved direct/indirect targets, invalid instruction ranges, jump tables, and exception-handler patterns. (Recorded in the three codegen runs in [docs/bringup-log.md](docs/bringup-log.md) and [docs/symbols.md](docs/symbols.md): 262 imports, 132 jump tables, 0 ambiguous data regions, 3 tail-branch targets.)
- [x] Confirm whether the title loads guest DLLs. Add only observed modules with the exact canonical guest paths expected by `XexLoadImage`. (No guest DLLs: the manifest maps only `game/default.xex`, and a boot loads exactly one image.)

Exit criterion: `rb_blitz_codegen` completes repeatably and a second unchanged run is a no-op through RexGlue's stamp/depfile mechanism.

### Milestone 2 — Close analysis and compile the translation

- [x] Resolve failures in small address-sorted batches (the 3 tail-branch
      thunks were resolved in Milestone 1 via `config/functions.toml`).
- [x] Add verified function boundaries and discontinuous chunks to `functions.toml`.
- [x] Add manual switch tables only where automatic detection is demonstrably wrong
      (none needed — 132 tables auto-detected).
- [x] Classify embedded data as data; do not invent fake functions just to silence
      validation (0 data regions — nothing to classify).
- [x] Mark known indirect calls and exception handlers from disassembly evidence
      (none flagged by codegen).
- [x] Map ReXCRT functions only after their addresses and ABI are confirmed.
      The heap group must be complete if enabled (not enabled — not required).
- [x] Enable generated exception handlers only if this binary requires them, then
      verify the Windows async-exception build flags stay consistent with generated
      PCH/source files (not required; `-fasync-exceptions` already carried).
- [x] Compile all generated translation units with line-table debug information and
      fix every compile/link error at its source configuration or supported hook
      boundary (done; one build-config fix in project `CMakeLists.txt`, B-004).

Exit criterion: the native executable links with no hand-edited generated files and
no unexplained forced validation bypass. **Met** — `out/build/win-amd64-debug/rb_blitz.exe`.

### Milestone 3 — Reach guest entry and stable offline boot

- [x] Configure the game-data root separately from writable user/update/cache roots.
- [x] Verify VFS mounts and case/path normalization against every attempted open.
- [x] Implement or correct blocking Xbox kernel imports one at a time.
- [x] Capture the last guest PC/function on fatal exceptions and hangs.
- [x] Make unavailable network services fail quickly and faithfully enough for the title's existing offline path. Do not emulate a fake successful service unless the game cannot otherwise proceed and the exact contract is understood.
- [x] Verify worker threads, events, timers, and shutdown do not deadlock before moving forward.

Exit criterion: ten consecutive launches reach the title screen/offline prompt and close cleanly. **Met** — 10/10 (Release), boot to "PRESS A TO START".

### Milestone 4 — Menus, content discovery, input, and saves

- [x] Confirm the ARK/HDR files are read from the game-data root with correct offsets and sizes. (Both halves are covered. File-level: `rb_blitz_fingerprint --all` checks the sizes and SHA-256 of `gen/main_xbox.hdr` and `gen/main_xbox_0.ark` against [config/game_fingerprints.toml](config/game_fingerprints.toml). Runtime: [scripts/audit_ark_reads.ps1](scripts/audit_ark_reads.ps1) traces a run at `--log_noisy=true --log_level=trace`, attributes each `NtReadFile` to its file through the handle timeline, and fails on any read that misses its file, starts past EOF, or delivers the wrong byte count. Canonical song-play pass: 2420 reads, 2418 of them on the game-data root (2203 + 213 on the main/patch ARK, one each on the two headers, 158 MB delivered), 0 unattributed, 0 position-based, 0 violations (`out/m4-offsets/songplay/read-audit.md`, untracked build output). The checker was itself checked: pointed at a non-existent game root it reports 1865 violations and exits 1.)
- [x] Reach the main menu and enumerate bundled songs without an online dependency. (Offline menus, song list and song selection have worked since 2026-09-19; [scripts/acceptance_song.ps1](scripts/acceptance_song.ps1) now drives the whole route and names the selected song on a screenshot, and the offset/size half of the item above is audited. The content enumerators report 2 items and 0 DLC, matching the dump.)
- [x] Map one standard XInput controller and verify navigation, accept/back, pause, and lane controls. (Hand-verified 2026-09-20 with an `XInput Controller #1` (0x0B05/0x1B4C): menu navigation, accept/back, pause, and the lane controls during a song all worked. This is a hand check by construction — the scripted routes need to be repeatable headlessly and therefore inject keys through the SDK's MnK emulation — so it is recorded with its date rather than as an automated test; [docs/bringup-log.md](docs/bringup-log.md).)
- [x] Provide a deterministic local profile/storage response sufficient for offline use. (The writable root defaults to `%USERPROFILE%\Documents\rb_blitz` and is overridable with `--user_data_root`/`--cache_root`/`--update_data_root` and the matching `REX_*` variables — those overrides were silently discarded until the `std::filesystem::relative()` bug was fixed on 2026-09-20, with [tests/path_policy_tests.cpp](tests/path_policy_tests.cpp) as the guard. The layout itself is fixed by title id `5841122D`, content type `00000001` and XUID `0xB13EBABEBABEBABE`: `globaloptions` (1024 B), `songcache` (16 B), a 328-byte aggregate header each, and two shader-cache files — six files, 6288 bytes, byte-identical between runs. All of it is SDK content-path code, no project code, which is why this item needed proof rather than implementation.)
- [x] Verify settings/save creation, restart persistence, and behavior with missing/corrupt writable data. (Five cases in [scripts/acceptance_persistence.ps1](scripts/acceptance_persistence.ps1), 5/5 passing, each a fresh process with an isolated `--user_data_root`: `fresh` shows the title authoring both files itself (`globaloptions` 1024 B, `songcache` 16 B), `restart` reads them back byte-identical and writes nothing, `missing` re-authors the defaults, a corrupt payload is loaded as-is with no fatal and no repair, and a corrupt header makes the title rewrite the default payload. The writes are only attributable because patch 0003 added the missing `NtWriteFile` trace calls: `[NtWriteFile]` shows the title writing both payloads. `out/m4-persistence/summary.json`.)
- [x] Keep achievements, leaderboards, and downloadable-song enumeration disabled or gracefully unavailable unless they block the core loop. (The Rock Central sign-in attempt is refused and dismissible and the aggregate DLC enumerator returns 0 items, so the offline path is unaffected. This is the **final** behaviour for the vanilla route, not a bring-up shortcut — unblocking online features is not a milestone of ours; [docs/ultimate-compat.md](docs/ultimate-compat.md).)

Exit criterion: the user can launch, navigate, see bundled content, select a song, and return to the menu repeatedly. **Met** (2026-09-20) — the offline route, bundled content and song selection have worked since 2026-09-19, and [scripts/acceptance_song.ps1](scripts/acceptance_song.ps1) now drives launch → song → results in a fresh process per run and back to the song list again (`-Replay`), asserting each transition on a screenshot and the run's own log. The four items that were still open on 2026-09-19 are closed with evidence: the runtime ARK/HDR offset audit (2418 in-scope reads, 0 violations, plus a negative self-test that fails on a bad game root), a hand-verified pad pass over all four control groups, the deterministic storage layout, and the five-case persistence run. Only the pad input is a hand check rather than a script, because a headless script must inject keys; the rest is reproducible from the commands in [docs/bringup-log.md](docs/bringup-log.md).

### Milestone 5 — Complete one song

- [x] Get through song load without timeout, deadlock, or missing-file errors. (Only after the 15 additional guest addresses registered on 2026-09-19; song selection trapped before that.)
- [x] Verify the Xenos pipeline draws the highway, notes, HUD, and essential background objects. (B-010 fixed the 32-bit fixed-point textures. Two format limits remain — see [docs/known-issues.md](docs/known-issues.md).)
- [x] Compare failing draws against a Xenia capture, especially resolve/readback and render-target transitions implicated by historical compatibility reports. **Disposed** (2026-09-20, per the author): the Xenia build available here does not play *Blitz* at all, so it cannot be a reference for draw behaviour, and nothing a full song needs fails to draw on the D3D12 backend. The two texture-format limits stay recorded in [docs/known-issues.md](docs/known-issues.md).
- [x] Verify audio voices, sample formats, streaming, clocks, and pause/resume. **Disposed** (2026-09-20, per the author): the audible result is what the item was for, and full songs play with music, note and miss feedback all correct, so voices/formats/streaming/clocks get no separate measurement pass. Pause/resume is not covered by that and is claimed nowhere; the decrypt path underneath all of it stays pinned by [tests/crypto_keytable_tests.cpp](tests/crypto_keytable_tests.cpp) (B-009).
- [x] Measure audio/gameplay drift across a full song; defer fine calibration unless drift makes play impossible. **Disposed** (2026-09-20, per the author): the drift seen while playing is within what the title's own audio-calibration screen corrects — the screen the title provides for exactly this — so no milestone measurement is owed. The exit-criterion runs play 315–317 s of audio per song without drift becoming a blocker.
- [x] Verify frame pacing and input polling are stable enough for a complete run. (Measured 2026-09-20 — [patch 0004](patches/rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch) traces the guest's `VdSwap`, the host's `XE_SWAP` and every `XamInputGetState`; [scripts/measure_pacing_input.ps1](scripts/measure_pacing_input.ps1) plays one named song in a fresh process and injects 15 timed presses; [scripts/audit_pacing_input.ps1](scripts/audit_pacing_input.ps1) reduces the ~74 MB trace to a 17-check verdict, and `-SelfTest` proves that verdict fails a synthesized broken run. Over a 317 s window with vsync on: 18 736 `VdSwap` / 18 735 `XE_SWAP` frames (no missing or duplicate counter, `dn=1`), median present dt 16.72 ms (59.3 fps), p99 22.62 ms, 23 hitches (0.12%); 127 498 successful polls at 402.1/s with a 4.10 ms median and 6.89 ms p99 gap; every asserted press reached the guest, worst key-down-to-visibility 47.0 ms. With vsync off the same route runs 21 579 frames in 316 s — 68.3 fps, median 14.34 ms — so the guest is not vblank-locked when it is allowed to run; both modes pass every check they share, and detail plus the honest limits are in [docs/bringup-log.md](docs/bringup-log.md).)
- [x] Reach results, return to song select, and play again without leaked state or a crash. (2026-09-20, `-Replay`: the results screen returns to the song list inside the same process, the same row is reselected, and the second song reaches its own results screen with no `[FATAL]` and a clean exit; `scripts/acceptance_song.ps1`.)

Exit criterion: one named bundled song passes the full launch-to-results path three times in a row from a clean process. **Met** (2026-09-20) — [`scripts/acceptance_song.ps1`](scripts/acceptance_song.ps1) drives the offline route in a fresh process per run and asserts each transition on a screenshot plus the run's own log; three consecutive runs passed (playback envelopes 315 / 317 / 317 s, a results screen naming `THESE DAYS`, no `[FATAL]`, clean window close, `out/m5-acceptance/summary.json`), and a `-Replay` run played the song twice inside one process. **Milestone 5 is closed** (2026-09-20): of the four bullets that were open, frame pacing and input polling are measured and pass in both vsync modes ([scripts/measure_pacing_input.ps1](scripts/measure_pacing_input.ps1) + [scripts/audit_pacing_input.ps1](scripts/audit_pacing_input.ps1)), and the other three — the Xenia draw comparison, the audio checklist, and drift — are disposed with the author's dated rationale rather than deferred; see "Milestone 5 close-out" in [docs/bringup-log.md](docs/bringup-log.md).

### Milestone 6 — Reproducible bring-up release

- [ ] Add a one-command documented configure/build flow and a clear runtime data-path argument.
- [ ] Run Debug and Release smoke tests on a clean checkout.
- [ ] Test wrong/missing game data and confirm the error is actionable. (Build-side done 2026-09-19: the gate fails closed and prints the digest it found next to the one expected. The runtime half — a launcher pointing a built `rb_blitz.exe` at the wrong root — warns and continues by design.)
- [ ] Freeze the minimal supported SDK/game fingerprints and compiler versions. (The game fingerprint is frozen in [config/game_fingerprints.toml](config/game_fingerprints.toml) and checked at build time; the SDK pin and compiler paths still need `toolchain.md`.)
- [ ] Document remaining issues and explicitly move non-blockers to the post-bring-up backlog.
- [ ] Ensure a distributable build contains no retail game data, symbols derived from proprietary databases, credentials, or machine-specific paths.

Exit criterion: another authorized developer can follow the README from a clean checkout and reproduce the working acceptance test.

## Bring-up loop

For each blocker, use the same short loop:

1. Reproduce from the nearest saved milestone with one deterministic action sequence.
2. Save the full log and identify the first bad event, not merely the final crash.
3. Record guest PC/function, thread, relevant registers, import/VFS request, and expected behavior from hardware or Xenia where available.
4. Classify it as analysis, kernel/runtime, filesystem/content, graphics, audio, input, network/platform, or game logic.
5. Apply the narrowest fix at the ownership boundary above.
6. Re-run the current milestone test and all earlier smoke tests.
7. Add the result to `docs/bringup-log.md`; promote enduring limitations to `docs/known-issues.md`.

Commands for the rebuild → run → log-capture steps of this loop are in
[`docs/build-and-run.md`](docs/build-and-run.md).

Suggested blocker entry:

```markdown
### B-000: short description

- Status: open | investigating | fixed | deferred
- Milestone/repro: ...
- Game fingerprint: ...
- SDK commit: ...
- Guest PC/function/thread: ...
- First bad event: ...
- Evidence/reference behavior: ...
- Fix layer and rationale: ...
- Regression check: ...
```

## Verification strategy

- Use a fixed smoke-test route: boot -> offline path -> main menu -> song select -> chosen bundled song -> results -> menu -> quit.
- Maintain milestone save points or deterministic local state where possible.
- Use structured log categories and avoid permanent high-volume per-instruction logging.
- Keep Debug assertions enabled during bring-up; use RelWithDebInfo for long gameplay runs.
- Add small host-side tests for project-owned path conversion, patch preconditions, save serialization, and service-response parsing. The first of these is in place (2026-09-19): `tests/` holds `crypto_keytable`, a dependency-free unit test for the B-009 key path, built and run by `ctest`. The rest are still open.
- Guard every binary patch with an expected-byte/value check so a different game revision fails closed.
- When comparing with Xenia or hardware, record exact versions and settings; screenshots alone are not sufficient evidence for timing or state bugs.

## Known risks to investigate early

| Risk | Early check | Bring-up response |
| --- | --- | --- |
| Wrong or incomplete retail dump | Fingerprint gate before codegen; runtime identity line in the log; ARK/HDR read audit | Stop with a clear error — automatic since 2026-09-19 in the build (mismatch or unreadable fingerprint file fails codegen) and logged at boot |
| Dead online-service dependency | Run offline baseline and log service calls | Preserve/force the legitimate offline failure path; never emulate a fake service — online behavior is the Ultimate mod's problem, not ours ([docs/ultimate-compat.md](docs/ultimate-compat.md)) |
| Ultimate or title-update content in the data root | Run the vanilla smoke route against an installed Rock Band Blitz Ultimate payload | Keep the vanilla `default.xex` as the codegen input, union the payload over the data root at boot, and fix behaviour deltas in the project layer ([docs/ultimate-compat.md](docs/ultimate-compat.md)) |
| Undiscovered PPC targets or EH funclets | Codegen validation plus crash-PC correlation | Layered function/analysis hints |
| Missing kernel exports | Import inventory before first launch | Implement generic behavior in SDK or narrow project hook |
| Gameplay graphics absent | Capture first song's draw/resolve sequence | Correct reusable Xenos behavior; avoid game-specific rendering hacks |
| Streamed music stalls or drifts | Full-song clock/voice trace | Fix audio/runtime contract needed for playability |
| Profile/save assumptions | Boot with empty writable root | Minimal deterministic offline profile/storage behavior |
| SDK churn | Pin and upgrade separately | Regenerate and run the entire milestone matrix |

## Deferred until after “working”

- Pixel-perfect graphics, high-resolution/UI upgrades, unlocked frame rates, latency tuning, and performance optimization.
- Restoring, replacing or emulating Rock Central, leaderboards, challenges, social features, online multiplayer, or any other online service. Out of scope permanently: that work belongs to the Rock Band Blitz Ultimate mod and our only obligation is to stay compatible with it ([docs/ultimate-compat.md](docs/ultimate-compat.md)).
- Reworking server-dependent progression, coins, power-ups, or achievements beyond what is necessary to play offline.
- **Rock Band Blitz Ultimate compatibility** — a drag-and-drop payload dropped next to the game root and unioned over it by the same executable, with the vanilla route unaffected. The install route is delivered (2026-09-20, B-012); what remains is variant-specific behaviour polish beyond the acceptance criteria, hazards (TU5/`update:`, overlay fingerprinting, replaced UI data) and the refusal list in [docs/ultimate-compat.md](docs/ultimate-compat.md).
- Exhaustive DLC/export/custom-song compatibility and content-management UI.
- Multiple controller backends, keyboard bindings, handheld tuning, Linux/macOS/ARM support, packaging, installers, and auto-update.
- **VR (Meta Quest) and iOS** — scoped, not planned: [docs/vr-port-plan.md](docs/vr-port-plan.md). Nothing there gates a milestone in this file.
- **In-game Audio/Video settings** — scoped, not planned: [docs/av-settings-plan.md](docs/av-settings-plan.md). Makes host settings (resolution, v-sync, volume, safe area) adjustable from inside the game's own Audio/Video screen instead of the F4 overlay or `rb_blitz.toml`. Nothing there gates a milestone in this file.
- **Custom button mapping** — scoped, not planned: [docs/button-mapping-plan.md](docs/button-mapping-plan.md). Emulator-style per-action binding that captures a controller *or* keyboard press, layered above the guest's input boundary so the game's four controller presets keep working untouched. This is the "keyboard bindings" item above given a design; the rest of that bullet (controller backends, handheld tuning, packaging) stays deferred as written.
- Large source cleanup, symbol-name campaigns unrelated to blockers, broad native rewrites, and mod APIs.

## First execution sequence

1. Commit only this plan and repository hygiene; keep `game/` out of Git.
2. Pin/normalize the SDK dependency and build its `win-amd64-release` preset.
3. Run the local v0.10.0 CLI roughly as follows (adjust the executable path for the selected SDK layout):

   ```powershell
   rexglue init --project-name rb_blitz `
     --xex-path game/default.xex `
     --game-root game `
     --project-root .
   ```

4. Review the generated manifest before codegen; do not enable `--scan-dll` unless the game root actually contains required DLL modules.
5. Configure the generated `win-amd64-debug` project preset with the pinned `REXSDK_DIR`.
6. Run only the `rb_blitz_codegen` target, archive its first diagnostics, and turn those diagnostics into the initial address-sorted bring-up backlog.

## References

- [ReXGlue repository and current SDK warning](https://github.com/rexglue/rexglue-sdk)
- [ReXGlue Getting Started](https://github.com/rexglue/rexglue-sdk/wiki/Getting-Started)
- [ReXGlue runtime/application lifecycle](https://github.com/rexglue/rexglue-sdk/wiki/ReXApp)
- [ReXGlue function overrides](https://github.com/rexglue/rexglue-sdk/wiki/Function-Overrides)
- [ReXGlue mid-ASM hooks](https://github.com/rexglue/rexglue-sdk/wiki/Mid-ASM-Hooks)
- [Historical Xenia Rock Band Blitz compatibility report](https://github.com/xenia-project/game-compatibility/issues/1533)

Because ReXGlue is pre-1.0 and the checked-out v0.10.0 source is newer than parts of its public wiki, the pinned local headers, templates, and CLI `--help` output are authoritative when documentation disagrees.

