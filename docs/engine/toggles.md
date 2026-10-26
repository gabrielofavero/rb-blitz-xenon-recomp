# Enhancement toggles

`song_load_crash_recovery` (Runtime, default **true**, restart-scoped) enables
the Windows external monitor that restarts a fatal process exit during an active
song load, returning through normal discovery to the native main-menu crash
dialog. Off disables fatal-exit recovery; the independent optional-MIDI-track fix remains
active. `song_load_recovered` is an internal one-shot reason; development-only
`song_load_test_crash` defaults false and is removed from replacement arguments.
Recovery automatically starts discovery with the local user and skips offline
prompts for that boot. It preserves the DLC Menu Songs setting and its launch
overrides, so enabled background songs still prepare through the normal path.
See [Load Guard part 4](../plans/finished/2026-10-13-load-guard.md),
[crash_recovery.cpp](../../src/loading/crash_recovery.cpp) and
[song_load_monitor.cpp](../../src/platform/song_load_monitor.cpp).

**Load Guard** (General → Downloadable Content) adds the separate
`general.load_guard` / `song_load_guard` enum: **Off / 30s / 60s**, default Off.
A successful settings commit applies to the next song attempt without restarting.
At expiry the external monitor ends the unfinished process, waits for exit and
fast-boots to the main menu with a timeout-specific native dialog. Completion,
manual departure and window close retire the deadline; waiting for A after load
completion does not count. System sleep/hibernate is excluded, focus loss is not.
Intentional debugger/process suspension needs Off. The helper starts even with
both recovery controls Off so a later live timeout change works.
`song_load_timeout` is the consumed internal 30/60-second reason;
`song_load_test_stall` is a default-false, one-attempt diagnostic removed on
recovery. See [part 5 of the evidence](../plans/finished/2026-10-13-load-guard.md).

