# Rock Band Blitz — Xbox 360 static recompilation

A reproducible native Windows build of the Xbox 360 version of **Rock Band Blitz**
(title ID `5841122D`), produced with [ReXGlue](rexglue-sdk) (pinned v0.10.0
submodule) by statically recompiling the retail PowerPC code to host C++.

Status: milestones 0–5 are done — the title boots, plays offline, shows its menus
and bundled content, takes pad and mouse input, authors its content into the
writable root, and runs a named song end to end three times in a row from clean
processes (`scripts/acceptance_song.ps1`), with frame pacing and input polling
measured and audited (`scripts/audit_pacing_input.ps1`). Milestone 6 — a release
another authorized developer reproduces from a clean checkout — is the one that is
open, and everything still pending is in [docs/backlog.md](docs/backlog.md). The
chronological record is in [docs/history/bringup-log.md](docs/history/bringup-log.md);
standing limits are in [docs/known-issues.md](docs/known-issues.md).

## You supply the game data

This repository contains **no** game code, assets, music, or keys. It expects a
game dump you extracted yourself from a copy you are authorized to use, pointed at
with a runtime path outside the source tree. Retail binaries, archives, music and
decryption keys are never committed here. See
[docs/build-and-run.md](docs/build-and-run.md).

## Vanilla or Rock Band Blitz Ultimate

