# Rock Band Blitz — Xbox 360 static recompilation

This is a Rock Band Blitz static recompilation project, based on the Xbox 360 version of the game and using [rexglue](rexglue-sdk) as the recompiler. The goal here is to make a stable, easy to install and customize, Windows version of this game.

Currently, the game boots, renders 2D and 3D elements correctly and is able to finish songs. Ultimate is compatible as well (optional).

I'm now improving stability & performance, adding modularized tweaks and creating an installer and launcher. I want this port to feel PC native, so very long road ahead.

## Enhancements

**Already here:** Compatibility with Ultimate was a must from the start and is already implemented. Mouse support is here, but glitchy at the moment and might take some time to fine-tune it.

**Future:** I plan to ship v1.0 with a simplified save/DLC system, custom resolutions and a working installer and launcher, so that anyone can run and customize the game without dev experience. Enhancements will be toggleable.

## How to Install

Download the installer from the releases section. It's very early into the development lifecycle, so keep in mind things might look rough. There is no release up yet, and until there is, the build below produces the same setup exe.

You will need to provide the 360 package to install. It can be either packed or already extracted. Nothing from the game ships in this repo. Use a copy you are authorized to have.

## How to Build (dev)

Everything below assumes Windows and runs in PowerShell, from the repo root.

**Prerequisites:** LLVM/Clang, CMake 3.25+, Ninja and the Visual Studio Build Tools *with* the Windows SDK, all of them resolvable from `PATH`. Clang alone is not enough, since without the MSVC/UCRT headers the build fails on `<windows.h>`, not on a missing compiler. [docs/build-and-run.md](docs/build-and-run.md) §1 has the `winget` commands and the two traps worth knowing about. The versions this project is built and accepted with are frozen in [config/toolchain.toml](config/toolchain.toml) and explained in [docs/toolchain.md](docs/toolchain.md); every build checks the machine against that record before it compiles anything.

**Once per checkout**, the pinned rexglue submodule needs two idempotent repairs: upstream's symlinked files cannot be created on a stock Windows checkout, and the patch set carries the fixes the note highway needs.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1
```

**Build.** The dump goes at `game/default.xex`; the build fails closed, before translating anything, if it is not the image [config/game_fingerprints.toml](config/game_fingerprints.toml) describes:

```powershell
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
```

The first configure+build compiles the SDK (SDL3, fmt, spdlog, mspack, the rexglue runtime) from source and takes tens of minutes. The very first build of a tree that has never run codegen stops once, right after generating the recompiled code, and says so: a build system cannot learn about sources that appear in the middle of it, so run the same two commands again and the second build links the executable. The other preset does not need that second pass — codegen has run by then. After that it is incremental and routinely under a minute: `cmake --build --preset win-amd64-release` after a source change, `--target rb_blitz` to skip unrelated targets, and `win-amd64-debug` / `win-amd64-relwithdebinfo` for the other configurations (the Debug preset builds, its host tests pass, and it runs the whole offline route; [docs/build-and-run.md](docs/build-and-run.md) §3). Outputs land in `out\build\win-amd64-release\`: `rb_blitz.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`.

**Run** it from the build directory (that is where the exe finds its DLLs, its `.toml` and `logs\`), with the game data path on the command line, since it can live anywhere outside the source tree:

```powershell
cd out\build\win-amd64-release
.\rb_blitz.exe --game_data_root=..\..\..\game
```

Each launch writes a new `logs\rb_blitz_NNN.log`. Pointing `--game_data_root` at an Ultimate payload root is supported and only warns; pointing it at the wrong dump behaves the same way, so check the `game data identity:` line at the top of the log instead of assuming a clean boot means the right game.

**Extract the assets.** The title's content is one ARK archive pair under `game\gen\`, plus the Ultimate payload's overlay pair. One command unpacks both into a gitignored `extracted\` — byte-exact, with a JSON index per archive:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\extract_assets.ps1
```

[assets.md](docs/assets.md) has the resulting layout, the archive format and what is still obfuscated.

**Tests** are SDK-free and game-data-free, about half a second:

```powershell
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

**Build the installer.** After the Release build exists, one command does the whole setup exe (payload snapshot, embedded config, helper, helper tests, wizard image, ISCC):

```powershell
powershell -ExecutionPolicy Bypass -File installer\build.ps1
```

Result: `installer\out\dist\RockBandBlitzSetup-<version>.exe`, which embeds the payload and therefore installs with no network access. It needs [Inno Setup 6](https://jrsoftware.org/isdl.php) (ISCC); `-RefreshPayload`, `-SkipTests`, `-SkipArt` and `-SkipSetup` control the individual stages, and [installer/README.md](installer/README.md) covers the releases side of it.

Everything I've worked out about the game lives in `docs\`: [known-issues.md](docs/known-issues.md) for the standing limits, [backlog.md](docs/backlog.md) for what's next, [toolchain.md](docs/toolchain.md) for the frozen build toolchain, [distributable.md](docs/distributable.md) for what the installer ships and the audit that refuses the rest, [dlc.md](docs/dlc.md) for where DLC packages go, [ultimate-compat.md](docs/ultimate-compat.md) for the Ultimate install, [rb3-references.md](docs/rb3-references.md) for the Rock Band 3 knowledge I borrow, [symbols.md](docs/symbols.md) for the guest addresses, [assets.md](docs/assets.md) for unpacking the title's archives, and [history/bringup-log.md](docs/history/bringup-log.md) for the whole chronological record.

## How about AI usage?! I WON'T play this if it is one of those AI trash ports!! 😡😡

Yes, this is a heavily AI assisted project. Like, heavily. The flow is: I use it for investigations, I decide the scope and architecture of the feature, AI implements and I do a code review and functional review. I follow the recomp/decomp community and know pretty well the public opinion about this.

Being 100% clear, I am a solo software engineer doing this as a pet project on my spare time. It was either AI assist or nothing. You are absolutely free to not install the game if it is not on your moral code, nobody is forcing you to. And I welcome other people to make different versions! This is one of my favorite Rock Band games and it would be great to see more love out of it.

What I can attest is that, to the best of my software engineer abilities, I will dedicate myself to delivering things that are well implemented and validated.

## The future

Being honest, a PC version was never my goal. RPCS3 is already great at emulating Blitz. But this is the first step for expansion.

My goal is to make an iOS version and, as a final and probably unlikely step, a Quest 3 VR & MR port. iOS will come first as I think it will be great on touch and only after that I will mess with VR since it's a whole different beast. It's honestly very ambitious and I'm not sure I will carry through this, so let's see.

My scope is only within Windows, iOS and Quest 3 because these are the platforms that I own and use. I do not want to make ports of things that I can't test to ensure they have quality. But of course, feel free to fork and work on other ports! License is [GPL-2.0-only](LICENSE), so knock your socks off.