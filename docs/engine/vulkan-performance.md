# Vulkan performance investigation (2026-10-24)

A warm shader/pipeline cache does not remove texture transfers, command submission,
barriers, shader execution or presentation costs. Backend parity is not guaranteed by
ReXGlue. The existing captures identify this machine as a GeForce GTX 970; the
backend/driver/hardware combination can matter, but those captures do not establish
the cause of every steady-state performance difference.

## Avoidable texture round trip

SDK patch 0002 added 32-bit fixed-point/integer texture support for the highway.
Vulkan's `ConvertTextureRange` originally read the CPU mirror of guest memory,
converted every component on the CPU, and uploaded the result through a mapped
host allocation. To make GPU resolves visible to that conversion, the application
automatically enabled `vulkan_readback_resolve`. This applied readback to all
resolves, including downscaling when internal resolution scaling was enabled.
The legacy flag selects delayed `fast` readback, with synchronous waits on new
readback slots. It is inaccurate to describe every warmed resolve as a full wait.
The per-resolve copies, CPU work and uploads remain after shader warm-up.

SDK patch **0022-vulkan-gpu-texture-conversion.patch** replaces that conversion
with a compute dispatch reading GPU shared memory, including current GPU-written
resolves. It preserves the guest layout, endian conversion, signedness and
integer/normalized number semantics before the existing texture-load shader.
This removes the conversion's dependence on CPU resolve readback and avoids
converting stale previous-frame CPU data. The application no longer automatically
enables readback on Vulkan. Explicit user readback overrides retain their behavior.
D3D12 is unchanged.

The compute pipeline is compiled/created during device initialization, uses the
texture-load pipeline layout, and is included in the helper inventory. GPU buffers
are reused only after their submissions complete and retired through the existing
cache-clear/shutdown lifecycle. Shared-memory barriers precede conversion; a
compute-write to compute-read barrier precedes texture loading. Large ranges use
two-dimensional dispatch within Vulkan's guaranteed minimum workgroup limits.
Non-float32 textures still use the existing unscaled-resolve restriction; this
change does not claim new resolution-scaling support.

Vulkan's documentation describes the cost of small submissions and
[GPU/CPU synchronization](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html).
That supports the optimization direction; it is not a measured FPS result for this game.

## Repeated texture descriptor updates

The owner reports that GPU conversion improved Vulkan performance but a gap to
D3D12 remains. A second code inspection found that `WriteTransientTextureBindings`
allocated/recycled a descriptor set and rewrote its texture/sampler descriptors
whenever the bindings changed, including a return to identical bindings already
used within the same command submission.

SDK patch **0023-vulkan-texture-descriptor-cache.patch** adds a bounded cache of
immutable descriptor sets. Its key includes the descriptor-set layout, ordered
image views and their layouts, and ordered sampler handles. It ignores padding and
fields unused by each descriptor type. Lookups borrow spans and allocate no memory;
only new cached keys allocate storage. It stores at most 1,024 combinations per
submission; reaching that limit uses the existing allocation/update path.

Hits reuse the existing set without allocation or `vkUpdateDescriptorSets` writes.
The caller still binds the returned set, including when zero descriptor writes were
needed. Misses retain the existing transient allocator and frame-based retirement.
The cache clears at every submission, frame and descriptor-pool reset. Limiting it
to a submission also prevents stale sampler-handle reuse after overflow retirement;
samplers used in the open submission cannot be evicted before its completion.
Likewise, completion-driven texture eviction cannot retire textures used by the
open submission. Texture clearing retains the existing queue-completion gate.
No resource contents, shaders or rendering quality change.

This follows the Vulkan sample's
[descriptor caching approach](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/descriptor_management/README.adoc).
The actual improvement depends on repeated bindings and the current CPU/GPU bottleneck;
the change does not establish performance parity with D3D12. For a controlled
comparison, launch with `--vulkan_texture_descriptor_cache=false` to disable this
optimization; it is enabled by default. Keep pipeline caches and graphics settings
the same between runs.

The host test exercises the production key/cache, including changed layouts,
signed/swizzled views, image layouts, sampler changes, ordering/counts, ignored
fields, owned key contents, cache bounds and reset. A synthetic 10,000-request
alternating-binding sequence requires two new sets and 9,998 reuse hits. Those
counts validate reuse logic; they are not a gameplay timing measurement.

The Renderer option's dynamic capability list now spells the Vulkan choice
`Vulkan`, matching its static label. The persisted token stays `vulkan`.

