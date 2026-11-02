# DaTArrest and BadUpdate in the PC recompilation

Research and code review: 2026-11-02.

## What the Xbox 360 chain does

[DaTArrest](https://github.com/RBEnhanced/DaTArrest) is the Rock Band Blitz DTA
script entry point. The [BadUpdate Rock Band Blitz guide](https://github.com/grimdoomer/Xbox360BadUpdate/wiki/Rock-Band-Blitz)
describes a crafted content/save package and a separate `BadUpdatePayload` USB
folder. The package's `songs/songs.dta` is loaded during downloadable-content
discovery. A script in that file escapes the title's DTA sandbox and starts a
PowerPC return-oriented-programming chain. The [BadUpdate stage description](https://github.com/grimdoomer/Xbox360BadUpdate/wiki/Exploit-Details)
then uses Xbox 360 kernel and hypervisor behavior to execute an unsigned XEX.
The guide calls the package a savegame, but the dangerous game input is song
metadata, not our `local_save.bin` profile checkpoint.

## Exposure in this port

This port recompiles the original guest code and presents STFS content to it.
`src/hooks/dlc.cpp` deliberately adapts saved-game CON song packages to
marketplace content, so the same class of external `songs.dta` can reach the
guest. The local progress checkpoint in `src/fs/local_save.h` has a separate
envelope and revision-11 shape check; it is not the DaTArrest input.

The later BadUpdate payload targets Xbox 360 PowerPC addresses, its OS, and its
hypervisor. A PC run has none of those targets, and this project has no loader
for `BadUpdatePayload/default.xex`. An unfiltered malicious DTA could still
make the recompiled guest execute DTA commands, corrupt guest state, or crash.
Native code execution cannot be ruled out solely from the architecture: the
guest calls host services and parsers, and those boundaries need their own
review and testing.

## Current guard

Patch `0047-content-device-validation.patch` adds a validation callback to the
SDK content mount path. The title installs it before guest content enumeration.
It runs on primary content, the structured DLC root, and flat libraries, after
the package is mounted but before its device or symbolic link is registered for
guest access. Every `songs.dta` and `songs.dtb` entry anywhere in the package
is bounded to 2 MiB, read fully, and checked as static
data. Text DTA refuses command/property delimiters, directives, variables,
autorun markers, control bytes, and malformed structure outside comments and
quotes. Compiled DTB accepts only scalar data, strings, symbols, and arrays;
executable tags are refused. A rejected package is not opened for the guest.
The log says `Nope. Not here` and gives the rejected path/reason as the
small easter egg. It adds no host overlay.

This guard is intentionally fail-closed for song metadata. A custom pack that
uses dynamic DTA or uncommon compiled tags may be refused even if benign. No
claim of complete code-injection immunity follows from this guard: other asset
parsers, malformed STFS structures, direct edits to trusted game files, and
file changes after validation are separate surfaces. In particular, the
integration test below uses a harmless script probe, not a real exploit package.

## Verification

`menu_music_pool` covers static text/compiled metadata and executable forms.
`song_content_safety` mounts synthetic content directories and checks that
the package guard refuses script-bearing entries even when another safe song
file is present.
`sdk_patch_stack` checks replay of the SDK patch. A release build checks that
the callback is linked into the PC application.
The device check also accepted two local STFS sample packages and one installed
song package as static metadata; these probes did not launch the game.

With the owner's authorization, a game run used an isolated structured DLC root
containing a synthetic package whose `songs/songs.dta` has `#autorun` and a
harmless `print` probe. The game log recorded two guard rejections during
content discovery, then `content_pipeline: usable menu` with two successful
content items. A second run with two ordinary STFS song packages in the
same layout reached the usable menu with four successful items and no guard
rejection. Both runs used fresh user-data roots and disabled optional library
background music; an earlier launch crashed while starting that music before
content discovery. Windows prevented the UI automation from
foregrounding the game window, so these are in-game log observations, not a
visual song-list or controller-navigation check. Logs: `out/build/win-amd64-release/logs/rb_blitz_468.log`
and `rb_blitz_469.log`.
