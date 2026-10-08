# Enhancement toggles

The contract for the customization features in
[plans/customization-plan.md](../plans/customization-plan.md) (R1–R9, plus R10 below, which a
follow-up request added). **Every toggle is off by default, with two deliberate exceptions:** R3
ships on, because the two questions it answers can no longer be answered on an install with no Rock
Central to reach, and R10 ships on, because its subject is the Ultimate mod's own screen and it does
nothing without that mod installed. This page is the table the plan's contract 1 promises: what each
toggle gates, what the faithful (off) behaviour is, and which prompt implements it — or, for the
remaining planned ones, what will. R3, R5, R7, R9 and R10 are implemented; R3, R5 and R10's
evidence is [main-menu-flow.md](main-menu-flow.md), R7's is
[src/fs/dlc_cache.h](../../src/fs/dlc_cache.h) and [dlc.md](../dlc.md) §4.1, and R9's is
[menu-music.md](menu-music.md).

The cvars live in [src/enhancements.cpp](../../src/enhancements.cpp); the names are asserted by the
boot log, not by this page.

## How to set one

The runtime reads `<exe name>.toml` beside the executable (`out\build\win-amd64-release\rb_blitz.toml`
on this machine — see [build-and-run.md](../build-and-run.md)). Its own rule joins a nested table's
path with `_` (`ApplyTomlTable` in `rexglue-sdk/src/core/cvar.cpp`), so the table below is the cvar
name split at its underscore:

```toml
[enhancements]
skip_offline_dialog = true
```

The same value can be given on the command line (`--enhancements_skip_offline_dialog=1`) or in the
environment (`REX_enhancements_skip_offline_dialog=1`), which is how a test drives one toggle without
touching the machine-local profile. Every toggle carries `kRequiresRestart`: what they gate is guest
behaviour decided at load, so the runtime records a change as pending until the next boot. An
implementing prompt that can apply its toggle live relaxes that lifecycle and updates this page.

## The boot line

`LogToggles()` runs once at boot, from `RbBlitzApp::OnPostLoadXexImage`, after the boot identity and
before the Ultimate and DLC layers:

```text
[info] enhancements: 10 toggle(s), off by default except R3 and R10 (docs/engine/toggles.md)
[info] enhancements: enhancements_expand_resolution = off (default) - R1 custom resolutions
...
[info] enhancements: enhancements_skip_offline_dialog = on (default) - R3 skip the offline prompts
[info] enhancements: enhancements_rename_mod_settings = on (default) - R10 rename the mod's settings row
[info] enhancements: enhancements_controller_scheme = "" (R8 scheme; empty means unnamed)
[info] enhancements: enhancements_hidden_menu_options = "splash_leaderboard,splash_achievements,splash_dlc" (R5 rows; the three offline-dead entries by default)
[info] enhancements: enhancements_menu_dlc_song_count = 3 (R9 pool size; spread over the loaded DLC library)
```

The value and the **source** are both stated (`default`, `config`, `environment`, `command line`,
`runtime`), because every toggle is off by design: without the source, "the toggle was never on" and
"the toggle did nothing" are the same log.

## The toggles

