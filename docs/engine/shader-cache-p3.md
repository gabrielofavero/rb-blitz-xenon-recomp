# Pipeline recipe catalogue (P3, 2026-10-24)

P3 turns the renderer's learned pipeline descriptions into a **versioned, validated
catalogue**: normalized D3D12 and Vulkan recipes with their shader modifications,
deduplicated across every captured store, with provenance, local shader-identity
resolution and an explicit helper-pipeline inventory. It is offline tooling plus one
configuration record; it does not integrate preparation (P4), gate the loading
transition (P6) or make any in-song compile-work claim. The candidate catalogue for
the preserved 2026-10-24 runs is produced and every shader identity it references
resolves locally; **helper recipes are inventoried and their runtime creation keys are
carried by the trace** (SDK patch 0026), so the catalogue no longer has an unresolved
helper identity.

Five owner-driven coverage sessions (2026-10-24) then browsed the options surface and
exercised **every power-up on both backends**, and finally the two alternate rendering
paths. The D3D12 `rtv` catalogue went from 175 to **412 recipes**, the Vulkan `fbo` one
from 144 to **409**; `d3d12/rov` and `vulkan/fsi` were captured separately (219 and 228
recipes), which took the combined catalogue to **1,268 recipes across all four declared
paths, 208/208 shader identities resolved** and the committed hash-only candidate from
186 to 208.

Implementation: [shader_pipeline_catalogue.py](../../scripts/shader_pipeline_catalogue.py),
[helper inventory](../../config/pipeline_helper_inventory.json),
[hermetic tests](../../tests/shader_pipeline_catalogue_tests.py). The catalogue itself
needs no SDK change — it reads the files both caches already write; SDK patch 0026 only
improves the separate trace (helper key identity).

## What the catalogue contains

| Part | Content |
| --- | --- |
| `stores` | Every input file's SHA-256, size, backend/rendering path, verified record count, duplicates and any unverified tail. |
| `backends.<backend>.paths.<path>` | One entry for every declared rendering path (`rtv`, `rov`, `fbo`, `fsi`) with `captured: true/false`; an uncaptured path is recorded rather than omitted. `recipes` holds each unique normalized `PipelineDescription`: its raw 64-byte (D3D12) or 60-byte (Vulkan) bytes, its stored `description_hash`, the vertex/pixel shader identity and translation modification, decoded state, and the stores it came from. |
| `backends.<backend>.paths.<path>.coverage` | Recipe count, unique shader and modification counts, shader pairs, recipes without a pixel shader, and the decoded state axes actually present. |
| `helpers` | The helper-pipeline inventory (creation site, key composition, dependencies, when it is created) and its coverage counts. |
| `coverage` | Shader-resolution result and reasons, committed-candidate scope, helper key-identity gaps, `paths_not_captured`, explicit blockers, and `live_ready: false`. |
| `provenance` | Input order and tool identity. |

Recipes are **not** game shader bytes: a description is the renderer's own
normalized state plus the 64-bit shader identities it references. Microcode stays in
the user's `.xsh`/`.xpso` storage and is never embedded in the catalogue.

Merging is a **union, never a replacement**: every input record is kept, identical
descriptions collect all their stores as provenance, and no richer store can be
replaced by a smaller one. A same-hash/different-bytes pair is refused rather than
deduplicated.

## Storage format, measured

The runtime writes a 12-byte header and fixed-size records
([D3D12](../../rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp),
[Vulkan](../../rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp)):

| Field | D3D12 | Vulkan |
| --- | --- | --- |
| File magic | `XEPS` (`0x53504558`) | `XEPS` |
| API selector | `DXRT` (rtv) / `DXRO` (rov) | `0` (fbo) / `1` (fsi) |
| Stored version (byte-swapped) | `0x20260226` = max(`PipelineDescription::kVersion` `0x20210425`, `DxbcShaderTranslator::Modification::kVersion` `0x20260226`) | `0x20260228` = max(`0x20260228`, SPIR-V `Modification::kVersion` `12`) |
| Description | 64 bytes, packed | 60 bytes, packed |
| Stored record | 8-byte hash + 64 = 72 bytes | 8-byte hash + 60 = 68 bytes |

