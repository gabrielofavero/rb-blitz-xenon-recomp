# RB3 reference mining — hooks, patches and engine knowledge worth porting

*Research pass: 2026-09-19. Scope: what the two Rock Band 3 projects already know
that we do not, and which parts of it apply to Rock Band Blitz (`5841122D`,
ReXGlue SDK v0.10.0.2-dev).*

Rock Band Blitz and Rock Band 3 are the same engine. That means a lot of the
work already done for RB3 transfers to us **as knowledge**, even though the
addresses never do. This document is the durable output of a survey of the two
external projects, and the source for the "Reference leads" sections added to
[prompts/04](../prompts/04-menus-content-input-saves.md),
[prompts/05](../prompts/05-complete-one-song.md) and
[prompts/06](../prompts/06-reproducible-release.md).

It is deliberately a catalogue of *techniques and known-answer checks*, not a
plan: nothing here changes our milestone order.

## 0. Sources

| Project | What it is | License | How we use it |
| --- | --- | --- | --- |
| [`ihatecompvir/band3_recomp`](https://github.com/ihatecompvir/band3_recomp) | Rock Band 3 on the same ReXGlue SDK. A mature set of guest hooks and patches behind a symbol map. | **GPL-2.0** | Knowledge **and** a code source we may adapt, because we are GPL-2.0 too (see §9). |
| [`freeqaz/rb3-xenon`](https://github.com/freeqaz/rb3-xenon) | Rock Band 3 for **Xbox 360** (MSVC X360, retail) decompilation. Engine from `dc3-decomp`, game code from the Wii decomp. Active 2026-09. | CC0-1.0 | Closest sibling: same compiler, same ABIs, same kernel imports. |
| [`freeqaz/rb3`](https://github.com/freeqaz/rb3) | Rock Band 3 **Wii** decompilation with an LP64 native port, plus the audio/MOGG tooling and a verification methodology. | CC0-1.0 | Engine source of truth + verification method. |
| [`freeqaz/milo-native-engine`](https://github.com/freeqaz/milo-native-engine) | The shared native (host) Milo engine extracted from that work. | CC0-1.0 | How they model a host engine layer at all. |
| [`solamint/rock-band-blitz-deluxe`](https://github.com/solamint/rock-band-blitz-deluxe) | **Rock Band Blitz Deluxe** — the MiloHax QoL mod for *Blitz itself*, i.e. the Blitz-side counterpart of the RB3DX patch set in §5–§6. Not an RB3 project and not part of this survey; listed because it is where the work this repository deliberately does not do actually happens. | Repository root has no `LICENSE` file (checked 2026-09-19); it redistributes patched game files, so treat it as knowledge only — never a code source, never something to vendor. | Game-data compatibility target: [deluxe-compat.md](deluxe-compat.md). |

Local clones used for this pass live outside the repo, in the session workspace
(`files/band3_recomp`, `files/xenia`); they are **not** vendored here. To refresh:

```powershell
git clone https://github.com/ihatecompvir/band3_recomp   # pinned: c51944bd (ReXGlue 0.8.0)
git clone https://github.com/freeqaz/rb3-xenon
git clone https://github.com/freeqaz/rb3
```

`band3_recomp` is pinned at `c51944bd` ("Upgrade to Rexglue 0.8.0"), so its hook
files are written against **0.8 APIs**. We are on **0.10**; §1–§3 below note
where that matters.

The last row of that table is not an RB3 project. **Rock Band Blitz Deluxe** is
the Blitz-side sibling of the RB3DX patch set catalogued in §5–§6, and it — not
this repository — is where unblocked online behaviour and QoL changes live. We
restore no online services; we aim to run a Deluxe install as a game-data variant
of the same executable. Policy, hazards and acceptance criteria are in
[deluxe-compat.md](deluxe-compat.md).

## 1. How hooking actually works in ReXGlue 0.10 (read this first)

Our generated PCI header emits, for **every** function it discovers:

```c
#define DECLARE_REX_FUNC(name)  REX_EXTERN(name); REX_EXTERN(__imp__##name)
#define DEFINE_REX_FUNC(name) \
    __attribute__((alias("__imp__" #name))) __attribute__((weak, noinline)) \
    extern "C" REX_FUNC(name); REX_EXTERN(__imp__##name)
```

— `generated/default/rb_blitz_pch.h` lines 51–79.

`REX_FUNC(x)` is `void x(PPCContext& __restrict ctx, uint8_t* base)`.

Consequences, all of which are load-bearing for us:

1. `Name` is a **weak, overridable alias**; `__imp__Name` is the **strong,
   always-original** implementation. Defining our own `Name` replaces it
   everywhere, including in already-generated call sites.
2. Therefore **we can hook any of the 38,344 Blitz functions today**, with no
   symbol-map work at all:

   ```cpp
   REX_HOOK(sub_82438F40, Native_RndMatLoad);           // divert
   void Native_RndMatLoad(PPCContext& ctx, uint8_t* base) {
       // ...
       __imp__sub_82438F40(ctx, base);                   // call the original
   }
   ```

   A readable name is a *readability nicety*, never a prerequisite.

3. `__imp__` prefixes appear on **imports** (e.g. our `__imp__XeKeysSetKey`), and
   on any function that a `[functions]` entry names explicitly. Everything else
   is `sub_XXXXXXXX`. `src/hooks/crypto.cpp` hooks the *kernel import*, which is
   why it uses the `__imp__` form.

> **Migration trap.** `band3_recomp` hooks things like `__imp__NewFile`. That is
> a ReXGlue **0.8** artefact: 0.8 emitted `[functions]`-named functions as
> `__imp__Name`, whereas 0.10 emits them with the bare `name`
> (`rexglue-sdk/src/codegen/phase_register.cpp`, `cfg.name.empty() ?
> "sub_%08X" : cfg.name`) and reserves `__imp__` for imports. Do not copy the
> `__imp__` pattern blindly; check which of the two we actually need.

Also available (`include/rex/hook.h`):

| Macro | Effect |
| --- | --- |
| `REX_HOOK(sub, fn)` | `fn` gets `(PPCContext&, uint8_t*)`, registered against `sub`. |
| `REX_HOOK_RAW(sub, fn)` | Same, but you own the ctx/base call exactly. |
| `REX_EXTERN(name)` | `extern "C" REX_FUNC(name)` — to call the original. |

**Landmine worth remembering:** `band3_recomp/src/Hooks/math.cpp` uses
`REX_HOOK_RAW` for `_Interp_Vector3` with the comment *"because REX_HOOK maps the
registers wrong in this case. I think it tries to use f3 instead of f1."* Float
arguments on some PPC functions do not survive the automatic mapping. If a
float-argument hook we write misbehaves, try `REX_HOOK_RAW` before suspecting the
guest code.

## 2. Symbol names — a readability lever we are not using

Our `config/functions.toml` forces nine entry points and names **none** of them,
so the generated tree is 37,851 anonymous `sub_XXXXXXXX` symbols. Adding
`name =` costs nothing at build time and buys real code:

```toml
[functions."0x82768C88"]
name = "VorbisReader_CheckHmxHeader"
```

Our 0.10 schema (`rexglue-sdk/src/codegen/config.cpp`):

| Key | Meaning |
| --- | --- |
| `size` / `end` | Force the function boundary. Mutually exclusive. We currently use neither (natural boundary from the code region — see [symbols.md](symbols.md)). |
| `name` | Symbol name; default `sub_%08X`. |
| `parent` | Attach under a parent symbol. |
| `share_registers` | Opt out of per-function register save/restore. |

Recommendation: name the functions we have *proved* by hand (the eleven MOGG rows
in [symbols.md](symbols.md) are the obvious first batch). Do it in the same commit
as a doc update so the symbol map and [symbols.md](symbols.md) stay in step.

## 3. `[[midasm_hook]]` — surgical patching without a hook function

Our SDK supports midasm hooks with a **richer schema than 0.8**
(`config.cpp` lines 310–371):

| Key | Effect |
| --- | --- |
| `address` | Guest address of the instruction (required). |
| `name` | Hook symbol name (required). |
| `registers` | Registers to expose (e.g. `["r3", "r11"]`). |
| `after_instruction` | Capture after the instruction runs, not before. |
| `return` | Force-return with the captured register values. |
| `return_on_true` / `return_on_false` | Conditional forced return. |
| `jump_address` | Force a branch to another address. |
| `jump_address_on_true` / `_on_false` | Conditional forced branch. |

Codegen logs an error if you both return and jump, or mix direct with
conditional return/jump.

Band 3's three uses, i.e. the shape of the tool:

| Name | Address | Register | Notes |
| --- | --- | --- | --- |
| `ControllerHook` | `0x825320B4` | `r11` | `after_instruction = true` |
| `UpdateArkHook` | `0x825216C0` | `r4` | after = false |
| `SongCountHook` | `0x82579880` | `r3` | after = true |

This is the right tool for the places where we only need to **force a branch or a
return value** — cheaper and far less brittle than a hand-transcribed body. Live
candidates in our tree:

* The **"Proceed in Offline Mode?"** branch (M4): once located, either force the
  branch or `return` a value, instead of reimplementing the caller.
* `BandSongMgr::IsDemo`-style gates (M5, see §6).
* The `SetDiskError` call sites (M4/M5, see §6).

The one thing midasm hooks cannot do is carry host state across calls; anything
stateful still wants `REX_HOOK`.

## 4. band3_recomp: the port catalogue

Seven hook files plus `patches.cpp`. Everything below is a *candidate*; each row
lists the Blitz evidence we would need before writing code.

### 4.1 Hooks (`band3_recomp/src/Hooks/`)

| band3 hook (RB3 addr) | What it does | Why we want it | Blitz evidence needed | Priority |
| --- | --- | --- | --- | --- |
| `NewFile` `0x825173E0` (`.cpp`) | Sanitises `..` → `(..)`, tries an `assets/<path>` fallback, and if the host file exists sets `ctx.r4.u64 = flags \| 0x10000` to **force the host-file read path**. | The mechanism for the asset-overlay idea in §7.2: let us shadow or fix shipping data without touching `game/`. | Blitz `NewFile` (import thunk to `__imp__NewFile`), the flags word layout, and our asset root (`game/`, not `assets/`). | P1 for M4 |
| `AddHeap` `0x827BC2D0` (`.cpp`) | Reads the DTA `DataArray` in `r5`, `strcmp(name, "main"/"char")` and overrides `ctx.r4.u32` (heap size) from config. | Gives us a heap dial when we hit `out of memory` at boot or mid-song. | Blitz's `AddHeap` and its DTA heap names. Note the RB3 hook **cannot** change `mem.dta` pools, so a `big_hunk`-style pool stays fixed. | P2 |
| `PlatformMgr__GetName` `0x8251CA58` (`.cpp`) | Calls the original, then copies a configured user name (≤32 chars) into the guest buffer at `ctx.r3`. | Canned profile data for UI automation; removes a manual step from the M4 smoke test. | Blitz's equivalent getter; where the name is consumed. | P3 |
| `BoxMapLighting__ApplyQueuedLights` (`.cpp) | Early-return: skip the approximate box-map lights. | Visual A/B only. | — | P3 |
| `RndMat__Load` `0x82438F40` (`.cpp`) | Reads `REX_LOAD_U32(this+0x118)`; the hair shader id `2` is zeroed. Also `REX_STORE_U8(this+0x99, 0)` to force `useEnviron` (fullbright). | Workaround for wrong shading; useful if M6 screenshots come out unlit. | Blitz `RndMat::Load` and the same field offsets. **Offsets are not portable — verify before trusting.** | P3 |
| `ProcCounter__ProcCommands` `0x8242FA80` (`.cpp`) | `ctx.r3.u64 = 7`, then early-return: disables even/odd rendering. | Debug aid for double-draw artefacts. | — | P3 |
| `OutfitConfig__CompressTextures` (`.cpp) | Skips texture compression when disabled. | Not relevant to Blitz (no outfits we need). | — | — |
| `CamShot__Shake` `0x824BDB80` (`.cpp`, 24 KB) | **Whole-function reimplementation** of the camera shake: a hand-transcribed copy of the generated PPC body with a host `steady_clock`-derived `GetRealFpsScale()` (clamped to 0.1 s, `× 60.0f`) replacing the guest frame-count delta, so shake is frame-rate independent. | Only if we ever uncap the frame rate. | — | P3 |
| `_Normalize_Vector3` / `_Matrix3` / `_Multiply_Matrix3`, `_Interp_Vector3`, `_acos/_asin/_atan/_atan2/_cos/_floor/_fmod/_pow/_sin/_tan` (`.cpp) | Native replacements for scalar math and trig. | **Perf**, and it sidesteps guest-FPU precision drift. Worth measuring once we run a frame-rate-sensitive scene. | Which of these Blitz actually has and whether they are hot. | P2 for M5 |
| `patches.cpp` | See §4.2. | | | |

### 4.2 `patches.cpp` — the patch catalogue

These are our best source of *known-answer* checks, because RB3 shipped with the
same problems.

| Patch | Effect | Blitz relevance |
| --- | --- | --- |
| `App__Run` → `RunFunc_AppRunWithoutDebugging` | "Patching debugger trap". | RB3DX does the **identical** patch (§6, group 2). Expect Blitz to have it; the M4 bring-up will hit it. |
| `OptionBool` / `OptionStr` | Injects host `argv` into guest DTA options, tracking consumed args. | High value for us: `drive_ui.ps1` currently injects keystrokes against a real focus transition. DTA options may let us script the same states deterministically. |
| `Rnd__PreInit` (`rnd_this + 0xf0` sync override) | Forces vertical sync behaviour. | Only if we need frame pacing. |
| `StreamChecksum__ValidateChecksum` → `1` | Skips stream checksum validation. | **P1 for M5.** Any asset we decrypt, repack or edit will otherwise fail validation. |
| `PlatformMgr__SetDiskError` → no-op | Suppresses the disk-error path. | RB3DX group 4; our `update:` reads already produce benign-looking failures. Check whether Blitz routes them here. |
| `MetaMusic__{Load,Poll,Start,Loaded}` disable switch | A/B switch for the music system. | Useful for isolating audio bugs once B-009 is confirmed. |
| `SongMgr__IsDemo` → `0` | Forces non-demo. | RB3DX group 6 disables the same check by branching. Demo logic hides content; check for a Blitz equivalent before M5 song enumeration. |
| `MetaPerformer__SetVenue` | Forces a venue, optionally random. | Only if Blitz venues misbehave. |
| Forced import of `__imp__XamContentAggregateCreateEnumerator` (`[[gnu::used]] static volatile auto`) | Keeps the DLC enumerator linked so content enumerates. | Relevant if Blitz mounts content; check whether our link already keeps it. |

## 5. Independent validation of our audio fix

`freeqaz/rb3-xenon` documents the **complete 10-group / 53-word patch set that
RB3DX applies to retail RB3** — i.e. what real players run on a real 360. Group 1
is our bug.

**Group 1 — MOGG AES key path, 26 words.** It rewrites the *same* construction we
found in Blitz: the import ordinals at `0x8200068C/90`, four import-thunk words
at `0x82C4C47C–90`, call sites at `0x82727698/B8/D4/DC`, `0x82840794/98/C8`,
`0x82840828–30` / `68–70` / `B4`, and the **64-byte `.data` key table at
`0x82C76258–94`** — replacing `xboxkrnl` `XeKeysSetKey`/`XeKeysAesCbc`
(ordinals `0x242`/`0x24B`) with `XeCryptAesKey`/`XeCryptAesCbc` (`0x159`/`0x15B`),
a stack AES state, and **the deobfuscated keys**.

`0x82C76258` (RB3) is byte-identical to `0x8280C568` (Blitz). That is exactly the
table our `src/hooks/crypto.cpp` deobfuscates at runtime, and exactly the
kernel-layer swap we perform by intercepting the two imports. **Independent
confirmation that B-009's diagnosis and technique are right**, from a project
that fixed it by byte-patching rather than by hooking.

Note the divergence, because it is in our favour: RB3DX had to patch the table
and the call sites in the image. We intercept the two kernel imports and derive
the plaintext at runtime, which is version-agnostic and survives a clean image.

The remaining groups are the rest of the catalogue above, and are worth checking
against Blitz one by one (they are listed in §6 for the ones with Blitz leads).

## 6. RB3DX's other nine groups, mapped to Blitz work

| # | Patch | Same thing in band3_recomp? | Blitz lead |
| --- | --- | --- | --- |
| 2 | `0x82272E90`: `bl App::Run` → `bcl RunWithoutDebugging` | Yes — `patches.cpp` `App__Run` | Debugger trap; expect it. Find the branch in M4. |
| 3 | Splash/ESRB skip: `0x82270F40` `beq`→`nop`, `0x82270F84` `bl`→`nop` | — | Cuts boot time and unblocks headless runs. Candidate for a midasm hook. |
| 4 | `PlatformMgr::SetDiskError` neutered (`blr` at `0x82516320`), head reused for a `DataSet` type-guard trampoline; 8 call sites left unchanged (`0x8227153C`, `0x825338E0`, `0x82533AE4`, `0x82533B14`, `0x82533BE0`, `0x8253566C`, `0x82B8C730`, `0x82B8C7D8`) | Yes — `patches.cpp` `PlatformMgr__SetDiskError` | Check whether our `update:\gen\patch_xbox.hdr` failure routes here. |
| 5 | `DataSet` type guard at `0x8275D6E0` | — | Only if we hit a DataSet type error. |
| 6 | `0x82575F9C` `bne`→`nop` ⇒ `BandSongMgr::IsDemo` always false | Yes — `SongMgr__IsDemo` → 0 | M5: demo gates hide songs. |
| 7 | `AddSongData`: `0x82579098` `bl` → `li r3,0` (special-song table disabled) | — | M5 song-list correctness. |
| 8 | Content prefix `0x82089B40/44`: `"UPDATE:"` → `"D:"` (mount content from `D:`) | — | **Directly relevant**: our `update:` reads are a known snag. |
| 9 | `0x82089518`: `"songcache"` → `"rbdxcache"` | — | Lineage fingerprint only; tells us the string is a real cache name. |
| 10 | `0x82AE6880`: `strcpy` → `strncpy` | — | Crash hardening; adopt only if we hit it. |

## 7. Engine and methodology knowledge from `freeqaz/rb3`

### 7.1 Audio

The Wii decomp is the engine source of truth for the MOGG path we reconstructed
from the recompiled image — `src/system/synth/` is where `VorbisReader`,
`ByteGrinder` and the keychain live. Their `scripts/native/decrypt_mogg.py` is an
independent implementation of the same deobfuscation, useful as a cross-check of
`kDeobfuscatedKeyTable` in `src/hooks/crypto.cpp`.

`.claude/skills/audio-verify/SKILL.md` is the thing we should adopt wholesale for
M5, because "music plays" is otherwise a vibes-based test. Their method:

* **identity** — chroma cross-correlation against a reference rendering;
* **speed** — resample-search to detect pitch/tempo drift;
* **distortion** — clip/flat-top detection.

That converts our B-009 verification from "the log shows the key was installed"
to "the decoded stream matches the reference", which is the difference between a
plausible fix and a proved one.

### 7.2 The DTA overlay technique

`docs/native/DTA_OVERLAY_ENGINE.md`: a git-tracked `native/dta/` overlay shadows
extracted assets **on read**, rejecting `..`. Their war story is instructive: a
`button_meanings` block missing from the shipped Xbox-flavoured `config/joypad.dta`
made every menu key resolve to `kAction_None`, so **all input silently did
nothing** — which is precisely the class of bug our M4 input work will hit.

The Xbox-specific finding does not transfer; the *technique* does, and it pairs
with band3's `NewFile` hook (§4.1) to give us overlay-without-repacking.

### 7.3 Methodology worth copying

`docs/native/NATIVE_HACK_AUDIT_2026-06-08.md` is a 79-finding audit collapsed to
73 unique, each classified `IN_SCOPE_FIXABLE` / `BLOCKED` / `OUT_OF_SCOPE_GLUE` /
`BENIGN`. Two lessons:

* Their dominant bug class is "faithful behaviour switched off" (`gDeforms = 0`):
  places where a port silently disables engine behaviour instead of implementing
  it. **We should audit our own hooks for that** — a hook that early-returns is a
  bug unless documented as such.
* The port had **zero automated tests**, so every fix was "verified once and
  forgotten" until a gtest harness existed. We are in the same position: our
  `src/hooks/crypto.cpp` deobfuscation is pure host code and could be unit-tested
  against the known plaintext keyset without ever booting the game. Do that when
  the toolchain lands; it is the cheapest durable guarantee we can buy.
* **Done 2026-09-19.** The pure half of that path was split into
  [`src/hooks/crypto_keytable.h`](../src/hooks/crypto_keytable.h) so it can be
  reached without the SDK, and [`tests/crypto_keytable_tests.cpp`](../tests/crypto_keytable_tests.cpp)
  (dependency-free harness in `tests/check.h`) now runs under `ctest` as
  `crypto_keytable`. It covers the 0xE0 id bias, the slot clamp, the
  `0x8280C568` table-address bounds, the version → entry mapping of
  `0x823DE070`, and the B-009 regression itself: installing by key id would select
  a different key than installing by buffer offset for MOGG versions 14–16. The
  plaintext bytes are *not* duplicated into the test, per §9 below.

Also filed as a warning: their `MILO_TRY`/`MILO_CATCH` broke on LP64 because
`Debug::Fail` longjmps a `const char*` as an `int`, truncating the pointer. When
we get to the `longjmp_address`/`setjmp_address` work below, expect the same class
of width bug.

## 8. Things we are *missing* in our own config

| Gap | Detail | Action |
| --- | --- | --- |
| `longjmp_address` / `setjmp_address` | Our 0.10 SDK supports both at `[entrypoint]` level (`config.cpp` 124–131). band3 sets `longjmp_address = 0x82BBB620`, `setjmp_address = 0x82BBBA50`. **We set neither**, so guest longjmp/setjmp has no host bridge. | Find Blitz's equivalents. Cheap, and it may already explain odd error-path deaths. |
| `d3d12_readback_resolve` | band3 has it in its **manifest**. In 0.10 it is a **cvar** (`graphics/d3d12/command_processor.cpp`), and the runtime profile is a flat recursive cvar table (`src/core/cvar.cpp` `ApplyTomlTable` joins nested tables with `_`). | Put it in the build-tree-local `rb_blitz.toml` (`out/build/<preset>/rb_blitz.toml`, alongside `mnk_mode`, `log_level`, `[log.levels]`), **not** the manifest. |
| Symbol names | 37,851 anonymous functions (§2). | Name the proved ones. |
| Hook hygiene | Our hooks have no "why is this disabled" record (§7.3). | Each hook file gets a header comment stating the faithful behaviour and the reason for deviating. |

## 9. Borrowing rules

As of 2026-09-19 this project is **GPL-2.0-only** (`LICENSE` at the repo root,
stated in the [README](../README.md)). That choice was made *because*
`band3_recomp` is GPL-2.0: the two are compatible, so its code can be adapted
directly instead of only re-derived. Every hook in §4 is now fair game to port,
not just to mimic.

* **Adapting band3 code is allowed** and is often the right move for the hooks in
  §4 (`file.cpp`, `patches.cpp` are hand-transcribed and hard-won). Keep the file
  under GPL-2.0, say so in a header comment, and add a row to the provenance
  table below. Do not relicense the project to GPL-3.0 — it would cut off the
  GPL-2.0 sources.
* **Prefer knowledge where a clean-room implementation is cheaper** than an
  adaptation: `math.cpp`'s native trig and the 24 KB `CamShot__Shake`
  transcription are candidates for re-implementation from Blitz's own
  disassembly.
* **The `freeqaz` projects are CC0-1.0**, so nothing is owed for their code
  either — but they are decompilations of copyrighted game code: port the
  structure, never vendor the tree.
* **Addresses still never transfer.** Same engine, different image, different
  link order: every offset, field offset and function address in this document is
  a *hint about structure*, never a constant to paste. Porting band3's `file.cpp`
  verbatim would hook the wrong Blitz function.
* **Field offsets are the risky part.** `RndMat__Load`'s `+0x118`/`+0x99` and
  `AddHeap`'s heap names are the most likely things to be silently wrong if
  copied unverified; confirm each against Blitz before trusting it.
* **Do not vendor the clones, the game image, or the deobfuscated keyset** into
  the repo. Reference clones live outside the tree, or as documented `git clone`
  commands.

### Provenance of adapted code

Any file that adapts code from another project must be listed here with its
origin. An empty table is the correct state until something is actually ported.

| Our file | Adapted from | Their commit | Notes |
| --- | --- | --- | --- |
| *(none yet)* | | | |

## 10. Where this lands

| Section | Goes to |
| --- | --- |
| §1–§3 (hook mechanism, names, midasm) | [prompts/04](../prompts/04-menus-content-input-saves.md) |
| §4 (port catalogue) | [prompts/04](../prompts/04-menus-content-input-saves.md), [05](../prompts/05-complete-one-song.md) |
| §5–§6 (RB3DX cross-reference) | [prompts/04](../prompts/04-menus-content-input-saves.md) (groups 2–4), [05](../prompts/05-complete-one-song.md) (6–9) |
| §7.1 (audio verification) | [prompts/05](../prompts/05-complete-one-song.md) |
| §7.3, §8 (methodology, config gaps) | [prompts/06](../prompts/06-reproducible-release.md), [README](../prompts/README.md) |
| §9 (borrowing rules) | [README](../README.md) (license), [prompts/06](../prompts/06-reproducible-release.md) (distribution hygiene) |
| §0 (Rock Band Blitz Deluxe row) | [deluxe-compat.md](deluxe-compat.md) — the Blitz-side sibling of the RB3DX groups catalogued in §5–§6 |
