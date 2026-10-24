# Pipeline recipe catalogue (P3, 2026-10-24)

P3 turns the renderer's learned pipeline descriptions into a **versioned, validated
catalogue**: normalized D3D12 and Vulkan recipes with their shader modifications,
deduplicated across every captured store, with provenance, local shader-identity
resolution and an explicit helper-pipeline inventory. It is offline tooling plus one
configuration record; it does not integrate preparation (P4), gate the loading
transition (P6) or make any in-song compile-work claim. The candidate catalogue for
the preserved 2026-10-24 runs is produced and every shader identity it references
resolves locally; **helper recipes are inventoried but their runtime key identity is
still the trace's call site**, which is the honest remaining P3 gap.

Three owner-driven coverage sessions (2026-10-24) then browsed the options surface and
exercised **every power-up on both backends**. They raise the D3D12 catalogue from
175 to **412 recipes** (206 identities) and the Vulkan catalogue from 144 to **409
recipes** (200 identities), all resolving locally, and the committed hash-only
candidate was extended from 186 to 208. `d3d12/rov` and `vulkan/fsi` remain
uncaptured: this machine's rendering paths are `rtv` and `fbo`.

Implementation: [shader_pipeline_catalogue.py](../../scripts/shader_pipeline_catalogue.py),
[helper inventory](../../config/pipeline_helper_inventory.json),
[hermetic tests](../../tests/shader_pipeline_catalogue_tests.py). No SDK change was
needed; the recipes are the files both caches already read and write.

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
P0 traces plus the two owner-driven coverage sessions below; outputs are under
`out/shader-cache-p3/`. Every store parsed to its last record with no duplicates
inside a store and no unverified bytes.

| Input set | Stores | Records | Unique recipes | Referenced shader identities | Resolved |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 (`rtv`), preserved P0 runs | 8 | 801 | 175 | 116 | 116 / 116 |
| D3D12 (`rtv`), plus coverage r1/r2 | 10 | 1,601 | 412 | 206 | 206 / 206 |
| Vulkan (`fbo`), preserved P0 runs | 2 | 193 | 144 | 95 | 95 / 95 |
| Vulkan (`fbo`), plus coverage r1 | 3 | 602 | 409 | 200 | 200 / 200 |
| Both, one catalogue | 13 | 2,203 | 821 | 208 | 208 / 208 |

Only `d3d12/rtv` and `vulkan/fbo` were captured; every catalogue marks
`d3d12/rov` and `vulkan/fsi` as `captured: false` and lists them under
`coverage.paths_not_captured`. Those paths are device- and setting-dependent, so
P8 must capture them rather than have this catalogue imply coverage.

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
| Vulkan `fbo` | 409 | 70 | 130 | 71 | 140 | 265 | 5 |

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
the menu, and songs with every power-up exercised (owner-confirmed). **Owner scope
decision (2026-10-24, gameplay judgment rather than a measurement): ordinary songs
and DLC packages do not vary shader usage**, so the song/DLC axis is not a coverage
obligation; the graphics-settings matrix and the `rov`/`fsi` paths still are. P8 owns
that remainder.

The merged stores are replayed by the same parser and reproduce the unique sets:
`out/shader-cache-p3/merged-rtv-coverage.d3d12.xpso` = 29,676 bytes / 412 records and
`merged-fbo-coverage.vk.xpso` = 27,824 bytes / 409 records (both re-sorted by
description hash, so not byte-identical to the session store each summarises);
`merged-rtv.d3d12.xpso` = 12,612 bytes / 175 records and `merged-fbo.vk.xpso` =
9,804 bytes / 144 records are the preserved-P0-only sets. Nothing in `out/` is
distributed.

The 208 recipe-referenced identities **exceed** P2's original 186-identity candidate
by 22 (5 vertex, 17 pixel): the candidate was the union of every `.xsh` translated
shader at that time, and the power-up route reached shaders no earlier store
referenced. All 208 resolve from the installed content with no prior gameplay, and
`config/shader_resolution_candidate.json` was extended to exactly that set (still
hash-only, no shader bytes).

## Coverage sessions (2026-10-24)

Three sessions with the owner driving the game, all on the same workstation, each
prepared by `scripts/shader_cache_trace.py prepare` with its own cache, saves and
profile copies and `--shader_compile_trace=true`:

| Run | Start | Records after | Boot preload | Route and outcome |
| --- | --- | ---: | --- | --- |
| `d3d12-ultimate-coverage-r1` | 132-record learned store | 388 | 132 | Options surface and songs with several power-ups. Ended in an unhandled guest access violation (read of guest `0x3FFF1A00`, thread `0xF8000028`) that the owner attributes to a different ticket; the store still parsed to its last record with **no unverified tail**, so the crash cost no captured evidence. |
| `d3d12-ultimate-coverage-r2` | r1's collected 388-record snapshot | 412 | 388 required / 388 succeeded, 0 failed, 0 missing in 367 ms | Every power-up exercised (owner-confirmed) and the game closed normally, so the cache writers drained. |
| `vulkan-ultimate-coverage-r1` | the 144-record retail `fbo` store | 409 | 144 required / 144 succeeded, 0 failed, 0 missing | Vulkan/Ultimate — a combination P0 never booted — with every power-up exercised (owner-confirmed), closed normally. |

The Vulkan session seeded **retail** records and then ran **Ultimate** content, and
its final store still contains every recipe of both preserved Vulkan stores, so the
two variants' pipelines overlapped completely on this machine.

