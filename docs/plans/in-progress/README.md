# In-progress plans

Plans that are actively being worked. A plan moves here from [`../todo/`](../todo/) when work starts
and leaves for [`../finished/`](../finished/) when it is delivered — one `git mv`, keeping its
`YYYY-MM-DD-` prefix.

The plan filed here is [`2026-10-13-rhythmverse-store-plan.md`](2026-10-13-rhythmverse-store-plan.md) —
the RhythmVerse customs catalogue inside the retail DOWNLOAD CONTENT store (P0 → P6 → P1 → P2 → P3 →
P4 → P5 → P7 → P8). It **finished on 2026-10-22** — its acceptance harness is green in all four legs
on one build (`out/store-acceptance/p8-final/`, 69 steps, 0 failures) — and was **reopened the same
day** for the owner's pass: **§9** carries fourteen prompts, **P9-1 … P9-14**, one per adjustment
(the master switch's default and its restart prompt, the settings reorganisation, the store's labels
and footer, the LT/RT tabs on Your Songs, the boot warm-up with its offline popup, the move to the
title's own keyboard search, the two main-menu/refresh row renames of P9-12, P9-13's
charter/release-date/audio-type/download block in the details page's download box, and P9-14's
A Refresh on the download toast, landed 2026-10-24 and still owing its same-session boot). Those
prompts are owed; the records in §5 are what shipped before the pass, and the P8 close-out record still
says which rows were measured by a probe rather than by the harness.

[`2026-10-10-shader-cache-plan.md`](2026-10-10-shader-cache-plan.md) is the shader-stutter program,
moved here from `todo/` and renamed on 2026-10-24. **P0 is done**: the post-A gate map, the opt-in
`shader_compile_trace` instrument and the repeatable measurement workflow, with controlled
D3D12 empty/warm and Vulkan empty traces collected the same day
([`../../engine/shader-cache.md`](../../engine/shader-cache.md) part 1, evidence under
`out/shader-cache-p0/runs/`). **P1** made preload completion explicit on both backends.
**P2** resolves all 186 candidate shader identities from installed content
([`../../engine/shader-cache.md`](../../engine/shader-cache.md) part 2). **P3** validates,
deduplicates and merges the renderer's pipeline recipes with provenance and resolves every
recipe-referenced identity, and inventories the 26 helper-pipeline sites
([`../../engine/shader-cache.md`](../../engine/shader-cache.md) part 3). **P4** turned preparation
into a cancellable generation both backends advance in bounded command-processor slices
([`../../engine/shader-cache.md`](../../engine/shader-cache.md) part 4), and **P5** persists the
driver's own results beside the shareable recipes - D3D12 cached PSO blobs and one real Vulkan
pipeline cache shared by every creation path
([`../../engine/shader-cache.md`](../../engine/shader-cache.md) part 5). P6–P9 (the post-A phase,
invalidation, validation and packaging) are prompts; the post-A gate and the cold/warm measurement
remain open.

The most recent plan before it,
[`2026-10-19-interface-gameplay-mapping-restructure-plan.md`](../finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md) —
Interface Mapping versus always-on Gameplay Mapping, preset-aware controller defaults, inferred
Custom, the 3→1 listening countdown, row-level binding text, and new Mouse & Keyboard defaults —
was delivered on 2026-10-20: P1–P5 all landed, ending with the countdown and the live proof.

The in-game settings program outgrew the plan format and lives with its subject; its live tracker is
[`../../settings/status.md`](../../settings/status.md).

See [`../README.md`](../README.md) for the status taxonomy.
