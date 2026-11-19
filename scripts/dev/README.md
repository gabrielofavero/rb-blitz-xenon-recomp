# Development commands

From the repository root, load the commands into Bash or zsh:

```sh
source ./dev.sh
build-game
run-game --mobile
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
bash ./dev.sh run --mobile --no-dlc
bash ./dev.sh build-installer
```

Both desktop architectures select their matching release preset. Game launches
use the checkout's `game/` folder and run from the build directory. Engine
arguments pass through after defaults; quote arguments containing spaces.
`run-game-normal`, `run-game-window`, `run-game-no-dlc`, `run-game-mobile`, and
`run-game-quick` remain available. `build-installer` uses the existing platform
packager and accepts its arguments. Linux packaging requires the pinned nFPM
archive and other arguments documented in `installer/linux/README.md`.

`--mobile` replaces `--portrait`. It opens a 9:16 portrait window, selects the mobile
settings menu (including Touch controls and interface/gameplay render scales),
and enables mouse-as-touch for UI elements. Desktop-only display, renderer,
VSync, resolution, and Force 16:9 rows are hidden; the native graphics backend
is preserved. Preview rendering and render scales use a forced 16:9 source even
on ultrawide monitors. It does not enable diagnostic power-up or achievement effects.

Mobile runs copy the selected launcher profile and settings into a new
`out/dev-mobile/` directory and set both interface and gameplay orientation to
`'portrait'`, including manual provenance. Source files are preserved; settings
changed in a mobile session stay in its copy.

`--touch` starts with touch prompts and treats mouse movement, clicks, and
scrolling as touch activity for the interface, including the touch HUD. Keyboard
and controller input still select their own prompts; mouse activity returns to
touch prompts. Mouse navigation stays available. The flag applies only to that
launch and can be combined with `--mobile`. It maps to the engine setting
`--blitz.diagnostics.mouse_as_touch=true` and does not enable the diagnostic
power-up or achievement effects of `--touch_preview=true`.

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
not Mac paths. Desktop window, quick, and mobile preview tags are unavailable there;
use the device's display settings. These helpers do not install or sign an app
as part of launch.

Run headless command tests with `python3 tests/dev_commands_tests.py`. Windows
also has `powershell -NoProfile -File tests/dev_windows_tests.ps1`. The tests use
temporary settings and mocked launch commands; they do not navigate the game.
