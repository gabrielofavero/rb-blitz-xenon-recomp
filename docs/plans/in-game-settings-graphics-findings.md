# In-game settings — P4 graphics capabilities, exact modes, first-run policy

Status: **P4 complete as a host-tested lane, with measured runtime evidence for the render path
and the per-draw extents.** The pure half (capabilities + reconciliation + first-run + recovery)
is implemented and tested: 146 host checks in a new `settings_graphics` ctest test, 28/28 tests
green. The render path's *decision* is implemented and tested (`PlanGuestScreen`); the guest-side
*application* of a 1080p guest size still needs the coordinator's one `[[midasm_hook]]` entry,
and that request is written out verbatim in §7. No cutover, no installer change, no launcher
change, no UI.

Read order: `docs/plans/in-game-settings-contracts.md` (frozen names and the §5.3/§7.1 shapes P4
fills) → `docs/plans/in-game-settings-resolution-findings.md` (P2's measurements, which P4
extends) → `docs/plans/in-game-settings-storage.md` (the `Reconciler` seam P4 implements) → this
file.

Evidence labels: **measured** = executed/observed in this pass; **source** = read from the
repository; **frozen** = a P0 decision; **P4 addition** = a delta this pass had to make, recorded
for coordinator review.

---

## 1. Verdict in one paragraph

The plan asks P4 for four things: publish truthful capabilities, implement the graphics
reconciliation, implement a measured first-run policy plus a bounded failure route, and finish
P2's render path on the backends this build offers. All four are done except the last step's
*g*uest-side application, which is blocked on one coordinator-owned line (§7): the capability
compilation and the whole reconciliation policy are pure, tested host logic that never lies about
a backend, never offers a step the device has not proved, never re-optimizes a valid manual
choice, and never erases a request because a monitor was unplugged. The render path is proven at
**1920x1080 with per-draw viewport and scissor instrumentation** — the specific gap P2 left open —
on the one backend this build contains (D3D12; Vulkan is not compiled here, §9).

---

## 2. Baseline, build and test record (measured, 2026-10-11)

