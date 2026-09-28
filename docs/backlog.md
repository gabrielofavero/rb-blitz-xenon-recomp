# Backlog

What is still pending, most important first. Every entry is a task, and the order is
the order to pick them up in. The bring-up work — scaffold, codegen, boot, menus,
input, persistence, songs, Ultimate, DLC — is closed; its chronology is in
[history/bringup-log.md](history/bringup-log.md) and the limits it left standing are
in [known-issues.md](known-issues.md).

## 1. Release readiness

The one goal that is not a feature: another authorized developer follows
[README.md](../README.md) from a clean checkout and reproduces the acceptance run.

- [x] **Debug and Release smoke tests on a clean checkout** — both presets, one clean
      tree, the documented route from `scripts/acceptance_song.ps1`. Done 2026-09-28 on a
      fresh clone in a scratch directory: Release `launch-to-results 3 / 3` (three 315 s
      envelopes, no `[FATAL]`, clean closes), Debug and Release both configure, build and
      pass `ctest` 6/6 in one tree without clobbering each other, and the route is
      scriptable with no local `rb_blitz.toml`. Four blockers found and fixed along the
      way — the symlink repair exiting 1, a first build that linked no recompiled code, a
      harness with no input path, and a song envelope read from a log level the clone did
      not have ([history/bringup-log.md](history/bringup-log.md), "Clean-checkout smoke
      test"). The Debug route itself aborts on an SDK assert; that is B-013, below.
- [x] **Freeze the supported toolchain** — SDK pin, compiler paths and versions — in
      a `toolchain.md`. Done 2026-09-28: [toolchain.md](toolchain.md) is the prose,
      [config/toolchain.toml](../config/toolchain.toml) is the record (SDK commit, clang,
      CMake, Ninja, MSVC toolset, Windows SDK — versions *and* the paths they were
      measured at), [tools/toolchain_check.cpp](../tools/toolchain_check.cpp) is the
      check, and it runs as a build gate before codegen the way the game fingerprint
      does. A binary built on a machine that is not the frozen set says so in its boot
      log, which is where that fact used to be missing entirely. The game fingerprint is
      frozen and enforced the same way
      ([config/game_fingerprints.toml](../config/game_fingerprints.toml)).
- [ ] **Record the runtime half of the wrong-data check.** The build half fails closed.
      The runtime half is a deliberate warn-and-continue (a mismatched
      `--game_data_root` may be an Ultimate payload) and has no capture yet — the
      comparison logic is covered by `tests/fingerprint_tests.cpp` instead.
- [ ] **Audit the distributable** — confirm no retail data, symbols derived from
      proprietary databases, credentials or machine-specific paths reach the
      installer payload or a packaged build ([installer/](../installer)).

The one-command configure/build flow, the runtime data-path argument and the log
locations are already documented in [README.md](../README.md); nothing is owed there.

## 2. Fixes implied by open known-issues

- [ ] **Decide the Debug preset's assert policy.** The Debug preset builds, links and
      passes its host tests, but the guest flow stops on an SDK assert —
      `XamAlloc_entry`'s `assert_true(unk == 0)`, the first A at the title screen — while
      Release completes the same route with every assert compiled out. Relax the
      guest-parameter asserts in Debug (an SDK patch, the way the diagnostics patches
      already do), or declare Debug build/test-only and keep acceptance on Release.
      Evidence and the reasoning for leaving it open: [known-issues.md](known-issues.md)
      B-013, [history/bringup-log.md](history/bringup-log.md) "Clean-checkout smoke test".
- [ ] `0x827EC038` — the last forced `functions.toml` entry of the indirect-call
      class: a 24-byte leaf whose address is only taken in data, so no segment
      mentions it (B-006).
- [ ] 32-bit integer textures (`num_format = 1`) still fail to create, and one
      converted texture plus its mips must fit a single 2 MiB upload page (B-010).
- [ ] `longjmp_address` / `setjmp_address` are unset, so guest `longjmp`/`setjmp` has
      no host bridge. Find Blitz's equivalents — `band3_recomp` sets
      `0x82BBB620` / `0x82BBBA50` — or record why they are not needed
      ([rb3-references.md](rb3-references.md) §8).
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

## 3. Engine knowledge still to port

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

## 4. Ultimate compatibility

- [ ] **Script an Ultimate run** — boot → menu → a song against an installed payload.
      The install itself landed and was verified by hand on 2026-09-20
      ([ultimate-compat.md](ultimate-compat.md) §7); what is missing is the scripted
      run that closes acceptance criterion 1 with evidence instead of an observation
      ([ultimate-compat.md](ultimate-compat.md) §10).
- [ ] Re-derive the 12-byte patch table if a payload release ships a different
      `default.xex` (compare its hash against `390e0ae0…` first).

## 5. The launcher — the one active design

- [ ] [plans/launcher-plan.md](plans/launcher-plan.md) — a launcher that owns the
      installed game's settings (General/Graphics/Controller/Experimental), ships in the
      installer payload beside `rb_blitz.exe` and launches it. It is written
      prompt-by-prompt with waves, a dependency graph and an open-questions list that has
      to be answered before wave 0. It absorbed the per-device remap design: lane C
      builds the remap core and the launcher's Controller → Manual page is its panel.
      Nothing in it gates release readiness.

## 6. Deferred, with no design yet

Pixel-perfect graphics and UI upgrades, unlocked frame rate, latency tuning,
custom-song/export compatibility, other controller backends, Linux/macOS/ARM hosts,
packaging and auto-update, symbol-name campaigns, and mod APIs. DLC packages do load
([dlc.md](dlc.md)) — what is still undefined is everything around them, from a store UI
to custom-song content that no content API ever enumerated.

Dropped for now, their documents deleted with them: in-game host Audio/Video settings,
the per-device button-mapping layer (its remap half lives on in the launcher plan), and
the VR port. Anything needed from them has to be re-derived.

## 7. Permanent non-goals

Online services — Rock Central, leaderboards, achievements, challenges, multiplayer.
Blitz ships its own offline mode, so restoring them was never required, and unblocking
them is the community mod's job. Our obligation runs the other way: staying compatible
with the **Rock Band Blitz Ultimate** payload ([ultimate-compat.md](ultimate-compat.md)).

## 8. Ground rules

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
