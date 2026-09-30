# Rock Band Blitz — Symbol & Analysis Inventory

Generated from the first codegen trace (`out/codegen-trace.log`) and the
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

These are the numbers [config/game_fingerprints.toml](../config/game_fingerprints.toml)
repeats as sizes and SHA-256 digests: the build checks the image against them
before running codegen, and the boot logs the digest it actually got — see
[build-and-run.md](build-and-run.md) §3–§4.

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

## Unresolved-call resolution (before the first compile)

The three absolute tail branches flagged by the first validation run were
resolved by adding `[functions]` entries (no explicit size → natural
discovery) in `config/functions.toml`:

| Branch target | From call site | Discovered as | Resolution |
| --- | --- | --- | --- |
| `0x82354DC0` | `0x8234017C` | `sub_82354DC0` (3 insns) | tail-calls `sub_82354D00` |
| `0x82379D20` | `0x82364A10` | `sub_82379D20` (1 insn) | tail-calls `sub_82380088` |
| `0x8243C688` | `0x8242A8DC` | `sub_8243C688` (10 insns) | tail-calls `sub_8243C458` |

Each target is a small thunk/stub ending in a tail branch; the real bodies are
the named `sub_*` targets above. They still need descriptive names, assigned from
runtime/disassembly evidence.

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
runtime-loaded module must be observed during a guest boot before
adding `[[modules]]` entries.

## First compile (2026-09-09)

- Full build (`cmake --build out/build/win-amd64-debug`) compiles all 104
  generated partitions and links `rb_blitz.exe` (Debug, ~77.9 MB).
- One build-config fix required: the host target must receive the vendored
  imgui include dir (`rexglue-sdk/thirdparty/imgui`) so `rex_app.cpp` can find
  `<imgui.h>`. Applied in project `CMakeLists.txt`; see `docs/history/bringup-log.md`
  B-004. Not a codegen/analysis fix — no change to `config/` or `generated/`.

## Indirect-call targets codegen had to be taught about

Guest boot reached functions that codegen discovery missed: real functions with
**no PDATA entry and no static `bl` caller**, reached only through a function
pointer. Each surfaced at runtime as
`[FATAL] Call to invalid or unregistered function at guest address 0x…` and was
forced into `config/functions.toml` one address at a time from that evidence:

| Guest address | Evidence | Notes |
| --- | --- | --- |
| `0x82789360` | boot, thread t19844 | body: `lwz r8,184(r3); mr r9,r3; li r4,1; …` |
| `0x8278A708` | boot, thread t18292 | after `0x82789360` registered |
| `0x8279A888` | boot, thread t16280 | body ends `… mtctr r11; bctr` (dispatcher) |
| `0x82779A70` | boot, thread t23656 | after `0x8279A888` registered |
| `0x82783D18` | boot, thread t12912 (Release) | 5th; added and confirmed in the Release build |
| `0x8278A6E0` | "PRESS A TO START", thread t21068 | a hole between the 8-byte adjuster thunks at `0x8278A6D8` and `0x8278A6E8` |
| `0x827EC038` | title-music MOGG open, thread t23752 | the 24-byte comparator; see the note below |
| 14 thunk slots | song pick, thread t13624 | `0x82783CC0..0x82783D17` plus `0x82783D70`, 8-byte `addi r3,r3,imm; b <target>` veneers |

**None of these is forced any more**, and the three tail-branch targets that are
still forced are a different class: nothing reaches them through a pointer. Patch
[0005](../patches/rexglue-sdk/0005-codegen-skip-stamp-and-gapfill-refinement.patch)
taught `GapFill` (`rexglue-sdk/src/codegen/phase_gapfill.cpp`) to treat `bctr` as a
terminator and to re-split a segmentation a later pass refines (a withdraw/refine
fixpoint, at most 8 passes), and its gap fill was extended on 2026-09-29 to register
the runs of words inside a code region that no function's **extent** claims, when the
word in front of the run leaves control.
Measured with this list trimmed to the three tail-branch entries, codegen found 0 of
the other 21 addresses before, 13 with the `bctr` terminator alone, 20 with the
fixpoint, and **21 with the unclaimed-run rule**; the register table grew 38,365
→ 38,439 → 38,457 (patch 0005, before and after its extension). Nothing in the table above needs
its `[functions."0x…"]` entry: `config/functions.toml` keeps the three tail-branch
entries and nothing else, and one of those is still load-bearing
(`0x8243C688`, in front of which sits a `bl`, so the word after it is not an entry).

