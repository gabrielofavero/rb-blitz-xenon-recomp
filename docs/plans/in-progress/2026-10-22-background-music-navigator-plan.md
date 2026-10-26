# Background music navigator — implementation plan

**Status:** navigator planning (2026-10-22), scope revised 2026-10-26. The session/mount-cache
architecture was approved and implemented separately on 2026-10-26; the navigator controls, the
setting, the pause seam, the media-key mapping and the footer remain unimplemented. This session
delivers only that architecture foundation, not P1–P3 of this navigator feature. Owner request of
2026-10-22, normalized in §1; the design is §4, the three execution prompts are §5.

**Scope revision (2026-10-26, owner).** The two controls the feature cannot ship without are in
scope, and the plan is corrected to match the tree: the **on/off setting** moves from
General → Interface → Main Menu to **General → Interface** (D1 — the Main Menu page is the live
mirror of the menu's own entries and its rows are replaced at boot, so a behaviour switch cannot
live there), and the **Interface Mapping row** is pinned to the page's fifth row plus a three-row
page of one-shot commands (R5). The media-key SDK patch takes the next free number, **0041**
(0020 is occupied — §10).

## 1. Request (normalized)

| Id | Requirement |
| --- | --- |
| R1 | An **independent** feature: navigate the music the main menu is playing with controller buttons and keyboard media keys. It must not require *DLC Menu Songs* (R9) or *Play Current Song*, but it must cooperate with both. |
| R2 | One setting, row label **Menu Music Navigation** (`interface.background_music_navigation`, id 53, default **on**), on **General → Interface** — the page of behaviour switches, directly after *Help & Options Labels*. It is the feature's only switch: off means no pad capture, no media keys, no footer and no **Background Music Play** page (R5, R7). **Not** on General → Interface → Main Menu: that page lists the menu's own hideable entries, its static rows are replaced by the live mirror built from `g_configuration.menu_rows`, and its documented rule is one visibility row per entry with no master switch. |
| R3 | Controller: **RB** (pad `0x0200`) / **MediaTrackNext** → next song. **LB** (pad `0x0100`) / **MediaTrackPrevious** → restart the current song when it has played ≥ 5 s, otherwise go to the previous song; on the first song of the queue, restart. **Start** (pad `0x0010`) / **MediaPlayPause** → pause/resume. |
| R4 | The main-menu footer, today guest-drawn as `A Select / B Back`, must read `LB Previous | A Select | B Back | RB Next`. |
| R5 | With the setting **on**, Controls → **Interface Mapping** gains a **Background Music Play** submenu as the page's fifth row (between *Music Library Options* and *Reset All*, so Reset All stays last; the page goes 5 → 6 rows, which is the visible budget), opening a three-row page: **Next Track / Previous Track / Play/Pause**. The three are one-shot **commands** — a press fires `rbbz-play/next|previous|pause` through the guest's `file_exists` channel — not `[remap]` bindings: that table's targets are physical controls, while these are feature actions, so the player triggers them here but does not assign them. With the setting **off** both the row and the page are absent. |
| R6 | When the setting is on, a navigation must keep **Play Current Song** pointing at the song that is now audible, so pressing it starts exactly what is playing in the background. |
| R7 | With the setting off, behaviour is exactly today's: no interception, no footer prompts, no Controls row. |
| R8 | Scope of the buttons: the **main menu only** (the owner's footer). The three Controls-page actions work wherever the menu music is playing (they are the same commands) while the setting is on; with it off the page does not exist. |
| R9 | Docs are part of the change (toggles, menu study, input-flow, settings plan/status, plan index). |

## 2. What exists today

- **The menu music layer** — `src/hooks/shell_music.{h,cpp}`. *Superseded 2026-10-26 (§10): R9
  no longer mounts "up to 50 DLC songs".* Today `Configure` (host thread) mounts **one** randomly
  chosen usable package and starts the demand worker; `SubstituteFilename` (Load hook
  `sub_82757F50`) replaces the panel's `sfx/shell/...` request; the menu's own per-frame poll
  (`sub_827586D8`, **guest thread**) hands the next track over via `LoadOnGuest` + `StartNextTrack`.
  Playback state is `shell_music::Playback` (`src/hooks/menu_music_playback.h`); the audible
  position is the stream's own sample counter (`Stream+0xA0`, ~44,100 counts/s).
- **The playlist and its mounts** — *superseded 2026-10-26 (§10): the fixed pool and its separate
  `Rotation` order are gone.* `shell_music::MusicSession` (`src/hooks/menu_music_session.h`) keeps a
  stable catalog of song IDs, one shared playlist and cursor across passes (`NextPosition`,
  `Select`, `PreviousPosition(samples)`, `Heard`/`Failed`/`Skipped`); `MusicMountCache`
  (`menu_music_cache.h`) holds at most `kResidentLimit` (8) packages and retires a cold one only
  when no native File is open; `MenuMusicQueue` (`menu_music_queue.h`) is the bounded demand
  worker. There is no pause and no seek.
- **The title's own tracks** — `kTitleTracks[3]` (`sfx/shell/samples/shellmusic_1..3`) are the
  fallback when R9 has no pool. Nothing can navigate them today: R9 off leaves the Load hook inert.
- **Play Current Song** — `AnswerPlayQuery` (`shell_music.cpp`) answers the main menu's
  `rbbz-play/*` probes through the `file_exists` builtin (`settings_bridge_hooks.cpp`
  `TakeExists`). `PlaySelection` (`menu_music_playback.h`) snapshots the audible/pending song per
  press; `Choose()` prefers `recent_` for 2 s after a transition.
- **The settings surface** — one table of `PanelSpec`s (`src/ui/settings_screen.cpp`): root →
  General → Interface (`settings_interface_screen`) → Main Menu (`settings_main_menu_screen`), and
  Controls → Interface Mapping (`settings_interface_mapping_screen`, five rows: Navigation,
  A, B, Music Library Options, Reset All). Settings ids are frozen in `src/settings/model.h`;
  the next free Interface id is **53**. `MenuConfiguration` (`settings_screen.h`) carries the
  boot-time facts; `src/hooks/settings_payload.cpp` fills it from the service.
- **Input** — the pad state filter in `src/input/remap.cpp` is the one seam that sees the physical
  pad and the delivered state; the `user_index == 0` block already merges mouse/PAD pulses after
  General Mapping. Keyboard state is the window's own key-event latch (`mouse_ui.cpp` `LatchKey` /
  `HostKeyDown`), keyed by SDL scancode name.