This port does **not** restore, emulate or route around online services — the
retail title's own offline mode is the supported route, and the original services
being gone changes nothing that a playable port needs. Unblocked behaviour and
quality-of-life changes are the community's
[Rock Band Blitz Ultimate](https://github.com/ultimate-mods-rb/blitz-ultimate) mod;
our job is compatibility with it: an installed payload — the mod's files dropped next
to a vanilla game folder — runs on this same executable by drag-and-drop, with no
separate build and with a vanilla dump unaffected. Drop the payload in
`<game root>/ultimate`, or delete it to go back to vanilla.

The compilation input stays the **vanilla** `default.xex`, because every address in
[config/](config) belongs to that image; the mod's three edits are reproduced
host-side instead. Full policy, evidence, hazards and acceptance criteria:
[docs/ultimate-compat.md](docs/ultimate-compat.md).

## Build and run

The short version is below; [docs/build-and-run.md](docs/build-and-run.md) is the
detailed reference (toolchain installation, stale-tree regeneration, log capture).

**Once per checkout.** The pinned SDK submodule needs two idempotent repairs before
it can be built from — upstream's symlinked files cannot be created on a stock
Windows checkout, and the patch set carries the fixes the note highway needs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1
```

**Once per machine.** LLVM/Clang, CMake ≥ 3.25, Ninja, and the Visual Studio Build
Tools *with* the Windows SDK, all resolvable from `PATH`. Clang alone is not
enough — without the MSVC/UCRT headers the build fails on `<windows.h>`, not on a
missing compiler. [docs/build-and-run.md](docs/build-and-run.md) §1 has `winget`
commands and the two traps worth knowing about.

**Build the game.** The dump goes at `game/default.xex` (see
[You supply the game data](#you-supply-the-game-data)); the build fails closed,
before any translation, if it is not the image
[config/game_fingerprints.toml](config/game_fingerprints.toml) describes:

```powershell
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
```

The first configure+build compiles the SDK (SDL3, fmt, spdlog, mspack, the ReXGlue
runtime) from source and takes tens of minutes. Later builds are incremental and
routinely under a minute: `cmake --build --preset win-amd64-release` after a source
change, `--target rb_blitz` to skip unrelated targets, and `win-amd64-debug` /
`win-amd64-relwithdebinfo` for the other configurations. Outputs land in
`out\build\win-amd64-release\`: `rb_blitz.exe`, `rexruntime.dll`,
`rexgpu-xenos.dll`.

**Run it** from the build directory, which is where the exe finds its DLLs, its
`.toml` and `logs\`, with the game data path given on the command line (it may live
anywhere outside the source tree):

```powershell
cd out\build\win-amd64-release
.\rb_blitz.exe --game_data_root=..\..\..\game
```

Each launch writes a new `logs\rb_blitz_NNN.log`. Pointing `--game_data_root` at an
Ultimate payload root is supported and only warns; pointing it at the wrong dump
behaves the same way, so check the `game data identity:` line at the top of the log
rather than assuming a clean boot means the right game.

**Run the host tests** — SDK-free, game-data-free, about half a second:

```powershell
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

**Build the installer.** After the Release build above exists, one command in
[installer/build.ps1](installer/build.ps1) does the whole setup executable
(payload snapshot → embedded config → `rb_blitz_setup_helper.exe` → helper tests →
wizard image → ISCC):

```powershell
powershell -ExecutionPolicy Bypass -File installer\build.ps1
```

Result: `installer\out\dist\RockBandBlitzSetup-<version>.exe`, which embeds the
payload and therefore installs with no network access. It needs
[Inno Setup 6](https://jrsoftware.org/isdl.php) (ISCC); `-Preset
installer-release-msvc` builds the helper with `cl.exe` from a Visual Studio
developer prompt, and `-RefreshPayload`, `-SkipTests`, `-SkipArt`, `-SkipSetup`
control the individual stages. Passing `-PayloadUrl -PayloadSha256 [-PayloadSize]`
builds the smaller variant that downloads the payload at install time (see
[*Cutting a release*](installer/README.md)); `Get-Help .\installer\build.ps1 -Full`
documents every switch, and the installer's own tests run with `ctest --preset
installer-release` from `installer\`.

## Where things are

| Path | What |
| --- | --- |
| [docs/backlog.md](docs/backlog.md) | What is still pending: milestone 6, the fixes open known-issues imply, engine knowledge to port, and the scoped-but-unplanned designs. |
| [docs/history/bringup-log.md](docs/history/bringup-log.md) | Chronological record of blockers and fixes. |
| [docs/known-issues.md](docs/known-issues.md) | Enduring issues and accepted limitations. |
| [docs/ultimate-compat.md](docs/ultimate-compat.md) | Vanilla vs Rock Band Blitz Ultimate: install, evidence, what we support, what we refuse to do. |
| [docs/dlc.md](docs/dlc.md) | Where DLC packages go (`<game_data_root>/dlc/<title_id>/<content_type>/<package>`), how the guest enumerates them, and the `--dlc_root` setting. |
| [docs/symbols.md](docs/symbols.md) | Image identity, guest addresses, and the evidence for them. |
| [docs/rb3-references.md](docs/rb3-references.md) | What the Rock Band 3 projects already solved on this engine. |
| [docs/plans/](docs/plans) | Scoped designs with their own milestones and kill gates: [AV settings](docs/plans/av-settings-plan.md) (resolution, v-sync, volume, safe area inside the game's own Audio/Video screen), [button mapping](docs/plans/button-mapping-plan.md) (per-action binding layered above the guest input boundary), [VR port](docs/plans/vr-port-plan.md) (true-stereo Meta Quest, then iOS). |
| [src/](src) | Host application and native overrides for guest functions. |
| [assets/](assets) | Images and icons: `blitz.ico` is compiled into `rb_blitz.exe` and used by the installer, `blitz.png` is the installer's corner badge, the side wizard image is generated and not committed. |
| [tests/](tests) | Host unit tests (`ctest`); no SDK and no game image needed. |
| [config/](config) | Codegen inputs: confirmed function boundaries, names, and the fingerprint the build gate enforces. |
| [tools/](tools) | Host-side build tools: `rb_blitz_fingerprint`, the game-data gate, audit and header generator. |
| [patches/](patches) | Local fixes to the pinned SDK, kept as patch files so SDK fixes survive a re-checkout without committing inside the submodule. |
| [scripts/](scripts) | Configure/build/run helpers, plus the two SDK-tree repairs (§0 of the build guide). |
| [installer/](installer) | Standalone Windows setup executable (Inno Setup + a native helper): the click-and-install path for a user who is not going to build anything. See [installer/README.md](installer/README.md). |

## License

This project is licensed under the **GNU General Public License, version 2**
(**GPL-2.0-only**) — see [LICENSE](LICENSE). Copyright (C) 2026 the Rock Band
Blitz recompilation contributors.

"Version 2 only", rather than "or later", is deliberate: GPL-2.0 is the license
this work must stay compatible with in order to be able to adapt code from the
GPL-2.0 Rock Band 3 recompilation project. Do not relicense the project to
GPL-3.0 — it would cut off those sources.

The license covers this repository's own source only. It grants no rights to Rock
Band Blitz, its assets, or its trademarks, and it does not cover the game data
you supply.

Third-party components keep their own licenses:

| Component | License |
| --- | --- |
| [ReXGlue SDK](rexglue-sdk) (`rexglue-sdk/`, pinned submodule) | BSD-3-Clause, portions derived from the [Xenia project](https://xenia.jp) |
| Vendored libraries under `rexglue-sdk/thirdparty/` | MIT / BSD / BSL-1.0 / LGPL-2.1+ etc., as marked in each directory |

Engine knowledge and some adapted code come from
[ihatecompvir/band3_recomp](https://github.com/ihatecompvir/band3_recomp)
(GPL-2.0), and from [freeqaz/rb3-xenon](https://github.com/freeqaz/rb3-xenon) and
[freeqaz/rb3](https://github.com/freeqaz/rb3) (both CC0-1.0).
[docs/rb3-references.md](docs/rb3-references.md) records what came from where;
anything adapted from `band3_recomp` keeps its GPL-2.0 terms.
