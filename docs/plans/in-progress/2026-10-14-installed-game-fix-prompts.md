# Installed-game review: researched implementation prompts

**2026-10-14 — research complete. P1, P2, P3 and P4 are implemented and verified (their status is
below); P6 is implemented, with its final installed acceptance recorded below; P5 is implemented and
its rotation is measured in an installed copy (its record is also below).**
This file moved from `plans/todo/` to `plans/in-progress/` when
P1 landed.

Of section 3's six rows, **Provenance** and **DLC** are now measured in an installed copy (P6's
acceptance pass, with the per-leg table and the build identity it ran), and **Music** has the rotation
and its transition cost measured in an installed copy with the song-end question left open (P5).
**Display**, **Settings** and **Captions** carry the partial coverage their own records name — no DPI
sweep, no injected save failure, no in-song capture.

This handoff groups the owner's installed-game test report into six contexts. Preliminary research
covered the current working tree, local CMake configuration, existing tests, historical findings,
and the platform references linked below. No game was launched and no reported defect was
reproduced during this research. `[tree]` means source inspection; `[local]` means a local build
artifact was inspected; `[hypothesis]` means an execution lead, not an established cause.

The attached screenshot shows a visually full discovery bar while **Discovering Downloadable
Content** remains on screen. The owner reports that it remains there for many seconds; the still
image alone cannot establish the duration or which work is pending.

## 1. Requirements and execution order

| ID | Owner's observation / requested result | Prompt |
| --- | --- | --- |
| R1 | First launch must use the ultrawide monitor's supported aspect automatically. | P2 |
| R2 | Windowed game must have a fixed, non-resizable size and preserve the selected aspect; ultrawide must not shrink into a 16:9 window. | P2 |
| R3 | Settings help labels need larger, friendlier text, a more transparent background resembling the settings buttons, and a higher position clear of A/B prompts. | P4 |
| R4 | Hide help labels inside calibration and informational/confirmation screens such as Restart now; add Interface -> Settings Labels -> On / Off. | P4 |
| R5 | Menu background songs have long gaps and repeat. Repetition prevention is a hard requirement. | P5 |
| R6 | Installed Renderer offers only D3D12; a Vulkan-capable install must also offer and run Vulkan. | P1 |
| R7 | Change the visible action label to exactly **Unlock Online Achievements**. | P4 |
| R8 | Settings navigation can become stuck around a saving/writing message, and Restart now can do nothing. Saving and recovery must keep the menu usable. | P3 |
| R9 | Cache On still shows discovery and takes a long time. Establish actual cache reuse and its measured benefit. | P6 |
| R10 | Discovery progress appears finished while loading continues. Represent remaining work truthfully and reduce demonstrated bottlenecks. | P6 |
| R11 | **Every** settings edit in an installed copy answers "Settings could not be saved. Check that the settings folder is writable", whatever row is changed. Saving must work for every row, and a failure must name what really failed. | P7 |

Recommended order: **P1 -> P2 -> P3 -> P4 -> P6 -> P5**, followed by the installed acceptance pass
in section 3. P1 establishes which binaries are being tested; P3 makes settings-based validation
reliable; P6 establishes a stable song library for the music tests. Each prompt is a separate
implementation context, with its research immediately above it.

### Decisions and working interpretations

- **D1 — Evidence comes from the installed artifact.** A passing source test or a newer checkout
  does not establish what the owner's installer shipped. Record build identity and binary hashes.
- **D2 — Window size and render size are separate.** Both must preserve the selected aspect.
  A window may fit down to the monitor's usable desktop area while keeping that aspect; this must
  not silently change the persisted render-resolution choice. Manual border resizing is disabled.
- **D3 — Labels means contextual help captions.** Keep setting names and the actual error/restart
  message visible. Only the extra explanatory overlay is hidden on the excluded screens.
- **D4 — Proposed music contract.** Never let the current song loop or immediately follow itself,
  including across deck boundaries and menu re-entry. Do not replay a song before the eligible
  rotation has been exhausted. Define song identity independently of mount aliases. If only one
  distinct playable song remains, stop or use distinct title music after it ends instead of
  silently repeating it. This is a finite-rotation policy, not a promise never to hear a track
  again across the lifetime of the installation. The existing 50-song pool must be considered
  explicitly when defining which songs are eligible; do not describe a pool-only guarantee as
  a library-wide guarantee.
- **D5 — Storage changes need evidence.** Splitting the settings file or moving it was an owner
  hypothesis. First identify the blocked operation, actual path, writer, and OS result. Preserve
  atomic saves, conflict detection, unknown fields, and existing profiles.
- **D6 — Cache status describes work actually performed.** Cache On permits cache use; it cannot
  guarantee a hit on first boot or after library changes. The label and progress must reflect the
  actual path, and performance claims must come from comparable current builds.

Every prompt inherits these rules: preserve unrelated working-tree edits and real user data; use
disposable profiles for experiments; do not edit generated guest C++; deliver SDK changes through
the repository's patch workflow in [patches/README.md](../../../patches/README.md). Recheck symbol
locations before editing because line numbers below describe the tree inspected on this date.

## 2. Implementation prompts

### P1 — Installer provenance and Vulkan availability

Preliminary findings:

- `[tree]` [CMakePresets.json](../../../CMakePresets.json) enables `REXGLUE_USE_VULKAN` in the
  Windows base preset. [CMakeLists.txt](../../../CMakeLists.txt), around line 185, derives
  `RBBLITZ_BACKENDS` from the configured backend switches.
- `[local]` At research time, `out/build/win-amd64-release/CMakeCache.txt` had Vulkan **ON**,
  while `out/build/win-amd64-debug/CMakeCache.txt` had Vulkan **OFF**. These are configured build
  trees, not proof of which binaries were rebuilt, packaged, or installed.
- `[tree]` [installer/build.ps1](../../../installer/build.ps1):295 builds the Controller Settings
  target. The payload stage at approximately line 382 **keeps an existing payload snapshot**
  unless `-RefreshPayload` is supplied. Refreshing snapshots an existing game build; it does not
  itself establish that `rb_blitz.exe` and the GPU plugin were rebuilt from current source.
- `[tree]` [make_payload.ps1](../../../installer/tools/make_payload.ps1):125 reads backend flags
  from CMakeCache, conditionally copies `vulkan-1.dll`, and normally rejects a Vulkan-enabled
  payload without a loader. A D3D12-only tree is currently a warning, not a failure.
- `[tree]` [boot.cpp](../../../src/settings/boot.cpp):95 checks loader loading and
  `vkGetInstanceProcAddr`; lines 281–304 gate the offered backend. This is not a complete GPU
  suitability test. [rb_blitz_app.h](../../../src/rb_blitz_app.h) also checks backend startup.
- `[hypothesis]` A retained snapshot, wrong source build directory, old executable/plugin pair,
  or unavailable loader could explain the report. The installed files have not been inspected.

