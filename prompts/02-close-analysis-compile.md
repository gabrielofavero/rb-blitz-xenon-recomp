---
status: done
milestone: 2
last_updated: 2026-09-09
---

# Milestone 2 — Close analysis and compile the translation

## Entry state

- Milestone 1 is **done**: codegen target builds and runs clean (0 analysis
  errors), inventory written. See `docs/symbols.md` and
  `docs/bringup-log.md`.
- Known backlog (from first codegen run, address-sorted) — **all resolved** by
  adding `config/functions.toml` entries (no explicit size → natural
  discovery):

  | Branch target | From call site | Identified as | Resolution |
  | --- | --- | --- | --- |
  | `0x82354DC0` | `0x8234017C` | `sub_82354DC0` (3 insns) | tail-calls `sub_82354D00` |
  | `0x82379D20` | `0x82364A10` | `sub_82379D20` (1 insn) | tail-calls `sub_82380088` |
  | `0x8243C688` | `0x8242A8DC` | `sub_8243C688` (10 insns) | tail-calls `sub_8243C458` |

  Each is a thunk/stub; the real bodies are the `sub_*` targets. Milestone 2
  should give these descriptive names from runtime/disassembly evidence.

- **Knowledge folded in (Milestone 1):**
  - Sections: `.text` 0x82190000 (exec), `.rdata` 0x82000400, `.pdata`
    0x82148C00, `.data` 0x82800000, `.XBMOVIE` 0x829E6A00, `.idata`
    0x829F0000, `.XBLD` 0x82A00000. Image base 0x82000000, entry point
    0x82193820.
  - Imports: 262 function imports (113 xam.xex + 162 xboxkrnl.exe minus 13
    patched variables), 0 unresolved. Full list in `out/imports.txt`.
  - 38,344 functions ready for codegen (31,076 from PDATA, 262 imports, 3
    CONFIG, save/restore helpers).
  - 132 auto-detected jump tables (no manual `[[switch_tables]]` needed).
  - 0 data regions / invalid instruction ranges; no SEH frame-size warnings.
  - Guest DLL loading: title imports and calls `XexLoadImage`
    (`0x827F6C34`, called from `0x82728048`) and `XexUnloadImage`
    (`0x827F6C24`, from `0x82727F94`), but no additional `.xex`/`.dll` is in
    `game/`. Watch for runtime module loads during guest boot before adding
    `[[modules]]` entries.

## Precondition

`game/default.xex` present and matching `config/game_fingerprints.toml`, plus a
disassembler/codegen diagnostics for identifying the unresolved targets.

## Steps

### 1. Resolve failures in small address-sorted batches

- Work the unresolved targets first (`0x82354DC0`, `0x82379D20`, `0x8243C688`).
- Identify what each target is from disassembly. Record the evidence in
  `docs/symbols.md`.

### 2. Fill the config files (never hand-edit `generated/`)

- `config/functions.toml` — verified function boundaries and discontinuous chunks.
- `config/jump_tables.toml` — manual switch tables only where auto-detection is
  demonstrably wrong.
- `config/analysis_hints.toml` — invalid data ranges, indirect calls, EH hints.
- `config/rexcrt.toml` — ReXCRT mappings only after addresses and ABI are
  confirmed. The heap group must be complete if enabled.
- `config/midasm_hooks.toml` — instruction hooks only when unavoidable.
- Classify embedded data as **data**; never invent fake functions to silence
  validation.

### 3. Enable EH only if required

- Enable generated exception handlers only if this binary requires them.
- Keep the Windows async-exception flags consistent with generated PCH/sources
  (`-fasync-exceptions` on clang, per `generated/rexglue.cmake`).

### 4. Compile everything and fix at the source

```powershell
cmake --build out/build/win-amd64-debug
```

- Compile all generated TUs with line-table debug info (`REXGLUE_RECOMP_DEBUG_INFO=line-tables-only`).
- Fix every compile/link error at its source config or a supported hook
  boundary — **never** in a hand-edited generated file.

## Acceptance / exit criteria

- [x] Native executable links (`out/build/win-amd64-debug/rb_blitz.exe`,
      77.9 MB Debug).
- [x] No hand-edited generated files (only project `CMakeLists.txt` touched).
- [x] No unexplained forced validation bypass; 0 analysis errors, no
      deliberate exception needed.
- [x] Fixes live at the ownership boundary: the single build-config fix
      (imgui include dir for the host target) is in `CMakeLists.txt`, the
      project's thin target definition. No `config/` or `generated/` changes.

## Handoff to Milestone 3

Written into `03-guest-entry-boot.md`:

- final import inventory: 262 function imports resolved (113 xam.xex +
  162 xboxkrnl.exe − 13 patched variable imports), 0 unresolved. Full list in
  `out/imports.txt`. `__C_specific_handler` is imported, so SEH support is
  expected but no funclets were flagged by codegen.
- EH/threading: no manual exception-handler hints needed; `-fasync-exceptions`
  already carried by `generated/rexglue.cmake` for clang on Windows. Guest
  worker-thread behavior is untested until the first boot.
- build command: `cmake --build out/build/win-amd64-debug`
  (configure with `cmake --preset win-amd64-debug`). Executable:
  `out/build/win-amd64-debug/rb_blitz.exe`.

## Bring-up loop

Same as Milestone 1: reproduce → first bad event → classify → narrowest fix →
regression → record in `docs/bringup-log.md`.
