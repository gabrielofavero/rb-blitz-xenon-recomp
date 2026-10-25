# Native pipeline caches (P5, 2026-10-24)

P5 persists the *driver's own* pipeline results beside the shareable recipes. Before it, both
backends replayed a validated recipe catalogue on every boot: the portable stores describe what the
title asked for (`.xsh` guest microcode, `.xpso` pipeline descriptions), and the driver turned each
recipe into a pipeline object from nothing. A D3D12 `ID3D12PipelineState` can hand the driver its own
cached blob, and Vulkan has a `VkPipelineCache`; both are exactly the state that makes the second
creation of a pipeline cheaper, and both are only valid for the adapter, driver build and translator
that produced them.

This document records what P5 added, how it is keyed and validated, what it refuses, and what is
**not** yet established. It does not claim a timing win: no cold-versus-warm boot was measured in
this pass, and the plan's own rule is that cache-file existence is not a performance result (P8 owns
that measurement).

## 1. The component

`rexglue-sdk/include/rex/graphics/native_pipeline_cache.h` (SDK patch
`0028-native-pipeline-caches.patch`) is header-only and backend-agnostic, like
[`pipeline_preload.h`](../../rexglue-sdk/include/rex/graphics/pipeline_preload.h) and
[`graphics_preparation.h`](../../rexglue-sdk/include/rex/graphics/graphics_preparation.h). It keeps
three things apart, because confusing any two of them turns a cache into a correctness bug:

| Part | What it is | Where it is used |
| --- | --- | --- |
| `NativePipelineCacheIdentity` | Adapter (vendor, device, subsystem, revision), driver version, Vulkan `pipelineCacheUUID`, backend, title id, rendering path, container schema version, `PipelineDescription::kVersion`, translator version. `Hash()` is the identity an entry file records. | Written into every entry; a file under another identity is refused, not reinterpreted. |
| Entry key | The portable description hash (`XXH3_64bits(&PipelineDescription)`) for a D3D12 guest pipeline, or one constant for Vulkan's whole-cache entry. | Names *which* pipeline the payload belongs to, independently of the identity. |
| Payload | Opaque driver bytes: an `ID3D12PipelineState` cached blob, or serialized `VkPipelineCache` data. | Never interpreted here - only length- and hash-checked. |

`NativePipelineCacheStore` writes one file per entry:

```text
<identity-dependent directory>/<key, 16 uppercase hex>.pso
  NativePipelineCacheEntryHeader (40 bytes)
    magic 'RBNP' | format version | identity hash | key | payload hash | payload size
  payload (opaque)
```

Rules the store enforces:

- **A rejected entry is never handed to a driver.** Wrong magic, wrong format version, wrong
  identity, wrong key, a payload size that does not match the file, or a payload hash mismatch all
  end in `rejected`; the file stays on disk for the owner to `Forget()` once it has decided to replace
  it.
- **Writes are atomic and bounded.** A flush writes a temp sibling and renames it over the entry, so a
  reader sees the old or the new complete file. One entry is capped at 64 MiB, pending bytes at
  128 MiB / 8192 entries, the directory at 1 GiB / 32768 entries. Everything refused is counted, never
  silently dropped.
- **Serialization happens outside play.** Entry bytes are *staged* in memory when a pipeline is
  created and only reach the disk on an explicit `Flush()`, which both backends call when a
  preparation generation finishes and when the storage shuts down - never from a draw.
- **Two instances sharing one cache directory cannot corrupt each other.** Each write is a complete
  atomic replace, and Vulkan's whole-cache file is merged (see §3) rather than blindly overwritten.
- A disabled store answers nothing and writes nothing, so a machine without a writable cache
  directory keeps running on the shareable recipes alone.

## 2. Direct3D 12 - cached PSO blobs

`PipelineCache` opens `shaders/local/<TITLEID>.<rtv|rov>.d3d12/` from `OpenShaderStorage`, once the
rendering path is known, and derives the identity from the live device: `ID3D12Device::GetAdapterLuid()`
→ `IDXGIFactory4::EnumAdapterByLuid` → `DXGI_ADAPTER_DESC1` (vendor, device, subsystem, revision) plus
the UMD version from `IDXGIAdapter::CheckInterfaceSupport(__uuidof(IDXGIDevice))`. The UMD version is
the field a driver update changes; when a driver does not implement `CheckInterfaceSupport` the field
stays zero and the blob is still guarded by the driver's own validation plus the retry below.

Creation (`CreateD3D12Pipeline`) now:

1. hashes the `PipelineDescription` (the same hash the portable store uses) and loads that entry;
2. passes the bytes as `state_desc.CachedPSO` when an entry exists;
3. on failure **with** a blob: logs the HRESULT, `Forget()`s the entry, clears `CachedPSO` and creates
   the pipeline the ordinary way - a rejected blob is never reported to the caller, and the portable
   recipe is untouched;
4. after a successful creation that did not use a blob, calls `ID3D12PipelineState::GetCachedBlob()`
   and stages the result.

Blob capture is per *newly created* pipeline, not per draw, and the write happens at the next flush.

## 3. Vulkan - a real `VkPipelineCache`

`VulkanPipelineCache::OpenNativeCache` creates `shaders/local/<TITLEID>.<fbo|fsi>.vk/` and one
`VkPipelineCache` from the stored bytes. Validation happens twice before the driver sees anything: the
store's container (identity, key, hash) and Vulkan's own `VkPipelineCacheHeaderVersionOne`
(`headerVersion`, `headerSize`, `vendorID`, `deviceID`, `pipelineCacheUUID` against
`VkPhysicalDeviceProperties::pipelineCacheUUID`). If `vkCreatePipelineCache` still refuses the data,
the store entry is forgotten, an empty cache is created, and the run continues.

