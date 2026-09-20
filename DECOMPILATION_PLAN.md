# Rock Band Blitz static recompilation plan

## Goal

Produce a reproducible native build of the Xbox 360 version of **Rock Band Blitz** with ReXGlue. The first release is a bring-up release: it should reliably launch, reach the offline song flow, and complete one bundled song with usable graphics, audio, controller input, and local persistence.

Correctness fixes, performance work, visual upgrades, broad DLC compatibility, and quality-of-life features are later projects. Restoring or unblocking online behavior is **not** one of them at all: the retail title ships an offline mode, so that work was never needed for a playable port, and it belongs to the community's Rock Band Blitz Deluxe mod rather than here. Our obligation on that front is compatibility — a Deluxe install dropped over a vanilla dump must run on the same executable ([docs/deluxe-compat.md](docs/deluxe-compat.md)). During bring-up, make only the smallest change needed to pass the next milestone.

This work assumes contributors use game files dumped from a copy they are authorized to use. Retail binaries, archives, music, keys, and other copyrighted game data must not be committed or redistributed. The project's own source is licensed **GPL-2.0-only** (`LICENSE`), a choice made so that code can be adapted from the GPL-2.0 recompilation projects for the same engine; see `README.md` for what the license permits and what it does not.

## Current baseline

- Title: Rock Band Blitz, Xbox Live Arcade title ID `5841122D`.
- Available entrypoint: `game/default.xex`.
- Available base content: `game/gen/main_xbox.hdr` and `game/gen/main_xbox_0.ark`.
- Available SDK: ReXGlue v0.10.0, commit `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.
- No recompilation project, manifest, generated source, or known-good trace exists yet.
- Historical Xenia testing reaches menus but reports missing gameplay track/background rendering. Treat graphics/readback behavior as a known investigation area, not as a conclusion about ReXGlue.
- The original online services are gone. The bring-up target is deliberately offline and must not depend on Xbox Live or Rock Central — permanently, not just during bring-up. The two content variants we support (a vanilla dump, and a Rock Band Blitz Deluxe install over one) and what compatibility with the latter means are defined in [docs/deluxe-compat.md](docs/deluxe-compat.md).

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

This definition intentionally does not require online services, leaderboards, power-up service behavior, achievements, every song/DLC package, multiplayer, non-Windows hosts, perfect timing, or polished presentation.

It also covers **only the vanilla content variant**. Running a Rock Band Blitz Deluxe install as the game-data root is a separate post-bring-up compatibility goal with its own acceptance criteria, not a condition of this release: [docs/deluxe-compat.md](docs/deluxe-compat.md).

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
│   ├── deluxe-compat.md              # Vanilla vs Deluxe content-variant policy
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
- [x] Record SHA-256 hashes and sizes for the expected XEX/HDR/ARK in a local, non-copyrighted fingerprint file. Decide whether hashes are safe and useful to commit.
- [x] Record the title/media ID, executable version, region, and whether the dump includes an update.
- [x] Establish one baseline comparison run in current Xenia Canary and retain its config, log, and milestone observations locally. The commit-safe findings are in [`docs/baselines/xenia-canary-80679bc.md`](docs/baselines/xenia-canary-80679bc.md).
- [x] Generated-code policy: do not commit generated C++; reproduce it from the manifest and pinned SDK. Keep only `generated/rexglue.cmake`, which is required by the generated project scaffold.

Exit criterion: a fresh checkout can acquire the SDK deterministically, reject the wrong game revision by fingerprint, and cannot accidentally commit retail content.

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

- [ ] Confirm the ARK/HDR files are read from the game-data root with correct offsets and sizes.
- [x] Reach the main menu and enumerate bundled songs without an online dependency. (Offline menus, song list and song selection are working as of 2026-09-19; the offset/size half of the item above is still unaudited.)
- [ ] Map one standard XInput controller and verify navigation, accept/back, pause, and lane controls.
- [ ] Provide a deterministic local profile/storage response sufficient for offline use.
- [ ] Verify settings/save creation, restart persistence, and behavior with missing/corrupt writable data.
- [x] Keep achievements, leaderboards, and downloadable-song enumeration disabled or gracefully unavailable unless they block the core loop. (The Rock Central sign-in attempt is refused and dismissible and the aggregate DLC enumerator returns 0 items, so the offline path is unaffected. This is the **final** behaviour for the vanilla route, not a bring-up shortcut — unblocking online features is not a milestone of ours; [docs/deluxe-compat.md](docs/deluxe-compat.md).)

Exit criterion: the user can launch, navigate, see bundled content, select a song, and return to the menu repeatedly. **Partially met** (2026-09-19) — the offline path, bundled content and song selection work, but XInput lane/pause verification, storage restart persistence, and the ARK/HDR + fingerprint audit are still open.

### Milestone 5 — Complete one song

- [x] Get through song load without timeout, deadlock, or missing-file errors. (Only after the 15 additional guest addresses registered on 2026-09-19; song selection trapped before that.)
- [x] Verify the Xenos pipeline draws the highway, notes, HUD, and essential background objects. (B-010 fixed the 32-bit fixed-point textures. Two format limits remain — see [docs/known-issues.md](docs/known-issues.md).)
- [ ] Compare failing draws against a Xenia capture, especially resolve/readback and render-target transitions implicated by historical compatibility reports.
- [ ] Verify audio voices, sample formats, streaming, clocks, and pause/resume.
- [ ] Measure audio/gameplay drift across a full song; defer fine calibration unless drift makes play impossible.
- [ ] Verify frame pacing and input polling are stable enough for a complete run.
- [ ] Reach results, return to song select, and play again without leaked state or a crash.

Exit criterion: one named bundled song passes the full launch-to-results path three times in a row from a clean process. **Not yet recorded** (2026-09-19) — full songs have been played through several times by observation, but there is no captured, repeatable acceptance run; there is also no script for one yet.

### Milestone 6 — Reproducible bring-up release

- [ ] Add a one-command documented configure/build flow and a clear runtime data-path argument.
- [ ] Run Debug and Release smoke tests on a clean checkout.
- [ ] Test wrong/missing game data and confirm the error is actionable.
- [ ] Freeze the minimal supported SDK/game fingerprints and compiler versions.
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
| Wrong or incomplete retail dump | Fingerprint and ARK/HDR read audit | Stop with a clear error |
| Dead online-service dependency | Run offline baseline and log service calls | Preserve/force the legitimate offline failure path; never emulate a fake service — online behavior is the Deluxe mod's problem, not ours ([docs/deluxe-compat.md](docs/deluxe-compat.md)) |
| Deluxe or title-update content in the data root | Run the vanilla smoke route against an installed Rock Band Blitz Deluxe root | Keep the vanilla `default.xex` as the codegen input, treat the overlay as data, and fix behaviour deltas in the project layer ([docs/deluxe-compat.md](docs/deluxe-compat.md)) |
| Undiscovered PPC targets or EH funclets | Codegen validation plus crash-PC correlation | Layered function/analysis hints |
| Missing kernel exports | Import inventory before first launch | Implement generic behavior in SDK or narrow project hook |
| Gameplay graphics absent | Capture first song's draw/resolve sequence | Correct reusable Xenos behavior; avoid game-specific rendering hacks |
| Streamed music stalls or drifts | Full-song clock/voice trace | Fix audio/runtime contract needed for playability |
| Profile/save assumptions | Boot with empty writable root | Minimal deterministic offline profile/storage behavior |
| SDK churn | Pin and upgrade separately | Regenerate and run the entire milestone matrix |

## Deferred until after “working”

- Pixel-perfect graphics, high-resolution/UI upgrades, unlocked frame rates, latency tuning, and performance optimization.
- Restoring, replacing or emulating Rock Central, leaderboards, challenges, social features, online multiplayer, or any other online service. Out of scope permanently: that work belongs to the Rock Band Blitz Deluxe mod and our only obligation is to stay compatible with it ([docs/deluxe-compat.md](docs/deluxe-compat.md)).
- Reworking server-dependent progression, coins, power-ups, or achievements beyond what is necessary to play offline.
- **Rock Band Blitz Deluxe compatibility** — a drag-and-drop Deluxe install used as the game-data root by the same executable, with the vanilla route unaffected. Acceptance criteria, hazards (TU5/`update:`, overlay fingerprinting, replaced UI data) and what we refuse to ship are in [docs/deluxe-compat.md](docs/deluxe-compat.md).
- Exhaustive DLC/export/custom-song compatibility and content-management UI.
- Multiple controller backends, keyboard bindings, handheld tuning, Linux/macOS/ARM support, packaging, installers, and auto-update.
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

