# Rock Band Blitz — Symbol & Analysis Inventory

Generated from the Milestone 1 codegen trace (`out/codegen-trace.log`) and the
plaintext XEX2 header (`scripts/parse_xex2_header.py`). Codegen completed with
**0 analysis errors** on 2026-09-09.

## Image identity

| Field | Value |
| --- | --- |
| Magic | `XEX2` |
| Module flags | `0x00000001` (TITLE) |
| Title ID | `5841122D` |
| Media ID | `78492654` |
| Version | `0.0.0.2` |
| Filetime | 2012-06-19 16:38:01 UTC |
| Image base | `0x82000000` |
| Entry point | `0x82193820` |
| Header size | `0x3000` |
| Encryption | normal (1), compression basic (1) |

## Sections

| Name | Guest range | Size | Executable |
| --- | --- | --- | --- |
| `.rdata` | `0x82000400` | `0x148704` | no |
| `.pdata` | `0x82148C00` | `0x3CB50` | no |
| `.text` | `0x82190000` | `0x666ED4` | **yes** |
| `.data` | `0x82800000` | `0x1E68F4` | no |
| `.XBMOVIE` | `0x829E6A00` | `0xC` | no |
| `.idata` | `0x829F0000` | `0x4C4` | no |
| `.XBLD` | `0x82A00000` | `0x140` | no |

Import thunk table starts at `0x827F5E74`; the import/export range ends at
`0x827F6ED4` (end of `.text`).

## Imports

Two import libraries (XEX2 `IMPORT_LIBRARIES` header), 275 records total:

| Library | Records | Function imports | Variable imports |
| --- | --- | --- | --- |
| `xam.xex` | 226 | 113 | patched |
| `xboxkrnl.exe` | 311 | 162 | patched |

Codegen resolved **262 function imports** (0 unresolved) and patched **13
variable imports** to fixed addresses (e.g. `XboxHardwareInfo`,
`XexExecutableModuleHandle`, `KeCertMonitorData`). Full per-symbol list is in
`out/imports.txt`. Notable imports include:

- Winsock via `NetDll_*` (WSAStartup, socket, send/recv, …).
- XAudio render-driver client (`XAudioRegisterRenderDriverClient`,
  `XAudioSubmitRenderDriverFrame`, …) and XMA (`XMACreateContext`).
- `XeKeysAesCbc` / `XeKeysSetKey`, `MicDeviceRequest`, `RmcDeviceRequest`.
- Kernel: `Ke*`, `Ex*`, `Io*`, `Mm*`, `Ob*`, `Ps*`, `Rtl*`, `Nt*`, `Vd*`,
  `Hal*`, `KfAcquireSpinLock`, `KiApcNormalRoutineNop`.
- `__C_specific_handler` (SEH support).

## Functions

| Metric | Value |
| --- | --- |
| Functions ready for codegen | 38,344 |
| From PDATA | 31,076 |
| Function imports | 262 |
| CONFIG (manual entry points) | 3 |
| Save/restore helpers | `__savegprlr_*`, `__restgprlr_*`, `__savefpr_*`, `__restfpr_*`, `__savevmx_*`, `__restvmx_*` |
| Discovery fixed point | iteration 3 (34,598 functions) |
| Code regions | 12,670 |
| Data regions | 0 |

## Unresolved-call resolution (Milestone 1 → 2 handoff)

The three absolute tail branches flagged by the first validation run were
resolved by adding `[functions]` entries (no explicit size → natural
discovery) in `config/functions.toml`:

| Branch target | From call site | Discovered as | Resolution |
| --- | --- | --- | --- |
| `0x82354DC0` | `0x8234017C` | `sub_82354DC0` (3 insns) | tail-calls `sub_82354D00` |
| `0x82379D20` | `0x82364A10` | `sub_82379D20` (1 insn) | tail-calls `sub_82380088` |
| `0x8243C688` | `0x8242A8DC` | `sub_8243C688` (10 insns) | tail-calls `sub_8243C458` |

Each target is a small thunk/stub ending in a tail branch; the real bodies are
the named `sub_*` targets above. Milestone 2 should assign descriptive names to
these and their targets from runtime/disassembly evidence.

## Jump tables

132 distinct switch tables were auto-detected (154 detections across discovery
iterations), e.g. `bctr 0x82332F14` with 141 targets, `0x821CB718` with 104.
No manual `[[switch_tables]]` entries were required.

## Exception handling

31,076 functions carry PDATA; no SEH scope/frame-size warnings and no
`UnimplementedInsn` errors were reported. No manual EH funclet hints needed.

## Guest DLL loading

The title imports and calls `XexLoadImage` (`0x827F6C34`, called from
`0x82728048`) and `XexUnloadImage` (`0x827F6C24`, from `0x82727F94`), so it
*can* load additional modules at runtime. No additional `.xex`/`.dll` is present
in `game/`, so the manifest carries only the `default.xex` entrypoint. Any
runtime-loaded module must be observed during Milestone 2 guest boot before
adding `[[modules]]` entries.
