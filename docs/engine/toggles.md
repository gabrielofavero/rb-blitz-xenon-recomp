# Enhancement toggles

The contract for the customization features in
[plans/customization-plan.md](../plans/finished/2026-10-03-customization-plan.md) (R1–R9, plus R10 below, which a
follow-up request added). **Every toggle is off by default, with four deliberate exceptions:** R3
ships on, because the two questions it answers can no longer be answered on an install with no Rock
Central to reach, R10 ships on, because its subject is the Ultimate mod's own screen and it does
nothing without that mod installed, R6 ships on for native menu mouse support, and **R9 ships on
since 2026-10-14** — the menu's DLC background music became an in-game setting on that date
(`general.dlc_menu_songs`, "DLC Menu Songs" on Help & Options → Downloadable Content), and both its
row default and this cvar's default are `true`, so a boot that has chosen nothing keeps today's
behaviour. This page is the table the plan's contract 1 promises: what each
toggle gates, what the faithful (off) behaviour is, and which prompt implements it — or, for the
remaining planned ones, what will. R3, R5, R6, R7, R9 and R10 are implemented; R3, R5 and R10's
evidence is [main-menu-flow.md](main-menu.md), R7's is
[src/fs/dlc_cache.h](../../src/fs/dlc_cache.h) and [dlc.md](../guides/dlc.md) §4.1, and R9's is
[menu-music.md](main-menu.md).

R6 uses `mouse_ui_nav`, outside the `[enhancements]` table; the old `enhancements_native_mouse` placeholder was removed.

The other cvars live in [src/enhancements.cpp](../../src/enhancements.cpp); the names are asserted by the
boot log, not by this page.

## How to set one

