# Documentation

Where everything lives, and where to start. Reorganized 2026-10-14: the flat pile of top-level docs
became topic folders, and the sixteen `in-game-settings-*.md` records were consolidated into six
documents under [`settings/`](settings).

## Layout

| Folder | What lives there |
| --- | --- |
| [`guides/`](guides) | Task guides: build, run, ship, extract assets, content and DLC, the toolchain record, the Ultimate mod's compatibility policy. |
| [`reference/`](reference) | Standing records: open issues and accepted limitations, the backlog, the guest image's symbols, and the RB3/RB3DX mining notes. |
| [`engine/`](engine) | How the recompiled guest actually behaves — per-subsystem studies plus the standing diagnostics harness. Start at [`engine/README.md`](engine/README.md). |
| [`settings/`](settings) | The in-game settings / Help & Options program: plan, frozen contracts, architecture, surfaces, graphics, and the live status. Start at [`settings/status.md`](settings/status.md). |
| [`plans/`](plans) | Dated work plans, filed by status (`todo/`, `in-progress/`, `finished/`, `cancelled/`). See [`plans/README.md`](plans/README.md). |
| [`history/`](history) | The append-only bring-up log ([`history/bringup-log.md`](history/bringup-log.md), indexed by [`history/README.md`](history/README.md)). |
| [`rythmverse-responses/`](rythmverse-responses) | Raw RhythmVerse API captures (JSON fixtures) consumed by the store plan. |

## Start here

| If you want to… | Read |
| --- | --- |
| build, run, or capture a log | [`guides/build-and-run.md`](guides/build-and-run.md) |
| know what is broken on purpose vs by accident | [`reference/known-issues.md`](reference/known-issues.md) |
| know what is being worked on right now | [`settings/status.md`](settings/status.md) and [`plans/todo/`](plans/todo) |
| understand a menu, a toggle, or the HUD | [`engine/`](engine) |
| work on the settings screen, the store, or a resolution | [`settings/`](settings) |
| ship a build | [`guides/distributable.md`](guides/distributable.md) |
| know why the toolchain is pinned | [`guides/toolchain.md`](guides/toolchain.md) |

## Conventions

- **Plans are dated.** A plan is `YYYY-MM-DD-<name>.md`, using the date the document itself states,
  otherwise the date it was first added. It lives in `plans/todo/` until work starts, then
  `plans/in-progress/`, then `plans/finished/` — or `plans/cancelled/` if it was abandoned.
  Moving a plan is one `git mv`; the date prefix stays.
- **Merged documents list their sources.** When several records are consolidated into one, the new
  document carries a `## Sources` table naming the originals, and each consolidated part keeps the
  original document's section numbers so existing citations still resolve.
- **Docs are a lab notebook.** Measurements carry dates and the evidence path they came from
  (`out/_swap/...`), and anything unproven says so.
- **No machine paths in tracked files.** Use `<checkout>`, `<home>`, or a neutral example such as
  `D:\Songs`. Build output (`out/`, `installer/out/`) and dumped game data (`game/`) are local.

## Changing these docs

The workflow is a skill: [`.github/skills/documentation/SKILL.md`](../.github/skills/documentation/SKILL.md)
(also `/documentation`). It covers where a document belongs, what each kind must contain, and how to
move, merge, split or retire one — including the script that re-points the references in code
comments. Verify a change with:

```powershell
python .github/skills/documentation/scripts/check_docs.py
```

It checks broken links, stale `docs/...` mentions, plan naming and placement, machine paths, line
endings and orphaned documents. Auditing without editing is the read-only `docs-auditor` agent
(roster: [`../.github/agents/README.md`](../.github/agents/README.md)).

## Index

### Guides

| Document | What it is |
| --- | --- |
| [`guides/build-and-run.md`](guides/build-and-run.md) | SDK submodule prep, toolchain prerequisites, configure/build/rebuild, the game-data and toolchain gates, running the title, the two graphics backends, capturing a log. |
| [`guides/toolchain.md`](guides/toolchain.md) | The frozen toolchain record: why it is frozen, what the check does, what it covers and what it does not. |
| [`guides/distributable.md`](guides/distributable.md) | What the distributable payload is, what must never ship, and the audit that proves it. |
| [`guides/assets.md`](guides/assets.md) | What the title ships, how to extract it, where assets land, and the MILO/DTB/texture format notes. |
| [`guides/dlc.md`](guides/dlc.md) | How the guest discovers DLC, where packages go, the library enumeration cache, and what a large library costs a boot. |
| [`guides/ultimate-compat.md`](guides/ultimate-compat.md) | Vanilla vs Rock Band Blitz Ultimate: the content-variant policy, install route, and the 12-byte executable delta. |

