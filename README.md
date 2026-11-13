# Rock Band Blitz: Xbox 360 Static Recompilation

Welcome! :)

This project statically recompiles the Xbox 360 version of Rock Band Blitz using [rexglue](rexglue-sdk). The goal is a stable, easy-to-install, customizable version for multiple platforms.

The project is in a soft launch: releases are public here, but I am not promoting them until version 1.0.0. You are welcome to try them. Pre-release builds are Windows only; Linux and macOS builds are planned for the full release.

The game boots, renders 2D and 3D elements, and can complete songs. It is also compatible with Ultimate and includes several enhancements.

Before v1.0.0, I plan to improve stability and performance and add the planned features outside the mobile scope. After release, I will focus on smaller stability improvements and additional platforms: mobile first, possibly VR. Other consoles are not currently planned.

## Enhancements

These enhancements aim to make the game feel native on PC and improve the offline experience. Restoring the original online mode, including leaderboards, Blitz Credits progression, and competition with friends, is outside the project scope.

| Feature                                                        | Type     | Status      | Notes                                                                                   |
| -------------------------------------------------------------- | -------- | ----------- | --------------------------------------------------------------------------------------- |
| Mouse Support                                                  | Toggle   | Done        | Navigate menus and play with a mouse.                                                   |
| Custom Interface and Gameplay Mapping                          | Toggle   | Done        | Set separate menu and gameplay inputs; mix mouse, keyboard, and controller.             |
| Multi-Thread DLC loading                                       | Toggle   | Done        | Choose the number of threads used to load DLC.                                          |
| Download Customs from  [RhythmVerse](https://rhythmverse.co/) | Toggle   | Done        | Download custom DLC through the in-game Music Store.                                    |
| Library Background Music                                       | Toggle   | Done        | Play songs from your library on the main menu.                                          |
| Music Player Overlay                                           | Toggle   | Done        | Skip between menu songs when Library Background Music is enabled.                       |
| Play Current Song                                              | Toggle   | Done        | Start playing the current menu song.                                                    |
| Custom bindings                                                | Toggle   | Done        | Map menu and gameplay controls independently.                                           |
| In-game Custom Settings                                        | Built-in | Done        | Configure options in the game, without a launcher.                                      |
| Simple DLC Directory                                           | Built-in | Done        | Place CON and LIVE DLC in one directory.                                                |
| Settings Labels Overlay                                        | Toggle   | Done        | Show optional settings descriptions in Normal, Big, or Biggest text.                                       |
| Refresh Songs quick button                                     | Toggle   | Done        | Refresh your library without returning to the main menu.                                |
| Compile shaders on Main Menu                                   | Built-in | Done        | Precompile shaders to reduce first-play stutter.                                        |
| Load Guard                                                     | Toggle   | Done        | Restart after a bad DLC load or a configurable loading timeout.                         |
| Bad DLCs fixes                                                 | Built-in | Done        | Fix some DLC-related crashes in sorting and song loading.                               |
| Custom Resolutions                                             | Toggle   | In Progress | 16:9 works; 32:9 and 4:3 support and HUD adjustments are planned.                       |
| Custom Button Layouts                                          | Toggle   | Done        | Switch among Xbox 360/Series, PlayStation 3/4/5, Nintendo, and Steam layouts.           |
| Many interface adjustments                                     | Toggle   | Done        | Hide unused menu options and the repeated Offline Mode prompt; more cleanup is planned. |
| In Game song manager                                           | Toggle   | To Do       | Delete DLC or hide songs from the library and menu music.                               |
| Non CON/LIVE DLCs support                                      | Built-in | To Do       | Explore support for extracted DLC folders and other formats.                            |
| In-game Messages and Update Checker                            | Built-in | To Do       | Show changelogs in game and offer updates.                                              |
| More framerate options                                         | Toggle   | To Do       | Investigate additional frame rates and engine limits.                                   |
| Play Powerups offline without Ultimate                         | Toggle   | To Do       | Add offline power-ups to the base game; Ultimate already supports them.                 |
| (Mobile) Portrait interface                                    | Built-in | To Do       | Rearrange the interface for portrait mode.                                              |
| (Mobile) Dynamic Orientation                                   | Built-in | To Do       | Adapt the interface and gameplay to device orientation.                                 |
| (Mobile) Touch controls (UI and gameplay)                      | Built-in | To Do       | Add native touch input for menus and gameplay.                                          |

Ultimate is compatible and recommended. It adds features such as offline power-ups and new HUD elements; see the [Rock Band Blitz Ultimate repository](https://github.com/ultimate-mods-rb/blitz-ultimate) for the full list. The installer offers Ultimate as an optional download.

## How to Install

Download the latest installer from the releases page and follow its instructions. You need the Xbox 360 Rock Band Blitz package or its extracted contents. The installer can download Ultimate. Importing PS3 button assets is not yet implemented, so you do not need those assets.

Pre-release builds are Windows only.

## How to Build

You can build the project on Windows using PowerShell from the repository root. Use the `develop` branch for the latest features.

You need LLVM/Clang, CMake 3.25+, Ninja, the Visual Studio Build Tools *with* the Windows SDK on `PATH`, and the dump at `game/default.xex`.

```powershell
# once per checkout
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\apply_sdk_patches.ps1

# the first build compiles the SDK and stops once after codegen; run it again to link
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
```

Build outputs go to `out\build\win-amd64-release\`; run `rb_blitz.exe` from that directory. Run host tests with `ctest --test-dir out\build\win-amd64-release --output-on-failure`. Use `installer\build.ps1` to build the installer from the current code.

See [`docs\`](docs) for information about the game, build and run instructions, and planned and completed features.

## AI Usage

This project uses AI extensively. I set the scope, make architectural and implementation decisions, review the code, and test the game manually.

AI helps investigate compiled code, suggest options, write code, and run automated tests. I am a software developer, but C++ is not my specialty, so I spend more time reviewing than coding, though I also make manual changes.

The icon was upscaled with AI from the 64 × 64 Xbox 360 icon. The installer cover uses official art extended vertically in Photoshop. Button layouts were adapted from human-created work, with attribution in [`docs\`](docs) and the in-game credits.

I know many people prefer projects made entirely by humans. As a solo developer, I use AI to help with this personal project while reviewing the results myself. I will release v1.0.0 and Linux/macOS builds when I am confident in their quality.
