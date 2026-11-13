# To-do plans

Plans awaiting work, including plans paused after an initial investigation. Each carries its `YYYY-MM-DD-` creation date in the filename.

| Plan | What it covers |
| --- | --- |
| [`2026-10-25-framerate-expansion-plan.md`](2026-10-25-framerate-expansion-plan.md) | Paused after P0/P1 diagnostics and gameplay pilots. No higher rate passed the in-song performance gate; the diagnostic code and results remain recorded for a later attempt. |
| [`2026-10-26-offline-powerups-restoration-plan.md`](2026-10-26-offline-powerups-restoration-plan.md) | Independently restore free offline powerups on the full default target: one default-on Unlock Powerups setting, native selection/unlocks, disabled costs and purchases, replay routes, and unchanged Ultimate/trial behavior. Planning only (P0–P4). |
| [`2026-10-26-in-game-song-management-plan.md`](2026-10-26-in-game-song-management-plan.md) | My Songs X Manage and shared RhythmVerse management: native song/package details, per-song hiding, exact-package deletion, search visibility and Unhide All settings, observed DLC dates and default-On Auto-hide Failed Songs with unchanged load-guard recovery when Off. Planning only (P0–P6). |
| [`2026-10-26-native-song-folders-plan.md`](2026-10-26-native-song-folders-plan.md) | YARG research spike complete (2026-11-13); implementation planned (P0–P7). Expand read-only DLC to PC folders, extracted/decrypted PS3 songs and SNG/YARGSONG, with guest asset preparation, dependency caches, performance gates and a default-Off setting with native enable confirmation. Raw PS3 PKG is a separate deferred spike. |
| [`2026-10-29-ui-pixel-imperfections-investigation-plan.md`](2026-10-29-ui-pixel-imperfections-investigation-plan.md) | Reproduce the reported text and star-edge pixels, test whether they follow an asset or screen coordinate, locate the earliest affected render stage, and fix only a demonstrated error. Planning only (P0–P3). |
| [`2026-10-31-engine-compatibility-closeout-plan.md`](2026-10-31-engine-compatibility-closeout-plan.md) | Ultimate scripted acceptance, shader preparation, compile-trace counts, selective RB3 porting and first-release evidence. Planning only (E1–E6, with E3/E5 split into prompts). |
| [`2026-10-31-content-and-music-closeout-plan.md`](2026-10-31-content-and-music-closeout-plan.md) | Current-build DLC list fault, same-session store refresh, Search header, and menu-music listening routes. Planning only (C1a–C5). |
| [`2026-10-31-runtime-ui-acceptance-plan.md`](2026-10-31-runtime-ui-acceptance-plan.md) | HUD, Ultimate setting restart, graphics recovery note, Load Guard and remaining live input acceptance. Planning only (U1–U6b). |

The Load Guard document moved to [`../in-progress/`](../in-progress/) when its controller work started
on 2026-10-25; its merge record is in [`../in-progress/README.md`](../in-progress/README.md). The
background-music navigator plan moved there on 2026-10-26, when its session/mount-cache foundation
was implemented; the navigator controls themselves are not started.

When work starts on one of these, `git mv` it to [`../in-progress/`](../in-progress/).

See [`../README.md`](../README.md) for the status taxonomy.