The contract for the customization features in
[plans/customization-plan.md](../plans/finished/2026-10-03-customization-plan.md) (R1–R9, plus R10 below, which a
follow-up request added). **The original default-off policy has deliberate exceptions:** R3
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
| `hide_achievements` | Hide achievement unlock notifications. Help & Options → General → Achievements & Unlocks → Hide Achievements (`general.hide_achievements`, default Off). Available for all editions; changes use the restart prompt because the SDK creates the notification dialog once per launch. | Display the SDK's achievement unlock toast. Earning and saving achievements works in either state. | [src/rb_blitz_app.h](../../src/rb_blitz_app.h), [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp) |
| `expand_resolution` | R1 custom resolutions: expand the guest's own layout (menu background, HUD, 3D, song selection, power-up menu) for a non-16:9 video mode | the guest's 16:9 layout is drawn as authored; the presenter's letterbox/safe-area path handles the window | R2, R3 (research R1) |
| `ui_scale` | R2 UI accessibility: scale guest text and the main-menu logo through the game's own font/layout metrics | the design's text and logo sizes | U2 |
| `skip_offline_dialog` | R3 skip the offline prompts: answer both of the title's failed-connect questions - "Cannot connect to Rock Central" and "Proceed in Offline Mode?" - as they arrive, so a start goes straight to the menu in offline mode | both questions are asked and the player answers them | [main-menu-flow.md](main-menu.md) § "the offline prompts" |
| `icon_schemes` | R4 different icons: replaceable button glyphs and controller-layout art per scheme | the art the title ships | A1, A2 |
| `hide_menu_options` | R5 hiding categories: remove named entries from the main menu's option list (the list the guest navigates, not only the drawing). Needs `enhancements_hidden_menu_options` to name them; the compiled default is the three rows that only ever worked online | every shipped entry is present | U1 ([main-menu-flow.md](main-menu.md)) |
| `mouse_ui_nav` (no prefix) | R6 native menu hover, A/B clicks and wheel navigation. In a song the pointer and the wheel are off (the cursor stays hidden), and a `[remap]` or Keyboard + Mouse table `mouse:` source reads the raw button state instead (P2/P3) - a bound mouse button acts on the song, a menu click never does. Off blocks every mouse path: the pointer and all mouse bindings. Since P7 it has an in-game row (Controls -> Input Types -> Mouse) and applies to the next poll. | keyboard/controller navigation | [input-flow.md](input-flow.md) |
| `dlc_cache` | On by default: reuse an unchanged flat-library content list; fingerprint every boot. A real hit displays "Loading Cached Content List" in the English locale. Cold, invalid and refresh boots scan and report discovery. Since 2026-10-15 the refresh row also re-runs the title's own discovery pass on a press - the connect screen, its bar and the mounting, exactly as a first boot's start - and the press waits (bounded) for the re-scan so the pass enumerates the fresh list ([main-menu.md](main-menu.md) §7.1, [dlc.md](../guides/dlc.md) §4.2). The row's own on/off is the `refresh_music_library` row below. See [dlc.md](../guides/dlc.md) §4.4 for current measurements and the separate guest songcache. | Scan headers every boot without reading or writing the host cache; structured loading, progress, zero default mount delay and the independent pacing option still apply. | [dlc_cached_library.h](../../src/fs/dlc_cached_library.h), [dlc.cpp](../../src/hooks/dlc.cpp), [menu_payload.cpp](../../src/settings/menu_payload.cpp) |
| `refresh_music_library` | **On by default since 2026-10-22** (off 2026-10-20 to 2026-10-22, on when it first shipped 2026-10-15; owner request): the main menu's refresh entry has a switch of its own - the visible row is Help & Options → Downloadable Content → "Refresh Songs" (`general.refresh_music_library`, renamed from "Refresh Music Library" on 2026-10-22; the setting id and cvar name are unchanged so existing profiles keep working), which the boot writes into this cvar at the config rank ([src/settings/cvars.cpp](../../src/settings/cvars.cpp), [src/settings/boot.cpp](../../src/settings/boot.cpp)). On shows the row (`menu_filter`'s hide decision, master and selection unaffected), and the payload repaints it. Needs `dlc_cache`: with the cache off the row is the title's own online entry and this switch says nothing about it. The retail **Download Content** entry is restored separately as `splash_dlc_store`, behind `interface.hide_download_content` (default Hidden; its own label is `Download content` since 2026-10-23). With this switch off, the Interface → Main Menu mirror no longer offers the row either, because the mirror lists the menu's actual rows (2026-10-23). | the row is hidden; the library is still re-scanned at boot whenever its fingerprint changed, and `--refresh_dlc_cache` still forces one | [src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp), [src/settings/menu_compat.cpp](../../src/settings/menu_compat.cpp) (`DecideHiddenRows`), [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp) |
| `dlc_uncap_enumeration` | On by default: temporarily free V-Sync throughout content discovery, including post-enumeration finalization; request restoration of the player's original value at the verified usable main menu. Independent of host-cache On/Off. The title's own discovery bar follows the actual aggregate enumerators, host-pushed per whole percent (2026-10-15); the historical last-item/idle restoration is superseded by P6 and the overlay by the native bar. | Keep the player's frame-pacing setting throughout loading; the title's own bar remains the title's own. | [content_progress.cpp](../../src/hooks/content_progress.cpp), [content_progress.h](../../src/ui/content_progress.h), [native_menu.cpp](../../src/input/native_menu.cpp), [dlc.md](../guides/dlc.md) §4.4 |
| `force_controller_scheme` | R8 force a predefined controller scheme on load (needs `enhancements_controller_scheme`, a string; empty means unnamed and leaves the saved layout alone) | the player's saved layout is used untouched | D3 |
| `menu_dlc_songs` | R9 change the main menu's songs for loaded DLC ones — **the carrier of an in-game setting since 2026-10-14**: the visible row is Help & Options → Downloadable Content → "DLC Menu Songs" (`general.dlc_menu_songs`), and the boot writes that row into this cvar at the config rank ([src/settings/cvars.cpp](../../src/settings/cvars.cpp), [src/settings/boot.cpp](../../src/settings/boot.cpp)), so a flag or environment value still outranks it. The menu's background pool is the title's own three streamed tracks (the `shellmusic` block of `config/synth.dtb`), and with this on every track it plays is a loaded DLC song instead - a rolling session drawn from the whole library, with three upcoming songs and two recent songs kept warm and an eight-package residency ceiling; a discovered package contributes every eligible `.mogg` to a lightweight catalog rather than only its first (a merged pack is one container with a disc's worth of songs in it, and taking only the first pinned the menu to the same track every boot; the old `enhancements_menu_dlc_song_count` size row is retired and ignored). Streamed read-only from the package it came from and walked as a **rotation**: every song of the pool is heard once before any of them is heard again, no song ever follows itself — including across a reshuffle, a menu re-entry or a pool rebuilt by a library refresh, because the order is remembered by song *identity* rather than by position — and a pool that can only offer the song that just played (a one-song library) plays it once and then hands the menu back to the title's own tracks — one of the three, played with the title's own loop, as the title plays its music — instead of repeating the DLC song. A song whose load fails sits out the pass it failed in, is tried again in the next, and is retired after repeated failures. The follow-up feature **Play Current Song** (2026-10-14) rides the same switch: with it on and a pool, the main menu gains a second entry that starts the song the menu is playing - the power-up screen with Ultimate, the loading screen on the offline base target ([main-menu.md](main-menu.md) part 1 §7.2) - and with the switch off the entry is not built and its settings row reads unavailable. Needs a DLC library: without one the menu is unchanged | the title's own three tracks, one chosen at random per menu visit and looped | [menu-music.md](main-menu.md) part 2 ([src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp), [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp)) |
| `rename_mod_settings` | R10 rename the mod's settings row: the Ultimate mod's own screen is drawn as "Ultimate Settings" instead of the "Mod Settings" it ships with (needs the payload installed; the in-game menu hides the row without it) | the mod's own label | [main-menu-flow.md](main-menu.md) § "the label" |
| `background_music_nav` | Default **On**; the carrier for General → Interface → **Menu Music Navigation** (`interface.background_music_navigation`, id 53, restart). Navigator **P1 (2026-10-26)** builds the always-visible **Background Music Play** command page and SDK-free navigator/snapshot core. Commands, input interception, footer and player are **not connected yet** (P2/P3). Independent of DLC Menu Songs and Play Current Song. | Switch and command submenu/page remain visible, including with an explicit off flag; runtime commands will be gated off in P2. Existing playback unchanged in P1. | [settings_screen.cpp](../../src/ui/settings_screen.cpp), [menu_music_nav.h](../../src/input/menu_music_nav.h), [menu_music_now_playing.h](../../src/hooks/menu_music_now_playing.h), [navigator plan](../plans/in-progress/2026-10-22-background-music-navigator-plan.md) §11 |
| `settings_help_overlay` (no prefix) | **the help captions, and the carrier of an in-game setting since 2026-10-14**: a one-or-two-line description of the highlighted settings row, drawn as a host overlay over the presented frame while Help & Options (or a page it routes into) is open. The visible row is Help & Options → Interface → "Settings Labels" (`interface.settings_labels`, default On), which the boot writes into this cvar at the config rank and the P9 live applier writes when the row is committed, so flipping it hides or shows the caption **on the next draw, with no restart prompt** ([docs/settings/status.md](../settings/status.md) § "Row descriptions"). Off is invisible rather than "drawn and empty": the dialog returns before `Begin()`, so nothing is drawn at all | no caption is drawn; every row's description is still authored and still published over the settings channel | [src/ui/help_overlay.cpp](../../src/ui/help_overlay.cpp) (the drawing and the font), [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp) (the texts and the guest-side publishing), [src/hooks/help_observer.cpp](../../src/hooks/help_observer.cpp) (the retail pages), [docs/settings/status.md](../settings/status.md) § "Row descriptions" |

