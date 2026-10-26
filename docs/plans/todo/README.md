# To-do plans

Plans that are written but not started. Each carries its `YYYY-MM-DD-` creation date in the filename.

| Plan | What it covers |
| --- | --- |
| [`2026-10-23-messages-and-update-feed-plan.md`](2026-10-23-messages-and-update-feed-plan.md) | A built-in message feed from a Cloudflare Worker (`workers/messages/`) drawn in the retail Messages screen, with a boot fetch, a JSON cache and a retail-fallback empty state, icons named per message (the game's own set or `assets/messages/`), plus the version check that turns the screen's `(START)` prompt into *Update Game* and launches the dormant updater. Planning only (M0–M8, three guest spikes). |
| [`2026-10-24-settings-hot-apply-plan.md`](2026-10-24-settings-hot-apply-plan.md) | Expand hot settings: display modes and actual render resolution, Ultimate gameplay/HUD, and loaded menu/header refresh, with acknowledged application and explicit immediate/menu-return/next-song boundaries. Planning only (P0–P7). |
| [`2026-10-25-framerate-expansion-plan.md`](2026-10-25-framerate-expansion-plan.md) | Framerate feasibility and gated implementation: current 60 Hz default and historical above-60 evidence, pinned RB3 timing/camera/particle findings, independent cap policy, and gameplay/physics acceptance before offering higher rates. Planning only (P0–P5); no safe upper limit established. |
| [`2026-10-26-offline-powerups-restoration-plan.md`](2026-10-26-offline-powerups-restoration-plan.md) | Independently restore free offline powerups on the full default target: one default-on Unlock Powerups setting, native selection/unlocks, disabled costs and purchases, replay routes, and unchanged Ultimate/trial behavior. Planning only (P0–P4). |

The Load Guard document moved to [`../in-progress/`](../in-progress/) when its controller work started
on 2026-10-25; its merge record is in [`../in-progress/README.md`](../in-progress/README.md). The
background-music navigator plan moved there on 2026-10-26, when its session/mount-cache foundation
was implemented; the navigator controls themselves are not started.

When work starts on one of these, `git mv` it to [`../in-progress/`](../in-progress/).

See [`../README.md`](../README.md) for the status taxonomy.
