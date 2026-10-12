# In-game settings — current status and remaining work

> **Two handovers, both current (2026-10-12):** [the implementation handover](in-game-settings-handover-2026-10-12.md)
> has the changes, the Auto-resolution decision, and the runtime blockers;
> **[the evening status](in-game-settings-status-2026-10-12.md) explains the black startup and what
> this session changed** — the two failing ctests are fixed (36/36 green), the Ultimate boot hang is
> fixed at its cause (the generated payload was making a two-part archive, which this title cannot
> read), and the merged Help & Options screen now opens with the provider active. Read that one
> second and last.
>
> Statements below that this session measured to be wrong: graphics recovery **is** wired (the
> marker, the fallback and safe mode all ran); the Ultimate boot **does** present and **does**
> carry the merged root; and the interactive matrix has still not been driven.
> The feature has not passed final acceptance.

**Updated 2026-10-12.** This status supersedes the older open-item ledger below in its previous
revision and the dated “open” statements in the handoff documents. Those documents retain the
research history; use this file for the current implementation state.

## Implemented

The settings feature is wired through the real boot and guest UI:

* Native settings are read from `settings.toml` during `OnConfigurePaths`, before the GPU plugin,
  title data, and guest rendering begin. The small path file and legacy migration are handled in the
  same early stage.
* Renderer selection is chosen before the GPU plugin loads. After the plugin registers its cvars,
  VSync, filtering, anti-aliasing, and the resolution scale are applied in `OnPreSetup`, before
  presentation and render resources are initialized. This is not work deferred until the player
  presses A on the menu.
* Resolution uses the measured 960×540 or 1280×720 title guest modes multiplied by an integer
  backend draw scale. In particular, 1920×1080 is 960×540 at 2×. The experimental native 1080p
  guest-height route was removed because it corrupted title-screen artwork before the menu.
* The generated Help & Options tree works from the main menu and the in-song pause route. The
  in-song construction-order fault was fixed by deferring those screens until the pause panels are
  available; the in-song root, child pages, Back path, and resume path were exercised in a running
  game.
* The menu payload extends the mounted effective archive, so Ultimate-shadowed records are retained.
  Provider choices come from the installed payload, its settings file is redirected into user
  storage, `unlockall` is stored by the native service, and the achievements action invokes the
  provider action.
* Save errors, restart confirmation, and restart coordination are connected. The child command
  builder no longer duplicates the game-data-root argument. A previous launch exposed that defect;
  it has been corrected.

## Remaining acceptance work

1. **Run the final acceptance matrix when requested.** The user asked that tests not be run unless
   requested, so this update only builds the release target. The last recorded test run was before
   the latest UI rows were added and had stale `settings_screen` expectations; do not treat that
   result as a pass. The final matrix still needs both base and Ultimate launches, settings save and
   direct-boot readback, restart A/B, target change, and the latest 1080p path.
2. **Graphics recovery is not wired.** The policy functions for first-run recommendations and
   last-known-good recovery exist, but boot does not yet maintain a durable “attempted/presented”
   marker. A failed graphics boot therefore cannot automatically restore the safe 720p/no-AA
   combination.
3. **Release checks remain.** Recheck packaged archive generation against base-only and Ultimate
   installs, installer path handling, Controller Settings launch/update behavior, and the no-copied-
   content constraint on the final distributable. These are integration checks, not blockers to the
   early settings load path.
4. **Refresh-row edge case.** The DLC refresh action is available from the settings tree. Verify that
   hiding `splash_dlc` still leaves the settings-side refresh reachable and functional on a populated
   library.

## Boot ownership

| When | Work |
| --- | --- |
| `OnConfigurePaths` | Resolve identity and path roots; migrate/load the store; apply target, renderer selection, and native settings. |
| `OnPreSetup` | Load the selected GPU plugin, apply its saved flags, plan the title guest mode and draw scale, and set the guest-height hook before graphics setup. |
| `OnPostLoadXexImage` | Inspect the mounted provider, install the generated menu archive, configure the bridge, and attach restart handling. |
| Guest menu interaction | Read/edit settings through the typed channel; commit durably before acknowledging; invoke live VSync or offer restart for restart-scoped changes. |

The retail “Loading Rock Band Blitz audio/video settings…” string belongs to the game's own menu
flow. This feature does not introduce an A-button-time settings-load screen: the native store is
already loaded before game processing begins.

For historical experiments and acceptance scripts, see [the main plan](in-game-settings-plan.md),
[merged-menu notes](in-game-settings-merged-menu.md), [graphics findings](in-game-settings-graphics-findings.md),
and [boot-wiring notes](in-game-settings-boot-wiring.md).
