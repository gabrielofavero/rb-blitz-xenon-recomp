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

## Named symbols

`config/functions.toml` names the functions this project has proved by hand;
everything else is still the codegen's `sub_XXXXXXXX`. A name is a **readability
lever only** — it must resolve to the same body — so it is added in the same
commit as this table. As of 2026-10-03 the file carries nine entries: the three
forced entries below and six name-only entries for functions that already
existed, so nothing is added to or removed from discovery.

**The naming rule.**

- A name states the **subsystem and the method** it belongs to,
  `Subsystem_Method` (e.g. `VorbisReader_CheckHmxHeader`). The subsystem comes
  from the engine class or module the evidence names, never from a guess.
- **Nothing is named without evidence**, and the evidence plus a confidence
  (1–10) is recorded in the table. A name is only added once that evidence
  exists.
- A function whose whole body is argument setup plus one unconditional branch is
  a **tail thunk** and has no method of its own; it is named `Thunk_<target>`
  after the function it forwards to, and the target's behaviour is the evidence.
- A rename never changes boundaries or behaviour. The entry carries no
  `size`/`end`, so codegen redisovers the natural boundary exactly as it does for
  a PDATA entry; the generated body is byte-identical apart from the symbol and
  the renamed call targets inside it. Verified 2026-10-03 by diffing every named
  body in `generated/default/` before and after (`Validate` clean, 8/8 `ctest`).

| Guest address | Name | Subsystem | Evidence | Confidence |
| --- | --- | --- | --- | --- |
| `0x82354DC0` | `Thunk_82354D00` | thunk | 3 instructions, `mr r5,r4; lwz r4,4(r3); b 0x82354D00`. The target is a growable-array reserve: it doubles the capacity at `this+8` and reallocates `this+0` at 4 bytes per element. | 9 |
| `0x82379D20` | `Thunk_82380088` | thunk | 1 instruction, `b 0x82380088`. The target packs a `(u16, u16)` pair and calls `sub_827266F0`, which gates on `XamInputGetCapabilities` before it writes — an input/pad setter. | 8 |
| `0x8243C688` | `Thunk_8243C458` | thunk | 10 instructions ending `b 0x8243C458`, with `r3 = this + 12*index` and `r7 = &0x82085618` (an assertion string). The target computes `(end-begin)/12 <= index` then inserts — a 12-byte-element container insert. | 9 |
| `0x823DE070` | `ByteGrinder_GetEncMethod` | ByteGrinder | MOGG version → key-table index (`12/13→0`, `14→1`, `15→2`, `16→3`, else `0`); matches `ByteGrinder::GetEncMethod` in `freeqaz/rb3` `src/system/synth/ByteGrinder.cpp`. | 10 |
| `0x823DE0C0` | `ByteGrinder_HvDecrypt` | ByteGrinder | Installs the version's key and AES-128-ECB-decrypts the 16-byte header block; produces `mKeyMask`. [bringup-log.md](history/bringup-log.md) B-009 and its follow-up. | 10 |
| `0x82768AD0` | `VorbisReader_SetupCypher` | VorbisReader | Builds the stream key as `GrindArray(keychain key, magicA, magicB) ^ mKeyMask` and starts AES-CTR with the header nonce; matches `setupCypher` in the engine sources. B-009. | 9 |
| `0x82768C88` | `VorbisReader_CheckHmxHeader` | VorbisReader | MOGG header parser; matches RB3's `VorbisReader::CheckHmxHeader` instruction for instruction and is the only caller of `ByteGrinder_HvDecrypt`. B-009. | 10 |
| `0x82727218` | `XeKeys_SetKey` | XeKeys | Guest wrapper over the `__imp__XeKeysSetKey` import: rejects index ≥ 8, a null buffer and a size ≠ 16, then calls the import with `index + 0xE0`. B-009. | 10 |
| `0x827272B0` | `XeKeys_AesCbc` | XeKeys | Guest wrapper over the `__imp__XeKeysAesCbc` import, same `+0xE0` id bias, decrypt direction. B-009. | 10 |

The two other rows of the MOGG table below are not `[functions]` entries:
`0x827F6BA4` and `0x827F6BB4` are import thunks and already carry codegen's
`__imp__XeKeysSetKey` / `__imp__XeKeysAesCbc` from import resolution, and
`0x8280C568` is a `.data` table, which `[functions]` cannot name.

## Unresolved-call resolution (before the first compile)

The three absolute tail branches flagged by the first validation run were
resolved by adding `[functions]` entries (no explicit size → natural
discovery) in `config/functions.toml`:

| Branch target | From call site | Thunk | Resolution |
| --- | --- | --- | --- |
| `0x82354DC0` | `0x8234017C` | `Thunk_82354D00` (3 insns) | tail-calls `sub_82354D00` |
| `0x82379D20` | `0x82364A10` | `Thunk_82380088` (1 insn) | tail-calls `sub_82380088` |
| `0x8243C688` | `0x8242A8DC` | `Thunk_8243C458` (10 insns) | tail-calls `sub_8243C458` |

Each target is a small thunk/stub ending in a tail branch; the real bodies are
the `sub_*` targets above. All three are named now — see "Named symbols" for the
rule and the evidence.

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

