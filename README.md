# Rock Band Blitz — Xbox 360 static recompilation

A reproducible native Windows build of the Xbox 360 version of **Rock Band Blitz**
(title ID `5841122D`), produced with [ReXGlue](rexglue-sdk) (pinned v0.10.0
submodule) by statically recompiling the retail PowerPC code to host C++.

Status: milestones 0–3 of [DECOMPILATION_PLAN.md](DECOMPILATION_PLAN.md) are done
(the recompiled title boots to the offline title screen); milestone 4 (menus,
content, input, saves) is in progress. The MOGG music fix is implemented and
awaiting a build — see [docs/bringup-log.md](docs/bringup-log.md).

## You supply the game data

This repository contains **no** game code, assets, music, or keys. It expects a
game dump you extracted yourself from a copy you are authorized to use, pointed at
with a runtime path outside the source tree. Retail binaries, archives, music and
decryption keys are never committed here. See
[docs/build-and-run.md](docs/build-and-run.md).

## Build and run

[docs/build-and-run.md](docs/build-and-run.md) covers the one-time SDK-tree
preparation (§0), the prerequisite toolchain (LLVM, CMake, Ninja, Visual Studio
Build Tools), configure/build via `CMakePresets.json`, the game-data path argument,
and log capture.

## Where things are

| Path | What |
| --- | --- |
| [DECOMPILATION_PLAN.md](DECOMPILATION_PLAN.md) | Milestones, ownership boundaries, and the definition of "working". |
| [prompts/](prompts) | One living prompt per milestone; read [prompts/README.md](prompts/README.md) first. |
| [docs/bringup-log.md](docs/bringup-log.md) | Chronological record of blockers and fixes. |
| [docs/known-issues.md](docs/known-issues.md) | Enduring issues and accepted limitations. |
| [docs/symbols.md](docs/symbols.md) | Image identity, guest addresses, and the evidence for them. |
| [docs/rb3-references.md](docs/rb3-references.md) | What the Rock Band 3 projects already solved on this engine. |
| [src/](src) | Host application and native overrides for guest functions. |
| [config/](config) | Codegen inputs: confirmed function boundaries, names, fingerprints. |
| [patches/](patches) | Local fixes to the pinned SDK, kept as patch files so the submodule stays pristine. |
| [scripts/](scripts) | Configure/build/run helpers, plus the two SDK-tree repairs (§0 of the build guide). |

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