The runtime reads `<exe name>.toml` beside the executable (`out\build\win-amd64-release\rb_blitz.toml`
on this machine — see [build-and-run.md](../guides/build-and-run.md)). Its own rule joins a nested table's
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
[info] enhancements: 10 toggle(s), off by default except R3, R6, R9 and R10 (docs/engine/toggles.md)
[info] enhancements: enhancements_expand_resolution = off (default) - R1 custom resolutions
...
[info] enhancements: enhancements_skip_offline_dialog = on (default) - R3 skip the offline prompts
[info] enhancements: enhancements_rename_mod_settings = on (default) - R10 rename the mod's settings row
[info] enhancements: enhancements_controller_scheme = "" (R8 scheme; empty means unnamed)
[info] enhancements: enhancements_hidden_menu_options = "splash_leaderboard,splash_achievements,splash_dlc" (R5 rows; the three offline-dead entries by default)
[info] enhancements: enhancements_menu_dlc_songs = on (default) - R9 main-menu DLC songs (the in-game DLC Menu Songs row)
[info] enhancements: enhancements_menu_dlc_song_count = 3 (R9 pool size; drawn at random from the loaded DLC library)
```

The value and the **source** are both stated (`default`, `config`, `environment`, `command line`,
`runtime`), because every toggle is off by design: without the source, "the toggle was never on" and
"the toggle did nothing" are the same log.

## The toggles

| Toggle (cvar = `enhancements_` + key) | Feature | Faithful (off) behaviour | Implemented by |
| --- | --- | --- | --- |
| `expand_resolution` | R1 custom resolutions: expand the guest's own layout (menu background, HUD, 3D, song selection, power-up menu) for a non-16:9 video mode | the guest's 16:9 layout is drawn as authored; the presenter's letterbox/safe-area path handles the window | R2, R3 (research R1) |
| `ui_scale` | R2 UI accessibility: scale guest text and the main-menu logo through the game's own font/layout metrics | the design's text and logo sizes | U2 |
| `skip_offline_dialog` | R3 skip the offline prompts: answer both of the title's failed-connect questions - "Cannot connect to Rock Central" and "Proceed in Offline Mode?" - as they arrive, so a start goes straight to the menu in offline mode | both questions are asked and the player answers them | [main-menu-flow.md](main-menu.md) § "the offline prompts" |
| `icon_schemes` | R4 different icons: replaceable button glyphs and controller-layout art per scheme | the art the title ships | A1, A2 |
| `hide_menu_options` | R5 hiding categories: remove named entries from the main menu's option list (the list the guest navigates, not only the drawing). Needs `enhancements_hidden_menu_options` to name them; the compiled default is the three rows that only ever worked online | every shipped entry is present | U1 ([main-menu-flow.md](main-menu.md)) |
| `mouse_ui_nav` (no prefix) | R6 native menu hover, A/B clicks and wheel navigation; hidden and inactive during gameplay | keyboard/controller navigation | [input-flow.md](input-flow.md) |
| `dlc_cache` | On by default: reuse an unchanged flat-library content list; fingerprint every boot. A real hit displays "Loading Cached Content List" in the English locale. Cold, invalid and refresh boots scan and report discovery. Refresh remains asynchronous. See [dlc.md](../guides/dlc.md) §4.4 for current measurements and the separate guest songcache. | Scan headers every boot without reading or writing the host cache; structured loading, progress, zero default mount delay and the independent pacing option still apply. | [dlc_cached_library.h](../../src/fs/dlc_cached_library.h), [dlc.cpp](../../src/hooks/dlc.cpp), [menu_payload.cpp](../../src/settings/menu_payload.cpp) |
| `dlc_uncap_enumeration` | On by default: temporarily free V-Sync throughout content discovery, including post-enumeration finalization; request restoration of the player's original value at the verified usable main menu. Independent of host-cache On/Off. Per-enumerator progress never claims overall completion; historical last-item/idle restoration is superseded by P6. | Keep the player's frame-pacing setting throughout loading; truthful progress remains active. | [content_progress.cpp](../../src/hooks/content_progress.cpp), [content_progress.h](../../src/ui/content_progress.h), [native_menu.cpp](../../src/input/native_menu.cpp), [dlc.md](../guides/dlc.md) §4.4 |
| `force_controller_scheme` | R8 force a predefined controller scheme on load (needs `enhancements_controller_scheme`, a string; empty means unnamed and leaves the saved layout alone) | the player's saved layout is used untouched | D3 |
| `menu_dlc_songs` | R9 change the main menu's songs for loaded DLC ones — **the carrier of an in-game setting since 2026-10-14**: the visible row is Help & Options → Downloadable Content → "DLC Menu Songs" (`general.dlc_menu_songs`), and the boot writes that row into this cvar at the config rank ([src/settings/cvars.cpp](../../src/settings/cvars.cpp), [src/settings/boot.cpp](../../src/settings/boot.cpp)), so a flag or environment value still outranks it. The menu's background pool is the title's own three streamed tracks (the `shellmusic` block of `config/synth.dtb`), and with this on every track it plays is a loaded DLC song instead - a pool of at most **50** songs drawn at random from the whole library every boot, where a mounted package contributes **every** `.mogg` it holds rather than only its first (a merged pack is one container with a disc's worth of songs in it, and taking only the first pinned the menu to the same track every boot; the old `enhancements_menu_dlc_song_count` size row is retired and ignored). Streamed read-only from the package it came from and walked as a shuffled deck, so one song follows the last until the deck is exhausted and reshuffled. The follow-up feature **Play Current Song** (2026-10-14) rides the same switch: with it on and a pool, the main menu gains a second entry that starts the song the menu is playing - the power-up screen with Ultimate, the loading screen on the offline base target ([main-menu.md](main-menu.md) part 1 §7.2) - and with the switch off the entry is not built and its settings row reads unavailable. Needs a DLC library: without one the menu is unchanged | the title's own three tracks, one chosen at random per menu visit and looped | [menu-music.md](main-menu.md) part 2 ([src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp), [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp)) |
| `rename_mod_settings` | R10 rename the mod's settings row: the Ultimate mod's own screen is drawn as "Ultimate Settings" instead of the "Mod Settings" it ships with (needs the payload installed; Controller Settings hides the row without it) | the mod's own label | [main-menu-flow.md](main-menu.md) § "the label" |
| `settings_help_overlay` (no prefix) | **the help captions, and the carrier of an in-game setting since 2026-10-14**: a one-or-two-line description of the highlighted settings row, drawn as a host overlay over the presented frame while Help & Options (or a page it routes into) is open. The visible row is Help & Options → Interface → "Settings Labels" (`interface.settings_labels`, default On), which the boot writes into this cvar at the config rank and the P9 live applier writes when the row is committed, so flipping it hides or shows the caption **on the next draw, with no restart prompt** ([docs/settings/status.md](../settings/status.md) § "Row descriptions"). Off is invisible rather than "drawn and empty": the dialog returns before `Begin()`, so nothing is drawn at all | no caption is drawn; every row's description is still authored and still published over the settings channel | [src/ui/help_overlay.cpp](../../src/ui/help_overlay.cpp) (the drawing and the font), [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp) (the texts and the guest-side publishing), [src/hooks/help_observer.cpp](../../src/hooks/help_observer.cpp) (the retail pages), [docs/settings/status.md](../settings/status.md) § "Row descriptions" |

**R3, R6, R9 and R10 are the toggles that ship on**, and so does the caption overlay's own
`settings_help_overlay`: it is a display aid rather than a deviation from the title, its effect is
purely additive to what is on screen, and it now has a visible row which is the honest control for
it (Help & Options → Interface → Settings Labels). R10's row belongs to the Ultimate mod, so a run without
that payload has neither the file nor the label and the toggle has nothing to do — the faithful path
and the feature are the same boot. Its switch in Controller Settings is shown only where the mod is
installed (`visible = "ultimate_installed"`), and turning it off is what passes
`--enhancements_rename_mod_settings=false`. R9 ships on because its feature is no longer a hidden
toggle but a setting with a row of its own: the menu's DLC music has been on since 2026-10-13, the
row was added on 2026-10-14 with both defaults `true`, and the honest control is that row (Help &
Options → Downloadable Content); the cvar stays as its carrier so a command line or environment value
can still outrank it. R3 is on because the failure it handles is not a
preference: with no Rock Central to reach, the title's connect attempt can only fail, so the two
questions have one answer each and asking them is a dead end. It edits a file of the title's own,
which is why Controller Settings can also ship it on, and turning it off is what passes
`--enhancements_skip_offline_dialog=false` — the switch for the two questions, for anyone who wants
to see them again.

**R7 is also the honest boundary of a toggle.** It removes what the *host* pays for a large DLC
library — repeated package-header reads (a fingerprint walk remains). Mount latency is now zero
independently of that cache setting. What remains of a
warm boot is the title's own enumeration, which advances one item per presented frame and which
neither a cache nor a host-side batching trick shortens: the guest's own 512-byte reply buffer takes
one item per call however many the enumerator offers
([dlc.md](../guides/dlc.md) §4.3, [song-cache-plan.md](../plans/finished/2026-10-10-song-cache-plan.md) §13). The one lever on
that is the frame rate, and the *V-Sync* row (Graphics ▸ Window) is the player's: R7 sets no value for
it at boot and recommends none. What R7 offers instead is `dlc_uncap_enumeration`, which moves it for
the loading phase and requests restoration when the usable main menu is observed
([song-cache-plan.md](../plans/finished/2026-10-10-song-cache-plan.md) §14); the SDK fault that once made a V-Sync-off
boot unsafe to suggest is fixed separately ([bringup-log.md](../history/bringup-log.md) B-016).

`enhancements_controller_scheme` is the one non-boolean cvar here: R8's parameter, not a toggle of
its own. R5's `enhancements_hidden_menu_options` is the same shape and now exists, because U1 had to
decide what a "named entry" is: the name the title gives the row (`splash_leaderboard`,
`splash_achievements`, `splash_dlc`), which is what the menu's own file carries. R9's
owned `enhancements_menu_dlc_song_count` **is retired** (2026-10-13): the pool is a fixed 50 songs,
capped by what a boot can mount, and the cvar is read only so a boot can say the legacy value is
ignored ([src/settings/menu_compat.h](../../src/settings/menu_compat.h)).
R10 needs no key: what it renames is the mod's
own row, and the two labels it moves between are the module's constants
(`src/ui/menu_options.h`) rather than a user's list.

## Adding a toggle

1. Add the `REXCVAR_DEFINE_BOOL/STRING` to [src/enhancements.cpp](../../src/enhancements.cpp) with the
   faithful behaviour in the description and `.lifecycle(kRequiresRestart)`, and add its row to
   `kToggles` — a toggle missing from that table is a toggle nobody sees.
2. Add the row to the table above, naming the prompt that implements it.
3. State in the implementing prompt's change what the faithful path is and why the feature deviates
   from it ([backlog.md](../reference/backlog.md) §6's rule, applied to a flag).