Measured headers (2026-10-24): a preserved D3D12 store begins
`58 45 50 53 44 58 52 54 20 26 02 26`; a preserved Vulkan store begins
`58 45 50 53 00 00 00 00 20 26 02 28`. The record sizes are confirmed by every
preserved file dividing exactly: 25,068 = 12 + 348 × 72; 9,804 = 12 + 144 × 68.

Each record's hash is `XXH3_64bits(description)` and is checked with the SDK's own
implementation through `rb_blitz_shader_microcode_hash` — the same bounded helper
P2 uses for microcode. A wrong stride, endianness or field layout fails the whole
store instead of publishing wrong recipes. The tests also re-read the four SDK
headers and refuse the recorded version constants when they drift.

The loader itself stops at the first corrupt record and truncates the file on its
next load. The catalogue mirrors that rule: an unverified tail is refused unless
`--accept-truncated-tail` is passed, and then only the verified prefix is used, the
byte count is recorded, and coverage is incomplete.

## Commands

```powershell
# One store, catalogue only (shader resolution not checked -> exit 2).
python scripts/shader_pipeline_catalogue.py --store <store.xpso> --catalogue-out out/catalogue.json

# Merge every preserved store for a backend and verify its identities locally.
python scripts/shader_pipeline_catalogue.py --store <a.xpso> --store <b.xpso> `
  --game-root game --catalogue-out out/d3d12-catalogue.json --merged-store out/merged-rtv.d3d12.xpso

# Cheap re-check against a P2 resolver index, without reading installed content.
python scripts/shader_pipeline_catalogue.py --store <store.xpso> `
  --resolver-index <p2-index.json> --catalogue-out out/catalogue.json
```

`--store` also accepts a directory (searched recursively for `*.xpso`). A catalogue
and a merged store are created exclusively and never overwritten. The merged store
is byte-compatible with the cache: same magic, API selector and version, records
sorted by description hash, every description byte preserved.

Exit codes: **0** = published with complete declared coverage; **2** = published
with explicit blockers (unresolved shader identities, helper recipes whose runtime
key is only a call site, an accepted corrupt tail, or shader resolution not checked);
**1** = invalid input or tool failure.

## Measurements from the preserved runs (2026-10-24)

All inputs are the preserved runs under `out/shader-cache-p0/runs/` — the controlled
P0 traces plus the five owner-driven coverage sessions below; outputs are under
`out/shader-cache-p3/`. Every store parsed to its last record with no duplicates
inside a store and no unverified bytes.

| Input set | Stores | Records | Unique recipes | Referenced shader identities | Resolved |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 (`rtv`), preserved P0 runs | 8 | 801 | 175 | 116 | 116 / 116 |
| D3D12 (`rtv`), plus coverage r1/r2 | 10 | 1,601 | 412 | 206 | 206 / 206 |
| D3D12 (`rov`), coverage r1 | 1 | 219 | 219 | 163 | 163 / 163 |
| Vulkan (`fbo`), preserved P0 runs | 2 | 193 | 144 | 95 | 95 / 95 |
| Vulkan (`fbo`), plus coverage r1 | 3 | 602 | 409 | 200 | 200 / 200 |
| Vulkan (`fsi`), coverage r1 | 1 | 228 | 228 | 172 | 172 / 172 |
| D3D12, both paths | 11 | 1,820 | 631 | 206 | 206 / 206 |
| All four paths, one catalogue | 15 | 2,650 | 1,268 | 208 | 208 / 208 |

**All four declared rendering paths are now captured.** Each alternate path has its own
store (`*.rov.d3d12.xpso`, `*.fsi.vk.xpso`) and is selected explicitly with
`render_target_path_d3d12=rov` / `render_target_path_vulkan=fsi`; the default path on
this machine is `rtv`/`fbo`. The path is part of every recipe's identity, and the two
D3D12 paths share no recipe at all (412 + 219 = 631), so a catalogue that reported only
`rtv` would have implied coverage it did not have. A merged store may not span paths:
the tool refuses that, which is why the 11-store D3D12 catalogue is published without one
and each path keeps its own merged store.

