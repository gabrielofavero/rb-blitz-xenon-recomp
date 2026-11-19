# Development commands

From the repository root, load the commands into Bash or zsh:

```sh
source ./dev.sh
build-game
run-game --portrait
run-game --normal --no-dlc
dev-help
```

Use Git Bash or MSYS2 on Windows. The dispatcher detects Windows, Linux, or macOS
and loads `dev-windows.sh`, `dev-linux.sh`, or `dev-mac.sh`. Windows commands use
PowerShell; Linux/macOS commands use Python 3.11+. No shell profile is modified.
Windows PowerShell users can instead dot-source `scripts/dev/dev-windows.ps1`.

The same commands can run without loading shell functions:

```sh
bash ./dev.sh build --configure
bash ./dev.sh run --portrait --no-dlc
bash ./dev.sh build-installer
```

Both desktop architectures select their matching release preset. Game launches
use the checkout's `game/` folder and run from the build directory. Engine
arguments pass through after defaults; quote arguments containing spaces.
`run-game-normal`, `run-game-window`, `run-game-no-dlc`, `run-game-portrait`, and
`run-game-quick` remain available. `build-installer` uses the existing platform
packager and accepts its arguments. Linux packaging requires the pinned nFPM
archive and other arguments documented in `installer/linux/README.md`.

Portrait runs copy the selected launcher profile and settings into a new
`out/dev-portrait/` directory. They set `graphics.orientation = 'portrait'`,
`graphics.aspect_ratio = '16:9'`, and `graphics.force_16_by_9 = true`, including
manual provenance. The resolution profiles therefore use a 16:9 source even on
an ultrawide display. The source files are preserved; settings changed in a
portrait session stay in its copy.

DLC configuration follows the runtime's native paths file. Windows falls back
to registered installations, the default installation, then the selected legacy
profile when the checkout has no configured root. Linux/macOS use their native
configuration directory and fall back to the selected legacy profile. Relative
paths resolve beside their configuration file. Explicit DLC flags/environment
and `--no-dlc` take priority; an unavailable configured drive is reported rather
than silently replaced.

Quick mode uses a window and an empty DLC library and skips offline dialogs.
Windows also presses A after its configured delay, using the existing diagnostics
helper; `-NoSkipSplash` disables that input. Linux/macOS leave title-screen input
to the player. Build and launch commands remain separate.

iOS development runs on a Mac with full Xcode. Select it explicitly:

```sh
bash ./dev.sh --platform ios build --sign-identity 'Apple Development: ...' \
    --provisioning-profile /path/to/profile.mobileprovision
bash ./dev.sh --platform ios run --device DEVICE_ID
```

For a sourced session, set `RBBLITZ_DEV_PLATFORM=ios` before sourcing `dev.sh`.
The iOS builder uses the existing IPA pipeline. Install the signed app on the
device before `run`; launching uses `xcrun devicectl device process launch`.
`--bundle-id` selects the importer or another signed bundle, and
`RBBLITZ_IOS_DEVICE` supplies a default device. Engine arguments are device paths,
not Mac paths. Desktop window, quick, and portrait tags are unavailable there;
use the device's display settings. These helpers do not install or sign an app
as part of launch.

Run headless command tests with `python3 tests/dev_commands_tests.py`. Windows
also has `powershell -NoProfile -File tests/dev_windows_tests.ps1`. The tests use
temporary settings and mocked launch commands; they do not navigate the game.
