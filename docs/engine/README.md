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
| [`shader-cache-p0.md`](shader-cache-p0.md) | The post-A loading gate and the shader-compilation measurement: the source-supported continuation and all ten exits, the progress object's ownership and lifetime, the opt-in `shader_compile_trace` instrument and its limits, and the 2026-10-24 empty/warm/Vulkan runs with their counters and long frames. |
| [`shader-cache-p2.md`](shader-cache-p2.md) | Local shader resolution: the `XOBX` asset framing, vertex declaration linking, bounded executable sources, and the 186-identity candidate that resolves from installed content without prior gameplay. |
| [`shader-cache-p3.md`](shader-cache-p3.md) | The pipeline recipe catalogue: the `.xpso` record layout and version pins, the validate/dedupe/merge tool with provenance and local identity checks, the helper-pipeline inventory and its key-identity gap, and the 2026-10-24 preserved-run measurements. |
| [`shader-cache-p4.md`](shader-cache-p4.md) | The preparation lifecycle: the cancellable generation both backends drive in bounded command-processor slices, its phases/key/progress/cancellation rules, the separate preparation pools that keep submissions from waiting, and the launch switch. |
| [`shader-cache-p5.md`](shader-cache-p5.md) | Native pipeline caches: the adapter/driver/translator identity, the bounded per-entry store and its refusal rules, D3D12 cached PSO blobs with the rejected-blob retry, the device-wide Vulkan pipeline cache and its merge-at-save policy, and what is left to P6/P7/P8. |
| [`vulkan-performance.md`](vulkan-performance.md) | Warm-cache Vulkan overhead: GPU texture conversion replacing CPU readback/conversion/upload, headless GPU validation and the pending gameplay comparison. |
| [`d3d12-loading-freeze.md`](d3d12-loading-freeze.md) | The DX12 post-A freeze report, invalid fence HRESULT/event sequencing, incomplete resolve readback, resource retirement fixes and headless validation. |
| [`diagnostics.md`](diagnostics.md) | The two standing instruments: the runtime probe (what the guest is doing, now) and the UI-state observer (what a captured state is made of). Both are also the harness the acceptance scripts drive. |
| [`messages.md`](messages.md) | The retail Messages screen: its record and panels, the `InboxProvider` and `InboxMessage` objects, the empty-state tokens, the bottom-bar inline help, and the row-icon set (twelve 512×512 DXT1 icons with converted PNGs). |

Reading order for a new feature: find its toggle in [`toggles.md`](toggles.md), then follow the
document it names — that document is where the addresses, the file offsets and the measurements are.

Evidence for these studies is not stored here: it is regenerated on demand under the gitignored
`out/_swap/` (per-subsystem subdirectories) by the harnesses named in each document.
