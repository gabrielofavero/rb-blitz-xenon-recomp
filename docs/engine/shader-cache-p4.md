# Graphics preparation lifecycle (P4, 2026-10-24)

P4 separates *preparing* persistent shader and pipeline storage from *blocking the launch* on
it. Before it, both backends ran one long call — open the storage, import every description,
translate every variant, create every stored pipeline — either before the guest started
(`ReXApp::LaunchModule`) or on the command processor thread. The loading UI could not animate
during that call, and a driver pipeline creation could hold the thread for seconds. P4 makes the
same work a **cancellable generation** that the command processor advances in bounded slices
between packets, with explicit progress and a result that is only "ready" when every required
entry produced a real pipeline.

It does not add the visible *Preparing graphics* phase (P6), native caches (P5) or route
validation (P8). It gives them the lifecycle they need, and it keeps the launch-time behaviour as
the default until P6 integrates the guest-owned bar.

## What a generation is

`include/rex/graphics/graphics_preparation.h` (SDK patch 0027) defines:

| Part | Content |
| --- | --- |
| `GraphicsPreparationPhase` | `importing` → `translating` → `creating` → `finalizing`, then one terminal: `succeeded`, `incomplete` (finished, but entries failed/missing, or the storage declared nothing), `cancelled`, `failed` (storage could not be opened). A phase never moves backwards. |
| `GraphicsPreparationKey` | Title id, the caller's configuration epoch and a hash of the resolved cache root. A result is only valid for the key it was produced for. |
| `PipelinePreloadResult` | The P1 counts plus `cancelled`. `Complete()` requires work, nothing unfinished, nothing failed, missing or cancelled. |
| `GraphicsPreparationProgress` | One immutable snapshot: generation token, key, phase, counts, backend and rendering path, `has_catalogue`, cancellation flag, elapsed time and the number of service slices. `ready()` is the single readiness question. |
| `GraphicsPreparationController` | The state machine: `Begin` (refuses a second concurrent generation), `AdvancePhase`, `NoteCatalogue`, entry accounting forwarded to `PipelinePreloadTracker`, `RequestCancel`, `Finish`, `Snapshot`, `WaitForTerminal`, `Reset`. |

Rules the controller enforces for every caller:

- One generation at a time, and the generation token strictly increases across `Begin`/`Reset`,
  so a stale snapshot can never be mistaken for the current one.
- A zero-entry catalogue is **not** coverage. It finishes `incomplete` even when every entry that
  did run succeeded (`Complete()` is false), which is what keeps "the storage was empty" from
  reading as "everything is prepared".
- Cancellation moves not-yet-started entries to a terminal *cancelled* state, which is never a
  success. Work already in flight — a driver creation cannot be interrupted — reports its own
  result, and the pipelines it produced stay resident because they are real.
- `Reset()` (shutdown, re-initialization) releases every waiter; a barrier can never be left
  blocking on work nobody will finish.

## Where it runs

`CommandProcessor` owns the controller and services it from its worker loop
(`src/graphics/command_processor.cpp`):

- one slice per loop iteration, after queued host work and before executing the ring;
- further slices while the ring is idle, so a quiet guest still makes progress;
- an atomic active flag, so the idle spin does not take the controller's lock per iteration.

`BeginShaderStoragePreparation()` on the command processor takes the key, calls `Begin` and
schedules the import with `CallInThread` — the pipeline maps and the storage streams belong to
the command processor thread, so even the import runs there. The caller gets the generation token
back immediately.

Both backends expose the same trio to that thread:

| Method | D3D12 | Vulkan |
| --- | --- | --- |
| `BeginShaderStoragePreparation` | opens the two storage streams, reads and validates every stored description, loads the guest shaders and starts the translation pool, then returns. | the same, but the translation list is only collected — translations are done one per slice. |
| `ServiceShaderStoragePreparation` | while translating: polls the translation workers; after they join: enqueues 32 descriptions per slice and creates up to 4 pipelines on the command processor thread; workers create the rest. | translates one variant per slice, then enqueues 16 creation arguments per slice and creates up to 2 on the command processor thread; workers create the rest. |
| `CancelShaderStoragePreparation` | cancels not-yet-started entries, stops enqueueing, and the generation finishes `cancelled` after in-flight creation is joined. | the same. |

