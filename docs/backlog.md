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

> **The customization scope's research plan:** [plans/customization-plan.md](plans/customization-plan.md)
> plans the function-naming, flow-mapping, probe and harness work that the nine UI / resolution /
> save / DLC enhancements need. Its S1 *is* the naming campaign above, and its flow maps are what make
> the remaining "Engine knowledge still to port" items cheap. No feature code lives there.

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
      installed game's settings (General/Graphics/Controller; the Experimental tab is
      withdrawn, D14), ships in the installer payload beside `rb_blitz.exe` and launches
      it. The current milestone is M1, "the launcher with the graphical settings"
      (§1.1): three tabs, Graphics whole, General limited to the launch target plus the
      save and DLC locations, and the launcher fully controller-navigable. It is written
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

- [ ] **Pin the sheet format and offset, then a reskin is a two-command job.** The claim
      that this already worked is withdrawn ? the pixel diffs it rested on were the animated
      backgrounds, not the art ([assets.md](assets.md) "Where the art would have to live,
      and a retraction"). What is solid: the payload route delivers a whole replacement
      scene (`gen/main_xbox_0.ark` in a payload directory is the copy the game uses, run-log
      confirmed; a payload with only `patch_xbox.*` mounts but prefers no copies); the
      archive index carries per-entry sizes, so a replacement scene can be any size; the
      360 and PS3 `buttons.milo_xbox` differ across one span, **stream `0x5C9`?`0x405DF`,
      262,167 bytes = a 512?512 sheet plus 23**, which is where a reskin has to aim; and the
      span has **16-byte elements** (pitch-16 neighbour difference is 26 where every other
      pitch sits at 55+) with a working **mip chain** on `controller_config`'s pad texture
      (DXT5 1024?1024, level 1 correlates 0.86 with the half-scaled level 0, alternating
      cleanly on a 16-byte period). DXT5 is the leading hypothesis; it is not confirmed.
      The next run should write one flat DXT5 colour over the span and check the prompt band
      with OCR plus a settled-frame A/B, which answers format and location at once.
- [ ] **Measure the noise floor before believing any screenshot diff.** Two runs of the
      same build differ by **3.5?5.9%** of the frame just after a screen is entered and
      ~0.14% once it has settled, and the prompt band sits on top of the animated aurora ?
      which is exactly how a whole round of "proven" swaps turned out to be nothing. Put a
      same-build baseline in the loop, prefer OCR of static text as the control, and treat a
      pixel diff as corroboration.
- [ ] **The controller page's pad art is byte-identical across platforms.** 1,398,101 bytes
      of `controller_config` match between builds after a 16-bit swap, so there is nothing to
      gain by sourcing that art from the PS3 dump ? the button sheet is the only
      platform-different art found so far.

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
