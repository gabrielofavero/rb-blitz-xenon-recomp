# Backlog

What is still pending, most important first. Every entry is a task, and the order is
the order to pick them up in. The bring-up work — scaffold, codegen, boot, menus,
input, persistence, songs, Ultimate, DLC — is closed; its chronology is in
[history/bringup-log.md](history/bringup-log.md) and the limits it left standing are
in [known-issues.md](known-issues.md).

## 1. Fixes implied by open known-issues

- [ ] **Hook hygiene**: every hook file states the faithful behaviour and the reason
      for deviating, including each early return.
- [ ] **Host test coverage beyond the seven existing targets** — `crypto_keytable`,
      `payload_overlay`, `path_policy`, `fingerprint`, `ui_nav`, `dlc_layout` and
      `toolchain`.
      Everything that needs a boot is still verified by hand: the SDK-touching hooks,
      pause/resume, and the UI paths outside the offline song loop.
- [ ] **Mouse navigation limits** are accepted, not fixed — a screen whose rows a
      frame difference cannot read is given up on rather than measured, there is no
      guest cursor, and the overlay/foreground gates are reasoned rather than
      exercised. The absolute mapping the old note asked for exists now: the guest's
      own frames are read back (`CaptureGuestOutput`) and the highlight is measured
      onto the pointer's row. What is left is the relative *fallback*, used only when
      frames cannot be read.

## 2. Engine knowledge still to port

[rb3-references.md](rb3-references.md) is the catalogue; these are the items in it
that name an action.

- [ ] **Name functions**: the three tail-branch thunks, then the MOGG rows, then the
      wider anonymous set ([symbols.md](symbols.md)). Names go in
      `config/functions.toml` in the same commit as the doc update.
- [ ] Move `d3d12_readback_resolve` out of the manifest into the build-tree-local
      `rb_blitz.toml`.
- [ ] Check the RB3DX groups 2–10 against Blitz one by one; the `NewFile` hook is P1.
- [ ] Adopt the audio-verify methodology (chroma correlation, speed, distortion) as a
      regression check (§7.1).
- [ ] Add a provenance-table row for anything adapted from another project (§9).

## 3. Ultimate compatibility

- [ ] **Script an Ultimate run** — boot → menu → a song against an installed payload.
      The install itself landed and was verified by hand on 2026-09-20
      ([ultimate-compat.md](ultimate-compat.md) §7); what is missing is the scripted
      run that closes acceptance criterion 1 with evidence instead of an observation
      ([ultimate-compat.md](ultimate-compat.md) §10).
- [ ] Re-derive the 12-byte patch table if a payload release ships a different
      `default.xex` (compare its hash against `390e0ae0…` first).

## 4. The launcher — the one active design

- [ ] [plans/launcher-plan.md](plans/launcher-plan.md) — a launcher that owns the
      installed game's settings (General/Graphics/Controller/Experimental), ships in the
      installer payload beside `rb_blitz.exe` and launches it. It is written
      prompt-by-prompt with waves, a dependency graph and an open-questions list that has
      to be answered before wave 0. It absorbed the per-device remap design: lane C
      builds the remap core and the launcher's Controller → Manual page is its panel.
      Nothing in it gates release readiness.

## 5. Deferred, with no design yet

- [ ] **Texture swapping for the in-game UI** — the mechanism works and the first target is
      located; what is left is one mapping. Verified: editing a *scene* inside the archive
      changes what the game draws (zeroing a 262,144-byte region of
      `ui/resource/fonts/gen/buttons.milo_xbox` removes the main menu's A/B button
      prompts; filling it makes them opaque). That region is the font's **512×512,
      row-bytes-512, single-level 8-bit** glyph sheet, sitting in chunk 0 — which every
      scene keeps *stored*, so a patch never needs recompression. The byte is ink
      coverage, `0x00` transparent and `0xFF` opaque, with the material supplying the
      colour. Open: the sample-to-screen mapping (a spatially varying pattern comes back
      as a one-pixel seam, which points at the font's per-character `Glyph` rectangles —
      `x`, `y`, `width`, `height` — rather than a row-major sheet). Read those glyph
      records next; the evidence, offsets and the four experiments are in
      [assets.md](assets.md), "Where the art actually lives". The standalone
      `*.png_xbox` / `*.bmp_xbox` path is already done
      ([scripts/hmx_tex.py](../scripts/hmx_tex.py), DDS round trip bit-exact) and
      [scripts/hmx_milo.py](../scripts/hmx_milo.py) reads any scene's chunk stream and
      lists the `.tex` assets a screen owns.

Pixel-perfect graphics and UI upgrades, unlocked frame rate, latency tuning,
custom-song/export compatibility, other controller backends, Linux/macOS/ARM hosts,
packaging and auto-update, symbol-name campaigns, and mod APIs. DLC packages do load
([dlc.md](dlc.md)) — what is still undefined is everything around them, from a store UI
to custom-song content that no content API ever enumerated.

Dropped for now, their documents deleted with them: in-game host Audio/Video settings,
the per-device button-mapping layer (its remap half lives on in the launcher plan), and
the VR port. Anything needed from them has to be re-derived.

## 6. Permanent non-goals

Online services — Rock Central, leaderboards, achievements, challenges, multiplayer.
Blitz ships its own offline mode, so restoring them was never required, and unblocking
them is the community mod's job. Our obligation runs the other way: staying compatible
with the **Rock Band Blitz Ultimate** payload ([ultimate-compat.md](ultimate-compat.md)).

## 7. Ground rules

Keep four layers separate: **game input** (locally dumped content — read-only,
untracked, fingerprinted), the **pinned SDK** (no title-specific edits), **generated
code** (regenerate, never hand-edit), and **project code** (manifest, hints, hooks,
patches, diagnostics — every piece of Blitz knowledge lives here). Change the SDK
only when the behaviour is generically wrong for Xbox 360 software and could go
upstream, and then as a patch file ([patches/README.md](../patches/README.md)).

Fix order: correct boundaries or hints → an existing SDK implementation →
a whole-function hook with the guest address recorded → a named data/code patch →
a mid-ASM hook → an SDK patch file. Every hook or patch states its guest address,
the observed failure, the intended behaviour, and the evidence.

Record as you go: chronology in [history/bringup-log.md](history/bringup-log.md),
enduring facts in [known-issues.md](known-issues.md) and [symbols.md](symbols.md),
cross-project knowledge in [rb3-references.md](rb3-references.md). A disproof edits
the document that made the claim.