**R3, R6, R7's cache (and, since 2026-10-22, its `refresh_music_library` entry), R9 and R10 are the
toggles that ship on**. The independent `enhancements_background_music_nav` also defaults on
since navigator P1 (2026-10-26), which currently provides its setting/command-page surface only;
playback wiring and the player are still pending. The caption overlay's own
`settings_help_overlay` also ships on: it is a display aid rather than a deviation from the title, its effect is
purely additive to what is on screen, and it now has a visible row which is the honest control for
it (Help & Options → Interface → Settings Labels). R10's row belongs to the Ultimate mod, so a run without
that payload has neither the file nor the label and the toggle has nothing to do — the faithful path
and the feature are the same boot. Its switch in the in-game menu is shown only where the mod is
installed (`visible = "ultimate_installed"`), and turning it off is what passes
`--enhancements_rename_mod_settings=false`. R9 ships on because its feature is no longer a hidden
toggle but a setting with a row of its own: the menu's DLC music has been on since 2026-10-13, the
row was added on 2026-10-14 with both defaults `true`, and the honest control is that row (Help &
Options → Downloadable Content); the cvar stays as its carrier so a command line or environment value
can still outrank it. R3 is on because the failure it handles is not a
preference: with no Rock Central to reach, the title's connect attempt can only fail, so the two
questions have one answer each and asking them is a dead end. It edits a file of the title's own,
which is why the in-game row can also ship it on, and turning it off is what passes
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
owned `enhancements_menu_dlc_song_count` **is retired** (2026-10-13): the 2026-10-26 rolling session has no fixed 50-song cap, and the cvar is read only so a boot can say the legacy value is
ignored ([src/settings/menu_compat.h](../../src/settings/menu_compat.h)).
R10 needs no key: what it renames is the mod's
own row, and the two labels it moves between are the module's constants
(`src/ui/menu_options.h`) rather than a user's list.

