# Shader cache and gameplay stalls: findings and handoff

Date: 2026-10-10.
Scope: first-play freezes that become less frequent with further play and do not
recur at the same spot after restarting a song.
Status: read-only investigation completed; cause of individual freezes is not yet
measured. This document proposes follow-up work, not an implemented fix.

## Assessment

**Shader caching is active. First-use shader translation and pipeline creation are
credible causes of the reported freezes.** The D3D12 renderer has a submission
path that waits for pipeline creation even with asynchronous compilation enabled.
However, improvement on replay also fits asset loading and other resource caches.
Do not treat the shader explanation as confirmed until a stall is correlated with
compilation or a pipeline wait.

The recommended direction is to collect portable shader/pipeline records, prepare
the known pipelines before gameplay, and distribute a versioned baseline if the
measurements support that approach. Keep investigating any stalls that remain with
a warm cache.

## What the repository and local runs establish

| Finding | Evidence |
| --- | --- |
| Persistent shader storage defaults to enabled. | `store_shaders` in [graphics_system.cpp](../../../rexglue-sdk/src/graphics/graphics_system.cpp), including its `InitializeShaderStorage` implementation. |
| Stored pipelines are prepared before the main guest thread starts. | [rex_app.cpp](../../../rexglue-sdk/src/ui/rex_app.cpp) calls `InitializeShaderStorage(..., true)` before `main_thread->Resume()`. |
| The recent local runs use D3D12. | Their startup logs identify a DXGI NVIDIA GeForce GTX 970 adapter and D3D12 pipeline creation. This describes those runs, not every user's backend. |
| The cache actually loads, rather than merely being configured. | `out/build/win-amd64-release/logs/rb_blitz_006.log`, lines 207–209: 186 shader translations in 9 ms, then 356 graphics pipelines in 356 ms. |
| The stored coverage grew through use. | `rb_blitz_002.log`: 171 shader translations and 324 pipelines; subsequent local logs: 186 and 356. These are preload counts, not measured stall counts or proof of completeness. |
| Async compilation defaults to enabled. | `async_shader_compilation` in [command_processor.cpp](../../../rexglue-sdk/src/graphics/command_processor.cpp); D3D12 creation threads default to automatic selection. |
| D3D12 still waits for compilation during submission. | `D3D12CommandProcessor::EndSubmission()` calls `PipelineCache::EndSubmission()`. The latter creates queued pipelines on the processor thread and waits for busy creation workers. |
| The development profile enables render-target readback. | `out/build/win-amd64-release/rb_blitz.toml`: `d3d12_readback_resolve = true`. Readback has GPU synchronization paths and is another profiling target. |
| Earlier pacing tests already recorded an unresolved long stall. | [Known issues](../../reference/known-issues.md) and the frame-pacing close-out in [bringup log](../../history/bringup-log.md): a 1174.9 ms guest `VdSwap` gap and 263.6 ms `XE_SWAP` gap in the vsync-on run. These do not identify the cause of the current report. |
| The installer does not currently ship a prepared shader-cache baseline. | [make_payload.ps1](../../../installer/tools/make_payload.ps1) packages the executable, launcher and runtime dependencies. |

The logs and developer profile above are local, ignored artifacts and may rotate
or be absent in another checkout. Preserve copies in an investigation evidence
folder before further sessions overwrite them. Startup times on this already-used
machine do not predict first-run compilation times on another machine.

### Storage and formats

[RbBlitzApp](../../../src/rb_blitz_app.h) defaults the cache root to
`<user_data_root>/cache`, normally under Windows Documents:

```text
Documents/Rock Band Blitz/cache/shaders/shareable/
    5841122D.xsh
    5841122D.rtv.d3d12.xpso
```

Both files were present during the investigation: 103,484 bytes of shader records
and 25,644 bytes of D3D12 pipeline records. Explicit path overrides can change
their location. This cache is separate from the DLC enumeration cache and player
saves.

- `.xsh` stores guest shader microcode and identifying metadata. It is not a
  collection of final GPU machine-code binaries.
- `.xpso` stores pipeline descriptions. The D3D12 path selects `rtv` or `rov`;
  Vulkan selects `fbo` or `fsi` and uses `.vk.xpso` instead of `.d3d12.xpso`.
- On startup, the renderer uses those records to translate shaders and create
  pipelines for the current host. A shader can have multiple translations and
  participate in multiple pipelines.
- The formats have version checks. Being shareable does not make them compatible
  with every renderer revision, setting or rendering path.