After this second optimization, the release build and **58/58 CTest tests** passed,
including the renderer-label checks, the 10,025 descriptor-cache checks and the
existing headless GPU conversion test. The full SDK patch audit reports 63 accounted
files, zero unexpected/untracked files and no patch failures. No game/UI automation
was performed; the owner's manual gameplay comparison remains the FPS check.

## Skip redundant descriptor binds and use nonblocking fence queries

The owner reports a substantial improvement with patch 0023. Inspection of the
remaining path shows that `UpdateBindings` still clears texture descriptor value
flags on every draw, then clears their bound flags before consulting the cache.
The cache avoids descriptor allocation/writes, but the same set is still rebound
on consecutive draws.

SDK patch **0024-vulkan-descriptor-binding-reuse.patch** postpones bound-state
invalidation until the actual descriptor set handles are known. Already bound,
valid, unchanged sets remain bound; changed sets still bind normally. This follows
[Vulkan's descriptor binding lifetime and layout compatibility rules](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-compatibility).
The pipeline switch path previously calculated a compatible layout prefix but
never applied it to the bound mask. It now applies that prefix, so a changed
vertex layout also invalidates the pixel binding even when the pixel set handle
is unchanged. New command buffers and external rendering still invalidate their
bindings. No shader work, synchronization barrier or required binding is removed.

The production state helper is covered by every combination of bound/valid/changed
bits for four descriptor sets, plus prefix changes, external invalidation and
reset. In a synthetic 10,000-draw sequence with identical sets, only the first
iteration requires a bind batch; that checks recording logic, not measured game FPS.

The same patch replaces zero-timeout `vkWaitForFences` polling with
[`vkGetFenceStatus`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetFenceStatus.html).
This is a nonblocking query, with the existing not-ready and device-loss handling.
Blocking waits for frame limits, readbacks or shutdown are unchanged. Its individual
performance benefit has not been measured.

Verification also found an incremental-build staging defect: the SDK copied its
runtime-loaded GPU plugin beside the game only after an executable link. A
renderer-only rebuild could therefore leave the game loading the previous DLL.
The parent build now stages the plugin through a dependency of `rb_blitz`, using
`copy_if_different` on every game build. This also runs when explicitly building
the game target. Source and staged plugin hashes are checked before handoff.

Validation: the release build and **58/58 CTest tests** passed. The extended
descriptor test reports **14,130 checks, zero failures**; the SDK patch audit
reports 64 accounted files, no unexpected/untracked files and no failures.
An explicit incremental `rb_blitz` build ran staging without relinking the
executable, and source/staged renderer SHA-256 hashes matched. Gameplay FPS for
these latest changes remains unmeasured; the owner continues manual testing.

## Validation and limits

`vulkan_texture_conversion` is a Windows headless GPU test. It compiles the exact
production GLSL with the bundled glslang, creates a compute pipeline, and compares
2,100,304 output words against an independent CPU reference on the GTX 970.
It covers all four endian modes, both signednesses, normalized/integer modes,
boundary/random values, partial workgroups, forced multi-row dispatch and a GPU
write missing from the CPU reference data. Guard words check dispatch bounds.
Machines without a usable Vulkan device report the test as skipped, not passed.
This test opens no window and does not navigate the game.

The release build passed. The existing 56-test CTest suite passed, including the
GPU test; the subsequently added `sdk_patch_stack` test also passed. That fixture
checks fresh application, repeated application/checking, and rejection of unrelated
edits or missing changes even when a path disappears from git status. The real SDK
patch audit reports 61 accounted files and zero unexpected or untracked files.
The local build retains the existing non-frozen Clang 19.1.7/CMake 4.4.0 warning.

Build and run:

```powershell
cmake --build --preset win-amd64-release
ctest --test-dir out/build/win-amd64-release -R '^vulkan_texture_conversion$' -V
```

The test validates conversion arithmetic and a real GPU source; it does not exercise
the renderer's texture-cache lifecycle, game visuals, synchronization under gameplay,
or an FPS improvement. Gameplay comparison remains pending. Use the same song,
resolution, AA, filtering, VSync and power-ups on both backends; compare repeated
warm runs after any initial shader/pipeline work. Check the highway, notes and
effects through a song ending. An old explicit `readback_resolve` or
`vulkan_readback_resolve` override can retain readback overhead: `readback_resolve =
'none'` explicitly disables it for a controlled comparison. Do not delete the cache.

The shader-cache preparation/native persistence work remains separate. This change
does not implement P4–P9 or establish full gameplay pipeline coverage.


## macOS M1 investigation (2026-11-15)

The owner reported clean graphics with heavy gameplay slowdown after the Mac
shader-cache fixes. The existing `out/macos/cache-portability-game.log` prepared
409/409 supported recipes successfully, then recorded 761 guest frame intervals
in 71.506 seconds of `active_song` (10.64 intervals/s). Seven additional real
pipelines took 826 ms of summed worker time; seven synchronous placeholders took
1,079 ms, including a 600 ms call. One first-use transfer helper took 223 ms.
Those calls explain some individual hitches, not the sustained frame rate.

Native probes used the Apple M1's seven-core GPU, 8 GiB shared RAM, the Release
arm64 build and MoltenVK 1.4.1. The owner's saved Auto selected a 1152x720 guest
base at 2x, rendering 2304x1440. Normal gameplay samples showed 92–97% GPU device
utilization and about 3.8–4.1 GB of graphics memory in use through IORegistry.
`sample` places much of the UI thread inside `CAMetalLayer nextDrawable`, reached
from `vkQueueSubmit`, while the command processor waits for its queue or fences.
These are CPU stacks and driver counters, not GPU timestamps or a per-pass GPU
profile. They identify GPU/presentation backpressure in the current backend; they
do not establish the M1's best achievable performance or justify a hardware upgrade.
`pmset` reports Low Power Mode disabled for both battery and AC, with no recorded
thermal or performance warning. This is a status check, not a sustained thermal profile.

The One Week route was repeated with input injected through SDL events by a local
ignored diagnostic harness. No host overlay was added. Logs and stack samples are
under `out/macos/performance/`:

| Capture | Change from baseline | Guest frame intervals | Active interval time | Intervals/s |
| --- | --- | ---: | ---: | ---: |
| `baseline-r3.log` | Warm caches, normal settings | 415 | 46.117 s | 9.00 |
| `bounds.log` | Prototype union of draw scissors for attachment render areas | 254 | 28.221 s | 9.00 |
| `async.log` | `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` | 170 | 18.517 s | 9.18 |
| `scale1.log` | `--resolution_scale=1` | 792 | 70.238 s | 11.28 |
| `native2x.log` | `--native_2x_msaa=true` | 255 | 27.307 s | 9.34 |

These are differently sized windows including the input-enabled song intro;
they are diagnostic comparisons, not a controlled benchmark or displayed-FPS
measurement. Tracing was enabled for that table. A separate final warm run with
`--shader_compile_trace=false --probe_framerate=true` still reports roughly ten
completed guest draws/presents per second in song (9.02 draws/s over 56 windows
in `trace-off.log`); the once-per-
second counter avoids the detailed trace. IORegistry still reports 96% GPU use,
and `trace-off-sample.txt` still shows drawable acquisition and queue waits.
Disabling the detailed trace does not restore full speed. Lowering the draw scale also restores the
normal guest base/aspect because the explicit override bypasses the saved screen
plan. It helped modestly and did not approach full speed. Native two-sample
attachments and asynchronous queue submission gave no substantial improvement.
The render-area prototype was removed. An FSI run (`fsi.log`) visibly corrupted
the title/offline screens, so it was stopped without a gameplay performance claim.
No experimental resolution, MSAA, queue or render-target settings were saved.

SDK patch 0068 retains the useful bounded Vulkan attribution spans. The Vulkan
specification permits restricting attachment work through
[render areas](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassBeginInfo.html),
but that alone did not improve this workload in the prototype. At this stage a real
GPU capture was still needed. The follow-up below records that capture and the
resulting optimizations; the original measurements did not prove full-speed play
was impossible on this Mac.

SDK patch 0069 fixes the confirmed async-path stall: MoltenVK no longer creates a
throwaway placeholder pipeline synchronously before queueing the real one.
`IssueDraw` already skipped placeholders, so a null pending entry follows the
same visible fallback. Its pending flag prevents repeated draws from compiling
or enqueueing it again; real handles and layouts still publish through the
existing submission-boundary completion queue. Entries left unready by cancelled
preparation use this route too, rather than falling back to a synchronous driver
call. Startup preparation and explicit synchronous mode retain their waits.
A first-use draw may still be absent while its real pipeline compiles; shader
translation, driver work on workers and first-use helper creation still have costs.
This change targets hitches and does not claim to fix the sustained low frame rate.

Validation uses the native game build, the existing pipeline completion/preload,
preparation, native-cache, trace and gradient regressions, and isolated cold-cache
runs that press the authored Skip control before entering One Week. Original
player cache files are retained; diagnostic caches live under the ignored output
directory. A full test-target build also exposed an unrelated existing macOS
compile failure in `tests/path_policy_tests.cpp:122` (native narrow path compared
with a wide literal); the focused targets build independently.


The final cold-cache/Skip regression (`pending-cold-r2.log`) records 90 gameplay
pipeline creations on six runtime worker threads, none on the GPU command thread,
and zero placeholder creations. The earlier intermediate cold run had created
103 retained, unready recipes synchronously on the GPU thread; this observation
led to the second half of patch 0069. Both runs rendered the One Week highway,
notes and city successfully. The final run covered 58.123 seconds of the
input-enabled song phase. It still averaged 9.08 guest frame intervals/s; the
worst gap was 327 ms. Those figures are not an FPS improvement claim and do not
attribute every remaining long gap. Live preparing-state Skip reached the main
menu and song successfully in both isolated runs.

The final game target builds with `cmake --build --preset mac-arm64-release
--target rb_blitz`. Fourteen focused non-UI tests pass, including the production
fragment-gradient GPU regression at 1x/2x/4x. All 68 SDK patches replay with no
drift. Build/test/audit outputs are `pending-final-build.log`, `final-tests.log`
and `patch-check.log` in the performance output directory. Windows and Intel
Mac regression runs are unavailable on this host.


### Root-cause follow-up: rendering cost and audio rewind (2026-11-15)

The owner described repeated audio and backward highway movement, then confirmed
forward playback with the combined fix. The guest song clock comes from the audio
stream; the clock filter was observing a real ring-position regression. The fix
keeps that audio-derived clock and does not clamp or replace song time.

`src/hooks/audio_stream.cpp` preserves the guest Vorbis decoder, MOGG decryption,
stream copy completion and pause/seek routes. It changes three related limits on
Mac by default (`audio_stream_catchup`, restart-scoped):

- A compressed input request reserves and reads 128 KiB instead of 16 KiB at the
  verified `VorbisReader` call sites (`0x827687A0` allocation return and
  `0x827687C4` read return). The larger `ArkFile::ReadAsync` request is coupled to
  the exact enlarged buffer; unrelated readers retain their sizes.
- `StandardStream::PollStream` (`0x823E61E0`) forwards at most 2,048 samples per
  original poll. Low-buffer catch-up uses at most four extra original polls, a
  four-millisecond budget and an immediate stop when neither PCM nor a copy
  progresses. It stays on the owning guest thread and restores its FP control.
- `StreamReceiver::Poll` (`0x823E9828`) originally starts a refill only at an exact
  half-ring block distance. A long render frame can skip that block. Catch-up
  records the missed crossing while retaining the original queued-PCM and
  outstanding-copy checks. The original DMA may otherwise replay old PCM and
  make the audio-derived gameplay clock wrap backward.

The Metal System Trace in `out/macos/root-cause/song.trace` found about 1,080
encoders in a gameplay command buffer, including roughly 500 render passes and
500 blits. Temporary counters in `combined.log` found no memory exports or index
snapshots in the sampled frames: guest vertex/index uploads caused most pass
interruptions, with roughly 300–500 guest render-pass starts and 9–20 MB of
uploads per frame. These counters were removed from production.

SDK patch **0070-moltenvk-shared-memory-upload-batching.patch** batches independent
staging snapshots before the current deferred render pass. Conservative 4 KiB
read/write tracking rejects aliases with earlier draws or uploads; memory-export
and untracked helper work retain the ordered path. Index/vertex cache hits still
contribute reads. Upload snapshots retain the existing submission-fence lifetime,
and transfer barriers bracket the batch. MoltenVK is the enabled path; other
Vulkan drivers retain their ordering. The sampled gameplay pass count fell to 26.

SDK patch **0071-moltenvk-vertex-arithmetic.patch** extends the existing explicit
Fma arithmetic lowering to MoltenVK vertex shaders. The pinned SPIRV-Cross lowers
ordinary `NoContraction` arithmetic to `[[clang::optnone]]` Metal helper calls.
The replacement retains separate operation rounding and signed zero without
those calls. Other drivers keep the previous arithmetic; shader modification
version 14 invalidates translations created under the old lowering.

Observed One Week runs on the same M1/8 GiB host:

| Capture under `out/macos/root-cause/` | Effective path | Completed guest draws/s | Clock result |
| --- | --- | ---: | --- |
| `combined.log` | Warm baseline at Auto 2x | 9.24 | 84 backward one-second windows in the measured sample |
| `hoist.log` | Upload batching at Auto 2x | 17.03 | Rewinds remained |
| `catchup-all.log` | Combined initial audio fix and batching | 14.03 | No backward windows; later near-freeze |
| `vertex-fma.log` | Bounded audio fix, batching and vertex arithmetic at Auto 2x | 26.27 | No backward windows; reached the 172-second song ending |
| `native1x-final.log` | Final arithmetic/audio path, explicit 1x override | About 60 | Reached the song ending; owner reported the best run but visible stretching |
| `aspect-bars-fixed` run | Integrated Auto aspect/1x, SDK 0072 | 50.63 | No backward windows; reached song ending with correct 16:10 presentation |

Auto 2x used a 1152x720 guest base and 2304x1440 internal rendering. The explicit
1x override bypassed the saved guest-size plan and used a 1280x720 guest on the
16:10 display. It demonstrates the rendering-cost difference but is not an
aspect-correct acceptance run. The owner's resulting
[desktop render-settings plan](../plans/finished/2026-11-15-desktop-render-settings-plan.md)
separates aspect from scale so reducing work does not change the guest proportions.
These are observed runs across different portions of the song, not a matched-scene
benchmark or physical scanout measurement. No graphics preferences were saved by
the diagnostic command-line overrides.

The first integrated Aspect Ratio/Render Scale Auto/1x run did not pass aspect
acceptance: the owner saw horizontal bars and closed before the menu
(`aspect-auto-final.log`). Its boot log reported the intended 1152x720 guest and
video-mode cvars, but did not record the actual SDK video-mode query. Source
inspection found that explicit height 720 compared equal to the default, so
`GetConfiguredVideoModeHeight` treated it as unset and substituted the Retina
window's 360 logical pixels, clamped to 480. Width remained 1152, producing a
2.4:1 queried aspect instead of 16:10. SDK patch
`0072-explicit-native-video-mode.patch` now preserves the explicit cvar source in
the kernel/SDL dimension predicate; `video_mode_intent` exercises that predicate
with the real cvar registry. The updated audit replayed 71 patches with zero
drift. The current Release build (`aspect-bars-build.log`) and all nine affected
CTest registrations (`aspect-bars-tests.log`) passed.

The rebuilt `aspect-bars-fixed` run passed the aspect check on the 16:10 display:
the pre-A, gameplay and results captures fill the screen with round interface
elements and no horizontal bars. The gameplay capture is named
`aspect-bars-fixed-menu.png`; the other two are `-prea.png` and `-results.png`.
The metrics JSON reports raw song time 172043.765625 ms, 176 measured windows,
zero backward windows, 50.63 weighted completed guest draws/s and a slowest
window of 10.06/s. This is full-song, aspect-correct acceptance, not locked 60 FPS.
`aspect-bars-fixed-vmmap.txt` reports a 2.2 GiB current/peak physical footprint,
versus the earlier 2x snapshot's 4.4 GiB peak. At the time, the system still had
about 12.5 GB swap, `photolibraryd` near 90% CPU and another project's `dotnet`
build near 32%. `aspect-bars-fixed-sample.txt` includes presenter-fence/drawable
waits. That competition prevents treating the rates as a clean scale benchmark
or proving that all frame-pacing limits are fixed. Authored settings
save/restart/relaunch remains unverified: the owner closed the final UI-focused
launch at the offline prompt, then explicitly stopped further game/UI launches.
The desktop settings delivery record names that remaining manual gate.

A prior near-freeze coincided with heavy system paging and about 14 GB of swap,
while another project's builds were also active. A game snapshot reported a
4.4 GB peak physical footprint and about 3.1 GB of graphics allocations; the 2x
scaled-resolve address space alone is 2 GiB on MoltenVK. This supports memory
pressure as a contributor, not a proof that one external process caused the
freeze. The later complete 2x song did not reproduce that collapse; performance
under arbitrary competing workloads remains unproven.

The native Release build and the audio/hazard host tests pass. The upload-hazard
fixture reports **3,627 checks**, including aliasing, repeated writes, boundary
ranges and an independent draw-value replay oracle. The expanded native GPU
fixture checks vertex and fragment arithmetic, signed zero and fragment gradients
at supported 1x/2x/4x sample counts. Its sandboxed run skipped because Metal was
unavailable; the same final binary **passed with device access** in 0.22 seconds
(`vertex-gradient-escalated.log`). The SDK audit reports **70 patches replayed,
zero files updated**. Windows, Intel Mac, other songs, installed payloads and
physical-controller acceptance remain unverified for this follow-up.

### Native-scale frame pacing and queue serialization (2026-11-15)

The owner reports that the full 720p/16:10 song remained visibly uneven and that
Vulkan on a base ROG Ally under Windows felt smoother. This is useful comparative
feedback, not a matched hardware benchmark or proof of an M1 horsepower limit.
The owner clarified that further launches risk losing the two retained sessions.
No game/UI was launched during this follow-up. Fifteen existing evidence files,
including the complete song and following startup, were copied into
`out/macos/preserved-stutter-2026-11-15/`; `manifest.json` records original/copy
paths, sizes and SHA-256 hashes. Both sources and copies were verified unchanged.

The detailed trace confirms a pacing problem hidden by the average rate:

| Existing capture | Guest frame intervals | Intervals above 50 ms | Worst guest interval |
| --- | ---: | ---: | ---: |
| `native1x-final.log`, original 16:9 guest with stretched presentation | 10,204 | 2 | 85.0 ms |
| `aspect-bars-fixed.log`, correct 1152x720 guest/16:10 presentation | 9,067 | 167 | 2,017.2 ms |

These totals come from the active-song trace summaries, not physical display
scanout. The full-song raw clock reaches 172,043.8 ms with no backward windows.
The second run includes substantial external memory/CPU pressure, so the table
does not isolate an aspect-ratio performance regression.

Of its 63 recorded slow GPU-operation CPU spans, 59 are guest queue submission;
that category reaches 1,837.9 ms. Presenter submission reaches 1,782.5 ms and
presenter fence waiting reaches 1,504.0 ms. These spans overlap; summing them would
double-count the same stalls. Seven pipeline creations total 4.60 worker-seconds,
including four concurrent 1.06-1.39 s calls near the end. Most earlier recurring
stutters have no recorded pipeline compilation overlap. A warm shader catalogue
therefore does not explain away the submit/presentation stalls.

`aspect-bars-fixed-sample.txt` captures the main thread inside MoltenVK
`vkQueueSubmit` -> render-pass encoding -> `CAMetalLayer nextDrawable`, while the
guest command thread also waits to acquire the shared queue. SDK submission holds
that queue's external-synchronization mutex across the driver call. MoltenVK's
synchronous default performs Metal encoding on that calling thread, so a drawable
wait can prevent the guest renderer from submitting unrelated work. The sample
also contains guest-memory upload copies and fence waits: this is one confirmed
blocking mechanism, not an attribution of every long frame.

SDK patch **0073-moltenvk-asynchronous-queue-submissions.patch** defaults
`MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` to `0` before loading the Mac Vulkan runtime.
Explicit environment choices remain authoritative. MoltenVK dispatches encoding
onto its ordered queue, outside the application's queue-lock critical section.
This follows [MoltenVK's documented submission modes](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md#mvk_config_synchronous_queue_submits).
Queue ordering, Vulkan semaphores/fences, frame limits and resource retirement
remain in place; no guest timing or graphics-quality setting changes.

The earlier `async.log` experiment averaged 9.18 intervals/s before upload batching
and arithmetic optimization, with no sustained improvement. That negative result
is retained. The new default targets the now-observed CPU serialization path; it
is not evidence that the current native-scale game becomes smooth.

The patch logs the selected queue preference and retains the aggregate
`vulkan.guest_submit`/`vulkan.paint_submit` spans while adding
`vulkan.guest_queue_wait`, `vulkan.paint_queue_wait`,
`vulkan.guest_driver_submit` and `vulkan.paint_driver_submit`. The next manual log
can distinguish lock contention from work inside the driver. These are bounded
CPU spans, not GPU timestamps.

The native Release build passes. The loader-policy test covers the missing-value
default, repeated configuration, and preservation of explicit `1`, `0` and empty
values. The headless vertex/fragment-gradient fixture uses a render -> semaphore
-> tracking-fence handoff before reading or recycling GPU data, at supported
1x/2x/4x sample counts. Both synchronous and asynchronous modes pass with real
Metal access. Gameplay performance after this patch remains unverified: the owner
retains control of the next manual run, and no further game launch was performed.
Build/test/audit records are in the preserved evidence directory. All eight
focused CTest registrations pass: loader policy, both real-device queue modes,
three trace modes, pipeline completion and upload hazards. The SDK audit replays
72 patches with zero drift. The documentation checker reports zero errors and
51 existing warnings.
