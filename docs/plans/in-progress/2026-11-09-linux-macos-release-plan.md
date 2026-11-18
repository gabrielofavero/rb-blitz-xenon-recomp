# Linux and macOS release plan

**Status: in progress, updated 2026-11-16.** The settings pre-step, Prompt 0 planning inputs, and Prompt 1 shared implementation have Windows build and non-UI test evidence below. The owner resumed macOS packaging after the M1 bring-up: the direct builder and audited Apple Silicon development `.app`/`.pkg` are now delivered. Installed launch, security flow and release acceptance remain pending. Linux and Intel Mac gates remain open. Mobile belongs in a separate plan.

## Goal and release contract

A player downloads a prebuilt native package, installs it through the desktop OS, selects their own supported Rock Band Blitz Xbox 360 package or extracted game folder once, and plays without an SDK, compiler, terminal, or copy of this repository. Optional Rock Band Blitz Ultimate and DLC remain separate user choices. A package contains the authored recompilation, runtime dependencies, licensed project assets, and no retail game files, keys, user DLC, or mirrored Ultimate files.

| Target | First release artifact | Player path | Release gate |
| --- | --- | --- | --- |
| Windows | `RockBandBlitz-<version>-windows-amd64.exe` | Existing Inno wizard imports the user's game data. | Preserve current installed behavior and release audit. |
| Linux x86-64 | One `.deb` for Debian/Ubuntu and one `.rpm` for Fedora-family systems | Install in a graphical package manager, launch from the application menu, import the game package/folder in a pre-game window. | Both installed packages play on native Linux hardware. |
| macOS Apple Silicon and Intel | One `.pkg` per architecture, or one universal `.pkg` only after both slices and dependencies are validated | Run Installer, use the documented Gatekeeper override if needed, launch the `.app` from Finder, then import the game package/folder. | Each advertised architecture plays on a real Mac. |

The existing [one-file Windows release model](../finished/2026-10-15-release-model.md) stays true **per platform artifact**. A multi-platform GitHub release necessarily has several assets: the Windows setup, Linux `.deb` and `.rpm`, and the tested macOS package variant(s). Keep one version from `installer/config/pins.toml` across package names, installed app identities, and release notes; publish hashes in the notes rather than a separate checksum asset. Do not publish an architecture or distro format before its acceptance gate passes. The exact minimum distro and macOS versions are set from clean-machine testing, not inferred from the CMake preset names.

The initial macOS route follows the attached plan's homebrew approach: an unsigned, unnotarized package and no App Store or paid publisher account. Apple documents the **System Settings → Privacy & Security → Open Anyway** path after a blocked first attempt ([Apple support](https://support.apple.com/en-us/102445)). Verify that the actual package and app launch with that path on every supported macOS version. If a current OS cannot install and run it through a reasonable documented flow, revisit signing/notarization and the artifact choice before calling macOS released. Do not tell players to disable Gatekeeper globally.

## Baseline recorded before Prompt 1

The following inventory describes the starting code and gaps found during planning. Prompt 1's implementation record below identifies which boundaries changed; native validation and player flows remain later gates.