Sources: D3D12 [pipeline cache](../../../rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp)
and [format definitions](../../../rexglue-sdk/include/rex/graphics/d3d12/pipeline_cache.h);
Vulkan [pipeline cache](../../../rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp).

### Why static recompilation does not remove this work

The CPU code is statically recompiled, but Xbox 360 graphics commands still go
through ReXGlue's GPU implementation. Guest shaders need host translation, and the
host driver needs graphics pipelines combining shaders with rendering state.
Encountering a new combination during a song can therefore require new work.

The D3D12 async draw path can skip a draw whose pipeline is not ready, yet the
submission path subsequently waits for queued compilation. On Vulkan, shader
translation occurs before the async pipeline decision, and placeholder pipeline
creation can also be synchronous. Switching backend or enabling async is not by
itself a guarantee of hitch-free gameplay.

## What you should do as the player

**Yes: play different visual situations and use different power-ups. You do not
need to begin by playing every song in the library.** The objective is to encounter
new effects and rendering combinations, not just increase the song count. Songs
can share graphics; two plays of the same song can expose different effects.

### First, provide a small repeatable example

1. Choose a song and power-up loadout where you noticed a freeze. Record the build,
   backend, graphics settings, retail/Ultimate mode and whether it is DLC/custom
   content. Keep these fixed for the comparison.
2. Play it and note approximately where the freeze happens. Note what appeared
   on screen, whether music continued, whether input still seemed responsive, and
   whether visuals disappeared or the whole displayed frame stopped updating.
3. Restart the song immediately with the same loadout and try to repeat the same
   actions. Record whether the freeze repeats at that event.
4. Exit normally, launch the game again and repeat once more. This separates reuse
   within one process from reuse across launches, although it still does not prove
   which cache helped.
5. Give the agent the observations and the corresponding logs. A short video is
   useful if convenient; a song-relative timestamp and event description are enough
   to start. Average FPS alone is not a useful record of a one-second freeze.

Keep your existing caches. Do not delete saves, shader caches or driver caches to
make a clean test; the agent should prepare isolated test runs when needed.

### Then, collect broader coverage with agent feedback

Once the agent can report newly observed pipelines, work through this checklist:

- [ ] Title screen, song library, loading transitions, pause/resume and results.
- [ ] Several songs with visibly different backgrounds, lighting or scenery.
- [ ] Every selectable power-up, actually activated during gameplay.
- [ ] Different power-up combinations where they produce overlapping effects.
- [ ] Track switching, sustained successful play, misses, and score/streak or
      checkpoint transitions that visibly change the scene.
- [ ] A complete song through the ending and results screen.
- [ ] Retail and Ultimate separately if both are supported release modes.
- [ ] Representative bundled, DLC and custom songs with unusual visual behavior.

The agent should maintain the coverage checklist and tell you which sessions added
pipelines. Stop repeating a route once it adds no new records, unless it is needed
to reproduce a remaining stall. Broaden to other scenes or effects instead.
Saturation on this checklist is evidence of useful coverage, not proof that every
possible pipeline has been discovered.

You should supply gameplay judgment and confirm that notes, effects, audio and
input remain correct. The agent should handle paths, logs, cache comparisons and
technical measurements. You should not need to inspect binary cache files or
guess which shader corresponds to a freeze.

## What an agent should do

The following is proposed future work. Creating this document does not authorize
renderer changes or controlled gameplay runs in the current task.

### Phase 1: establish the cause

1. Record the repository/SDK revisions, built binaries, backend, GPU/driver,
   effective settings, game/mod variant and resolved cache path. Check actual
   runtime values rather than assuming defaults or that the binaries match source.
2. Preserve the relevant logs and shader-cache snapshot. Prepare an isolated
   shader cache for controlled runs, leaving player progress and normal caches
   intact. Keep other game data and settings the same.
3. Add focused timing, if implementation is requested, for shader translation,
   pipeline misses/creation, D3D12 submission waits and GPU readback waits. Correlate
   these with frame gaps; include pipeline identity and whether work was preloaded
   or first encountered during gameplay. Avoid logging every draw to disk.
4. Compare the same route with an empty application shader cache, immediate replay,
   and full process restart with populated storage. An empty application cache does
   not imply an empty driver or OS file cache; report those limits explicitly.
5. If a long frame occurs without compilation or its associated wait, investigate
   resource upload/conversion, asset I/O/decompression and CPU/GPU synchronization.
   Do not label every first-use cost as shader compilation.