- **Media keys are not delivered** — `rexglue-sdk/src/ui/sdl_virtual_key.cpp`
  `TranslateSDLScancode` has no case for `SDL_SCANCODE_MEDIA_*`, so the window drops those events
  before `mouse_ui.cpp` ever sees a `VirtualKey` (the enum *does* have `kMediaNextTrack`,
  `kMediaPrevTrack`, `kMediaPlayPause`). SDL names them `MediaTrackNext`,
  `MediaTrackPrevious`, `MediaPlayPause`.
- **The footer and the overlay** — `A SELECT / B BACK` is guest-drawn. The host's ImGui overlay
  (`src/ui/help_overlay.cpp` `OnDraw`) draws the caption above that band and already draws over
  guest labels (`DrawBindingRows`); it early-outs when there is no caption text.
- **No pause exists** anywhere in this layer: the only track teardowns are navigation (`Stop`,
  `StopStreams`, destructor hooks) and R7's refresh stand-down.
- **Menu context** — `src/input/native_menu.cpp` `Context()` records the guest's screen name
  (`splash_screen` for the main menu) and `menu_available` every frame.

## 3. Decisions

| Id | Decision |
| --- | --- |
| **D1** | **Setting.** `SettingId::kBackgroundMusicNavigation = 53` (the next free Interface id), token `interface.background_music_navigation`, `Category::kInterface`, `WidgetKind::kBoolean`, default **true**, `ApplicationStrategy::kRestart` (it decides boot-built rows and the footer, like the other menu-shaping rows). **Row:** `BoolRow("settings_music_navigation", …)` labelled **`Menu Music Navigation`** (21 characters; `Menu Music Navigation: On` is 25, inside the 28-character line budget) on **General → Interface**, the page's fifth row directly after `settings_labels` (4 rows today, budget 6). **Why not the Main Menu page:** that page lists the menu's own hideable entries and its static rows are replaced by the live mirror built from `g_configuration.menu_rows` (`src/ui/settings_screen.cpp`), so a behaviour switch there would be dropped on a normal boot and would break the page's "no master switch" rule. Carrier cvar `enhancements_background_music_nav` (default `true`, `kRequiresRestart`), written at the config rank by `CvarForSetting`, read once by `shell_music::Configure` (P2) and per frame by the latch and the overlay. |
| **D2** | **The navigator owns the menu music source, not R9.** With a pool it navigates the pool; with R9 off (or no pool) it navigates the title's three tracks. It therefore learns the panel's Load arguments (`this`, gain, pan) on every Load call when the setting is on, and keeps one guest path buffer allocated even when R9 built no pool. |
| **D3** | **One session playlist and cursor serve manual and automatic advancement.** `MusicSession` retains stable catalog song IDs and backward/forward history independently of mounted devices. Next follows an existing forward entry; at the frontier it draws an unseen source. Previous follows the same history under D4. After library discovery and one full pass, begin a fresh randomized pass without an immediate repeat. Keep current + three upcoming + two recent songs warm, with an eight-package residency ceiling and explicit active/selection pins. Retired mounts are remounted asynchronously when revisited; a missing ready successor never blocks the UI. The title's three tracks use the same cursor semantics when R9 is off. This supersedes the former fixed-50 pool cursor and separate automatic `Rotation` order. |
| **D4** | **The 5-second rule uses the stream's sample counter** (`Stream+0xA0` / 44,100), not wall time. A track that is still loading counts as "< 5 s". "First song" means cursor 0 → restart. Next/Previous always resume playback if paused. |
| **D5** | **Pause is proven or reported, never faked.** **P2** must find a seam that (a) silences the stream, (b) freezes the reported position, (c) resumes at the same position, and (d) is not undone by the guest's own poll. Candidate ladder: the `StandardStream` message/vtable pause-and-resume call; else the stream state word (`Stream+0x14`, value 4 = the usual paused state) under a measured write; else the Play/Pause action is drawn as unavailable (`<label>: Unavailable`) and Start does nothing, with the limitation in `known-issues.md`. During a pause the `Playback` watchdog must be suspended or it marks the track failed after 3 s. |
| **D6** | **Buttons are consumed on the main menu only** (`splash_screen` *and* its option list drawn), while the setting is on and a menu-music source exists. Bits: LB `0x0100`, RB `0x0200`, Start `0x0010`; media keys ride the existing keyboard gate (`keyboard_support`). One command per physical press (rising edge), keyboard hold does not repeat. |
| **D7** | **The footer is host-drawn beside retail's, never a payload/locale edit.** `help_overlay.cpp` draws `LB Previous` immediately left of the guest's A prompt and `RB Next` right of B, same baseline and face, only while the setting is on, the main menu is current and a menu-music source exists. The guest's `A Select / B Back` is left untouched (its button glyphs are guest art; strings are ours and are recorded as such). |
| **D8** | **Play Current Song follows the navigated song through the existing probe, with one fix.** The probe answers at press time, so it already names the audible song; the 2 s "recent" grace in `PlaySelection` must be cleared on a *manual* transition or a press right after Next/Previous would name the song that just left. |
| **D9** | **Commands travel as host state, not guest calls.** The input filter only *posts* a command (`rbbz-play/next|previous|pause` from the Controls page, or a rising edge in the filter). The per-frame poll applies it on the guest thread, the same place R9's handover already runs. |
| **D10** | Nothing is committed or pushed without the owner's approval. |