The Vulkan loader and device drivers are separate parts of the runtime. Packaging a loader alone
does not prove device support or successful presentation.
[Khronos loader architecture](https://vulkan.lunarg.com/doc/view/latest/windows/LoaderInterfaceArchitecture.html).

```text
Fix the installed game's missing Vulkan option. Read P1 and D1 in
docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md, installer/README.md,
installer/build.ps1, installer/tools/make_payload.ps1, CMakeLists.txt,
CMakePresets.json, src/settings/boot.cpp, and src/rb_blitz_app.h.

First establish the artifact chain: configured source tree -> freshly built game
and GPU plugin -> payload snapshot -> setup executable -> installed binaries.
Record backend flags, executable/plugin hashes, payload identity and loader path.
Distinguish a missing compiled backend from a missing loader and a device/backend
initialization failure. Do not infer an installed capability from CMakeCache alone.

Fix the failing build/packaging/detection stage. Make the normal dual-backend
release path reliably ship matching current binaries with Vulkan support and its
required runtime files. Prevent silent reuse of a stale or D3D12-only payload in
that release path; keep deliberate special builds explicit. A claimed source
commit must not conceal a retained snapshot built from different sources.

Validate through a newly built installer in an isolated destination, launching
as an ordinary user outside the build directory and without relying on a Vulkan
SDK directory in PATH. On compatible hardware, Renderer offers D3D12 and Vulkan;
selecting Vulkan, restarting and reaching a rendered menu/song succeeds. Also
exercise a controlled missing-loader/unsupported-device path: preserve D3D12
recovery and report the actual reason. Do not force-enable an unusable option.

Extend the relevant installer/capability coverage for the failure actually found.
Deliver the packaging fix, exact reproducible build commands, installed-file
evidence, and an honest list of untested hardware/backend cases.
```

#### P1 — done (2026-10-14)

**What was wrong**, two packaging defects, both silent:

1. `build.ps1` kept whatever snapshot it found in `installer\out\payload`. `REXGLUE_USE_VULKAN=ON`
   reached the Windows presets on 2026-10-13; a snapshot taken before that still held a
   `rexgpu-xenos.dll` with one backend, and every plain release build after it kept, embedded and
   installed that copy — which is the installed *Renderer* row the owner saw offering D3D12 alone.
2. `make_payload.ps1`'s `-VulkanLoaderDir` did nothing: the line that chose the loader assigned
   `$vulkanLoaderDir`, which *is* the `-VulkanLoaderDir` parameter (PowerShell names are
   case-insensitive), so the value was wiped before it was read. Measured: a `-VulkanLoaderDir`
   holding a 19-byte `vulkan-1.dll` still shipped System32's 1,452,408-byte copy, and the fatal
   "no loader anywhere" branch was therefore unreachable on a machine that has one.

**What changed.** `payload-manifest.toml` gained a `[payload]` table (`backends`, `commit`);
`installer\tools\payload_provenance.ps1`, shared by both scripts, reads it back; and `build.ps1`
reuses an existing snapshot only when it proves to be the tree it is about to embed — the
backends, the commit, and every file the tree owns byte for byte — rebuilding it with the reason
printed otherwise, and re-checking the snapshot it just took (a release that cannot account for
its own payload now fails the build). `-SkipPayload` is the only way to keep a non-matching
snapshot and it warns with the same reasons; the loader-source bug is fixed and pinned by a test.

**Found and fixed while running the project's checks.** `cmake --preset win-amd64-release` failed
outright on this checkout — and therefore on any clean clone of it — with `Controller Settings: no
[installer] version in installer/config/pins.toml`. `controller-settings/CMakeLists.txt` reads its
window-title version out of that file with `string(REGEX MATCH "\[installer\][^\[]*")`, and a
bracket inside a *comment* on the section's first line (`` `[name]` ``) ended the match 8 lines
before `version = "0.1.0"`; the entry came in with the 2026-10-14 installer-config rewrite, and the
previous configure predated it. The reader drops comment lines first now, and the configure
completes with `RBBLITZ_VERSION` resolving to `0.1.0`. Neither the installer's own CMake project nor
`build.ps1` reads that file this way, which is why the break was invisible to the installer path
this prompt is about.

**Evidence** (`out/_swap/p1/`, 2026-10-14):

| Observation | Value |
| --- | --- |
| Tree backends | `out/build/win-amd64-release/CMakeCache.txt`: `REXGLUE_USE_D3D12=ON`, `REXGLUE_USE_VULKAN=ON` (the Vulkan-off `win-amd64-debug` cache the research found is a different, stale tree) |
| Snapshot provenance | `payload-manifest.toml`: `backends = "d3d12,vulkan"`, `commit = "012f0b18c2ea6b8b6473dfaad2cec84869dd8e80"` (the checkout's `HEAD`) |
| Reuse of a current snapshot | `build3-final-2026-10-14.log`: `snapshot: … (matches <tree>)`, `backends: d3d12, vulkan - Vulkan and its loader are in the payload` |
| Stale detection | `build-2026-10-14.log`: a provenance-less snapshot → `stale` + `action: rebuilding the snapshot from the build tree`; `build5-recovery.log`: a manifest edited to `backends = "d3d12"` → `the snapshot was built for d3d12 and the build tree is configured for d3d12,vulkan` → rebuilt |
| Deliberate escape hatch | `build4-skippayload.log`: `-SkipPayload` kept the edited snapshot and warned `the kept snapshot was not taken from this build tree: …` |
> **Naming note (2026-10-15, after the release model changed).** `RockBandBlitzSetup-latest.exe`, as
> recorded in this prompt, was the complete package built with a rolling commit pin. Since
> 2026-10-15 that name is the *latest bootstrap* (a downloader that carries no payload), the rolling
> complete package is `RockBandBlitzSetup-nightly-<commit>.exe`, and a pinned release is
> `RockBandBlitzSetup-<version>.exe` — [`../finished/2026-10-15-release-model.md`](../finished/2026-10-15-release-model.md).
> The measurements below are left exactly as they were taken; only the name they used has moved.

| Installed files | `RockBandBlitzSetup-latest.exe` (24,856,075 bytes in the final build, `build6-green-2026-10-14.log`; the exe is not byte-reproducible) silent-installed `/CURRENTUSER` into `out/_swap/p1/install`; the installed `rb_blitz.exe`, `rexgpu-xenos.dll` and `rexruntime.dll` hash-match the build tree, `vulkan-1.dll` 1,452,408 bytes is present, and `install-manifest.toml` records `payload_commit` |
| Vulkan on the installed copy | `installed-vulkan.log`: `Vulkan instance API version 1.3.290`, `Vulkan device 'NVIDIA GeForce GTX 970': API 1.3.289`, `VulkanPresenter: Created 1280x720 swapchain`, `settings_menu: Renderer offers 2: D3D12, VULKAN`, `settings_boot: presented menu frame` |
| D3D12 control | `installed-d3d12.log`: the same installed copy with no backend named → `settings_boot: gpu_backend = d3d12`, D3D12 draw/scissor lines, presented frame |
| Loader gate | `-VulkanLoaderDir <empty dir>` → `-VulkanLoaderDir was given but …\vulkan-1.dll does not exist.` (exit 1); `-VulkanLoaderDir <dir holding a stub>` → the stub is what the snapshot ships |
| Installer suite | `ctest --preset installer-release` green, 421 checks, four new cases (the payload fixture now carries a `[payload]` table; the two scripts' shape, including the loader-source assignment) |

**Honest limits.** No Vulkan-incapable GPU and no machine without a `vulkan-1.dll` were available,
so the "no loader anywhere" branch is exercised only through its explicit-directory refusal, and
no unsupported-device path was reproduced. The recorded commit is the checkout's `HEAD` rather
than the working tree, so it is a label and not an identity — the per-file digests are what decide
whether two snapshots are the same build ([known-issues.md](../../reference/known-issues.md)).
Selection of Vulkan *from the in-game row* was not driven; the boot above asked for it on the
command line.

**Where the project's checks stand.** No Windows preset can build a test at all: with
`REXGLUE_USE_VULKAN=ON` the SDK's glslang guard writes `BUILD_TESTING=OFF`, so
`out/build/win-amd64-release` has no test targets, and the 37 entries `ctest -N` still lists are
stale files from a pre-Vulkan-flip configuration (the `launcher/` → `controller-settings/` rename
left an orphan `launcher/CTestTestfile.cmake` behind; that tree's copies were removed, 30 of the 37
still had binaries and passed, the 7 `rb_blitz_launcher_*` ones had none). The affected suite for
this prompt is the installer's, which does build: `ctest --preset installer-release` is green at
421 checks, and `build.ps1` runs it on every release build.

**Reproducing it** (from `<checkout>`):

```powershell
cmake --preset win-amd64-release ; cmake --build --preset win-amd64-release
powershell -ExecutionPolicy Bypass -File installer\build.ps1 -SkipArt
# one isolated install, as an ordinary user, with no Vulkan SDK on PATH:
installer\out\dist\RockBandBlitzSetup-latest.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCANCEL ^
  /CURRENTUSER /DIR=<dir> /GAMEMETHOD=folder /GAMEFOLDER=<checkout>\game /ULTIMATESOURCE=none /RUNATEND=none
```

### P2 — First-run ultrawide and fixed window proportions

Preliminary findings:

- `[tree]` [boot.cpp](../../../src/settings/boot.cpp):109 probes the Windows primary display
  through `EnumDisplaySettingsW(nullptr, ...)`. Monitor selection and startup-window placement
  must be compared with that primary-display assumption.
- `[tree]` [capabilities.cpp](../../../src/settings/capabilities.cpp), `SupportedResolutions`,
  `PlanGuestScreen`, and `LargestSupportedFitting`, already admit supported native aspect modes.
  [graphics_policy.cpp](../../../src/settings/graphics_policy.cpp) recommends Auto and resolves
  it from the capabilities; a valid manual choice is retained.
- `[tree]` Limits in [capabilities.h](../../../src/settings/capabilities.h):51–72 include a
  3840x2160 offer ceiling and aspect ceiling of 3.2. A 32:9 display is outside that aspect limit.
  The owner's exact monitor dimensions are not supplied; do not assume every ultrawide mode is
  already supported or raise limits without validating the guest layout/render path.
- `[tree]` `SettingsBoot::ApplyRow(kResolution)` at `boot.cpp:470` sets draw scale;
  `SetBootRenderFrame` at `boot.cpp:661` configures guest frame dimensions. Neither of these
  operations sets startup window dimensions. [render_settings.cpp](../../../src/hooks/render_settings.cpp):30
  writes the guest-frame override, not the SDL window.
- `[tree]` [window_sdl.cpp](../../../rexglue-sdk/src/ui/window_sdl.cpp):72–107 resolves window
  dimensions from window overrides, video-mode values or a resolution preset. `OpenImpl` creates
  the window with `SDL_WINDOW_RESIZABLE`. Patches 0014 and 0016 own prior sizing/borderless work.
- `[hypothesis]` A correct ultrawide guest frame can therefore coexist with a default 16:9 window
  or presenter mode. Also investigate stale settings/migration, explicit overrides, safe-graphics
  recovery, unsupported monitor modes, and the artifact mismatch from P1.

SDL exposes user-resizability independently of programmatic sizing; changes belong on the main
thread and cannot change resizability while fullscreen.
[SDL_SetWindowResizable](https://wiki.libsdl.org/SDL3/SDL_SetWindowResizable).
Measure the client area in pixels and fit the window to the usable desktop bounds, accounting for
DPI and decorations.
[SDL_GetWindowSizeInPixels](https://wiki.libsdl.org/SDL3/SDL_GetWindowSizeInPixels),
[SDL_GetDisplayUsableBounds](https://wiki.libsdl.org/SDL3/SDL_GetDisplayUsableBounds).

```text
Fix first-launch ultrawide selection and the game window's dimensions together.
Read P2/D2 in docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md and
docs/settings/graphics.md, especially its custom-resolution findings.

Reproduce from the installed artifact using a genuinely fresh disposable profile
and record the monitor's current mode, DPI, target monitor, supported modes,
requested/effective resolution and source, guest size, draw scale, presenter
aspect and physical client size. Repeat with an existing profile to distinguish
first-run policy from migration, overrides or crash recovery.

Connect the supported effective aspect/resolution to the startup window and
presenter at the correct initialization stage. First run with Auto should use
the supported native monitor aspect without a manual visit to settings. Preserve
valid explicit choices. If the actual monitor is outside current guest limits,
validate the needed render/layout extension rather than stretching a 16:9 frame.

Make the GAME window non-resizable by the user in windowed mode. Its client area
must keep the effective aspect and fit the usable desktop including decorations,
with correct logical/pixel conversion. A programmatic settings change may choose
a new fixed size. Handle return from fullscreen/borderless consistently. Do not
apply the game's fixed-size policy to the separate Controller Settings utility.
Use the SDK patch workflow for any SDK change.

Extend meaningful graphics-policy/window-size coverage. Validate 16:9 and the
owner's ultrawide mode, 2560x1080 and 3440x1440 where supported, multiple DPI
scales, a non-primary target display, fresh/existing profiles, and all three
display modes. Check menus, gameplay/HUD, mouse coordinates and A/B prompts.
Attempt edge/corner resize and maximize/snap paths; none may distort the image.
Verify actual frame and client dimensions, not just the Resolution label.
Record unsupported monitor modes and any missing physical-monitor coverage.
```

#### P2 — done (2026-10-14)

**What was wrong.** P2's hypothesis was right and the cause was narrower than "ultrawide support":
the render path already produced a correct 21:9 frame (part 3 §1's three levers, measured
2026-10-12), but the *boot* wrote only `resolution_scale` and the guest frame. `video_mode_width/height`
stayed at their 1280x720 default, so the presenter — which letterboxes with the **video mode** aspect,
not the frame's — drew the 21:9 frame into a 16:9-shaped region, and the window was created at the
SDK's default 1280x720. The `uw-native` arm in `out/_swap/ultrawide/` had reached
`display=2560x1080` by passing `--video_mode_width/height` by hand; the product path never did, and no
boot could have shown the owner's composition without them.

**What changed.** `SettingsBoot::ApplyWindowPolicy` (`src/settings/boot.cpp`, from
`BeforeGraphicsSetup`) now makes the window and the presenter follow the effective render size:
`video_mode_width/height` = that size (the presenter's aspect, and the window's own default),
`window_width/height` = a fitted *logical* client size only when the decorated window would not fit
the display's usable area, and `window_resizable = false`. Every write stands aside for a cvar
another source already named and is read back before it is reported. The fit is SDK-free arithmetic
(`src/settings/window_policy.{h,cpp}`, ctest `window_policy`, 68 checks) against Win32 measurements of
the desktop (work area, `AdjustWindowRectExForDpi(WS_OVERLAPPEDWINDOW)`, `GetDpiForSystem`). The
non-resizable half is [patch 0017](../../../patches/rexglue-sdk/0017-fixed-size-window.patch), which
adds the SDK's `window_resizable` cvar (default **true** = SDL's own behaviour) and applies it with
`SDL_SetWindowResizable` before the window is shown; the Controller Settings utility builds its own SDL
window and never reads it.

**Evidence** (`out/_swap/p2/`, 2026-10-14, three boots against disposable user roots,
`--fullscreen=false`, D3D12, this machine's 21:9 primary at 2560x1080):

| Observation | Value |
| --- | --- |
| First run, nothing stored (`fresh`) | `settings_boot: display 2560x1080, 4 exact render sizes`; guest render base 1280x540 at draw scale 2 — the ultrawide plan with no visit to Help & Options |
| The presenter is told the frame's aspect | `window policy: video_mode_width = 2560`, `video_mode_height = 1080`; `swapdims … display=2560x1080 … resolve_dest_scaled=2560x1080 draw_scale=2x2 source_is_scaled=true` |
| The window (Win32, not a label) | client **2354x993**, framed 2370x1032 — 2354/993 = 2.3706 against the frame's 2.3704, inside `SameAspect`'s tolerance; the work area is 2560x1032 (48 px taskbar) and the frame adds 16x39 |
| Fixed size | `GetWindowLong(GWL_STYLE)` has **no** `WS_THICKFRAME` and no `WS_MAXIMIZEBOX`; `window policy: window_resizable = false` |
| A size that fits is untouched (`manual-720`) | `settings_boot: window 1280x720 (aspect 1.7778) fits the usable desktop 2560x1032 with 16+39 px of decorations`, no `window_width`/`window_height` write, client measured at exactly **1280x720** |
| The picture, measured (§8.2's disc ruler) | `fresh` 42x38 (1.105, no bars); the control `control-pre` **22x28 (0.786)** — the same 21:9 render with the policy standing aside, squashed by ~25 % |
| A named cvar is respected | `control-pre`: `'video_mode_width' is explicitly configured ('1280'); the window policy left it alone` and `a 2560x1080 render wants a 2354x993 client here, but this boot's window size is explicitly configured` |
| SDK audit | `scripts/apply_sdk_patches.ps1 -Check` → 17 patches, 40 files patched, **0 UNEXPECTED**, exit 0 (the stray `thirdparty/fmt/support/.gradle` cache that made it exit 1 was removed) |
| Host tests | 38/38 (`out/build/rename-tests`, the Vulkan-off tree that can still build them); `window_policy` = 68 checks |

**Honest limits.** No boot at 125/150/300 %, none on a non-primary target monitor, and none that
switched windowed → borderless/fullscreen *at runtime* rather than by restarting. The fit uses the
system DPI and `WS_OVERLAPPEDWINDOW`'s metrics rather than the created window's own border size
(`SDL_GetWindowBordersSize` needs the window to exist). A display whose own mode the planner cannot
produce (an aspect outside 1.0..3.2 — a 32:9 panel is 3.556) still falls back to a 16:9 size; that is
unmeasured rather than stretched. `window_resizable=false` is verified through the window *style*, not
by driving a drag. The in-game/HUD half of R1/R2 is unchanged and stays open with part 3 §9.4. Details,
including the `GetConfiguredVideoModeWidth` fallback that makes a 1280x720 video mode follow
`window_width`, are in [graphics.md](../../settings/graphics.md) part 3 §11.

**Where the project's checks stand.** `ctest --test-dir out\build\rename-tests` is **38/38** (the new
`window_policy` is #38); the release preset still cannot build a test at all
(`REXGLUE_USE_VULKAN=ON` ⇒ the SDK's glslang guard writes `BUILD_TESTING=OFF`), so the fix was
measured in the release tree by booting it. Note the two trees share the SDK's own output directory
(`rexglue-sdk/out/win-amd64`), so building the Vulkan-off test tree **clobbers the Vulkan-on SDK
objects** — re-run `cmake --preset win-amd64-release` before the next release build, or the GPU plugin
fails to link with undefined `VulkanProvider`/`vma*` symbols.

**Reproducing it** (from `<checkout>`):

```powershell
cmake --preset win-amd64-release ; cmake --build --preset win-amd64-release
pwsh -File out\_swap\p2\run.ps1 -Label fresh -Fresh                    # first run, windowed
pwsh -File out\_swap\p2\run.ps1 -Label manual-720 -Request 1280x720    # a 16:9 size that fits
```

### P3 — Settings save stalls and restart recovery

Preliminary findings:

- `[tree]` [settings_screen.cpp](../../../src/ui/settings_screen.cpp):1080 onwards emits a set
  plus commit for a normal setting change; this is not a single save deferred until menu exit.
  [settings_bridge_hooks.cpp](../../../src/hooks/settings_bridge_hooks.cpp), `HandleRequest`,
  dispatches commits synchronously and then applies eligible live changes.
- `[tree]` [atomic_file.h](../../../src/fs/atomic_file.h):31 uses an OS-held sidecar lock with
  a roughly two-second contention retry. Its failure text conflates another writer with access
  denial. A leftover `.lock` filename alone does not mean the lock is held. Writes use a flushed
  temporary sibling and `MoveFileExW` with replacement/write-through flags.
- `[tree]` [service.cpp](../../../src/settings/service.cpp):443 additionally locks the commit
  journal. [bridge_sink.cpp](../../../src/settings/bridge_sink.cpp):482 refuses restart when
  `commit_ok_` is false. These are investigation points, not proof of a deadlock.
- `[tree]` [rb_blitz_app.h](../../../src/rb_blitz_app.h):307 drains a DLC refresh, synchronously
  dispatches restart coordination to the UI thread, and defers window closure. Inspect each wait
  and its thread ownership if Restart now has no visible effect.
- `[tree]` [paths.cpp](../../../src/settings/paths.cpp), `SettingsFileForProfile`, derives
  settings from the profile location. [profile_path.h](../../../src/profile/profile_path.h)
  documents overrides, the settings-directory pointer, portable mode and `%APPDATA%/rb_blitz`.
  Changing `user_data_root` does not automatically relocate native settings. The current installer
  defaults to Program Files/admin installation; verify the ordinary player's resolved paths,
  including Ultimate's redirected settings, instead of assuming every write targets Program Files.
- `[unproven]` The owner's saving/writing wording has not been mapped to an exact message emitter.
  It could concern the native settings bridge, provider persistence, or a guest save/transition
  guard. The user-visible message and complete navigation sequence must be captured first.

Windows replacement operations have explicit permission and failure contracts. Log the actual
operation/path/error, not just a generic saving state; choosing a different replacement API would
not by itself fix contention or an unwritable directory.
[MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw),
[ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).

```text
Fix settings navigation becoming stuck around saving and Restart now doing
nothing. Read P3/D5 in docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md,
docs/settings/architecture.md, and the settings/restart modules identified there.

Capture the exact message, screen, focused row and input sequence. Trace set,
commit, journal recovery, provider write, acknowledgement, restart request,
refresh drain, child launch and parent exit with monotonic timings and request
identities. Distinguish actual disk I/O from a stale UI flag, rejected transaction,
guest save guard, duplicate request, or a thread waiting on another thread.
Log resolved native/profile/provider paths and precise filesystem error codes.

Fix the demonstrated lifecycle or I/O issue. Ordinary navigation and help-caption
updates must not start writes. Save success, failure, conflict and cancellation
must each resolve the UI state; a transient failure must permit retry or Back.
Keep menus responsive during real slow writes. If writes are moved off-thread,
keep guest UI calls on the verified guest context and revise the current immediate
commit.ok query contract to acknowledge actual completion. Do not claim success
before durability or permit competing writers to lose each other's changes.

Inspect the actual installed storage policy as a non-admin user. Relocate only
paths demonstrated to be wrong/unwritable, with compatibility/migration coverage.
Do not split the file, delete locks, relax ACLs, or require running the game as
administrator as a substitute for identifying the cause.

Restart now must either launch exactly one replacement, preserve the selected
profile/data roots, close the parent and apply durable settings, or show an
actionable recoverable failure. Audit refresh draining and synchronous UI-thread
handoff for unbounded waits. A retry after a save/restart failure must work.

Extend settings_store, settings_service, settings_bridge_service and game_restart
tests as needed, using actual competing-writer and injected-failure cases rather
than arbitrary sleeps. Cover rapid edits, no-op/back navigation, mixed native and
Ultimate edits, lock contention, access denied, interrupted/partial saves, child
launch failure and refresh in flight. Run the installed reproducer repeatedly
with keyboard/controller/mouse; prove subsequent edits and restart still work.
Deliver the root cause and before/after traces, not merely a hidden warning.
```

#### P3 — done (2026-10-14)

**What was wrong.** Two defects in one flow, both reproduced in a driven boot rather than inferred.
The owner's "saving/writing message" is the restart confirmation's own text — *"Settings saved.
Restart the game to apply these changes."*, settings_screen.cpp's `settings_restart_message`.

1. **The confirmation could not be dismissed, so Help & Options could not be left.** Its Cancel
   pressed B, the root opened the confirmation, and B put the player back on the **root** — so the
   next leave asked the same question. Cause: the branch used the generic `EnterScreen`, whose
   `{<screen> set back_screen {ui current_screen}}` points a destination at the screen it is leaving.
   The prompt belongs to the *act* of leaving, so the confirmation now inherits the target the root
   was about to use (`RestartPrompt` in `src/ui/settings_screen.cpp`). Measured on the route the
   owner described: `07-leave-root.png` (the confirmation) → `08-after-close.png` (**the merged root
   again**) → `09-close-then-back.png` (**the confirmation again**); after the change
   `08-after-close.png` is the **main menu**, and the log carries no restart request at all.
2. **A restart refused before the boot's handler ran said nothing.** The confirmation draws its
   "could not restart" text only when `snapshot/restart.ok` is false, and `ServiceSink` refused on
   `!commit_ok_` (or with no handler) **before** touching that bit — so "Restart now" was a silent
   no-op. Every answer now resolves the bit (`src/settings/bridge_sink.cpp`), a missing handler is an
   explicit `kUnsupported`, and the message is actionable: *"The game could not restart. Close this
   message and start the game again to apply your saved changes."*

**Found while measuring, and fixed.** `fs::FileLock`'s failure text was *"cannot lock settings file
(another writer or access denied)"* — it could not tell a transient competing writer (retry) from a
path the player may not write (stop and report), which is exactly the distinction a save-failure
report needs. Both it and `WriteAtomic` now name the operation, the path and the OS reason through
`fs::OsErrorText` (stable phrase + numeric code): `cannot replace <dir>\settings.toml: access denied
(win32 error 5); the previous file is unchanged`, `cannot lock <dir>\settings.toml.lock: sharing
violation: another writer holds the file (win32 error 32)`. The boot also logs the directory writes
land in and the profile that chose it (`settings_boot: writes land in '<dir>'; profile
'<dir>\launcher.toml'`). The store test was *also* wrong: its "an unwritable destination keeps its
bytes" case had a base naming a key the file does not hold, so it exercised a **conflict** (which
writes nothing and reports no I/O error) and never reached the write at all.

**Evidence** (`out/_swap/p3/`, 2026-10-14, D3D12, windowed, disposable roots, `common` target —
the harness is `out/_swap/p3/run.ps1 -Label X -PromptAction none|close|restart`):

| Observation | Value |
| --- | --- |
| The message, verbatim | `repro3-close/shots/07-leave-root.png`: **"SETTINGS SAVED. RESTART THE GAME TO APPLY THESE CHANGES."** with rows **Close** / **Restart now** and the overlay caption "Close this message." |
| The trap, before | `repro3-close/shots/08-after-close.png` = the merged **HELP & OPTIONS** root (General selected); `09-close-then-back.png` = the **confirmation again**; the route's later A+D-pad landed on "Restart now" and restarted the game (`16:07:55 request_restart`) — the only way out of the menu |
| The fix | `after-close/shots/07-leave-root.png` = the confirmation; `08-after-close.png` = the **main menu** (PLAY … EXIT GAME); `after-close.log` has **no** `request_restart` line |
| Restart now still works | `after-restart.log`: `set 'general.cache_dlc_library' -> ok`, `commit -> ok (committed)`, `request_restart '-' -> ok (restart requested)`; `settings_restart: replacement child is waiting for parent exit` 16:38:52.834 → `Title terminated; hard-exiting process.` 16:38:53.035 → a **new boot** at 16:38:53.433 (one child, ~0.6 s, same root) |
| Navigation starts no write | `after-close.log`: the entire route (title → main menu → merged root → General → Downloadable Content → back ×3) produces exactly one `set` and one `commit` — the deliberate A on *Cache DLC Library*. No other request, so plain navigation and the caption channel write nothing |
| Resolved paths | `settings_boot: writes land in '<root>'; profile '<root>\launcher.toml'` |
| Host tests | **38/38** (`out/build/rename-tests`, the Vulkan-off tree that can build them). New: `settings_screen` pins that the confirmation inherits the leave target and that nothing points it at the root; `settings_bridge_service` pins that a refusal (unsupported / handler refused) clears `restart.ok`, that a retry after a failure works and follows the *last* answer; `settings_store` pins the two lock messages apart and a genuine read-only destination |

**Honest limits.** Only the **main-menu** context was driven; the in-song twin uses the same
`RestartPrompt` path but no boot has been taken from a song. A *genuine* refusal (a child that cannot
be started) is not reproducible on demand — it would mean breaking the exe — so it is pinned by the
host tests and by the code path, not by a boot. The `!commit_ok_` refusal is conservative: the prompt
itself only appears for values that are durably stored, so a failed *later* attempt blocks a restart
until the next successful commit (previously it blocked it silently; now it says so). A restart while
a DLC refresh is in flight joins that worker before the handshake (`DrainRefreshForRestart`), a wait
bounded by the scan rather than by a timeout — deliberate, since the alternative is a dying process
handing its library over. `Config: unknown cvar 'restart-handoff'` on the child's boot is P11a's
documented shape (`src/main.cpp`: the one-shot switch stays on the process's own command line), not a
regression. The commit itself is still synchronous on the guest thread, so a slow write would stall
frames; the reads are local `MoveFileExW` replacements and no boot has shown a stall (measured: 22 ms
between `set` and `commit`'s log line), and the query contract is therefore truthful by construction.

**Reproducing it** (from `<checkout>`):

```powershell
cmake --preset win-amd64-release ; cmake --build --preset win-amd64-release
pwsh -File out\_swap\p3\run.ps1 -Label after-close   -PromptAction close     # dismiss the confirmation
pwsh -File out\_swap\p3\run.ps1 -Label after-restart -PromptAction restart   # Restart now: one child
```

### P4 — Help-caption appearance, visibility setting, and achievement wording

Preliminary findings:

- `[tree]` [help_overlay.cpp](../../../src/ui/help_overlay.cpp):22 already defines the live
  `settings_help_overlay` cvar, default true. `OnDraw` uses a black background alpha of **0.72**,
  a bottom margin of **6%** (minimum 24), and a width of **72%** (minimum 320). It does not select
  a caption-specific font or font size.
- `[tree]` [imgui_drawer.cpp](../../../rexglue-sdk/src/ui/imgui_drawer.cpp):171 installs the
  small ProggyTiny font at 10 px and offers a font setup callback. The separate Controller
  Settings app has a host-font loading pattern in `controller-settings/main.cpp:652`; its
  font atlas/context is not automatically shared with the game overlay.
- `[tree]` [help_topics.cpp](../../../src/ui/help_topics.cpp), `CaptionFor`, explicitly supplies
  a calibration-welcome caption. Generated restart/close/info rows also carry descriptions in
  [settings_screen.cpp](../../../src/ui/settings_screen.cpp):775. Suppression therefore needs
  to cover both observation-driven and generated caption publishers, including stale captions.
- `[tree]` `settings_help_overlay` is not currently an Interface setting in the model/schema/boot
  path. Existing live application code must be extended deliberately, including effective readback.
- `[tree]` The old achievement wording occurs in `settings_screen.cpp:538` and
  [schema.cpp](../../../src/settings/schema.cpp):151. `SettingId::kUnlockOnlineAchievements`
  is already the action's stable identifier; renaming the label does not require a token migration.
- `[tree]` `tests/settings_screen_tests.cpp:2178` currently expects captions for every row. Update
  that expectation for intentional exclusions instead of leaving contradictory tests.

```text
Improve the in-game settings help captions and apply the requested wording change.
Read P4/D3 in docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md and the
repository's .github/skills/in-game-settings/SKILL.md before changing menu rows.

Use a visibly larger, friendly outline/sans font with DPI-aware sizing and a
reliable fallback. Reduce the dark panel's opacity from its current 0.72 and tune
its styling against the actual native settings buttons. Move the caption upward
into the safe space between menu content and the A/B button area, centered where
appropriate. Do not interpret this as moving it over the title or selected rows.
Measure its bounds; long text must wrap without covering buttons at any supported
aspect or scale. Keep it non-interactive and unable to steal focus.

Hide the extra caption throughout calibration and on informational/confirmation
surfaces such as Restart now, save-error messages and Close. Keep the real message
and controls visible. Keep useful help for ordinary settings and for the Calibration
entry before entering calibration. Clear stale text on transitions, Back and pause
variants, regardless of which caption publisher last wrote it.

Add Interface -> Settings Labels with horizontal On / Off choices, default On.
Reuse the existing runtime flag where suitable, but wire a real persisted setting
through a new unused stable SettingId, schema, store, boot, bridge, menu payload and
live applier. It should hide/show captions immediately after a successful commit
without a restart prompt, remain reachable while Off, and survive relaunch.
Respect explicit config overrides and save failures; allocate no ID by renumbering.

Change the visible action wording to exactly 'Unlock Online Achievements' in all
current generated labels/schema surfaces that name the action. Keep its action ID,
bridge token and behavior. Do not rewrite historical documents merely to remove
their old wording.

Update caption eligibility, transition and persistence tests, including the former
'every row has a caption' assumption. Capture 720p/1080p and ultrawide menus with
long labels, both toggle states, calibration, restart/error dialogs, and pause
variants. Show the A/B controls unobscured and the exact new achievement label.
Record the final font, size/scale, opacity and layout values with screenshots.
```

#### P4 — done (2026-10-14)

**What changed**, in the three parts the prompt asked for. The full record, with the tables of
values and the per-observation evidence, is [status.md](../../settings/status.md) § "Row
descriptions" (the P4 subsection); this is the summary.

1. **The caption's appearance.** The overlay inherited the drawer's own face — ProggyTiny at 10 px,
   which is why it read as small and technical — so a **system sans** (`segoeui.ttf` → `tahoma.ttf` →
   `arial.ttf` under `%WINDIR%\Fonts`, the SDK's font as the fallback) joins the atlas in a new
   `RbBlitzApp::OnConfigureFonts` override, is drawn at **2.8% of the display height** (floor 16 px,
   ceiling the 40 px the atlas bakes), sits at a bottom margin of `max(56, 13.5% of height)` — the
   A/B prompt strip is the last tenth of the screen, so the caption is clear of it — and shows on a
   panel whose background alpha dropped from **0.72 to 0.32**. The bake-large/draw-small choice is
   forced, not stylistic: this build's ImGui backend is a *legacy* one (no
   `ImGuiBackendFlags_RendererHasTextures`), so glyphs are preloaded at each font's `LegacySize` and
   any other requested size is scaled from the nearest bake.
2. **The caption-free pages.** A page whose definition carries a message token is the
   informational/confirmation set, and it is exactly three panels — the restart confirmation, the
   save-error report and the achievements confirmation — each with an in-song twin. All six now
   publish the *hide* on entry and on every move instead of a row's description, which also fixes the
   stale caption the prompt names (entering the confirmation from the root's Cancel used to leave the
   departed row's text over the message). Calibration is the other exclusion: `cal_welcome_screen`
   left both `CaptionFor` and `KnowsScreen`, so the observer's transition rule clears the caption on
   the way in. Ordinary pages are untouched, and both halves are asserted (six suppressed pages
   counted; no other page lost its caption).
3. **Interface → Settings Labels** and the **wording**. New stable id `SettingId::kSettingsLabels = 51`
   (nothing renumbered), schema row, storage `[interface] settings_labels`, boot write of
   `settings_help_overlay` at the config rank, and a **live applier** — the surface's second `hot`
   row, because the overlay re-reads that cvar on every draw. The action row and its confirmation
   button now both read exactly **Unlock Online Achievements** (26 characters, inside the row line's
   28-character budget; the old 31-character label was drawn without its value).

**Evidence** (`out/_swap/p4/`, 2026-10-14; harness `out\_swap\p4\run.ps1 -Label captions|labels-off|achievements`):

| Observation | Value |
| --- | --- |
| The face | every run logs `help_overlay: caption font 'segoeui.ttf' baked at 40 px` |
| A caption on an ordinary page, 720p | `captions/shots/03-dlc.png`: *"Remember the content list so later boots skip the long library scan."* above the A/B prompts, A/B unobscured |
| The restart confirmation | `captions/shots/06-restart-prompt.png`: the message and **Close / Restart now** with **no** caption |
| Calibration | `captions/shots/08-calibration.png`: the wizard's own instructions, **no** caption |
| The row, before | `labels-off/shots/02-interface-labels.png` (2560x1080 → 2354x993 client): **Settings Labels: On** highlighted with its caption |
| The live apply | `labels-off.log`: `hot_apply: settings_help_overlay = false (was true), applied to the running game`; `settings_bridge: applied 'interface.settings_labels' to the running game; the row carries no pending state` |
| The effect, same screen | `labels-off/shots/03-labels-off.png`: **Settings Labels: Off** and no caption |
| No restart prompt | **no** `request_restart` line; the route leaves the root and reaches the main menu |
| Durable + survives relaunch | `settings.toml` gains `[interface] settings_labels = false`; a second run of the same route shows **no** caption at the root (`shots-2/01-root.png`) and restores it live when the row is turned back on |
| The wording | `achievements/shots/02-unlock-online.png` (Ultimate target): **Unlock Online Achievements** beside its caption |
| Host tests | **38/38** (`out/build/rename-tests`); `settings_screen` **28,612 checks** with 3 new cases (the caption-free pages, the Settings Labels row, the wording), and `settings_hot_apply` **266 checks** with its hot-set expectation moved from one row to two |

**Honest limits.** No in-song/pause capture, so the confirmation pages' pause twins are pinned by
their generated definitions rather than seen; no 1920x1080 shot (the two captured are 1280x720 and
21:9 2560x1080); the no-system-font fallback was not exercised (all three candidates exist here); the
save-error confirmation is generated and asserted but was not driven, because producing a real write
failure on demand is its own experiment.

**Where the project's checks stand.** `ctest --test-dir out\build\rename-tests` is **38/38**; the
release tree still cannot build a test at all (`REXGLUE_USE_VULKAN=ON` ⇒ the SDK's glslang guard
writes `BUILD_TESTING=OFF`), so the runtime half was measured by booting the release build — and
configuring that tree again is necessary after the test build, because the two share
`rexglue-sdk/out/win-amd64`.

### P5 — Menu music transitions and strict repetition prevention

Preliminary findings:

- `[tree]` [menu_music_pool.h](../../../src/hooks/menu_music_pool.h), `BuildPool` and `Deck`,
  already deduplicate candidates, shuffle a bounded pool, deal each index once per cycle and
  avoid the same index at the next cycle boundary when the pool has at least two entries.
  [menu_music_pool_tests.cpp](../../../tests/menu_music_pool_tests.cpp):155–182 tests that
  bookkeeping. It does not prove that the audible guest stream changes.
- `[tree]` `SongName` uses the final backslash-separated path component as identity. Review
  duplicate aliases, case/path normalization and what happens if a pool is rebuilt; index-based
  history and `Deck::Reset` do not preserve a song-identity history across pool changes.
- `[tree]` [shell_music.cpp](../../../src/hooks/shell_music.cpp):358 configures a pool capped at
  50 songs. `StartNextTrack` at line 284 loads the next guest stream only after rotation is
  triggered. No next-track prebuffering is established by this path.
- `[tree]` The `sub_827586D8` poll hook at line 565 detects either stream teardown or a backwards
  sample-position jump larger than `kRewindSlack` (44100 samples). The source documents that
  the native stream loops; changing its `loop` argument alone does not disable that behavior.
- `[hypothesis]` A missed seam, changed stream object, stalled stream, single playable entry,
  duplicate identity, failed load or long trailing silence can look like a broken deck or slow
  transition. Instrument the audible lifecycle before changing randomization.

The owning study is [main-menu.md](../../engine/main-menu.md), part 2. Coordinate with
[Play Current Song](2026-10-14-play-current-song.md), which consumes the current
pool/song identity; a transition must not advertise a different song from the one actually playing.

```text
Fix menu background music gaps and repeats. Read P5/D4 in
docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md, src/hooks/shell_music.cpp,
src/hooks/menu_music_pool.h and docs/engine/main-menu.md part 2.

Reproduce on the installed build with a known multi-song library and capture
song identity, stream identity, sample position/rate, natural end or rewind,
next-choice decision, load completion and audible start. Separate intentional
silence inside a song from silence added by the transition. Test actual end-of-song
playback; directly calling Deck::Next is not runtime evidence.

Enforce D4 through the real playback lifecycle: no native loop of the current song,
no immediate repeat even across reshuffle/menu re-entry, and no repeat before the
eligible rotation is exhausted. Preserve history by canonical song identity where
pool rebuilds/refresh would otherwise lose it. State whether the rotation covers
the whole playable library or the current bounded pool; account for the 50-song
cap explicitly. Treat zero/one-song libraries and failed songs deliberately so a
single remaining track cannot loop forever. Quarantine a failed candidate for the
attempt/rotation and advance with bounded retries, preserving menu responsiveness.

Fix seam/end recognition at the verified guest lifecycle boundary. Reduce measured
transition overhead; if prebuffering is needed, first establish loader ownership
and safe handoff so streams are not torn down or guest calls made on a worker.
Do not use a guessed universal song duration or repeatedly reload on every frame.
Keep Play Current Song synchronized to the audible track and clear it when no DLC
track is playing. Preserve normal behavior when DLC Menu Songs is Off.

Test duplicate packages/aliases, normalized identities, deck boundaries, failed
loads, short tracks, stream replacement, long trailing silence, menu re-entry and
library refresh. Capture multiple real transitions and at least two full rotations
of a small fixture pool. Report median/p95/max transition gaps and their sources
before/after, with a concrete target based on those measurements. Acceptance
requires zero repeated audible tracks within the defined exclusion window and
no unexplained multi-second transition stalls. Record actual runtime evidence
separately from the existing menu_music_pool unit-test result.
```

#### P5 — execution record (2026-10-14)

**What the rotation is now.** `Deck` in [menu_music_pool.h](../../../src/hooks/menu_music_pool.h) — an
index shuffle that started over whenever the pool changed size — is replaced by a `Rotation` that
remembers **song keys**:

- every distinct song of the pool is heard once before any of them is heard again, and a pick is
  never the song that just played; at the pass boundary the song that has just ended is not allowed
  to open the next pass;
- a pool rebuilt with the same songs in another order cannot replay a song the pass has already
  heard, because the history is identity, not position (the same guard covers a library refresh or
  the toggle going off and on again);
- a song whose load failed sits out the pass it failed in and is tried again in the next one; three
  failures retire it from the boot's rotation;
- a pool that can only offer the song that just played reports "nothing distinct left" rather than
  repeating it. The runtime then hands the menu one of the title's own three tracks
  (`sfx/shell/samples/shellmusic_<n>`, the panel's own pool, loaded through the original
  `ShellMusic::Load` so it is not substituted again) and stops watching it, so the title's music
  loops exactly as it does without this feature. That is the deliberate end of a one-song library
  rather than a loop of one song.

**Runtime.** [shell_music.cpp](../../../src/hooks/shell_music.cpp) picks through the rotation, treats
a track that produces no sample within **3 s** as a failed candidate (quarantine, next candidate, at
most three in a row, then the title's tracks), and keeps `Play Current Song` in step: the index it
answers for is the song the rotation actually handed over, and it reads as none whenever the menu is
not playing one of ours.

**How it was measured.** `out/_swap/p5/run.ps1` boots a build against a disposable user root and a
fixture library of *N* symlinked single-song packages (so the pool **is** *N*), enters the main menu
once with `--mnk_mode=1`, and then leaves the pad alone. The instrumentation in `shell_music.cpp`
logs the only two moments the guest offers: the **seam** (the stream's position jumping backwards,
logged with the position it had reached — which is the song's own length, measured rather than
guessed) and the next stream's **first samples**. `out/_swap/p5/report.py <boot.log>` turns those
lines into a per-transition table. The transition gap is therefore **seam → the next track's first
samples**: the audio device's own buffer latency sits inside that number and is not separable, and a
fade the guest writes into the sample data at the seam is not part of it.

**What the rotation covers.** The pool, not the library: `Configure()` samples up to **50** songs out
of the packages this boot mounted ([menu_music_pool.h](../../../src/hooks/menu_music_pool.h),
`PoolCap`), and the exclusion rule — no song before every other song of the rotation has been heard —
is a guarantee about *that sample*. A 1,773-package library is heard on the menu as a 50-song
rotation, and a different sample every boot; the library-wide promise would need a pool the size of
the library, which is not what this feature is.

**Evidence** (`out/_swap/p5/`, 2026-10-14; every arm windowed 1280×720 with Vulkan, one A press to
enter the main menu and then no input at all):

| Arm | Binary | Pool | Transitions | Observed sequence | Gap, median (min–max) |
| --- | --- | ---: | ---: | --- | ---: |
| `baseline-short` | build tree, pre-P5 | 3 | 3 | `wakebake` → `MaxwellMurder` → `far_ifollowthesun` → `wakebake` | 86 ms (83–250) |
| `after-short` | **installed** `CAD559E37F15…` | 3 | **6 (2 passes)** | `MaxwellMurder` → `wakebake` → `far_ifollowthesun` → *(new pass)* `MaxwellMurder` → `far_ifollowthesun` → `wakebake` → *(new pass)* `far_ifollowthesun` | 82 ms (81–83) |
| `one-short-before` | installed P6 `91079A5D6C95…` | 1 | 3 | `MaxwellMurder` → `MaxwellMurder` → `MaxwellMurder` | — |
| `after-one-short` | **installed** `CAD559E37F15…` | 1 | 1 | `MaxwellMurder` → *the title's own track* | 83 ms |
| `one-long` | installed P6 `91079A5D6C95…` | 1 (72 MB file) | 0 in 6:52 | `highvoltage_live` | — |

Each arm's `arguments.txt`, `hashes.json`, `boot.log`, `timeline.txt`, `result.json` (and `final.png`
where the arm ended on its own) sit in its directory; `out/_swap/p5/report.py <boot.log>` reproduces
the tables above.

What the arms establish, rule by rule:

- **Every song before any repeat, no song after itself** (`after-short`): six transitions over two
  full passes of a three-song pool, with the two pass boundaries logged explicitly (`opening a new
  pass over the 3 song(s) of the pool`) and zero cases of a song following itself. In the second pass
  the order differed from the first, so the boundary is a fresh pass rather than a repeat of the
  previous order.
- **A one-song pool is not a rotation**: the old installed build reloaded the same song at every seam,
  three times (`one-short-before`: `MaxwellMurder` → `MaxwellMurder` → `MaxwellMurder`). The new build
  plays it once and then logs `the DLC pool has nothing new to play; the menu goes back to the title's
  own tracks ("sfx/shell/samples/shellmusic_2.mogg")` (`after-one-short`), after which the poll stops
  watching, so the track that follows is the title's own and loops as it does without this feature.
- **The transition gap is small and is not host work**: median 82 ms in the installed arm
  (`after-short`), 86 ms before it, with the `ShellMusic::Load` call inside that window returning in
  0–2 ms. The gap is the guest's own stream startup plus the audio device. No prebuffering was added,
  and no call was moved off the guest thread, because the measurement did not ask for either.
- **`Play Current Song` follows the audible track**: the pool index the probe answers for is the song
  the rotation handed over, and it is cleared to none both when a candidate fails and when the menu
  falls back to the title's own music — so the row reads unavailable exactly when a DLC track is not
  playing.

**Validation.** `menu_music_pool` is **4,427 checks** in 13 cases — pool identity/dedup/cap/shuffle, the
rotation hearing every song of a pass before repeating one, 300 picks × 7 pool sizes with no song
following itself, the one-song rule, identity kept across a rebuilt pool, a failed candidate skipped
and retried in the next pass (retired after three strikes), and the pass-boundary flag. The whole
Vulkan-off suite is **39/39** (`ctest --test-dir out\build\rename-tests`, re-run on these sources), and
`check_docs.py` reports 0 errors. The runtime half is the five arms above rather than the unit tests:
the "before" legs ran `out/build/win-amd64-release` (the build tree) or the P6 install, and the "after"
legs ran an installer built from these sources and installed into `out/_swap/p5/install`
(`rb_blitz.exe` = `CAD559E37F1594AE121C785E871A9AE0F4281A0E158D4166814E83C9D0F1DCA2`; the runtime and
GPU plugin are the P6 hashes, unchanged).

**Honest limits.** The gap is a *position-based proxy*: the seam is the guest's own stream restart and
the end is the next stream's first non-zero sample, so the audio device's buffer latency is inside the
number and neither a fade the guest writes at the seam nor any silence inside a song is separated from
it (no audio capture exists in this harness). Nothing here proves whether the seam is the file's true
end — see [main-menu.md](../../engine/main-menu.md) part 2, "What the seam really is (open)": the same
three fixture packages wrapped at a different point in two boots, so the guest is looping at its
streaming buffer rather than provably at the end of the file, and a long commercial track may play only
its buffered prefix on the menu. A failed candidate was **not** induced at runtime (it is pinned by the
unit tests and by the code path, which needs a package whose stream cannot be opened); duplicate
packages/aliases, library refresh and menu re-entry are likewise unit-tested only — the runtime arm is
three distinct packages, and a refresh does not rebuild the music pool today (`SetPool` runs once per
boot), so the identity-across-rebuild rule has no runtime arm. Long trailing silence, short tracks
below the 1 s rewind slack, and the 50-song pool boundary were not exercised in a boot.

**Reproduction.** From `<checkout>`: `cmake --preset win-amd64-release` then
`cmake --build --preset win-amd64-release`; for the installed legs also
`powershell -NoProfile -ExecutionPolicy Bypass -File installer/build.ps1 -SkipArt -RefreshPayload`
and a silent `/CURRENTUSER /GAMEMETHOD=folder /ULTIMATESOURCE=none` install. Then

```powershell
pwsh -File out\_swap\p5\run.ps1 -Label after-short -Build out/_swap/p5/install -Seconds 600 -Fresh
pwsh -File out\_swap\p5\run.ps1 -Label after-one-short -Build out/_swap/p5/install `
  -Songs 'Rancid - Maxwell Murder' -Seconds 300 -Fresh
python out\_swap\p5\report.py out\_swap\p5\after-short\boot.log
```

`-Songs` is the pool (one song per package, symlinked from a folder of single-song packages outside the
checkout); `-DlcMenuSongs false` is the control that keeps the title's own tracks. The arm needs no
particular library: any folder of single-song packages whose names are passed as `-Songs` works.

### P6 — DLC cache effectiveness, status text, and honest progress

Preliminary findings:

- `[tree]` `general.cache_dlc_library` defaults On and is restart-scoped
  ([schema.cpp](../../../src/settings/schema.cpp):117,339); its runtime cvar is
  `enhancements_dlc_cache`. [dlc.cpp](../../../src/hooks/dlc.cpp), `LoadOrScanLibrary` at
  line 238, records a hit only after matching the fingerprint, roots and content-type override.
- `[tree]` The host cache defaults to `<user_data_root>/cache/dlc_library.cache`.
  [dlc_cache.h](../../../src/fs/dlc_cache.h), `FingerprintTree`, still enumerates/stats regular
  files and sorts the fingerprint. A hit avoids container-header scanning, not every filesystem
  operation. Structured DLC is scanned separately in `dlc.cpp:449–462`.
- `[tree]` [menu_filter.cpp](../../../src/hooks/menu_filter.cpp):288 changes
  `server_connect_enumerating_content` to **Loading Song Cache** only after `CacheServed()` is
  true. It logs replacement success/failure. Cache On alone should not force that label.
- `[tree]` Host library caching and guest `songcache:` are different layers. The guest can still
  mount packages and load/build song metadata on a host-cache hit. See
  [DLC guide](../../guides/dlc.md), sections 4.1–4.3.
- `[tree]` `g_content_count` at `dlc.cpp:509` counts only the flat library. In
  [content_progress.cpp](../../../src/hooks/content_progress.cpp):284, `CountItem` estimates
  total work as twice that count. `WatchXmsg` counts matching calls without checking their item
  success/completion result, and progress is clamped to 100%. Structured/user-content sources
  and post-enumeration work are not represented by that denominator.
- `[tree]` The same approximate completion restores VSync; guest completion also has a separate
  full-bar/reset path. `[hypothesis]` Early completion, post-enumeration parsing or persistence,
  and pacing restoration could contribute to the plateau. The screenshot does not distinguish them.
- `[tree]` `dlc.cpp:474–482` now disables emulated content-mount latency on every boot. Older
  cache measurements attributed this saving to caching. Treat the
  [finished cache plan](../finished/2026-10-10-song-cache-plan.md) as dated evidence, not proof
  of the current incremental benefit of Cache On.
- `[tree]` [acceptance_dlc.ps1](../../../scripts/acceptance_dlc.ps1) deliberately creates fresh
  user roots for its correctness legs. Repeating those legs alone cannot measure a warm cache.

```text
Investigate and fix the DLC cache/status/progress experience as one loading
pipeline. Read P6/D6 in docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md,
docs/guides/dlc.md and the cited cache, discovery, locale and progress modules.

Start with the installed artifact from P1 and capture effective cache setting and
source, restart status, resolved DLC/user/cache roots, actual hit/miss reason,
cache-write outcome, guest songcache reuse, and package counts by source. Verify
the precise locale record being patched and whether the installed provider uses
it. Do not replace the label with 'Loading Song Cache' merely because On is saved.

Timestamp fingerprinting, host cache load/header scan, guest enumeration, mounting,
song metadata parsing, guest cache persistence and first usable menu. Record when
the bar first looks full and the remaining wait. Correlate the full-bar interval
with actual work and VSync/pacing transitions before proposing an optimization.

Compare the same library/build/settings with cache Off, On with cold host cache,
On with warm host cache, and explicit refresh. Control cold/warm guest songcache
separately and record OS filesystem-cache/order effects. Reuse a disposable profile
for warm legs; never clear the owner's live caches or saves. Run at least three
comparable trials per timing condition and report total and phase times, spread,
absolute savings and percentage. Do not reuse old mount-delay savings as proof
that the present cache is faster.

Fix demonstrated cache misses, invalidation/path/write errors or locale integration
bugs. Warm unchanged libraries must actually reuse the cache and report that path;
cold/invalid/refresh runs must report real discovery. Preserve added/removed/changed
content detection and safe recovery from corrupt or unwritable caches. Optimize
the measured expensive phase without weakening invalidation or omitting songs.

Replace the approximate progress logic with a tested model of the actual sources,
phases and completed work. Enumeration terminators and failures are not successful
items. Do not show overall completion while mounting/parsing/persistence is pending;
use a clear finalization phase if a total cannot be known accurately. Keep progress
bounded and monotonic within a phase, reset it on a new attempt, and tie pacing
restoration to verified completion. A cosmetic timed ramp or a permanent 99% cap
does not resolve the underlying issue.

Cover empty, single-package, large flat, structured-only and mixed libraries;
missing/corrupt cache, changed roots/content-type override, unreadable/unwritable
paths, content changes, explicit refresh and restart. Extend dlc_cache, dlc_library,
dlc_layout, refresh_state and menu_options coverage as relevant. Add focused phase/
counting tests if the progress model changes; no dedicated content-progress test
currently exists. In the installed game verify final song count and DLC-only song
playability as well as the label and bar. Report warm-cache gains and full-bar dwell
before/after, including any remaining load cost and unverified phase boundaries.
```

#### P6 — execution record (2026-10-14)

**Implemented.** The host cache now has reason-bearing timings, checksum validation, atomic
replacement and refusal to persist incomplete/unstable scans. The English hit label is **Loading
Cached Content List**, delivered through the installed settings provider's whole locale record.
Cold/Off/refresh use the discovery label. A real aggregate-enumerator model replaces the flat-count
times-two estimate, excludes terminators/failures/pending calls, handles interleaved enumerators,
and shows an unnumbered finalization phase. Loading-phase uncapping works independently of Cache
On and restores the player's V-Sync at the verified usable main menu. Details and boundaries:
[DLC guide §4.4](../../guides/dlc.md#44-current-loading-pipeline-p6-2026-10-14).

**Root causes reproduced.** In the P1 installed artifact (`out/_swap/p1/install`), with 1,773
flat packages and a copied guest cache from the older 1,402-package library, the first 100% event
was `18:00:27.160`; V-Sync was restored one millisecond later. The guest then set a metadata
total of **742**, mounted packages and only completed at `18:02:10.577`. The usable menu was
`18:02:10.581`: **103.421 s after the full-bar event**, **117.726 s after the discovery screen**.
This is post-enumeration work, not a failed host-cache hit (`before-large-1/boot.log`). The prior
full-bar screenshot filename in that directory was captured late and actually shows the menu;
the timing evidence is the log, not that screenshot.

The initial baseline also reproduced an independent locale integration defect: a split-read
edit changed the expected guest checksum before checking whether its edited first chunk could
still be delivered. Rejecting the edit then caused a dirty-disc error. Digest edits are now
committed only after delivering bytes, with a regression test. The installed provider edits the
whole English record before archiving it. Completed IOSB status/byte count are used for reads;
the SDK may return pending even though its synchronous read has completed. Scatter segment
tables are no longer scanned as if they were contiguous file contents.

**Artifact and controls.** Fresh dual-backend release, installer built using
`powershell -NoProfile -ExecutionPolicy Bypass -File installer/build.ps1 -SkipArt -RefreshPayload`,
then `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCANCEL /CURRENTUSER /DIR=<disposable-install>
/GAMEMETHOD=folder /GAMEFOLDER=<checkout>/game /ULTIMATESOURCE=none /RUNATEND=none`.
The installed test runs use Vulkan, 1280×720 windowed, Ultimate, DLC Menu Songs Off, uncapping On,
one header-scan thread and zero emulated mount delay. The launcher profile is explicitly empty;
all user/cache roots are under `out/_swap/p6/`. `arguments.txt`, `hashes.json`, `boot.log`, screenshots
and `result.json` in each run record the exact invocation, effective roots, setting source and
outcome. Cache On/Off and refresh are command-line overrides in these comparisons; settings-row
persistence/restart UI is not a new acceptance claim here.

The working tree is modified on top of `88cd2e543cadb6158e40b213ae1f974160a19a48`; the manifest's
commit alone is not build identity. The measured executable SHA-256 is
`6281D66F570278FAAAB87239C4E064561051675D36616E86B30C99B1C4BE13BC`, runtime
`7EE201B89E9F0F58E09F3BD7A572EF3972C0BB675B884AA61EE0533408CABDA3`, GPU plugin
`AD1D3501F0B6C5EB290497803D255F16DFD9F3B90524BF85DA17483C38EDC842`.
All three match build → payload → install (`artifact-hashes.json`); the packaged loader hash is
`C939A1984BEF03DE9BB8FC0CF7D961D2C722CBF244E8683899C903990E59014A`.

Those are the **matrix's** hashes. The final acceptance legs below ran the last build of the same
tree (`cmake --preset win-amd64-release` → `cmake --build`, 18:35), whose executable SHA-256 is
`91079A5D6C950EE1405171B42A2AFE794EA1A0DF5E794B388601E1ABF8064E12`, runtime
`5326DBCCC1F661EC0F37A7395E5D79822B87176DF5332817B949AB5E115D588C`, GPU plugin
`B745F2502A7AF231938AAACB1CD212E2F300EFCAEF10E1B7243601844BBEC080` — each matching
build → payload → install (`artifact-final-hashes.json`), with the same loader hash. The two builds
differ only because the log-field rename landed after the matrix had started: the per-file digests,
not the recorded commit, are what decide identity.

**Controlled installed trials.** `matrix-<trial>-<condition>/` uses the same 1,773-package library
and a fresh copy of the same 1,009,556-byte guest cache for every leg. This is **partially warm**,
not fully warm: 371 packages are outside that saved cache. Off has no host-cache file; cold On has
none; warm On starts with a validated host-cache copy; refresh starts with that copy and forces
scanning. Trial order is Off/cold/warm/refresh, refresh/warm/cold/Off, cold/Off/refresh/warm.
The OS filesystem cache is uncontrolled and already warmed; none of these means cold storage.
Process-to-menu time includes startup and the driver's fixed start-press delay, measured from
the first log timestamp; discovery-to-menu uses the new phase clock. The captured native total
and increment count are 742; STFS mount lines and metadata IO account for the remaining phase.
Individual parser CPU time is unmeasured.

Three trials per condition, all 12 reached the usable menu. Values are median (min–max).
Host time is the sum of fingerprint/load/scan/verify/write measurements; other columns are seconds.

| Condition | Host phases, ms | Process → menu, s | Discovery → menu, s | Enumeration, s | After enumeration, s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off | 189.494 (174.201–250.422) | 41.380 (41.295–41.556) | 31.777 (31.695–31.841) | 14.046 (14.034–14.047) | 17.743 (17.649–17.794) |
| On, cold host cache | 228.311 (222.923–229.453) | 41.425 (41.422–41.683) | 31.823 (31.559–31.934) | 14.048 (13.920–14.208) | 17.726 (17.640–17.775) |
| On, warm host cache | 14.603 (14.598–16.852) | 41.201 (40.967–41.271) | 31.730 (31.639–31.739) | 13.986 (13.983–13.986) | 17.744 (17.656–17.754) |
| Explicit refresh | 230.747 (220.191–254.734) | 41.281 (41.160–42.528) | 31.733 (31.587–31.842) | 14.020 (13.944–14.082) | 17.712 (17.643–17.760) |

Warm hits: **3/3**. Each leg delivered **3,546** successful item results, **0** enumeration failures,
**742** metadata reads and **1** guest-cache read; none wrote guest songcache. Its SHA-256 stayed
unchanged in every leg (`guest-cache.json`). The `batches` field in these logs is the last batch ID,
not a count; unique handle/batch records establish **three** enumerators (1,773, 1,773, 0). The final
build renames that log field `last_batch` and corrects a stale pacing log sentence; behavior is unchanged.

Median warm host-phase saving against Off: **174.891 ms (92.3%)**. Process-to-menu medians differ
by only **179 ms (0.43%)**, with overlapping ranges; three OS-warm runs do not establish a reliable
end-to-end cache speedup beyond the measured header-scan saving. Guest discovery is effectively
unchanged by host-cache On/Off. Cold/refresh additionally pay fingerprint verification and atomic
cache persistence; those costs are deliberately visible rather than attributed to guest discovery.

The same production loader was also measured with no game/build running (`host-benchmark-2.csv`),
three trials each, one scan thread, same 1,773 packages and already-warm filesystem:

| Condition | Host-only wall time, ms, median (min–max) |
| --- | ---: |
| Off | 154.671 (151.906–176.548) |
| On, cold host cache | 210.096 (194.322–211.895) |
| On, warm host cache | 10.936 (10.398–11.988) |
| Explicit refresh | 206.212 (199.544–209.901) |

That host-only median warm saving is **143.735 ms (92.9%)** against Off. The earlier
`host-benchmark-1.csv` is retained but was collected alongside a running game and is not the clean
comparison above. No old 100-ms mount-delay savings are included in either cache claim.


The first installed comparison (`installed-large-cold`) reached the usable menu in **31.611 s**,
including **17.675 s** after enumeration, with 3,546 successful items, zero enumeration failures,
742 metadata reads, one guest-cache read and no guest-cache write. That single before/after
comparison removes **86.115 s (73.1%)** of discovery time; it is a pacing/finalization improvement,
not a host-cache saving. `installed-large-cold/finalization.png` and
`matrix-1-warm/finalization.png` verify discovery versus actual-hit text and the unnumbered
**Loading song information...** state. Model tests ensure no terminal 100% bar is left visible;
these captures do not constitute a frame-by-frame visual trace.

**Validation.** `ctest --test-dir out/build/rename-tests --output-on-failure`: **39/39 passed**
(`tests-final.log`); this SDK-free test configuration exists because the Vulkan release configure
disables `BUILD_TESTING` through SDK glslang. It includes empty/one/large/structured-sized/mixed-sized
enumerator cases, handle interleaving/reuse, reset, failures/terminators/pending calls, actual
host-loader Off/cold/warm/refresh/type/change/corruption/unavailable/write-refusal cases and split
delivery/locale-provider tests. The new split regression was rebuilt before the final suite.
Installer tests: **421 checks passed**, fresh setup built (`installer-build2.log`). Filesystem
tests initially failed inside the sandbox and passed unchanged with normal filesystem access.
SDK patch check: zero failed patches, 40 expected modified files; only the existing nested
`thirdparty/fmt/support/.gradle/` untracked directory was flagged. P6 makes no SDK changes.

#### P6 — final installed acceptance (2026-10-14)

One installer was built from the final tree and installed into a disposable directory
(`/VERYSILENT /CURRENTUSER /GAMEMETHOD=folder /ULTIMATESOURCE=none`). Every leg boots that installed
copy as an ordinary user with an empty launcher profile, a disposable user root, Vulkan, 1280×720
windowed and DLC Menu Songs off; the harness is `out/_swap/p6/boot.ps1` driven by
`out/_swap/p6/layouts.ps1`, and each leg writes `arguments.txt`, `hashes.json`, `boot.log`,
`result.json` and `shots/` under `out/_swap/p6/<leg>/`. The binaries it ran are the second hash set
above (`artifact-final-hashes.json`).

| Leg | Content | Host cache | Outcome | Process → menu | Pipeline total | After last enumeration | Items | Metadata reads | Guest cache r/w | English label |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `final-empty` | no flat library, no structured root | none | usable menu | 15.1 s | 0.209 s | 0.030 s | 0 | 0 | 0 / 1 | *Discovering Downloadable Content* |
| `final-single-cold` | one flat package (`soundtrack-flat`) | cold | usable menu | 20.1 s | 5.286 s | 5.105 s | 2 | 2 | 0 / 1 | *Discovering Downloadable Content* |
| `final-single-warm` | the same package and profile | **warm** | usable menu | 14.5 s | 0.085 s | 0.022 s | 2 | **0** | **1 / 0** | **Loading Cached Content List** (`host_cache_hit=true`) |
| `final-structured` | `game/dlc` only (21 structured packages) | cold | usable menu | 15.5 s | 0.320 s | 0.147 s | 2 | 2 | 0 / 1 | *Discovering Downloadable Content* |
| `final-mixed` | one flat package + `game/dlc` (21 structured) | cold | usable menu | 15.3 s | 0.387 s | 0.190 s | 4 | 4 | 0 / 1 | *Discovering Downloadable Content* |
| `final-large-guest-cold` | 1,773 flat packages, guest cache empty | cold (miss) | usable menu at 350.998 s; `result.json` says `timeout` because the harness's 360 s deadline expired 1.5 s *before* the menu line the log carries | 361.2 s | 350.998 s | 336.900 s | 3,546 | 3,546 | 0 / 1 | *Discovering Downloadable Content* |
| `final-large-guest-warm` | the same library, profile and binaries | **warm (hit)** | usable menu | 184.5 s | 108.249 s | 94.067 s | 3,546 | 3,546 | **1 / 0** | **Loading Cached Content List** (`host_cache_hit=true`) |

What the legs establish:

- **Every content shape reaches a usable menu**, and the mixed leg's `YOUR SONGS` capture draws the
  library's own rows (`final-mixed/shots/songs.png`: AC/DC *High Voltage (Live)*, All-American
  Rejects, Avenged Sevenfold, Barenaked Ladies, Blink-182), so the final song list is populated
  rather than merely counted. Five of the six legs carry that `songs.png` capture.
- **Only a genuine hit changes the label.** `final-single-warm` is the same library, build and
  settings as `final-single-cold` with the host cache left in place: it reads
  *Loading Cached Content List* with `host_cache_hit=true`, and the guest's own cache is what skips
  the work (`songcache_reads=1`, `metadata_reads=0` against the cold twin's 2).
- **The card is not a completion claim.** The large leg's capture
  (`final-large-guest-cold/finalization.png`) shows the unnumbered **Loading song information…**
  overlay while the screen still reads *Discovering Downloadable Content* — the phase the old build
  reported as a full bar.
- **Cache On is not a promise about the guest.** With 1,773 packages the title still enumerates 3,546
  items and still reads 3,546 metadata records, host hit or not; the saving is the host's own header
  scan, which is what the trial table above measures.
- **The two large legs are a storage measurement, not a cache one.** Same library, same profile, same
  binaries: the guest issued the same **3,546** metadata reads both times, but the host's I/O inside
  them measured **6,130.5 ms** cold against **138.0 ms** warm, and the phase after the last
  enumeration shrank **336.9 s → 94.1 s**. The residual ~94 s is therefore guest-side work, not file
  I/O. The warm leg also logged **more** guest package mounts (5,039 by the time its menu was
  observed, 5,261 after the settle) than the cold one's 3,546, which is unexplained and stays an open
  measurement.
- **The guest's own cache is written once and never filled.** The 16-byte version-11 stub the cold leg
  created was still 16 bytes — **0 packages** — through the warm leg, sampled every 2 s across the
  60 s settle window (`final-large-guest-warm/guest-persistence.csv`: 30 samples, unchanged mtime).
  The title re-mounts and re-reads the whole library on every boot.

**Honest limits.** "Warm" in this record always means the **host cache file**, never storage, and the
OS filesystem cache is uncontrolled and demonstrably moves these numbers (the 6,130.5 ms → 138.0 ms
host I/O above is the same library, build and settings). A first attempt at the warm twin, run an hour
later, mounted **1,579 of 3,546** packages in 364 s and was stopped by a 600 s deadline; it is kept
only as the reason the deadline was raised (`final-large-guest-warm-deadline600/`) and its timings are
not comparable. The installed copy is a per-user folder install under `out/_swap/p6/install-final`
rather than the `Program Files` default, so an admin install's write policy is not exercised; **no leg
played a DLC song** — the mixed leg's list draws the library, but DLC-only playability stays unverified
and contradicted by the open song-start fault in
[known-issues.md](../../reference/known-issues.md); the ~94 s is not broken down by stage (host I/O is
138 ms of it; the rest is guest parsing/scheduling, per-stage CPU unmeasured); the extra mounts are
unexplained as stated above; and the overlay text is a frame capture, not a frame-by-frame trace. The
harness kills the process as soon as it sees the menu, so end-of-boot guest persistence is observed
only through the settle window (`boot.ps1 -SettleSec`).

**Reproduction.** Build with `cmake --preset win-amd64-release` then
`cmake --build --preset win-amd64-release`, package/install as above. Launch the installed copy with
`--game_data_root=<checkout>/game --user_data_root=<new-disposable-profile>
--launcher_profile=<empty-toml> --dlc_library=<library> --dlc_root=<absent-structured-root>
--enhancements_dlc_cache=true --enhancements_dlc_uncap_enumeration=true
--enhancements_menu_dlc_songs=false --gpu_backend=vulkan --log_level=info --log_flush_interval=1`.
Use the same settings file (`general.target='ultimate'`, windowed 1280×720), press Start and wait
for `content_pipeline: usable menu`. Copy only disposable guest-cache seeds between comparison
profiles. For Off change the cache argument; for cold omit the host cache; for warm reuse it;
for refresh add `--refresh_dlc_cache=true`. Never clear the owner's cache or saves.

For host-only phase isolation:
`clang++ -O2 -std=c++23 -Isrc tools/dlc_cache_benchmark.cpp -o <output>/dlc_cache_benchmark.exe`,
then `dlc_cache_benchmark.exe <library> <new-output-directory>`. It runs three trials of each
condition through the production loader and emits CSV. It does not measure guest loading or
claim to flush the OS cache. The full installed matrix's raw timings are `out/_swap/p6/matrix.csv`.

### P7 — "Settings could not be saved" in an installed copy

Preliminary findings (2026-10-14; read off the installed log and the tree — no boot was taken during
this research):

- `[local]` `<install>\logs\rb_blitz_001.log`, the 22:08 boot of the install the owner was testing:
  five edits, and five times one `settings_bridge: set '<row>' -> ok (staged)` immediately followed
  by `settings_bridge: commit '-' -> io error (gameplay.forceplot cannot hold that value,
  gameplay.which_skybox cannot hold that value)`. `settings.toml` was never written (its mtime
  stayed at 12:31 while that boot wrote its own graphics marker at 22:09), and the screen showed the
  surface's own `settings_save_error_message`: *"Settings could not be saved. Check that the settings
  folder is writable, then try again."*
- `[local]` `%APPDATA%\rb_blitz\settings.toml.commit-journal` — written at 12:31, never removed —
  holds `[native] 2 = "false"` (id 2 is `kCacheDlcLibrary`) and `[provider] 65, 66, 67 = "true",
  "plot_bodega_a", "nfs_mw2012"`.
- `[tree]` `Service::Commit` runs `RecoverCommit` **before** it writes anything, and recovery's last
  step is `provider_->Save(provider)` → `EncodeProviderValues`. A journal whose provider rows the
  encoder cannot spell therefore fails recovery, which fails the commit, and the journal is left in
  place — so **every** later save on that installation is refused, whatever row it changes.
- `[tree]` The two rows named in the error are the payload's *boolean-with-submenu* pair: the store's
  descriptors are `ValueKind::kEnum` over the payload's inventory
  ([architecture.md](../../settings/architecture.md) part 5 §3) and the screen offers those tokens,
  while the adapter's bindings declared them `RowKind::kBool`. That is
  [status.md](../../settings/status.md) item 18, and it is the source of the unencodable value.
- `[tree]` The error text is chosen by the guest from one bit: `CommitResult` in
  `src/ui/settings_screen.cpp` enters the save-error page whenever `snapshot/commit.ok` is false, so
  a conflict, a refused value or a provider failure all wore the folder's text.
- `[unproven]` Whether the owner's installation ever wrote a *provider* file: `<user_data>\ultimate`
  exists but is empty, so every provider value in play came from the payload's compiled defaults.

```text
Fix "Settings could not be saved" for every row in an installed copy. Read P7 above,
docs/settings/architecture.md part 3 §4 and part 5 §3, docs/settings/status.md item 18, and
src/settings/{service,bridge_sink,ultimate_adapter}.cpp with src/hooks/settings_bridge_hooks.cpp.

Reproduce it in a boot; the installed log alone names the io error and the two rows. Make the
provider row's value path consistent end to end: one representation for the screen, the store and
the request, and the provider's own spelling in its file, with the payload's inventory as the only
authority. A row the player can press must be a row a commit can encode.

Then make the failure impossible to repeat. A save that finds an interrupted save it cannot finish
must not refuse forever: keep the bytes, name the reason, and let the player save - and prove a
retry works. A save that fails on screen must say what failed instead of blaming the settings
folder.

Extend the host suites (ultimate_adapter, settings_service, settings_bridge_service,
settings_screen) with the failing value, with a journal nobody can finish, and with a retry after a
failure. Seed the owner's own journal into a driven boot and show the edit saves.
```

#### P7 — done (2026-10-15)

**What was wrong.** Two independent defects on one path, both visible in the installed log alone.

1. **The two flat-panel rows could not be spelled.** `forceplot` and `which_skybox` are the payload's
   *boolean-with-submenu* pair: the payload's row is a `uOnOFF` toggle, its choices live in a panel of
   its own, and that panel's `SELECT_MSG` stores the selected **position**
   (`{set $which_skybox {$component selected_pos}}`). This build draws the whole choice set as one
   select over the payload's tokens — which is what the screen, the store and a request all carry —
   so the file is the only place that needs the position, and the bindings said `RowKind::kBool`
   instead. A press on either row staged a token the encoder refused, and because that commit also
   carried a *native* row it wrote the recovery journal first: `<settings>.commit-journal` survived
   the failure, and held a provider value nothing could ever encode.
2. **A journal nobody can finish refused every later save.** Recovery runs inside each commit, so the
   journal above answered `io error` for *any* edit from then on — the installed log's five
   consecutive failures — and `bridge_sink` maps that to the guest's save-error page, whose text
   ("check that the settings folder is writable") described a failure that was not about a folder at
   all. The committed native half of the interrupted save (`cache_dlc_library = false`) was on disk
   the whole time.

**What changed.** `RowBinding::position_value` (in `ultimate_adapter.h`) says a row's file value is
the choice **position**, and the two rows are `kSelect`; the shape check accepts the payload's
boolean row for exactly that pair and now *requires* the flat choice list that names the positions
(`the provider declares no choice list for 'forceplot'`). `ConfigureProvider` resolves a
file/default position into a token against the payload's inventory (dropping, with a note, a value
that names no position) and `InstalledProviderSink::Save` resolves the token back into a position.
`Service::Commit` **parks** a journal it cannot finish (`<settings>.commit-journal.stale`, numbered
if taken; the bytes are kept) and lets the player's own save go ahead, recording why through
`TakeRecoveryNote()` — which the boot logs and a commit appends to its own log line, so a park can
never be silent. The save-error page now asks `snapshot/commit.failed/t:<reason>` and draws one of
four texts (`not_writable` keeps the folder's wording; `provider` says the settings file saved and
the mod's file did not; `conflict`; `other`).

**Evidence** (`out/_swap/settings-save/`, 2026-10-15, `out/build/win-amd64-release`, D3D12,
windowed, `ultimate` target with the installed payload and empty DLC roots; the harness is
`out/_swap/settings-save/run.ps1 -Label X -SeedJournal <journal>`, which seeds the owner's **real**
journal and the native row it expects):

| Observation | Value |
| --- | --- |
| The failure, verbatim | The owner's install: `set 'general.dlc_scan_threads' -> ok (staged)` then `commit '-' -> io error (gameplay.forceplot cannot hold that value, gameplay.which_skybox cannot hold that value)`, five times, with `settings.toml` untouched |
| The harness reproduces it on the **shipped** binary | `control-installed` — the installed `rb_blitz.exe` (from `installer-latest`, i.e. *without* this fix) run through the same route with the same seeded journal: `set 'general.cache_dlc_library' -> ok (staged)` → `commit '-' -> io error (gameplay.forceplot cannot hold that value, gameplay.which_skybox cannot hold that value)`; `settings.toml` keeps its pre-boot mtime (the 08:08:43 press wrote nothing) and the journal is still there (`out/_swap/settings-save/control-installed-userdata/settings.toml.commit-journal`, the owner's own 104 bytes). A harness that cannot reproduce the failure proves nothing about the fix; this one does |
| The same route, after the fix | `armA-owner-journal` (journal seeded, nothing native committed yet): `set 'general.cache_dlc_library' -> ok (staged)` → `commit '-' -> ok (committed)` **twice**; `settings.toml` written at 07:52:59 with `cache_dlc_library` and `'general.cache_dlc_library' = 'manual'`; the journal is gone |
| Recovery *completes* the interrupted save | `armB-owner-journal-recovered` (its native half already on disk, as the owner's install had it): the journal is finished during the provider install, and the provider's own file is created as `(0 (disable_plots TRUE) (forceplot 1) (which_skybox 2))` — the **positions**, `plot_bodega_a` = 1 and `nfs_mw2012` = 2, in the file the provider's loader reads; both edits after it commit (`ok (committed)`) |
| A journal *nobody* can finish no longer refuses the save | `armC-unknown-token` (the same journal with `66 = "plot_does_not_exist"`): the boot warns `settings_boot: Interrupted provider settings are incompatible with this installation; the next save will set the journal aside`, and that save answers `commit '-' -> ok (committed; the interrupted save was kept as settings.toml.commit-journal.stale (Interrupted provider settings are incompatible with this installation))` — the note is reported **once** (the next commit is a plain `ok (committed)`), the parked file still holds the journal's 119 bytes, and `settings.toml` was written |
| The surface stays editable | `04-toggled.png`: *Help & Options → Downloadable Content* with **Cache DLC Library: On** selected and **no** error dialog; the second pass toggles it back and commits again |
| Host tests | **39/39** (`out/build/rename-tests`). New: `settings_service` pins that an uncompletable journal is parked and the save still lands (and that the parked bytes and the reason are both kept), `settings_bridge_service` pins the reason bits plus a retry after an I/O failure and after a provider failure, `ultimate_adapter` pins the pair's token ⇄ position contract and the refusal of a bare boolean, `settings_screen` pins that the save-error page asks why and that every message it can draw exists in the locale |
| Docs check | `python .github/skills/documentation/scripts/check_docs.py` — 0 errors, the same 29 known warnings |

**Honest limits.** The control leg runs the *installed* binary and the fix legs run the rebuilt
`out/build/win-amd64-release` one, both against a disposable root and the owner's own journal — so
the pair is a before/after on the same harness and the same state, but the *fix* has not been taken
through a freshly built installer and a real install. The parking rule means an interrupted save that
cannot be completed is kept as `<settings>.commit-journal.stale` rather than finished, so a future
build that *can* finish one would have to be pointed at that file by hand; nothing reads `.stale`
automatically, by design (guessing at it would be worse than saying so). `forceplot`/`which_skybox`
are still `restart` rows, so a saved plot or skybox is applied on the next launch, not live. The
`provider` failure text assumes the Ultimate provider is the only provider this build has, which is
true today.

**Reproducing it** (from `<checkout>`):

```powershell
cmake --preset win-amd64-release ; cmake --build --preset win-amd64-release
Copy-Item "$env:APPDATA\rb_blitz\settings.toml.commit-journal" out\_swap\settings-save\journal-owner.toml
pwsh -File out\_swap\settings-save\control-installed.ps1 -SeedJournal out\_swap\settings-save\journal-owner.toml  # the installed binary: io error
pwsh -File out\_swap\settings-save\run.ps1 -Label armA-owner-journal -SeedJournal out\_swap\settings-save\journal-owner.toml  # the fix: ok (committed)
```

## 3. Final installed acceptance and evidence
After the relevant fixes, build one fresh installer from the verified artifacts and run the
following against its installed game as the ordinary player. Keep logs/captures in a disposable
directory such as `out/_swap/installed-review/`; record the exact commands used, without putting
user-specific paths or game assets in tracked files.

| Area | Required evidence |
| --- | --- |
| Provenance | Source revision/working-tree state, build switches, matching binary hashes through payload and install, selected backend and loader result. |
| Display | Fresh-profile first boot on ultrawide; fixed client bounds and correct aspect; mode switches/restart; DPI, HUD and pointer checks. |
| Settings | Repeated navigation/edits, an injected save failure followed by successful retry, and exactly one successful restart with saved values applied. |
| Captions | Larger readable font, lighter panel and safe placement; On/Off persistence; no extra caption in calibration or message dialogs; exact achievement wording. |
| Music | Audible transition recordings/logs, no excluded repeat, small-pool full rotations, zero/one-song/failure behavior, gap measurements. |
| DLC | Cold/warm/Off/refresh comparison, reason-bearing hit/miss logs and truthful text, complete song list, phase timings and reduced or explained finalization delay. |

Use the existing affected CTest targets listed in each prompt, then the project's required checks.
Documentation-only work does not establish those runtime gates. Update the owning live documents
with the resulting evidence; keep historical measurements dated. Run
`python .github/skills/documentation/scripts/check_docs.py` before handing off documentation changes.

Open runtime facts: exact monitor resolution/DPI and target monitor; installed binary identities;
the precise saving message and reproduction route; whether the repeated song is a native stream
loop, immediate replay or a later rotation; and the installed DLC layout/cache state. These can
be collected by the executing agent and should not block independent source fixes or preparation.