6. Report individual long-frame durations and correlated work, plus the worst
   frame and counts of frames exceeding useful thresholds such as 50/100/250 ms.
   Check that instrumentation does not itself create the observed freezes.

Deliverable: a short evidence table identifying confirmed causes, unresolved
stalls and the next change justified by the measurements.

### Phase 2: collect and validate portable coverage

1. Give the player a simple launch/capture workflow and the coverage checklist
   above. Preserve each session's configuration and records.
2. Provide a cache inspection tool/report that validates headers and hashes,
   counts unique records and reports additions per session. Preload log counts
   alone are insufficient to attribute new records to a particular gameplay event.
3. Track coverage by backend, rendering path and supported content/configuration.
   Do not assume D3D12 pipeline records also cover Vulkan.
4. Merge compatible records by identity with format validation; do not concatenate
   files or replace a richer user cache with a smaller baseline.
5. Replay the collected baseline on representative machines and verify preload
   success, remaining misses, startup cost and rendering correctness.

Deliverable: a candidate baseline plus a coverage report stating what was exercised
and what remains untested.

### Phase 3: implement preparation and distribution

1. Package a versioned baseline or generate the necessary records from the user's
   game installation. The `.xsh` format contains original game shader bytes, so
   make that an explicit distribution decision consistent with how this project
   handles game assets, rather than treating the file as metadata only.
2. Seed/merge records into the resolved writable cache without discarding existing
   discoveries. Handle missing, incompatible and corrupt records predictably.
3. Reuse the existing blocking startup preload. Add progress reporting and safe
   cancellation if its duration warrants it. Explore song-loading warmup only if
   pipeline requirements can be identified before the song begins.
4. Keep learning new combinations at runtime. Preserve provenance and invalidate
   records when relevant formats or translation assumptions change.
5. If profiling justifies it, improve the D3D12 submission wait behavior. Do not
   simply remove the wait: audit deferred commands, pending shader data, object
   lifetimes and rendering correctness first. Missing notes or incorrect effects
   are not an acceptable trade for a better frame-time graph.

Deliverable: a reviewable change with cold/warm measurements, installer coverage,
and player confirmation of correct gameplay visuals and timing.

## What can be prepared and shared

| Approach | Benefit | Limit or follow-up |
| --- | --- | --- |
| Ship guest shader records and pipeline descriptions. | Moves known first-use work to startup using infrastructure already present. | Requires coverage and compatible backend/path records; host compilation still happens. |
| Extract shaders and build a pipeline catalogue ahead of time. | Could improve coverage beyond manual captures. | Extracting every shader does not enumerate every rendering-state combination. Requires game/asset analysis. |
| Cache translated host shader bytecode. | Could avoid repeated translation work. | New cache design must account for translator revision and relevant host/settings variants. |
| Persist native driver pipeline results locally. | Can reduce repeated host pipeline creation cost. | Hardware/driver compatibility matters. This is not one universal release cache. |
| Compile in the background during startup/loading. | Moves work away from interactive song playback. | Needs early knowledge of requirements and a completion policy before gameplay. |
| Use fallback rendering while compilation finishes. | Can avoid waiting for some missing pipelines. | High correctness risk for notes and gameplay feedback; existing async paths already have limitations. |

D3D12 currently does not store native PSO blobs in this application's shader cache;
the source explicitly describes them as a possible future addition. Vulkan's
graphics pipeline creation currently passes `VK_NULL_HANDLE` for `VkPipelineCache`.
Neither observation rules out caching performed internally by the driver.

Microsoft documents native D3D12 cached PSOs as hardware-, driver- and
machine-specific: [D3D12_CACHED_PIPELINE_STATE](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_cached_pipeline_state).
Khronos describes native Vulkan pipeline caching and its potential to reduce
creation cost: [Pipeline Cache](https://docs.vulkan.org/guide/latest/pipeline_cache.html).

## Completion criteria

- A captured freeze has a measured cause, or is explicitly left unresolved.
- Known baseline pipelines finish preparation before song playback on tested
  configurations; any misses during the coverage route are reported.
- Immediate replay and full process restart are both tested.
- Startup costs and remaining long frames are reported, not hidden by average FPS.
- Player saves and previously learned cache records survive installation/updates.
- Missing/corrupt/incompatible baseline handling is checked, including fallback.
- The player confirms notes, effects, audio and input remain correct.
- Coverage and machine limits are written down; no claim of universal, complete
  pre-caching is made from a single machine or a handful of songs.