## 4. Design

### 4.1 Modules

| Module | Role |
| --- | --- |
| `src/input/menu_music_nav.{h,cpp}` (new, SDK-free) | `Command {kNext, kPrevious, kPlayPause}`; the rising-edge `Latch` (pad bits + the three key names); `Post`/`Take` (one command in flight); `PreviousAction(position_samples, cursor)` = restart vs step back (pure; the same rule `MusicSession::PreviousPosition` already carries); `SetAvailable`/`Available`. |
| `src/hooks/menu_music_session.h`, `menu_music_cache.h`, `menu_music_queue.h` | Delivered foundation: stable metadata catalog, shared playlist/cursor, outcomes, no-repeat passes, three-ahead/two-behind warming, explicit pins, bounded residency and a demand worker. Navigator commands will use `NextPosition`, `PreviousPosition` and `Select`; a cold target waits asynchronously while the current stream remains in control. |
| `src/hooks/menu_music_playback.h` | `PlaySelection::Skipped(index, now)` (a manual transition: `current_ = -1`, `pending_ = index`, **clears `recent_`**) and a `Paused` accessor for the poll. |
| `src/hooks/shell_music.{h,cpp}` | The guest half: learn Load args when navigator-only, allocate a path buffer, `ApplyNavigatorCommand(base, ctx)` at the top of `sub_827586D8`, `NavigatorNext/Previous/TogglePause` helpers, `NavigatorEnabled()`/`NavigatorHasSource()` for the overlay, and the pause seam (P2). |
| `src/input/remap.cpp` | `user_index == 0`: sample LB/RB/Start + the three `HostKeyDown` names, run the latch when `menu_music_nav::Available()` and `MenuScreenIs("splash_screen")`, post one command, clear the three pad bits from the delivered state. |
| `src/input/mouse_ui.cpp` (+ a SDK patch) | `KeycodeForVirtualKey` cases for `kMediaNextTrack/kMediaPrevTrack/kMediaPlayPause`, which needs `TranslateSDLScancode` cases in `rexglue-sdk/src/ui/sdl_virtual_key.cpp` — delivered as **`patches/rexglue-sdk/0041-media-keys.patch`** (0020 is `bounded-shader-compilation-trace`, 0040 is the highest in the tree), applied by `scripts/apply_sdk_patches.ps1` (never a hand edit). |
| `src/hooks/shell_music.cpp` `AnswerPlayQuery` | Adds `rbbz-play/next`, `rbbz-play/previous`, `rbbz-play/pause` → `Post(...)`, answer `true`. |
| `src/ui/settings_screen.{h,cpp}` | `Kind::kPlaybackAction` (one symbol, always its label, press body `{file_exists "<path>"}`, no commit — `kPathAction` writes and commits and `kAction` goes through `invoke`, while this namespace must answer with the settings channel off or forward-only, `settings_bridge_hooks.cpp` `TakeExists`); the `BACKGROUND MUSIC PLAY` page; the `Background Music Play` submenu as Interface Mapping's fifth row (before `Reset All`); the `Menu Music Navigation` boolean on General → Interface; `MenuConfiguration::background_music_navigation`. |
| `src/ui/help_overlay.cpp` | `DrawMainMenuMusicFooter(io)` before the caption's early-out. |