In-song work is where these recipes come from on both backends: r1 counted 226 guest
pipeline creations and 97 translations in its `active_song` phase, and the Vulkan
session counted 221 creations, 94 translations and **29 compile waits totalling
2.90 s (worst 168.3 ms)** although its 144 seeded recipes had preloaded. The phase
counters are cumulative, so they are not per-frame costs, but they are the first-play
work P4/P6 exist to move off playback.

| Change | D3D12 recipes | Vulkan recipes | D3D12 identities | Vulkan identities |
| --- | ---: | ---: | ---: | ---: |
| Preserved P0 stores | 175 | 144 | 116 | 95 |
| After the first coverage session | 388 (+213) | — | 197 | — |
| Final | 412 (+237) | 409 (+265) | 206 (+90) | 200 (+105) |

**What this establishes:** the power-up effects, the options surface and the songs
actually played are captured, validated and locally resolvable on **both** backends,
and each backend's final store subsumes that backend's earlier stores. **What it does
not establish:** saturation. Graphics settings were not varied and neither
`d3d12/rov` nor `vulkan/fsi` was ever selected; the owner's judgment that songs and
DLC do not vary shader usage removes that axis from the list but is not itself a
measurement. If a future package ever does introduce a new effect, the renderer keeps
learning and the catalogue gains a recipe, so the cost is an extension of this
catalogue rather than a broken guarantee.

## Helper pipeline inventory

Guest pipelines live in `.xpso`; the renderer also creates graphics and compute
pipelines directly (render-target transfer, dump, resolve, presenters, immediate
drawers). Those are not stored in `.xpso`, so P0's trace identifies them by creation
site. P3 inventories every such site in
[`config/pipeline_helper_inventory.json`](../../config/pipeline_helper_inventory.json)
with its key composition and dependencies. The test checks each declared trace site
still appears verbatim in the SDK source, so the list cannot silently drift.

| Backend | Helper | Kind | Created | Key identity in `shader_compile_trace` |
| --- | --- | --- | --- | --- |
| D3D12 | `clear_rtv` | graphics | startup | call site, 6 pipelines |
| D3D12 | `resolve_copy` | compute | startup | shared util site |
| D3D12 | `host_depth_store` | compute | startup | shared util site |
| D3D12 | `rov_resolve_clear` | compute | startup (ROV) | shared util site |
| D3D12 | `transfer` | graphics | lazy | call site, one per `TransferShaderKey` |
| D3D12 | `transfer_stencil_bit` | graphics | lazy | call site, 8 per key |
| D3D12 | `dump` | compute | lazy | shared util site, one per `DumpPipelineKey` |
| D3D12 | `immediate_drawer_triangle` / `_line` | graphics | startup | unique site |
| D3D12 | `presenter_intermediate` / `_final` | graphics | startup | call site, per paint effect |
| D3D12 | `compute_util` | compute | shared helper | shared util site |
| Vulkan | `transfer` | graphics | lazy | call site, one per `TransferPipelineKey` |
| Vulkan | `transfer_masked_samples` | graphics | lazy | call site, derivative samples |
| Vulkan | `resolve_copy` / `host_depth_store` / `fsi_resolve_clear` / `dump` | compute | startup or lazy | shared util site |
| Vulkan | `swap_gamma_graphics` / `_rb_swap` / `_compute` / `swap_fxaa` | graphics/compute | startup | call site, several per site |
| Vulkan | `immediate_drawer_triangle` / `_line` | graphics | startup | unique site |
| Vulkan | `presenter` | graphics | startup | call site, per paint effect |
| Vulkan | `compute_util` | compute | shared helper | shared util site |

Coverage is **26 inventoried: 4 site-unique, 22 site-repeated**. A site-repeated
helper is fully described statically but the trace's `key=0` collapses its variants,
so the catalogue cannot attribute an observed helper creation to one recipe yet.
This is an explicit blocker, not a silent success; the catalogue still exits 2 when
it is present. Carrying the real keys into the trace is bounded work for the P4
preparation lifecycle or the P8 measurement pass, not a guest transition guess.

## What is still unproven

- **Reachability.** A captured store set is evidence of exercised pipelines, not
  proof that a setting or a future package cannot add a recipe. With the coverage
  sessions every power-up is represented on both backends, and the owner states that
  ordinary songs and DLC do not vary shader usage (judgment, not a measurement); the
  graphics-settings matrix and the `rov`/`fsi` rendering paths remain untested. No
  cartesian enumeration of shader pairs was performed or claimed.
- **Helper key identity.** As above: call-site identity only; variants are known
  from source but not carried at runtime.
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
validation; CLI exit codes and overwrite refusal; a replayable merged store; a
refused mixed-backend merge; directory expansion; the SDK version pins; the shipped
inventory's source/trace-site drift guard; and — when the local P0 evidence exists —
every preserved store parses with no unverified tail. Full release `ctest` is
**55/55** with `TEMP`/`TMP` under the workspace `out/` (the pre-existing
`path_policy` default-temp issue is unchanged). The tooling itself required no game
run; the three coverage sessions above were owner-driven. After each candidate
extension the shader subset (`shader_asset_resolver`, `_retail`, `_ultimate`,
`shader_pipeline_catalogue`, `shader_compile_trace_enabled`, `_disabled`) passes
**6/6**, including the installed-content check that all 208 candidate identities
still resolve.

Next: P4 separates storage opening from a reusable asynchronous preparation
operation and consumes this catalogue; P6 attaches it to the proven post-A
boundary; P8 validates the route matrix and helper coverage on both backends.
