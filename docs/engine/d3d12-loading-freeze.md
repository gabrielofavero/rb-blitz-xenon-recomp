# DX12 post-A loading freeze and readback synchronization (2026-10-24)

The owner switched from Vulkan to D3D12, pressed A and reported transient broken
visuals, a frozen `Loading Rock Band Blitz Library File...` screen and repeating
audio. The owner subsequently closed the window. No live thread stack was captured.

## Evidence from the reported run

`out/build/win-amd64-release/logs/rb_blitz_320.log` confirms a D3D12 restart on the
GTX 970. Before A, shader storage reported **421 required / 421 succeeded**, zero
failed/missing/pending entries, in approximately 333 ms. At 17:39:30.345, content
discovery began and its existing pacing hook disabled VSync. At 17:39:30.435, the
recorded song-cache read completed successfully in **0.338 ms**. No enumeration or
usable-menu completion followed before window close at 17:40:35.999.

This was not an unfinished boot shader preload. The recorded disk read also
completed; that does not prove that all subsequent guest parsing/work completed.
The prior Vulkan run reached the menu after about 43.8 seconds of discovery.
The absence of a thread stack or GPU capture means the exact blocked call in the
reported run is unproven. The fixes below address concrete defects found in the
active DX12 synchronization/readback paths; gameplay reproduction is the final check.

## Invalid fence sequencing

The out-of-submission queue-operation path used this condition:

```cpp
SUCCEEDED(queue->Signal(fence, value) && SUCCEEDED(fence->SetEventOnCompletion(value, event)))
```

`Signal` returns an HRESULT. A successful `S_OK` is zero, so boolean `&&` skips
`SetEventOnCompletion`. The enclosing `SUCCEEDED(false)` still evaluates true.
The code then entered `WaitForSingleObject(event, INFINITE)` on an event it had
never armed. A failed signal could also be incorrectly converted to boolean success.

SDK patch **0025-d3d12-fence-and-resolve-readback.patch** uses production helpers
that check the signal HRESULT, then the arm HRESULT, then wait, in that order.
Failed operations cannot enter the wait or be reported as completion. Submission
signal failures are checked as well.

Fence event waits now poll at 100 ms intervals, verify the actual fence value,
detect device removal, and log scope/target/completed/elapsed details after one
second and periodically thereafter. Required GPU work is still awaited; no arbitrary
timeout declares it complete. Device/API failures use the existing graphics-failure
path. `UINT64_MAX` from `GetCompletedValue` is treated as device removal, never
as completion permitting transient resources to be recycled. Frame opening and
the all-queue completion result also reject failed or still-open work.

## Unfinished readback and resource lifetime

This installation explicitly enables `d3d12_readback_resolve`, which selects
delayed `fast` resolve readback. The old code considered a previous slot usable
when a resource and mapping existed, without recording its producing submission
or checking its completion. It could copy unfinished GPU data into guest RAM.
Buffer replacement and eviction also released resources without protecting their
last GPU write. These are correctness defects independent of shader warm-up.

The patch records each resolve slot's producing submission and written length.
Delayed readback uses a previous slot only when its write has completed and is
large enough. Otherwise it submits/waits for the current result, preserving the
existing correctness fallback. Even the final CPU copy checks readiness. Slot
replacement/eviction defers resource release through the existing submission
retirement queue when a write remains in flight. Existing explicit readback
settings, content discovery, library data and saves are unchanged.

Microsoft documents that [mapping a readback resource does not synchronize it
with GPU execution](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps);
the fence is what establishes completion before CPU access.

## Validation

The `d3d12_fence_sync` test exercises the exact production sequencing/readiness
helpers. It checks `S_OK` and positive success codes, signal/arming failures,
preserved error results, unfinished/uninitialized/short readback data, and the
device-removal sentinel. Its headless GPU part creates a real DX12 queue, gates
completion with another fence, submits a 1,024-byte GPU copy, verifies that the
pending slot is rejected, then signals/arms/waits and compares the completed
readback byte for byte. All **40 checks passed** on this machine. No game window
or game navigation is involved. A machine without a DX12 device reports this test
as skipped rather than claiming GPU validation.

The release build and **59/59 CTest tests passed**. The SDK patch audit reports
66 accounted files, zero unexpected/untracked files and zero patch failures.
The staged renderer DLL's SHA-256 matches the freshly built SDK renderer DLL.
The owner retried the rebuilt DX12 post-A route and confirmed: **"It reaches the
menu normally."** The subsequent `rb_blitz_321.log` independently confirms DX12,
421/421 preloaded pipelines, and a usable menu after 54,924 ms of content discovery
(3,554 successful enumeration results, zero failures). It contains no new
fence-wait or synchronization-failure messages. This validates recovery of the
reported loading route on this machine; it does not establish song/effect coverage
or prove which defective call was reached in the original run.

```powershell
cmake --build --preset win-amd64-release
ctest --test-dir out/build/win-amd64-release -R '^d3d12_fence_sync$' -V
```

The headless test validates sequencing and real GPU completion/readback; it does
not reproduce the title's loading route or every renderer lifetime transition.
The owner's manual retry validates reaching the menu; song/effect validation is
separate. If the problem recurs, the new fence-wait diagnostics distinguish a GPU fence
stall from another guest/content-loading blockage.