## Graphics cvars (not enhancement toggles)

The in-song HUD re-anchor of [graphics.md](../settings/graphics.md) part 3 §9 lives in
[src/hooks/hud_layout_runtime.cpp](../../src/hooks/hud_layout_runtime.cpp), not `src/enhancements.cpp`.
Its five cvars (including the retired scan flag) are registered here:

| Cvar | Default | What it does |
| --- | --- | --- |
| `hud_reanchor` | on | Re-anchors the six measured HUD anchors during the guest's WorldXfm evaluation, with pre-draw resolution for clean cached anchors (including directly submitted comparison groups via ownership/transform ancestry, graphics.md §9.2.8) and change-triggered descendant invalidation. Independent of element visibility; no present pass, scene search, guest heap queries or retained object cache. 16:9 returns before inspecting metadata. See graphics.md part 3 §9.2.7; earlier capture timings belong to §9.2.6's superseded implementation. |
| `hud_reanchor_apply` | on | Places `band_meter_hud`, `letter_collect_hud` and `remedial_help.grp` on the left, `score.lbl`, `star_board` and `dynamic_leaderboard` on the right. Their children inherit the shift; named children compensate for an already shifted ancestor. Authored local transforms stay intact. The six sides/offsets were measured in graphics.md §9.2.5; the WorldXfm implementation's gameplay validation is pending (§9.2.7). |
| `hud_reanchor_hud_half_height` | 240 | The HUD scene's half-height in its own units, measured 2026-10-17: the left block authors `x = -298.537` and sits 335.9 px left of centre on a 540-line frame, i.e. 1.125 px per unit, so 270 px (half of 540) is 240 units. 2560x1080 shifts by 142.2 units = 160 guest px = 320 px on a 1080p frame. The 180 it carried before was derived from the payload anchors and was wrong. |
| `hud_reanchor_probe` | off | Logs a bounded sample (eight placements per anchor per thread) of original/placed world x during WorldXfm. No container inventory or present timer remains. |
| `hud_reanchor_scan` | off | Retired 2026-10-24: accepted for configuration/command-line compatibility, performs no work. The heap sweep and its page cache were removed with the present-pass finder (graphics.md §9.2.7). Historical sweep costs remain in §9.2.6. |