The handle is published on `VulkanDevice::shared_pipeline_cache()`, and **every** creation path on the
device passes it - guest graphics pipelines, the render-target transfer/resolve pipelines, the
swap-effect gamma/FXAA pipelines, the presenter and immediate-drawer pipelines, and
`ui::vulkan::util::CreateComputePipeline`. That is why the compute helpers the P3 inventory names
(transfer, dump, clear, resolve copy, host depth store) are covered on Vulkan without touching their
sites individually: they are device-wide creations, and the cache is device-wide state.

Serialization is guarded by the one condition that makes it legal: no pipeline creation may run while
the cache is snapshotted. `SaveNativeCache` therefore refuses to serialize while
`IsCreatingPipelines()`, and a new gameplay creation can only be enqueued by the command processor
thread, which is the thread doing the snapshot - so "queue empty and no worker busy" is a real
quiescence window. When a preparation generation finishes while the guest is playing, the snapshot can
be skipped and logged; the shutdown path (after the creation threads are joined) always succeeds.

Concurrent instances: at save time the store's current file is read back; if it is valid and differs
from what this instance loaded, it is created as a temporary `VkPipelineCache` and merged with
`vkMergePipelineCaches` before serializing. The last writer therefore includes the other run's
pipelines instead of discarding them. `CloseNativeCache` stops publishing the handle first, and skips
`vkDestroyPipelineCache` when a creation is still in flight (re-initializing the storage while the
guest draws), abandoning the object rather than racing it.

`vkCreatePipelineCache`, `vkDestroyPipelineCache`, `vkGetPipelineCacheData` and
`vkMergePipelineCaches` were added to the SDK's Vulkan 1.0 device function list
(`include/rex/ui/vulkan/functions/device_1_0.inc`); all four are core 1.0, so their absence would mean
a broken loader, and a missing pointer still fails device creation the same way every other entry does.

## 4. The switch and the log

`native_pipeline_cache` (GPU, default **true**, requires restart) disables the whole layer on both
backends; the shareable recipe path is unaffected either way. It exists for A/B measurement and as a
rapid escape hatch, not as a product feature.

Boot and boundary lines, all at info level except the failures:

```text
Native pipeline cache: adapter 10DE:13C2 subsystem 1111 revision A1 driver 32.0.15.7602
Native pipeline cache: adapter 10DE:13C2 driver 0x…, 123456 byte(s) restored
D3D12 rejected a stored cached-PSO blob (hr=0x…); creating the pipeline without it and replacing the entry
Native pipeline cache: 132 cached PSO blob(s) written (d3d12 native pipeline cache: 132 reused, … entry file(s) / … byte(s) on disk)
Native pipeline cache: serialization deferred at shader storage shutdown, pipelines are still being created
```

## 5. Tests

`tests/native_pipeline_cache_tests.cpp` → ctest `native_pipeline_cache`, 86 checks against the real
header with a real scratch directory: the identity hash covers every compatibility field, an entry
round-trips, a flipped payload byte / truncated file / forged size / wrong key / foreign identity are
all refused, a store configured for another adapter refuses the entry on load and leaves the file in
place, staging replaces atomically and counts one pending entry per key, `Forget` removes the entry
and the directory counters, the size bounds refuse instead of writing, and a disabled store answers
nothing.

## 6. Gates and limits

- Verified in this session: the full release build with both backends (`rexruntime.dll`,
  `rexgpu-xenos.dll`, `rb_blitz.exe`), the complete ctest roster (**60/60**, including the new suite),
  and the SDK patch audit (**27 patches, 0 failed, 78 patched files, 0 unexpected**).
- **Not measured:** no game/UI run was made in this pass. Nothing here claims that a song now compiles
  less, that a warm boot is faster, or what the cache costs on disk in practice - those need P8's
  route matrix (cold, replay, full restart, backend switch) with the preparation time and disk size
  reported per backend.
- **P6/P7 still own the lifecycle around it:** the visible *Preparing graphics* phase and the post-A
  gate are P6; invalidation by settings/content/SDK change and partial-write handling are P7. This
  layer only refuses what its own identity and the driver reject.
- **Direct3D 12 scope:** guest graphics pipelines only. D3D12's helper compute pipelines are created
  from `ui::d3d12::util::CreateComputePipeline`, which does not see the cache's store - and the ones
  created during command-processor initialization run before a storage root exists, so they could not
  use it even if the signature were extended. Guest PSOs are the measured dominant cost there (81 per
  song on an empty cache, [shader-cache-p0.md](shader-cache-p0.md) §4); extending the layer to the
  helper compute sites is a follow-up measurement, not a claim of this pass.
- Persisting translated shader bytecode (a third layer) stays conditional on a measurement that the
  two layers are not enough; P5 deliberately did not add it.

## Sources

| Source | Role |
| --- | --- |
| `rexglue-sdk/include/rex/graphics/native_pipeline_cache.h` | The identity, the entry format and the bounded store. |
| `rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp` | Adapter identity, blob load/retry/capture, flushes at preparation end and shutdown. |
| `rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp` | `VkPipelineCache` creation, validation, merge-at-save, quiescence rule, close. |
| `rexglue-sdk/include/rex/ui/vulkan/device.h` | The device-wide handle every creation path reads. |
| `rexglue-sdk/include/rex/ui/vulkan/functions/device_1_0.inc` | The four pipeline-cache functions the loader now resolves. |
| `rexglue-sdk/src/graphics/graphics_system.cpp` | The `native_pipeline_cache` cvar. |
| `patches/rexglue-sdk/0028-native-pipeline-caches.patch` | The applied change set. |
| [`tests/native_pipeline_cache_tests.cpp`](../../tests/native_pipeline_cache_tests.cpp) | ctest `native_pipeline_cache`. |
