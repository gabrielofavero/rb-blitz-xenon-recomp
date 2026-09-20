# VR port plan — Meta Quest, true stereo

Scoping for a **stereo, head-tracked** Meta Quest build of this title: what it
takes, what it costs, and what has to be *proven* before each step is worth
paying for.

**Status: scoping only.** No VR code exists in this tree, nothing in
[DECOMPILATION_PLAN.md](../DECOMPILATION_PLAN.md) changes, and this file does
not gate any Windows milestone. It exists because the question "is this viable"
has a concrete answer now and the answer is worth not rediscovering later.

Convention used below: **[tree]** = read directly in this repository or the
pinned SDK tree; **[cited]** = quoted from an upstream primary source (spec
text, vendor documentation, upstream code) in the research pass that produced
this plan, where the quote is recorded but the source has not been re-read here;
**[assumed]** = engineering judgement with no source behind it.

Precondition: milestone 4 closed and milestone 5 measured (see the status
paragraph in [README.md](../README.md)). Every VR milestone needs the Windows
build to be reproducible, so this work starts after that, not beside it.

[DECOMPILATION_PLAN.md](../DECOMPILATION_PLAN.md) §"Deferred until after
"working"" already excludes non-Windows hosts, and its packaging line covers
installers, auto-update and ARM. VR is a further scope addition on top of that,
which is why the milestones here are numbered `V0…V5` — they cannot be mistaken
for Windows milestones 4 and 5, and the reference to that section now points
here so the two documents stay in step.

## 1. Verdict

Viable as a real project, not research. Two things define it:

- **One hard dependency.** The title's camera has to be a discoverable
  view-projection matrix in the guest's vertex float constants. If it is not,
  per-eye rendering degrades into shader-translation work (much larger) or
  depth reprojection (much worse quality). That is V0, and it is startable
  today on Windows with no headset.
- **One distribution reality.** Developer-mode sideload is the supported route,
  not the Meta Horizon Store — a conclusion that comes from platform policy and
  from the absence of any precedent, not from pessimism. See §7.

The mechanism (patching the guest's own camera constants so the *guest* renders
both eyes) is established: CitraVR does exactly this on a much less tractable
guest, and it ships without any game data — the same posture this repository
already takes.

## 2. Precedents: what transfers and what does not