## Input switches (gameplay mapping)

Three cvars registered by `src/input/remap.cpp` for
[the gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md). They are
declared and stored as of P1 (2026-10-18); P2 made the two device gates real, P3 (2026-10-18)
built the direct dispatch, so a gameplay table's switch now selects the routing its name
describes (`kGameplayDispatchReady` in `src/input/remap.cpp` is `true`), and P7 (2026-10-18) gave
all three an in-game row in Help & Options -> Controls, where an edit is saved through the profile
and applied on the next poll.

| Cvar | Default | Reads it today | Notes |
| --- | --- | --- | --- |
| `keyboard_support` | on | **P2's source gates** ([input-flow.md](input-flow.md) § Source gates): off blocks every keyboard-to-game path - General Mapping's `key:` sources, both gameplay tables' `key:` sources, and the SDK's legacy keyboard/mouse emulation driver. Since P7 it is the in-game **Controls -> Input Types -> Keyboard** row and applies live. | A device gate: the in-game row can still re-enable it (and so can the recovery path below). Its one exception is the recovery path (P8a): a profile with **both** host gates off and no pad leaves no menu route at all, so one 2 s hold of F10 reopens this switch through the same store the row writes (plan §18; `input recovery:` in the log). A pad once you plug one in, and one held key on a pad-less machine, are the two ways back. |
| `gameplay_mapping_controller` | **retired 2026-10-20** | Was P3's controller dispatch selector. **Removed** by P2 of [the interface/gameplay mapping restructure plan](../plans/finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md) (D3/D15): the Controller table acts exactly when it has a custom row, and the native preset drives the pad otherwise (D4). An old profile's row is preserved untouched and decides nothing. | — |
| `gameplay_mapping_keyboard_mouse` | **retired 2026-10-20** | Same change: the Mouse & Keyboard table is always active in a song, so the switch stopped selecting anything. Preserved in an old profile, never applied (the allowlist no longer names it). | — |

All three are written by the in-game Controls menu through the same profile the game reads
(the allowlist in `docs/settings/contracts.md` §8), so a standalone boot reads them from that file.
The in-game rows apply
live: `InputStore::Commit` saves the profile, writes the cvar on the host thread, reads it back
and publishes a new `EffectiveInput`, and the state filter builds its gates from that snapshot on
the next poll (plan §16.2). The Controller switch defaults off because its
physical-pad arm is unmeasured on this machine; the Keyboard + Mouse switch ships on because its
dispatch is measured live (plan §13.3). `kGameplayDispatchReady` is the one place a build can hold
both switches' routing off again without deleting the code path.

## Music-library and store headers

**Current (2026-10-23, independent switches):** General ▸ Interface ▸
Music Library & Store has two native booleans, both restart-scoped. The owner's
final defaults (2026-10-23) are **Messages On / Recommendations Off**:

| Row | Settings key | Stable id |
| --- | --- | --- |
| Show Messages | `interface.show_messages` | 87 |
| Show Recommendations | `interface.show_recommendations` | 88 |

The retired combined id 86 is never reused. Its old key seeds an absent new
key, while explicit new keys win; loading does not rewrite the profile.
`src/settings/store.cpp` uses the same fallback for optimistic conflict checks.