The D3D12 warm store that P0 measured as a **132-pipeline boot preload** is exactly
132 records here; the empty first-play store adds 46, 144, 160, 22, 137 and 16 in
the other runs. The D3D12 coverage sessions started from that 132-record store, added
256 records in the first session (388) and 24 more in the second (412); the Vulkan
session started from the 144-record retail store and added 265 (409). Each backend's
final store **already contains every recipe of that backend's earlier stores**, so
the merges add ordering, provenance and validation rather than new descriptions.

| Backend / path | Recipes | Vertex shaders | Pixel shaders | Vertex mods | Pixel mods | Shader pairs | No pixel shader |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D12 `rtv` | 412 | 71 | 135 | 72 | 145 | 272 | 4 |
| D3D12 `rov` | 219 | 64 | 99 | 65 | 100 | 199 | 2 |
| Vulkan `fbo` | 409 | 70 | 130 | 71 | 140 | 265 | 5 |
| Vulkan `fsi` | 228 | 64 | 108 | 65 | 109 | 206 | 3 |

State axes present (decoded values, not a completeness claim):

- D3D12: MSAA 1x/2x/4x (`host_msaa_samples` 0/1/2, 362/48/2 recipes); depth formats
  0/1; depth funcs 4/6/7; cull modes 0/2; primitive topologies 2 (393), 1 (12) and
  0 (7); strip cut always 0; every used render target is color format 0
  (`k_8_8_8_8`), with write masks 15 (328 recipes), 7 (77) and 0 (7); depth write is
  on for 289 recipes and the stencil test for 3.
- Vulkan: render-pass MSAA 1x/2x/4x (`render_pass.msaa_samples` 0/1/2,
  346/61/2 recipes); primitive topologies 3 (375), 4 (13), 6 (12) and 0 (9); depth
  compare 4 (271), 6 (59), 7 (52) and 0 (27); depth write on for 279 recipes and the
  stencil test for 3; polygon mode 0; tessellation mode 0; sample-rate shading 0;
  used color view format 0; color write masks 15 (331), 7 (69) and 0 (9); six
  distinct render-pass keys.

These are **the recipes the captured sessions reached**, not the renderer's full
reachable set. On both backends they now include title, loading, the options surface,
the menu, songs with every power-up exercised (owner-confirmed) and both rendering
paths of each backend. **Owner scope decision (2026-10-24, gameplay judgment rather
than a measurement): ordinary songs and DLC packages do not vary shader usage**, so
the song/DLC axis is not a coverage obligation; the graphics-settings matrix is the
remaining axis. P8 owns it.

The merged stores are replayed by the same parser and reproduce the unique sets:
`out/shader-cache-p3/merged-rtv-coverage.d3d12.xpso` = 29,676 bytes / 412 records,
`merged-fbo-coverage.vk.xpso` = 27,824 bytes / 409 records, `merged-rov.d3d12.xpso` =
15,780 bytes / 219 records and `merged-fsi.vk.xpso` = 15,516 bytes / 228 records (all
re-sorted by description hash, so not byte-identical to the session store each
summarises); `merged-rtv.d3d12.xpso` = 12,612 bytes / 175 records and
`merged-fbo.vk.xpso` = 9,804 bytes / 144 records are the preserved-P0-only sets.
Nothing in `out/` is distributed.

The 208 recipe-referenced identities **exceed** P2's original 186-identity candidate
by 22 (5 vertex, 17 pixel): the candidate was the union of every `.xsh` translated
shader at that time, and the power-up route reached shaders no earlier store
referenced. All 208 resolve from the installed content with no prior gameplay, and
`config/shader_resolution_candidate.json` was extended to exactly that set (still
hash-only, no shader bytes).

## Coverage sessions (2026-10-24)

