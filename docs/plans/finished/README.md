# Finished plans

Delivered plans, kept because they are still read: each one carries the measurements and the
decisions behind code that is running today, and the per-prompt tables are how a reader finds out
*why* something is the way it is.

| Plan | Delivered |
| --- | --- |
| [`2026-09-27-launcher-plan.md`](2026-09-27-launcher-plan.md) | The launcher: its 27 prompts are built (the launcher is part of the release preset). The residual manual passes — real pads by hand, the three finish-page choices, one driven Ultimate install — are listed in the plan itself and in [`../../settings/status.md`](../../settings/status.md). |
| [`2026-10-03-customization-plan.md`](2026-10-03-customization-plan.md) | The guest-side enhancement research: it produced [`../../engine/`](../../engine/) and the toggle registry in [`../../engine/toggles.md`](../../engine/toggles.md); the R-items it names are implemented. |
| [`2026-10-09-save-persistence-findings.md`](2026-10-09-save-persistence-findings.md) | Offline save persistence: root-caused and verified in-game, including the checkpoint re-application after the guest's own `SaveData` teardown. |
| [`2026-10-10-song-cache-plan.md`](2026-10-10-song-cache-plan.md) | The three song-cache issues (discovery screen, progress bar, refresh row): every prompt done and verified in-game; the durable mechanics live in [`../../guides/dlc.md`](../../guides/dlc.md). |

A finished plan is not edited afterwards. When later work changes a conclusion, it is recorded in the
new document and the finished plan is left as the record of what was known at the time.

See [`../README.md`](../README.md) for the status taxonomy.