| Toggle (cvar = `enhancements_` + key) | Feature | Faithful (off) behaviour | Implemented by |
| --- | --- | --- | --- |
| `expand_resolution` | R1 custom resolutions: expand the guest's own layout (menu background, HUD, 3D, song selection, power-up menu) for a non-16:9 video mode | the guest's 16:9 layout is drawn as authored; the presenter's letterbox/safe-area path handles the window | R2, R3 (research R1) |
| `ui_scale` | R2 UI accessibility: scale guest text and the main-menu logo through the game's own font/layout metrics | the design's text and logo sizes | U2 |
| `skip_offline_dialog` | R3 skip the offline prompts: answer both of the title's failed-connect questions - "Cannot connect to Rock Central" and "Proceed in Offline Mode?" - as they arrive, so a start goes straight to the menu in offline mode | both questions are asked and the player answers them | [main-menu-flow.md](main-menu-flow.md) § "the offline prompts" |
| `icon_schemes` | R4 different icons: replaceable button glyphs and controller-layout art per scheme | the art the title ships | A1, A2 |
| `hide_menu_options` | R5 hiding categories: remove named entries from the main menu's option list (the list the guest navigates, not only the drawing). Needs `enhancements_hidden_menu_options` to name them; the compiled default is the three rows that only ever worked online | every shipped entry is present | U1 ([main-menu-flow.md](main-menu-flow.md)) |
| `native_mouse` | R6 different input waves: the engine's own pointer path instead of the synthetic pad | the pad the guest actually reads ([src/input/mouse_ui.cpp](../../src/input/mouse_ui.cpp)) | I2 → later |
| `dlc_cache` | R7 DLC cache: persist the flat DLC library's enumeration in the save folder and reuse it while a fingerprint proves the tree unchanged, and — because the host's walk of the library is the *smaller* half of what a large library costs a boot — drop the SDK's emulated per-package mount latency (`deferred_overlapped_delay_ms = 0`, patch 0011) for the run. The refresh is the main menu's last row, drawn as "Refresh Song Library" rather than the store label it ships with, and backed by `--refresh_dlc_cache`; the discovery screen says "Loading Song Cache" instead of "Discovering Downloadable Content" when the enumeration came out of the cache, and its progress bar is given the library's package count (the title is never told one). What the toggle cannot remove is the title's own enumeration, one item per presented frame ([dlc.md](../dlc.md) §4.3) | the library is walked and every package's header read once per boot, and every mount the title makes waits out the emulated 100 ms | [src/fs/dlc_cache.h](../../src/fs/dlc_cache.h) (D2), [dlc.md](../dlc.md) §4.1, [main-menu-flow.md](main-menu-flow.md) § "the refresh row" |
| `force_controller_scheme` | R8 force a predefined controller scheme on load (needs `enhancements_controller_scheme`, a string; empty means unnamed and leaves the saved layout alone) | the player's saved layout is used untouched | D3 |
| `menu_dlc_songs` | R9 change the main menu's songs for loaded DLC ones: the menu's background pool is the title's own three streamed tracks (the `shellmusic` block of `config/synth.dtb`), and with this on every track it plays is a loaded DLC song instead - picked at random from the library (as many as `enhancements_menu_dlc_song_count`, 3 by default) and streamed read-only from the package it came from, so the menu becomes a jukebox over the DLC. Needs a DLC library: without one the menu is unchanged | the title's own three tracks, chosen at random and looped | [menu-music.md](menu-music.md) ([src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp)) |
| `rename_mod_settings` | R10 rename the mod's settings row: the Ultimate mod's own screen is drawn as "Ultimate Settings" instead of the "Mod Settings" it ships with (needs the payload installed; the launcher hides the row without it) | the mod's own label | [main-menu-flow.md](main-menu-flow.md) § "the label" |

**R3 and R10 are the toggles that ship on.** R10's row belongs to the Ultimate mod, so a run without
that payload has neither the file nor the label and the toggle has nothing to do — the faithful path
and the feature are the same boot. Its switch in the launcher is shown only where the mod is
installed (`visible = "ultimate_installed"`), and turning it off is what passes
`--enhancements_rename_mod_settings=false`. R3 is on because the failure it handles is not a
preference: with no Rock Central to reach, the title's connect attempt can only fail, so the two
questions have one answer each and asking them is a dead end. It edits a file of the title's own,
which is why the launcher can also ship it on, and turning it off is what passes
`--enhancements_skip_offline_dialog=false` — the switch for the two questions, for anyone who wants
to see them again.

**R7 is also the honest boundary of a toggle.** It removes what the *host* pays for a large DLC
library — the walk of the tree and the emulated per-package mount latency — and what remains of a
warm boot is the title's own enumeration, which advances one item per presented frame and which no
cache can shorten ([dlc.md](../dlc.md) §4.3). The one lever on that is the launcher's own *V-Sync*
row (Graphics ▸ Window), deliberately left alone: R7 neither sets it nor recommends it, and the SDK
fault that once made a V-Sync-off boot unsafe to suggest is fixed separately
([bringup-log.md](../history/bringup-log.md) B-016).

`enhancements_controller_scheme` is the one non-boolean cvar here: R8's parameter, not a toggle of
its own. R5's `enhancements_hidden_menu_options` is the same shape and now exists, because U1 had to
decide what a "named entry" is: the name the title gives the row (`splash_leaderboard`,
`splash_achievements`, `splash_dlc`), which is what the menu's own file carries. R9's
`enhancements_menu_dlc_song_count` is the same shape: how many DLC songs the menu's pool draws from,
spread over the loaded library. R10 needs no key: what it renames is the mod's
own row, and the two labels it moves between are the module's constants
(`src/ui/menu_options.h`) rather than a user's list.

## Adding a toggle

1. Add the `REXCVAR_DEFINE_BOOL/STRING` to [src/enhancements.cpp](../../src/enhancements.cpp) with the
   faithful behaviour in the description and `.lifecycle(kRequiresRestart)`, and add its row to
   `kToggles` — a toggle missing from that table is a toggle nobody sees.
2. Add the row to the table above, naming the prompt that implements it.
3. State in the implementing prompt's change what the faithful path is and why the feature deviates
   from it ([backlog.md](../backlog.md) §6's rule, applied to a flag).
