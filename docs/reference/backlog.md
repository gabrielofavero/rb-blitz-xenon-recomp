# Backlog

What is still pending, most important first. Every entry is a task, and the order is
the order to pick them up in. The bring-up work — scaffold, codegen, boot, menus,
input, persistence, songs, Ultimate, DLC — is closed; its chronology is in
[history/bringup-log.md](../history/bringup-log.md) and the limits it left standing are
in [known-issues.md](known-issues.md).

## 1. Engine knowledge still to port

[rb3-references.md](rb3-references.md) is the catalogue; these are the items in it
that name an action.

- [ ] **Name functions**: the first batch landed 2026-10-03 — the three tail-branch
      thunks and the six MOGG functions, named in `config/functions.toml` with the
      rule, evidence and confidence in [symbols.md](symbols.md) "Named symbols".
      What remains is the wider anonymous set; names keep going in
      `config/functions.toml` in the same commit as the doc update.
- [ ] Check the RB3DX groups 2–10 against Blitz one by one; the `NewFile` hook is P1.
- [ ] Adopt the audio-verify methodology (chroma correlation, speed, distortion) as a
      regression check (§7.1).
- [ ] Add a provenance-table row for anything adapted from another project (§9).

> **The customization scope's research plan:** [plans/customization-plan.md](../plans/finished/2026-10-03-customization-plan.md)
> plans the function-naming, flow-mapping, probe and harness work that the nine UI / resolution /
> save / DLC enhancements need. Its S1 *is* the naming campaign above, and its flow maps are what make
> the remaining "Engine knowledge still to port" items cheap. No feature code lives there.

## 2. Ultimate compatibility

- [ ] **Script an Ultimate run** — boot → menu → a song against an installed payload.
      The install itself landed and was verified by hand on 2026-09-20
      ([ultimate-compat.md](../guides/ultimate-compat.md) §7); what is missing is the scripted
      run that closes acceptance criterion 1 with evidence instead of an observation
      ([ultimate-compat.md](../guides/ultimate-compat.md) §10).
- [ ] Re-derive the 12-byte patch table if a payload release ships a different
      `default.xex` (compare its hash against `390e0ae0…` first).

## 3. Controller Settings — the one active design

> **Superseded, 2026-10-19.** The settings application this section tracks was retired (P8b,
> [gameplay-input-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md)
> §17.4/§19). Its rows now live in the in-game settings store and its input profile is the game's
> own `launcher.toml`; the two scripts named below no longer exist. The items are kept as the
> record of the launcher plan's open work, not as work in progress.

