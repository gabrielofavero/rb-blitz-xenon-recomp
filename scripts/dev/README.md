# Development commands

`dev.sh` and `dev.ps1` provide build, launch, and packaging shortcuts for a
prepared checkout. Start with the [build and run guide](../../docs/guides/build-and-run.md)
for toolchains, SDK patching, game import, and each platform's first build.
The helpers do not install those prerequisites or acquire the retail game.

## Load commands or run one action

From the repository root, load functions into Bash or zsh:

```sh
source ./dev.sh
build-game
run-game --window
dev-help
```

Use Git Bash or MSYS2 on Windows. Windows PowerShell users can load aliases with:

```powershell
. ./dev.ps1
build-game
run-game --window
dev-help
```

Loading the helpers changes only the current shell session; it does not edit
your shell profile. You can also execute one action without loading functions:

```sh
bash ./dev.sh build --configure
bash ./dev.sh run --window --no-dlc
bash ./dev.sh help
```

```powershell
./dev.ps1 build --configure
./dev.ps1 run --window --no-dlc
./dev.ps1 help
```

Quote each argument containing spaces. Extra engine arguments follow the
helper's defaults, so explicit values can override defaults for that launch:

```sh
run-game --window '--blitz.runtime.dlc_root=/path/to/DLC library'
```

## Dispatch and file responsibilities

| File | Responsibility |
| --- | --- |
| Root `dev.sh` | Detects the host with `uname`, or accepts `--platform windows\|linux\|mac\|ios` before the action. Loads the platform shell helper. |
| Root `dev.ps1` | Executes or dot-sources the Windows PowerShell implementation. |
| `dev-windows.sh`, `dev-linux.sh`, `dev-mac.sh`, `dev-ios.sh` | Select the platform and load `common.sh`. iOS must be explicitly selected on a Mac. |
| `common.sh` | Defines the shared shell functions and aliases. Calls PowerShell on Windows and Python on Linux/macOS/iOS. |
| `dev-windows.ps1` | Implements Windows builds, launches, DLC discovery, temporary mobile profiles, quick boot input, and installer dispatch. |
| `dev.py` | Implements native Linux/Mac builds and launches, settings copies, and dispatch to Apple builders, Linux packaging, or Xcode device launch. Requires Python 3.11+. |

The Windows Bash bridge prefers `pwsh`, then `powershell.exe`. Windows DLC/mobile
profile handling also needs Python 3.11+. Platform selection chooses a native
workflow; it does not create a cross-compilation environment. Mac/iOS actions
require a Mac; Linux actions require a native Linux host.

Desktop helpers detect x86-64/ARM64 and choose the matching Release preset:
`win-{amd64,arm64}-release`, `linux-{amd64,arm64}-release`, or
`mac-{amd64,arm64}-release`. Game launches use the checkout's `game/` folder and
run from `out/build/<preset>`. They do not launch an installed desktop copy.
Platform acceptance limits still apply; see the main guide's platform table.

## Command reference

| Loaded command | One-action spelling | Behavior |
| --- | --- | --- |
| `build-game` | `build` | Configure if no cache exists, then build the native Release preset. Extra arguments go to `cmake --build`. On Mac, stage validated direct-build resources after success. |
| `run-game [flags] [engine arguments]` | `run` or `run-game` | Launch the existing build; does not rebuild. |
| `run-game-normal` | `run-normal` | Launch with `--normal`. |
| `run-game-window` | `run-window` | Launch with `--window`. |
| `run-game-no-dlc` | `run-no-dlc` or `no-dlc` | Launch with `--no-dlc`. |
| `run-game-mobile` | `run-mobile` | Launch with `--mobile`. |
| `run-game-quick` | `quick` | Launch with `--quick`; Windows also injects title-screen input by default. |
| `build-installer [arguments]` | `build-installer` or `installer` | Build and dispatch to the existing platform package pipeline. |
| `dev-help` | `help`, `--help`, or `-h` | Show help without building or launching. |

`build --configure` forces configure before building on desktop. Bash functions
also accept `build-game --configure`; a dot-sourced PowerShell session uses
`build-game -Configure`. This requests a plain preset configure, not a place to
pass `-D` cache variables. Set cache variables with `cmake --preset <preset> -D...`
directly, then use the helpers for incremental builds. Debug/RelWithDebInfo also
use CMake directly.

```sh
build-game --target rb_blitz
bash ./dev.sh build --configure --parallel 4
```

The Windows builder refuses to run while the game is open or another CMake/Ninja
process is using the selected tree. Close the game or wait for that build before
retrying. Linux/Mac callers must also avoid concurrent operations in one tree.
The helpers do not reapply SDK patches or import game input. For the first Mac
build, use `sh scripts/macos/build.sh --source '/path/to/game'`.

## Desktop launch flags

Combine these with `run-game` or its one-action equivalent:

| Flag | Effect for this launch |
| --- | --- |
| `--window` / `--windowed` | Disable fullscreen. |
| `--normal` | Force retail game mode even if Ultimate is installed. |
| `--no-dlc` | Point both DLC root and library at an empty folder under `out/`. |
| `--touch` | Start with touch prompts and treat mouse activity as touch UI activity. |
| `--mobile` | Use a portrait desktop window, mobile settings UI, mouse-as-touch, and a separate settings copy. |
| `--quick` | Windowed launch, empty DLC library, and skipped offline dialogs. Windows also presses A after a delay. |

`--mobile` replaces the old `--portrait` spelling. It opens a 9:16 preview window
and selects mobile settings, including Touch controls and interface/gameplay
render scales. Desktop-only display, renderer, VSync, resolution, and Force 16:9
rows are hidden; the native graphics backend is preserved. Preview rendering
uses a forced 16:9 source even on ultrawide monitors. This previews mobile UI
on the desktop; it does not build or run an iOS binary.