Five sessions with the owner driving the game, all on the same workstation, each
prepared by `scripts/shader_cache_trace.py prepare` with its own cache, saves and
profile copies and `--shader_compile_trace=true`:

| Run | Start | Records after | Boot preload | Route and outcome |
| --- | --- | ---: | --- | --- |
| `d3d12-ultimate-coverage-r1` | 132-record learned store | 388 | 132 | Options surface and songs with several power-ups. Ended in an unhandled guest access violation (read of guest `0x3FFF1A00`, thread `0xF8000028`) that the owner attributes to a different ticket; the store still parsed to its last record with **no unverified tail**, so the crash cost no captured evidence. |
| `d3d12-ultimate-coverage-r2` | r1's collected 388-record snapshot | 412 | 388 required / 388 succeeded, 0 failed, 0 missing in 367 ms | Every power-up exercised (owner-confirmed) and the game closed normally, so the cache writers drained. |
| `vulkan-ultimate-coverage-r1` | the 144-record retail `fbo` store | 409 | 144 required / 144 succeeded, 0 failed, 0 missing | Vulkan/Ultimate — a combination P0 never booted — with every power-up exercised (owner-confirmed), closed normally. |
| `d3d12-rov-coverage-r1` | the 412-record `rtv` store plus the 208-shader `.xsh` | 219 (`rov`) | none: the `rov` store did not exist yet | `render_target_path_d3d12=rov`, one song with power-ups, closed normally. The `rtv` store was untouched beside it. Owner reported heavy first-play-style stuttering. |
| `vulkan-fsi-coverage-r1` | the 409-record `fbo` store plus the `.xsh` | 228 (`fsi`) | none: the `fsi` store did not exist yet | `render_target_path_vulkan=fsi` (the device advertises `fragmentShaderSampleInterlock` and `fragmentShaderPixelInterlock`), one song with power-ups, closed normally. |

The Vulkan power-up session seeded **retail** records and then ran **Ultimate** content,
and its final store still contains every recipe of both preserved Vulkan stores, so the
two variants' pipelines overlapped completely on this machine. The alternate paths did
not overlap at all: `rtv` and `rov` share no recipe, and a merged store spanning two
paths is refused by the tool.

In-song work is where these recipes come from on both backends. The `active_song` phase
counters below are cumulative CPU-time sums, not wall time (work overlaps across
workers), but they show the shape clearly: a path whose store is empty creates every
pipeline during the song, and the cost is pipeline creation, not shader translation.

| Session | Guest pipelines created in `active_song` | Creation work (cumulative) | Compile waits |
| --- | ---: | ---: | --- |
| `d3d12` `rtv` r2 (412-record store preloaded) | 226 | 827 ms, worst 19.3 ms | 23, 110 ms total, worst 13.2 ms |
| `vulkan` `fbo` (144-record store preloaded) | 221 | 11.3 s, worst 536 ms | 29, 2.90 s total, worst 168.3 ms |
| `d3d12` `rov` (no `rov` store) | 165 | **61.9 s, worst single 8.38 s** | **29, 11.9 s total, worst 859 ms**, 23 of them ≥250 ms |
| `vulkan` `fsi` (no `fsi` store) | 172, plus 172 placeholders | **85.2 s, worst 1.86 s** | **34, 13.4 s total, worst 701 ms**, 25 of them ≥250 ms |

The ROV and FSI sessions are the owner's "felt like playing for the first time"
reports: on those runs only 44 ms and 1.54 s respectively were spent translating
shaders, so the stalls were driver pipeline creation on an empty per-path store. This
is the first-play cost P4/P6 exist to move off playback; it is **not** a regression
and nothing in P3 changed when a pipeline is created.

| Change | D3D12 recipes | Vulkan recipes | D3D12 identities | Vulkan identities |
| --- | ---: | ---: | ---: | ---: |
| Preserved P0 stores | 175 | 144 | 116 | 95 |
| After the first coverage session | 388 (+213) | — | 197 | — |
| Primary paths (`rtv`, `fbo`) | 412 (+237) | 409 (+265) | 206 (+90) | 200 (+105) |
| Alternate paths (`rov`, `fsi`) | 631 (+219) | 637 (+228) | 206 (no new) | 206 (no new) |

