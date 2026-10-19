# In-game settings

The in-game Help & Options program: a native settings store, a typed channel into the
guest's DTA scripts, a merged Help & Options screen that the title itself draws, the graphics policy,
and the cutover that made the game own the rows it can apply. The external Controller Settings
executable that preceded it retired on 2026-10-19 (gameplay-mapping plan §19); every
player setting is an in-game row.

## Reading order

| # | Document | Read it for |
| --- | --- | --- |
| 1 | [`contracts.md`](contracts.md) | The frozen interfaces (P0): `SettingId`/`Value`/`Descriptor`, requested/effective/pending provenance, the snapshot and capability shapes, both file shapes, migration rules, file ownership, and the retired application's dev gate (§8 records what survives). Nothing here should change without a decision. |
| 2 | [`plan.md`](plan.md) | The master plan: what was asked, the merged menu's row-by-row specification, storage and precedence, the package sequence (P0–P12), and the copy-ready prompts. |
| 3 | [`architecture.md`](architecture.md) | How it is actually built — the guest bridge and its transport, the panel/list script API, the store and service, the boot wiring and its ordering, the Ultimate provider adapter and the settings redirect, hot apply and the restart coordinator. |
| 4 | [`menu.md`](menu.md) | The surfaces a player sees: the merged Help & Options screen and its payload, menu compatibility (hide list, song pool, the mod-row rename), the retired settings surface and the Guide prohibition (part 3, superseded), and the installer's folder pages. |
| 5 | [`graphics.md`](graphics.md) | Resolution and graphics: why the title is resolution-blind, both routes to 1920×1080, the capability/policy rules and first-run recovery, and the three levers a non-16:9 frame needs. |
| 6 | [`status.md`](status.md) | **The live record.** Read this first in practice: what ships today, the owner punch list, every open item by package, the black-startup post-mortem and the earlier handover. |

## How the consolidated documents are arranged

`architecture.md`, `menu.md`, `graphics.md` and `status.md` are consolidations: several per-package
records merged into one document, with a `## Sources` table naming the originals. Each consolidated
part keeps **the original document's section numbers**, so a citation such as
"`architecture.md` part 3 §3" means §3 of the old `in-game-settings-storage.md`. When you cite one,
name the part — the numbers repeat across parts by design.

| Old document | Now |
| --- | --- |
| `in-game-settings-bridge-findings.md` | [`architecture.md`](architecture.md) part 1 |
| `in-game-settings-panel-api.md` | [`architecture.md`](architecture.md) part 2 |
| `in-game-settings-storage.md` | [`architecture.md`](architecture.md) part 3 |
| `in-game-settings-boot-wiring.md` | [`architecture.md`](architecture.md) part 4 |
| `in-game-settings-ultimate-adapter.md` | [`architecture.md`](architecture.md) part 5 |
| `in-game-settings-hot-apply-restart.md` | [`architecture.md`](architecture.md) part 6 |
| `in-game-settings-merged-menu.md` | [`menu.md`](menu.md) part 1 |
| `in-game-settings-menu-compat.md` | [`menu.md`](menu.md) part 2 |
| `in-game-settings-controller-settings.md` | [`menu.md`](menu.md) part 3 |
| `in-game-settings-installer.md` | [`menu.md`](menu.md) part 4 |
| `in-game-settings-resolution-findings.md` | [`graphics.md`](graphics.md) part 1 |
| `in-game-settings-graphics-findings.md` | [`graphics.md`](graphics.md) part 2 |
| `ultrawide-findings.md` | [`graphics.md`](graphics.md) part 3 |
| `in-game-settings-remaining-work.md` | [`status.md`](status.md) part 1 |
| `in-game-settings-status-2026-10-12.md` | [`status.md`](status.md) part 2 |
| `in-game-settings-handover-2026-10-12.md` | [`status.md`](status.md) part 3 |

`plan.md` and `contracts.md` moved unchanged (their section numbers are cited from source comments).
The originals of everything consolidated are in git history before the 2026-10-14 reorganization.

## Related reading

- [`../guides/dlc.md`](../guides/dlc.md) — the DLC library, its cache, and what a big library costs.
- [`../engine/toggles.md`](../engine/toggles.md) — the enhancement toggles the settings screen exposes.
- [`../plans/finished/2026-09-27-launcher-plan.md`](../plans/finished/2026-09-27-launcher-plan.md) — the retired settings application's own plan (its rows are in-game now).
- [`../plans/finished/2026-10-17-gameplay-input-mapping-plan.md`](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md) — the in-game Controls menu and the application's retirement (§16-§19).
