# Apple performance implementation plan

The [interface memory delivery](../finished/2026-11-26-interface-render-memory.md)
implements compact non-sparse MoltenVK scaled-resolve backing, authored portrait
Interface targets and late allocation rejection. Those allocation paths supersede
the monolithic-backing assumptions below. Device acceptance and sustained timing
remain open; this does not complete the broader profiling or 1080p performance gates.

Date: 2026-11-26. Status: planning only; implementation and new gameplay measurements have not started.

Target sustained 60 FPS at 1080p-equivalent internal resolution on iOS and macOS,
with correct audio timing, sharp interface text and the existing scene content.
The first work is to measure the current critical path, then reduce CPU work,
GPU memory traffic and presentation stalls. Several large optimizations already
exist; their presence has not established this target.

## 1. Requirements and acceptance

| ID | Requirement |
| --- | --- |
| R1 | Sustain a 16.667 ms frame cadence during full songs, including dense charts and power-up effects. Menu averages do not qualify. |
| R2 | Render actual 1920x1080 scene pixels at 16:9. For another aspect, record visible dimensions and total source pixels explicitly; 1920x1080 output upscaled from 720p does not meet this requirement. |
| R3 | Preserve song speed, judgement, audio calibration, forward song-clock progression, pause and resume behavior. |
| R4 | Reduce the hardware demand through less work and bounded memory retention. Retain the baseline scene, textures and effects for the primary quality comparison. |
| R5 | Validate physical iPhone hardware separately from Apple Silicon Macs. Retain Intel macOS support without assuming Apple GPU capabilities. |
| R6 | Keep retail and Ultimate, bundled and imported launches, profile recovery, orientation changes and lifecycle handling correct. |

Minimum device models remain open. Use the existing M1 with 8 GiB RAM as an
initial Mac reference, not an established minimum. Record the actual physical
iPhone model, RAM, OS, display rate and sustained thermal behavior before setting
the phone support floor. Intel Mac acceptance requires an actual Intel machine.

Proposed release gates, to freeze before comparative runs:

- Warm gameplay: at least 59.5 distinct displayed game frames/s over each full
  song, p99 presentation interval at most 17.5 ms on a verified 60 Hz cadence,
  at most 0.1% of intervals above 33.4 ms, and no application-caused interval
  above 50 ms in the warmed active-song segment. Average FPS alone cannot pass.
- Correlate completed guest frames with displayed updates. Re-presenting an old
  frame at 60 Hz does not count. On a 120 Hz panel, measure the intended 60 FPS
  delivery cadence and duplicates explicitly rather than counting scanouts.
- Aim for CPU critical-path work and GPU execution each below 13.3 ms at the
  target resolution, leaving roughly 20% headroom. CPU and GPU overlap; do not
  sum overlapping spans to claim total frame cost.
- Repeat at least three alternating baseline/candidate pairs under comparable
  system load. Report all runs and variability, not just the best run.
- Run at least 20 minutes continuously on the target phone, including several
  full songs. Compare early and late frame distributions, thermal state, power
  and physical footprint. A cold-device pass alone does not qualify.
- Require bounded memory through repeated menu/song/profile transitions and no
  allocation failure or OS memory termination. Set per-device resident-memory
  budgets after measuring the active working set and available process headroom;
  the desktop's total RAM is not an iOS process allocation budget.
- Test first-run compilation separately. A warm-cache pass must not hide missing
  geometry or effects while pipelines compile asynchronously.

## 2. Existing evidence and implementation

The current title revision at inspection is `fd18de7`; the SDK is v0.11.0 at
`a3e1d826`. The active patch registration is
[`patches/rexglue-sdk-v0.11/`](../../../patches/rexglue-sdk-v0.11/README.md).
The older directory retains historical patch lineage and is not the production
stack to edit. Source references below describe the 2026-11-26 tree.