**What this establishes:** the power-up effects, the options surface, the songs
actually played and **all four declared rendering paths** are captured, validated and
locally resolvable, and each store subsumes the earlier ones on its own path. **What
it does not establish:** saturation. Graphics settings were not varied; the owner's
judgment that songs and DLC do not vary shader usage removes that axis from the list
but is not itself a measurement. If a future package or setting does introduce a new
effect, the renderer keeps learning and the catalogue gains a recipe, so the cost is
an extension of this
catalogue rather than a broken guarantee.

## Helper pipeline inventory

Guest pipelines live in `.xpso`; the renderer also creates graphics and compute
pipelines directly (render-target transfer, dump, resolve, presenters, immediate
drawers). Those are not stored in `.xpso`, so P0's trace identifies them by creation
site. P3 inventories every such site in
[`config/pipeline_helper_inventory.json`](../../config/pipeline_helper_inventory.json)
with its key composition and dependencies. The test checks each declared trace site
still appears verbatim in the SDK source, so the list cannot silently drift.

| Backend | Helper | Kind | Created | Trace key (upper byte = `HelperKind`) |
| --- | --- | --- | --- | --- |
| D3D12 | `clear_rtv` | graphics | startup | `kClearRtv`, format index << 8 \| sample index |
| D3D12 | `resolve_copy` | compute | startup | `kResolveCopy`, shader index \| 0x100 when scaled |
| D3D12 | `host_depth_store` | compute | startup | `kHostDepthStore`, 0/1/2 = 1x/2x/4x |
| D3D12 | `rov_resolve_clear` | compute | startup (ROV) | `kRovClear`, 32bpp/64bpp \| 0x100 when scaled |
| D3D12 | `transfer` | graphics | lazy | `kTransfer` + `TransferShaderKey` |
| D3D12 | `transfer_stencil_bit` | graphics | lazy | `kTransferStencilBit` + key; variant = stencil bit |
| D3D12 | `dump` | compute | lazy | `kDump` + `DumpPipelineKey` |
| D3D12 | `immediate_drawer_triangle` / `_line` | graphics | startup | unique site, no key needed |
| D3D12 | `presenter_intermediate` / `_final` | graphics | startup | `kPresenter`, effect \| 0x100 for intermediate |
| D3D12 | `texture_load` / `_scaled` | compute | startup | `kTextureLoad`, load shader index \| 0x100 when scaled |
| D3D12 | `swap_gamma_compute` / `swap_fxaa` / `resolve_downscale` | compute | startup | `kSwapChain`, variant ranges 0x200–0x500 |
| D3D12 | `compute_util` | compute | shared helper | `kComputeUtil` + the caller's key |
| Vulkan | `transfer` | graphics | lazy | `kTransfer`, `render_pass_key` << 32 \| `shader_key` |
| Vulkan | `transfer_masked_samples` | graphics | lazy | same key; variant = sample index |
| Vulkan | `resolve_copy` / `host_depth_store` / `fsi_resolve_clear` / `dump` | compute | startup or lazy | `kResolveCopy` / `kHostDepthStore` / `kRovClear` / `kDump` |
| Vulkan | `texture_conversion` | compute | startup | `kTextureConversion` (one pipeline, format is a push constant) |
| Vulkan | `texture_load` / `_scaled` | compute | startup | `kTextureLoad`, load shader index \| 0x100 when scaled |
| Vulkan | `swap_gamma_graphics` / `_rb_swap` / `_compute` / `swap_fxaa` / `resolve_downscale` | graphics/compute | startup | `kSwapChain`, variant ranges 0x000–0x500 |
| Vulkan | `immediate_drawer_triangle` / `_line` | graphics | startup | unique site, no key needed |
| Vulkan | `presenter` | graphics | startup | `kPresenter`, effect \| 0x100 for intermediate |
| Vulkan | `compute_util` | compute | shared helper | `kComputeUtil` + the caller's key |

