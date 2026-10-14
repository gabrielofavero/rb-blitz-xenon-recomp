# Installed-game review: researched implementation prompts

**2026-10-14 — research complete. P1 and P2 are implemented and verified (their status is below);
P3–P6 are not started.** This file moved from `plans/todo/` to `plans/in-progress/` when P1 landed.

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
