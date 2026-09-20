# Vanilla vs Rock Band Blitz Deluxe — content-variant policy

Policy decision recorded 2026-09-19. **No code exists for this yet.** This file
exists so the decision is not re-litigated each session, and so that Milestone 4/5
effort is not spent on a job that belongs to another project.

## The decision

One executable, two supported content variants:

| Variant | Who owns it | What it is |
| --- | --- | --- |
| **Vanilla** | us, from day one | The retail Xbox 360 dump used as the compilation input. Its built-in offline mode ("Proceed in Offline Mode?") is the supported route. |
| **Rock Band Blitz Deluxe** | the MiloHax mod; we only make it *work* | A quality-of-life mod installed by copying files over a vanilla game folder. Where unblocked/restored online behaviour and QoL features come from. |

This project **does not restore, emulate or route around online services** — not
Rock Central sign-in, not leaderboards, not achievements, not challenges, not
store/DLC enumeration. Blitz already ships an offline mode, so "unblocking online
restrictions" was never a requirement for a playable port, and it is out of scope
permanently. The community mod is where that work lives, exactly as Rock Band 3
Deluxe (RB3DX) is where it lives for Rock Band 3 (see
[rb3-references.md](rb3-references.md) §5–§6).

**Our goal is compatibility, not reimplementation.** A user who installs Rock Band
Blitz Deluxe into their game folder should get a working recomp by drag-and-drop:
no recomp-specific second build, no recomp-side patcher to run first, and no asking
them to uninstall the mod. A user who keeps vanilla must be unaffected.

## What the mod is, and how it installs

- Repository: [`solamint/rock-band-blitz-deluxe`](https://github.com/solamint/rock-band-blitz-deluxe)
  — "QoL enhancements and additions to Rock Band Blitz", by MiloHax. Checked
  2026-09-19: the repository root carries no `LICENSE` file and its feature list
  section is still a stub, so re-read it before starting any Deluxe work.
- It targets **PS3** (`NPUB30749`, a `.pkg`) and **Xbox 360**. Only the Xbox 360
  flavour matters here; the PS3 package is not a thing we can consume.
- The Xbox 360 payload is a set of files copied *over* an existing vanilla install:
  a patched `default.xex` plus `gen/` (ark content), `nxeart`, `charnames.zbm`. It
  also ships optional song packs through the normal content path. That
  drag-and-drop shape — not a from-scratch repack — is what makes compatibility
  achievable at all.
- The install guide tells users to rename `default.xex` → `default_vanilla.xex`
  first, and notes that the mod **rolls TU5 into its base installation**, so title
  updates must be disabled. Its install section is marked *not final*; the layout
  can change between releases.

## Why the Deluxe executable is not swappable

Our translation is generated from **one specific retail `default.xex`**:
`config/functions.toml`, the discovered function boundaries, the jump tables, the
import ordinals and every byte-guarded patch are addresses in *that* image. The
Deluxe `.xex` is a different image. Therefore:

- **The codegen input stays vanilla.** A build checkout needs the vanilla
  `default.xex`; the mod's own backup step (`default.xex` → `default_vanilla.xex`)
  is the intended way to keep it. Recompiling the Deluxe image is out of scope —
  it would invalidate the entire analysis (38,344 discovered functions, 262
  imports, 132 jump tables).
- **The data overlay is the supported surface.** An installed Deluxe copy *is* a
  candidate `--game_data_root`, because content is read through the VFS at runtime
  and the arks are data.
- **Deluxe code deltas are reproduced on our side.** Whatever the patched `.xex`
  changes that we actually need (an offline branch, a gate, a checksum) belongs in
  the project layer as a documented hook or patch with the guest address and the
  evidence, following the plan's fix order — never as "replace your executable".
  A Deluxe-root run must not be the gate for such a fix: if we need the behaviour,
  it has to work for vanilla too.

## Hazards to expect on the first Deluxe-root run

| Hazard | Why it bites | Where the answer lives |
| --- | --- | --- |
| `update:` mount / TU5 | Deluxe folds TU5 into its base install, while our `update:\gen\patch_xbox.hdr` read already fails with `0xc000000f` on a dump with no title update. | [known-issues.md](known-issues.md), [rb3-references.md](rb3-references.md) §6 group 8 (`"UPDATE:"` → `"D:"`) |
| Fingerprinting must not fail closed on an overlay | `config/game_fingerprints.toml` was written for the vanilla dump and is consumed by nothing; a Deluxe root changes `gen/` and the `.xex`, so whenever it gains a consumer it has to distinguish *vanilla image* from *content-root variant*. | [known-issues.md](known-issues.md), [build-and-run.md](build-and-run.md) |
| Menu/input data is replaced | The mod ships its own `_ark/ui/…` and `config` data; a missing or renamed DTA block can silently kill every menu binding (the RB3 native port's `button_meanings` war story). | [rb3-references.md](rb3-references.md) §7.2 |
| Content-enumeration counts change | Our verified offline state (`XamContentCreateEnumerator: added 2 items`, aggregate enumerator `0 items`) is a **vanilla** baseline. | [bringup-log.md](bringup-log.md), [prompts/05](../prompts/05-complete-one-song.md) |
| Mod churn | Its install instructions are explicitly unfinished, so a Deluxe release can move files we documented. | this file |

## What "Deluxe works" will mean

Deluxe compatibility is a **post-bring-up backlog item**, not a bring-up milestone
and not part of the first release's definition of "working". When it is taken up,
"works" means:

1. The same `rb_blitz.exe`, pointed at a Deluxe game-data root, boots to the main
   menu and completes a bundled song — while the vanilla route still passes.
2. Any recomp-side change needed for that run is a documented hook or patch with a
   guest address and evidence: no monkey-patching, no per-variant build flags, no
   `default.xex` fork.
3. This file and [known-issues.md](known-issues.md) state what a Deluxe root does
   and does not give us, per release.

## What we never do

- Reimplement or emulate Rock Central, leaderboards, achievements, challenges,
  social features, or the store. "Online works" in the Deluxe sense is the mod's
  behaviour on its patched image; our job is to not break it.
- Ship, mirror or vendor Rock Band Blitz Deluxe, its patched `.xex`, its `gen/` or
  `_ark/` data, or anything extracted from it. A Deluxe install is user-supplied
  game data, on the same footing as the retail dump
  ([prompts/06](../prompts/06-reproducible-release.md) §5).
- Support the PS3 `.pkg` flavour.
