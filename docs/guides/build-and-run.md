# Build and run

Build the game from source, boot a local build, or create a platform package.
Run commands from the repository root unless a step explicitly changes directory.
Keep build and launch separate: the build scripts do not start the game.

For an already prepared checkout, go to [incremental development](#3-incremental-development).
The [development command reference](../../scripts/dev/README.md) explains the
root `dev.sh` and `dev.ps1` helpers, launch flags, and installer shortcuts.

Platform instructions: [Windows](#windows), [Linux](#linux),
[macOS](#macos), [iPhone/iPad](#iphone-and-ipad).

## Platforms and current limits

These are the routes implemented in this repository. A preset or package builder
does not establish release acceptance on that platform. The current SDK is
ReXGlue v0.11.0; older native build and gameplay measurements do not validate the
migrated stack. See the [toolchain record](toolchain.md#platform-validation-scope)
and [migration record](../plans/in-progress/2026-11-17-rexglue-v0.11-migration-plan.md).

| Target | Release preset | Build host and graphics | Current validation scope |
| --- | --- | --- | --- |
| Windows x86-64 | `win-amd64-release` | Windows x86-64; D3D12 and Vulkan | Current SDK build, title link, and CTest evidence. Windows setup builder available. |
| Windows ARM64 | `win-arm64-release` | Windows ARM64 with matching compiler/toolset libraries; D3D12 and Vulkan | Preset and dev helpers exist; native and cross-link acceptance remain pending. The recorded Windows builder lacks MSVC ARM64 libraries. |
| Linux x86-64 / ARM64 | `linux-amd64-release` / `linux-arm64-release` | Matching native Linux host; Vulkan | Native build/boot acceptance remains pending. The x86-64 package route has open dependency and release audits. |
| macOS Apple Silicon / Intel | `mac-arm64-release` / `mac-amd64-release` | Matching native Mac; Vulkan through MoltenVK | Direct build and development `.app`/`.pkg` routes exist. Earlier Apple Silicon evidence predates v0.11; current native migration acceptance and Intel acceptance remain pending. |
| iPhone / iPad ARM64 | `ios-arm64-release` / `ios-arm64-importer-release` | Mac with full Xcode; physical iOS device; Vulkan through MoltenVK | Bundled-assets and first-launch importer IPAs exist. Earlier device output/input evidence predates v0.11; current migration and full gameplay acceptance remain pending. No simulator route. |

Android and Meta Quest have no build presets, packaging scripts, or boot route
yet. Desktop presets also offer `-debug` and `-relwithdebinfo`; the dev helpers
select Release. The x86-64 presets target `x86-64-v2`; ARM64 presets target
`armv8-a`. Build each desktop architecture natively for the documented routes.

## 0. Prepare the checkout and game input

### SDK checkout

Clone the repository, enter it, and initialize its pinned dependencies:

```sh
git submodule update --init --recursive
```

On Windows, repair flattened symlinks if the checkout used `core.symlinks=false`,
then apply the patch stack through the PowerShell wrapper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup/repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup/apply_sdk_patches.ps1
```

On Linux or macOS:

```sh
python3 scripts/setup/apply_sdk_patches.py
```

The applier selects the stack for the parent's staged SDK gitlink using
[`patches/sdk-stacks.json`](../../patches/sdk-stacks.json). Keep the pinned
revision; do not manually mix the baseline and v0.11 patches. Patched SDK files
are expected local changes. Audit without writing with the PowerShell wrapper's
`-Check` or Python's `--check`. See [patches/README.md](../../patches/README.md)
for replay rules and patch lineage.

### Private game input

Every game build needs the supported Xbox 360 game locally for code generation,
including builds of installers and asset-free importer IPAs. The required layout is:

```text
game/
  default.xex
  gen/
    main_xbox.hdr
    main_xbox_0.ark
```

[`config/game_fingerprints.toml`](../../config/game_fingerprints.toml) records the
supported base revision, sizes, and hashes. Keep the unmodified retail
`default.xex` for code generation. Ultimate belongs in `game/ultimate/`; its
replacement executable is not the build input. See [Ultimate compatibility](ultimate-compat.md).

The Mac and iOS builders below accept `--source` and import a supported STFS
package or extracted folder into `game/`. For Windows and Linux, the SDK-free
command-line importer can prepare the same input before configuring the game:

```sh
cmake -S installer/portable -B out/build/portable-import -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/portable-import --target rb_blitz_import
```

On Windows, use the native Clang/MSVC environment from the prerequisites below,
then run:

```powershell
& ./out/build/portable-import/rb_blitz_import.exe --fingerprints config/game_fingerprints.toml --destination game --source 'D:/Games/Your game package'
# Verify an existing import instead:
& ./out/build/portable-import/rb_blitz_import.exe --fingerprints config/game_fingerprints.toml --destination game --verify
```

On Linux:

```sh
out/build/portable-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
  --destination game --source '/path/to/game-package-or-folder'
out/build/portable-import/rb_blitz_import --fingerprints config/game_fingerprints.toml \
  --destination game --verify
```

Import verifies the three required files and commits them transactionally,
preserving a previous valid import on failure. No retail download is provided.
Keep `game/` private. Desktop packages and the iOS importer exclude it; the
bundled iOS route embeds it in a private IPA.

### Vulkan sources

The default builds include Vulkan. Its Headers, Loader, VMA, and Apple MoltenVK
sources must match the SDK manifest and canonical nested patch. CMake can acquire
the required sources during configure by default. For an explicit, auditable
setup, prepare them first and pass an absolute source root when configuring:

```powershell
# Windows: used by the configure command in section 2.
python scripts/setup/prepare_vulkan_stack.py --source-root out/deps/rex-vulkan --only vulkan-headers vulkan-loader vulkan-memory-allocator
$vulkanSources = (Resolve-Path out/deps/rex-vulkan).Path
```

```sh
# Linux: used by the configure command in section 2.
python3 scripts/setup/prepare_vulkan_stack.py --source-root out/deps/rex-vulkan \
  --only vulkan-headers vulkan-loader vulkan-memory-allocator
```

The Mac/iOS builder scripts configure their own build trees and let CMake prepare
the required Vulkan sources there. Apple builds additionally need MoltenVK;
desktop Apple builds use a loader/ICD, while iOS links MoltenVK directly.

For local mirrors, the preparation tool accepts `--mirror-root DIR` or repeated
`--mirror NAME=PATH`. Audit an already prepared root without network access with
the same `--only` list plus `--check --offline`. Setting
`REXGLUE_FETCH_VULKAN_DEPS=OFF` makes configure check existing Vulkan sources
instead of acquiring them. Other SDK downloads and MoltenVK build dependencies
still need network access or populated caches. Preserve source trees needed for
packaging license notices. Exact pins and backend scope are in [toolchain.md](toolchain.md#pinned-vulkan-sources).

## 1. Toolchain prerequisites

All platforms need Git, CMake 3.25+, Ninja, Python 3.11+, the patched SDK, and
verified game input. The first build compiles the SDK and its dependencies as
well as the recompiled title; later builds are incremental. CMake/build gates
check the compiler and target-specific toolchain record in `config/toolchain*.toml`.

| Build host | Additional requirements |
| --- | --- |
| Windows | LLVM Clang 20+ (`clang`, `clang++`, `lld-link`, `llvm-ar`); Visual Studio C++ Build Tools and Windows SDK headers/libraries for the target architecture. Vulkan loader builds also need the Visual Studio MASM tools. Use an appropriate Visual Studio developer shell. |
| Linux | LLVM Clang 20+, a compatible C++23 standard library, libcurl 7.62+ development headers/libraries, and native SDL window/audio development dependencies. SDL enables X11/Wayland and ALSA/PulseAudio/PipeWire; provide development packages for the selected desktop/audio stack. A Vulkan-capable driver is needed to run. Zenity is needed for the packaged graphical importer. |
| macOS / iOS builder | Full Xcode 26+ (AppleClang 17+), selected Apple SDKs, and CMake/Ninja/Python on `PATH`. macOS deployment minimum is 13.3. iOS needs the iPhoneOS SDK and targets iOS/iPadOS 17.0+. |

On Windows, installing LLVM alone is insufficient: missing `<windows.h>`, C++
headers, or MSVC import libraries means the Visual Studio/Windows SDK environment
is incomplete. Ensure LLVM's `bin` directory is on `PATH`. Check before configuring:

```powershell
Get-Command cmake,ninja,python,clang,clang++,lld-link,llvm-ar
clang++ --version
```

On Linux, the presets name `clang-20` and `clang++-20`. If using another supported
LLVM release, pass `-DCMAKE_C_COMPILER=clang-XX -DCMAKE_CXX_COMPILER=clang++-XX`
at first configure. The SDK accepts `-DREXGLUE_GCC_INSTALL_DIR=/path/to/gcc/install`
to select a specific libstdc++ installation. Native distro dependency/minimum
version acceptance is still open; there is no validated universal package list.

On a Mac, select full Xcode before any builder invocation:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
xcrun clang --version
xcrun --sdk macosx --show-sdk-path
# Required for iOS builds:
xcrun --sdk iphoneos --show-sdk-path
cmake --version
ninja --version
python3 --version
```

The system's older Command Line Tools may select an unsupported compiler even
with a newer Xcode installed. `DEVELOPER_DIR` selects Xcode for this shell.
The Mac packager additionally checks recorded Xcode/SDK/compiler inputs; see
[its native validation rules](../../installer/macos/README.md).

## 2. First build and boot by platform

### Windows

After SDK patching, game import, and Vulkan preparation in section 0:

```powershell
cmake --preset win-amd64-release "-DREXGLUE_VULKAN_SOURCE_ROOT=$vulkanSources" -DREXGLUE_FETCH_VULKAN_DEPS=OFF
cmake --build --preset win-amd64-release
. ./dev.ps1
run-game
```

The helper runs `out/build/win-amd64-release/rb_blitz.exe` from its own directory
and supplies the checkout's `game/` root. The equivalent manual launch is:

```powershell
Push-Location out/build/win-amd64-release
try { & ./rb_blitz.exe --game_data_root=../../../game }
finally { Pop-Location }
```

For D3D12-only Windows builds, skip Vulkan preparation and configure with
`-DREXGLUE_USE_VULKAN=OFF`. Reconfigure with `ON` to restore Vulkan. Runtime
`--gpu_backend=vulkan` selects Vulkan only if it was compiled and a usable driver
is present. The in-game Renderer setting saves the choice for subsequent boots.

For native Windows ARM64, substitute `win-arm64-release` in the configure/build
commands and use ARM64 compiler/toolset libraries. The helper selects that preset
on ARM64 Windows. This route remains unvalidated; the preset alone does not set
up an x86-64-to-ARM64 cross compiler. The Windows installer currently packages
x86-64, independently of the game's ARM64 preset.

### Linux

After section 0, select the preset matching the native CPU:

```sh
# x86-64; use linux-arm64-release on a native ARM64 host.
cmake --preset linux-amd64-release \
  -DREXGLUE_VULKAN_SOURCE_ROOT="$PWD/out/deps/rex-vulkan" \
  -DREXGLUE_FETCH_VULKAN_DEPS=OFF
cmake --build --preset linux-amd64-release
source ./dev.sh
run-game
```

The helper selects the matching architecture, runs from `out/build/<preset>`,
and supplies `--title.game_root=<checkout>/game`. For a manual x86-64 launch,
retain an absolute game root before changing directory:

```sh
game_root="$PWD/game"
(cd out/build/linux-amd64-release && ./rb_blitz "--title.game_root=$game_root")
```

Native configure/link, graphics, audio, and gameplay acceptance remain open.
Keep the executable beside the staged SDK modules and native dependencies.
Linux packages have additional gates described below.

### macOS

Use the direct builder for the first native build. It imports or verifies game
input, applies SDK patches, configures the matching Mac preset, builds the title,
and validates/stages controller artwork, runtime dependencies, and licenses:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
sh scripts/macos/build.sh --source '/path/to/your/game-package-or-folder'
sh scripts/macos/run.sh
```

Later direct builds reuse verified input:

```sh
sh scripts/macos/build.sh
# Optional mod import; choose either source:
sh scripts/macos/build.sh --ultimate
sh scripts/macos/build.sh --ultimate-source '/path/to/Ultimate.zip'
```

The builder selects `mac-arm64-release` on Apple Silicon and `mac-amd64-release`
on Intel. It also creates `out/macos/Run Rock Band Blitz.command` for Finder or
Terminal launch. The runner accepts extra engine arguments, uses the private
checkout `game/`, and starts from the build directory. Keep the checkout in place.
Ultimate is selected through the in-game Game Version setting.

After the initial direct setup, the shared helpers provide incremental builds
and desktop/mobile preview launches:

```sh
source ./dev.sh
build-game
run-game --window
```

`build-game` stages the direct-build resources after a successful Mac build,
but does not import game input or apply SDK patches. macOS uses native
Application Support/settings/cache paths even for a direct build.

### iPhone and iPad

Build on a Mac with full Xcode. There are two distinct IPA routes; both still
need local verified retail input to generate the native game code.

**First-launch importer:** build an IPA without retail or Ultimate assets:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
sh scripts/ios/build-importer.sh --source '/path/to/your/game-package-or-folder'
```

This selects `ios-arm64-importer-release`. On device, first launch opens the
Files game picker, verifies/imports the base game, then offers skip/local/download
choices for Ultimate. Subsequent launches reuse verified imported assets.

**Bundled assets:** build a private IPA that skips on-device asset import:

```sh
sh scripts/ios/build.sh --source '/path/to/your/game-package-or-folder'
# To provide Ultimate locally instead of downloading it when missing:
sh scripts/ios/build.sh --ultimate-source '/path/to/Ultimate.zip'
```

This selects `ios-arm64-release` and embeds verified retail data and Ultimate.
If needed, it downloads the pinned Ultimate release. Keep this IPA private.
For either route, later builds can omit `--source` to verify/reuse `game/`.
The scripts build runnable Mac code generators and validators separately from
the device code; running only the device CMake preset misses this host-tool setup.

The default outputs under `out/ios/packages/` are:

| Mode | Filename | Default bundle ID |
| --- | --- | --- |
| Bundled | `RockBandBlitz-<version>-arm64-ios.ipa` | `io.github.gabrielofavero.rbblitz` |
| Importer | `RockBandBlitz-<version>-arm64-ios-importer.ipa` | `io.github.gabrielofavero.rbblitz.importer` |

With no signing flags, the output is unsigned. To sign during either build,
supply both a local identity and a compatible provisioning profile:

```sh
sh scripts/ios/build-importer.sh --source '/path/to/your/game-package-or-folder' \
  --bundle-id com.example.rbblitz.importer \
  --sign-identity 'Apple Development: Your Name (TEAMID)' \
  --provisioning-profile '/path/to/profile.mobileprovision'
```

The profile must cover the bundle ID/device and permit the requested extended
virtual addressing entitlement. Preserve that entitlement when re-signing.
The builders package and audit; they do not obtain signing credentials or install.

Install a correctly signed app on a connected physical device using Xcode's
device tools or the chosen signing/install workflow. For a signed `.app`
extracted from the IPA's `Payload/` directory, Xcode's CLI can install and launch:

```sh
xcrun devicectl list devices
xcrun devicectl device install app --device DEVICE_ID '/path/to/Payload/Rock Band Blitz.app'
bash ./dev.sh --platform ios run --device DEVICE_ID \
  --bundle-id io.github.gabrielofavero.rbblitz.importer
```

Use the ID actually used for signing; omit the importer suffix for the default
bundled app. `run` only launches an already installed app. You can also open its
icon on the device. See the [iOS guide](../../installer/ios/README.md) for signing
checks, Files import, DLC, touch controls, lifecycle behavior, and remaining
device acceptance. There is no simulator target or Android equivalent.

## 3. Incremental development

Once SDK patches, dependencies, and `game/` are prepared:

```sh
# Bash or zsh; Git Bash/MSYS2 on Windows.
source ./dev.sh
build-game
run-game --window
```

```powershell
# Windows PowerShell; dot-source to load aliases into this shell.
. ./dev.ps1
build-game
run-game --window
```

Use `bash ./dev.sh build --configure` or `./dev.ps1 build --configure` to
reconfigure before building. In a dot-sourced PowerShell session, use
`build-game -Configure`. Extra build arguments go to `cmake --build`, for example
`build-game --target rb_blitz`. These helpers configure a missing tree but do not
perform the initial SDK/game/dependency preparation. They always select Release;
use CMake directly for Debug or RelWithDebInfo.

On iOS, `bash ./dev.sh --platform ios build [builder arguments]` reruns the
bundled IPA pipeline. Add `--importer` for the separate importer route. These are
Mac commands, not commands to run in a terminal on the phone.

The current SDK generates and reloads `generated/default/sources.cmake` during
the normal build. No intentional first-build failure or mandatory second build
is required. Generated translation and fingerprint outputs remain untracked.

Before code generation, the fingerprint gate checks `game/default.xex` and the
toolchain gate checks the target's recorded SDK/compiler inputs. A fingerprint
mismatch usually means wrong revision or modified input; restore the supported
retail dump. Audit all three game files using the native build's
`rb_blitz_fingerprint --project-dir . --all` (`.exe` on Windows). Minimum version
or SDK pin mismatches fail; other tool version differences are reported unless
strict checking is enabled. Details: [toolchain.md](toolchain.md).

Close the game before rebuilding on Windows, and let an existing CMake/Ninja
operation finish before starting another in the same tree. The Windows helper
checks for these conflicts. Build the game target after runtime/graphics module
changes so the SDK restages the matching modules beside the executable.

Build trees record absolute source/tool paths. After moving a checkout or
changing compiler, configure a fresh tree. Back up local `rb_blitz.toml`, portable
settings, logs, and any Vulkan dependency sources before removing or replacing
a build tree. Do not copy a configured `out/` tree between platforms or machines.

### Tests without game navigation

Build the selected native preset first, then run its CTest suite:

```sh
cmake --build --preset win-amd64-release
ctest --test-dir out/build/win-amd64-release --output-on-failure
```

Substitute the native Linux/Mac preset as appropriate. If tests were disabled,
reconfigure with `-DBUILD_TESTING=ON` first. On Windows, run payload test binaries
through `out/build/win-amd64-release/rb_blitz_windows_test_runner.exe <test.exe>
[args...]`; the CTest registrations for `menu_payload`, `store_payload`, and
`payload_overlay` already do this. This keeps crashes in the console/CTest log.

The portable importer can be tested without the SDK or real game input:

```sh
cmake -S installer/portable -B out/build/portable-import -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/portable-import
ctest --test-dir out/build/portable-import --output-on-failure
```

The dev command tests use mocked launches and temporary profiles:

```sh
python3 tests/dev_commands_tests.py
```

```powershell
powershell -NoProfile -File tests/dev_windows_tests.ps1
```

Scripts in `scripts/acceptance/` and some `scripts/diagnostics/` helpers actually
launch/navigate the game. See [diagnostics](../engine/diagnostics.md) and the
[script directory guide](../../scripts/README.md) before using them.

## 4. Runtime data and logs

The launch helpers supply the checkout's game root and start in the matching
build directory. For manual launches, retain that working directory and the
complete modular runtime next to the executable. Use an absolute game root when
launching from another location. Useful desktop options include:

```sh
run-game --normal --no-dlc
run-game --window '--blitz.runtime.dlc_root=/path/to/DLC library'
run-game --mobile
run-game --touch
```

`--normal` forces retail mode for that launch. `--no-dlc` supplies an empty
library. `--mobile` opens a portrait desktop preview using a separate settings
copy; `--touch` lets mouse activity select touch prompts. Extra engine arguments
follow defaults and can override them. See the [full helper reference](../../scripts/dev/README.md)
for DLC discovery, profile selection, and quick mode.

Keep DLC outside the build tree when possible. The runtime accepts both the
Xbox title/content directory layout and recursively scanned loose CON/LIVE
packages. Configure the DLC library in game or supply an explicit root. See
[DLC layout and discovery](dlc.md). Runtime settings, saves, and caches are
separate from the private base game input.

| Platform | Default user data and diagnostics |
| --- | --- |
| Windows | Installed paths follow the chosen save/DLC directories and `paths.toml`; profiles normally use `%APPDATA%/rb_blitz`. Development logs are under `out/build/<preset>/logs/`. |
| Linux | Data under `${XDG_DATA_HOME:-$HOME/.local/share}/rb_blitz`, settings under `${XDG_CONFIG_HOME:-$HOME/.config}/rb_blitz`, cache under `${XDG_CACHE_HOME:-$HOME/.cache}/rb_blitz`. Development logs are in the build tree. |
| macOS | Imported game/settings/saves under `~/Library/Application Support/rb_blitz`; cache under `~/Library/Caches/rb_blitz`. Direct development logs are in the build tree. |
| iOS | Imported assets, settings, saves, and caches use the app sandbox's Library directories. Files exposes the app's `DLCs`, `Pictures`, and `Diagnostics` folders. |

Development sessions normally create numbered `logs/rb_blitz_001.log`, `_002`,
and later files. Installed sessions retain `game.log`, `game.1.log`, `previous.log`,
and `previous.1.log` in the selected save root's `Diagnostics` directory. Each
file is bounded to 2 MiB. An explicit `log_file` overrides the destination.
iOS exposes these session logs and `session-info.txt` in Files under the app's
`Diagnostics` folder.

Find a development log on Windows:

```powershell
$log = Get-ChildItem out/build/win-amd64-release/logs/*.log |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
Get-Content -LiteralPath $log.FullName -Tail 80
```

Or on Linux/macOS, substitute the matching preset:

```sh
ls -lt out/build/mac-arm64-release/logs/
tail -n 80 out/build/mac-arm64-release/logs/rb_blitz_001.log
```

When reporting a boot issue, include the platform/CPU, preset, build identity,
game identity, graphics backend, last log lines, and the route that failed.
State whether menu music or effects played. On Windows, retain the crash `.txt`
and `.dmp` and adjacent `song-load-recovery.log` if present; match process IDs to
the boot log. [Known issues](../reference/known-issues.md) and the
[bring-up history](../history/bringup-log.md) retain older diagnoses and evidence.

## Create an installer or package

Build the latest game before packaging. Package version comes from
`installer/config/pins.toml`; audits and acceptance gates still apply.

| Platform | Command from a prepared checkout | Output and limits |
| --- | --- | --- |
| Windows x86-64 | `./dev.ps1 build-installer` | Builds the game, then the Windows setup pipeline. Requires Inno Setup; outputs `installer/out/dist/RockBandBlitz-<version>-amd64-windows.exe`. |
| macOS | `sh installer/macos/build.sh --development --source '/path/to/game'` | Builds/stages a matching native `.app` and local development `.pkg` under `out/macos/`. Omit `--source` to reuse verified input; `--stage-only` creates only the app. Release acceptance is pending. |
| Linux | `python3 installer/linux/package.py --build-dir out/build/linux-amd64-release --stage-only` | Staging/audit route after a native build. Package generation remains gated by dependency inventory and release audit; the later `.deb`/`.rpm` route needs the pinned nFPM archive. No validated ARM64 package route. |
| iOS | `sh scripts/ios/build.sh [--importer] [signing/source arguments]` | Builds and audits the selected IPA under `out/ios/packages/`. Both routes are described above. |

`build-installer` dispatches to these existing packagers; it does not bypass their
gates. Windows and Linux helpers build first; the Mac helper invokes the native
development builder. On iOS it is another name for the IPA builder.

Desktop packages exclude private game/Ultimate data and import it during setup
or first launch. On Linux the desktop entry uses the Zenity importer; on macOS
the app uses its Cocoa launcher. Updates preserve imported data outside the app.
Changing `config/graphics_preparation.json` also requires rebuilding before
packaging so the embedded and staged catalogues agree.

Detailed package requirements and audits: [Windows](../../installer/README.md),
[Linux](../../installer/linux/README.md), [macOS](../../installer/macos/README.md),
[iOS](../../installer/ios/README.md), and [distribution policy](distributable.md).