**Preparation never uses the gameplay creation queue.** Each backend gets a second, bounded pool
and queue for preparation. That is the ownership rule that makes the phase safe: `EndSubmission()`
still waits only for pipelines a draw actually needs, and preparation never makes a submission
wait — while a draw that needs a pipeline preparation has already published reuses it once its
real state exists. The Vulkan `startup_loading_` flag is left tied to the old blocking path; the
preparation path does not set it, so submissions keep their normal semantics.

The old blocking `InitializeShaderStorage(cache_root, title_id, blocking)` remains as a thin
wrapper: `Begin`, then drive `Service` to completion on the calling thread, then wait. Its result,
log lines and readiness rules are the P1 contract, now including the cancelled count and the
terminal phase name. The launch path uses it unless
`shader_storage_async_preparation=true`, which starts the generation after `main_thread->Resume()`
so the guest — and whatever the title is drawing — keeps running while it is prepared.

## App-facing surface

- `IGraphicsSystem` gained `BeginShaderStoragePreparation`, `CancelShaderStoragePreparation`,
  `QueryShaderStoragePreparation` and `WaitForShaderStoragePreparation` (default: unsupported),
  implemented by `GraphicsSystem` on top of the command processor. A caller never needs the
  concrete backend type.
- `shader_storage_async_preparation` (GPU category, default **false**) is the launch switch. When
  on, the launch logs `Preparing shader storage for title … beside the guest...` and the guest
  starts immediately; when off, the old `Initializing shader storage …` blocking path runs.
- Progress is readable from any thread; it is a snapshot, not a callback, because the UI phase
  (P6) is decided by the guest's own records, not by a host thread.

## Tests

`tests/graphics_preparation_tests.cpp` → ctest `graphics_preparation` exercises the real header
with real threads (not a copied model): one generation at a time and increasing tokens, forward
only phases, progress counting only completed work, the zero-entry catalogue that can never be
ready, cancellation that cancels queued entries without counting them as success while a running
entry keeps the generation open, the delayed-worker barrier (waiting at least 150 ms for a 200 ms
entry), shutdown releasing a waiter, idle snapshots, and the tracker's cancelled terminal state.

## Gates and limits

- Verified in this session: the full release build, the complete ctest roster including the new
  suite, and the SDK patch audit with patch 0027. Numbers are recorded in the P4 record of the
  [shader-cache plan](../plans/in-progress/2026-10-10-shader-cache-plan.md#p4--add-asynchronous-preparation-without-starving-the-loading-screen).
- **Not measured:** no game/UI run was made for P4. This document makes no claim that in-song
  compile work dropped, that a warm boot is faster, or that every pipeline a song needs is in the
  catalogue — those are P8's route matrix, P5's native caches and P3's coverage respectively.
- **Not yet integrated:** the visible *Preparing graphics* progress and the post-A gate are P6;
  the loading screen's own pipelines are whatever the title already draws before preparation
  starts, because preparation is scheduled after the guest resumes and never runs on the
  submission path.
- The configuration key is stored and compared, but nothing invalidates a generation
  automatically yet: a settings change that alters pipelines must start a new generation (P7).

## Sources

| Source | Role |
| --- | --- |
| `rexglue-sdk/include/rex/graphics/graphics_preparation.h` | The lifecycle, key, progress and controller. |
| `rexglue-sdk/include/rex/graphics/pipeline_preload.h` | P1 entry states, extended with cancellation. |
| `rexglue-sdk/src/graphics/command_processor.cpp` | The worker loop's slices and the app-facing begin/cancel/query/wait. |
| `rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp`, `src/graphics/vulkan/pipeline_cache.cpp` | The backend import, translation, enqueue and creation slices and their preparation pools. |
| `rexglue-sdk/src/ui/rex_app.cpp` | The launch switch between the blocking and asynchronous forms. |
