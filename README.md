# Rock Band Blitz: Xbox 360 Static Recompilation

This is a Rock Band Blitz static recompilation project, based on the Xbox 360 version of the game and using [rexglue](rexglue-sdk) as the recompiler. The goal here is to make a stable, easy-to-install and customizable multi-platform version of this game. The initial goal is Windows, but Linux, macOS and mobile are planned.

Currently, the game boots, renders 2D and 3D elements correctly and is able to finish songs. Ultimate is compatible as well and a few enhancements are in place.

I'm now improving stability & performance, adding modularized tweaks and creating an installer and settings UI. I want this port to feel PC native, so very long road ahead.

## Enhancements

| Feature                                   | Type     | Status      | Notes                                                                                                                                                                                                                      |
| ----------------------------------------- | -------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Simple DLC Directory                      | Built-in | Done        | You can put all your CON/LIVE DLCs in the same directory without needing to emulate the 360 folder structure                                                                                                               |
| Hidden options and prompts                | Toggle   | In Progress | Online exclusive menu items (leaderboards, music store) and prompts (proceed to offline mode) are hidden. A few items left (Blitz Cred, Coins, Library headers)                                                            |
| DLC Cache                                 | Toggle   | In Progress | The idea is for only the first load to search for all content. After that, a cache file will be saved to speed things up and you can refresh manually the library. Works partially, but long road ahead to make it stable. |
| DLC background songs                      | Toggle   | In Progress | A random song is selected to play on the main menu instead of the default ones. Already works, but needs adjustments.                                                                                                      |
| Mouse Support (UI)                        | Toggle   | To Do       | Non-native version made, but unreliable. I'll need to reimplement this to be native.                                                                                                                                       |
| Non 16:9 resolutions                      | Toggle   | To Do       | Ultrawide support and preparation for portrait (mobile).                                                                                                                                                                  |
| Quick Play Background song                | Toggle   | To Do       | When combined with "DLC background songs", you can quickly play the song playing on main menu.                                                                                                                             |
| Multi logos                               | Toggle   | To Do       | Switch between Ultimate and default Rock Band Blitz logo                                                                                                                                                                   |
| Load Timeout                              | Toggle   | To Do       | Go back to the music library if DLC loading fails. Complex feature, might not be possible.                                                                                                                                     |
| Mouse Support (Controller)                | Toggle   | To Do       | Play with the mouse (Right click, left click and scroll)                                                                                                                                                                   |
| Multi button UI                           | Toggle   | To Do       | Be able to import from the PS3 assets the controller UI + create custom packages                                                                                                                                           |
| Uncap framerate                           | Toggle   | To Do       | No promises. Chances are the engine is not capable. (Spike needed)                                                                                                                                                        |
| (Mobile) Portrait interface               | Built-in | To Do       | Rearrange UI elements for portrait                                                                                                                                                                                        |
| (Mobile) Dynamic Orientation              | Built-in | To Do       | Adjust UI/Gameplay according to phone orientation. If not possible, toggle.                                                                                                                                                |
| (Mobile) Touch controls (UI and gameplay) | Built-in | To Do       | My goal is for this not to be an overlay, but true "click on text -> call to action"                                                                                                                                      |

Keep in mind that, as Ultimate is compatible (and recommended!), you get its features as well, such as power-ups in offline mode and gameplay adjustments. The full list is available in the [Rock Band Blitz Ultimate repository](https://github.com/ultimate-mods-rb/blitz-ultimate).

## How to Install

This is not released yet. I'll make a v1.0.0 once I'm satisfied with performance and stability (plus at least the most important enhancements). Once released, there will be an installer on the releases page that will allow you to quickly select your Xbox 360 copy and generate the game. First release is expected to be Windows only.

## How to Build

You can build for yourself to test how things are going (Windows + PowerShell, from the repo root). You need LLVM/Clang, CMake 3.25+, Ninja, the Visual Studio Build Tools *with* the Windows SDK on `PATH`, and the dump at `game/default.xex`.

```powershell
# once per checkout
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1

# the first build compiles the SDK and stops once after codegen; run it again to link
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
```

Outputs land in `out\build\win-amd64-release\`, and `.\rb_blitz.exe` runs from there. `ctest --test-dir out\build\win-amd64-release --output-on-failure` runs the host tests, and `installer\build.ps1` builds the installer exe (WIP).

**Full walkthrough and everything I've worked out about the game: [`docs\`](docs/)** — start with [docs/build-and-run.md](docs/build-and-run.md).

## AI Usage

This is a heavily AI assisted project. Like, heavily. The flow is: I use it for investigations, I decide the scope and architecture of the feature, AI implements and I do a code review and functional review. I know it's not the best flow, but I am a solo software engineer doing this as a pet project in my spare time. What I can attest is that, to the best of my software engineer abilities, I will dedicate myself to delivering things that are well-implemented and validated.
