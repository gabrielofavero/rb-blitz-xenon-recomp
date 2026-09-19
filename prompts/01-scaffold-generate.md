---
status: done
milestone: 1
last_updated: 2026-09-19
---

# Milestone 1 — Scaffold and generate

## Entry state (verified 2026-09-09)

Done so far:

- ReXGlue CLI built: `rexglue-sdk/out/win-amd64/Release/rexglue.exe`
  (SDK commit `c94f5eb`).
- `rexglue init` ran; manifest at `rb_blitz_manifest.toml` (entrypoint
  `game/default.xex`, out dir `generated/default`, `includes = []`).
- `CMakePresets.json` sets `REXSDK_DIR = ${sourceDir}/rexglue-sdk` in all base
  presets. `cmake --preset win-amd64-debug` configures cleanly and reports
  "Using ReXGlue SDK from source tree".
- Codegen ran once without `--force`. Result: **validation failed with 3
  unresolved calls**, no code emitted into `generated/default`. Diagnostics
  were logged (see `docs/bringup-log.md`).

Open items:

- **B-001**: `rb_blitz_codegen` CMake target fails. Root cause is the SDK
  submodule's symlinks being flattened on this Windows checkout
  (`git config core.symlinks` is `false` in this repo, even though global is
  `true`). The `libmspack/cabextract` tree contains 29-byte text-file stubs
  instead of symlinks, so `lzxd.c` fails to compile.
- Inventory of sections/imports/functions/jump tables/EH patterns not done.
- Guest-DLL loading not yet confirmed.
- Toolchain note in `docs/bringup-log.md` references a repo memory
  `toolchain.md` that does not exist yet.

## Precondition (user, not the agent)

Place the authorized dump at `game/` so `game/default.xex`,
`game/gen/main_xbox.hdr`, and `game/gen/main_xbox_0.ark` exist and match
`config/game_fingerprints.toml`. `game/` is currently absent.

## Steps

### 1. Fix B-001 (pick one, prefer the first that works)

- **Option A (repo-local, least invasive).** Enable symlink capability
  (Developer Mode or elevated shell), then in this repo:
  ```powershell
  git config core.symlinks true
  git -C rexglue-sdk checkout -- .   # re-materialize symlinks from the index
  ```
  Verify with `Test-Path rexglue-sdk/thirdparty/libmspack/cabextract/mspack/lzxd.c`
  and confirm it is a real file (not a 29-byte stub).
- **Option B (SDK package).** Install the pinned SDK and switch
  `generated/rexglue.cmake` to `find_package` by unsetting `REXSDK_DIR`.
- **Option C (upstream-worthy patch).** Edit `rexglue-sdk/thirdparty/CMakeLists.txt`
  to reference `libmspack/libmspack/mspack/lzxd.c` (and siblings) directly,
  avoiding the cabextract symlink indirection on Windows.

Record the chosen fix and its verification in `docs/bringup-log.md` under B-001.

### 2. Re-run codegen via the build target

```powershell
cmake --build out/build/win-amd64-debug --target rb_blitz_codegen
```

Then run it a second time — it must be a **no-op** through the stamp/depfile
mechanism. That no-op run is this milestone's exit gate.

### 3. Inventory (fill `config/`, evidence in `docs/symbols.md`)

From codegen diagnostics and, where needed, disassembly of `game/default.xex`:

- sections and their guest ranges;
- imports (kernel/runtime) with ordinals;
- discovered function boundaries and the unresolved targets:
  | Branch target | From call site | Instruction |
  | --- | --- | --- |
  | `0x82354DC0` | `0x8234017C` | `b 0x82354DC0` |
  | `0x82379D20` | `0x82364A10` | `b 0x82379D20` |
  | `0x8243C688` | `0x8242A8DC` | `b 0x8243C688` |
- any invalid instruction ranges, indirect calls, and EH funclets.

### 4. Confirm guest DLL loading

Determine whether the title calls `XexLoadImage` on additional modules. Add
manifest entries **only** for modules actually observed, with exact canonical
guest paths.

### 5. Record toolchain locations

Create the missing repo memory `toolchain.md` with the exact paths for
clang/clang++ 22.1.8, cmake 4.4.3, and ninja 1.13.2 (they are not on PATH).

## Acceptance / exit criteria

- [x] `rb_blitz_codegen` completes (exit 0; 215 files emitted).
- [x] A second unchanged run produces byte-identical output (`0 written,
      215 unchanged`). NOTE: on this Windows machine the SDK re-runs the
      analysis phase each invocation instead of the intended `OutputStamp`
      skip — a perf issue, not a correctness issue (flagged in
      `docs/bringup-log.md` as B-003).
- [x] Inventory written to `config/` + `docs/symbols.md`.
- [x] Guest DLL loading confirmed or ruled out, manifest updated accordingly
      (no additional modules present; manifest unchanged).

## Handoff to Milestone 2

Write these into `02-close-analysis-compile.md`:

- the resolution of each unresolved target (function name / address evidence);
- the full import inventory;
- any jump tables, EH funclets, or invalid ranges found;
- which config files now exist and what each holds.

## Bring-up loop (use for every blocker)

1. Reproduce from the nearest saved milestone with one deterministic action.
2. Save the full log; identify the **first** bad event, not just the crash.
3. Record guest PC/function, thread, registers, import/VFS request, expected behavior.
4. Classify: analysis | kernel/runtime | filesystem/content | graphics | audio | input | network/platform | game logic.
5. Apply the narrowest fix at the ownership boundary from the plan.
6. Re-run this milestone's test and all earlier smoke tests.
7. Add the result to `docs/bringup-log.md`; promote enduring limits to `docs/known-issues.md`.
