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
