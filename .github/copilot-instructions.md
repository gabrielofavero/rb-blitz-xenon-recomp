# rb-blitz-xenon-recomp — project instructions

Static recompilation of Rock Band Blitz (Xbox 360) with ReXGlue: the guest runs as generated C++
(`generated/default/`), the host is our own code in `src/`, and the installer is a separate
target in the same build tree. Documentation is part of the change — source comments cite it by
section, so paths and section numbers are an interface.

## Documentation

- The index is `docs/README.md`; the workflow (placement, naming, expected content, moving, merging,
  verification) is the **documentation** skill: `.github/skills/documentation/SKILL.md` — also
  `/documentation`.
- Docs are a lab notebook: a claim is a measurement with a date and its evidence path
  (`out/_swap/...`), a `file:line`, or an explicit "unproven". Never delete a measurement; mark
  superseded statements and name the successor.
- A change that alters behaviour, a toggle, a limitation, or a plan's state owes a documentation
  update — the skill's §4 table says which document owns what (`engine/toggles.md`, `reference/known-issues.md`,
  `reference/backlog.md`, the plan's status folder, `settings/status.md`, the index).
- Plans: `docs/plans/<todo|in-progress|finished|cancelled>/YYYY-MM-DD-<name>.md`.
- No machine-specific values in tracked files: no `C:\Users\<name>\...`, no absolute checkout paths.
  Use `<checkout>`, `<home>`, or a neutral example such as `D:\Songs`.

## Agents and skills — nothing to point at

Your own context already lists the available custom agents/subagents and skills, with their
descriptions. VS Code builds that list from `.github/agents/*.agent.md`, `.github/skills/*/SKILL.md`,
the user profile, and the extension's built-ins. Read it before starting, and delegate when a
specialist matches instead of doing that work inline.

- Documentation work → the `documentation` skill. Auditing docs without changing them → the
  `docs-auditor` agent. Context gathering before writing → a read-only exploration subagent.
- Roster and routing rules: `.github/agents/README.md`. Add a row there when you add an agent.

## Build, test, and the docs check

```powershell
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
ctest --test-dir out\build\win-amd64-release
python .github/skills/documentation/scripts/check_docs.py
```

`ctest` runs 37 tests; `game_restart` (#33) can flake in the parallel run and passes when re-run alone.

## UI: guest-rendered, never host-drawn (owner directive, 2026-10-22; overlay authorization 2026-10-24)

- **Drawing directly on the guest's own UI is always authorized.** Authored DTA (the settings
  surface, screens, lists), the title's own widgets and labels, or a texture/record we serve — that
  is the default route, it reads as part of the game, and it needs no permission.
- **A new overlay needs the owner's explicit authorization first.** "Overlay" means any host-drawn
  element over the guest's screen: a panel, box, button, or a label/text drawn where the guest did
  not draw one. Do not ship one because the guest route is inconvenient — ask, say what the guest
  route would cost, and wait for the answer.
- **Exception — a feature that already uses an overlay keeps it.** When the work extends a surface
  that already draws overlays, the matching overlay for the new item is expected and pre-authorized
  by that feature: e.g. a new Help & Options row gets its label/value and help caption drawn the
  same way the rows beside it are. This exception ends when the feature's own guest replacement
  lands.
- A missing engine capability is not a reason to draw over the game: extend the guest channel
  (DTA records, list data, bridged text) instead. Say "unproven" and plan a spike rather than
  shipping an overlay.
- `src/ui/help_overlay.cpp`'s `DrawBindingRows` is the case that motivated this rule; its
  replacement is `docs/plans/todo/2026-10-22-in-game-binding-rows-plan.md`.

## Rules that bite

- **Line endings.** This checkout is CRLF (`core.autocrlf=true`); only `*.patch` is pinned to LF. A
  scripted text edit must not translate newlines — read and write with `newline=""` (or bytes) — or
  whole files flip to LF and pollute the diff. The scripts in the documentation skill already do this.
- **The SDK is patch-driven.** Changes to `rexglue-sdk/` land as a patch under `patches/`, applied by
  `scripts/apply_sdk_patches.ps1` (see `patches/README.md`). Never hand-edit the submodule and never
  `git checkout` inside it — a patch owns those files.
- **Gates.** The toolchain record (`config/toolchain.toml`) and the game-data fingerprint
  (`game/default.xex`) are deliberate gates; see `docs/guides/toolchain.md` and
  `docs/guides/build-and-run.md` before relaxing one.
- **Never commit** build output (`out/`, `installer/out/`), dumped game data
  (`game/`, `assets/game/`), or anything machine-specific.
- Do not commit, push or deploy without the owner's explicit approval.
