# Rock Band Blitz — Xbox 360 static recompilation

A reproducible native Windows build of the Xbox 360 version of **Rock Band Blitz**
(title ID `5841122D`), produced with [ReXGlue](rexglue-sdk) (pinned v0.10.0
submodule) by statically recompiling the retail PowerPC code to host C++.

Status: milestones 0–3 of [DECOMPILATION_PLAN.md](DECOMPILATION_PLAN.md) are done.
Milestone 4 (menus, content, input, saves) works — the title takes its offline
route, shows its menus and bundled song list, and writes its content into the
writable root — with the ARK/HDR audit, pad-driven verification and save
persistence still open, so the milestone is not closed. Milestone 5 (complete one
song) is in progress: songs load, the note highway, gems, HUD and 3D background
render and music plays, and its exit criterion is now recorded rather than
observed — `scripts/acceptance_song.ps1` drives launch → a named song → results
three times in a row from clean processes, and plays a second song in the same
process with `-Replay`. What the milestone still owes is measurement, not function.
Playable-state detail and the open items are in
[docs/bringup-log.md](docs/bringup-log.md); standing limits are in
[docs/known-issues.md](docs/known-issues.md).

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
| [docs/ultimate-compat.md](docs/ultimate-compat.md) | Vanilla vs Rock Band Blitz Ultimate: install, evidence, what we support, what we refuse to do. |
| [docs/symbols.md](docs/symbols.md) | Image identity, guest addresses, and the evidence for them. |
| [docs/rb3-references.md](docs/rb3-references.md) | What the Rock Band 3 projects already solved on this engine. |
| [src/](src) | Host application and native overrides for guest functions. |
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
