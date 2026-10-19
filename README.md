# Rock Band Blitz: Xbox 360 Static Recompilation

Welcome! :)

This is in soft-launch stage, meaning I'm making releases public here, but not sharing them on any public page until version 1.0.0. For now, if you found this page by accident, feel free to test the releases! Just keep in mind the pre-release builds are Windows only (Release build will include Linux and macOS).

This is a Rock Band Blitz static recompilation project, based on the Xbox 360 version of the game and using [rexglue](rexglue-sdk) as the recompiler. The goal here is to make a stable, easy-to-install and customizable multi-platform version of this game. The initial goal is Windows, but Linux, macOS and mobile are planned.

Currently, the game boots, renders 2D and 3D elements correctly and is able to finish songs. Ultimate is compatible as well and a few enhancements are in place.

My goal in order to deliver v1.0.0 is to improve stability and performance (shaders can cause hiccups) and add some cool enhancements. After release, my goal will be minor improvements and work on expanding platforms (mobile first, VR possibly, other consoles not in plans for now).

## Enhancements

| Feature                                                   | Type     | Status      | Notes                                                                                                                                                                                                                                                                                                           |
| --------------------------------------------------------- | -------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Custom bindings                                           | Toggle   | Done        | You can map different inputs for gameplay and interface navigation. Meaning you can control the menu in a very user friendly pc-like way and play the game in whatever bindings you wish (Controller, Keyboard, Mouse, mix and match).                                                                          |
| In-game Custom Settings                                   | Built-in | Done        | Every customizable option is configurable in game. There is no launcher so that this port feels as native as possible.                                                                                                                                                                                         |
| DLC Cache                                                 | Toggle   | Done        | This speeds up the process of loading your DLCs on boot. First load always takes more time, but once the cache is built, the next boots will be faster. Keep in mind adding DLCs recreates the cache partially, so loading time will increase on that new boot.                                                 |
| Simple DLC Directory                                      | Built-in | Done        | CON/LIVE DLCs can be added in the same directory side by side. No need to emulate the Title ID Xbox 360 structure.                                                                                                                                                                                              |
| Multi-Thread DLC loading                                  | Toggle   | Done        | You can increase the number of threads to load the DLC. Helps speeding up loading times.                                                                                                                                                                                                                        |
| Mouse Support                                             | Toggle   | Done        | Native mouse support for navigating the interface. Scrolling also works. Left click confirms and right click goes back.                                                                                                                                                                                         |
| Settings Labels Overlay                                   | Toggle   | Done        | A little overlay that shows a description of that setting option you are hovering. Looks like a subtitle and is not very intrusive, but you can disable if you like.                                                                                                                                            |
| Play Current Song button                                  | Toggle   | Done        | One of my favorite features. When "DLC Background Songs" is on, you can add this option on main menu to quickly go to whatever is playing on the background.                                                                                                                                                   |
| Refresh Music Library button                              | Toggle   | Done        | Quick little button to refresh your library in game. Not a big feature, but helps not having to go back to main screen every time.                                                                                                                                                                              |
| Ultrawide and 4:3 support                                 | Toggle   | In Progress | 21:9 is already possible. Other proportions (32:9, 4:3) are to do. HUD is already dynamic, meaning the elements will adjust their positioning to your screen.                                                                                                                                                  |
| Vulkan Compatibility                                      | Built-in | In Progress | Hard requirement for a Linux/macOS build. Partially implemented, but many tests and adjustments are needed.                                                                                                                                                                                                     |
| DLC Background Songs                                      | Toggle   | In Progress | A pool is drawn at random from your whole DLC library every boot and the main menu plays it as a shuffled deck: one song after another, a different one each time a song ends. Main feature already works. I'm planning to include an option to navigate songs (RB for next and LB for previous)               |
| Many interface adjustments                                | Toggle   | In Progress | You can hide useless options from the main menu (leaderboards, music store), as well as the annoying Offline Mode double prompt. More improvements planned, like hiding Blitz Cred / Coins.                                                                                                                     |
| Wire Music Store to [RhythmVerse](https://rhythmverse.co/) | Toggle   | To Do       | This is one of the more complex, but coolest features to implement. It will allow to download customs directly in game on the largest customs website today. It's feasible, but many UI changes and API studies are needed.                                                                                     |
| Shader Cache alternative                                  | Built-in | To Do       | Another big and important feature. Right now, we compile shaders on the fly. This means the first gameplays are going to stutter. For music games, that's not acceptable, so the goal here is to pre-cache shaders on the first menu loading screen with a progress bar.                                       |
| Non CON/LIVE DLCs support                                 | Built-in | To Do       | The goal here is to be able to read more formats that are common for YARG and Clone Hero, like extracted folders. No promises as it might need too much engine tinkering.                                                                                                                                       |
| Load Guard                                                | Toggle   | To Do       | The idea is to go back to main menu when the game crashes while trying to load a song. Complex and might be one of the last features.                                                                                                                                                                           |
| Custom Button Layouts                                     | Toggle   | To Do       | The idea is to have swappable, hot UI buttons. I'll do by hand (no AI) layouts for Modern Xbox, Modern PlayStation, Nintendo Switch and Keyboard. You will also be able to import from the PS3 original icon. For keyboard, once a press is detected, we will auto swap the elements just like modern games do. |
| In-game Update Checker                                    | Built-in | To Do       | A simple, lazy check if there are new releases. We will prompt once on main menu, and if ignored never again until user updates. There will be an Update button on Help & Options. It's not an in-game updater. It will close the game and open the updater executable.                                         |
| More FPS options                                          | Toggle   | To Do       | For now, I don't know if this is possible and will work more down the line. There might be engine limitations.                                                                                                                                                                                                  |
| (Mobile) Portrait interface                               | Built-in | To Do       | Rearrange UI elements for portrait                                                                                                                                                                                                                                                                              |
| (Mobile) Dynamic Orientation                              | Built-in | To Do       | Adjust UI/Gameplay according to phone orientation. If not possible, in-game toggle that requires a restart.                                                                                                                                                                                                     |
| (Mobile) Touch controls (UI and gameplay)                 | Built-in | To Do       | My goal is for this not to be an overlay, but true "click on text -> call to action"                                                                                                                                                                                                                            |

Keep in mind that, as Ultimate is compatible (and recommended!), you get its features as well, such as power-ups in offline mode and gameplay adjustments. The full list is available in the [Rock Band Blitz Ultimate repository](https://github.com/ultimate-mods-rb/blitz-ultimate) and to install is quite simple, as it is downloadable in the installer as an optional step. You can play without Ultimate of course... I just don't know why would you.

## How to Install

Go to the releases page and pick the latest installer. Follow the steps, as they are straightforward enough. The only hard requirement is to have the Xbox 360 package for Rock Band Blitz. If you have it extracted, it works as well. Ultimate can be downloaded on the fly and PS3 assets will only be needed if you want to import the button scheme (right now is not implemented, so for now no need).

As I mentioned before, pre-releases are Windows only.

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

Outputs land in `out\build\win-amd64-release\`, and `.\rb_blitz.exe` runs from there. `ctest --test-dir out\build\win-amd64-release --output-on-failure` runs the host tests, and `installer\build.ps1` builds the installer exe out of the current code.

There are many documents under [`docs\`](docs) about how the game works, build and run details and planned/past features. Feel free to explore.

## AI Usage

Being transparent, this is a heavily AI assisted project. I don't consider to be AI driven because it comes with the presumption that the AI directs the flow, makes all choices and my only work is prompt engineering. That's not the case.

My job is to decide the scope of the features, plan for the architecture, take the implementation decisions, review the code and do manual testing. AI's job is to do the investigations on the compiled/hard to read code, present me options, do automated testing and coding. I'm a software developer, but C++ is not my speciality so I'm usually more focused on reviewing rather than coding (though I do some manual adjustments here and there).

About art, the icon was AI Upscaled from the 64x64 Xbox 360 Icon. The cover on the installer is the official background, but Photoshop expanded (not AI per se) to be higher. All button layouts that I'll do are going to be fully human made, either by picking something public online or by doing by hand.

And yes, I know that 100% human projects are way more welcome by the community. It's just that I'm a solo dev working on this. And this is like my pet project, not something that I want to consume all of my free time into (even though, to be honest, I am at the moment --'). What I can promise is that I'm doing my best to deliver quality and not another AI-slop port, that's why I plan to take things slow and only release v1.0.0 and Linux/macOS once I'm 100% confident.