### Reference

| Document | What it is |
| --- | --- |
| [`reference/known-issues.md`](reference/known-issues.md) | Open issues and limitations accepted by design. |
| [`reference/backlog.md`](reference/backlog.md) | What is not started yet, and the ground rules for the project. |
| [`reference/symbols.md`](reference/symbols.md) | The guest image inventory: identity, sections, imports, named symbols, jump tables, exception handling. |
| [`reference/rb3-references.md`](reference/rb3-references.md) | Mining the RB3/RB3DX projects: how hooking works in this SDK, the `[[midasm_hook]]` route, the port catalogue, and the borrowing rules. |

### Engine

See [`engine/README.md`](engine/README.md) for the per-document summary.

| Document | What it is |
| --- | --- |
| [`engine/main-menu.md`](engine/main-menu.md) | The main menu: its option list, file, loader, label, offline prompts, refresh row, and background music. |
| [`engine/toggles.md`](engine/toggles.md) | Every enhancement toggle: what it does, its faithful (off) behaviour, and where it is implemented. |
| [`engine/input-flow.md`](engine/input-flow.md) | Native menu mouse and wheel navigation (R6): the retail path, the remap, and the checks. |
| [`engine/loading-cancel.md`](engine/loading-cancel.md) | Why a song load that never finishes cannot be cancelled yet, and what was measured trying. |
| [`engine/diagnostics.md`](engine/diagnostics.md) | The two standing instruments: the runtime probe overlay and the UI-state observer. |

### Settings

| Document | What it is |
| --- | --- |
| [`settings/status.md`](settings/status.md) | **The live record**: what ships, the owner punch list, every open item by package, the black-startup post-mortem, the earlier handover. |
| [`settings/plan.md`](settings/plan.md) | The master plan: requirements, the merged menu's row-by-row specification, storage and precedence, the package sequence. |
| [`settings/contracts.md`](settings/contracts.md) | The frozen contracts (P0): identifier/value/descriptor shapes, state model, file shapes, migration, file ownership, Controller Settings dev gate. |
| [`settings/architecture.md`](settings/architecture.md) | How it is built: the guest bridge, the panel/list API, storage and service, boot wiring, the Ultimate provider adapter, hot apply and restart. |
| [`settings/menu.md`](settings/menu.md) | The surfaces: the merged Help & Options screen, menu compatibility (hide list, pool, rename), Controller Settings, and the installer's folder pages. |
| [`settings/graphics.md`](settings/graphics.md) | Resolution and graphics: what the title can actually render, the capability/policy rules, and what a non-16:9 frame costs. |

The layout rules for a settings row or page — which control a setting gets, the six-visible-rows page
budget, and the row-text budget — are a skill:
[`.github/skills/in-game-settings/SKILL.md`](../.github/skills/in-game-settings/SKILL.md) (also
`/in-game-settings`).

### Plans