- `CMakePresets.json` already names Linux x86-64/arm64 and macOS Intel/arm64 configurations, and `CMakeLists.txt` enables Vulkan by default. Those are configuration intentions, not evidence that the game compiles or runs there. The macOS Intel preset currently requests `x86-64-v3`; check its hardware floor before promising broad Intel coverage.
- `src/main.cpp` unconditionally includes Win32 headers and implements the pre-entry restart handoff with `CommandLineToArgvW` and `ExitProcess`. `src/platform/crash_diagnostics.cpp` and several hooks also use Win32 APIs directly. `src/platform/game_restart.cpp` has portable policy but a Windows process implementation. The song-load monitor executable is built only on Windows, so recovery needs an explicit Linux/macOS design and acceptance.
- The online store, downloads, album art and Messages runtime have WinHTTP, WIC, and ShellExecute implementations selected only on Windows in `CMakeLists.txt`. Linux/macOS need real TLS transport, image decoding, browser opening, and tested failure behavior; a linkable stub is not feature parity.
- `src/settings/paths.cpp` locates `paths.toml` beside the executable. `src/profile/profile_path.cpp` recognizes XDG config, but user data, cache, logs, DLC, and imported game data do not yet have a complete installed-platform contract. A package's executable tree or `.app` bundle must be read-only at runtime. Use [XDG data/config/state/cache locations](https://specifications.freedesktop.org/basedir/) on Linux and the user's [Application Support](https://developer.apple.com/documentation/foundation/url/applicationsupportdirectory) and Caches locations on macOS.
- The Windows Inno helper (`installer/src`) already probes/imports STFS or an extracted folder, checks fingerprints, writes manifests, and handles optional Ultimate. Its UI and several helper facilities use Win32. Reuse the validation, extraction, and transaction rules in a portable core rather than treating Inno Setup as a portable installer.
- The game build needs the supported `game/default.xex` on the build machine for code generation and the fingerprint gate. Release runners therefore need authorized local source data, even though no such data goes into a release. The current tag workflow is self-hosted Windows only. `config/toolchain.toml` records a Windows acceptance toolchain, not Linux/macOS release pins.
- `scripts/setup/audit_distributable.ps1` and the Windows payload allow-list audit PE and Inno outputs. Extend the same negative checks to ELF, Mach-O, `.deb`, `.rpm`, `.app`, and `.pkg`, including nested resources, generated code/catalogues, native dependencies, personal paths, and credentials.

## Development setup and player installation

| Target | Build machine | Clean install and gameplay evidence | Player requirements |
| --- | --- | --- | --- |
| Linux x86-64 | Native Linux with pinned clang/CMake/Ninja, the ReXGlue submodule and native graphics/audio libraries; VS Code may be used. A self-hosted runner can access the authorized game dump without copying it into artifacts. | Clean Debian/Ubuntu and Fedora-family installations for package behavior; native GPU machine for full play, audio, controller, and display tests. A VM only counts for graphics if acceleration is actually working. | Matching `.deb` or `.rpm`, a supported game package/folder, and a Vulkan-capable driver if the proved renderer requires it. No development tools. |
| macOS arm64/x86-64 | A Mac with pinned Xcode command-line tools, clang/CMake/Ninja, the SDK and authorized game dump. Build each architecture natively or prove the cross-architecture build on a Mac; retain matched native libraries per slice. | Fresh install/update and Finder launch on real Apple Silicon and Intel Macs for every architecture advertised. A macOS VM on Apple hardware can supplement clean-install checks. | Matching `.pkg`, supported game package/folder, and the one-time security override if the unsigned package is blocked. No build tools or publisher account. |

The OS package installer installs only the authored app. It must not prompt for the game archive in a privileged package script. A normal-user pre-game importer supplies the file/folder picker, progress, cancel, errors and repair path; it is a separate launch flow before guest content exists, not a new host overlay over the running guest screen. Preserve the Windows wizard's current import route while sharing the portable import core.

## Installer pattern from `z0mb1es-rec0mp1led`

The sibling project's `installer/linux/README.md`, `package.sh`, and `nfpm.yaml`, plus `installer/macos/README.md`, `build.sh`, `package.py`, and `components.plist`, provide a working packaging pattern to adapt. Its Apple Silicon `.pkg` and first-launch flow have Mac build checks and owner-reported gameplay acceptance. Its Linux `.deb` and `.rpm` were built and audited locally, and a headless import passed; graphical installation, native dialogs, and gameplay are still open there. Use those steps as implementation examples, not as acceptance evidence for Rock Band Blitz. Its .NET/FNA binaries, FFmpeg converter, optional-art flow, and Metal renderer are specific to that project; this plan still requires the ReXGlue/C++ runtime, STFS importer, and Vulkan proof.

- Build an explicit, read-only app staging tree from the current release build and reviewed native dependencies. Pin the packaging toolchain and external binary inputs by version and SHA-256 before copying them; derive package names and metadata from `installer/config/pins.toml`. Include icons, desktop/bundle metadata, required helper tools, and third-party license notices. Audit the staged tree **and** the finished package against the allow-list and retail-data, credential, symbol, and build-path exclusions.
- For Linux, feed the same staging tree to both `.deb` and `.rpm` recipes, as the sibling's nFPM recipe does. Install the app under a stable system path, a `Terminal=false` desktop entry under `/usr/share/applications`, and its icon under the hicolor theme. Declare a native dialog dependency if one is used; launch the pre-game importer as the normal user. Verify executable modes, native library names and loader resolution from the installed desktop entry. Package installation and removal must never import or delete the player's game data.
- For macOS, stage a complete `.app` with `Contents/MacOS`, `Contents/Resources`, `Info.plist`, icon, bundled dependencies, and a stable bundle ID. Build the unsigned `.pkg` on a Mac with `pkgbuild`, installing at `/Applications`. Use a component property list with bundle relocation disabled and upgrade replacement defined; the sibling found that Installer otherwise moved the app to an existing development copy. Check every promised Mach-O architecture with `lipo` and every dependency with `otool -L`, then expand the `.pkg` and compare its payload to the audited `.app`. Keep the app bundle and prior installed copies out of the writable import/saves path.
- Exercise first launch from an installed package in a fresh user account: select the supported game source, preview its matched version and required space, stage and verify it in user data, then launch. Confirm a later launch works after moving the source package away, and that cancel, repair, reinstall, and update preserve the last valid import and saves. For the unsigned Mac package, check Gatekeeper's first-open path separately for the `.pkg` and installed `.app`.

## Shared import, storage, and runtime contract

1. Probe the selected STFS package or extracted game folder against `config/game_fingerprints.toml` before changing any active install. Reject unsupported versions, missing files, unsafe paths, symlinks that escape the source, and archive resource exhaustion with a useful reason. Never require the original source path after successful import.
2. Show source type, matched version, expected space, and destination; stage as the current user on the destination filesystem, verify the prepared files, then atomically activate a manifest-backed game-data set. Cancel, process crash, disk-full, bad input, or extraction failure must leave the prior set playable. Separate `base game`, `Ultimate`, and DLC selections; importing only the base game must work. Ultimate is fetched from its upstream release or imported from the user's zip/folder under the existing pin and fingerprint rules, never bundled or mirrored.
3. Keep app binaries/resources read-only. On Linux place imported game data and user DLC under a stable XDG data root, preferences under XDG config, logs/state under XDG state, and disposable cache under XDG cache, with valid existing user overrides preserved. On macOS use an app-specific Application Support root and a separate Caches root. Saves, profiles, custom controller schemes, DLC and imported base game must survive an application update and an import repair. Define whether uninstall leaves user data in place and document the result; package removal must never silently delete it.
4. At boot, read the active manifest and start the game directly when it is valid. If it is absent/corrupt, open the importer/repair flow before the game starts. Provide a **guest-authored** Settings action to reimport or repair; the action exits to the same pre-game importer, then restarts a fresh game session after a committed change. Cancel/failure returns to the prior working set. Paths must be explicit, independent of process working directory, and Unicode-safe.
5. Keep renderer, audio, input, store, Messages, shell-open, image decode, keyboard, restart, crash diagnostics, and song-load recovery behind platform adapters. Preserve existing guest menus and behavior. Apply the version-specific settings contract below so each build shows only its supported settings and uses its own defaults. Never implement a new host-side overlay over the guest without the owner's separate authorization.

## Implementation prompts

Run these prompts **in order**, one at a time. Before each, read this contract, `AGENTS.md`, [build and run](../../guides/build-and-run.md), [distributable audit](../../guides/distributable.md), [toolchain](../../guides/toolchain.md), and [installer contract](../../../installer/README.md). Keep a dated evidence table in this plan or `docs/history/bringup-log.md`: machine/OS/architecture, commands, build IDs, package hashes, game-input fingerprints, UI observations, and gaps. Use only owner-supplied game data; keep it and any generated proprietary assets outside tracked files and release packages. Each code prompt ends with the existing Windows build and applicable non-UI tests, plus the target build/tests when a target environment is available. Before driving the game UI for acceptance, follow `AGENTS.md` and offer the owner the manual test unless they have authorized autonomous UI testing for that work. Complete each acceptance gate before proceeding, or revise the design and record why. The owner assigned native Ubuntu checks to the Linux prompts and native Mac checks to the Mac prompts on 2026-11-10; those checks are no longer a prerequisite for starting Prompt 1.

### Pre-step — Make settings version-aware

Before platform bring-up, define a settings contract that allows each released version and platform build to have its own visible rows, available choices, and defaults. The guest-authored Settings display must follow that contract; a row may appear, change its choices, or disappear between versions. Keep stable setting identifiers and migrate persisted values across upgrades: retain a user's supported choice, and use the current build's default when a value is absent or unsupported. Restoring defaults must use the current build's defaults rather than values from an older version.

For Linux and macOS, DirectX is unavailable: hide the Renderer setting entirely and default the renderer to Vulkan. This also applies when loading settings written by a Windows build; an old `d3d12` selection must not override Vulkan. Preserve the existing Windows renderer choices and default unless separately changed. Require a working Vulkan route on each advertised Linux/macOS target; if it cannot be proved, defer that target rather than exposing a nonfunctional renderer choice.

**Acceptance:** Settings visibility, choices, defaults, restore-defaults behavior, and migration are verified for each supported build. Linux and macOS show no Renderer row and boot with Vulkan even with a persisted DirectX selection; Windows retains its existing behavior.

**Implementation record, 2026-11-10:** `src/settings/schema.{h,cpp}` now resolves release/platform row rules without changing stable setting IDs or the persisted file format. `Service` uses those rules for descriptors, effective-value fallback, hidden-row edits, and Restore Defaults. The guest-authored menu omits rows marked hidden by the active release; Linux and macOS rules hide Renderer and force Vulkan even when `settings.toml` contains `d3d12`. The non-Windows `gpu_backend` cvar and boot fallback default to Vulkan. Host tests simulate both target platforms and a later release with a changed choice set/default, including reload and restore. The Windows release build and six focused non-UI CTest suites passed; see the final command record below. **Unverified:** Linux/macOS compilation, the actual menu display and renderer boot on those systems, and gameplay. Those require the target machines and remain gates for Prompts 2 and 4.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-10 | Windows x86-64, release preset | `cmake --build --preset win-amd64-release` | Passed; host toolchain checker reported clang 19.1.7 and CMake 4.4.0 instead of frozen 23.1.1 and 4.4.3, then continued under the non-strict preset. |
| 2026-11-10 | Windows host simulations | `ctest --test-dir out/build/win-amd64-release -R '^(settings_service\|settings_screen\|settings_bridge_service\|settings_graphics\|settings_store\|settings_migration)$' --output-on-failure` | Six suites passed. The Linux/macOS cases are policy simulations, not native builds or UI observations. |

### Prompt 0 — Define the platform route and target gates

On native Linux x86-64 and macOS arm64/x86-64 build environments, inventory SDK platform support, generated-source assumptions, clang flags, third-party libraries, executable/plugin paths, and the renderer actually available. Attempt minimal configure/codegen/host link using the existing presets and record every failure as source-level work, not as a passed target. Check Linux glibc and native-library ABI floors on proposed baseline distros; check Intel CPU instruction requirements and macOS deployment target. Choose package IDs, app/bundle ID, directory layout, minimum OS, native dependencies to bundle versus declare, and the first test machine matrix. Review the sibling installer recipes above, select and pin the Linux package tool and Mac `pkgbuild` inputs, and define a single staged-file manifest for each target before building packages. Add platform-specific toolchain pin records and make the checker report/validate the correct target rather than comparing Linux/macOS against Windows MSVC/SDK values.

**Revised acceptance, owner direction 2026-11-10:** A reproducible target matrix and blocker list exist for Linux and both Mac architectures. Each target has an identified build/test route, renderer expectation, package identity, stage contract and target-specific toolchain record. Native Linux configure/codegen/link, ABI and Vulkan checks move to Prompts 2–3 on a real Ubuntu machine; native Mac configure/codegen/link, deployment and Vulkan checks move to Prompts 4–5 on real Macs. Until those checks pass, no architecture or minimum OS is advertised as released.

**Prompt 0 planning and repository inputs complete, 2026-11-10; native validation scheduled later.** This Windows workstation has an Ubuntu 20.04 x86-64 WSL distribution, but it has glibc 2.31 and none of clang-20, CMake, Ninja or the required development packages. The installed Docker Desktop has no reachable daemon; no Mac builder is connected. These environments cannot run the proposed Ubuntu 24.04/Fedora 44 or Mac configure/codegen/host-link routes. Static source inspection and a Windows host build cannot establish ABI floor, loader behavior or playability for another OS. All three proposed release targets remain **deferred for release**, with their native gates assigned above. No minimum OS or native dependency set is advertised yet.

| Target and candidate floor | Build/test route | Renderer and CPU requirement | First native gate |
| --- | --- | --- | --- |
| Linux x86-64; [Ubuntu 24.04 LTS](https://documentation.ubuntu.com/release-notes/24.04/) `.deb` and [Fedora Workstation 44](https://www.fedoraproject.org/workstation/download/) `.rpm` as initial clean machines, minimum distro/`GLIBC_*` floor pending measurement | Native x86-64 GPU host, `cmake --preset linux-amd64-release`, codegen target, reconfigure, `cmake --build --preset linux-amd64-release`, CTest; then clean package installs on both families | Vulkan loader, physical driver and SDL3 Wayland/X11 path; preset `-march=x86-64-v2`; record actual GPU/driver/session and `readelf --version-info`/`ldd` on every shipped ELF | Configure/link, full song and both graphical package paths. A headless or WSL build alone is insufficient. |
| macOS arm64; deployment minimum 13.3, minimum OS pending installed test | Native Apple Silicon Mac with Xcode 26+ command-line tools (AppleClang 17+), `cmake --preset mac-arm64-release`, codegen, reconfigure, host link, CTest; then Finder/Installer test | SDK's Vulkan loader and MoltenVK ICD staged per architecture; Apple GPU support must be observed; `-march=armv8-a`; inspect `otool -L`, `lipo -info`, `otool -l` min OS | Native link, Vulkan boot, install and full song. |
| macOS x86-64; deployment minimum 13.3, minimum OS pending installed test | Native Intel Mac with Xcode 26+ command-line tools (AppleClang 17+), `cmake --preset mac-amd64-release`, codegen, reconfigure, host link, CTest; then Finder/Installer test | Same Vulkan/MoltenVK route with x86-64 slices; preset lowered from `x86-64-v3` to `x86-64-v2`, so the candidate CPU floor is SSE4.2/POPCNT rather than AVX2; inspect actual slices and host CPU | Native Intel GPU/Vulkan boot, install and full song. Apple Silicon/Rosetta alone does not accept Intel playability. |

The build-side inventory is concrete but is not a build result: the SDK declares Linux and macOS platform targets and Vulkan-only non-Windows builds. Linux needs `pkg-config` modules `x11-xcb` and `wayland-client`, SDL3 and the SDK's Vulkan stack. The SDK sets the Linux host and plugin RPATH to `$ORIGIN` and the macOS host RPATH to `@executable_path`; both require runtime libraries and GPU plugin **next to the executable** in the first stage. An earlier proposed `bin/` plus `lib/` layout, and a macOS `Contents/Frameworks/` location, would not satisfy those paths without a deliberate loader-path change. macOS stages `libvulkan.1.dylib`, `libMoltenVK.dylib` and `MoltenVK_icd.json` beneath `vulkan/` beside the host. The SDK pins Vulkan SDK 1.4.357.0 and source revisions in `rexglue-sdk/cmake/rexglue_vulkan_stack.cmake`. The generated `sources.cmake` is absent on a clean tree until codegen: the first build intentionally stops after generating it and must be reconfigured. The retry instruction now names the active OS/architecture preset. Shipped library aliases and any generated modules still need per-target loader audits.

Ubuntu 24.04's published glibc is 2.39 ([release notes](https://documentation.ubuntu.com/release-notes/24.04/)); a candidate `.deb` must use no newer `GLIBC_*` symbol, and its `GLIBCXX_*`/`CXXABI_*` requirements must also fit the clean baseline or be satisfied by approved bundled libraries. Fedora 44's floor needs a measurement on its clean image. These are ABI checks, not evidence that either package runs.

At Prompt 0 inspection, source blockers included unconditional Win32 startup and diagnostics headers, Windows-only store transport, WIC image decoding and browser opening, missing native writable paths/importer, and a Windows-only song-load monitor. Prompt 1 addresses the first group in source; native compilation and the pre-game UI remain unverified, and the song-load monitor still has no non-Windows build route. `CMakeLists.txt` selects the appropriate target pin (`config/toolchain-linux-amd64.toml`, `toolchain-mac-arm64.toml`, or `toolchain-mac-amd64.toml`) and the checker rejects a record whose `target` differs. These files contain bootstrap compiler/CMake/Ninja floors and the exact SDK commit, **not** accepted tool versions. Replace versions with measured native toolchain values and capture Xcode, macOS SDK, glibc, Vulkan loader/driver and third-party binary hashes before a strict release gate. Mac pins require AppleClang 17+ (Xcode 26+); if LLVM Clang is used, the SDK requires 20+ and the pin must change.

**Package contract chosen for later prompts.** Linux package name `rock-band-blitz-recomp`, application/desktop ID `io.github.gabrielofavero.rbblitz`; stage one immutable tree at `/opt/rock-band-blitz-recomp` and one desktop entry and icon under `/usr/share`. Both `.deb` and `.rpm` consume the `installer/config/platform-packaging.json` stage manifest through nFPM 2.48.0; the sibling recipe pins the x86-64 nFPM archive SHA-256 `6d7f9a28b2f397934403950fcbba83f598b08d02edbe79a0815b3b4897341750`. Verify that archive locally before use. The Linux stage contains `bin/rb_blitz`, `bin/librexruntime.so*`, `bin/librexgpu-xenos.so*`, any generated guest module and approved/hashed dependent `.so*` beside the host, plus `bin/graphics-preparation.json` and `bin/assets/messages/feed.json` (the latter is currently read relative to the executable), icon, desktop entry, third-party notices and license. File names and SONAME symlinks are audited from the first linked build; the package must fail closed on extras or missing entries. System glibc, graphics driver/loader, X11/Wayland, audio and any picker dependency are declared only after ELF dependency and clean-machine checks; do not blindly bundle them.

The macOS bundle/package ID is `io.github.gabrielofavero.rbblitz`; install `/Applications/Rock Band Blitz.app` with `pkgbuild` from one stage rooted at that bundle. The Mac stage manifest in the same JSON contains `Contents/Info.plist`, `Contents/MacOS/rb_blitz`, `Contents/MacOS/librexruntime.dylib`, `Contents/MacOS/librexgpu-xenos.dylib`, any generated guest module and approved dynamic libraries beside the host, the graphics catalogue and Messages feed beside the executable while their current paths require that, `Contents/Resources/` with icon and notices, plus the SDK-staged `Contents/MacOS/vulkan/lib/{libvulkan.1.dylib,libMoltenVK.dylib}` and `Contents/MacOS/vulkan/share/vulkan/icd.d/MoltenVK_icd.json`. `installer/macos/components.plist` sets `RootRelativeBundlePath`, `BundleIsRelocatable=false`, `BundleHasStrictIdentifier=true`, and `BundleOverwriteAction=upgrade`; `pkgbuild --root ... --install-location /Applications --component-plist ... --identifier ... --version` uses the common `installer/config/pins.toml` version. Pin the actual `pkgbuild`/Xcode and every dylib hash from the Mac builder before packaging. The current stage manifest marks native dependency inventory and Mac icon conversion as pending, so packaging must fail until they are filled. No installer script imports game data or touches saves.

`installer/config/platform-packaging.json` is the one manifest for each target. The Linux desktop entry, Mac `Info.plist.in`, and Mac component properties are checked against its IDs/paths by `python installer/tools/check_platform_packaging.py`. The same checker rejects a stage audit while native library inventory, Mac Xcode/pkgbuild pin or icon conversion is pending; once those are resolved, it compares the exact stage file set with the manifest and rejects extras. This deliberately prevents a package script from treating today's partial inventory as a complete release tree.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-10 | Windows host, Linux/macOS routes | Escalated `wsl --list --quiet`; Ubuntu WSL `uname -m`, `/etc/os-release`, `getconf GNU_LIBC_VERSION`, tool availability; `wsl -d Ubuntu --exec /bin/bash --noprofile --norc -c 'cd /mnt/d/.../rb-blitz-xenon-recomp && cmake --preset linux-amd64-release'`; `docker info`; source inspection of presets, SDK CMake helpers, host sources and sibling package recipes | Ubuntu 20.04 x86-64, glibc 2.31, missing clang/CMake/Ninja/dev packages; configure stops at `cmake: command not found`. Docker Desktop reported it could not start; its processes were stopped after the attempt. No target codegen/link or package ABI measurements. All three release targets deferred. |
| 2026-11-10 | Windows x86-64, Prompt 0 code | `cmake --build --preset win-amd64-release`; `ctest --test-dir out/build/win-amd64-release -R '^toolchain$' --output-on-failure`; checker invoked with Linux pin and mismatched `--target mac-arm64`, then with Mac arm64 pin against Windows LLVM Clang; correct Linux pin inspected | Release build and focused toolchain suite passed; wrong target rejected with exit 2 and wrong compiler family with exit 1. The Linux record report contains no MSVC or Windows SDK comparison. This validates selection logic on Windows, not target compilation. |
| 2026-11-10 | Documentation | `python .github/skills/documentation/scripts/check_docs.py`; `git diff --check` | 0 errors, 193 pre-existing warnings; diff whitespace check passed. |
| 2026-11-10 | Windows host, package inputs | `python installer/tools/check_platform_packaging.py`; `python installer/tools/check_platform_packaging.py --target linux --stage-root installer/out`; same for `macos` | Metadata and source paths validated; both stage audits correctly refused with unresolved native inputs. No package built. |
| 2026-11-10 | Windows host, Prompt 0 follow-up | `cmake --build --preset win-amd64-release`; `python installer/tools/check_platform_packaging.py`; `python .github/skills/documentation/scripts/check_docs.py`; `git diff --check` | Windows release build and packaging metadata validation passed; docs checker 0 errors/193 existing warnings, diff whitespace check passed. Native build and package gates remain open. |
| 2026-11-10 | Native Linux x86-64, pending | `cmake --preset linux-amd64-release`; `cmake --build --preset linux-amd64-release --target rb_blitz_codegen`; reconfigure; full build; `ctest`; `readelf -d/-V`, `ldd`, Vulkan/driver and package install/play test | Required gate; record failures as source work, not successful targets. |
| 2026-11-10 | Native macOS arm64 and x86-64, pending separately | Corresponding `mac-*-release` configure/codegen/reconfigure/build and CTest; `lipo -info`, `otool -L/-l`, `pkgbuild --expand`, Finder/Installer and Vulkan play test | Required separately per architecture; Mac deployment floor and `pkgbuild` pin remain unmeasured. |

### Prompt 1 — Extract portable runtime and preparation boundaries

Separate Win32 startup, process/restart, diagnostics, store transport, image decoding, shell opening, path discovery, and native library loading from platform-neutral game logic. Extract the existing STFS/folder probe, fingerprint validation, extraction, and manifest rules from the Windows helper into shared code callable without Inno Setup. Implement the pre-game importer transaction and minimal native picker/progress contract. Preserve the Windows installer and all existing data paths. Add failure-injection tests for validation, staging/commit/recovery, Unicode paths, and update migration; use real supported fingerprints without committing game files.

**Acceptance:** Shared code imports and verifies a supported game source into an explicit destination on each host in a non-UI test; unsupported or interrupted imports cannot replace a valid set. Windows setup, installed launch, saves, and optional Ultimate still work. A new-platform player UI is **not** claimed until Prompt 2.

**Prompt 1 shared implementation delivered on the available Windows host, 2026-11-10; native and player gates deferred.** `installer/src/game_import_core.{h,cpp}` extracts folder/STFS probing, supported-fingerprint verification, staging, sibling backup activation and recovery from the Windows helper. The Windows helper delegates its game import commands to the same core. `installer/portable/CMakeLists.txt` builds and tests that core without Inno Setup, WinHTTP or the game SDK. A caller supplies a source picker, progress and cancellation callbacks; the actual Linux/macOS picker and pre-game launch flow belong to Prompts 2 and 4. Synthetic folder/STFS fixtures and the checked-in production fingerprint test reject bad data without tracking retail files; phase faults, Unicode paths, interrupted backup recovery and reimport preserving optional mod/save files are covered. The platform code now guards startup, restart and diagnostics, resolves native symbols, adds libcurl store transport, stb image decode and browser launch, and places native path configuration and default user data in writable per-user roots. Windows uses its existing WinHTTP, WIC, ShellExecute, installer paths and helper API.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-10 | Windows x86-64, game | `cmake --build --preset win-amd64-release`; `ctest --test-dir out/build/win-amd64-release -R '^(game_restart\|crash_diagnostics\|settings_screen\|settings_service\|settings_migration\|store_download\|store_client\|store_search\|button_scheme\|messages)$' --output-on-failure` | Release executable linked; all nine matching non-UI suites passed. No game navigation or installed launch was performed. |
| 2026-11-10 | Windows x86-64, portable import | `cmake -S installer/portable -B out/build/portable-import -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++`; `cmake --build out/build/portable-import`; `ctest --test-dir out/build/portable-import -R '^portable_import$' --output-on-failure` | Core and test built independently; the one suite passed. Real supported hashes were loaded for a rejection case; successful imports used synthetic bytes. |
| 2026-11-10 | Windows x86-64, installer | From `installer/`: `cmake --build --preset installer-release`; `ctest --preset installer-release -R '^installer$' --output-on-failure` with `TEMP`/`TMP` in `installer/out/test-temp` | Setup helper built and staged; installer suite passed (430 checks). The workspace-local temporary folder avoids this sandbox's denial for its default temporary directory. Inno Setup wizard and installed behavior were not driven. |
| 2026-11-10 | Windows x86-64, setup audit | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File installer/build.ps1 -SkipArt -SkipTests`; `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/setup/audit_distributable.ps1` | Inno Setup compiled `RockBandBlitzSetup-0.1.0.exe` (SHA-256 `897f3475288e70bbb3702a3f0687c34740e2f2eba629dcbb89f871471398879e`); all eight distributable checks passed on that final package. No installed launch or gameplay was observed. |
| 2026-11-10 | Native Linux and both Mac architectures, pending | Build `installer/portable` and run `portable_import`; configure/codegen/link the host and native tests on target machines in Prompts 2 and 4 | No native compilation, TLS or graphics validation yet; Mac system libcurl 7.62+ and native library resolution must be checked. |

### Prompt 2 — Make Linux x86-64 playable and package `.deb`/`.rpm`

Complete Linux adapters, native Vulkan/plugin/audio/input support, TLS/store/Messages, image decoding, browser opening, file/folder selection, restart and load recovery. Resolve all read-only program-tree writes, set the XDG storage layout, and integrate the pre-game import/repair UI. Stage one audited install tree for the ELF host, needed shared objects, approved assets, dependency notices, and desktop icon/entry; generate both `.deb` and `.rpm` from it. Start with the sibling's pinned nFPM/staging approach, or use [CMake's DEB and RPM generators](https://cmake.org/cmake/help/latest/cpack_gen/deb.html) if they preserve the same manifest and audit contract. Pin system package dependencies or bundle libraries with a compatible license; check executable permissions, library aliases where needed, installed loader paths, and a graphical package-manager launch. Package scripts must not touch game data or user saves.

**Acceptance:** On clean supported distro installations, both graphical package paths install and launch the importer from the desktop entry without a terminal or SDK. A complete base import reaches the game and a second launch skips import. A package inventory contains no retail content or owner-specific paths.

**Windows-host structure pass, 2026-11-10; Prompt 2 remains in progress.** The Linux-only `rb_blitz_linux_launcher` target now links the shared SDK-free importer and is the desktop entry command. Its Zenity flow selects a package or folder, previews the matched source and destination, imports to XDG data, and starts the game only after verification; `--repair` is a direct reimport entry point. Import progress can cancel during a file copy, with a focused portable-core regression check. The app's native writable defaults now use separate XDG data, cache, and update locations, and safe graphics mode keeps Vulkan on non-Windows builds. `installer/linux/package.py` stages only the declared files, rejects non-ELF targets and forbidden resource types, and prepares both formats from one version and stage; it refuses package generation while the native dependency inventory and full release audit flags are pending. The exact next work is a native Linux configure/codegen/link and dependency inventory, validated controller asset staging, guest Settings repair route, full package audit, and clean graphical install/import/boot checks. No `.deb` or `.rpm` has been built or accepted.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-10 | Windows x86-64, source preparation | `python installer/tools/check_platform_packaging.py`; `python -m py_compile installer/linux/package.py installer/tools/check_platform_packaging.py` | Metadata and Python syntax passed; Linux stage audit remains blocked until native dependencies and assets are inventoried. |
| 2026-11-10 | Windows x86-64, portable importer | `cmake --build out/build/portable-import`; `ctest --test-dir out/build/portable-import -R '^portable_import$' --output-on-failure` | Core built and one suite passed, including active-copy cancellation; no native picker/UI was run. |
| 2026-11-10 | Windows x86-64, game | `cmake --build --preset win-amd64-release`; `ctest --test-dir out/build/win-amd64-release -R '^(settings_service\|settings_screen\|settings_migration\|game_restart\|crash_diagnostics)$' --output-on-failure` | Release build linked and five non-UI suites passed. The non-strict toolchain check noted clang 19.1.7 and CMake 4.4.0 against frozen 23.1.1 and 4.4.3. |
| 2026-11-10 | Ubuntu 20.04 WSL, compile-only probe | `g++ -std=c++2a -fsyntax-only ... installer/linux/importer_main.cpp` | Cannot parse the shared core's `consteval` with GCC 9.4.0; this environment has no clang/CMake/Ninja and is not the native acceptance machine. No Linux build or UI result is claimed. |

### Prompt 3 — Validate Linux release behavior

On native Linux hardware, test clean install, update, reinstall, removal, import/reimport/repair, bad input, canceled and failed staging, game boot, song completion/results, Ultimate on/off, DLC enumeration/load, RhythmVerse store, Messages browser action, settings, keyboard/mouse/controller hot plug, resolution/fullscreen, audio, saves, crash/load recovery, and missing/offline network cases. Test both package families and at least the declared minimum distro versions; record renderer/driver and Wayland/X11 session. Confirm updates preserve imported content, profile, DLC and saves. Run the ELF/package audit and publish SHA-256 values in release notes only after failures are fixed.

**Acceptance:** A player can install either Linux package, import once, play a full song, recover or reimport, and update without developer tools or loss of user data. All advertised features are observed or clearly scoped out in player documentation.

### Prompt 4 — Build the macOS `.app` and `.pkg`

Port the shared runtime/importer adapters to macOS: prove the Vulkan path supported by the SDK and hardware (including any required translation layer), native libraries, file/folder picker, paths, keyboard/mouse/controller, audio, TLS/store/Messages, image decode, Finder/browser opening, restart and load recovery. Stage a proper `.app` with `Info.plist`, stable bundle ID, icon, resources, executable and matched native libraries; keep writable data outside the bundle. On a Mac, build an unsigned `.pkg` with `pkgbuild`, a fixed `/Applications` destination, and a component property list that disables bundle relocation and defines upgrade replacement. Audit the app and expanded package payload, verify Mach-O slices with `lipo` and load paths with `otool -L`, and reject dependencies on developer-machine locations. Do not promise a universal package until the app and every bundled dependency contain tested arm64 and x86-64 slices. Record the exact first-open and **Open Anyway** flow for both installer and app on the supported macOS versions; Apple documents this as a per-app security exception ([Apple support](https://support.apple.com/en-us/102445)).

**Acceptance:** Each promised Mac architecture installs its package, opens the pre-game importer through Finder after the documented security flow, imports base data, and reaches a playable game. The package is self-contained except for the player's game source and OS-provided services; audit finds no proprietary files or build-machine paths.

**Mac build structure prepared on Windows, 2026-11-10; Prompt 4 remains pending.** `installer/macos/build.sh` adapts the sibling project's native preflight/build/stage sequence to the ReXGlue CMake presets. The shared portable importer is now a macOS CMake target with a Cocoa pre-game picker, preview, progress/cancel, recover/reimport and Finder-launched handoff to `rb_blitz`. `Info.plist.in` names that launcher. `installer/macos/package.py` renders the pinned version, stages the explicit app manifest including the SDK's Vulkan/MoltenVK files, makes an `.icns`, inspects Mach-O slices and load paths, and defines `pkgbuild` plus expanded-payload comparison. The package command is gated by pending native dependency, Xcode/CLT, icon and full audit checks; controller assets, third-party notices and any additional native libraries need a reviewed final manifest. This is source structure only: no Mac compilation, `.app`, `.pkg`, Finder launch, Vulkan rendering or game UI has been observed. The target minimum OS and both architecture promises remain provisional until real Mac tests.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-10 | Windows x86-64, Mac source preparation | `python installer/tools/check_platform_packaging.py`; `python -m py_compile installer/macos/package.py`; `bash -n installer/macos/build.sh` through WSL | Metadata, Python and shell syntax passed; the macOS CMake/Objective-C++ build and stage/package commands require a Mac. |
| 2026-11-10 | Windows x86-64, shared regression | `cmake --build --preset win-amd64-release`; `ctest --test-dir out/build/portable-import -R '^portable_import$' --output-on-failure`; `ctest --test-dir out/build/win-amd64-release -R '^(settings_service\|settings_screen\|settings_migration\|game_restart\|crash_diagnostics)$' --output-on-failure` | Windows release linked; portable importer and five focused non-UI suites passed. No Mac native UI or package test was run. |

**Native Mac bring-up, 2026-11-15; Prompt 4 is in progress.** On Apple M1,
macOS 27.2.1, the internal `mac-arm64-release` game now links with Xcode 26.0.1
(17A400), AppleClang 17.0.0/SDK 26.0, CMake 3.31.6, Ninja 1.13.2, and Python
3.12.16. The owner-supplied Downloads STFS package was extracted to ignored
`game/` and all three supported fingerprints passed. The SDK was initialized
at the existing pin and the tracked patches replayed without changing its gitlink.

Native compilation exposed Windows memory APIs in graphics probes/HUD layout,
a missing platform transfer declaration, unavailable libc++ `atomic<shared_ptr>`
specializations, Windows-only guards around shared menu/music/progress logic,
and the SDK ICD staging path defect. The fixes use native memory queries/protection,
shared-pointer atomic operations and scoped platform guards; SDK patch 0064 fixes
the ICD lookup. The first boot created an M1 Vulkan device/window but crashed from
recursive `RTLD_DEFAULT` import lookup. A second reached guest graphics but exposed
crash diagnostics replacing the SDK's recoverable SIGBUS handler. Import lookup
now selects the SDK module and diagnostics preserve existing signal handlers.
The owner requested pausing installer work to test this internal game build.
The first successful relaunch reached the pre-A title phase
(`out/macos/internal-game.log`). The subsequent graphics investigation below
was authorized by the owner; installer work remains paused.

**Mac texture artifacts, 2026-11-15 [measured].** Screen captures reproduced
dotted triangle diagonals, sprite bounding boxes and text outlines on M1 with
MoltenVK 1.4.1. Base-mip-only sampling removed them, identifying the derivative/LOD
path. A synthetic native Metal reproduction isolated non-inlined `optnone`
precision helpers before derivatives. SDK patch 0065 replaces affected fragment
arithmetic with NoContraction Fma equivalents on MoltenVK, retaining mipmapping
and the existing MSAA mode; shader storage version 13 invalidates old translations.
The production-builder headless Vulkan regression reports zero bad gradient
pixels at 1×, 2× and emulated-2×-via-4× sampling, including a separate-rounding
check with runtime inputs. Disabling the workaround reproduces 1,124 bad pixels
at 2× (`out/macos/gradient-baseline.log`). The rebuilt title screen and autosave
notice are visibly clean (`out/macos/texture-fma-title.png`,
`out/macos/texture-fma.png`); this is startup rendering evidence, not gameplay
acceptance. Keyboard automation is still blocked by macOS accessibility access.
Full-song graphics, audio and input acceptance remain unverified.

**Mac Auto and visibility, 2026-11-15 [measured].** The owner reported black
after switching away/back and top/bottom bars with Auto. Auto was saved, but
the Windows-only display probe returned 0×0 on Mac, selecting 1280×720. Native
SDL/Cocoa probes now measure backing pixels, usable area and frame decorations;
Retina window sizes convert to points even when no shrinking is needed. An
initial native 2880×1800 attempt exposed a 4.5 GiB scaled resolve allocation
exceeding this M1's 4 GiB Metal buffer limit. Native capabilities now exclude
unallocatable scales; Auto prefers the highest supported size with the exact
display aspect. M1 selects 1152×720 at 2×, yielding 2304×1440 on the 2880×1800
screen. `out/macos/display-final-game.log` records the plan, and
`out/macos/display-auto-title.png` shows a clean title filling the screen.
The development failure's recovery journal was backed up under `out/macos/`
and cleared; the owner's saved Auto token was retained.

The pre-fix sample (`out/macos/focus-black-sample-full.txt`) has the UI thread
in Metal drawable acquisition and the GPU thread waiting for its queue. SDK
patch 0066 skips queued/expose paints while hidden/occluded and retains the
live Metal swapchain on same-size focus gains. A blank capture during the
post-A guest transition (`out/macos/display-after-return.png`) is separate
from the owner's task-switch result. Automated external app
activation returned success without changing the foreground application, so
the owner was asked to verify an actual switch away/back after the rebuild.
The owner confirms the picture returns normally after switching away/back
on the rebuilt game (`out/macos/focus-preserve-normal-game.log`).
An optional fast-start diagnostic hit
an unrelated guest null read; it was stopped and the normal startup restored.
The `settings_graphics`, `window_policy`, `settings_screen` and
`vulkan_fragment_gradient` suites pass (`out/macos/display-final-tests.log`).

**Mac startup cache, 2026-11-15 [measured].** The native content-ready/base-panel
hooks were compiled only on Windows, leaving Mac waiting indefinitely. Enabling
the shared hooks exposes the real preparation result. SDK patch 0067 then fixes
macOS append-stream header reads and the Clang packed Vulkan recipe layout;
compile-time assertions retain the canonical Windows format. It also preserves
valid unsupported recipes instead of truncating the un-compacted stream. The
rebuilt native game imports 409 supported recipes, skips 25 unsupported geometry
recipes, and successfully prepares 409/409 with zero failures or missing entries.
The continuation releases once after 59,153 ms, and the owner confirms preparation
completes (`out/macos/cache-portability-game.log`). Preparing-state Skip now cancels
optional work without joining the extraction worker on the guest thread and uses
the saved main-menu continuation rather than native Back. The authored menu test
proves that route; live Skip navigation remains unverified. The `menu_payload`,
`preparation_assets`, and `preparation_state` suites pass
(`out/macos/cache-gate-tests.log`); all 66 SDK patches replay without drift
(`out/macos/cache-portability-patch-check.log`). Installer work remains paused.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-15 | Mac arm64, importer | Build `installer/portable`, then CTest `portable_import` | Shared importer and Cocoa launcher linked; synthetic importer suite passed. The real package also extracted and matched the production fingerprints without UI. |
| 2026-11-15 | Mac arm64, internal game | `cmake --build --preset mac-arm64-release --target rb_blitz`; logs under `out/macos/` | Game, native runtime, Vulkan/MoltenVK and controller validator linked. All 1,182 declared controller files validated and staged for internal play. No gameplay acceptance claimed. |
| 2026-11-15 | Mac arm64, focused regressions | CTest `guest_memory`, `runtime_symbol`, `crash_diagnostics_posix`, `settings_screen` | Four suites passed: unaligned protected writes restore permissions; SDK lookup bypasses the host override; recoverable signal handlers survive diagnostics installation; settings authoring remains valid. |
| 2026-11-15 | M1, MoltenVK fragment gradients | `vulkan_fragment_gradient`; `out/macos/gradient-test.log`; `out/macos/texture-final-build.log` | Production arithmetic passes the headless GPU regression at 1×/2×/4× with separate rounding preserved; baseline fails at 2×. Native game rebuilt; startup text and title textures observed without the reported artifacts. Gameplay acceptance is pending. |
| 2026-11-15 | Mac arm64, packaging implementation | `python3 -m unittest discover -s installer/tests -p '*packaging_tests.py' -v`; `out/macos/packaging-tests.log` | 13 non-UI checks passed using tiny non-game Mach-O fixtures and real `pkgbuild` expansion. Authored icon converted. Actual game app/package staging and final inventory remain pending; no release package has been produced. |

The package implementation now includes controller validation and dependency
notices, rewrites actual dylib IDs/rpaths, signs final bytes ad hoc, checks every
Mach-O slice/minimum OS/signature and real rpath resolution, scans large binaries,
verifies the embedded portable graphics catalogue, and compares the entire expanded
package payload and modes. A development-package route retains the release gate.
Architecture-specific native audit records are still required before it can run.
Windows regression builds are unavailable on this Mac; Intel Mac, installed Finder
launch, unsigned security flow, Ultimate picker/download, guest repair route,
update preservation, graphics and gameplay acceptance remain open.

**Apple Silicon installer and direct builder, 2026-11-16 [measured].** The owner
resumed packaging and requested the sibling Zombies project's separate direct
build/run path. `sh scripts/macos/build.sh` now verifies private `game/` input,
builds the native Release game, and stages all 1,182 validated controller files.
`--source` accepts a supported package or extracted folder through the SDK-free
`rb_blitz_import` CLI; the atomic transaction preserves existing extra files.
`sh scripts/macos/run.sh` and `out/macos/Run Rock Band Blitz.command` launch this
local build without installing or entering the app's importer. The builder itself
does not start the game. The owner's newly downloaded 512px Blitz icon is preserved
as the Apple source; the Swift generator adds only the rounded border/padding for
macOS and makes a separate opaque, unmasked square for future iOS use.

The actual bundle audit found two gaps absent from the earlier fixtures:
`install_name_tool` rejects removing and adding the same rpath in one invocation,
and signing rejects controller PNGs in `Contents/MacOS`. The packager now retains
canonical existing rpaths. Authored resources and the SDK-supported Vulkan tree
live under `Contents/Resources`; the launcher and game resolve bundle resources
there, while direct builds retain executable-adjacent resources. The native test
fixtures include an existing `@loader_path` and the corrected resource layout.

`sh installer/macos/build.sh --development` built
`out/macos/packages/RockBandBlitz-0.1.0-mac-arm64-development.pkg` (65 MiB displayed).
The app and expanded package contain 1,229 files and seven arm64 Mach-O images.
Signature, CPU, declared macOS 13.0 minimum, actual loader resolution, content/path
scans, embedded preparation catalogue, expanded file hashes and modes all passed.
The only external dependencies are Apple system libraries/frameworks, including
`/usr/lib/libcurl.4.dylib`. Measured Apple Silicon dependency and Xcode/SDK/compiler
inputs are recorded in `platform-packaging.json`; other architectures retain
their pending gates. The package is unsigned and the app is signed ad hoc.
Release acceptance remains closed. The owner chose to perform installation and
first-launch testing themselves; no installed app/UI test was run in this pass.

| Date | Machine/target | Command or evidence | Result and limit |
| --- | --- | --- | --- |
| 2026-11-16 | M1, native Release | `sh scripts/macos/build.sh`; `out/macos/direct-build-final.log` | Game/importer/controller validator linked; 1,182 controller files staged; local Finder command created. No game launch. |
| 2026-11-16 | M1, initial base-only app/package | `sh installer/macos/build.sh --development`; `out/macos/installer-package.log`; archived artifacts under `out/macos/packages/previous-base-only/` | Real `.pkg` built and expanded payload matched all 1,229 signed-app files and modes. SHA-256 `df5b852d0942f13d53727f4d4571ef72297ba92e61e5405db630553a8116c151`. Superseded below by the Ultimate-capable build. No installation. |
| 2026-11-16 | Mac, packaging fixtures | `python3 -m unittest discover -s installer/tests -p '*packaging_tests.py' -v`; `out/macos/installer-tests.log` | 14 tests passed, including native package expansion, relative-rpath retention, and toolchain pin rejection. |
| 2026-11-16 | Mac, import core/CLI | CTest in `out/build/macos-import`; `out/macos/importer-tests.log` | `portable_import` and `import_cli` passed. CLI cases cover spaces in paths, verification, corrupt source rollback, and local DLC preservation. |

Apple's `iconutil` reported `Invalid Iconset` inside the agent sandbox for a valid
PNG set; the same conversion and full native packaging succeeded outside that
sandbox. The normal build script requires no system installation or launch to
produce its artifacts. Windows regression builds, Intel Mac, clean macOS 13,
installed Finder launch, unsigned security flow, update preservation and the
remaining Prompt 4/5 player flows remain unverified.

**Optional Ultimate added, 2026-11-16 [measured].** The owner found the missing
Ultimate setup choice while testing the initial package. The Cocoa launcher now
offers the pinned official download, a local ZIP/extracted folder, or Play without
Ultimate after base import. Skip/success is remembered outside the app; failed or
canceled first setup remains retryable, and `--ultimate` explicitly reopens the
choice. The game's Game Version setting selects the installed mod.

The native core reuses the Windows helper's config embedder/parser, release pins,
ZIP/inflate reader, path validation, SHA-256 and fingerprint code. The download
uses Apple's system libcurl with HTTPS-only redirects, cancellation and strict
size/hash checks. Local revisions retain Windows's mismatch-warning policy.
The existing portable import transaction commits the overlay and restores old
files on cancellation/failure. The mod's `default.xex` is excluded, and neither
the mod archive nor its extracted data enters the package manifest. The direct
builder accepts `--ultimate` and `--ultimate-source ZIP_OR_FOLDER` too.

The real pinned 2.11 download and install passed in
`out/macos/ultimate-download-check/` against an isolated synthetic base tree;
both installed payload fingerprints matched. No game was launched. Four native
CTest suites pass (`ultimate_cancel`, `portable_import`, `import_cli`,
`ultimate_cli`), covering ZIP/folder import, traversal rejection, corrupt archives,
base preservation, XEX exclusion, cancellation before download and rollback after
moving the previous overlay (`out/macos/ultimate-tests.log`). The new Release
build/package passed the complete app/expanded-payload audit again
(`out/macos/ultimate-game-build.log`, `out/macos/ultimate-package.log`). The latest
package SHA-256 is
`4658702aeaf32e0ce2429a210a730f036243934d9aa2442ea8c419ae3176a5d5`.
It replaces the base-only package at the usual output path; the old artifact is
archived. The owner retains manual installation, picker and Ultimate game checks.

### Prompt 5 — Validate macOS play and publish the multi-platform release

On real Macs for every advertised architecture, test fresh install, update, uninstall behavior, Finder/Dock launch, security override, import and repair failures, a full song/results, Ultimate and DLC, store/Messages, settings, controller and keyboard/mouse input, audio, saves, display/retina/fullscreen, sleep/resume, restart and crash recovery. Rerun the Windows acceptance gate after shared changes. Extend `.github/workflows/builds.yml` with isolated Linux and macOS self-hosted builders (or an equivalent reviewed release process) that can access authorized build inputs without uploading them. Validate tag/version and SHA-256 per artifact; publish only artifacts whose platform gates passed. Update README, install instructions, supported OS/architecture matrix, known issues and uninstall/data-location guidance.

**Acceptance:** A tagged release offers the Windows setup, tested Linux packages and tested Mac package(s), each named from the same version and individually audited. A player on every advertised target can install, import their own game data once, play and update; the release notes disclose the unsigned Mac security step and any tested platform limits.

## Decisions and limits to revisit at the gates

- **Linux arm64 and Steam Deck:** presets exist, but this plan only commits to Linux x86-64. Add arm64 or Deck-specific claims after native build, package and input/graphics evidence; Proton is not a native Linux release.
- **macOS renderer:** prove Vulkan on real target hardware, using a translation layer if required; a macOS preset alone does not establish a working renderer. Defer an architecture if Vulkan cannot run there.
- **macOS signing:** an unsigned `.pkg` is the initial homebrew target because it matches the attached plan. If current Gatekeeper behavior makes that route too fragile, decide whether to sign/notarize or change the download format, and document the account/certificate cost before publication.
- **Data locations and uninstallation:** retain user data by default, like the Windows uninstaller's default, and clearly document the exact path and optional removal procedure after testing each package manager and macOS Installer behavior.
- **Release assets:** the existing one-file rule describes a Windows product; multiple native package formats require multiple assets on one release. Do not publish placeholder packages or imply they passed because Windows did.
