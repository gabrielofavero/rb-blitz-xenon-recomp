# Enhancement toggles

The contract for the customization features in
[plans/customization-plan.md](../plans/customization-plan.md) (R1–R9, plus R10 below, which a
follow-up request added). **Every toggle is off by default, with one deliberate exception:** R10
ships on, because its subject is the Ultimate mod's own screen and it does nothing without that mod
installed. This page is the table the plan's contract 1 promises: what each toggle gates, what the
faithful (off) behaviour is, and which prompt implements it — or, for the eight that are still
planned, what will. R5 and R10 are implemented; their evidence is
[main-menu-flow.md](main-menu-flow.md).

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
[info] enhancements: 10 toggle(s), off by default except R10 (docs/engine/toggles.md)
[info] enhancements: enhancements_expand_resolution = off (default) - R1 custom resolutions
...
[info] enhancements: enhancements_rename_mod_settings = on (default) - R10 rename the mod's settings row
[info] enhancements: enhancements_controller_scheme = "" (R8 scheme; empty means unnamed)
[info] enhancements: enhancements_hidden_menu_options = "splash_leaderboard,splash_achievements,splash_dlc" (R5 rows; the three offline-dead entries by default)
```

The value and the **source** are both stated (`default`, `config`, `environment`, `command line`,
`runtime`), because every toggle is off by design: without the source, "the toggle was never on" and
"the toggle did nothing" are the same log.

## The toggles

| Toggle (cvar = `enhancements_` + key) | Feature | Faithful (off) behaviour | Implemented by |
| --- | --- | --- | --- |
| `expand_resolution` | R1 custom resolutions: expand the guest's own layout (menu background, HUD, 3D, song selection, power-up menu) for a non-16:9 video mode | the guest's 16:9 layout is drawn as authored; the presenter's letterbox/safe-area path handles the window | R2, R3 (research R1) |
| `ui_scale` | R2 UI accessibility: scale guest text and the main-menu logo through the game's own font/layout metrics | the design's text and logo sizes | U2 |
| `skip_offline_dialog` | R3 auto offline mode: answer "Proceed in Offline Mode?" for the player, enabling offline mode | the prompt is shown and the player chooses | I1 |
| `icon_schemes` | R4 different icons: replaceable button glyphs and controller-layout art per scheme | the art the title ships | A1, A2 |
| `hide_menu_options` | R5 hiding categories: remove named entries from the main menu's option list (the list the guest navigates, not only the drawing). Needs `enhancements_hidden_menu_options` to name them; the compiled default is the three rows that only ever worked online | every shipped entry is present | U1 ([main-menu-flow.md](main-menu-flow.md)) |
| `native_mouse` | R6 different input waves: the engine's own pointer path instead of the synthetic pad | the pad the guest actually reads ([src/input/mouse_ui.cpp](../../src/input/mouse_ui.cpp)) | I2 → later |
| `dlc_cache` | R7 DLC cache: persist the DLC enumeration host-side, with an explicit refresh | the guest's own scan, once per boot | D1, D2 |
| `force_controller_scheme` | R8 force a predefined controller scheme on load (needs `enhancements_controller_scheme`, a string; empty means unnamed and leaves the saved layout alone) | the player's saved layout is used untouched | D3 |
| `menu_dlc_songs` | R9 change the main menu's songs for loaded DLC ones | the songs the title puts there | C1 → later |
| `rename_mod_settings` | R10 rename the mod's settings row: the Ultimate mod's own screen is drawn as "Ultimate Settings" instead of the "Mod Settings" it ships with (needs the payload installed; the launcher hides the row without it) | the mod's own label | [main-menu-flow.md](main-menu-flow.md) § "the label" |

**R10 is the one toggle that ships on.** The row it renames belongs to the Ultimate mod, so a run
without that payload has neither the file nor the label and the toggle has nothing to do — the
faithful path and the feature are the same boot. Its switch in the launcher is shown only where the
mod is installed (`visible = "ultimate_installed"`), and turning it off is what passes
`--enhancements_rename_mod_settings=false`.

`enhancements_controller_scheme` is the one non-boolean cvar here: R8's parameter, not a toggle of
its own. R5's `enhancements_hidden_menu_options` is the same shape and now exists, because U1 had to
decide what a "named entry" is: the name the title gives the row (`splash_leaderboard`,
`splash_achievements`, `splash_dlc`), which is what the menu's own file carries. R9 grows its key the
same way when its prompt knows what a "DLC song" is. R10 needs no key: what it renames is the mod's
own row, and the two labels it moves between are the module's constants
(`src/ui/menu_options.h`) rather than a user's list.

## Adding a toggle

1. Add the `REXCVAR_DEFINE_BOOL/STRING` to [src/enhancements.cpp](../../src/enhancements.cpp) with the
   faithful behaviour in the description and `.lifecycle(kRequiresRestart)`, and add its row to
   `kToggles` — a toggle missing from that table is a toggle nobody sees.
2. Add the row to the table above, naming the prompt that implements it.
3. State in the implementing prompt's change what the faithful path is and why the feature deviates
   from it ([backlog.md](../backlog.md) §6's rule, applied to a flag).