| Document | Status |
| --- | --- |
| [`plans/finished/2026-09-27-launcher-plan.md`](plans/finished/2026-09-27-launcher-plan.md) | Controller Settings' own plan; 27 prompts built, with the residual hand-check list. |
| [`plans/finished/2026-10-03-customization-plan.md`](plans/finished/2026-10-03-customization-plan.md) | The guest-side enhancement research plan that produced `engine/` and the toggle registry. |
| [`plans/finished/2026-10-09-save-persistence-findings.md`](plans/finished/2026-10-09-save-persistence-findings.md) | Why offline saves did not persist, the fix, and the evidence. |
| [`plans/finished/2026-10-10-song-cache-plan.md`](plans/finished/2026-10-10-song-cache-plan.md) | The three song-cache issues, all prompts done, with the measurements. |
| [`plans/finished/2026-10-14-controller-settings-rename.md`](plans/finished/2026-10-14-controller-settings-rename.md) | The launcher → Controller Settings rename: the decisions, the compatibility names that were kept, and the three collisions the pass created. |
| [`plans/finished/2026-10-14-installed-game-fix-prompts.md`](plans/finished/2026-10-14-installed-game-fix-prompts.md) | The installed-game review's prompts, each closed with its own record. **P1–P8 are done** (P1–P4 2026-10-14, P6 with its final installed pass, P5's correction and P7/P8 2026-10-15); the coverage §3 still does not claim is named in the plan's own records. |
| [`plans/finished/2026-10-14-play-current-song.md`](plans/finished/2026-10-14-play-current-song.md) | The main menu's *Play Current Song* entry (and the revived *DLC Menu Songs* row): the press **works end to end since 2026-10-15** (the lookup handed `SongMgr` a String where it reads a Symbol; measured press → loading screen → gameplay), with the title's own-list fault tracked separately. |
| [`plans/finished/2026-10-15-release-model.md`](plans/finished/2026-10-15-release-model.md) | The release model: one file per release — the setup executable, built from the checkout by `installer\build.ps1`, versioned by `[installer] version`, and what replaced D19's update check. |
| [`plans/finished/2026-10-15-native-discovery-bar.md`](plans/finished/2026-10-15-native-discovery-bar.md) | The discovery bar returns to the title's own drawing (owner request): the P6 overlay is retired, the host pushes the aggregate counts into the engine's own bar, and the in-game evidence. |
| [`plans/finished/2026-10-17-gameplay-input-mapping-plan.md`](plans/finished/2026-10-17-gameplay-input-mapping-plan.md) | General vs Gameplay Mapping, the in-game **Controls** menu, and the retirement of the external Controller Settings executable. **Finished 2026-10-19:** P0-P4 and P7-P8 landed (§10-§16, §18-§19), P5/P6 were cancelled by the 2026-10-18 change of course, and `rb_blitz.exe` is now the only program the build and the installer produce. |
| [`plans/in-progress/2026-10-19-interface-gameplay-mapping-restructure-plan.md`](plans/in-progress/2026-10-19-interface-gameplay-mapping-restructure-plan.md) | Interface Mapping versus always-on Gameplay Mapping: preset-aware controller defaults, inferred Custom, 3→1 listening countdown, row-level binding text, and new Mouse & Keyboard defaults. **P1 landed 2026-10-19** (binding rows draw their inputs); P2–P5 not started. |
| [`plans/todo/2026-10-10-shader-cache-findings.md`](plans/todo/2026-10-10-shader-cache-findings.md) | Shader-stutter findings and implementation plan: preload completion, pipeline coverage, preparation before playback, and distribution. |
| [`plans/todo/2026-10-13-load-guard-findings.md`](plans/todo/2026-10-13-load-guard-findings.md) | Song Load Guard feasibility, with [`load-guard-prompts.md`](plans/todo/2026-10-13-load-guard-prompts.md) for implementation. |
| [`plans/todo/2026-10-13-rhythmverse-store-plan.md`](plans/todo/2026-10-13-rhythmverse-store-plan.md) | Wiring the RhythmVerse customs catalogue into the retail store: research complete, no code yet. |
| [`plans/todo/2026-10-19-resolution-scale-expansion-plan.md`](plans/todo/2026-10-19-resolution-scale-expansion-plan.md) | Enable the six requested ultrawide/32:9 sizes and 4:3 as a first-class shape, with display-driven offers and a diagnostic downscale override for validation on a smaller panel. |
| [`plans/todo/2026-10-19-button-layout-hotswap-plan.md`](plans/todo/2026-10-19-button-layout-hotswap-plan.md) | Button glyphs and controller art as replaceable scheme packages: a live texture override, in-game selection, keyboard/controller auto-switch, and an optional PS3 `.pkg` import. Planning only. |

### History

| Document | What it is |
| --- | --- |
| [`history/bringup-log.md`](history/bringup-log.md) | The append-only bring-up log, from the first codegen run to the boot phases, blockers and toolchain notes. |
| [`history/README.md`](history/README.md) | Its index (phase list, and which section answers which question). |

## Known stale references

Some comments and older plans cite documents that were never added to this repo (they come from an
earlier tree): `docs/engine/content-flow.md`, `docs/engine/asset-model.md`,
`docs/engine/resolution-flow.md`, `docs/engine/ui-layout.md`, `docs/engine/ingame-layout.md`,
`docs/native/*`, `docs/plans/vr-port-plan.md`, `docs/plans/av-settings-plan.md`,
`docs/plans/button-mapping-plan.md`, `docs/plans/load-guard-runtime-findings.md`. Treat those names
as a plan that was proposed but never written, not as a broken link to chase.