## Setjmp / longjmp

The guest's `setjmp`/`longjmp` are the CRT's own context save and restore, written
for the Xenon ABI — where f14–f31, r13–r31, CR, LR and v64–v127 live across a call —
into a 1,344-byte (`0x540`) buffer. Codegen bridges the pair to the host's
`setjmp`/`longjmp` instead of recompiling the save
(`longjmp_address`/`setjmp_address`, `[entrypoint]` of
[rb_blitz_manifest.toml](../rb_blitz_manifest.toml)).

| Guest address | What it is | Evidence |
| --- | --- | --- |
| `0x82772940` | `setjmp(_JUMP_BUFFER*)`, returns 0 | `mflr r0; mfcr r4`, then `stfd f14..f31` at 0–143, `std r1` (SP) at 144, `std r13..r31` at 152–303, `stw r4` (CR) at 304, `stw r0` (LR) at 308, a word it zeroes at 312, `stvlx128` for v64–v127 at 320–1343, ending `li r3,0; blr` at `0x82772C08`. Four `bl` sites: `0x82600060`, `0x826001E0`, `0x826D98D4`, `0x826D9E9C` |
| `0x82772510` | `longjmp(_JUMP_BUFFER*, int)` | restores that layout offset for offset (`lfd` 0–136, `lwz` 144 = SP, `ld` 152–296, `lwz` 304 = CR, `lwz` 308 = LR, `lvx128` v64–v127 from 320), forces a zero value to 1 (`cmpwi r4,0` … `li r6,1`), takes the SEH path at `0x827727FC` (`bl __imp__RtlUnwind`, thunk `0x827F6164`) when the word setjmp left at `buf+312` is non-zero, and returns through `mtlr r5; ld r1,144(r7); mtcr r4; mr r3,r6; blr` at `0x827727F8`. 17 call sites: 15 `bl` (`0x825FF290`, `0x826D8924`, `0x826D9E30`, `0x82702A8C`, …) and 2 `b` (`0x825FF030`, `0x82702F0C`) |

Why these two and not a lookalike:

- The listings are each other's inverse, offset for offset — a layout nothing else
  in the image writes. `0x82772510` is also a PDATA entry point (`0x82772510` …
  `0x82772828`), which is what the size above is read from.
- One guest function calls **both**, on the same buffer: `0x826D9868` (PDATA size
  1488) does `addi r3,r31,16; bl 0x82772940`, keeps the result and branches on it
  (`cmpwi r3,0; bne 0x826D9AB4` at `0x826D98F0`), and longjmps `r31+16` with value 1
  at `0x826D9E30`.
- Neither address is taken in data, and neither is built in a register: the only
  dword in the decrypted image equal to `0x82772510` is its own PDATA entry
  (`0x82183018`), `0x82772940` appears nowhere at all in either byte order, and no
  `lis`/`addi`/`ori` pair constructs either one. Every call into the pair is one of
  the 21 direct branches in the table.

Two properties of the pair the recompiler has to live with:

- **`setjmp` has no PDATA entry of its own.** The entry at `0x82772828` (unwind word
  `0x40004506`, whose low byte encodes a six-instruction prologue) belongs to the
  libm function in front of it, so by the table's own begin-to-next-begin arithmetic
  `0x82772940`…`0x82772C08` sits inside *that* function's extent. Codegen still
  registers it — the `bl` targets re-split the extent, the mechanism
  [bringup-log.md](history/bringup-log.md) B-006 ended up relying on — and
  `generated/default/codegen.partition.json` gives it an index of its own.
- **The first five instructions are a call through a function pointer in `.data`**
  (`lis r4,0x829E; lwz r0,0x67CC(r4); cmpwi r0,0; mtctr r0; bnectr` before anything
  is saved): a hook `setjmp` runs first. The slot is zero in the image, so a plain
  dump never calls it, and nothing in this port has to know what it would do.

One reading caution for the next session: the VMX128 stores at 320–1343 are the one
place the bundled `powerpc-none-elf-objdump` disagrees with the recompiler. It prints
them as `psq_stx`/`psq_stux f0..f31` (a same-encoding ambiguity); the SDK's own
decoder reads `stvlx128` in `setjmp` and `lvx128` in `longjmp`, one per register,
v64–v127 — which is the reading the table above uses, and the one the generated
bodies (`generated/default/rb_blitz_recomp.78.cpp` for `0x82772940`,
`…recomp.40.cpp` for `0x82772510`) show.

Measured on 2026-09-30, with a `REXLOG_INFO` in front of each of the 21 bridge sites
(temporary, removed and the tree re-generated afterwards, and with a control probe in
`sub_82768C88`, the MOGG header parser, to prove the channel logs: it fired 2,164
times in a 40 s boot): **no run reaches either bridge.** A 40 s boot to the title
screen and the whole offline route — boot → menu → song list → a 315 s song → the
results screen (`scripts/acceptance_song.ps1 -Runs 1`, pass) — execute 0 of the 21
sites; the same route on the clean tree passes as well. So
the engine's `setjmp`/`longjmp` are error-path only: the bridge is correct by
construction, and it is not what a happy-path crash would be, which disposes of the
"it may already explain odd error-path deaths" hypothesis.

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