Mobile runs copy the selected launcher profile and settings into a fresh
`out/dev-mobile/` session and set interface/gameplay orientation to `'portrait'`,
with manual provenance. Source files are preserved. Settings changed during a
mobile session remain in that session's copy. The helper prints its directory.
Profile selection respects `RBBLITZ_CONTROLLER_SETTINGS_PROFILE`, an explicit
`--blitz.runtime.launcher_profile=...` argument, and the runtime's settings-directory
selection. Relative profile arguments resolve from the build directory.

`--touch` maps to `--blitz.diagnostics.mouse_as_touch=true`. Mouse movement, clicks,
and scrolling select touch prompts, including the touch HUD. Keyboard/controller
activity still selects its own prompts; later mouse activity selects touch again.
Mouse navigation stays available. Neither `--touch` nor `--mobile` enables the
diagnostic power-up/achievement effects of `--touch_preview=true`.

Quick mode has a platform-specific input behavior:

- Windows starts the game and returns immediately, then the diagnostics helper
  presses A once after 28 seconds. `-BootDelaySec N` changes the delay;
  `-NoSkipSplash` disables automatic input. This mode can navigate the title screen.
- Linux/macOS run the game in the foreground and leave title-screen input to the
  player. They have no automatic A press.

```sh
run-game --mobile --normal --no-dlc
run-game --touch --window
# Windows only: quick configuration without injected input.
run-game --quick -NoSkipSplash
```

## DLC discovery

Explicit DLC arguments/environment and `--no-dlc` take priority. Relevant engine
flags are `--blitz.runtime.dlc_root` and `--blitz.runtime.dlc_library`; legacy
`--dlc_root` and `--dlc_library` spellings are also recognized.

Windows checks the checkout's build-tree `paths.toml`, then registered
installations/default installation, then the selected legacy launcher profile.
Only the installed DLC path is inherited; development saves keep their own route.
Linux/macOS defer to the runtime's native `paths.toml`, falling back to the
selected legacy profile when no root is configured. Portable markers select
build-tree configuration instead of native config directories. Relative paths
resolve beside their configuration file. A missing configured drive is reported
instead of silently replaced with a different library.

Linux configuration defaults to `${XDG_CONFIG_HOME:-$HOME/.config}/rb_blitz`;
Mac configuration defaults to `~/Library/Application Support/rb_blitz`. See the
[DLC guide](../../docs/guides/dlc.md) for supported package layouts.

## Installer dispatch

| Platform | What `build-installer` does |
| --- | --- |
| Windows | Build the native game, then call `installer/build.ps1` using Windows PowerShell 5.1. The setup pipeline currently targets x86-64 and requires Inno Setup. |
| macOS | Call `sh installer/macos/build.sh --development`, which imports/verifies input, builds, stages, audits, and makes a local development package. |
| Linux | Build the native game, then call `installer/linux/package.py --build-dir out/build/<preset>`. Supply packager arguments such as `--stage-only` or the pinned nFPM archive; release gates still apply. |
| iOS | Same as the IPA `build` action, including its signing/importer options. |

Additional arguments go to the packager, not to CMake. Packaging details and
outputs: [Windows](../../installer/README.md), [Mac](../../installer/macos/README.md),
[Linux](../../installer/linux/README.md), [iOS](../../installer/ios/README.md).

## iOS commands on a Mac

Select iOS explicitly; an ordinary Mac session selects the desktop Mac workflow:

```sh
bash ./dev.sh --platform ios build --source '/path/to/game-package-or-folder'
# Separate asset-free importer IPA:
bash ./dev.sh --platform ios build --importer --source '/path/to/game-package-or-folder'
```

These call `scripts/ios/build.sh`. The default route bundles private retail and
Ultimate data; `--importer` packages a first-launch importer without those assets.
Both require verified local game input for developer-side code generation.
`--sign-identity` and `--provisioning-profile` must be supplied together to sign;
otherwise the IPA is unsigned and needs signing before installation.

Install the signed app before launching with this helper:

```sh
bash ./dev.sh --platform ios run --device DEVICE_ID
# Importer app, whose default bundle ID differs:
bash ./dev.sh --platform ios run --device DEVICE_ID \
  --bundle-id io.github.gabrielofavero.rbblitz.importer
```

Launch calls `xcrun devicectl device process launch`. The default bundle ID is
`io.github.gabrielofavero.rbblitz`; use `--bundle-id` for the importer or a custom
signed identity. `RBBLITZ_IOS_DEVICE` supplies a default device ID. Engine path
arguments refer to the device sandbox, not the Mac filesystem. Launch does not
build, sign, copy, or install the app.

For loaded iOS commands, select the platform before sourcing:

```sh
export RBBLITZ_DEV_PLATFORM=ios
source ./dev.sh
build-game --importer
run-game --device DEVICE_ID --bundle-id io.github.gabrielofavero.rbblitz.importer
```

Desktop window, quick, and mobile preview tags are unavailable on iOS. Use the
device's in-game display settings. The helper supports `--normal` and `--no-dlc`
for launch isolation. See the [complete iOS build/install route](../../docs/guides/build-and-run.md#iphone-and-ipad)
for device signing and first-launch setup.

## Headless checks

```sh
python3 tests/dev_commands_tests.py
```

```powershell
powershell -NoProfile -File tests/dev_windows_tests.ps1
```

These use temporary settings and mocked launch commands; they do not navigate
the game. Actual launch and acceptance scripts require a separate runtime test.