| Project | Mechanism | Transfers here? |
| --- | --- | --- |
| [UEVR](https://github.com/praydog/UEVR) (UE4/UE5 + OpenXR) **[cited]** | Activates the *engine's* dormant stereo path; Unreal reports 2 views, renders both eyes into one double-wide target, submits two `XrCompositionLayerProjectionView`s. | **No.** There is no engine here to hold an unused stereo path. UEVR's own fallbacks (alternating-eye AFR, depth reprojection) are documented as inferior and are the parts that would transfer — which is an argument against copying it. |
| [CitraVR](https://github.com/amwatson/CitraVR) (3DS, GPL) **[cited]** | "Engineless OpenXR application": hooks the guest GPU's vertex-shader uniform block immediately after the guest loads its own registers (`ApplyVRDataToPicaVSUniforms`) and writes head pose + half-IPD into the *discovered* view-matrix register. | **Yes — this is the model.** It validates three things at once: patch guest GPU constants instead of replacing guest rendering; find the camera registers automatically instead of by hand-RE; ship VR with no bundled game data. |
| Dolphin VR (`VR-Hydra`), PrimedGun **[cited]** | Inject per-eye matrices into the *guest's* transform constants, layer eyes with geometry shaders (`gl_Layer = eye`) or two passes. | **Partly.** Same constant-patching idea, but the geometry-shader dependency is unavailable on Metal (MoltenVK) and unnecessary on Quest. |
| PPSSPP VR **[cited]** | Two passes per frame (`GetVRPassesCount() == 2`). | **Partly.** Validates the two-pass cost model, which is exactly the cost question in V3. |
| vorpX Z-mode / SuperDepth3D / geo-11 **[cited]** | Single render plus depth-based reprojection. | **Only as the fallback**, not the plan. It is a screen-space trick, and a note highway plus a 3D background is the content class that exposes it. |
| visionOS decompilation ports ("Shipwright") **[cited]** | Decompiled game with a working VR mode; Metal/visionOS; self-described low rigor. | Feasibility evidence only. |

No public precedent exists for **"recompiled PowerPC CPU + a custom Vulkan GPU
plugin → stereo"**. CitraVR is the structurally nearest relative, so this plan
follows its shape: make the recompiled guest's own renderer draw twice, with the
camera told to be a different eye each time.

## 3. Architecture

| # | Decision | Why |
| --- | --- | --- |
| 1 | Stereo by **per-eye duplication at the draw layer**, patching the view-projection constant range between issues | The CitraVR model. Guest logic ticks once per frame; only camera-dependent draws are issued twice. |
| 2 | OpenXR binding through **`XR_KHR_vulkan_enable` (v1), not `enable2`** **[cited]** | Under `enable2`, Vulkan valid usage requires the device to have been created by `xrCreateVulkanDeviceKHR`. Under `enable`, the application keeps owning the raw `VkInstance`/`VkDevice` and must only enable the runtime's required extensions — a parameter addition to the existing factories instead of a rewrite of GPU ownership. Note there is no `xrGetVulkanInstanceExtensions2KHR`; the v1 entry points are the ones to call. No window or surface is involved at all. |
| 3 | Eye targets: **two passes first, multiview later** **[cited]** | Meta: "Multiview is enabled by default for OpenXR apps on Quest" and "Every Quest device supports Multiview rendering, via both OpenGL and Vulkan." Multiview (device feature + `VkRenderPassMultiviewCreateInfo` view mask + 2-layer array targets) is the optimization; it reshapes render-pass construction, so it is not the bring-up path. |
| 4 | Check the SDK's **clip-space convention** against OpenXR's (Y-down, depth 0..1) | OpenXR's requirement is documented; whether the SDK's projection paths already match is not something to assume. |
| 5 | Head tracking is **3DOF (rotation only)** | Rotation preserves the camera position the title authored. Translation exposes geometry that was never built, and turns this into a research project. 6DOF stays an explicit experiment, not a milestone. |
| 6 | HUD drawn to a **quad composition layer**, not stereo-projected | Meta guidance prefers compositor layers for text and UI, and the VR check that calls headlocked UI uncomfortable points the same way. Blitz draws its HUD with the guest renderer, so this is a routing decision (redirect that target to a quad layer) with "stereo HUD" as the fallback if routing looks wrong. `XrCompositionLayerQuad` is core OpenXR 1.0 — no extension needed. |
| 7 | Presentation via a **new `Presenter` implementation**, not a `VulkanPresenter` subclass | See §4: `VulkanPresenter` is `final` and requires a `VkSurfaceKHR`/`VkSwapchainKHR`. |
| 8 | Input via a new **OpenXR input driver** behind the existing merge layer | Devices already merge into one `X_INPUT_GAMEPAD`; Touch controllers become that pad, the SDL pad path stays for desktop. |
| 9 | Android shell is a **Java `SDLActivity` subclass**, not `NativeActivity` **[cited]** | Quest starts an activity without `com.oculus.intent.category.VR` in "pancake" 2D mode; SDL3's own XR manifest is the worked example. Khronos' `hello_xr` uses `NativeActivity`, so this is a sample choice, not a requirement. Link Khronos' `openxr_loader_for_android`; SDL's own source warns against Meta's `libopenxr_forwardloader.so` because it does not export `xrGetInstanceProcAddr` directly. |
| 10 | **`XR_FB_space_warp` (AppSW) is a later milestone, never a v1 crutch** **[cited]** | It requires app-elected half-rate rendering, a PROJECTION layer, and an RGBA16F motion-vector pair plus depth. This renderer has no velocity-pass concept, so that is its own piece of work. (Naming traps recorded so nobody hunts for them: `XR_META_space_warp` does not exist, and `XR_KHR_composition_layer_quad` does not exist.) |
| 11 | **SDL3's built-in OpenXR support is not usable here** **[tree]** | `SDL_CreateGPUXRSession`/`SDL_CreateGPUXRSwapchain` are `SDL_GPU` APIs: they require the video subsystem to be initialised and create the instance through the OpenXR wrappers. They cannot carry a raw-Vulkan renderer that owns its own device. (Version note **[cited]**: the XR code is on SDL `main`, milestone 3.6.0, unreleased when this was written.) SDL3 is still used for audio, input and the Android activity. |

## 4. Where it plugs into this tree

| Seam | Location | Role |
| --- | --- | --- |
| Draw choke point | `rexglue-sdk/src/graphics/vulkan/command_processor.cpp` — `IssueDraw` (line 3590) | Every guest draw passes through here. The second eye is a second issue of the same draw with the other eye's constants bound. |
| Constant tracking | same file — `WriteRegistersFromMem` (2163), `UpdateBindings` (6362; float-constant leg 6384+, upload from ~6421) | The SDK already tracks which float-constant registers each shader uses (`current_float_constant_map_vertex_`, `range_has_any_constant_usage` ≈2204). That is both the patch site and the raw material for camera discovery. |
| Guest-code hooks | `rexglue-sdk/include/rex/hook.h` (`REX_HOOK`, `REX_HOOK_RAW`) | Hooking a recompiled guest function with raw guest register/memory access. This tree already patches guest bytes at absolute addresses ([src/hooks/ultimate.h](../src/hooks/ultimate.h), `OnPostLoadXexImage`), so intercepting the guest's camera update is an established technique here. |
| Unnamed guest functions | [config/functions.toml](../config/functions.toml) | Nothing in the render or camera path is named yet (three forced entries, all elsewhere). If the camera has to be hooked rather than sniffed from constants, this file grows. |
| Presenter | `rexglue-sdk/include/rex/ui/vulkan/presenter.h` — `class VulkanPresenter final : public Presenter` (61) | The `Presenter` base is the abstraction to implement; `final` plus a mandatory `VkSurfaceKHR`/`VkSwapchainKHR` means a **sibling** implementation. `GuestOutputImage` (153) is how guest output already reaches it. |
| Instance / device creation | `rexglue-sdk/include/rex/ui/vulkan/instance.h` — `Create(bool with_surface, bool try_enable_validation)` (27); `device.h` — `CreateIfSupported(...)` (25) | Neither accepts an extension or feature list. OpenXR requires the runtime's extensions on both, so this is a small upstream-worthy change (see the patch policy in [patches/README.md](../patches/README.md)). |
| Render-target cache | `rexglue-sdk/src/graphics/vulkan/render_target_cache.cpp` | EDRAM emulation and the `Path::kHostRenderTargets` / pixel-shader-interlock split. Doubling eye targets doubles work here, and this is where [B-010](known-issues.md)-class bugs live. |
| Input | `rexglue-sdk/include/rex/input/` — `input_driver.h`, `sdl/`, `mnk/`, `state_merge.h` | An OpenXR driver slots in beside the existing drivers and merges into the same `X_INPUT_GAMEPAD`, with the existing neutral-device rules untouched. |
| Platform layer | `rexglue-sdk/include/rex/platform.h` | `__ANDROID__` sets `REX_PLATFORM_ANDROID` + `REX_PLATFORM_LINUX` (36–37); Apple sets `REX_PLATFORM_MAC` (32), which an iOS port would need to subdivide. |
| POSIX risk areas | `rexglue-sdk/src/core/fiber_posix.cpp`, `seh_posix.cpp`, `mapped_memory_posix.cpp`, `exception_handler_posix.cpp`; `rexglue-sdk/src/system/xmemory.cpp` | Fibers, SEH, and VA reservation. Android is much closer to these paths than Windows is, so V2 exercises them. |
| Texture formats | [patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch](../patches/rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch) | D3D12-side only. The Vulkan host-format table lacks the same formats, so B-010 recurs on Vulkan until the equivalent lands — V1 owes it, and nothing Vulkan (Quest or iOS) is testable without it. |

## 5. Milestones

Each milestone has a kill gate. A kill gate that fires is a result, not a
failure: it stops spending before the expensive steps.

| ID | Goal | Deliverable / exit criterion | Kill gate |
| --- | --- | --- | --- |
| **V0** | "Find the camera" — Windows, no headset | Initialise the 9 uninitialised SDK submodules and build the Vulkan backend (see [known-issues.md](known-issues.md)); port the 32-bit fixed-point conversion to the Vulkan host-format path; instrument `IssueDraw`/`UpdateBindings` to log the used vertex float-constant ranges and values per draw; implement a CitraVR-style scoring heuristic over those logs to nominate a view-projection range and a left/right eye indicator; measure guest tick cadence. | Written mapping from a live Blitz frame to a constant range that tracks the camera, plus a cadence number. If no range behaves like a view-projection matrix, per-eye injection becomes shader-translation work or depth reprojection: **stop and report before building V1.** |
| **V1** | Vulkan + stereo on the desktop | OpenXR session on Windows over Quest Link/SteamVR: `XR_KHR_vulkan_enable`, extension injection into instance/device creation, new presenter; two-pass per-eye stereo at `IssueDraw` with the V0 range patched; 3DOF tracking; HUD quad layer; OpenXR input driver. | Blitz playable in stereo with head tracking on desktop, with a measured per-eye and per-frame render cost. If per-eye cost cannot be brought near budget, decide between AppSW and a flat Quest build (itself a shippable deliverable) **before** funding Android bring-up. |
| **V2** | Quest, **no stereo** | Android build of the SDK and app; manifest per §3 row 9 (VR intent category, `org.khronos.openxr.permission.OPENXR`, broker `<queries>`, `glEsVersion`, `android.hardware.vulkan.version`); sideload with `adb install`; guest rendered flat into the XR swapchain; audio through SDL → AAudio at a latency the title tolerates. | Sideloaded APK running the title on the headset with retrievable logs — the Android, presenter, lifecycle and audio work is proven separately from the stereo work. On-device `vkEnumerateDeviceExtensionProperties`/features dump taken here (interlock, multiview, transform feedback). |
| **V3** | Quest stereo | Per-eye rendering on device (multiview if it pays for itself, two-pass otherwise); foveation; eye constants driven by the HMD pose; HUD quad layer; second-eye work skipped for draws that do not depend on the camera. | Sustained 72 Hz at the headset's per-eye resolution. |
| **V4** | Hold the frame rate | `XR_FB_space_warp` if V3 needs it (half-rate rendering, RGBA16F motion vectors + depth, projection layer only), or accept 72 Hz with foveation. | Frame rate holds under the acceptance workload, with artifacts bounded. |
| **V5** | iOS | MoltenVK; the deprecated-`ucontext` fiber replacement; the extended-virtual-addressing entitlement; unsigned device bundle. Quest and iOS share the same fallbacks (no fragment-shader interlock on Adreno, no geometry shaders under MoltenVK), so V0–V4 is most of the de-risking. | Port boots and renders; distribution is device-only (no App Store assumption — see §7). |

**Not do-able in parallel with V0–V2:** any assumption that the second eye is
cheap. The eye count doubles draws *and* EDRAM resolves, and the resolve path is
where this project's hardest graphics bugs have already been
([known-issues.md](known-issues.md)).

## 6. Two risks that are not graphics

**Camera motion comfort.** Blitz's camera moves, and full authored camera motion
in VR is uncomfortable no matter how correct the stereo is. Comfort affordances
(vignette on movement, yaw-lock option, framing the highway as a floating stage)
are a workstream with real design decisions, not a polish pass at the end.

**Audio/video sync.** This is a **rhythm game**. OpenXR provides no audio path;
on Android audio goes through SDL to AAudio, and SDL's Android audio is
pause-coupled to the activity lifecycle by default. Tight A/V sync plus a
calibration path is arguably as risky as the stereo work, and it is invisible in
any plan that starts from "can I render two eyes".

A third, smaller one: milestone 4's open item is pad-driven verification. On the
headset there is no keyboard, so pad input has to be correct before VR input can
be layered on it.

## 7. Distribution and store policy

Three shipping paths exist on Quest, and only the first is planned:

| Path | Shape |
| --- | --- |
| **Sideload (developer mode)** — `adb install`, Unknown Sources | No review, no updates, no store listing. Meta's policy documents state that apps are **not required to perform a platform entitlement check**, so nothing has to be faked to make this work. |
| **Release channel** | Upload to the developer dashboard; no full store review, but release packaging rules still apply, and it is invite-based (default 200 testers, expandable to 2,500). |
| **Store** | Production channel + review; mandatory since 2024-07-31 (App Lab is gone). |

**Recommendation: sideload, and treat the store as out of scope.** Reasons, in
order of weight:

1. **No precedent.** No emulator or retrofitted-VR title has ever been on the
   Meta Horizon Store — CitraVR shipped on GitHub/SideQuest, PPSSPP VR is
   sideload-only, Dolphin VR was abandoned. There is nothing to model the
   submission on.
2. **Policy text points the same way.** The Platform Abuse Policy forbids
   content that is "ripped, copied, not legally acquired", and the sideloading
   documentation says outright that enabling developer mode "is not intended
   for piracy". The Developer Distribution Agreement makes the developer
   warrant non-infringement and indemnify Meta. Meta's content guidelines do
   **not** mention emulators or ROMs at all — the constraint arrives through
   those clauses, and it is real.
3. **The repository's existing posture already fits.** "You supply the game
   data" is exactly the framing Meta's App Policies use for content the user
   already has a right to ("already been purchased by the user"). Nothing here
   needs to change for a sideload build.

**A flat Quest build is a legitimate intermediate deliverable.** Meta accepts
plain non-OpenXR Android "2D" applications, and the manifest requirements are
published (ARM64 only, v2-signed release APK, `minSdk ≥ 29`, `targetSdk ≥ 34`
for 2D panel apps, APK < 1 GB / OBB < 4 GB). That gives VR work a fallback
outcome that is still worth having if V3's frame-rate gate fires.

Policy document text is quoted here from a single research pass **[cited]**;
re-read the source documents before acting on any of it.

## 8. Risk register

| Rank | Risk | Where it bites | Mitigation |
| --- | --- | --- | --- |
| 1 | The camera is not a clean matrix in float constants | V0 | The kill gate. Auto-discovery over logged constant values (CitraVR's approach) rather than manual RE, and early enough to abandon cheaply. |
| 2 | Doubled draws and doubled EDRAM resolves cost too much on Adreno | V3 | Multiview, foveation, skipping second-eye work for camera-independent draws; AppSW as the escape hatch, budgeted as its own milestone. |
| 3 | Audio latency / A/V sync in a rhythm game | V2 onward | Treat as a first-class workstream with its own acceptance test, not a bug to fix later. |
| 4 | Camera motion comfort | V1 onward | Rotation-only tracking, comfort affordances, and a willingness to reframe the play space. |
| 5 | GPU feature gaps (no fragment-shader interlock on Adreno, no geometry shaders under MoltenVK) | V2 | Both platforms land on the same fallback paths, so this is measured once. The V2 device dump is the datum. |
| 6 | SDK changes needed are more than two patches (instance/device extensions, presenter) | V1 | Write them as upstream-worthy patches under the existing policy ([patches/README.md](../patches/README.md)) instead of accumulating an unmanaged local diff. |
| 7 | Unverifiable policy surface (nothing documents "user-supplied game data" on Quest) | §7 | Sideload only. Do not make decisions that depend on store approval. |

## 9. Only the headset can answer these

Recorded so they are not mistaken for settled:

- The on-device extension and feature dump (interlock, multiview, transform
  feedback, and whatever the Adreno model in the headset actually exposes).
  Meta publishes no supported-extension index and no GPU model numbers.
- Whether multiview pays for itself against two passes at this workload.
- Whether `XR_FB_space_warp` is needed at all.
- Whether ~4.5 GB of virtual-address reservation with fixed shared mappings
  survives Android's allocator (and, later, iOS's entitlements).

## 10. Sources

Read in this tree: the vendored SDL3 headers and sources
(`thirdparty/sdl3/include/SDL3/SDL_openxr.h`, `SDL_vulkan.h`, `SDL_gpu.h`,
`src/gpu/xr/`, `src/gpu/vulkan/`), and the ReXGlue SDK paths listed in §4.

Quoted in the research pass behind this plan, not re-read here: the CitraVR and
UEVR repositories, Dolphin VR (`VR-Hydra`) and PrimedGun, PPSSPP VR, Khronos
`OpenXR-SDK-Source` (`hello_xr`) and the OpenXR specification's Vulkan binding
valid usage, Meta Horizon developer documentation (VR check guidelines, store
packaging requirements, sideloading and release channels, Platform Abuse
Policy, App Policies, Developer Distribution Agreement), and Apple's App Store
Review Guidelines plus its entitlement documentation for
`com.apple.security.cs.allow-jit` and
`com.apple.developer.kernel.extended-virtual-addressing`.

Header and step references above point at the pinned SDK revision, so they move
when the submodule does. Where a claim is marked **[cited]** it came from one
research pass and has not been independently confirmed in this repository;
policy text in particular should be re-read at the source before it is relied
on.
