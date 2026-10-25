# To-do plans

Plans that are written but not started. Each carries its `YYYY-MM-DD-` creation date in the filename.

| Plan | What it covers |
| --- | --- |
| [`2026-10-13-load-guard.md`](2026-10-13-load-guard.md) | Song Load Guard: which stall/crash cases a cooperative timeout can safely handle and which it cannot (part 1), plus the five implementation prompts that follow it (part 2). |
| [`2026-10-22-background-music-navigator-plan.md`](2026-10-22-background-music-navigator-plan.md) | The main menu's background music navigator: a default-on setting, LB/RB/Start plus media keys for next/previous/pause, a host-drawn footer, a Background Music Play page under Controls, and Play Current Song following the navigated song (planning; three prompts). |
| [`2026-10-22-in-game-binding-rows-plan.md`](2026-10-22-in-game-binding-rows-plan.md) | Replace the host-drawn binding rows with guest-drawn rows fed from host text: a `read_file` text op on the existing bridge, index dispatch on the list, page-entry refresh, and the retirement of `DrawBindingRows` (planning; four prompts, spike first). |
| [`2026-10-23-messages-and-update-feed-plan.md`](2026-10-23-messages-and-update-feed-plan.md) | A built-in message feed from a Cloudflare Worker (`workers/messages/`) drawn in the retail Messages screen, with a boot fetch, a JSON cache and a retail-fallback empty state, icons named per message (the game's own set or `assets/messages/`), plus the version check that turns the screen's `(START)` prompt into *Update Game* and launches the dormant updater. Planning only (M0–M8, three guest spikes). |
| [`2026-10-24-settings-hot-apply-plan.md`](2026-10-24-settings-hot-apply-plan.md) | Expand hot settings: display modes and actual render resolution, Ultimate gameplay/HUD, and loaded menu/header refresh, with acknowledged application and explicit immediate/menu-return/next-song boundaries. Planning only (P0–P7). |
| [`2026-10-25-framerate-expansion-plan.md`](2026-10-25-framerate-expansion-plan.md) | Framerate feasibility and gated implementation: current 60 Hz default and historical above-60 evidence, pinned RB3 timing/camera/particle findings, independent cap policy, and gameplay/physics acceptance before offering higher rates. Planning only (P0–P5); no safe upper limit established. |

The Load Guard document is a merge (2026-10-22) of two records, kept as `## Part N` sections so old
citations still resolve:

| Old document | Now |
| --- | --- |
| `2026-10-13-load-guard-findings.md` | [`2026-10-13-load-guard.md`](2026-10-13-load-guard.md) part 1 |
| `2026-10-13-load-guard-prompts.md` | [`2026-10-13-load-guard.md`](2026-10-13-load-guard.md) part 2 |

When work starts on one of these, `git mv` it to [`../in-progress/`](../in-progress/).

See [`../README.md`](../README.md) for the status taxonomy.