### 4.2 Command flow

```text
LB/RB/Start (physical pad)      media keys (event latch)      Controls → Background Music Play
        │                              │                                  │
        └──── rising edge (main menu, setting on) ────┐                   │ {file_exists "rbbz-play/next"}
                                                      ▼                   ▼
                                        menu_music_nav::Post(Command)  (AnswerPlayQuery)
                                                      │
                                                      ▼   one command per frame
                                   sub_827586D8 (guest thread) → ApplyNavigatorCommand
                                                      │
             ┌────────────────────────────────────────┼─────────────────────────────┐
             ▼                                        ▼                             ▼
   next/previous: retire + Load + Start        pause: the proven seam        PlaySelection::Skipped
   (pool entry, or title track when R9 off)     (watchdog suspended)         → Play Current Song follows
```

### 4.3 The two sources

- **DLC session (R9 on):** use the shared `MusicSession` playlist, not mounted-cache
  order. `SongAt(position)` is a stable catalog ID; the catalog supplies the exact
  metadata key and source alias/path. Request a cold target asynchronously and keep
  the current stream until it is ready. `LoadOnGuest` performs the handoff on the guest
  owner thread; pin active/pending/captured selections and defer eviction while native
  Files remain open. A manual move calls `Skipped` and the future
  `PlaySelection::Skipped(...)` so the previous audible song cannot win the grace rule.