**35 entries: 4 site-unique, 31 key-identified, 0 unresolved.** `HelperKind` names the
creator in the key's upper byte and the rule is documented per entry in the
inventory's `trace.key_bits`, so a logged `key=` can be read back into a recipe. Two
tests keep that honest: every declared trace site must still occur verbatim in the SDK
source, and every `kind N (kName)` in the inventory must match the value the SDK header
gives that enumerator. A creation logged with `key=0` at a shared util site is a caller
that has not been keyed yet — it is visible as such rather than mistaken for identity.
The GPU texture-conversion helper was added after these captures and has headless
numeric validation; its gameplay validation remains pending (see
[Vulkan performance](vulkan-performance.md)).

## What is still unproven

- **Reachability.** A captured store set is evidence of exercised pipelines, not
  proof that a setting or a future package cannot add a recipe. With the coverage
  sessions every power-up is represented on both backends and all four declared
  rendering paths are captured, and the owner states that ordinary songs and DLC do
  not vary shader usage (judgment, not a measurement); the graphics-settings matrix
  remains untested. No cartesian enumeration of shader pairs was performed or claimed.
- **Helper key identity.** Carried since 2026-10-24 (patch 0026). What remains is
  that a key is only as good as its fields: a helper whose recipe also depends on a
  setting not in its key would collide. The documented layouts are the current claim;
  P8's route matrix is what would expose a collision.
- **Portability.** A recipe is valid only for the same backend/rendering path,
  translator/settings generation, render-pass format and shader identities. The
  catalogue records the version constants and the path, and refuses a mismatched
  store; it does not prove another GPU, driver or DLC installation is covered.
- **Preparation.** No live GPU pipeline is created by this work, nothing is imported
  into a player's cache, and `.xsh`/`.xpso` files on disk are untouched. P4 and P6
  own that. A merged store has been re-parsed by this tool and structurally verified
  (header, record size, every hash); **no game boot has loaded one yet**.
- **`live_ready`.** Always false: local resolution and a stored recipe are not a
  resident, verified GPU pipeline.

## Validation

`cmake --build --preset win-amd64-release` links `rb_blitz` and the helper. The new
`shader_pipeline_catalogue` ctest runs seventeen hermetic tests: both backends'
field decoding; every header/API/version rejection; title mismatch; an accepted
corrupt tail and its byte count; cross-store deduplication and provenance; hash
collisions; resolver-index and candidate scope; inventory schema/duplicate/site
validation; the key-identity pairing rules (`key` ⇔ `key_identified`, and a keyed
entry must document its bit layout); CLI exit codes and overwrite refusal; a
replayable merged store; a refused mixed-backend merge; directory expansion; the SDK
version pins; the shipped inventory's source/trace-site drift guard and its
`HelperKind` value guard; and — when the local P0 evidence exists — every preserved
store parses with no unverified tail. Full release `ctest` is **55/55** with
`TEMP`/`TMP` under the workspace `out/` (the pre-existing `path_policy` default-temp
issue is unchanged). The tooling itself required no game run; the three coverage
sessions above were owner-driven. After each candidate extension the shader subset
(`shader_asset_resolver`, `_retail`, `_ultimate`, `shader_pipeline_catalogue`,
`shader_compile_trace_enabled`, `_disabled`) passes **6/6**, including the
installed-content check that all 208 candidate identities still resolve. After patch
0026, `apply_sdk_patches.ps1 -Check` replays 25 patches with 0 failed and 0
unexpected SDK files, and the combined catalogue exits **0** — complete declared
coverage across 15 stores and all four rendering paths, which the earlier 13-store
revision (same records, one path per backend, call-site-only helper keys) reported as
exit 2.

Next: P4 separates storage opening from a reusable asynchronous preparation
operation and consumes this catalogue; P6 attaches it to the proven post-A
boundary; P8 validates the route matrix and helper coverage on both backends.