| Item | Value |
| --- | --- |
| Baseline | `1e40765`, branch `main` (tree shared with the P1/P2/P3 lanes, as recorded in P3's doc) |
| New files | `src/settings/capabilities.{h,cpp}`, `src/settings/graphics_policy.{h,cpp}` |
| New test | `tests/settings_graphics_tests.cpp` (ctest name `settings_graphics`, **146 checks**) |
| Shared file touched | root `CMakeLists.txt` — one `add_executable`/`add_test` block inside `BUILD_TESTING` only |
| SDK patch | `patches/rexglue-sdk/0015-render-resolution-instrumentation.patch` **extended** (P2's swap/RT totals + P4's per-draw viewport/scissor); it is the only patch that touches those three files |
| Tests | **28/28 pass** (`ctest --test-dir out/build/win-amd64-release --output-on-failure`, 11.3 s) |
| SDK audit | `scripts/apply_sdk_patches.ps1 -Check` → exit 0, `38 patched, 0 UNEXPECTED, 0 untracked` |
| This machine | 4 logical processors; primary display **2560x1080**; **D3D12 built, Vulkan not built** (`REXGLUE_USE_D3D12=ON`, `REXGLUE_USE_VULKAN=OFF` in the build cache) — **amended 2026-10-13:** the Windows presets now configure `REXGLUE_USE_VULKAN=ON`, so this is the state the measurements below were taken in, not the tree today |

```powershell
# Repository root
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
ctest --test-dir out/build/win-amd64-release --output-on-failure     # 28/28
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\apply_sdk_patches.ps1 -Check
```

The two new modules are SDK-free and compile standalone (`clang++ -std=c++23 -Isrc`, same source
set as `rb_blitz_settings_service_tests` plus the two new files).

---

## 3. What each new file is

| File | Contents |
| --- | --- |
| `capabilities.h/.cpp` | `RenderPathLimits`, `RenderPathSizes`, `RendererProbe`, `CapabilityInputs`, `SupportedResolutions`, `NormalizeDisplayModes`, `BuildCapabilitySnapshot`, `ActiveRenderer`, `FindRenderer`, `IsSupportedResolution`, `LargestSupportedFitting`, `SmallestSupported`, `DisplayModeToken`, `ParseDisplayModeToken`. |
| `graphics_policy.h/.cpp` | `IsGraphicsRow`, the correction/validation reason keys, `AntiAliasingTokens`, `FilteringTokens`, `PolicyRenderer`, `GuestScreenPlan`/`PlanGuestScreen`, `GraphicsReconciler` (P0's `Reconciler`), `GraphicsRecommendation`/`RecommendGraphics`, `GraphicsBootMarker`/`GraphicsFallbackWarranted`/`RecoverGraphics`. |

Both are P4-owned: they are new file names in `src/settings/`, not edits to P3's
`{model,schema,store,migration,paths,service,cvars}.*`. `graphics_policy.cpp` includes
`settings/service.h` for the `Reconciler` base and `settings/cvars.h` for the filtering value map;
neither is modified.

---

## 4. The capability rules (frozen here, tested)

`BuildCapabilitySnapshot` enforces three "do not lie" rules. All three are the difference between
a selector a user can trust and one that offers what the machine cannot do.

1. **A backend is usable only if it initialized.** `RendererProbe` carries `built`, `detected`,
   `usable` and `active` as four separate facts, and a renderer that is none of them is not
   published at all. A present Vulkan loader makes a renderer `detected`, never `usable` — the
   plan's "do not list an unusable backend just because its DLL or Vulkan loader is present".
   `ActiveRenderer` refuses to guess when two renderers are usable and neither is marked active.
2. **`supported_resolutions` is exact render dimensions, not output sizes.** It is
   `RenderPathSizes` (each guest base size x each offered draw scale, above the offer ceiling
   trimmed away, deduplicated, ordered by area then width) trimmed to the primary display.
   Measured consequence on this machine (2560x1080, bases 720p and 1080p, scales 1..3): the offer
   set is exactly **1920x1080 and 1280x720** — 2560x1440 does not fit the 1080-px-tall display.
3. **A failed enumeration is not evidence.** With no monitor query the producible set is published
   unchanged, and a display smaller than everything producible still gets the smallest producible
   size. A row's choice list is therefore never empty and never shrinks because a monitor was
   unplugged for one boot.

The list is deterministic (area desc, width desc, height desc), so the UI order is stable across
boots.

---

## 5. The graphics policy (tested)

### 5.1 Which rows, and against which renderer

The policy owns `kResolution`, `kRenderer`, `kAntiAliasing`, `kFiltering`. `Vsync` is a
graphics-category row but not capability-dependent, so it keeps the default reconciler's
kind/choice check.

AA and filtering are evaluated **for a named renderer**, defaulting to the active one
(`PolicyRenderer`). That is what makes contracts §6.3's "a renderer change revalidates
AA/filtering/resolution together" implementable: `ValidateForRenderer(id, …, renderer_token)`
refuses an acceptance that was valid for the previous backend instead of clamping it by index.

### 5.2 Choice sets come from the device, not the device's advertisement

| Row | Offered | Gate |
| --- | --- | --- |
| `anti_aliasing` | `none`, `fxaa`, `fxaa_extreme` | always, for any usable renderer |
| `anti_aliasing` | `+ native` | only when the renderer's `native_aa_label` is non-empty — an unproved native path does not ship (contracts §7.1) |
| `filtering` | `native`, `none` | always (ceiling 0 or more) |
| `filtering` | `+ 1x/2x/4x/8x/16x` | only up to the renderer's `filtering_ceiling`; a "16x" label is never written as 16 |

P5's menu should show exactly these lists rather than the static schema choices; no P3 schema
change is needed for that (the functions return the tokens in presentation order).

### 5.3 Correction is deterministic and never silent

| Case | Effective | Reason key |
| --- | --- | --- |
| Requested resolution not offered, and exceeds the primary display (4K on a monitor that is now 1080p) | the largest offered size that fits | `settings.graphics.for_display` |
| Requested resolution not producible at all (e.g. 1600x900) | the largest offered size that fits | `settings.graphics.unsupported` |
| Requested renderer gone (a backend stopped initializing) | the active renderer (or the only usable one) | `settings.graphics.renderer_unavailable` |
| `native` AA, device never verified it, FXAA offered | `fxaa` | `settings.graphics.aa_unavailable` |
| Filtering step above the ceiling | the highest offered step at or below both the request and the ceiling; `native` if none qualifies | `settings.graphics.filtering_ceiling` |

Two invariants the tests pin: **a valid manual choice is never re-optimized** (a 720p request on a
4K monitor stays 720p; a shorter filtering step is never raised), and **the request is never
modified** — only `effective` changes, so the user's 4K survives a boot on a 1080p monitor
(contracts §5.2). With no enumeration the same 4K request is left completely alone.

`Set` refusals carry their own keys (`…unsupported_resolution`, `…renderer_unavailable_choice`,
`…aa_unavailable_choice`, `…filtering_unavailable_choice`); where the row's own choice list already
excludes the value, the service's `settings.invalid_choice` stands.

### 5.4 First run and failure recovery

`RecommendGraphics` uses measurements only:

* **Measured machine** → the primary display's largest renderable size (1920x1080 here, 3840x2160
  on a 4K primary), the backend this boot actually uses, `native` AA **only** when the device
  verified its sample semantics, and `native` filtering. Filtering deliberately stays the title's
  own choice: the ceiling is a fact that bounds what the user may *pick*, not a preference to
  impose, and `anti_aliasing`'s `native` is the one field the measurement genuinely gates.
* **Unknown hardware** (no usable renderer, or no producible size) → the bounded conservative
  route: 1280x720, the only usable backend (d3d12 as the last resort), `none`, `native`, with
  `conservative = true` and `settings.graphics.recommended_conservative` so the UI can say why.

`GraphicsFallbackWarranted(marker)` is `attempted && !presented` — a first run or a boot that never
reached presentation is not a graphics failure. `RecoverGraphics` returns the request untouched
when no failure is recorded (no boot-time re-optimizing) and the safe floor when one is:
**1280x720, the active backend, `none`, `native`** — the combination that presented on every
machine this project measured — with `settings.graphics.recovered_from_failure`. The durable
marker's shape is P4's (`GraphicsBootMarker`); *where* the boot writes and clears it is the
coordinator's wiring, like P3's storage wiring.

---

## 6. The render path: the guest-size plan, and what was measured

### 6.1 The planner (implemented, tested)

P2 established that the title picks a guest screen size and the SDK multiplies it by an integer
draw scale. `PlanGuestScreen(limits, requested)` is the inverse and is the whole of the render
path's decision:

| Requested render size | Guest size | Draw scale |
| --- | --- | --- |
| 1280x720 | 1280x720 | 1 |
| 1920x1080 | 1920x1080 | 1 |
| 2560x1440 | 1280x720 | 2 |
| 3840x2160 | **1920x1080** | **2** |

The largest guest size that divides the request by an offered scale wins, so 3840x2160 plans as
1080p x 2 (a crisp UI) rather than 720p x 3 (a blurry one), and both are exact. Every size in
`RenderPathSizes` plans back to exactly itself — the two directions are tested against each other.
Without the 1080p guest height, `PlanGuestScreen(1920x1080)` reports `supported = false`, so an
unreachable 1080p is never offered as if it worked.

### 6.2 Per-draw extent evidence (measured, the gap P2 left)

P2's instrumentation reported swap-time and render-target totals. P4 extends the same patch with
the **per-draw host viewport and scissor** — the independent confirmation P2's handoff asked for —
each tuple logged exactly once per boot at debug level.

Evidence: `out/_swap/p4/baseline720.log` (no flags) and `out/_swap/p4/halfres2x_1080p.log`
(`REX_RESOLUTION_SCALE=2` + `--probe_screen_1080p=true`, i.e. P2's measured Variant A).

| | 720p baseline | 1920x1080 (Variant A) |
| --- | --- | --- |
| `guest_output` (presented) | 1280x720 | **1920x1080** |
| `packet_unscaled` (guest buffer) | 1280x720 | 960x540 |
| `resolve_dest_scaled` | 1280x720 | **1920x1080** |
| `draw_scale` | 1x1 | 2x2 |
| host render target width | 1280 px (16 tiles) | **1920 px** (12 tiles) |
| `msaa_samples` seen | 0 and 2 | 1 and 2 |
| per-draw **viewport** | `w=1280 h=720` scale 1x1 | **`w=1920 h=1080` scale 2x2** |
| per-draw **scissor** (main rect) | `r=1280 b=720` | **`r=1920 b=1080`** |

The 1920x1080 numbers are the same render-target/resolve facts P2 measured, now with the per-draw
viewport and scissor that a GPU capture would show. The swap source is `source_is_scaled=true`
once the title's own resolve path runs (the same self-check P2 documented). The distinct-tuple
sets contain no other full-screen rectangle, i.e. nothing in the frame is being drawn at a
different scale from the one the swap advertises.

A rendered frame at 1920x1080 is captured in `out/_swap/p4/menu1080p.png` (the title screen, in
colour, with its scrolling-background shader and button prompt — a composed frame, not a
loading stage).

### 6.3 Which route this is, honestly

The table above is **Variant A** — the title's own half-resolution (960x540) mode times draw scale
2. It reaches exactly 1920x1080 and needs no guest change, which is why it could be measured
today. It is *not* the product route: the guest lays its UI and HUD out at 960x540 and the host
upscales them 2x, and P2 recorded that the half-resolution mode's other effects are not
established. The product route is **Variant B** (§7) — the guest at a real 1920x1080 with the
scale left at 1 — and P4's planner is written for it: `PlanGuestScreen` returns
`{1920x1080, scale 1}` for the 1920x1080 request, which is exactly the Variant B combination, and
the base-size list that makes it reachable is `{{1280,720},{1920,1080}}`.

---

## 7. The one thing P4 needs from the coordinator (exact request)

P2 asked for a single `[[midasm_hook]]` entry; P4 confirms the request and can now write it
precisely. `sub_823F3698` loads the height into `r11` at guest **0x823F382C** (`li r11,720`; the
540 path at 0x823F3834 is selected by bit 0 of `+796`). Changing that immediate to **1080** makes
the title compute `1080 * 16 / 9 = 1920` itself at scale 1.

`config/functions.toml` (coordinator-owned, §9.1 of the contracts) would gain:

```toml
# P4 Variant B: the title's own screen height (720) becomes 1080, so its own height*16/9
# yields 1920 and the draw scale stays 1. Guest 0x823F382C, `li r11,720`.
[[midasm_hook]]
address = "0x823F382C"
name = "RenderHeightHook"
registers = ["r11"]
after_instruction = true
```

and the project would define the handler next to the other guest hooks:

```cpp
// r11 is the register the generated call passes as ctx.r11, so the hook writes the height the
// title will use for the rest of its own size computation.
extern "C" void RenderHeightHook(PPCRegister& r11) {
  if (rb_blitz::settings::WantsGuestHeight1080()) {   // the resolution row's effective value
    r11.u32 = 1080;
  }
}
```

**Unverified, and labelled as such.** P4 did not test a midasm hook: the emitted call is
`Hook(ctx.r11)` (codegen `BuilderContext::r()` returns the `PPCRegister` member, so a reference
parameter binds to it), but whether the generated body reads the register back from `ctx` after
the hook instead of from a cached local is a codegen property this pass did not exercise.
Verification is one boot:

```powershell
$env:REX_RESOLUTION_SCALE='1'
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p4\run-probe.ps1 -Label variantb -Seconds 165
# expect: swapdims guest_output=1920x1080 packet_unscaled=1920x1080 draw_scale=1x1 source_is_scaled=true
#         viewport d3d12: w=1920 h=1080 draw_scale=1x1
```

If it comes back at 1280x720 the hook is not taking effect and the fallback is P2's Variant A
(measured, §6.2) or a whole-function override of `sub_823F3698` — P2 ruled out poking `+64`/`+68`
after the call.

---

## 8. Evidence index

| Path | What it proves |
| --- | --- |
| `tests/settings_graphics_tests.cpp` | 146 checks: every case the P4 prompt names (4K→1080p, CPU-count reduction, disappearing backend, AA/filter changes, renderer change inside an edit, missing enumeration, display removal/reconnection, unknown hardware, first-run, recovery) plus the service integration |
| `out/_swap/p4/baseline720.log` | 720p: viewport/scissor 1280x720 at scale 1x1, RT width 1280 |
| `out/_swap/p4/halfres2x_1080p.log` | **1920x1080**: RT width 1920, resolve 1920x1080, viewport/scissor 1920x1080 at scale 2x2 |
| `out/_swap/p4/menu1080p.png` | a composed, rendered frame at 1920x1080 |
| `out/_swap/p4/report.ps1` | the summariser both logs above were read with (strips timestamps, groups by distinct tuple) |
| `out/_swap/p4/run-probe.ps1` | the boot runner (disposable config/save root; reuses P2's warm shader cache) |
| `out/_swap/p4/shot.ps1` | the screenshot boot |
| `patches/rexglue-sdk/0015-render-resolution-instrumentation.patch` | the instrument: `swapdims`, `rtdims`, `viewport d3d12` (`SetViewport`), `scissor d3d12` (`SetScissorRect`) |

Grep inside a log: `Select-String -Path <log> -Pattern 'viewport d3d12|scissor d3d12|swapdims|rtdims'`.

---

## 9. Honest unknowns

* **Vulkan is not built on this machine** (`REXGLUE_USE_VULKAN=OFF`). "Finish P2's render path on
  every offered backend" therefore means D3D12 here, and this build offers exactly one renderer.
  The snapshot's `renderers` list would contain a single d3d12 entry with `usable`/`active` set,
  and the Renderer row would collapse to one choice — which is the truthful outcome, not a bug.
  A Vulkan-enabled build has not been exercised by P4; the instrumentation was written to compile
  for both backends (patch 0015 patches the Vulkan swap too) but the Vulkan path is unproven.
* **The midasm hook is unverified** (§7). The product route is designed and its planner is tested;
  the guest-side application of a 1080p guest height is one coordinator entry away and one boot
  from proof.
* **No host probe writes the snapshot yet.** P4 delivered the compilation (pure, tested) and the
  policy; the code that fills `CapabilityInputs` from the machine — `std::thread::hardware_concurrency()`
  for `logical_processors`, an SDL display query for `primary_display_size`/`display_modes`, and
  the plugin's build/detect/usable result for the renderers — is a small host step whose interface
  is frozen in `capabilities.h`. Its wiring (`src/settings/capabilities_probe.{h,cpp}` or a
  function in the existing diag code, plus the `RBBLITZ_SOURCES` entry) is the coordinator's, for
  the same reason P3's boot wiring is: the root `CMakeLists.txt` and `rb_blitz_app.h` belong to
  the coordinator. Until it lands, the runtime evidence above was produced with the render
  instrumentation and the P2 spike, not through the settings service.
* **The AA/FXAA visual matrix was not run.** Contracts §7.1 requires P2/P4 to prove the disabled
  native path meets the visible None/FXAA semantics before the labels ship. P4 gates the `native`
  label on a *verified* `native_aa_label` (so an unproved device does not offer it), but the
  visual A/B — DoF on/off, native AA vs FXAA at 1080p — is not in this pass. The instrumentation
  now in place (`viewport`/`scissor`/`rtdims` plus a capture) is what that A/B needs.
* **No song was played at 1080p.** The 1080p evidence is the title screen and the draw stream;
  a song's draw stream is the same viewport/scale (the per-draw records show no other full-screen
  rectangle), but a song and a restart at 1080p were not captured.
* **Restart/failure recovery is policy plus a marker shape, not a boot.** `RecoverGraphics` and
  `GraphicsBootMarker` are tested; no boot yet writes the marker or acts on it.

---

## 10. Handoff to P5

| Item | State |
| --- | --- |
| Capability schema | Frozen and filled: `CapabilitySnapshot` as contracts §5.3 defines it; `renderers[]` truthful per §4 above; `supported_resolutions` is the offer set; `primary_display_size`/`display_modes` formal. |
| Resolution choices | Use `caps.supported_resolutions` (P3's `BuildDescriptors` already fills the row from it). Show `DisplayModeToken(size)`. |
| Renderer choices | Use `caps.renderers` where `usable`; label from the token. A single usable renderer means the row has one choice — keep the row (it is the honest state) but do not present it as a choice list. |
| AA choices | `AntiAliasingTokens(caps, renderer_token)` — do not use the static list; a device without a verified label must not show `native`. |
| Filtering choices | `FilteringTokens(caps, renderer_token)` — the label is the token; never write the label as a number. |
| Editing | Bind the "changed capabilities" refusal to `Service::Set`'s returned reason; call `ValidateForRenderer` for AA/filtering/resolution after a pending renderer change. |
| Overridden rows | `Service` already disables them; use `OverrideExplanation` for the text. |
| Restart prompt | `FieldState::restart_required` plus `CommitResult::restart_required`; P9 owns the mechanism. |
| Recommendation | Apply `RecommendGraphics` only to rows whose provenance is `default`/`automatic` (never over `manual`). |
| Recovery | Show `settings.graphics.recovered_from_failure` when `GraphicsFallback::needed`. |

---

## 11. Reproduction

```powershell
# host tests (fast, no boot)
cmake --build out/build/win-amd64-release --target rb_blitz_settings_graphics_tests
ctest --test-dir out/build/win-amd64-release -R settings_graphics --output-on-failure

# 720p baseline (per-draw viewport/scissor at scale 1x1)
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p4\run-probe.ps1 -Label baseline720 -Seconds 165
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p4\report.ps1 -Label baseline720

# 1920x1080 (Variant A: 960x540 x 2x), per-draw viewport/scissor at 1920x1080
$env:REX_RESOLUTION_SCALE='2'
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p4\run-probe.ps1 -Label halfres2x_1080p -Seconds 165 -Extra "--probe_screen_1080p=true"
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p4\report.ps1 -Label halfres2x_1080p
Remove-Item Env:\REX_RESOLUTION_SCALE
```

`REX_RESOLUTION_SCALE` is set through the environment on purpose: it is defined in the GPU plugin
(`rexgpu-xenos.dll`), which registers its cvars after the config is applied, so neither
`rb_blitz.toml` nor the command line can set it (P2 recorded the same ordering fact for
`resolution_scale` — and it is why the resolution row's draw-scale half must be applied by the
plugin's own cvar path in the coordinator's wiring, not by the settings config).