- **Title tracks (R9 off / no pool):** the Load hook records the asked path; if it matches
  `sfx/shell/samples/shellmusic_N`, the cursor is N and Next/Previous load the neighbouring entry
  with `loop=true` (the title's own behaviour). No pool index exists, so `Play Current Song` says
  "nothing to start" (its probe answers `begin` false) — correct, since the track is not a DLC song.
- **R9 off** returns from `Configure` before allocating `g_buffers` (`shell_music.cpp`, the
  `if (!g_menu_music_enabled)` early return); with the navigator on, move the buffer allocation (and
  the Load-argument capture) ahead of those early returns so the title's three tracks can still be
  handed over.

### 4.4 Pause

The only open RE question. Instruments for P2, in order:

1. `scripts/scan_process_strings.ps1` on a live menu boot, around the `ShellMusic`/stream class, to
   find a message-table entry naming a pause/resume; call it on the guest thread as
   `LoadOnGuest` does.
2. The `StandardStream` vtable (RTTI → slots via a scratch `rtti_vtables.py`, the tool cited in
   `docs/engine/main-menu.md` part 2; regenerate if absent) — find the slot the poll's state
   predicates do not use.
3. The state word `Stream+0x14` (3 = playing, 6 = finished today): try 4 while watching the
   position and the guest's own poll.

Acceptance for the seam (all measured in one boot log): position stops advancing, silence, no
`Playback` failure line, `rbbz-play/pause` again → position continues from the same count.
If none holds, D5's fallback applies and the Play/Pause row is drawn unavailable.

### 4.5 The footer

`DrawMainMenuMusicFooter` reads `VisibleMenuRows()` (screen name + drawn list), the setting's cvar,
and `shell_music::NavigatorHasSource()`; it draws two `ImGui::TextUnformatted` runs on the prompt
band's baseline. The x positions are calibrated from a boot capture (P3): the guest prompt cluster
is centred at `0.5 * DisplaySize.x`; `LB Previous` ends one measured gap left of it, `RB Next`
starts one gap right of it. No cover rectangle: nothing of the guest's is replaced.

## 5. Prompts

Three prompts, each self-contained (goal → files → acceptance → evidence). Start each in a fresh
session; do not commit.

### P1 — The setting, the control row, and the pure navigator core

**Goal.** Land the switch and the tested bookkeeping with **no guest behaviour change yet**.

**Deliverable**

1. `src/settings/model.h` (`kBackgroundMusicNavigation = 53`), `model.cpp` token,
   `schema.cpp` descriptor + strategy row (`kRestart`, empty evidence), `cvars.cpp`
   `CvarForSetting` → `enhancements_background_music_nav`.
2. `src/enhancements.cpp`: cvar (default `true`, `kRequiresRestart`), `kToggles` row (**13**
   entries — the array's explicit size moves with it), and the boot sentence updated to name it
   ("…R3, R6, R7's cache, R9, R10 and the menu-music navigator's own cvar"). The row is keyed by its
   cvar name, not by an `R<n>` id: `R1`–`R10` are the customization plan's items, and `R11` already
   belongs to the load-guard plan's retired loading-cancel record.
3. `src/hooks/settings_payload.cpp`: read the service's effective value into
   `MenuConfiguration::background_music_navigation`.
4. `src/ui/settings_screen.{h,cpp}`: the **General → Interface** row
   (`BoolRow("settings_music_navigation", "interface.background_music_navigation",
   "Menu Music Navigation", …)`), placed directly after `settings_labels`; `Kind::kPlaybackAction`
   + `PlaybackActionRow` (always draws its label, press body `{file_exists "<path>"}`, no commit);
   the Interface Mapping row (`SubmenuRow("settings_interface_music_play",
   "settings_interface_music_play_screen", "Background Music Play", …)`), inserted **before**
   `settings_reset_general` so Reset All stays last (5 → 6 rows, the visible budget), and the
   three-row page with **Next Track / Previous Track / Play/Pause** (`rbbz-play/next`,
   `rbbz-play/previous`, `rbbz-play/pause`); both the row and the panel removed when the setting is
   off (erase before `AllPanels()` returns, so `TestEveryDestinationExists` never sees a dangling
   destination); title `BACKGROUND MUSIC PLAY` in `ApplyDefaultTitle`.
5. `src/input/menu_music_nav.{h,cpp}` with the whole pure API of §4.1 (no SDK), added to
   `RBBLITZ_SOURCES`; reuse `MusicSession` cursor/history in `menu_music_session.h` (do not add a second rotation order);
   `PlaySelection::Skipped` in `menu_music_playback.h`.
6. Tests: `tests/menu_music_pool_tests.cpp` (shared history/cursor, asynchronous cold-target handling, the
   5 s rule, `Skipped` clears the grace, the latch fires once per press), `tests/settings_screen_tests.cpp`
   (the Interface switch, the three action rows, the hidden-when-off case, `TestEveryDestinationExists`,
   the row/page counts move), `tests/settings_hot_apply_tests.cpp` if its pinned row count moves,
   `settings_service` for the new descriptor.
7. `docs/settings/plan.md` §3.2 (Interface Mapping gains the submenu) and §3.5 (General → Interface
   gains the switch) — the rows land in this prompt, so the row tables move with them.

**Acceptance.** `ctest` green in the Vulkan-off debug tree; `check_docs.py` 0 errors. A boot with
the setting on shows `Menu Music Navigation: On` as the fifth row of General → Interface and the
`Background Music Play` submenu as Interface Mapping's fifth row, its page drawing **Next Track**,
**Previous Track** and **Play/Pause** (screenshot via `scripts/capture_window.ps1`); the same boot
with `--no-enhancements_background_music_nav` shows none of them. No behaviour change to the music
itself.

**Evidence.** `out/_swap/music-nav/p1-menu/` (two screenshots + the boot log lines naming the row
state).

### P2 — The runtime: pause seam, navigation, inputs, Play Current Song

**Goal.** Make Next/Previous/Pause actually change the menu music, driven by LB/RB/Start and the
media keys, and keep Play Current Song on the audible song.

**Deliverable**

1. Pause seam per D5 (measurement first; the chosen seam + the values in the plan record and in the
   `shell_music` comment). Suspend the `Playback` watchdog while paused; a manual move unpauses.
2. `shell_music`: Load-argument capture and buffer allocation for the navigator-only case (D2);
   `ApplyNavigatorCommand` at the top of `sub_827586D8`; `NavigatorNext/Previous/TogglePause`;
   `PlaySelection::Skipped` on every manual move; `AnswerPlayQuery` handles the three commands.
3. `menu_music_nav` production wiring in `src/input/remap.cpp`: the `user_index == 0` block samples
   LB/RB/Start and `HostKeyDown("MediaTrackNext"|"MediaTrackPrevious"|"MediaPlayPause")`, gates on
   `Available()` + `MenuScreenIs("splash_screen")` + `keyboard_support` for the keys, posts one
   command on a rising edge, clears the three pad bits.
4. `src/input/native_menu.{h,cpp}`: `bool MenuScreenIs(std::string_view name)` (the mutex-guarded
   `current_screen_name`).
5. SDK patch **`patches/rexglue-sdk/0041-media-keys.patch`** (0020 is
   `bounded-shader-compilation-trace`; 0040 is the highest number in the tree):
   `TranslateSDLScancode` cases for
   `MEDIA_NEXT_TRACK/PREVIOUS_TRACK/PLAY_PAUSE` → `kMediaNextTrack/kMediaPrevTrack/kMediaPlayPause`,
   plus the three cases in `mouse_ui.cpp`'s `KeycodeForVirtualKey`; `patches/README.md` row.
6. `scripts/drive_ui.ps1`: `media_next`/`media_prev`/`media_play_pause` (VK `0xB0/0xB1/0xB3`,
   extended-key scancodes `E0 19/E0 10/E0 22`).
7. A diagnostic cvar for the pad arm (the harness cannot press a pad): `background_music_nav_probe`
   = `next|previous|play_pause`, queued once N seconds after the menu is up, so the guest-side
   execution is testable without a physical pad.

**Acceptance** (one release build, one boot log each):

- Media next twice → two different songs (log names), OCR unchanged (audio only).
- Media previous at ~2 s → previous song; at ~7 s → same song restarted (position returns to 0).
- Play/Pause → silence + position frozen + no `failed to start` line; again → resumes at the same
  position.
- After a skip, pressing **Play Current Song** loads the *new* song (`settings_play_unavailable` is
  not shown; the loading screen names it).
- Setting off (`--no-enhancements_background_music_nav`): the same keys do nothing (log silent).
- R9 off (`--no-enhancements_menu_dlc_songs`): the three keys move the title's tracks; pause holds.
- Pad bits: unit test for the latch; the diagnostic probe boot for execution; a physical-pad boot is
  **not claimed** (no pad on this machine — say so).

**Evidence.** `out/_swap/music-nav/p2-media/`, `p2-pad/`, `p2-play-current/`, `p2-off/`,
`p2-title-tracks/` (logs + captures).

### P3 — The footer, the soak, and the documentation

**Goal.** The visible footer, the final acceptance sweep and the docs.

**Deliverable**

1. `src/ui/help_overlay.cpp`: `DrawMainMenuMusicFooter(io)` before the caption early-out; draws
   `LB Previous` / `RB Next` on the guest prompt band's baseline, only with the setting on, the
   main menu current and a source available; x offsets are the P3 calibration constants (measure
   the prompt cluster at 16:9 and 21:9 in one boot each).
2. Rows/pages polish: the Play/Pause row becomes unavailable when P2's seam failed (D5).
3. Docs: `docs/engine/toggles.md` (the `background_music_nav` row and the "toggles that ship on"
   paragraph), `docs/engine/main-menu.md` part 2 (new section on the
   navigator: the queue, the 5 s rule, the pause seam, the footer), part 1 §7.2 (Play Current Song
   follows a manual skip), `docs/engine/input-flow.md` (the menu-music latch and the media-key
   patch), `docs/settings/status.md` (one bullet), `docs/reference/known-issues.md` (only what is
   really limited: Start is consumed on the main menu; the footer's shoulder labels are host text;
   the media keys may be claimed by another Windows media app), this plan's status record.
4. Index: `docs/README.md` and `docs/plans/in-progress/README.md` already point at `in-progress/`
   (corrected 2026-10-26); when P1–P3 are green, `git mv` the plan to `finished/` and re-point every
   reference with `move_docs.py`.

**Acceptance.** One full boot with a capture read by OCR: the footer reads
`LB PREVIOUS … A SELECT … B BACK … RB NEXT` (OCR may drop the glyphs; assert on the words), the
`Background Music Play` page (Controls → Interface Mapping) draws its three rows, and the music
responds. `ctest` green, `check_docs.py` 0 errors.
The `nav-off` boot shows the retail footer alone.

**Evidence.** `out/_swap/music-nav/p3-footer/` (16:9 + 21:9), `p3-acceptance/`.

## 6. Acceptance matrix (summary)

| Arm | Setting | R9 | Expected |
| --- | --- | --- | --- |
| `p1-menu` | on / off | on | Interface switch + Interface Mapping submenu + action page present / all absent |
| `p2-media` | on | on | next, prev(<5 s), prev(≥5 s), pause/resume |
| `p2-play-current` | on | on | Play Current Song starts the navigated song |
| `p2-off` | off | on | keys inert; footer absent |
| `p2-title-tracks` | on | off | title tracks navigate and pause |
| `p3-footer` | on / off | on | host footer / retail footer alone |
| `p3-acceptance` | on | on | full route + OCR, ctest, check_docs |

## 7. Documentation obligations

| Document | Why |
| --- | --- |
| `docs/engine/toggles.md` | New cvar `enhancements_background_music_nav`, keyed by its cvar name (no `R<n>` id). |
| `docs/engine/main-menu.md` | Part 2 owns the background music; add the navigator section, and part 1 §7.2 the manual-skip rule for Play Current Song. |
| `docs/engine/input-flow.md` | The main-menu button capture, the media-key patch, the one-command-per-frame seam. |
| `docs/settings/plan.md` | §3.2 (Controls → Interface Mapping gains the submenu) and §3.5 (General → Interface gains the switch) — **P1**, the prompt that lands the rows. |
| `docs/settings/status.md` | One at-a-glance bullet. |
| `docs/reference/known-issues.md` | Only the measured limits (Start consumption, host-drawn shoulder labels, Windows media-key arbitration, pause if unavailable). |
| `docs/plans/in-progress/README.md` + `docs/README.md` | Index rows — corrected 2026-10-26 to point at `in-progress/`; `finished/` on delivery. |

## 8. Risks and open questions

| Id | Question / risk | Resolution |
| --- | --- | --- |
| OQ1 | Is there a pause seam at all? | D5's ladder; a failed seam is a documented limitation, not a fake pause. |
| OQ2 | Does the guest's own poll restart a paused stream or tear it down? | P2 boot: read `Stream+0x14`/`+0xA0` across 5 s while paused. |
| OQ3 | Does Start do anything retail on the splash screen? | P2 control boot with the setting off: press Start, read the action line. If it starts PLAY, the navigator still consumes it only while it advertises the prompt (D6). |
| OQ4 | Are media keys delivered to this window at all on Windows? | P2: log the latch; if the OS shell swallows them, Start/LB/RB remain and the limitation is recorded. |
| OQ5 | Does the pre-press title screen also report `splash_screen`? | Gate on the option list being drawn (`VisibleMenuRows`), not on the name alone; P2's off-arm proves the title ignores the keys. |
| OQ6 | Do LB/RB conflict with a General Mapping row? | Interface Mapping's shoulders have no visible row (compiled defaults only), so the main-menu capture shadows nothing a player can see; recorded in known-issues. |
| OQ7 | Footer geometry at 21:9 | Two calibration boots in P3; constants, not magic numbers, in the overlay. |

## 9. How to reproduce the measurements

```powershell
cmake --preset win-amd64-debug -DREXGLUE_USE_VULKAN=OFF -DBUILD_TESTING=ON
cmake --build --preset win-amd64-debug
ctest --test-dir out\build\win-amd64-debug
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
python .github/skills/documentation/scripts/check_docs.py
```

Boots use a scratch profile (`--launcher_profile=<scratch>/launcher.toml`,
`--user_data_root=<scratch>`, `--dlc_root=<absolute>`), a log level of `info`, and
`dev.ps1 run-game-quick` for the auto-A route. `capture_window.ps1` + `ocr_image.ps1` (Windows
PowerShell 5.1) read the frame. **Never edit the SDK by hand** — the media-key map is patch 0041,
applied by `scripts/apply_sdk_patches.ps1`.


## 10. Architecture foundation delivered separately (2026-10-26)

Owner approval: implement the rolling architecture now; leave the background-music
navigator feature for another session. `MusicSession`, `MusicMountCache` and the demand
worker replace the runtime's fixed 50-song working set. Boot mounts one usable randomly
selected package. Cache loading and menu playback prepare only the nearby window; new
speculative requests stop while gameplay owns audio. Song metadata and playlist history
outlive residency. SDK patch `0040-device-open-file-accounting.patch` supplies the native
File lifetime count used after active/selection pins permit a package to retire.

The existing Play Current Song row uses the dynamic Symbol read introduced earlier;
its stable IDs remain valid after growth, retirement and remounting. No navigator input,
new setting, media key, pause operation, action row or footer is part of this delivery.
Future P1–P3 must build on D3 above rather than restoring the old pool-order cursor.
The media-key patch is allocated as **0041** (0020 is `bounded-shader-compilation-trace`, 0040 is
the highest in the tree); the historical "0020" in §4.1, §5/P2 and §9 is corrected. The scope
revision of 2026-10-26 also moved the switch to **General → Interface** (D1) — the Main Menu page
is the live mirror and cannot carry a behaviour row — and confirmed the Interface Mapping submenu
(R5) as the feature's only mapping surface: the three entries are one-shot commands, not `[remap]`
bindings. Footer implementation must follow the current owner directive in AGENTS.md.