`0x827EC038` is the one whose recorded reason was wrong. B-011 called its address
"taken in data"; it is taken by nothing - no dword in the decrypted image equals it
in either byte order, aligned or not, and no direct branch reaches it. It follows the
tail branch that ends `sub_827EBE08` (PDATA size 560, so that function's own extent
stops exactly at `0x827EC038`), and the guest builds the address in a register at run
time, so no segment ever started there and the first indirect call into it trapped.
With the extended patch 0005 it is registered from the unclaimed bytes, and the call that used to
trap now resolves: a 40 s boot reached the title screen with no `[FATAL]` and
`sub_827EC038` ran 206 times (measured with a temporary probe in its generated body).
Full detail, including the two defects patch 0005 exposed and the two the first cut of
the unclaimed-run rule exposed, is in [bringup-log.md](history/bringup-log.md) under "Codegen: the
B-003/B-006 root causes" and "B-006: the last forced entry goes away".

## Audio (MOGG) decryption path

Music is encrypted and is decrypted with a key the kernel derives, not with a key
embedded in the image. The path below was recovered from the recompiled image
(`out/codegen-trace.log`, `generated/default/`), the run log, the shipping data and
a match against the engine sources (`freeqaz/rb3`, `src/system/synth/`); the
overrides live in `src/hooks/crypto.cpp` with the SDK-free half of the logic in
`src/hooks/crypto_keytable.h` (see `docs/history/bringup-log.md` B-009).

| Guest address | What it is |
| --- | --- |
| `0x827F6BA4` | import thunk → `__imp__XeKeysSetKey` (`xboxkrnl.exe`) |
| `0x827F6BB4` | import thunk → `__imp__XeKeysAesCbc` |
| `0x82727218` | guest `XeKeysSetKey` wrapper: rejects index ≥ 8 (`0x585`), null buffer (`0x57`), size ≠ 16 (`0x18`), then calls the import with `index + 0xE0` |
| `0x827272B0` | guest `XeKeysAesCbc` wrapper: same `+0xE0` id bias, null feed, decrypt direction, forwards `r4..r6` |
| `0x823DE070` | MOGG version → key index selector (`12/13→0`, `14→1`, `15→2`, `16→3`, else `0`) — `ByteGrinder::GetEncMethod` |
| `0x823DE0C0` | `ByteGrinder::HvDecrypt(in, out, version)`: install the key for that version, then AES-128-ECB-decrypt the 16 bytes at `in` into `out` (the 16-byte stream mask) |
| `0x82768C88` | `VorbisReader::CheckHmxHeader`: the MOGG header parser and the only caller of `0x823DE0C0`, once per music stream |
| `0x82768AD0` | `VorbisReader::setupCypher(version)`: stream key = `GrindArray(keychain key) ^ mask`, then `ctr_start(nonce)` |
| `0x8280C568` | 64-byte `.data` table of four **obscured** AES-128 keys, one per MOGG version |

Because the guest id bias is `0xE0` and the wrapper accepts only `index < 8`, the
kernel sees key ids `0xE0..0xE7` for the four table entries. `XeCryptAesKey` /
`XeCryptAesCbc` (the real primitives the override forwards to) are exported by
`rexruntime` from `rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_crypt.cpp`; the
`XeKeys*` layer itself is `REX_EXPORT_STUB` there, which is what made music
silent.

**Independent confirmation:** RB3DX — the patch set real players run on retail
Rock Band 3 — fixes the same bug the same way (its patch group 1 swaps
`XeKeysSetKey`/`XeKeysAesCbc` for `XeCryptAesKey`/`XeCryptAesCbc` and patches a
64-byte key table at `0x82C76258`, byte-identical to Blitz's obscured table at
`0x8280C568`). See [rb3-references.md](rb3-references.md) §5.