| Evidence | Finding and limit |
| --- | --- |
| [2026-11-15 Metal trace](../../engine/vulkan-performance.md#root-cause-follow-up-rendering-cost-and-audio-rewind-2026-11-15), `out/macos/root-cause/song.trace` | About 1,080 encoders in a gameplay command buffer; roughly 500 render passes and 500 blits. Temporary counters attributed most interruptions to guest vertex/index uploads, with 9-20 MB uploaded per frame. Patch 0070 subsequently reduced the sampled pass count to 26. These are historical counts, not today's baseline. |
| [2026-11-26 memory and CPU study](../../engine/vulkan-performance.md#apple-gpu-memory-retention-2026-11-26), `out/memory-optimization/` | Apple memory policies, host-backed GPU mirrors and compilation priority are already implemented. Recent 720p game peaks are about 1.4-1.5 GiB physical footprint, with remaining below-60 gameplay. |
| [Register-localization comparison](../../engine/vulkan-performance.md#guest-register-allocation-and-frame-rate-2026-11-26), `fps-final-comparison.json` | Corrected localized build: 46.2 guest iterations/s versus 43.2 in the control over song seconds 5-45. Runs were sequential under variable system pressure; this suggests a CPU opportunity but does not prove an isolated gain or displayed FPS. Localization is already enabled. |
| Same study, exact upload snapshots | The optional cache eliminates many uploads, but the 64 MiB trial lacks a repeatable FPS result under comparable load. `gpu.vulkan.shared_memory_snapshot_cache_mb` remains 0 by default. |
| Same study, frame memory refresh experiment | Disabling `gpu.xenos.clear_memory_page_state` improved one diagnostic sample, but writable physical spans can bypass notifications. Production refresh remains necessary for coherency. |

The M1 captures include substantial external swap and competing process activity.
They establish specific costs and below-target runs, not a clean hardware ceiling.
Xbox 360 CPU and EDRAM semantics are translated into host operations; nominal
hardware throughput alone does not establish the efficiency of this mapping.

Preserve and measure the existing work instead of implementing it again:

- GPU fixed/integer texture conversion, descriptor caching and redundant-bind
  avoidance already exist in the active equivalents of 0002/0022/0023/0024.
- MoltenVK shader arithmetic lowering, pending-pipeline handling, upload batching
  and asynchronous submissions are owned by 0065/0069/0070/0073.
- Portrait visible-region scissors already exist in 0085. They reduce eligible
  fragments, but leave full-size allocations and resolves.
- Profile-transition cache cleanup, resolve allocation limits, Apple memory
  retention and foreground compilation priority exist in 0095/0097/0098/0099.
- Guest general-register localization and its shared-register ABI correction
  are already active through the manifest and 0100.

## 3. Decisions

| ID | Decision |
| --- | --- |
| D1 | Separate guest CPU work, renderer CPU work, GPU execution, memory pressure and presentation delay before selecting an optimization. Extend existing diagnostics with bounded, opt-in collection. |
| D2 | Establish smooth 720p first as a diagnostic milestone, then pass real 1080p. 720p stability does not finish this plan. |
| D3 | Keep the audio-derived clock and 60 Hz gameplay behavior. Do not obtain higher throughput by skipping required simulation, audio servicing or draws. Above-60 engine work remains in the [framerate expansion plan](2026-10-25-framerate-expansion-plan.md). |
| D4 | Make lossless optimizations first. Lower-resolution effects or reconstruction are separate quality options and cannot be reported as native 1080p acceptance. |
| D5 | Keep each cache's resolution multiplier fixed until an explicit, synchronized cache rebuild. Choose its initial capacity from required profiles, rather than silently reserving for an unused native display tier. |
| D6 | Preserve immutable upload snapshots and CPU/GPU ordering. Unified memory does not authorize binding live writable guest pages directly as the renderer's historical snapshot. |
| D7 | Keep performance changes independently reversible for A/B comparisons. Do not promote diagnostic switches without a correctness gate and repeated benefit above measured run variation. |
| D8 | Retain Vulkan/MoltenVK for the initial work. Consider a native Metal prototype only after profiling attributes a material remaining cost to translation or encoding. |
| D9 | Use logs, files, Instruments and external capture for diagnostics. No new host overlay is part of this plan. Before any game/UI testing, build the changed binary and offer the manual-testing choice required by AGENTS.md; wait for the answer. |
| D10 | Extend the existing patch that owns a correction. Create a patch only for a distinct responsibility, preserve dependent contexts, update lineage and verify isolated full-stack replay. |

## 4. Optimization map

Priority ranks investigation order and potential relevance, not a promised FPS
gain. Effort estimates describe implementation scope: small is a contained change,
medium crosses several components, and large changes renderer architecture.

| ID | Priority and area | Concrete point in the current tree | Candidate change and proof | Scope and effort |
| --- | --- | --- | --- | --- |
| O1 | First: frame attribution | `rexglue-sdk/include/rex/diagnostics/compile_trace.h` retains slow GPU-operation CPU spans at 50 ms and above; `scripts/diagnostics/matched_performance.py` is Windows-only. Neither supplies Apple per-pass GPU timings and displayed-frame distributions. | Add bounded per-frame records, pass/upload counters, signposts and capability-gated asynchronous timestamp readback. Correlate with Metal System Trace and presentation events; collect sub-16.7 ms costs without serializing the GPU. | Shared trace infrastructure plus Apple capture adapter; medium. |
| O2 | High: allocation and resolution policy | `src/settings/resolution_profiles.h:96-114`, `src/settings/boot.cpp:565-571`: macOS computes a fixed scale from native display dimensions; iOS forces 1x and fits source dimensions inside 1920x720. | Select the minimum fixed scale needed by the active interface and gameplay profiles. Log requested, effective, guest, full internal and visible dimensions. A higher future profile either triggers an explicit safe rebuild/restart or remains unavailable. Enable phone 1080p only after the backing-memory gate. | Title policy on Apple, with shared planner regression coverage; medium. |
| O3 | High: scaled resolve backing | `rexglue-sdk/src/gpu/xenos/vulkan/texture_cache.cpp:2142` reserves 512 MiB times scale area on the non-sparse path. 2x2 is a 2 GiB logical range. Patch 0098 imports demand-paged host backing with private fallback; 0097 checks allocation limits. | Measure resident touched pages, GPU writes and fallback behavior. Eliminate excess scale first. If the remaining resolve working set is still excessive, prototype segmented backing/address remapping for used ranges rather than a monolithic reservation. Test every resolve/load address consumer and cross-page range. | Initial policy is Apple/MoltenVK; segmented backing would be a distinct Vulkan responsibility, large and conditional. |
| O4 | High: repeated guest uploads | `rexglue-sdk/src/gpu/xenos/shared_memory.cpp:327` rebuilds, sorts and merges request ranges; Vulkan `UploadRanges` stages bytes and emits GPU copies. Frame refresh intentionally invalidates page validity. | Profile pages requested, changed and reused; CPU comparison/copy time; staged bytes; GPU copy time and hazard rejections. Re-evaluate the existing exact snapshot cache at several bounded sizes. Add a single-range/reusable-scratch fast path if CPU allocation shows up. A later producer/write-watch audit may permit precise invalidation only for proven providers. | Shared range handling and Vulkan upload selection; medium. Preserve D3D12 behavior where unchanged. |
| O5 | High: attachment traffic and pass fragmentation | `rexglue-sdk/src/gpu/xenos/vulkan/render_target_cache.cpp:1424-1510` uses LOAD/STORE attachments; `CreateRenderTarget` at 1766 sizes height from the EDRAM pitch/address space rather than the visible frame. Upload hoisting already reduces interruptions. | Count remaining pass breaks and bytes loaded/stored per attachment. Keep compatible draws together where hazards allow. Prove dead/fully overwritten attachment regions before using CLEAR/DONT_CARE; prototype bounded target extents only with alias/ownership coverage. Reducing render area alone previously failed to improve this title. | Vulkan/MoltenVK first, shared target model only if required; medium to large, high correctness risk. |
| O6 | High when waits dominate: presentation | `rexglue-sdk/src/gpu/xenos/host/vulkan/vulkan_presenter.cpp` records queue/fence spans; command processor has three frames in flight. Async MoltenVK submission is already enabled by 0073. | Attribute drawable acquisition, queue lock, driver encoding, GPU completion and compositor waits separately. Benchmark supported two/three-image swapchains and bounded queue depth with latency as well as throughput. Avoid redundant paints/copies only when ownership and UI redraw requirements allow. | Apple presentation policy, preserve lifecycle fixes 0078/0080/0086; medium. |
| O7 | Medium, concrete CPU candidate: ARM64 VMX fused math | `rexglue-sdk/include/rex/ppc/fpu.h:1294,1333` enables the fused path only for `__FMA__`. Apple ARM64 defines `__ARM_FEATURE_FMA`, so this branch uses `MultiplyAddWide`; SIMDe already has a NEON fused implementation. | Add a native ARM64 capability path without globally defining an x86 feature macro. Preserve guest NaN, signed zero, tiny-result handling, FP control and NJ=0 behavior. Require differential reference tests and an executed hotspot profile before claiming gameplay benefit. | Generic ARM64 PPC helper, all ARM64 hosts/backends; small to medium. |
| O8 | Medium: remaining CPU hot paths and build policy | Manifest localization is already on. Mac Release uses `-O3 -DNDEBUG`, `-march=armv8-a`; local cache has Tracy on, iOS preset turns it off. No LTO/PGO is present in inspected command lines. | Profile executed PPC helpers, endian loads, dispatch, memory operations and app hooks. Compare shipping diagnostics off/on; benchmark ThinLTO, representative PGO and portable Apple CPU tuning independently. Track binary size, instruction-cache behavior, startup, link memory and compile cost. Replace only demonstrated hot guest functions with equivalent host implementations. | Apple build experiments; helper changes need shared architecture coverage; medium. |
| O9 | Medium: audio and worker scheduling | `rexglue-sdk/src/base/threading_posix.cpp:991` maps creation priority to Darwin QoS, but runtime `ApplyThreadPriority` at 930 still calls `pthread_setschedparam(SCHED_FIFO)`. APU/XMA workers use runtime priority requests. | Measure effective native QoS, return codes, wake latency and audio deadline misses. Extend the owning priority policy to runtime changes where appropriate. Reserve headroom for audio/render threads; do not increase all worker priorities or pin cores blindly. | Darwin scheduling, retain other POSIX policy; small to medium. |
| O10 | Medium when GPU execution dominates: shader execution | `rexglue-sdk/src/gpu/xenos/pipeline/shader/spirv_translator_alu.cpp` and `_fetch.cpp`; precise MoltenVK arithmetic lowering already exists in 0065. | Inspect the hottest generated MSL and GPU counters for register pressure, repeated conversions, dynamic indexing and redundant fetches. Specialize proven static state and remove dead calculations while preserving rounding, gradients and guest formats. Revalidate derivatives, LOD and blend behavior. | MoltenVK-specific lowering when necessary; medium to large. No blanket fast-math change. |
| O11 | Medium when cache churn dominates: textures and preparation | `rexglue-sdk/src/gpu/xenos/vulkan/texture_cache.cpp:2518` selects RGBA fallbacks when BC formats lack required features. Apple texture budgets are already smaller in 0098; preparation and foreground priority already exist. | Record actual format support, decoded working set, eviction/reload rates, helper misses and late pipeline creation. Tune budgets for measured reuse. Retain native compression where supported. Consider a validated derived texture cache for immutable unsupported formats only if expansion is material; any lossy recompression needs separate visual acceptance. | Capability-gated Apple texture policy; medium, derived asset caching large. |
| O12 | Later: quality policy and native backend experiment | Existing separate interface/gameplay profiles and portrait clipping provide integration points. The renderer currently maps Xenos semantics through Vulkan to Metal. | Once fixed-resolution costs are known, optionally lower only proven expensive effects or reconstruct a lower-resolution scene with a sharp interface. Separately replay a representative captured workload through a minimal Metal prototype if translation remains dominant. Compare identical pixels and synchronization requirements before considering a backend project. | Optional title quality tier; native Metal is a distinct large project. Neither substitutes for R2 acceptance. |

## 5. Implementation sequence

### Step 1 Establish a reproducible Apple baseline

Deliver an attach-only Apple collector and report format alongside the Windows
matched-performance tooling. Reuse common analysis/schema where practical; keep
platform collectors separate. Preserve original logs and raw traces.

Record executable/module hashes, source and patch identity, compiler options,
MoltenVK identity, content variant, song/chart, camera segment, quality settings,
effective resolutions, MSAA, cache warmth, refresh and queue policy. Capture
competing CPU load, swap/page activity, memory pressure, power mode and thermal
state. Pause unrelated local builds/simulators only with appropriate authority;
do not terminate unrelated workloads to manufacture a result.

Use a light collector for timed runs and separate short instrumented captures for
attribution. Retain instrumentation overhead controls. Add frame/submit IDs and
bounded in-memory storage; flush after the measured segment rather than logging
every draw synchronously. Timestamp queries must be supported, availability-polled
and read after completion without a new per-frame wait. Unsupported devices use
external Metal traces rather than fabricated GPU timings. MoltenVK does not
support Vulkan pipeline-statistics queries; use available GPU counters instead.

Compare the same scene at 540p, 720p, 900p where supported, and 1080p where safe,
holding aspect and quality constant. Log the real multiplier and full source size
for every run. Pixel-sensitive GPU time suggests fill/bandwidth work; flat CPU
cost suggests guest/encoding work; idle GPU plus long waits suggests scheduling or
presentation. These are diagnostic clues, to confirm with traces.

Gate: repeated clean controls plus a ranked frame-cost breakdown for each device.
If the phone cannot reach 1080p through the existing policy, label that arm
unavailable until Step 3; do not bypass the memory limits for a benchmark.

### Step 2 Land contained CPU and scheduling candidates

Implement O7's ARM64 FMA support and O9's runtime QoS correction as independent
changes, conditional on native correctness and scheduling tests. Benchmark them
separately before combining. Add a shipping Mac profiler policy experiment and
retain a profiling build with symbols and explicit diagnostics.

Run the PPC multiply-add reference vectors and randomized bit-pattern differential
checks on native ARM64, including denormals, NaNs, infinities, cancellation and
both NJ modes. Extend native worker tests to observe effective QoS after runtime
priority changes, and check audio underruns under CPU contention.

Gate: no semantic/audio regression and a repeated CPU/frame-tail benefit, or keep
the experiment out of production. Release is already optimized; compiler flag
changes require evidence rather than an assumed large gain.

### Step 3 Make real 1080p allocation affordable and reachable

Implement O2's profile-capacity policy. For example, a 2560x1600 native display
currently selects 3x even if both selected profiles are 1920x1080; those profiles
only need 2x with a 960x540 guest base. The unused scale changes a non-sparse
resolve range from 2 GiB to 4.5 GiB. These are logical sizes, not measured resident
RAM savings with host-backed pages.

Prove host-backed allocation behavior and graceful private-fallback handling at
the target phone's limits before offering 2x. Do not simply remove the iOS 1x
guard or ask the title to allocate a native 1080-high guest frame: that guest
allocation path has a documented corruption history in `src/hooks/render_settings.cpp`.
Use the verified lower guest base and backend multiplier.

Cover interface/gameplay combinations, saved native requests, display changes,
orientation, crop dimensions, explicit overrides, startup recovery and allocation
failure. If dense scaled backing still exceeds the budget, isolate O3's segmented
resolve prototype behind a separate decision with shader/addressing tests.

Gate: correct real 1080p image, bounded resident memory and a reliable lower-tier
fallback. No stable-FPS claim until the timing gates pass.

### Step 4 Reduce repeated copies and attachment traffic

Use O4/O5 measurements to choose the next bottleneck. Start with reusable request
scratch and the single-range path if they execute materially. Re-run the existing
snapshot-cache comparison at 0/16/32/64 MiB where budgets permit, counting CPU
comparison cost, avoided copies, resident bytes and GPU pass interruptions.

Audit writable provider APIs and aliases before replacing broad frame refresh.
Extend the independent queued-copy oracle for any upload-selection change and
retain late CPU writes, overlapping GPU resolves, invalidation and partial-failure
rollback coverage. Compare final GPU values after delayed execution.

Then reduce measured pass boundaries or attachment loads/stores with ownership
proof. Test tiled addresses, partial clears, MSAA, depth/stencil, render-to-texture,
memory exports, occlusion side effects, profile transitions and aliased targets.
Shrinking cache allocations or using DONT_CARE must never discard guest-visible
EDRAM contents that a later draw or resolve reads.

Gate: lower measured bytes/pass cost and better frame tails with equivalent images
and state. A cache hit count alone is insufficient.

### Step 5 Correct the remaining presentation and compilation stalls

Apply O6 only to a demonstrated remaining wait path. Preserve queue external
synchronization, completion-driven retirement, iOS background draining and drawable
retention fixes. Evaluate throughput together with input-to-display latency and
audio deadlines. Moving a wait to another thread is insufficient if displayed
frame delivery is unchanged.

Use O11 to close repeated late pipeline/helper misses for representative content.
Retain cache invalidation by device, driver, translator and render state. Exercise
cold setup and cancellation as well as warm playback; do not let skipped pending
draws make the performance result look better.

Gate: target presentation intervals with complete rendered content and no
pause/resume, resize or shutdown regression.

### Step 6 Optimize the measured shader and texture hot spots

Apply O10/O11 only to top contributors remaining in GPU captures. Preserve the
existing signed-zero and gradient fixes. Expand native vertex/fragment-gradient
fixtures and image comparisons at supported sample counts. Profile cache-budget
changes across several songs so smaller memory retention does not produce a
reupload loop. Evaluate compiler/PGO experiments from O8 independently.

Gate: repeatable lower GPU or CPU critical-path time at the same image quality,
with memory and frame-tail results recorded separately.

### Step 7 Validate sustained operation and publish device tiers

Complete full songs on the reference Mac, selected minimum phone and a newer
phone. Include retail/Ultimate, bundled/imported content, a dense custom chart,
effects-heavy sections, profile transitions, orientation, pause, background/resume,
song restart and a long session. Check calibration and chart-time input outcomes;
an idle or autoplay run does not verify real input judgement.

Publish measured 720p/900p/1080p/native tiers by actual device evidence. Preserve
explicit settings and recovery behavior. Any optional quality mode uses the
existing authored settings pages, six-entry maximum, concise names and captions
within the current layout budgets. Automatic dynamic resolution, if later added,
must use frame-time hysteresis, bounded changes and stable resource lifetimes;
it remains separate from fixed-1080p acceptance.

If the target still fails and a dominant translation/encoding cost is isolated,
write a separate native Metal implementation proposal from O12's workload spike.
Preserve Xenos EDRAM, formats, shader arithmetic, resolves and presentation behavior
in the comparison; a triangle demo is not a meaningful backend benchmark.

## 6. Patch ownership and platform validation

| Responsibility | Owner to inspect and extend |
| --- | --- |
| CPU VMX arithmetic | Audit existing PPC/FPU patches and upstream tests first. A native ARM64 fused path is distinct from 0065's GPU shader lowering and 0100's generated-call ABI. |
| Darwin runtime QoS | 0099's existing native QoS policy; coordinate with 0031's audio-worker priority requests. |
| Upload selection and lifetime | 0101 for snapshot selection, 0070 for upload ordering; shared-provider coherency is a separate boundary. |
| Memory retention and limits | 0098 for backing/retirement policy, 0097 for allocation limits, 0095 for profile-resize cleanup. Segmented address mapping is a distinct responsibility if needed. |
| Shader lowering and preparation | 0065 for MoltenVK arithmetic; 0021/0028/0069/0099 retain lifecycle, persistence, pending draws and driver priority respectively. |
| Presentation and lifecycle | 0073 for queue policy, with 0076/0077/0078/0080/0086 diagnostics, sizing, lifecycle and recovery contexts preserved. |

Define scope before each patch. Apple GPU-specific behavior must be capability
gated and must not silently apply to Intel/discrete Mac GPUs. ARM64 CPU helpers
also affect Windows/Linux ARM64 and require the appropriate shared tests. Vulkan
changes affect Windows/Linux/macOS/iOS and available Android configurations;
D3D12 must retain its defaults unless a shared change intentionally affects it.
Record untested architectures and backends rather than inferring parity.

Before asking for runtime acceptance, build the changed target. Native Apple
commands are `cmake --build --preset mac-arm64-release --target rb_blitz` and
`cmake --build --preset ios-arm64-release --target rb_blitz`; also build the
importer preset when its launch path is affected. On Windows, use
`cmake --build --preset win-amd64-release` and the required payload test runner.
Choose focused tests from the change: existing Apple allocator/worker priority,
upload hazards/snapshots, pipeline priority/completion, graphics/profile planning,
present resize, lifecycle and native vertex/fragment-gradient fixtures. Discover
current CTest names before running them; a successful build is not device proof.

Verify isolated full-stack replay, inspect preserved live SDK drift before
applying patches, run documentation checks and `git diff --check`, and attach
build/test identities to each comparison. Update the performance study with raw
evidence paths and the settings contracts only when behavior actually changes.

## 7. Evidence collection and remaining questions

Store future runs under
`out/apple-performance/<build>/<device>/<variant>/<resolution>/<run>/` with
`manifest.json`, per-frame data, memory/thermal summaries, launch arguments,
raw traces and image correctness artifacts. Run IDs identify alternating
baseline/candidate order and the common song-clock window. Keep diagnostic and
shipping timing runs separate.

The main unknowns are the minimum iPhone/Intel Mac models; the current clean
CPU/GPU/presentation split; the resident 1080p resolve working set on a phone;
and the magnitude of the ARM64 arithmetic and runtime QoS candidates in gameplay.
None requires guessing an FPS gain. Steps 1-3 resolve those questions and produce
the first concrete feasibility checkpoint for the selected devices.

Profiling references, checked 2026-11-26:

- [Apple CPU and GPU performance analysis](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app)
  describes correlating threading, system calls and Metal execution in Instruments.
- [Apple GPU memory bandwidth guidance](https://developer.apple.com/documentation/xcode/measuring-the-gpus-use-of-memory-bandwidth)
  supports measuring attachment traffic and loading/storing only required data.
- [MoltenVK 1.4.1 runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/Docs/MoltenVK_Runtime_UserGuide.md)
  covers Metal capture, native pipeline caching, swapchain image tradeoffs,
  timestamp limitations and unsupported pipeline-statistics queries.

Planning validation: no runtime source or settings changed, and no game/UI was
launched. The pre-change documentation checker reports 18 existing errors and
49 warnings, primarily stale plan links. Those existing errors are outside this
plan's scope. The post-change checker scans 818 files and reports the same
18 errors and 49 warnings, with none in the new plan. `git diff --check` passes;
the new file also has no trailing whitespace. All seven implementation steps
remain pending. No binary rebuild is needed for this documentation-only change.
