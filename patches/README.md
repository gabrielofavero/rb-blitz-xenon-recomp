# Local patches for the pinned ReXGlue SDK

`rexglue-sdk/` is a pinned submodule of the **upstream**
`https://github.com/rexglue/rexglue-sdk.git` (pinned at `c94f5eb`). Local edits
must **not** be committed inside the submodule: the parent gitlink would then
reference a commit that does not exist on the upstream remote, so a fresh
checkout could not initialize the SDK (breaking the reproducibility
guarantee).

Instead, SDK fixes are kept here as patch files and applied to the pinned
checkout.

## Patches

`0086-macos-skipped-paint-recovery.patch` acknowledges a queued paint skipped
while Cocoa reports the window hidden, minimized or occluded. Leaving the
presenter's request pending made later guest frames coalesce into an event that
had already been consumed, freezing the initial black loading image even after
the guest reached the title. Focus also requests a repaint without replacing
an unchanged Metal swapchain. Expose can reconnect a previously unavailable
surface even without a size change. A headless regression exercises request
coalescing, skipped paints, repeated focus gains and unavailable-surface recovery
through the real presenter/window state machines. The startup was reproduced locally;
bringing its window forward restored the live title immediately.
The final rebuilt portrait run displays the title and retires the loading icon;
the presenter regression passes 12 checks, four related CTest suites pass, and
both macOS and iOS builds and the exact patch replay audit pass.

`0082-forward-unhandled-posix-faults.patch` forwards rejected native faults to
the prior signal handler or the OS, preserving recoverable guest page faults.
Previously a rejected fault returned to the same instruction indefinitely.