`src/settings/menu_payload.cpp` authors a single tab ring: Your Songs →
RhythmVerse → Messages → Recommended. Every trigger case skips disabled tabs,
retaining the retail preview cleanup and store highlight saves. The shared
background handler chooses the full header Flow whenever any optional tab is
enabled, independently hides each disabled label, and hides the badge only
when Messages is Off. Both arrows disappear only when all three optional tabs
are Off. This also hides RhythmVerse while its master is Off. The eight-state
matrix is checked on synthetic and extracted records.

**Animation/layout correction (2026-10-23):** the owner's split-control tests
showed RT from Songs to Messages swiping left with the store Off, and hidden
tabs leaving holes. Header-only transition wrappers now carry the pressed
direction; ordinary transitions reset that override before entering a cached
transition object and retain retail's index-based fallback. Background entry
rewrites the enabled labels' `TextShift.anim` endpoint X keys into consecutive
retail positions in cyclic order, including the new-message badge; unused space
stays at the right. All four tabs enabled preserves the original position keys.
`menu_payload` and the eight-state extracted-record check pass (1,550 checks),
and the release build succeeds. The owner manually confirmed the corrected swipes
and compact layout work.
See RhythmVerse plan P9-4 for record paths, native handler and evidence.
**Search follow-up (2026-10-23):** a subsequent owner screenshot shows a disabled
Recommended caption overlapping compacted Your Songs around Search. The shared
background now reapplies the disabled-label mask on entry and poll, covering the
browser entry that skips the front menu's header setup. Poll only hides disabled
labels; it does not restart animations. All eight configurations pass the updated
host check (1,982 checks); visual confirmation remains pending.

**Earlier combined implementation (superseded by the split above):**

RhythmVerse owner pass **P9-4** (2026-10-22) adds a native settings boolean rather
than a cvar. **General ▸ Interface ▸ Music Library & Store ▸ Show Headers** stores
`interface.show_messages_recommendations` (id 86), default **On**, and requires
a restart. On preserves retail's shared `background_panel` record. Off appends
`set_showing FALSE` for `main_Home.lbl` (RECOMMENDED), `main_inbox.lbl` (MESSAGES)
and `main_new_msg.lbl` after `set_offline_mode_headers`, and suppresses the badge
after `update_inbox_header`. Both the store and Your Songs call that same panel.
The boot reads the effective setting in `src/hooks/settings_payload.cpp`;
`src/settings/menu_payload.cpp` edits `ui/background/gen/background.dtb`.
**2026-10-23 correction:** hiding uses direct scene-object commands, matching
retail's badge handler; the initial nested `$this find` form is retired after
the owner reported an Off crash. An in-game retest remains pending. The song
list's trigger cases now bypass their offline gate. With headers Off, both
triggers go to the enabled store, and the store menu/browser's RT skips Inbox
for Your Songs. With the store master Off and headers On, song-list RT goes
to Inbox; with both Off, triggers only pulse their arrows. Header positions
and the other labels retain their retail behavior. The payload tests cover
all four switch combinations on synthetic and extracted retail records.

**Header presentation correction (2026-10-23):** the owner's screenshots showed
Your Songs retained dim tabs and no LT/RT. Its screen-entry call still chose the
offline header Flow based on sign-in, independently of the button handlers.
`RouteSongSelectHeaders` now sends FALSE to `set_offline_mode_headers` whenever
Show Headers or the store master is On (the same value the store sends), and TRUE
only when both switches are Off. The tests assert this argument in both synthetic
and generated retail records. The visual change awaits the owner's retest.

## Adding a toggle

1. Add the `REXCVAR_DEFINE_BOOL/STRING` to [src/enhancements.cpp](../../src/enhancements.cpp) with the
   faithful behaviour in the description and `.lifecycle(kRequiresRestart)`, and add its row to
   `kToggles` — a toggle missing from that table is a toggle nobody sees.
2. Add the row to the table above, naming the prompt that implements it.
3. State in the implementing prompt's change what the faithful path is and why the feature deviates
   from it ([backlog.md](../reference/backlog.md) §6's rule, applied to a flag).
