# Engine notes

How the recompiled guest actually behaves, one subsystem per document. These are the studies the
enhancement toggles ([`toggles.md`](toggles.md)) are built on: each one names the panel, file, loader
or hook point it measured, with the evidence it came from.

| Document | What it answers |
| --- | --- |
| [`main-menu.md`](main-menu.md) | The main menu's option list — the panel, the file, the loader, the two shipped arrays, the edit that hides rows (R5), the label that renames the mod's row (R10), the offline prompts (R3), the refresh row (R7) — and, in part 2, the menu's background music pool (R9). |
| [`toggles.md`](toggles.md) | The enhancement toggle registry: how to set one, the boot line, and a row per toggle with its faithful (off) behaviour and the module that implements it. |
| [`input-flow.md`](input-flow.md) | Native menu mouse, hover and wheel navigation (R6): the retail path it hooks, the click/remap rules, and the live checks. |
| [`loading-cancel.md`](loading-cancel.md) | Cancelling a song load that never finishes (R11): what the title does, what was measured, why the "try-catch the load" route is unavailable, and what the removed module did. |
| [`shader-cache.md`](shader-cache.md) | The shader-cache program in nine parts: **1** the post-A loading gate, its exits and the 2026-10-24 empty/warm/Vulkan measurements; **2** local shader resolution from the `XOBX` assets, vertex linking and the executable; **3** the pipeline recipe catalogue, version pins, validate/dedupe/merge tool and helper inventory; **4** the cancellable preparation lifecycle and its launch switch; **5** native D3D12/Vulkan pipeline caches; **6** the post-A guest loading gate and its recovery UI; **7** invalidation and repair; **8** measured campaign and runtime misses; **9** packaging, troubleshooting and remaining release gates. |
| [`vulkan-performance.md`](vulkan-performance.md) | Warm-cache Vulkan overhead: GPU texture conversion replacing CPU readback/conversion/upload, headless GPU validation and the pending gameplay comparison. |
| [`d3d12-loading-freeze.md`](d3d12-loading-freeze.md) | The DX12 post-A freeze report, invalid fence HRESULT/event sequencing, incomplete resolve readback, resource retirement fixes and headless validation. |
| [`diagnostics.md`](diagnostics.md) | The two standing instruments: the runtime probe (what the guest is doing, now) and the UI-state observer (what a captured state is made of). Both are also the harness the acceptance scripts drive. |
| [`messages.md`](messages.md) | The retail Messages screen: its record and panels, the `InboxProvider` and `InboxMessage` objects, the empty-state tokens, the bottom-bar inline help, and the row-icon set (twelve 512×512 DXT1 icons with converted PNGs). |

Reading order for a new feature: find its toggle in [`toggles.md`](toggles.md), then follow the
document it names — that document is where the addresses, the file offsets and the measurements are.

[`shader-cache.md`](shader-cache.md) merges the six per-delivery records that used to sit beside it:
part 1 was `shader-cache-p0.md`, part 2 `shader-cache-p2.md`, part 3 `shader-cache-p3.md`, part 4
`shader-cache-p4.md`, part 5 `shader-cache-p5.md`, part 6 `shader-cache-p6.md`. Cite a part, e.g.
"`docs/engine/shader-cache.md` (part 3) §Storage format".

Evidence for these studies is not stored here: it is regenerated on demand under the gitignored
`out/_swap/` (per-subsystem subdirectories) by the harnesses named in each document.