- [ ] [plans/launcher-plan.md](../plans/finished/2026-09-27-launcher-plan.md) — Controller Settings that owns the
      installed game's settings (General/Graphics/Controller; the Experimental tab is
      withdrawn, D14), ships in the installer payload beside `rb_blitz.exe` and launches
      it. The current milestone is M1, "Controller Settings with the graphical settings"
      (§1.1): three tabs, Graphics whole, General limited to the launch target plus the
      save and DLC locations, and Controller Settings fully controller-navigable. It is written
      prompt-by-prompt with waves, a dependency graph and an open-questions list that has
      to be answered before wave 0. It absorbed the per-device remap design: lane C
      builds the remap core and Controller Settings' Controller → Manual page is its panel.
      It also reuses the game's own UI art where it can (D15, [§10](../plans/finished/2026-09-27-launcher-plan.md)):
      background, logo, button prompts and the controller diagrams, all derived from the
      user's own game data on their machine and never shipped. §10 has the inventory of
      what is reusable and where it lives, the capture route that works today, and the one
      parsing detail that still blocks reading the art straight out of `game/`.
      Nothing in it gates release readiness.

      **Status, 2026-10-05.** M1 ("Controller Settings with the graphical settings") is finished, and so are
      packaging (D3–D5), first-run prefill (D4) and the whole evidence lane — E1's test names, E2's
      capture harness, E3's installed end-to-end run and E4's write-up are all in the tree. Six prompts
      are left: A4 (cover art), A5 (accessibility and recovery), and C1, C2, C6 (the Controller tab's
      remaining lanes), none of them gated by anything. Re-measuring any of it is scripted rather than
      manual now: `ctest -R Controller Settings`, `scripts/capture_controller_settings.ps1` (the ring, the bar, a tab's body)
      and `scripts/acceptance_controller_settings.ps1` (install the built setup, boot four legs, read the game's
      own log).

- [ ] **Make Controller Settings' settings reach a game Controller Settings did not start** — D3's rank 4, "the
      profile applied as a config file at the top of `OnConfigurePaths`", is not in the tree: measured
      2026-10-05, a top-level key in a profile handed to `--launcher_profile` does not reach a cvar, and
      Controller Settings' rows arrive only as `--<key>=<value>` flags (`plans/launcher-plan.md` §11.6). So
      *Launch Game* is covered and a double-click, or the game's own Start-menu shortcut, is not: it
      boots with `rb_blitz.toml` and the compiled defaults. Either apply `[settings]` at config rank
      before the game's own file — which is what D3 describes, and what R4's standalone half needs —
      or drop rank 4 and document that Controller Settings is the entry point. One decision, then a small hook
      or a documentation change.

- [ ] **One store for the General tab's two path rows** — the panel writes `[launch] user_data_dir` /
      `dlc_dir` and `launch/src/game_launch.cpp` reads `[settings] user_data_root` / `dlc_root`, so a
      folder picked in the panel is shown by the panel and never passed to the game (measured
      2026-10-05, `plans/launcher-plan.md` §11.7, `known-issues.md`). Pick one store, bridge or migrate
      the other, and pin it with a test that the panel's own value is what `--print-command` carries.

## 4. Deferred, with no design yet

- [ ] **Shader preparation residual release gates (P9 close-out, 2026-10-25):**
      owner-driven installed cold/warm D3D12 and Vulkan, Vulkan native reuse,
      rov/fsi and retail, cancellation/focus/trial/error routes, and other hardware.
      Exercise a genuinely missing recipe on patch 0034 and verify frame continuity,
      eventual effects, audio and input recovery. Dynamic scene/material pairings
      still add recipes; continue organic discovery without promising universal
      first-song coverage. Investigate non-compilation song-start stalls separately.
      [Shader-cache.md](../engine/shader-cache.md) parts 8-9 retain evidence and limits.

- [x] **Runtime async submission drained/waited for unprepared pipelines**
      (measured 2026-10-25, `-vulkan-ultimate-fbo-cold-r3`: 6 ms placeholder,
      230 ms `compile_wait`, 271 ms frame). Implemented in SDK patch 0034:
      both backends keep runtime creation on workers in async mode, Vulkan
      publishes replacements at submission boundaries, and incomplete frames now
      present by default rather than holding the entire picture. Loading barriers
      and explicit synchronous mode remain blocking. **Gameplay verification is
      pending**; translation, synchronous placeholder creation and other first-use
      costs remain outside this bound. Missing effects may last several frames,
      depending on compilation and guest redraws. Development builds now journal
      uncovered portable recipes for organic catalogue growth. Evidence and
      limits: [shader-cache.md](../engine/shader-cache.md) part 8.
- [x] **The Vulkan presenter rebuilt its guest-output paint pipeline on every present**
      (measured 2026-10-25, P8's `-vulkan-ultimate-fbo-cold`;
      **fixed the same day** in `patches/rexglue-sdk/0033-vulkan-presenter-reuse-paint-pipeline.patch`):
      `vulkan_presenter.cpp` compared a stored `swapchain_format` it never assigned, so
      the cached pipeline was destroyed and rebuilt per present - **2,026 creations of
      one identity** in one run (~100 us each, up to 2.3 ms, 209 ms during an 86.6 s
      song for the 1,473 in-song calls). Recording the format at creation makes the
      cache work; frames were healthy either way (29.46 ms mean), so this was waste,
      not breakage. In-game verification is the `-vulkan-ultimate-fbo-cold-r2` row.
      Evidence: [shader-cache.md](../engine/shader-cache.md) part 8.
- [ ] **The compile trace cannot report how many distinct helper identities a phase
      used.** Detail lines are suppressed once the detail limit is spent, and the
      suppressed lines on Vulkan are exactly the ones a reader needs, so the P8
      acceptance has to reason from **totals** (and says so). A per-(phase, event)
      distinct-key count in the `shader_trace: summary` line would turn "1,473 helper
      calls, no new identity" from an inference into a measurement. Small change to
      `rex::graphics::compile_trace`, one more field in the P8 report parser.

Framerate expansion has a [feasibility study and implementation plan](../plans/todo/2026-10-25-framerate-expansion-plan.md)
as of 2026-10-25. It is no longer undesigned: implementation remains pending,
with 120 FPS the first high-rate candidate and no safe upper ceiling established.
The older "unlocked frame rate" deferral below is superseded by
that plan; it does not claim that a high-rate mode has shipped.

- [x] **Pin the button sheet format and offset.** The 2026-10-19 measurement and P0's
      `hmx_milo.py texture` command decode the 360 and PS3 512×512 button sheets at
      header `0x5C9`, payload `0x5E8`, with a 23-byte header, eight-byte zero preamble,
      DXT5 and an Xbox-only 16-bit word swap. The earlier `0x5E0` decode was eight
      bytes early and produced visibly corrupted PNGs.
      The same command decodes `blitz_icons` and all eight controller pad images. Both
      platform scenes' two `Glyph` tables match their counterparts, and
      `assets/controller/slots.toml` records the 32 drawable rectangles. See
      [assets.md](../guides/assets.md#button-and-controller-scene-textures-measured-2026-10-19-automated-in-p0).
      A live, safe texture replacement remains in the
      [button layout hot-swap plan](../plans/in-progress/2026-10-19-button-layout-hotswap-plan.md)
      P1; offline decode alone does not establish live swapping.
- [x] **Measured 2026-10-04 - the noise floor is not one number.** [observing.md](../engine/diagnostics.md)
      carries the per-state table and [known-issues.md](known-issues.md) B-015 the reason for it: at
      `-CompareScale 4` only `help-options` and `exit-confirm` reproduced at 0 %, with `song-list` at
      0.473 %, while the title screen alone measured 0.046 %, 1.978 %, 18.28 %, 10.74 % and 1.894 %
      across five same-build pairs (three of them OCR-identical as `TO START`). The rule stands and is
      now enforced rather than advised: put a same-build baseline in the loop, prefer OCR of static
      text as the control, treat a pixel diff as corroboration, and do not gate a run on a diff for a
      screen that has no settled frame (`Settled = $false`) or against a baseline taken at another
      scale. The old 3.5-5.9 % / ~0.14 % pair was measured on ~1 Mpx captures and does not transfer to
      the 8.3 Mpx ones here.
- [ ] **The controller page's pad art is byte-identical across platforms.** 1,398,101 bytes
      of `controller_config` match between builds after a 16-bit swap, so there is nothing to
      gain by sourcing that art from the PS3 dump ? the button sheet is the only
      platform-different art found so far.

- [ ] **Investigate UI text and star-edge pixel imperfections.** The owner reported small breaks
      near centered menu elements on D3D12 and Vulkan. The 2026-10-29 D3D12 guest capture
      reproduced the `Audio/Video` close-up, but a fixed screen-center seam and an upstream
      cause remain unproven. Follow the
      [investigation plan](../plans/todo/2026-10-29-ui-pixel-imperfections-investigation-plan.md)
      P0–P3; fix only after the affected stage is isolated.

Pixel-perfect graphics and UI upgrades, unlocked frame rate, latency tuning,
custom-song/export compatibility, other controller backends, Linux/macOS/ARM hosts,
packaging and auto-update, symbol-name campaigns, and mod APIs. DLC packages do load
([dlc.md](../guides/dlc.md)) - and the store UI around them and custom-song content no content API
ever enumerated are **built and acceptance-measured**: the retail DOWNLOAD CONTENT screens are fed
from the RhythmVerse customs catalogue, on by default behind General ▸ Downloadable Content ▸
RhythmVerse and silent when that switch is off ([finished
plan](../plans/in-progress/2026-10-13-rhythmverse-store-plan.md), whose §5/P8
record holds the acceptance matrix and `scripts/acceptance_store.ps1`; the closed-out run is
`out/store-acceptance/p8-final/`, 69 steps and 0 failures — the switch, Recent, Search, a download,
the install, the song on the title's own next-boot list, and the control boot's silence). What the
plan deliberately leaves out stays out of scope: online services, purchases and entitlement, audio
preview (the API serves none) and archive extraction.

Dropped for now, their documents deleted with them: in-game host Audio/Video settings,
the per-device button-mapping layer (its remap half lives on in Controller Settings plan), and
the VR port. Anything needed from them has to be re-derived.

## 5. Permanent non-goals

Online services — Rock Central, leaderboards, achievements, challenges, multiplayer.
Blitz ships its own offline mode, so restoring them was never required, and unblocking
them is the community mod's job. Our obligation runs the other way: staying compatible
with the **Rock Band Blitz Ultimate** payload ([ultimate-compat.md](../guides/ultimate-compat.md)).

## 6. Ground rules

Keep four layers separate: **game input** (locally dumped content — read-only,
untracked, fingerprinted), the **pinned SDK** (no title-specific edits), **generated
code** (regenerate, never hand-edit), and **project code** (manifest, hints, hooks,
patches, diagnostics — every piece of Blitz knowledge lives here). Change the SDK
only when the behaviour is generically wrong for Xbox 360 software and could go
upstream, and then as a patch file ([patches/README.md](../../patches/README.md)).

Fix order: correct boundaries or hints → an existing SDK implementation →
a whole-function hook with the guest address recorded → a named data/code patch →
a mid-ASM hook → an SDK patch file. Every hook or patch states its guest address,
the observed failure, the intended behaviour, and the evidence.

Record as you go: chronology in [history/bringup-log.md](../history/bringup-log.md),
enduring facts in [known-issues.md](known-issues.md) and [symbols.md](symbols.md),
cross-project knowledge in [rb3-references.md](rb3-references.md). A disproof edits
the document that made the claim.