`rexglue-sdk/thirdparty/moltenvk/0001-retain-presentation-drawable.patch` protects
shared drawable ownership and retains each presentation's drawable before its
callbacks capture it. Drawable acquisition remains outside the ownership lock;
release happens outside it too, so callbacks can retire objects without deadlock.
Completion callbacks finish accessing the image before releasing it. The iPhone
watchdog reports from 2026-11-16 captured an invalid read in `objc_retain` during
presentation, with both UI and GPU workers waiting on fences. This matches the
drawable lifetime race reported in
[MoltenVK issue 2791](https://github.com/KhronosGroup/MoltenVK/issues/2791).
The exact device failure still requires confirmation with the rebuilt IPA.

Both patch scripts recursively replay nested dependency patch sets, check their
upstream pins, and audit their contents before accounting for a dirty gitlink.
The upstream SDK and MoltenVK pins remain unchanged.

`0079-chainable-application-initialization.patch` exposes the existing app
initialization coordinator to subclasses. The separate iOS importer completes
native setup before paths/settings/graphics initialize, then calls that same
coordinator on the UI thread. Initialization order and lifecycle handling stay
in one SDK implementation.

`0078-ios-application-lifecycle.patch` handles UIKit activation synchronously
through SDL's event watch. It stops inactive UI paints, pauses audio and guest
time, gates all Vulkan queue acquisitions and waits for submitted GPU work before
background entry. Foreground resumes queue waiters and requests a new paint;
interrupted startup uploads on the UI thread keep pumping UIKit notifications.
The owner's failed run reported Metal `BackgroundExecutionNotPermitted` followed
by `VK_ERROR_DEVICE_LOST`. Host regressions cover draining, blocked submissions,
duplicate events, repeated resume, startup pumping and guest-clock continuity.
Device lock/unlock and app switching remain acceptance checks.

`0077-moltenvk-replacement-drawable-size.patch` restores the Metal layer's drawable
extent after replacement swapchain creation and old-swapchain retirement. The
pinned MoltenVK 1.4.1 can flush pending presentations by shrinking that shared
layer to 1x1 after the replacement has initialized it. This matches the owner's
iPhone report: a correct captured guest splash with persistent suboptimal screen
presentation and black visible output. The correction leaves an already-correct
drawable alone and is covered by a native CAMetalLayer test without a window.
Device confirmation remains pending.

`0073-moltenvk-asynchronous-queue-submissions.patch` defaults Mac queue submission
to MoltenVK's ordered asynchronous encoder, preserving explicit
`MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS` overrides. The preserved 720p/16:10 song
shows presenter drawable waits inside synchronous submission while the guest
command thread waits for the same externally synchronized queue. The patch also
separates queue-lock wait spans from driver-submit spans and logs the selected
preference. Semaphore/fence ordering and resource retirement stay in place.
The real-device vertex/gradient fixture now includes a render/semaphore/fence
handoff and runs in both queue modes. Gameplay pacing after this change is
unverified; the owner requested no further game launches. See the
[preserved-run investigation](../docs/engine/vulkan-performance.md#native-scale-frame-pacing-and-queue-serialization-2026-11-15).

`0072-explicit-native-video-mode.patch` preserves explicitly configured video
width/height even when they equal the defaults. The kernel query and SDL path
now consider the cvar source as well as its value. Previously explicit height
720 fell through to a Retina logical-window height of 360, clamped to 480;
width 1152 stayed explicit, producing horizontal bars from a 2.4:1 video aspect.
The `video_mode_intent` host fixture uses the real cvar registry to cover native
1280x720, 1152x720 and reset-to-default behavior. The native Release build,
nine affected tests and a full-song 16:10 live run passed after this fix; the
earlier boot log had shown cvar intent without proving the query.

`0071-moltenvk-vertex-arithmetic.patch` applies the explicit, separately rounded
Fma lowering to vertex shaders as well as fragments on MoltenVK. It avoids the
pinned SPIRV-Cross's unoptimized arithmetic helper calls, retains signed zero,
and raises shader modification version to 14. A full One Week run at the previous
Auto 2x path averaged 26.27 guest draws/s, compared with roughly 17 after batching
alone. The expanded headless GPU test covers vertex/fragment arithmetic and
fragment gradients. Other drivers retain their previous lowering.

`0070-moltenvk-shared-memory-upload-batching.patch` moves independent staging
uploads before a deferred MoltenVK render pass to avoid repeated attachment
store/reload. Conservative page dependencies include cached vertex/index reads;
aliasing uploads, memory-export draws and untracked helper work keep the ordered
path. Sources retain submission-fence retirement, and transfer barriers bracket
the batch. Sampled gameplay pass starts fell from roughly 300–500 to 26; observed
completed guest draws rose from about 9 to 17 per second. See the
[Mac root-cause follow-up](../docs/engine/vulkan-performance.md#root-cause-follow-up-rendering-cost-and-audio-rewind-2026-11-15).

`0069-moltenvk-nonblocking-pending-pipelines.patch` removes synchronous placeholder
pipeline creation on MoltenVK. Pending draws were already skipped, but constructing
the unused placeholder could still block the command processor for 600 ms. New
recipes and unready entries retained after a cancelled preload now queue the real
pipeline once and skip its draws until the existing completion handoff publishes
its handle/layout. Startup and explicit synchronous compilation retain their
barriers; other Vulkan drivers keep their existing placeholder route. This targets
first-use hitches, not the sustained M1 rendering slowdown. See the
[Mac performance investigation](../docs/engine/vulkan-performance.md#macos-m1-investigation-2026-11-15).

`0068-vulkan-stall-attribution.patch` adds bounded, >=50 ms Vulkan spans for
command replay, guest/paint submissions (including queue acquisition), swapchain
image acquisition and submission/paint fence waits. It uses the existing shared
trace and its budgets; it changes no rendering or scheduling policy. On M1 the
samples locate Metal drawable acquisition inside paint submission, while the
command processor waits for the same queue. This complements the existing
D3D12 attribution spans.

`0067-portable-vulkan-shader-storage.patch` rewinds append/update streams before
reading shader and recipe headers, avoiding macOS's initial EOF position. It
also makes the packed recipe layout explicit: a 60-byte description with render
targets at byte 44 and a 68-byte stored record, matching the shipped catalogue
on Clang as well as Windows. Static assertions guard those values. Valid recipes
unsupported by the current GPU stay on disk rather than causing an incorrect
truncation to the filtered count. The M1 import now reads 409 supported recipes
and skips 25 unsupported ones without corrupt records; live completion evidence
is tracked in [shader-cache.md](../docs/engine/shader-cache.md), part 6.

`0066-macos-drawable-visibility.patch` treats hidden, minimized and occluded
Cocoa windows as unavailable for presentation even when their pixel size is
positive. Queued and expose paints check the latest SDL visibility after
event pumping, so they cannot enter Metal drawable acquisition ahead of the
queued hide event. Ordinary Mac focus gains retain the live swapchain when
its size is unchanged, and expose repaints it; Windows keeps forced focus
reconnection. The owner reported a black fullscreen window after
switching away/back; the captured UI thread was waiting in `nextDrawable`
while the GPU thread waited for its queue. The final build preserves the
live Metal swapchain; the owner confirms the picture returns normally after
switching away/back (2026-11-15). A separate blank capture during the guest's
post-A transition does not establish a task-switch failure.

`0065-moltenvk-fragment-gradients.patch` avoids non-inlined Metal precision
helpers in translated fragment shaders. On M1/MoltenVK 1.4.1, their results are
incorrect for helper invocations outside MSAA triangles, corrupting derivatives
and texture mip selection along sprite borders and diagonals. MoltenVK fragment
arithmetic uses explicitly rounded Fma equivalents for add/subtract/multiply
and vector scaling, retaining NoContraction and the sign of zero products.
Other drivers retained their existing translation. This patch initially left
vertex shaders unchanged and used shader modification version 13; patch 0071
extends the MoltenVK lowering to vertex shaders and advances that version to 14. The synthetic headless
`vulkan_fragment_gradient` test renders the production builder's operations,
checks derivatives at 1×/2×/4× and verifies separate arithmetic rounding;
`--baseline` reproduces the defect. See the dated native Mac evidence in the
[release plan](../docs/plans/in-progress/2026-11-09-linux-macos-release-plan.md).

`0064-macos-vulkan-icd-stage.patch` resolves the in-tree MoltenVK ICD relative
to the SDK helper's definition. A parent project does not inherit the SDK's
directory-local `REXGLUE_ROOT`, so the old post-link command tried to copy
`/cmake/MoltenVK_icd.json` and failed after linking the Mac game. Reproduced
and rebuilt on Apple Silicon on 2026-11-15; the installed-SDK route is unchanged.

`0063-auto-open-search-keyboard.patch` requests the OS keyboard automatically
when the themed search dialog opens, on the frame after text receives focus.
A can still reopen it; the existing submit, cancel and dismissal paths remain.

`0062-stutter-attribution.patch` adds long guest-work, guest file-read,
GPU-command-ring idle and presentation spans to the same default-on trace.
File sites are JSON quoted so paths retain spaces and backslashes; counters
remain cumulative while logging allowances renew on entry to active playback.
The shared presenter span covers both backends, with separate DXGI/Vulkan
present calls. No scheduling, I/O or rendering policy changes. See
[sharing stutter diagnostics](../docs/engine/diagnostics.md#sharing-stutter-diagnostics).

`0061-long-d3d12-operation-trace.patch` records selected non-compilation D3D12
CPU spans lasting at least 50 ms: submission/replay/queue execution, swap, fence
waits, texture creation and sparse shared-memory allocation/mapping/uploads.
Short spans return before the shared trace lock. This fills an attribution gap
in the owner's Bounce Man first-play log, whose seven roughly 0.8-second pauses
have no compilation overlap. It changes no rendering or synchronization policy;
clock-read overhead is unmeasured and a new runtime capture is still needed.
See the [shader-cache study](../docs/engine/shader-cache.md).

`0060-default-shader-compilation-trace.patch` enables the bounded shader, wait
and frame-gap instrument by default so normal session logs retain evidence for
intermittent stalls. Explicit config, environment and CLI overrides can disable
it. It changes no rendering or audio policy; timing overhead is unmeasured.

`0059-preserve-touch-mouse-source.patch` preserves SDL's touchscreen source on
mouse events so the title can keep controller hints for touch while retaining
mouse hints for physical mouse clicks. Pointer navigation is unchanged.

`0058-native-window-host.patch` lets an application embed SDL's rendering/input
window in a persistent native frame. The host handles display modes and titles;
SDL keeps rendering, mouse and keyboard events in the game process. Native focus
notifications also update SDL keyboard focus, because SDL's foreground HWND test
assumes its window is top-level. Unhosted windows keep the existing behavior.

`0057-animate-startup-during-shader-preparation.patch` keeps an app's startup
paint loop active while stored shaders are prepared. For apps opting into startup
paint, the command processor runs its existing sliced preparation and a waiter
resumes the guest only when that generation finishes. The default launch barrier
is preserved; the visible preboot spinner can continue animating during it.

`0054-sync-pixel-size-after-restore.patch` reconciles the presenter's pixel
size on SDL resize, focus, expose, and restore events. Windows can report a
zero-height SDL size during minimization without a matching pixel-size event
after restore; the native HWND client size is used when SDL's cached size is
zero. Focus gain and restore also reconnect the presentation surface even when
the cached size or focus latch did not change. C1's Vulkan UI check reproduced
lost input after minimize/restore on the prior stack and recovered it with this
patch; Desktop Duplication was needed to see Vulkan's real Full Screen image.

`0053-startup-paint-and-guest-output-signal.patch` gives synchronous startup a
bounded SDL paint pump. It services only the presenter's queued paint event, so
boot work can keep the window responsive without running deferred UI callbacks
before the app is initialized. The presenter also exposes a monotonic count
after each active guest image is refreshed and published, so a temporary boot
icon can wait for a new guest frame after an observed loading state.
`ReXApp::WantsStartupPaint` lets a title
opt in to one paint immediately after its startup drawer is installed, before
runtime construction. The application must keep calling the pump at bounded
work checkpoints because an SDL timer cannot paint while the UI thread is blocked.

`0052-boot-stage-timings.patch` logs the duration of guest function registration
and SDL gamepad initialization at info level. These are the two unmeasured stages
between function-table creation and the first controller log in a slow boot.
It is temporary diagnostic instrumentation for the large-DLC startup issue.

`0036-keyboard-dialog-theme.patch` adds an opt-in theme for the existing XAM
keyboard modal. The title enables it for My Songs and RhythmVerse searches:
a compact, centered, resolution-scaled translucent black panel, three glowing
red border rails, a full-width input and smaller centered Done/Cancel buttons.
Bold sans headings and condensed prompts approximate the retail popup's type;
system fonts are baked at 64 px before atlas upload, with a bundled fallback. Enter
confirms, Escape cancels, and the guest's prompt and completion path are kept.
Colors and fonts live in `src/ui/keyboard_theme.cpp`; other SDK dialogs retain
their existing presentation. Upstream candidate: the opt-in styling seam and
responsive keyboard layout are general; the palette is title-specific.

`0037-keyboard-controller-navigation.patch` lets B cancel, including while
typing, and A request the OS keyboard. Directions have no action. Enter
(including keypad Enter) is the submit shortcut; mouse clicks on Done/Cancel
also work. There is no typing lock or Start handoff. Each button has its own
press edge, so held A or stick drift cannot prevent B cancellation.
The title checks actual window focus instead of the SDL keyboard-focus latch.
A focuses text, then requests the keyboard on the following frame; it never
submits. Windows uses the documented InputPane desktop interop/TryShow API in
`src/platform/virtual_keyboard.cpp`, and requests TryHide at dialog close only
after a successful show request. This API is best effort: Windows may keep using
an attached hardware keyboard instead of showing its input pane. Request results
are logged. External keyboards and normal typing remain available. See
[InputPane.TryShow](https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.inputpane.tryshow).
Host tests exercise the controller rule and real ImGui fields/buttons without
opening the game. Upstream candidate: the opt-in sampler and cancel rule.

`0039-host-dialog-controller-state.patch` fixes the input layer beneath that
dialog. SDL's guest-facing `GetDeviceState` deliberately zeroes buttons while
ImGui captures the mouse, so reading it from the host modal also hid B. The new
`InputDriver::GetDeviceStateForUI` defaults to the normal read, with an SDL
override that copies physical state independently of the guest activity gate.
Only the host-dialog sampler opts in; guest reads retain their suppression.
`keyboard_controller_driver` reproduces this using the production SDL driver,
an SDL virtual pad and an inactive guest-input callback without opening a native
window: B reaches the modal, while guest reads remain neutral. Actual device
acceptance remains a manual check. Upstream candidate: host dialogs need input
while they own capture.

`0038-achievement-toast-theme.patch` adds opt-in styling to the existing unlock
notification. The title shares the keyboard's translucent dark background,
three glowing red border rails, bold sans fonts, and white text through
`src/ui/keyboard_theme.cpp`. The toast retains its 320 × 70 footprint and
bottom-right placement, icon, queue timing, and fade; gamerscore sits beside the
heading, and long achievement names use an ellipsis. Every visual element fades
together. SDK defaults remain unchanged unless the toast theme is enabled.
Upstream candidate: the styling seam is general; the palette is title-specific.
Release build and patch replay validate integration; in-game appearance awaits
manual confirmation.

Shader-cache P0 adds `0020-bounded-shader-compilation-trace.patch` and extends
0019's swap observations with the shared, opt-in instrument. The runtime owns the
clock/phases/counters so helper creation in `rexruntime.dll` and guest creation in
`rexgpu-xenos.dll` use the same timeline. It changes no compilation or submission
policy. See [the P0 evidence map](../docs/engine/shader-cache.md) (part 1).

Shader-cache P1 adds `0021-preload-completion-accounting.patch`: the shared
`PipelinePreloadTracker`/`PipelinePreloadResult` in
`include/rex/graphics/pipeline_preload.h`, one explicit state per required pipeline in
both caches, the corrected D3D12 worker-growth count, and the blocking preload barrier
moved out of the temporary-worker cleanup (it skipped the no-growth case entirely).
The restructuring replaces the D3D12 preload drain/wait sites that 0020 originally
instrumented, so **those two hunks live in 0021 now** — a patch whose post-image
another patch rewrites can never `--reverse --check` again, which the audit reads as
drift. 0020 keeps the rest of the instrument. See the P1 record in the
[shader-cache plan](../docs/plans/finished/2026-10-10-shader-cache-plan.md#p1--make-backend-completion-and-failure-reporting-trustworthy).

The patch audit now replays the full stack against pristine HEAD files under
ignored `out/sdk-patch-audit/` and compares final contents (normalizing CRLF).
This handles new files and disjoint edits added out of source order; concatenated
hunk bodies could not validate those cases. Nested dependency artifacts that only
make untracked files are excluded; tracked edits and moved gitlinks remain visible.

| Patch | Purpose | Upstream candidate |
| --- | --- | --- |
| `rexglue-sdk/0054-sync-pixel-size-after-restore.patch` | Reconciles the actual pixel size and reconnects presentation after minimize/restore. | yes |
| `rexglue-sdk/0055-time-bound-incomplete-gpu-packets.patch` | Waits up to one second without guest write-pointer progress before treating an incomplete primary GPU packet as corrupt. The old 64 retries finished in about a millisecond in `rb_blitz_693.log`, then exhausted resynchronisation and froze the discovery picture. | yes |
| `rexglue-sdk/0053-startup-paint-and-guest-output-signal.patch` | Lets a title opt in to an immediate startup paint, service later paints without running startup-sensitive callbacks, and identify the first active guest output. | maybe |
| `rexglue-sdk/0051-ui-present-display-confirmation.patch` | Counts successful UI-thread paints separately so an older guest-output present cannot confirm a new display mode. | maybe |
| `rexglue-sdk/0050-verify-startup-display-mode.patch` | Refuses startup when SDL cannot apply the saved native display mode. | yes |
| `rexglue-sdk/0049-successful-present-sequence.patch` | Counts successful presenter submissions for runtime display diagnostics. | maybe |
| `rexglue-sdk/0048-atomic-runtime-display-mode.patch` | Adds one UI-thread operation for Windowed, Borderless and Full Screen, with SDL rollback and window placement restoration. | maybe |
| `rexglue-sdk/0047-content-device-validation.patch` | Lets the title inspect a mounted content device before registering it for guest access. Rock Band Blitz uses this to refuse executable song DTA from primary, structured, and flat-library content; see [DaTArrest security](../docs/reference/datarrest-security.md). | maybe |
| `rexglue-sdk/0045-distinct-sdl-extra-buttons.patch` | Keeps SDL misc, touchpad and paddle buttons separate from XInput aliases, and exposes the connected pad's available and held extra controls for gameplay capture. | maybe |
| `rexglue-sdk/0046-indirect-ringbuffer-recovery.patch` | Propagates a failed indirect GPU packet to its primary-ring caller, retries from the failed primary packet, and skips the full indirect call when bounded recovery is exhausted. This preserves the chance to reach later frame packets after the library-loading overflow seen in `rb_blitz_443.log`. Live recovery remains to be checked. | maybe |
| `rexglue-sdk/0043-texture-override.patch` | Adds a process-wide texture override provider in `rexruntime`, candidate fingerprinting in the shared texture cache, and narrow generation invalidation. D3D12 and Vulkan upload canonical BC3 blocks directly to a matching BC3 host texture; unsupported host formats keep the guest load. The application supplies P1's synthetic markers and the measured fingerprint selection. No guest scene or guest memory is rewritten. | yes |
| `rexglue-sdk/0041-media-keys.patch` | Deliver SDL Media Next/Previous/Play-Pause through the existing VirtualKey channel. | yes |
| `rexglue-sdk/0001-xthread-log-once-core-count-warning.patch` | `XThread::SetActiveCpu` re-logged "Too few processor cores" on every thread CPU assignment (~11k lines, ~580 KB per boot). Log it once. | yes |
| `rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch` | The SDK has no host format and no load shader for the 32-bit guest texture formats (`k_32`, `k_32_32`, `k_32_32_32_32`), which are 0.32 fixed point when `num_format = 0`; `CreateTexture` then returned `nullptr` and the SRV sampled (0,0,0,0), making every alpha-blended 3D element invisible (B-010 in [../docs/history/bringup-log.md](../docs/history/bringup-log.md)). Maps them to the float host formats with the existing word-mover load shaders, carries `num_format` in `TextureKey`, and converts the guest words to IEEE float bits on the CPU into an upload-pool staging buffer bound as the load shader's source. On 2026-09-30 the two limits the first cut left were removed: the conversion now covers `num_format = 1` (integer) as well as `0` (fraction), using the scaling rule of the SDK's own other number-format consumers (the `is_integer` mode of the vertex fetch and the integer surface number formats of the memory export both leave an integer component unscaled) and reporting the first use of each such format at `REXGPU_WARN`; and the staging buffer comes from a texture-cache-owned pool that allocates a page large enough for the request instead of requiring the whole converted texture and its mips to fit `GraphicsUploadBufferPool`'s 2 MiB default page — which is what `Page::size_` (the size each page was actually allocated with) in `GraphicsUploadBufferPool` and the page-size increase in `D3D12UploadBufferPool::Request` are for. **Extended 2026-10-24 to Vulkan:** its format table still rejected `k_32_32_32_32` on every gameplay frame (`rb_blitz_310.log`), matching the owner's missing-highway screenshot. Both backends now share the component conversion in `pipeline/texture/util.h`. Vulkan uses float host images and mapped VMA storage buffers, reuses them only after their submissions complete, flushes noncoherent writes, and converts base/mip ranges separately. Converted formats cannot use the scaled-resolve shortcut. The title enables Vulkan resolve readback by default while honoring explicit readback overrides. Host conversion tests and the release build cover the code; visual gameplay confirmation is pending the owner's test. | yes |
| `rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch` | `NtWriteFile_entry` and `NtReadFileScatter_entry` in `src/kernel/xboxkrnl/xboxkrnl_io.cpp` called neither `REXKRNL_IMPORT_TRACE` nor `REXKRNL_IMPORT_RESULT`, unlike `NtReadFile_entry` — so `--log_level=trace` showed every guest read and **no** guest write, which reads as "the title never writes" when the truth is "writes are not logged". Adds both trace points to both entry points. The write trace is what makes the persistence evidence (`[NtWriteFile] … len=0x400` while the title authors a fresh `globaloptions` payload) visible; the scatter trace is inert so far — no `NtReadFileScatter` call has been observed in any run. | yes |
| `rexglue-sdk/0018-command-processor-incomplete-packet-recovery.patch` | The primary ring buffer can be observed mid-write, because the guest publishes its write pointer as a separate store: a packet header that is readable while its payload has not landed yet. `ExecutePacketType0/Type3` report that as an overflow, and `ExecutePrimaryBuffer` then broke out of the loop and returned the write index — **discarding every remaining packet of the frame, including its `XE_SWAP`**, so nothing was presented again while the guest kept running behind a dead picture. Found finishing P0 of [../docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md](../docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md): 2 of ~22 Ultimate boots froze on the title's own "Discovering Downloadable Content" screen with `ExecutePacketType0 overflow (read count 00007C44, packet count 00007EF8)` + `**** PRIMARY RINGBUFFER: Failed to execute packet.` as the last lines. Such a packet is incomplete, not corrupt, so `ExecutePrimaryBuffer` now returns without advancing the read pointer, which makes `WorkerThreadMain` reload the write pointer and call back in; the same packet executes normally once the rest of the payload is visible. A packet still incomplete after 64 retries, or a handler that failed after consuming payload, is treated as a desync instead: the loop steps over that header and keeps executing so the rest of the frame (in particular its swap) still runs, bounded at 8 resyncs per boot before falling back to the old drop-the-ring behaviour. `ExecutePacketType1` also gains the length check its two sibling types already had — it read two payload words unconditionally, consuming the next packet's header as data. No cvar; a well-formed ring takes the same path as before. | maybe |
| `rexglue-sdk/0019-trace-frame-swaps-and-input-polls.patch` | Nothing in a vanilla log measures frame pacing or input polling: a run has no fps, frame-time or poll-rate line anywhere, and the only frame-ish number in it belongs to the payload overlay. Adds three `--log_level=trace` points — `[VdSwap]` in `VdSwap_entry` (`src/kernel/xboxkrnl/xboxkrnl_video.cpp`, every guest frame submitted, with the host-microsecond delta and the guest tick), `[XE_SWAP]` in `ExecutePacketType3_XE_SWAP` (`src/graphics/command_processor.cpp`, each frame that reached the host as a present) and `[XamInputGetState]` in `XamInputGetState_entry` (`src/kernel/xam/xam_input.cpp`, every guest poll of a pad, with the button word the guest was handed). They back the frame-pacing and input-polling measurement in [../docs/history/bringup-log.md](../docs/history/bringup-log.md). Numbered 0019 rather than 0004 because its only hunk in `src/graphics/command_processor.cpp` sits at line ~945, after 0018's: the audit concatenates a file's patch bodies in patch-name order and compares them with the live ascending diff, so a patch's number must follow its hunk position. | yes |
| `rexglue-sdk/0005-codegen-skip-stamp-and-gapfill-refinement.patch` | The three codegen defects behind B-003/B-006 in [../docs/reference/known-issues.md](../docs/reference/known-issues.md), plus the gap-fill rule that closed the last of that class (item 5, added 2026-09-29). **(1) Skipped builds.** `ManifestConfig::WriteSdkVersionStamp` (`src/codegen/manifest.cpp`) rewrote the manifest unconditionally after `RecompileProject` had already written `codegen.build.stamp`, so the manifest — a codegen input — was newest again on the next `ninja`, and the codegen edge re-ran with nothing changed (the Debug tree's stamped re-run cost ~390 s; a real Release regeneration measures 160.4 s here); it now returns early when the `sdk_version` line already matches, leaving the manifest mtime alone. `ComputeInputFingerprint` (`src/codegen/output_stamp.cpp`) also hashed the input list in caller order and with the caller's spelling, so the same tree fingerprinted differently depending on whether the manifest was named relative or absolute; inputs are now sorted and deduped by canonical key, and a `codegen=<n>` behaviour version (`kCodegenBehaviourVersion`) joins the `sdk=` line so a future analyser change can force exactly one regeneration. **(2) Missing gap-fill entries.** `splitRegionOnTerminators` (`src/codegen/phase_gapfill.cpp`) did not treat `bctr` as a terminator, and `gapFillCodeRegions` built its callable set once before the region loop and never revisited a segmentation it had already registered — a thunk slot that is only reachable indirectly and ends in `bctr`, or a boundary a later segment exposed inside an earlier coarse one, stayed interior to that segment and was never registered, so calling it trapped with `[FATAL] Call to invalid or unregistered function`. Gap filling is now a withdraw/refine fixpoint: each pass gives back the previous pass's own `GAP_FILL` registrations (never a real function), re-splits against the callables they revealed, and stops when a pass reproduces the previous segmentation (≤ 8 passes, then a warning). **(3) Dangling call targets.** Withdrawing a registration exposed a pre-existing use-after-free: `CallTarget::ToFunction` (`include/rex/codegen/function_types.h`) holds a raw `FunctionNode*`, and `FunctionGraph::removeFunction` erased the owning `unique_ptr` without touching the edges that pointed at it, so emission read `targetFn->name()` from freed memory and printed a symbol that belonged to an unrelated function, which is what the link's `undefined symbol` errors named. `removeFunction` now rewinds every affected edge back into an address-keyed unresolved jump (`FunctionNode::rewindEdgesTo`), which the graph re-resolves to whatever ends up owning that address — the re-registered function, an internal label, or an explicit `FATAL` line instead of freed memory. `tryResolveAgainst` / `tryResolveAgainstImport` also stopped promoting every re-resolved jump to a tail call, so a `bl` site no longer loses its call. **(4) Measured.** Applying the patch forces exactly one regeneration per project — the `codegen=` behaviour version in the fingerprint goes from absent to 3 (2 before the 2026-09-29 extension), so every existing stamp mismatches once, by design; after that an unchanged tree is a 0.4 s skip instead of a 160 s Release analysis (measured on this project: `0 written, 0 unchanged, 1 module(s) up to date`), and `ninja` no longer schedules the codegen edge at all because the manifest mtime is left alone. With `config/functions.toml` trimmed to its three oldest entries it also took codegen's own gap-fill discovery from 0 of the other 21 addresses to 20.; **(5) The unclaimed run** (added 2026-09-29). `splitRegionOnTerminators` proposes an entry only where a *segment starts*, and a segment whose start is an already registered address is skipped whole - so the 24-byte comparator `0x827EC038`, which follows the tail branch that ends `sub_827EBE08` and whose address is mentioned nowhere in the image (measured: no dword in the decrypted image equals it in either byte order, and no direct branch reaches it; the guest builds it in a register), stayed unregistered and the first indirect call into it trapped. Gap fill now also registers the runs of words inside a code region that no function's **extent** claims, when the word in front of the run leaves control (a return, an indirect branch, or an unconditional branch; a `bl` falls through and does not count). The extent rather than the blocks is the test: the words between a function's blocks still belong to it, and registering them as an entry shadows that function in `getFunctionContaining`'s nearest-base lookup - measured, the block-based first cut registered the dead `b` words `0x823DD6FC` and `0x823DD738` inside `sub_823DD6C0` and emission then lost the conditional branches to `0x823DD73C`. Measured with `config/functions.toml` trimmed to its three tail-branch entries: all 21 indirect-call addresses are registered and the table holds 38,457 entries against the pre-fix 38,438; those 21 entries are gone from the file, and with it emptied to a comment codegen still finds 23 of the historical 24 addresses and Validate names `0x8243C688` as the one it does not, which is why that entry stays (B-006 in [../docs/reference/known-issues.md](../docs/reference/known-issues.md)). | yes |
| `rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch` | The content manager handled one form of content item: a directory. That is what this runtime installs (`InstallContent()` extracts a package, `CreateContent()` makes one), but not what a console — or a dumped DLC folder — stores, which is the STFS container itself; a package file was therefore skipped at enumeration and, if the guest opened one by name, mounted as a directory that is not one. Upstream Xenia picks a container-backed package for a file (`ContentPackageContainer`) and a directory-backed one otherwise (`ContentPackageDirectory`), and this restores that, including reading the license bits from the container when there is no `.header` file beside it. It also adds a second, read-only content root laid out as `<root>/<title_id>/<content_type>/<name>` (no xuid level), which enumeration and opening fall back to while create/thumbnail/install/delete resolve against the writable root alone, so a bundle directory next to the game data can be served without being writable. The container probe asks `file_info.path / file_info.name`, not `file_info.path`: `rex::filesystem::ListFiles` puts the **parent** directory in `FileInfo::path` and the entry name in `FileInfo::name` (as `GetInfo` does too), so the first cut tested the directory itself and every package was rejected as "not a content package" (fixed 2026-10-03 — the DLC package then enumerated and the aggregate enumerator's item count went from 0 to 1). This is what lets `game/dlc/…` deliver Rock Band 3's DLC to Blitz — [../docs/guides/dlc.md](../docs/guides/dlc.md). **Extended 2026-10-05** with a flat library: a dumped song folder is thousands of loose containers with no `title_id`/`content_type` levels, and on a volume that cannot hold links (exFAT) it cannot be indexed into the extra root's shape, so `set_extra_content_library(std::vector<ExtraContentItem>)` takes the same packages keyed by the title id and content type their **own headers** name, and enumeration (`ListContent`) and opening (`ResolvePackagePath`) consult that list beside the extra root; nothing is copied, renamed or linked. The name the guest carries (`XCONTENT_DATA::file_name_raw`, 42 bytes) is assigned host-side (`src/fs/dlc_library.h`) because a custom library's file names are longer than the field. The same patch now also names the content type and device in `XamContentAggregateCreateEnumerator`'s item-count line, which is how a library's enumeration is read back (`added 1401 items (device=0, type=00000001)`), and it gives `OpenContent` a license fallback for those sources: a package served from the extra root or a library has no `.header` beside it, so its license was only what the container carried, and an unsigned custom `CON` carries nothing — which Blitz read as "not licensed" and used to list a custom song and then refuse to start it. `OpenContent` now answers with the configured `license_mask` (the value `XamContentGetLicenseMask` already answers with) when such a package's own mask is zero, leaving a container that does carry entries, and an explicit `license_mask = 0`, untouched. | yes |

| `rexglue-sdk/0007-xamalloc-heap-flag-word.patch` | `XamAlloc_entry` (`src/kernel/xam/xam_info.cpp`) asserted that its first parameter is zero — `assert_true(unk == 0)` — while the implementation ignores it entirely (`SystemHeapAlloc(size)` is all that happens). The parameter is not an unknown: Xenia Canary spells it `flags` in its `XamAllocImpl`, identifies bit `0x00100000` ("HEAP_ZERO_memory used unless this flag") as the only bit it knows about, reports the top four bits as selecting one of several heaps, and allocates from the system heap without acting on them either. The assert therefore only rejected **in Debug** a call a Release build completes unchecked: with the assert compiled out, this title finishes its content enumeration and the whole offline route. The patch names the parameter `flags`, drops the assert, and leaves the allocation untouched (B-013 in [../docs/history/bringup-log.md](../docs/history/bringup-log.md)). | yes |
| `rexglue-sdk/0008-xex-image-bounds-checks.patch` | A XEX header describes its own layout — the header size, an optional-header table, the security info and its page descriptors, the file format info and, for a compressed image, the block table naming the length of each chunk of image data — and the loader read through every one of them with nothing but the image's length to stop it. A truncated copy therefore ran off the end of the mapping inside the load path: the first 1 MiB of this title's recorded image (1,048,576 bytes of 9,023,488) ended the process with `0xC0000005`, the loader's own `Loading XEX image: game:\default.xex` as the last line written and no diagnostic at all (B-014 in [../docs/history/bringup-log.md](../docs/history/bringup-log.md)). The patch checks each of those claims against the image's own length before anything reads through it — for a basic-compressed image by summing the block table against the data region, which is the layout this title and the Ultimate payload's own image use — and gives the module loader (`src/system/user_module.cpp`) a word for a file too short to hold a signature and for one it cannot map. Three measured shapes, one line, no identity line and a non-zero exit each, asserted with their numbers by [scripts/acceptance/acceptance_wrong_data.ps1](../scripts/acceptance/acceptance_wrong_data.ps1): 1 MiB → `its block table describes 9011200 bytes of image data, the image holds 1036288 after its 12288-byte header`; 2 KiB → `its header claims 12288 bytes, the image holds 2048`; 0 bytes → `Failed to map … (0 bytes) for module game:\default.xex`. It is a check on the container's own arithmetic, not on the image's contents: a valid image with a different digest is still only a warning, and the boot continues. A **normal**-compressed image (`compression_type = 2`, which neither this title nor the payload's image is) describes its data as a chain in which each block names the length of the next, so it cannot be summed before it is read and keeps the SDK's original de-block loop. | yes |
| `rexglue-sdk/0009-log-levels-table-not-cvars.patch` | `cvar::ApplyTomlTable` (`src/core/cvar.cpp`) walks every table in the profile, so the SDK's own `[log.levels]` convention produced a pending cvar named `log_levels_<category>` for every per-category log level. The logging subsystem reads that table straight out of the file (`ParseCategoryLevelsFromConfig` in `src/core/logging.cpp`) and never registers those keys, so they sat in the pending-value store for the whole run and would be named by `FinalizeInit`'s "unknown cvar" report alongside real typos — which made the report useless for its purpose. `[log.levels]` is now left to the logging subsystem. With the report trustworthy, this project finalizes the registry last, in `RbBlitzApp::OnPreLaunchModule`, so a misspelled key in `rb_blitz.toml` (or an unmatched `--flag`) is logged instead of silently dropped (the limit recorded in [../docs/reference/known-issues.md](../docs/reference/known-issues.md) on 2026-10-03). | yes |
| `rexglue-sdk/0010-input-system-state-filter.patch` | `InputSystem::GetState` merges every device assigned to a user, and the merge only ever *adds* input — so a title that wants to rebind the pad cannot do it with a driver, and the pad the guest is handed is the SDK's own translation with no seam over it. Adds `InputSystem::SetStateFilter`, a callback run on the merged state just before it is handed back, so a title can rewrite what the guest sees (used by [../src/input/remap.cpp](../src/input/remap.cpp) for Controller Settings' `[remap]` table, D17 in [../docs/plans/finished/2026-09-27-launcher-plan.md](../docs/plans/finished/2026-09-27-launcher-plan.md)). Ten lines: one `std::function` member, one setter, one call. Not a defect fix, so an upstream candidate only if the same need is general enough to be wanted there. | maybe |
| `rexglue-sdk/0011-deferred-overlapped-delay-cvar.patch` | `KernelState::CompleteOverlappedDeferredEx` slept a hard-coded 100 ms (`constexpr kDeferredOverlappedDelayMillis`) inside every deferred overlapped completion, to make the console's asynchronous storage look asynchronous. That is per completion, and a title that mounts a large content library pays it once per package: measured on a 1402-package library, `XamContentCreate` (which defers the whole mount through this path, `src/kernel/xam/xam_content.cpp`) is entered 2803 times on the way to the main menu, so 280 s of the 667 s the title's own "Discovering Downloadable Content" screen takes. The constant becomes the cvar `deferred_overlapped_delay_ms` (default **100**, the faithful value; a `uint32_t` in the `Kernel` category, hot-reload, and declared in `include/rex/system/flags.h` for a host to write through), read per completion and skipped entirely at 0. [../src/hooks/dlc.cpp](../src/hooks/dlc.cpp) sets it to 0 when R7's `enhancements_dlc_cache` toggle is on, because the cache alone answers for only the host's walk of the library and not for the per-package wait the toggle promises to remove. Nothing else changes: the completion still runs on the dispatch thread, still sets the overlapped, still queues the completion routine, so only the wait goes. Not a defect fix — the emulated latency is a deliberate choice — so an upstream candidate only if a settable delay is wanted there. | maybe |
| `rexglue-sdk/0012-stale-native-object-handles.patch` | `XObject::GetNativeObject` finds a guest-initialised object again through the handle it stashed in that object's dispatch header (`wait_list_flink` = `kXObjSignature`, `wait_list_blink` = the handle). Two things were wrong with trusting that handle unconditionally, and together they were a use-after-free rather than a mis-read (B-016 in [../docs/reference/known-issues.md](../docs/reference/known-issues.md), found while measuring a V-Sync-off boot): **(1)** nothing checked the *type* the header claims against the object the handle names - upstream's own `TODO: assert if the type of the object != as_type` - and handle slots are reused, so a structure the guest keeps using after the object behind it is gone resolves to whatever object holds the slot by then (measured: an `XThread` and an `XEnumerator`, 6 ms after the event that owned it was destroyed), which the typed entry points then read through a blind cast (`ev->Set(...)` is a direct call to `XEvent::Set`, so `+0x68` of a thread was read as its `event_` - that is the `event_ = 0xFFFFFFFFFFFFFFFF` of the two minidumps); and **(2)** `ObDereferenceObject_entry` (`src/kernel/xboxkrnl/xboxkrnl_ob.cpp`) resolved its pointer through `GetNativeObject`, which *creates* an object for a structure it has never seen, and then called `ReleaseHandle()` - the inverse of a reference this SDK hands out, and one it had not handed out here - dropping the creation's only handle reference, removing the object from the object table and letting it be deleted with the signature and handle still stashed in the guest's own KEVENT. Adds `XObject::LookupNativeObject` (follow the stashed handle only while it names an object of the type the header claims; never create), makes `GetNativeObject` treat an unfollowable handle exactly like a structure that was never initialised, checks the caller's type expectation in `GetNativeObject<T>` on top of the header's, and uses the lookup in `ObDereferenceObject`. Anomalies are logged (rate-limited `object: ...` warns) instead of faulting. **Standing limit:** a handle recycled by an object of the same type is still followed, because no pointer-to-object link is recorded for structures the guest initialises itself. | yes |
| `rexglue-sdk/0013-cvar-config-source.patch` | The registry could apply a value from the command line (`SetFlagFromCommandLine`, `Source::kCommandLine`), from the environment, or from a TOML file (`LoadConfig`, `Source::kConfig`) — but not from a *configuration-shaped* source an application supplies itself. Controller Settings' profile is exactly that: its `[settings]` table is a flat list of cvar names and values, so `LoadConfig` cannot read it (it is a table under `[settings]`, not the top level), and applying it from the command line would put it above the game's own `rb_blitz.toml` and take that file's precedence away (D3). Adds `rex::cvar::SetFlagFromConfig(name, value)`, which applies at `Source::kConfig` with the same reject-on-bad-value contract as `SetFlagFromCommandLine`. Backs D3 rank 4: [../src/profile/profile_apply.cpp](../src/profile/profile_apply.cpp) applies the profile's rows at the top of `OnConfigurePaths`, so the SDK's own `LoadConfig` of the game's file runs later and still outranks it at equal rank. | maybe |
| `rexglue-sdk/0014-resolution-preset-window-pixels.patch` | The resolution preset sizes the startup window as well as the guest video mode, but `ResolveWindowWidth`/`ResolveWindowHeight` (`src/ui/window_sdl.cpp`) returned the preset's *pixel* count as a *logical* size, which `OpenImpl` then multiplies by the display scale (`SizeToPhysical`). On a 300%-scaled display "1080p" therefore created a 5760×3240 window on a 3840×2160 screen — larger than the screen, so the picture read as zoomed in and cut at the edges. Divides a pixel count (the preset, and `video_mode_width`/`_height`, which are pixel counts too) by the primary display's content scale, the same scale `GetLatestDpiImpl` uses before the window exists, so the created window really is the requested number of pixels; at a scale of 1 it is the identity. `window_width`/`window_height` are documented as logical and are unchanged. | yes |
| `rexglue-sdk/0016-borderless-window-mode.patch` | The SDK has two window states, windowed and `fullscreen` (SDL desktop fullscreen), and no third one — an undecorated window that covers the display while staying an ordinary window to alt-tab, task switching and other windows. Found while cutting Controller Settings' graphics rows over to the in-game Display row (`graphics.display`, [../docs/settings/status.md (part 1, current status)](../docs/settings/status.md)): the row offers Windowed / Borderless / Full Screen and drops the two keys that express it, `fullscreen` and `borderless`, so the middle choice needs a runtime that can draw one. Adds the `borderless` cvar (`UI/Window`, default false, `kRequiresRestart`; declared in `include/rex/ui/flags.h`) and, in `WindowSDL::OpenImpl`, an `else if (REXCVAR_GET(borderless))` arm after the fullscreen one that clears the decorations (`SDL_SetWindowBordered(false)`) and sizes and positions the window to the display's own bounds (`SDL_GetDisplayForWindow` → `SDL_GetDisplayBounds`). The two are deliberately separate mechanisms: `fullscreen` still means SDL fullscreen, and borderless never enters that state. Applying the row is [../src/settings/cvars.cpp](../src/settings/cvars.cpp)'s `DisplayToCvars`, which maps the token to the pair (`borderless` → `fullscreen=false, borderless=true`; `windowed` → `fullscreen=false`; `fullscreen` → `fullscreen=true`), and the boot decodes the pair back into the token for the row's own display. Upstream candidate only if a third window mode is wanted there. | maybe |
| `rexglue-sdk/0017-fixed-size-window.patch` | The SDK had no way for an application to decline the *user's* resize handle: `WindowSDL::OpenImpl` passed `SDL_WINDOW_RESIZABLE` unconditionally, so the only lever was the window *size*, and a drag could leave any shape at all. Found fixing the installed game's window ([../docs/plans/finished/2026-10-14-installed-game-fix-prompts.md](../docs/plans/finished/2026-10-14-installed-game-fix-prompts.md) P2): rb_blitz decides its guest frame, the SDK draw scale and the presenter's output region from one resolution row *before* the window exists, and its windowed client area has to keep that aspect or the picture is squashed/letterboxed. Adds the `window_resizable` cvar (`UI/Window`, default **true** = SDL's own default and the SDK's unchanged behaviour; `kRequiresRestart`; declared in `include/rex/ui/flags.h`, defined in `src/ui/window.cpp`) and applies it in `WindowSDL::OpenImpl` with `SDL_SetWindowResizable(window, false)` — SDL exposes resizability separately from both the creation size and the fullscreen state, and the call site is beside the other desired-state applies, just before the window is shown. Not a defect fix upstream (nothing there asks for it), and the separate Controller Settings utility builds its own SDL window and never reads the cvar. | maybe |
| `rexglue-sdk/0021-preload-completion-accounting.patch` | A shader-storage preload could report success it had not achieved. In `src/graphics/d3d12/pipeline_cache.cpp` the growth loop compared `creation_threads_.size()` against `creation_thread_original_count` — the value it had just read from that same size — so a preload needing no extra worker (one stored description, or a pool already at the computed count) never entered the block holding the blocking wait, and `InitializeShaderStorage(..., true)` returned with creation threads still busy; the success line counted `++pipelines_created` per *scheduled* entry regardless of whether its `ID3D12PipelineState` was ever created. Adds `include/rex/graphics/pipeline_preload.h`: `PipelinePreloadTracker` (one `queued → running → succeeded/failed`, or `missing`, per identity; duplicate registration refused; `WaitUntilFinished`/`Reset`) and `PipelinePreloadResult` (`Complete()` = has work, nothing unfinished, nothing failed or missing — an empty preload is not coverage), plus `PreloadCreationThreadCount()` for the growth arithmetic. Both caches register every stored description, mark success only from a real pipeline (`state != nullptr` / a non-placeholder `VkPipeline`), and count retained-placeholder recovery (`Pipeline::real_pipeline_failed`) as a failure; `InitializeShaderStorage` returns the result and logs it, with a warning when required work did not produce every pipeline. D3D12's barrier is the tracker's, outside the temporary-worker cleanup; `EndSubmission()` waits are untouched. | yes — the growth condition and the skipped barrier are defects; the tracker is a design addition |

`rexglue-sdk/0022-vulkan-gpu-texture-conversion.patch` replaces 0002's Vulkan CPU
conversion with a compute shader reading current GPU shared memory. It reuses
completed device-local buffers and creates its pipeline at device initialization.
The application no longer automatically enables Vulkan resolve readback. D3D12
and explicit readback overrides retain their behavior. See
[Vulkan performance](../docs/engine/vulkan-performance.md) for validation and limits.
When a later patch replaces an earlier hunk, the checker accepts the earlier
patch only if every file it touches matches the final pristine-HEAD patch replay.

`rexglue-sdk/0023-vulkan-texture-descriptor-cache.patch` avoids reallocating and
rewriting identical texture/sampler descriptor sets within a command submission.
The cache is bounded, preserves the existing allocator's retirement rules, and
clears before submission/frame/pool reuse. Set `vulkan_texture_descriptor_cache`
to false for an A/B comparison. Host tests exercise the actual cache/key; gameplay
timings remain to be measured. See [Vulkan performance](../docs/engine/vulkan-performance.md).

`rexglue-sdk/0024-vulkan-descriptor-binding-reuse.patch` preserves valid descriptor
sets that are already bound when a cache lookup returns the same immutable set.
It also applies the computed pipeline-layout compatibility prefix and uses
`vkGetFenceStatus` for nonblocking polls. Required bindings and blocking waits
remain in place. Host tests cover the state helper, including layout and command
buffer invalidation; see [Vulkan performance](../docs/engine/vulkan-performance.md).

`rexglue-sdk/0025-d3d12-fence-and-resolve-readback.patch` fixes DX12's
out-of-submission signal/arm/wait sequence: `S_OK == 0` previously short-circuited
event arming inside a boolean HRESULT expression, then entered an infinite wait.
It checks signal/arm/wait errors and device loss, protects resolve CPU copies with
producing-submission completion/size checks, and defers release of in-flight
readback slots. The new headless DX12 test exercises production sequencing and
GPU readback. See [DX12 loading freeze](../docs/engine/d3d12-loading-freeze.md) for
the reported run, validation and the limits of the diagnosis.

`rexglue-sdk/0026-helper-pipeline-key-identity.patch` gives every helper pipeline
creation a real identity in `shader_compile_trace`. Before it, the shared
`CreateComputePipeline` utilities and the render-target, presenter and swap-chain
creators logged the placeholder `key=0`, so one trace site could not be told apart
from another and the P3 catalogue could only describe those recipes by call site. The
`HelperKind` enum (`include/rex/graphics/compile_trace.h`) names the creator in the
key's upper byte and the caller's own packed key sits below it (`TransferShaderKey`,
`DumpPipelineKey`, `ResolveCopyShaderIndex`, sample counts, paint effect, texture load
shader index, and so on); masked-sample and stencil-bit derivatives use the trace's
`variant` field, and `compile_trace::Call` gained that argument to carry them. The
change is instrumentation only — no creation, submission or synchronization decision
reads a key. See the [P3 record](../docs/engine/shader-cache.md) (part 3) and
[`config/pipeline_helper_inventory.json`](../config/pipeline_helper_inventory.json).

`rexglue-sdk/0027-graphics-preparation-lifecycle.patch` makes persistent
shader/pipeline preparation a cancellable generation instead of one blocking call.
`include/rex/graphics/graphics_preparation.h` adds `GraphicsPreparationPhase`,
`GraphicsPreparationKey`, `GraphicsPreparationProgress` and
`GraphicsPreparationController`; `pipeline_preload.h` gains a terminal *cancelled*
state and `CancelPending()`, so a cancelled generation can never report `Complete()`.
The base `CommandProcessor` owns the controller, starts a generation from any thread
(the import is scheduled with `CallInThread`) and services it in bounded slices from
its worker loop — between packets and while the ring is idle — so the loading UI keeps
polling and presenting. Both pipeline caches are split into begin/service/cancel
around their existing import, translation and creation code, and each gets a
**separate preparation creation pool and queue**, so `EndSubmission()` never waits for
preparation and preparation never consumes a pipeline a draw needs first.
`InitializeShaderStorage(..., blocking)` stays as a wrapper with the P1 result
contract; `IGraphicsSystem`/`GraphicsSystem` expose begin/cancel/query/wait, and
`rex_app.cpp` starts the generation after guest resume when
`shader_storage_async_preparation` is on (default off — the visible loading phase is
P6's). See the
[P4 record](../docs/plans/finished/2026-10-10-shader-cache-plan.md#p4--add-asynchronous-preparation-without-starving-the-loading-screen)
and the [shader-cache record](../docs/engine/shader-cache.md) (part 4).

`rexglue-sdk/0028-native-pipeline-caches.patch` persists the driver's own pipeline
results beside the shareable recipes. `include/rex/graphics/native_pipeline_cache.h`
adds `NativeAdapterIdentity`/`NativePipelineCacheIdentity` (backend, title, rendering
path, container schema, `PipelineDescription::kVersion`, translator version, adapter
descriptor, driver version and Vulkan `pipelineCacheUUID`) and a header-only
`NativePipelineCacheStore` whose entry files carry magic, format, identity hash, key,
payload hash and payload size; anything unexpected is refused instead of being handed to
a driver, writes are a temp file plus one rename, and both pending and on-disk sizes are
bounded. D3D12's `PipelineCache` loads a stored `ID3D12PipelineState` cached blob into
`D3D12_GRAPHICS_PIPELINE_STATE_DESC::CachedPSO`, forgets and retries without it when the
driver rejects it, and stages `GetCachedBlob()` for later. Vulkan creates one real
`VkPipelineCache` from validated data, publishes it on
`VulkanDevice::shared_pipeline_cache()` so every creation path (guest, render-target,
swap effects, presenter, immediate drawer, `util::CreateComputePipeline`) uses it, and
merges the on-disk data of another instance before serializing. Both backends flush only
at a preparation boundary and at shutdown; `native_pipeline_cache` (default on) disables
the layer. See the
[P5 record](../docs/plans/finished/2026-10-10-shader-cache-plan.md#p5--persist-native-results-for-faster-subsequent-boots-on-both-backends)
and the [shader-cache record](../docs/engine/shader-cache.md) (part 5).

## Apply / verify

`0076-ios-presentation-diagnostics.patch` adds bounded iOS-only records of the
guest output dimensions, paint effects, final draw rectangle and queue-present
result. It supports investigation of the owner's A18 Pro black-screen report;
it changes no rendering decisions. The app also saves its first non-black
guest GPU image and records UIKit view visibility in the shared diagnostics
folder. Device acceptance remains open.

```powershell
# Apply every patch set to the pinned SDK checkout (idempotent; safe to re-run).
# `-ExecutionPolicy Bypass` is required on this machine, where the policy is
# Restricted and a bare `.\scripts\...ps1` fails with UnauthorizedAccess.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\apply_sdk_patches.ps1

# Same, without touching the working tree
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\apply_sdk_patches.ps1 -Check
```

The script reports each patch as *already applied*, *applied now*, or *failed*,
and warns if the SDK checkout no longer matches the gitlink recorded in the
parent commit. Manual equivalent, from the repository root:

```powershell
# Use absolute paths: `git -C <dir>` resolves relative ones against <dir>.
$p = (Resolve-Path patches\rexglue-sdk\0001-xthread-log-once-core-count-warning.patch).Path

git -C rexglue-sdk apply --reverse --check $p   # exit 0 => already applied
git -C rexglue-sdk apply --check         $p     # exit 0 => applies cleanly
git -C rexglue-sdk apply                 $p     # apply it
```

The patches are intentionally **not** applied automatically by the build: apply
them once after a fresh submodule checkout.

## Why the parent repository stays clean

`.gitmodules` marks the submodule:

```ini
[submodule "rexglue-sdk"]
	ignore = dirty
```

so an applied patch no longer shows up as a modified submodule forever. `git
status`, `git diff` and `git status --porcelain=v2` (what editor SCM panels use)
in the parent report nothing for the SDK, while the submodule itself still shows
` M` on the files the patch set touches (`src/system/xthread.cpp` for 0001;
`include/rex/graphics/d3d12/shared_memory.h`,
`include/rex/graphics/d3d12/texture_cache.h`,
`include/rex/graphics/pipeline/texture/cache.h`,
`src/graphics/d3d12/texture_cache.cpp`, `src/graphics/pipeline/texture/cache.cpp`,
`include/rex/ui/graphics_upload_buffer_pool.h`,
`include/rex/ui/d3d12/d3d12_upload_buffer_pool.h`,
`src/ui/graphics_upload_buffer_pool.cpp` and
`src/ui/d3d12/d3d12_upload_buffer_pool.cpp` for 0002; `src/kernel/xboxkrnl/xboxkrnl_io.cpp` for 0003;
`src/kernel/xboxkrnl/xboxkrnl_video.cpp`, `src/graphics/command_processor.cpp`
and `src/kernel/xam/xam_input.cpp` for 0019; `src/codegen/manifest.cpp`,
`src/codegen/output_stamp.cpp`, `src/codegen/phase_gapfill.cpp`,
`src/codegen/function_graph.cpp` and `include/rex/codegen/function_node.h` for
0005; `include/rex/system/xam/content_manager.h`,
`src/system/xam/content_manager.cpp` and `src/kernel/xam/xam_content_aggregate.cpp`
for 0006; `src/kernel/xam/xam_info.cpp` for
0007; `src/system/xex_module.cpp` and `src/system/user_module.cpp` for 0008; `src/core/cvar.cpp` for 0009;
`include/rex/input/input_system.h` and `src/input/input_system.cpp` for 0010;
`include/rex/system/flags.h` and `src/system/kernel_state.cpp` for 0011);
`include/rex/system/xobject.h`, `src/system/xobject.cpp` and
`src/kernel/xboxkrnl/xboxkrnl_ob.cpp` for 0012; `include/rex/cvar.h` and
`src/core/cvar.cpp` for 0013 — the second patch to touch `src/core/cvar.cpp`
after 0009, which is why `apply_sdk_patches.ps1`'s audit compares hunk *bodies*
per file and unions a file's hunks across the whole patch set; `src/ui/window_sdl.cpp`
for 0014).

`src/ui/window_sdl.cpp` is touched by **three** patches (0014, 0016 and 0017). The union
above is why that works, but it also means the patch that is *lower in the file* must be the
one with the higher number, and that two patches' hunks for one file must not be close enough
for the live `git diff` to merge them into one hunk — the merged body then stops matching the
per-patch concatenation. Measured 2026-10-14 while adding 0017: a hunk three lines after
0016's made `include/rex/ui/flags.h` read as UNEXPECTED until it was moved to the far side of
the `// Display` group (the same reason 0017's `window_sdl.cpp` change sits beside the other
desired-state applies rather than in the window's flag list).

`ignore = dirty` hides work-tree edits only. A **moved gitlink is still
reported**: verified 2026-09-19 by pointing the index entry at a different
commit, which produced `MM rexglue-sdk`. That is the case that matters — a pin
bump must never be silent.

Because the setting also hides *accidental* SDK edits, `apply_sdk_patches.ps1`
audits the work tree on every run: each modified file must match a patch here
(the `index` line is ignored, since its abbreviation length follows
`core.abbrev`). Anything else is listed as `UNEXPECTED` and the script exits 1.

```
SDK work tree      : 33 patched, 0 UNEXPECTED, 0 untracked
    patched    : include/rex/codegen/function_node.h
    patched    : include/rex/graphics/d3d12/shared_memory.h
    patched    : include/rex/graphics/d3d12/texture_cache.h
    patched    : include/rex/graphics/pipeline/texture/cache.h
    patched    : include/rex/input/input_system.h
    patched    : include/rex/system/flags.h
    patched    : include/rex/system/xam/content_manager.h
    patched    : include/rex/system/xobject.h
    patched    : include/rex/ui/d3d12/d3d12_upload_buffer_pool.h
    patched    : include/rex/ui/graphics_upload_buffer_pool.h
    patched    : src/codegen/function_graph.cpp
    patched    : src/codegen/manifest.cpp
    patched    : src/codegen/output_stamp.cpp
    patched    : src/codegen/phase_gapfill.cpp
    patched    : src/core/cvar.cpp
    patched    : src/graphics/command_processor.cpp
    patched    : src/graphics/d3d12/texture_cache.cpp
    patched    : src/graphics/pipeline/texture/cache.cpp
    patched    : src/input/input_system.cpp
    patched    : src/kernel/xam/xam_content_aggregate.cpp
    patched    : src/kernel/xam/xam_info.cpp
    patched    : src/kernel/xam/xam_input.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_io.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_ob.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_video.cpp
    patched    : src/system/kernel_state.cpp
    patched    : src/system/user_module.cpp
    patched    : src/system/xam/content_manager.cpp
    patched    : src/system/xex_module.cpp
    patched    : src/system/xobject.cpp
    patched    : src/system/xthread.cpp
    patched    : src/ui/d3d12/d3d12_upload_buffer_pool.cpp
    patched    : src/ui/graphics_upload_buffer_pool.cpp
```

Untracked files are listed for information only, not treated as drift. To bypass
the setting and see the raw state:

```powershell
git -C rexglue-sdk status --porcelain
```

When a patch lands upstream, drop the file here and bump the submodule pin.

## Making the SDK tree sane on Windows

An applied patch is not the only reason the SDK submodule can look dirty. Sixteen
tracked entries under `rexglue-sdk/thirdparty/` are symbolic links upstream:

| Repo | Entries | If flattened |
| --- | --- | --- |
| `libmspack` | 15 — `cabextract/mspack/*.c`, `*.h` and `ChangeLog`, each pointing into the real source tree of the same repo (e.g. `cabextract/mspack/lzxd.c` → `../../libmspack/mspack/lzxd.c`) | **Build-breaking.** `rexglue-sdk/thirdparty/CMakeLists.txt` compiles `cabextract/mspack/lzxd.c`, which becomes 29 bytes of link text instead of 30,796 bytes of source. |
| `o1heap` | 1 — `CLAUDE.md` → `AGENTS.md` | Documentation only. |

This machine cannot create symlinks: the shell is not elevated, Windows Developer
Mode is off, and both `New-Item -ItemType SymbolicLink` and `cmd mklink` fail. The
`rexglue-sdk`, `libmspack` and `o1heap` repos carry no local `core.symlinks`
setting, so they inherit `false` from the system gitconfig
(`C:/Program Files/Git/etc/gitconfig`) and a checkout writes the *link text* into a
plain file.

Do **not** "fix" this by setting `core.symlinks true` in a submodule. Git then
calls `symlink()`, which is unavailable here: the checkout fails with
`error: unable to create symlink cabextract/mspack/lzxd.c: Function not
implemented`, exits 255, and leaves the file **missing** — worse than the flattened
stub. (Verified 2026-09-19; the root repo's `core.symlinks=true`, left over from
B-001, only affects the root repo, which contains no symlinks.)

Expand the flattened files from their targets instead:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\repair_flat_symlinks.ps1 -NoIndexMarks
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\repair_flat_symlinks.ps1 -ClearMarks
```

`repair_flat_symlinks.ps1` walks every submodule (and the root repo), follows each
index entry with mode `120000`, and copies the target's content over the flattened
file only when the content differs — so it is idempotent and safe to re-run. A
healthy tree prints `content already correct : 16` and
`repaired from target    : 0`. It then marks exactly those paths
`--skip-worktree`, because the real difference (a type change, `120000` →
`100644`) cannot be resolved on this machine and would otherwise show as permanent
dirt. Use `-ClearMarks` before editing libmspack sources by hand if you want to see
your edits in `git status`, and `-NoIndexMarks` if you do not want the index
touched at all.

After both scripts, `git -C rexglue-sdk status --porcelain` should show only the
files named in the patch set (the files the eight rows above name). The parent should be
clean: `.gitmodules`
sets `submodule.rexglue-sdk.ignore = dirty`, so the applied patches are no longer
reported as a modified submodule, and `apply_sdk_patches.ps1` audits whatever that
setting hides. Re-run the scripts after `git submodule update`, a fresh clone, or
any checkout that rewrites the SDK tree. Background:
[../docs/history/bringup-log.md](../docs/history/bringup-log.md) B-001 and
[../docs/reference/known-issues.md](../docs/reference/known-issues.md).

`rexglue-sdk/0029-application-loading-preparation.patch` adds the application hook
`DeferShaderStoragePreparation`, retaining the SDK default while allowing this title
to start bulk work after A at its native content-ready boundary. Both backend begin
operations close the prior terminal generation's writer before reopening storage.
`0030-required-loading-payload.patch` adds `CanLaunchModule`: a required authored gate
that could not be built refuses guest launch. See [the P6 record](../docs/engine/shader-cache.md) (part 6).

`0031-audio-loading-scheduling.patch` raises the Audio Worker/XMA Decoder priority,
limits lower-priority preparation pools to four workers with CPU headroom, and keeps
async driver pipeline creation off the command-processor submission thread.
`0032-bounded-audio-device-handoff.patch` replaces SDL's allocated frame queue with
an owned fixed-capacity PCM ring, shortens its mutex scope to copies, and adds
rate-limited starvation/callback-gap diagnostics. It retains the configured output
queue depth. See [menu audio stability](../docs/engine/main-menu.md) part 2 §6.

`0033-vulkan-presenter-reuse-paint-pipeline.patch` fixes an upstream Vulkan defect
found by P8's Vulkan row: `VulkanPresenter::PaintContext::GuestOutputPaintPipeline`
stores the swapchain format its cached pipeline was built for, and the paint path
compares that field with the current render pass format to decide whether the
pipeline can be reused — but **nothing ever wrote it**, so the comparison was true
on every paint. Each present therefore awaited the previous paint submission,
destroyed the pipeline and rebuilt it, including the `vkCreateGraphicsPipelines`
call P0's trace counts as a helper: **2,026 creations of a single identity in one
run**, 1,473 of them inside an 86.6 s song (209 ms, worst call 818 µs). The patch
records the format where the pipeline is created, so the cached pipeline is reused
until the render pass really is rebuilt for another format. Writing it before the
create call is safe because a failed creation leaves the handle null and the paint
path re-tests the handle before trusting the format. Upstream candidate: yes — the
field's only reader is that comparison, so the missing writer is a plain bug, not a
design choice. Verified in-game on the first boot carrying it: in-song helper traffic
went from **1,473 calls / 209 ms to 1 call / 108 us** (`p8-vulkan-ultimate-fbo-cold-r2`).
See [the P8 record](../docs/engine/shader-cache.md) (part 8).

`0034-nonblocking-async-pipeline-submission.patch` removes runtime pipeline queue
draining and worker waits from submission in async mode on both backends. D3D12
already skips draws with a null PSO. Vulkan keeps its pending-draw fallback,
defaults whole-frame suppression off, and publishes completed handle/layout pairs
on the command processor at submission boundaries. Old handles remain alive until
the relevant GPU submission completes; shutdown joins workers and drains the last
completions. Primary-buffer submission no longer depends on workers being idle in
async mode. Explicit synchronous mode and the loading-preparation barriers retain
their waits. Upstream candidate: the submission wait defeats the existing async
fallback; changing the default incomplete-frame presentation policy is a deliberate
product choice. Host tests cover the actual completion queue with delayed and
concurrent workers. Gameplay verification and synchronous first-use costs remain
open; see [the P8 record](../docs/engine/shader-cache.md) (part 8).

`0035-borderless-hotkey-presentation-recovery.patch` adds the missing Alt+Enter
Windowed/Borderless toggle, preserves windowed placement across desktop modes,
and prevents borderless resize events from replacing the desired windowed size.
Focus regain refreshes the existing presentation connection even without a size
change; D3D12 mode-change status also requests recovery instead of being treated
as ordinary success. Window mode and focus recovery are logged. Host tests cover
the production key handler without opening a window. Capture slowdown recovery
still needs runtime acceptance; see [display modes](../docs/engine/display-modes.md).
Upstream candidate: the missing shortcut and DXGI status handling are general;
unconditional focus-return reconnection is a defensive policy needing runtime
validation.


`0040-device-open-file-accounting.patch` adds a per-device native File lifetime
counter. Base File construction retains one count and base destruction releases it,
after the derived file has closed its resources. The rolling menu-music cache uses
zero only after stopping new opens and excluding active/pending/captured sources,
so guest handles still retaining a File prevent device eviction. It does not change
the VFS's general unregister policy. Host tests open two real HostPath files and
exercise the production cache's pins, residency bound and deferred retirement.
Upstream candidate: resource accounting is general; eviction policy stays in the title.


`0041-media-keys.patch` delivers SDL Media Next Track, Previous Track and Play/Pause
as the existing VirtualKey values. The title's latch/capture and menu-music navigator
use them through ordinary keyboard/focus gates. No global hotkeys or playback engine
are added. Upstream candidate: missing media-key translation is general. Windows
delivery/arbitration still requires live acceptance.


`0042-imgui-image-textures.patch` exposes drawer-owned bounded RGBA image textures for host UI.

`0043-texture-override.patch` is the P1 seam for the
[controller layout plan](../docs/plans/finished/2026-10-19-button-layout-hotswap-plan.md).
The provider gets a read-only view of the original guest base level to identify a
texture. It returns the same block-compressed host format in untiled, unswapped
order. This matches how both backends already keep native BC3 textures and avoids
changing their resource/view formats. The project can decode an authored PNG to
RGBA and encode its replacement to BC3 on the host; the guest's DXT5 bytes stay
untouched. The cache invalidates only previously observed candidates when the
provider bumps its generation, so ordinary textures stay resident. The P1 probe
currently covers the base level of 2D DXT5 textures on BC3-capable devices,
preserving the guest's mip levels; the live
menu/pad capture and switch-cost measurement remain acceptance gates.
`CreateImageTexture` uses the existing D3D12/Vulkan ImmediateDrawer texture API; IDs are consumed
by the existing ImGui draw-list renderer. `ReleaseImageTexture` requires the drawer generation;
replacement clears textures and increments that generation. Both backends already defer in-use
resource destruction behind submission fences. Calls must run on the UI/draw thread. Limits:
eight images, at most 512×512 each. The menu-music player keeps one 256×256 cover and rebuilds it
from owned CPU pixels after replacement. Host tests/build and exact patch replay are covered;
real image rendering and reset/soak acceptance await the owner.


`0074-ios-runtime.patch` adds the experimental iOS arm64 platform, native host
codegen import, UIKit SDL startup and event pumping, signed-framework discovery
for GPU/MoltenVK, and sandbox-compatible guest memory backing/protection queries.
Guest instructions remain data; generated native functions run from signed code.
The existing Darwin fiber APIs compile/link against the device SDK. Patch replay
and device source compilation are checked; actual VM aliases, memory pressure,
fibers and gameplay remain unverified. See the
[iOS CLI plan](../docs/plans/finished/2026-11-16-ios-cli-installer-plan.md).


`0075-table-function-registration.patch` reuses the generated image's existing
sentinel-terminated function mapping table for registration. Its filter and
order match the old straight-line template: all 38,457 entries matched in the
2026-11-16 device-build input. This avoids a giant registration function that
kept AppleClang busy for minutes even without optimization; the equivalent
loop compiled for iOS arm64 at `-O3` in 2.06 seconds. The registration calls and
gameplay function pointers are unchanged. See the
[iOS CLI plan](../docs/plans/finished/2026-11-16-ios-cli-installer-plan.md).

`0080-ios-resume-swapchain-retention.patch` retains a same-size iOS Metal
swapchain on focus and restore, matching the existing macOS focus policy.
The 2026-11-16 device stall log recreated it twice immediately after resume,
then stopped producing guest frames while audio repeated. Genuine surface
resizes and Vulkan out-of-date results still reconnect. Patch replay and host
lifecycle checks cover the change; device resume acceptance remains pending.
