# Backlog

What is still pending, most important first. Every entry is a task, and the order is
the order to pick them up in. The bring-up work — scaffold, codegen, boot, menus,
input, persistence, songs, Ultimate, DLC — is closed; its chronology is in
[history/bringup-log.md](history/bringup-log.md) and the limits it left standing are
in [known-issues.md](known-issues.md).

## 1. Engine knowledge still to port

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

## 2. Ultimate compatibility

- [ ] **Script an Ultimate run** — boot → menu → a song against an installed payload.
      The install itself landed and was verified by hand on 2026-09-20
      ([ultimate-compat.md](ultimate-compat.md) §7); what is missing is the scripted
      run that closes acceptance criterion 1 with evidence instead of an observation
      ([ultimate-compat.md](ultimate-compat.md) §10).
- [ ] Re-derive the 12-byte patch table if a payload release ships a different
      `default.xex` (compare its hash against `390e0ae0…` first).

## 3. The launcher — the one active design

- [ ] [plans/launcher-plan.md](plans/launcher-plan.md) — a launcher that owns the
      installed game's settings (General/Graphics/Controller/Experimental), ships in the
      installer payload beside `rb_blitz.exe` and launches it. It is written
      prompt-by-prompt with waves, a dependency graph and an open-questions list that has
      to be answered before wave 0. It absorbed the per-device remap design: lane C
      builds the remap core and the launcher's Controller → Manual page is its panel.
      It also reuses the game's own UI art where it can (D15, [§10](plans/launcher-plan.md)):
      background, logo, button prompts and the controller diagrams, all derived from the
      user's own game data on their machine and never shipped. §10 has the inventory of
      what is reusable and where it lives, the capture route that works today, and the one
      parsing detail that still blocks reading the art straight out of `game/`.
      Nothing in it gates release readiness.

## 4. Deferred, with no design yet

- [ ] **Scene texture extract/import: pin the buffer offset, then a scene reskin is a
      two-command job.** Everything else is in place and verified. The map exists
      ([scripts/hmx_milo.py](../scripts/hmx_milo.py) `records`: **706 texture records in
      151 scenes**, with each texture's builder source path and dimensions — the button
      prompts are `buttons.milo_xbox`'s `../image/icons_buttons_xbox_nomip.bmp` at 512×512,
      the controller diagrams are `controller_config.milo_xbox`'s `img/xbox_0..3.png` and
      `img/ps_0..3.png` at 1024×1024, the lane icons are `track_shared_textures`), the
      delivery works (editing a scene inside the archive changes what the game draws,
      proven on the A/B prompts), and a texture is 8-bit with `0x00` transparent and
      `0xFF` opaque. What is missing is the one thing an extractor needs: **where the
      pixel buffer starts**. The dimensions follow from the record and so the buffer size
      follows from them (`buttons.tex`: 512 × 512 × 1 = 262,144 bytes, exactly the region
      whose edit changed the screen), but the writer interleaves other objects —
      materials, and for a font its `Glyph` table — into the same section, so the offset
      has to be derived per texture rather than assumed. Read the object records between
      a texture and the next one (the `Glyph` rectangles are the natural first target,
      since they also give the sample-to-screen mapping the last experiments could not
      pin down), then add `extract`/`import` to `hmx_milo.py`. Evidence and the rejected
      hypotheses — not a mip chain, not DXT — are in
      [assets.md](assets.md) "Where the art actually lives".
- [ ] **Use the PS3 build as the decoder oracle.** The PS3 dump is the same game in the same
      container — `.\scripts\extract_assets.ps1 -Platform ps3 -GameRoot <USRDIR>` writes
      `extracted-ps3\`, and every tool in `scripts\` reads it unchanged ([assets.md](assets.md#ps3))
      — which gives the open texture problem something last round did not have: the same asset
      from an independent build, at the same offset. `buttons.milo_ps3` is the same size as its
      360 twin, its object records are **99.5% identical**, its texture record is the same, and
      only the pixel region differs — as a **16-bit word swap**, 70% matching after swapping byte
      pairs against 20% left alone, collapsing to 5% at a wrong offset, so the structure is real
      and the format is **16-bit elements**. The region is raw (no zlib/raw-deflate/gzip/lzma
      framing; it deflates to 20%) and shows no glyph-grid periodicity. A correct decoding has to
      read *both* copies, and the 70% they share is what separates "this is the image" from "this
      is a per-platform re-encode" — that is the test to build the next attempt around.

Pixel-perfect graphics and UI upgrades, unlocked frame rate, latency tuning,
custom-song/export compatibility, other controller backends, Linux/macOS/ARM hosts,
packaging and auto-update, symbol-name campaigns, and mod APIs. DLC packages do load
([dlc.md](dlc.md)) — what is still undefined is everything around them, from a store UI
to custom-song content that no content API ever enumerated.

Dropped for now, their documents deleted with them: in-game host Audio/Video settings,
the per-device button-mapping layer (its remap half lives on in the launcher plan), and
the VR port. Anything needed from them has to be re-derived.

## 5. Permanent non-goals

Online services — Rock Central, leaderboards, achievements, challenges, multiplayer.
Blitz ships its own offline mode, so restoring them was never required, and unblocking
them is the community mod's job. Our obligation runs the other way: staying compatible
with the **Rock Band Blitz Ultimate** payload ([ultimate-compat.md](ultimate-compat.md)).

## 6. Ground rules

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
