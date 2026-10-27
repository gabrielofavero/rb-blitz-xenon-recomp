# Background music navigator — implementation plan

**Status:** P1–P3 implementation present (2026-10-26); P3 acceptance remains open.
P2 connects navigation, bindings and owner-accepted native pause/resume. P3 adds an authorized
bottom-right player, local covers, bounded asynchronous artwork and explicit pointer ownership.
The owner confirms the player and live cover work and Start pauses. Their latest review removes
all LB/RB footer additions; the retail A/B prompts remain unchanged. RB Next crashed during a
song handoff in `rb_blitz_372.log`; its cause is still unresolved. The next build adds a bounded
first-chance fault trace for diagnosis. Live UI testing remains with the owner. See §21.

**Scope revision (2026-10-26, owner).** The two controls the feature cannot ship without are in
scope, and the plan is corrected to match the tree: the **on/off setting** moves from
General → Interface → Main Menu to **General → Interface** (D1 — the Main Menu page is the live
mirror of the menu's own entries and its rows are replaced at boot, so a behaviour switch cannot
live there), and the **Interface Mapping row** is pinned to the page's fifth row plus a three-row
page of one-shot commands (R5). The media-key SDK patch takes the next free number, **0041**
(0020 is occupied — §10).

**Player revision (2026-10-26, owner).** In addition to the LB/RB footer, add a compact,
polished **bottom-right host overlay** with album cover, song title, artist and clickable
**Previous / Play-Pause / Next** controls (`<< / Play-Pause / >>`). This request explicitly
authorizes this music-player overlay under the owner directive in `AGENTS.md`; it does not
authorize unrelated overlays. The two-way in-process bridge, artwork lifecycle, mouse routing and
acceptance are now part of P1–P3 below. This is a plan update only; none of that UI or bridge is
implemented or runtime-proven by this revision.

**Visibility revision (2026-10-26, owner).** Background Music Play is **always visible**,
including its three-row page and in-song twin, regardless of Menu Music Navigation. A restart
must never be required merely to discover this page. This supersedes P1's original hide-when-off
rule. The switch still gates runtime playback/input/player behavior; its restart strategy remains
until P2 proves live source setup and input/pause ownership. See §12 for the follow-up record.

## 1. Request (normalized)

| Id | Requirement |
| --- | --- |
| R1 | An **independent** feature: navigate the music the main menu is playing with controller buttons and keyboard media keys. It must not require *DLC Menu Songs* (R9) or *Play Current Song*, but it must cooperate with both. |
| R2 | One setting, row label **Menu Music Navigation** (`interface.background_music_navigation`, id 53, default **on**), on **General → Interface** — the page of behaviour switches, directly after *Help & Options Labels*. It is the feature's only switch: off means no pad capture, no media keys, no footer, no player overlay/artwork work; **Background Music Play stays visible** (R5, R7). **Not** on General → Interface → Main Menu: that page lists the menu's own hideable entries, its static rows are replaced by the live mirror built from `g_configuration.menu_rows`, and its documented rule is one visibility row per entry with no master switch. |
| R3 | Controller: **RB** (pad `0x0200`) / **MediaTrackNext** → next song. **LB** (pad `0x0100`) / **MediaTrackPrevious** → restart the current song when it has played ≥ 5 s, otherwise go to the previous song; on the first song of the queue, restart. **Start** (pad `0x0010`) / **MediaPlayPause** → pause/resume. |
| R4 | **Withdrawn by owner, 2026-10-26 (§21):** leave the retail `A Select / B Back` footer unchanged; no LB/RB prompts or spacing edits. |
| R5 | Controls → **Interface Mapping** always carries a **Background Music Play** submenu as the page's fifth row (between *Music Library Options* and *Reset All*, so Reset All stays last; the page goes 5 → 6 rows, which is the visible budget), opening a three-row page: **Next Track / Previous Track / Play/Pause**. The three are one-shot **commands** — a press fires `rbbz-play/next|previous|pause` through the guest's `file_exists` channel — not `[remap]` bindings: that table's targets are physical controls, while these are feature actions, so the player triggers them here but does not assign them. With the setting **off**, both the row and page remain visible; runtime commands are unavailable while navigation is disabled. |
| R6 | When the setting is on, a navigation must keep **Play Current Song** pointing at the song that is now audible, so pressing it starts exactly what is playing in the background. |
| R7 | With the setting off, behaviour is exactly today's: no interception, no footer prompts, no player overlay and no player artwork requests. Background Music Play remains visible; its commands cannot bypass the off gate. |
| R8 | Scope of the pad/media buttons and player overlay: the **main menu only** (the owner's footer), with the option list drawn and a menu-music source available. The player hides during gameplay, loading and other screens; it yields to modal dialogs/input capture. The three Controls-page actions work wherever the menu music is playing (they are the same commands) while the setting is on; with it off the page remains visible, but commands are unavailable. |
| R9 | Docs are part of the change (toggles, menu study, input-flow, settings plan/status, plan index). |
| R10 | Add a **bottom-right music-player overlay**: square album cover, song title, artist and `<< / Play-Pause / >>` controls. Use a compact translucent panel, readable text and hover/pressed/disabled feedback, scaled and inset from the viewport edges without covering the guest footer or menu rows. |
| R11 | All three player actions are **mouse-clickable**, one command per click, using the same Previous 5 s/restart rule, Next and pause/resume semantics as R3. The center icon reflects confirmed playback state (Pause while playing, Play while paused); if P2 cannot prove pause, show it disabled with an unavailable explanation. |
| R12 | **Two-way communication:** the playback owner publishes coherent track identity/title/artist/artwork reference and actual transport/capability state to the host UI; clicks post commands back for execution on the guest thread. Command acceptance/completion and playback updates flow back so the UI never treats a queued request as a completed skip or pause. |
| R13 | Load the audible song's album art asynchronously from its local package, with a neutral fallback while loading or when absent/invalid. Track metadata and decoded art must survive package retirement safely; late results from a previous track/session must never replace the current cover. No online lookup or store dependency is required. |

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
- **Player metadata/artwork are not wired.** `SongMetadata` already parses `title` and `artist`
  (`src/hooks/menu_music_metadata.h`), but `PreparePackage` in `shell_music.cpp` currently retains
  only alias, stream and song key in `PoolEntry`. Preserve display metadata and a package-relative
  artwork locator in the stable catalog; do not assume the existing session already carries them.
  `src/assets/texture_codec.h` exposes `DecodePlatformTexture` for Xbox texture bytes; the store's
  art service is a separate API/download-to-guest path, not a host-player texture bridge.
- **Mouse routing needs integration.** `src/input/mouse_ui.cpp` uses `io.WantCaptureMouse` in its
  draw-time focus gate but also queues guest mouse actions in event listeners. Prove event ordering
  and consume player hits before guest routing; a drawn ImGui button alone does not prove this.
- **No pause exists** anywhere in this layer: the only track teardowns are navigation (`Stop`,
  `StopStreams`, destructor hooks) and R7's refresh stand-down.
- **Menu context** — `src/input/native_menu.cpp` `Context()` records the guest's screen name
  (`splash_screen` for the main menu) and `menu_available` every frame.

## 3. Decisions

| Id | Decision |
| --- | --- |
| **D1** | **Setting.** `SettingId::kBackgroundMusicNavigation = 53` (the next free Interface id), token `interface.background_music_navigation`, `Category::kInterface`, `WidgetKind::kBoolean`, default **true**, `ApplicationStrategy::kRestart` (retained until P2 proves live source setup and input/pause ownership; **it no longer controls any row/page visibility**). **Row:** `BoolRow("settings_music_navigation", …)` labelled **`Menu Music Navigation`** (21 characters; `Menu Music Navigation: On` is 25, inside the 28-character line budget) on **General → Interface**, the page's fifth row directly after `settings_labels` (4 rows today, budget 6). **Why not the Main Menu page:** that page lists the menu's own hideable entries and its static rows are replaced by the live mirror built from `g_configuration.menu_rows` (`src/ui/settings_screen.cpp`), so a behaviour switch there would be dropped on a normal boot and would break the page's "no master switch" rule. Carrier cvar `enhancements_background_music_nav` (default `true`, `kRequiresRestart`), written at the config rank by `CvarForSetting`, read once by `shell_music::Configure` (P2) and per frame by the latch and the overlay. |
| **D2** | **The navigator owns the menu music source, not R9.** With a pool it navigates the pool; with R9 off (or no pool) it navigates the title's three tracks. It therefore learns the panel's Load arguments (`this`, gain, pan) on every Load call when the setting is on, and keeps one guest path buffer allocated even when R9 built no pool. |
| **D3** | **One session playlist and cursor serve manual and automatic advancement.** `MusicSession` retains stable catalog song IDs and backward/forward history independently of mounted devices. Next follows an existing forward entry; at the frontier it draws an unseen source. Previous follows the same history under D4. After library discovery and one full pass, begin a fresh randomized pass without an immediate repeat. Keep current + three upcoming + two recent songs warm, with an eight-package residency ceiling and explicit active/selection pins. Retired mounts are remounted asynchronously when revisited; a missing ready successor never blocks the UI. The title's three tracks use the same cursor semantics when R9 is off. This supersedes the former fixed-50 pool cursor and separate automatic `Rotation` order. |
| **D4** | **The 5-second rule uses sample progress since playback begins** (`Stream+0xA0` delta / 44,100), excluding the native random offset and decoded reserve, not wall time. Manual starts use zero offset (owner acceptance fix, §14). A track that is still loading counts as "< 5 s". "First song" means cursor 0 → restart. Next/Previous always resume playback if paused. |
| **D5** | **Native pause/resume, verified in §17.** StandardStream slot 7 pauses the channels and changes `Stream+0x14` from 3 to **5**; slot 6 resumes them at the same position. The Play hook prevents the guest poll from auto-resuming an owned pause. The owner confirms listening behavior, and `rb_blitz_367.log` proves a 7.285-second stable cursor and same-position resume. The earlier state-4 guess is superseded. Defer the `Playback` watchdog while paused; a manual move starts its successor playing. A final in-flight decoder block may settle during pause and is excluded from playback age. Never fake pause by muting or restarting. |
| **D6** | **Buttons are consumed on the main menu only** (`splash_screen` *and* its option list drawn), while the setting is on and a menu-music source exists. Bits: LB `0x0100`, RB `0x0200`, Start `0x0010`; media keys ride the existing keyboard gate (`keyboard_support`). One command per physical press (rising edge), keyboard hold does not repeat. |
| **D7** | **Retail footer retained (§21).** Remove native shoulder actions, locale additions, eligibility query and spacing/transform edits. No host footer is drawn. |
| **D8** | **Play Current Song follows the navigated song through the existing probe, with one fix.** The probe answers at press time, so it already names the audible song; the 2 s "recent" grace in `PlaySelection` must be cleared on a *manual* transition or a press right after Next/Previous would name the song that just left. |
| **D9** | **Commands travel as host state, not guest calls.** The input filter only *posts* a command (`rbbz-play/next|previous|pause` from the Controls page, or a rising edge in the filter). The per-frame poll applies it on the guest thread, the same place R9's handover already runs. |
| **D10** | Nothing is committed or pushed without the owner's approval. |
| **D11** | **The authorized player keeps the retail footer clear.** A separate host ImGui player lives at bottom right, gated by the same navigator switch/main-menu/source facts and independent of `settings_help_overlay` or caption text. Keep the footer and guest A/B prompts readable. No extra player switch in this scope. |
| **D12** | **The guest playback owner is authoritative.** A synchronized, immutable `NowPlayingSnapshot` carries a session epoch, monotonically increasing revision, stable source/song identity, display strings, artwork key/status, actual playback state, supported actions and latest command result. The UI copies it without reading guest memory. Mouse, pad, media keys and Controls actions all use `Post`/`Take`; only the guest poll applies playback operations. No socket, web service or second playback engine is needed for this in-process overlay. |
| **D13** | **Artwork is local, asynchronous and bounded.** Preserve metadata and an exact package-relative artwork locator by stable song identity. Resolve package assets on a worker, decode Xbox art with the existing codec and upload through the renderer's supported host texture path on its owning thread. Keep copied bytes/pixels/handles independent of guest pointers; hold a cache lease/open File only while reading. Use an explicit bounded art cache and discard stale results. Missing/corrupt art uses a neutral cover and never delays playback. |
| **D14** | **Clicks belong to one surface.** While the mouse-support gate is on, player controls accept left clicks and consume their press/release sequence before the guest can turn it into Select/Back/hover/wheel actions. Hovering/clicking the player must not move guest focus; outside it normal routing continues. Respect window focus, modal capture, visibility changes and DPI coordinates; reset held input on those boundaries. The overlay takes no keyboard/gamepad focus away from the menu. |

## 4. Design

### 4.1 Modules

| Module | Role |
| --- | --- |
| `src/input/menu_music_nav.{h,cpp}` (new, SDK-free) | `Command {kNext, kPrevious, kPlayPause}`; the rising-edge `Latch` (pad bits + the three key names); `Post`/`Take` (one command in flight, explicit accepted/busy/unavailable result and request id); `PreviousAction(position_samples, cursor)` = restart vs step back (pure; the same rule `MusicSession::PreviousPosition` already carries); `SetAvailable`/`Available`. |
| `src/ui/menu_music_player.{h,cpp}` (new) | Bottom-right ImGui player, snapshot consumer, three clickable controls, safe-area/DPI layout, renderer-owned album-art handles and integration with mouse hit testing. Register through the existing overlay/drawer lifecycle independently of help-caption visibility. |
| `src/hooks/menu_music_now_playing.{h,cpp}` (new; data/bridge core SDK-free) | Immutable snapshots and synchronized publication; command result/sequence tracking; stable display metadata and artwork keys. `ReadSnapshot()` never calls the guest. |
| `src/ui/menu_music_artwork.{h,cpp}` (new; pure cache policy testable separately) | Bounded local artwork worker/cache, package read leases, decode via `src/assets/texture_codec.h`, tagged completion payloads and renderer-thread upload/release integration. |
| `src/hooks/menu_music_session.h`, `menu_music_cache.h`, `menu_music_queue.h` | Delivered foundation: stable metadata catalog, shared playlist/cursor, outcomes, no-repeat passes, three-ahead/two-behind warming, explicit pins, bounded residency and a demand worker. Navigator commands will use `NextPosition`, `PreviousPosition` and `Select`; a cold target waits asynchronously while the current stream remains in control. |
| `src/hooks/menu_music_playback.h` | `PlaySelection::Skipped(index, now)` (a manual transition: `current_ = -1`, `pending_ = index`, **clears `recent_`**) and a `Paused` accessor for the poll. |
| `src/hooks/shell_music.{h,cpp}` | The guest half: learn Load args when navigator-only, allocate a path buffer, `ApplyNavigatorCommand(base, ctx)` at the top of `sub_827586D8`, `NavigatorNext/Previous/TogglePause` helpers, `NavigatorEnabled()`/`NavigatorHasSource()` for the overlay, authoritative snapshot/result publication after automatic/manual transitions and pause/resume, and the pause seam (P2). |
| `src/input/remap.cpp` | `user_index == 0`: sample LB/RB/Start + the three `HostKeyDown` names, run the latch when `menu_music_nav::Available()` and `MenuScreenIs("splash_screen")`, post one command, clear the three pad bits from the delivered state. |
| `src/input/mouse_ui.cpp` (+ a SDK patch) | `KeycodeForVirtualKey` cases for `kMediaNextTrack/kMediaPrevTrack/kMediaPlayPause`, which needs `TranslateSDLScancode` cases in `rexglue-sdk/src/ui/sdl_virtual_key.cpp` — delivered as **`patches/rexglue-sdk/0041-media-keys.patch`** (0020 is `bounded-shader-compilation-trace`, 0040 is the highest in the tree), applied by `scripts/apply_sdk_patches.ps1` (never a hand edit). |
| `src/hooks/shell_music.cpp` `AnswerPlayQuery` | Adds `rbbz-play/next`, `rbbz-play/previous`, `rbbz-play/pause` → `Post(...)`, answer `true`. |
| `src/ui/settings_screen.{h,cpp}` | `Kind::kPlaybackAction` (one symbol, always its label, press body `{file_exists "<path>"}`, no commit — `kPathAction` writes and commits and `kAction` goes through `invoke`, while this namespace must answer with the settings channel off or forward-only, `settings_bridge_hooks.cpp` `TakeExists`); the `BACKGROUND MUSIC PLAY` page; the `Background Music Play` submenu as Interface Mapping's fifth row (before `Reset All`); the `Menu Music Navigation` boolean on General → Interface; `MenuConfiguration::background_music_navigation`. |
| `src/ui/help_overlay.cpp` | `DrawMainMenuMusicFooter(io)` before the caption's early-out; keep the separate player independent of captions and the help-label toggle. |

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

The player adds the reverse publication path and another command producer:

```text
Playback owner (guest poll) -- NowPlayingSnapshot + command results --> Host player
Local artwork worker       -- epoch/song/art-key tagged pixels ------> Render thread
Host player mouse click    -- Post(Command, request_id) -------------> Guest poll
                                  <-- accepted/busy/unavailable; then applied/failed
```

A single in-flight slot never silently overwrites another producer's command: reject as busy,
disable repeated player clicks until the slot is free, and report the result through the bridge.
Applying a command can leave a cold target loading; publish that pending state without changing
the audible track's identity/cover until the handoff is confirmed. Revalidate setting, source,
producer-specific visibility/capture scope and session epoch when taking commands; cancel stale requests on exit,
refresh or shutdown. The command envelope identifies its producer so Controls-page actions retain
R8's wider scope. No guest call, package I/O or GPU upload runs inside an ImGui click handler.

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

**Current seam (owner-verified 2026-10-26, §17):** StandardStream slot 7
(`0x823E3FD8`) pauses the channels and changes state 3 → 5; slot 6 (`0x823E4808`)
resumes them at the same position. The Play hook prevents ShellMusic's poll from
automatically resuming an explicitly paused stream. Watchdogs are deferred while
paused; navigation and teardown clear pause ownership. No mute/restart or seek is used.

The following was the original investigation ladder, retained as history:

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

The latest owner request (§21) removes the shoulder prompts and their spacing. Keep the retail
A/B footer untouched. No native footer mutation, extra locale entry, availability query or host
footer drawing remains. LB/RB continue to execute their configured transport actions.

### 4.6 The player and its snapshot

Planned layout (illustrative, geometry unmeasured):

```text
                                    bottom-right safe area
                         +--------------------------------+
                         | [ album ]  Song title           |
                         | [ cover ]  Artist               |
                         |            <<    Play/Pause  >> |
                         +--------------------------------+
      LB Previous       A Select       B Back       RB Next
```

Use a square cover, a subtle rounded/translucent background, the existing player-facing font,
clear transport symbols and generous click targets. Ellipsize long title/artist lines and expose
the full strings on hover. Scale from logical viewport/DPI dimensions, clamp to small windows and
inset the panel above the footer band; calibrate against guest rows, captions and the footer at
16:9 and 21:9. Controls have descriptive tooltips and visible hover/pressed/disabled states.
No seek bar, volume control or extra playback feature is added by this revision.

`NowPlayingSnapshot` includes session epoch/revision; stable source type and catalog/song id;
title/artist; artwork key and missing/loading/ready/error state; actual
loading/playing/paused/stopped/unavailable state; supported Next/Previous/Pause flags; pending
request id/action and applied/failed/cancelled result. Publish on automatic advances too, including
the first audible track, load failure/recovery, refresh and stand-down. Keep strings/keys owned
by the snapshot. Read/copy under a short lock or immutable handoff; never hold it during decode,
I/O, rendering or a guest call. Playback fields are written by the guest owner; artwork workers
report tagged art status/completions through the synchronized bridge without rewriting transport
state. The player changes its center icon only on confirmed state.

Metadata/artwork must describe the **audible** track while its successor loads. If no stream is
audible, show loading/unavailable honestly. Use a neutral title-track label and unknown-artist
fallback for built-in tracks with no verified metadata, and a neutral cover for missing artwork;
never substitute a DLC song's metadata or use `PlaySelection`'s old-song grace as now-playing data.
Controls-page commands can run outside the visible player's main-menu scope as R8 already allows.

### 4.7 Album-art path and lifecycle

1. At package preparation, keep the parsed display strings and resolve the corresponding local
   art locator using exact stream/song identity. Verify the actual art paths with both a single
   DLC package and a merged pack before committing the resolver; no guessed basename-only match.
   Preserve catalog metadata when the mount is retired.
2. On an audible-song/art-key change, display the neutral cover immediately and enqueue a bounded
   worker request tagged with session epoch, song id and artwork key. Reuse ready decoded art for
   the same key. An evicted package is read through the mount-cache lease/remount mechanism;
   optional art must neither evict active audio nor force residency above eight packages. If no
   lease/slot is available, defer art and keep the fallback without blocking navigation.
3. Copy bounded encoded bytes while the read lease/File is live, then release it; validate byte
   size, dimensions and decode allocation limits before `DecodePlatformTexture`, resize to the
   display/cache budget and publish owned RGBA pixels. No raw guest pointers or borrowed device
   handles escape to the UI. Missing, unsupported or corrupt art ends in the neutral cover.
4. P3 must identify and prove the ImGui backend's arbitrary-image texture create/upload/destroy
   API; existing caption/font drawing does not prove album-art support. Upload/release only on
   the renderer's required thread. If an SDK change is needed, ship a numbered patch using the
   next free number at implementation time; do not hand-edit the SDK. Record the chosen API.
5. Reject epoch/key mismatches at publication and attachment. Cache reusable pixels by stable
   package/song/art identity, with explicit byte/entry limits and bounded queued jobs; release
   GPU handles safely on eviction, renderer reset and shutdown. Cancel work/clear visible state
   on session reset and stop new player-art requests when the feature is off. Decoded art may
   outlive a mount but must not keep all previously visited packages resident.

Album covers with supported local assets are part of acceptance. A placeholder is the fallback
for absent/bad assets, not evidence that the valid-art rendering path is complete. No network
lookup, RhythmVerse matching or image generation is required for this feature.

### 4.8 Mouse routing

Use the existing mouse-support setting for click availability; media keys keep their keyboard
setting gate. Draw the player without requesting pad/keyboard navigation focus. Mouse-disabled
profiles retain the visible player and working pad/media controls, but the player consumes no
mouse actions. Resolve hit bounds in the same logical coordinates used to draw, with correct
physical-window/DPI conversion; do not reuse guest normalized coordinates blindly.

Consume input over the visible player before `mouse_ui.cpp` queues guest menu actions. Verify
listener priority and ImGui capture timing rather than relying only on last frame's
`WantCaptureMouse`. A left press/release on one enabled control posts once; holding does not
repeat, and dragging/releasing elsewhere does not activate another control. Suppress underlying
row hover, click, double-click and wheel actions over the panel. Outside it preserve native menu
routing. On focus loss, a modal opening, leaving the main menu or a gate changing, clear armed
clicks and prevent an unmatched release from activating the guest. Modal dialogs/input capture
win over the player; loading/gameplay have neither visible player nor player hit regions.

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
   `rbbz-play/previous`, `rbbz-play/pause`); both row and panel remain present when the setting is
   off, including the in-song twin; `TestEveryDestinationExists` validates both configurations.
   Title `BACKGROUND MUSIC PLAY` in `ApplyDefaultTitle`.
5. `src/input/menu_music_nav.{h,cpp}` with the whole pure API of §4.1 (no SDK), added to
   `RBBLITZ_SOURCES`; reuse `MusicSession` cursor/history in `menu_music_session.h` (do not add a second rotation order);
   `PlaySelection::Skipped` in `menu_music_playback.h`; the SDK-free snapshot/command-result
   bridge in `src/hooks/menu_music_now_playing.{h,cpp}` (D12), with owned strings, epoch/revision,
   capability state, explicit busy handling and reset/cancel semantics. Extend catalog data to
   retain title/artist and a stable local-art locator without tying metadata lifetime to mounts.
6. Tests: `tests/menu_music_pool_tests.cpp` (shared history/cursor, asynchronous cold-target handling, the
   5 s rule, `Skipped` clears the grace, the latch fires once per press), `tests/settings_screen_tests.cpp`
   (the Interface switch, the three action rows, identical on/off trees, `TestEveryDestinationExists`,
   the row/page counts move), `tests/settings_hot_apply_tests.cpp` if its pinned row count moves,
   `settings_service` for the new descriptor. Add focused bridge/cache-policy tests for coherent
   snapshots, command busy/results, epoch reset, stale artwork rejection and metadata retention
   across retirement; cover automatic as well as manual track publication in P2.
7. `docs/settings/plan.md` §3.2 (Interface Mapping gains the submenu) and §3.5 (General → Interface
   gains the switch) — the rows land in this prompt, so the row tables move with them.

**Acceptance.** `ctest` green in the Vulkan-off debug tree; `check_docs.py` 0 errors. A boot with
the setting on shows `Menu Music Navigation: On` as the fifth row of General → Interface and the
`Background Music Play` submenu as Interface Mapping's fifth row, its page drawing **Next Track**,
**Previous Track** and **Play/Pause** (screenshot via `scripts/capture_window.ps1`); the same boot
with `--no-enhancements_background_music_nav` retains the same submenu and command page;
the switch reflects the off/externally configured state. Page visibility never requires a restart
(owner follow-up, 2026-10-26).
No behaviour change to the music itself; the three command rows are authored but inert until P2.

**Evidence.** `out/_swap/music-nav/p1-menu/` (two screenshots + the boot log lines naming the row
state).

### P2 — The runtime: pause seam, navigation, inputs, Play Current Song

**Owner acceptance follow-up (2026-10-26).** The P1 page draws only action names and its presses
are inert, as originally scoped. The owner expects visible current bindings (for example
`Next Track: RB, Media Next`) and an assignment flow on selection. Carry that work into **P2**,
alongside the runtime input wiring: persist action-specific pad/media-key bindings, display their
effective values, support assignment/cancellation/reset, and make the input latch consume those
values. Do not map a feature action to an unrelated physical-control target in `[remap]`.
This revises R5/D9's command-only page design for the future implementation; P1's existing
`file_exists` commands remain the execution channel, and P3 must polish bound/unavailable values
within the guest row budget. Acceptance must include reassignment surviving restart, one command
per press, cancellation leaving the prior binding intact, and the navigation-off gate. This is
recorded future work, not a claim that P1 supports assignment or playback commands.

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
8. Publish authoritative snapshots/results from the playback owner: first audible song,
   automatic advance, manual skip/restart, cold-target loading/handoff, pause/resume, failure,
   refresh, stand-down and shutdown. Track applied vs merely queued commands. Preserve current
   audible metadata until a successor starts; publish unavailable pause capability if D5 fails.
   Unit tests verify these transitions and command cancellation/revalidation without a live UI.

**Acceptance** (one release build, one boot log each):

- Media next twice → two different songs (log names), OCR unchanged (audio only).
- Media previous at ~2 s → previous song; at ~7 s → same song restarted (position returns to 0).
- Play/Pause → silence + position frozen + no `failed to start` line; again → resumes at the same
  position.
- After a skip, pressing **Play Current Song** loads the *new* song (`settings_play_unavailable` is
  not shown; the loading screen names it).
- Setting off (`--no-enhancements_background_music_nav`): the same keys do nothing (log silent);
  the visible Controls page cannot execute commands or bypass the off gate.
- R9 off (`--no-enhancements_menu_dlc_songs`): the three keys move the title's tracks; pause holds.
- Pad bits: unit test for the latch; the diagnostic probe boot for execution; a physical-pad boot is
  **not claimed** (no pad on this machine — say so).

**Evidence.** `out/_swap/music-nav/p2-media/`, `p2-pad/`, `p2-play-current/`, `p2-off/`,
`p2-title-tracks/` (logs + captures).

### P3 — Clickable player, artwork, soak and documentation

**Goal.** Deliver the authorized bottom-right music player, prove its two-way
bridge and local artwork path, complete the acceptance sweep and update the docs.

**Deliverable**

1. **Retail footer (§21):** retain the original A/B widgets. Remove all shoulder prompt and
   spacing additions, their locale entries and `rbbz-play/footer`; no host footer is drawn.
2. `src/ui/menu_music_player.{h,cpp}` plus lifecycle registration/source-list wiring: the compact
   player in §4.6 with cover/title/artist and clickable Previous/Play-Pause/Next. Consume the P2
   snapshots and post through the shared command queue. Respect switch, main-menu/source,
   mouse-support and modal/focus gates; draw independently of help-caption text/toggle.
3. `src/ui/menu_music_artwork.{h,cpp}`: implement §4.7's local resolver, read lease, bounded
   decode/cache, stale-result checks and proven backend texture upload/release path. Reuse the
   existing Xbox decoder. Include source-list wiring and an SDK patch only if required; record
   the measured backend API and limits. Prove valid-art rendering, fallback and resource cleanup.
4. `src/input/mouse_ui.cpp` and player hit-testing/capture integration per §4.8: consume player
   pointer sequences before guest actions, preserve normal routing elsewhere and clear armed
   clicks on boundaries. Add focused tests for routing/coordinate conversion, one command per
   click, stale/hidden bounds, drag cancellation and disabled controls.
5. Rows/pages polish: the Play/Pause row and player center control become unavailable when P2's
   seam failed (D5). Pause is visibly disabled; Next/Previous still work and resume playback
   while navigation is enabled. With navigation off or no source, the always-visible command
   page must explain unavailable actions rather than hiding the page.
6. Docs: `docs/engine/toggles.md` (the `background_music_nav` row and the "toggles that ship on"
   paragraph), `docs/engine/main-menu.md` part 2 (new section on the
   navigator: the queue, the 5 s rule, the pause seam, the footer, the player, the snapshot/command
   bridge and the art resolver/cache/texture lifecycle), part 1 §7.2 (Play Current Song
   follows a manual skip), `docs/engine/input-flow.md` (the menu-music latch and the media-key
   patch and mouse capture/click routing), `docs/settings/status.md` (one bullet), `docs/reference/known-issues.md` (only what is
   really limited: Start is consumed on the main menu; RB handoff has an unresolved crash;
   the media keys may be claimed by another Windows media app), this plan's status record.
7. Index: `docs/README.md` and `docs/plans/in-progress/README.md` already point at `in-progress/`
   (corrected 2026-10-26); when P1–P3 are green, `git mv` the plan to `finished/` and re-point every
   reference with `move_docs.py`.

**Acceptance.** One full boot with the retail A/B footer unchanged, the
`Background Music Play` page (Controls → Interface Mapping) draws its three rows, and the music
responds. `ctest` green, `check_docs.py` 0 errors.
The `nav-off` boot shows the retail footer alone, with no player hit regions or artwork jobs.
Also require:

- Player displays the audible title/artist and a **real local cover** in both a single-song DLC
  package and a merged pack; automatic advance updates it as well as manual skip. Missing/invalid
  art and title tracks show the defined fallback. Long/non-ASCII metadata remains readable.
- Click Previous below/above 5 s, Next, and Play/Pause; results match controller/media commands.
  The center icon follows confirmed pause/resume state. If pause is unproven, it is disabled with
  an explanation, the limitation is recorded, and pause acceptance is not claimed as passed.
- Hold, double-click, drag out, click a disabled control and click through overlapping guest-row
  coordinates: no unintended duplicate command or guest action. Outside-player clicks still work.
  Modal/input capture and focus loss cancel armed clicks; mouse support off consumes nothing.
- Rapid skips with deliberately delayed cover decoding never display the previous cover on the
  new song. Cold remount, refresh/reset, repeated navigation and renderer teardown leave no stale
  handles, unbounded artwork queue/cache growth or extra package pins beyond the eight-mount cap.
- At 16:9, 21:9, a small window and a non-default DPI, the player and clickable bounds align and
  avoid guest prompts/rows. With `settings_help_overlay` off it still draws. Leaving the main menu,
  loading or entering gameplay clears the player and its input regions.

Build release before any live acceptance. Before navigating/driving the game UI, offer the owner
the choice to perform the test and wait for their answer, unless they have authorized autonomous
UI testing for that execution scope (`AGENTS.md`). A docs-only plan update does not run these boots.

**Evidence.** `out/_swap/music-nav/p3-footer/` (16:9 + 21:9), `p3-acceptance/`, `p3-player/`
(layout/DPI + click logs), `p3-artwork/` (valid/missing/corrupt covers, delayed completion, cache/lease and teardown logs). Record which checks were run by the
owner versus automation, and keep unmeasured behavior explicitly unproven.

## 6. Acceptance matrix (summary)

| Arm | Setting | R9 | Expected |
| --- | --- | --- | --- |
| `p1-menu` | on / off | on | switch, Interface Mapping submenu and action page always present; identical authored trees |
| `p2-media` | on | on | next, prev(<5 s), prev(≥5 s), pause/resume |
| `p2-play-current` | on | on | Play Current Song starts the navigated song |
| `p2-off` | off | on | keys inert; footer/player absent; no player art jobs or mouse capture |
| `p2-title-tracks` | on | off | title tracks navigate and pause |
| `p3-footer` | on / off | on | retail A/B footer unchanged; no LB/RB labels or spacing edits |
| `p3-player` | on | on / off | title/artist/cover fallback, three mouse actions, confirmed state, no click-through, gate/modal/focus checks |
| `p3-artwork` | on | on | real covers in single/merged packs; missing/corrupt fallback; delayed-result rejection; bounded cache and read leases |
| `p3-layout` | on | on | 16:9, 21:9, small window and DPI: player/bounds aligned, footer/rows clear, help captions off still works |
| `p3-acceptance` | on | on | full route + OCR, automatic updates, lifecycle soak, ctest, check_docs |

## 7. Documentation obligations

| Document | Why |
| --- | --- |
| `docs/engine/toggles.md` | New cvar `enhancements_background_music_nav`, keyed by its cvar name (no `R<n>` id). |
| `docs/engine/main-menu.md` | Part 2 owns the background music; add navigator/player, two-way bridge and local artwork lifecycle; part 1 §7.2 the manual-skip rule for Play Current Song. |
| `docs/engine/input-flow.md` | The main-menu button capture, media-key patch, shared command seam and player mouse hit-testing/capture rules. |
| `docs/settings/plan.md` | §3.2 (Controls → Interface Mapping gains the submenu) and §3.5 (General → Interface gains the switch) — **P1**, the prompt that lands the rows. |
| `docs/settings/status.md` | One at-a-glance bullet. |
| `docs/reference/known-issues.md` | Only the measured limits (Start consumption, host-drawn shoulder labels, Windows media-key arbitration, pause if unavailable). |
| `docs/plans/in-progress/README.md` + `docs/README.md` | Index rows — corrected 2026-10-26 to point at `in-progress/`; `finished/` on delivery. |

## 8. Risks and open questions

| Id | Question / risk | Resolution |
| --- | --- | --- |
| OQ1 | Is there a pause seam at all? | D5's ladder; a failed seam is a documented limitation, not a fake pause. |
| OQ2 | Does the guest's own poll restart a paused stream or tear it down? | Resolved for the verified stream by the Play ownership guard; `rb_blitz_367.log` retains state 5 and the same sample cursor across 7.285 s (§17). |
| OQ3 | Does Start do anything retail on the splash screen? | P2 control boot with the setting off: press Start, read the action line. If it starts PLAY, the navigator still consumes it only while it advertises the prompt (D6). |
| OQ4 | Are media keys delivered to this window at all on Windows? | P2: log the latch; if the OS shell swallows them, Start/LB/RB remain and the limitation is recorded. |
| OQ5 | Does the pre-press title screen also report `splash_screen`? | Gate on the option list being drawn (`VisibleMenuRows`), not on the name alone; P2's off-arm proves the title ignores the keys. |
| OQ6 | Do LB/RB conflict with a General Mapping row? | Interface Mapping's shoulders have no visible row (compiled defaults only), so the main-menu capture shadows nothing a player can see; recorded in known-issues. |
| OQ7 | Footer/player geometry at 21:9, small windows and DPI | P3 layout captures and hit tests; named measured constants and no guest-row/footer overlap. |
| OQ8 | Which exact local asset names identify a song's cover in single-song and merged packages? | P3 inspect both, retain an exact package-relative locator; fallback for missing/unsupported art. Resolver unproven. |
| OQ9 | Does the current ImGui backend expose arbitrary-image upload/release? | P3 identify and measure the renderer API, lifecycle and supported backends; ship an SDK patch if needed. Caption drawing is not proof. |
| OQ10 | Can a player mouse event reach the guest before ImGui capture is updated? | P3 verify listener ordering and consume hits before guest queuing; prove no click-through/hover leakage under modal/focus transitions. |
| OQ11 | Can delayed art outlive a skip, retired mount or session reset? | Epoch/song/art-key tags, owned bytes, short read leases, bounded caches; delayed-completion and teardown acceptance in P3. |

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

The later owner request on 2026-10-26 adds the explicitly authorized bottom-right player **beside
the planned footer**, with clickable transport, title/artist and album art (R10–R13, D11–D14,
§4.6–§4.8). The snapshot/command bridge, metadata retention, artwork loading/renderer integration
and mouse routing remain future P1–P3 work; none is claimed as part of the delivered foundation.


## 11. P1 implementation record (2026-10-26)

**Delivered in the tree; live acceptance pending.** The owner requested P1 and will perform the
manual game checks. No game UI was driven for this delivery.

- Setting id **53**, token `interface.background_music_navigation`, default **true**, native
  Interface boolean, explicit **restart** strategy and carrier `enhancements_background_music_nav`.
  The enhancement log has 13 entries and names the P1 surface/runtime boundary.
- General → Interface has five rows; Menu Music Navigation is fifth, after Help & Options Labels.
  Interface Mapping always has six rows: Background Music Play is fifth, Reset All last. Its
  three command rows use direct `file_exists` calls, with no settings write/commit or remapping.
  The owner follow-up keeps the submenu, page and in-song twin visible even when navigation is
  disabled (§12), superseding the original P1 hide-when-off behavior. The switch also stays visible.
- SDK-free `menu_music_nav` implements rising-edge sampling and Previous's 5 s/first-song rule.
  `NowPlayingBridge` owns the synchronized snapshot and one-command mailbox, with request ids,
  explicit accepted/busy/unavailable/stale answers, completion/failure, epoch reset/cancellation
  and tagged art-status rejection. Its default availability is false; no production input or
  guest command execution is wired in P1.
- `PlaySelection::Skipped` clears old-song grace for a manual transition; captured presses retain
  their original selection. Existing `MusicSession` history remains the single playlist/cursor.
  Catalog entries own title/artist and an artwork-locator field; preparation retains parsed
  display metadata, and P3 will resolve the currently empty art locator. No artwork worker/GPU
  resource or host player is created in this prompt.
- Focused regressions cover on/off authored trees and destination validity, plain command bodies,
  row/label budgets, persistence/restart classification, held-control gates, queue lifecycle,
  coherent concurrent snapshots, stale session/art rejection, cold-target cursor preservation,
  shared backward/forward history, retained metadata after retirement and manual-skip grace.

**Manual checks after the release build:**

1. General → Interface: Menu Music Navigation is fifth, its On/Off value fits and its caption
   appears with Help & Options Labels enabled. Back returns correctly.
2. With it On or Off: Controls → Interface Mapping has six rows, Background Music Play
   fifth and Reset All last. Open the page: Next Track / Previous Track / Play/Pause, title
   BACKGROUND MUSIC PLAY; Back returns to Interface Mapping. Presses are safe and do not alter
   the music in P1. No new footer/player or pad/media behavior is expected yet.
3. Turn it Off and revisit Controls → Interface Mapping: the submenu remains fifth and its
   page stays accessible, without restarting to reveal it. After a normal restart, the switch
   still reads Off and the same six-row mapping page remains. Ordinary music/navigation work.
4. Re-enable it; the page remains present throughout. An optional explicit
   `--no-enhancements_background_music_nav` boot must also retain the submenu/page and show the
   switch as externally configured rather than silently overriding the flag.

**Verification.** `cmake --build --preset win-amd64-release` succeeds, with all final source
and test changes included (`out/_swap/music-nav/p1-menu/build-release-final.log`). The complete
release host roster is **83/84**: every settings/music test passes; the only failure is the
unchanged, previously recorded `path_policy` assertion that a nonexistent descendant is inside
its root (`tests/path_policy_tests.cpp:88`; prior record in `docs/engine/main-menu.md` part 2).
`tests-release-full.log` records that run, including a passing SDK patch-stack audit. The direct
command-page dump is `command-page.dta`; `manual-checks.txt` repeats the owner checklist.

The documentation checker reports **43 existing errors / 94 warnings**, matching
`docs-before.json`; comparison of check/file/detail identities finds no new errors or warnings
(`docs-after.json`). `git diff --check HEAD` passes. Screenshots and live boot logs are still owed
by manual acceptance; do not infer them from host tests.

The full Vulkan-off debug host-target build hit a Windows permission error while parallel
post-link steps copied the same `rexruntimed.dll` (`build-debug-all-tests.log`). A serial retry
was stopped before completion because it began rebuilding the unrelated SDK dependency set;
no full debug-roster pass is claimed. The six relevant SDK-free P1 targets were then rebuilt
directly and successfully (`build-debug-final.log`). All **six** pass in the Vulkan-off debug
tree (`tests-debug-final.log`): `settings_screen`, `menu_music_pool`, `menu_music_nav`,
`menu_music_session`, `settings_service`, `settings_hot_apply`. The new SDK-free navigator/bridge
suite contributes 122 checks. P1 host validation is complete; live menu/restart acceptance stays
with the owner.


## 12. Always-visible command page (2026-10-26, owner follow-up)

The owner requested that **Background Music Play always remain visible**, avoiding a restart
merely to discover its controls. This supersedes the original P1 hide-when-off rule; the
requirements, prompts and manual checklist above now describe the revised behavior.

Removed the conditional row/page erasure from `AllPanels()`. Interface Mapping now always has
six rows, Background Music Play fifth and Reset All sixth, including the command page's in-song
twin. The boot log reports the page as always present regardless of the effective switch.
`MenuConfiguration::background_music_navigation` remains a boot fact, not a visibility gate.
The runtime switch's restart classification is retained until P2 proves live source setup and
input/pause ownership. Commands remain inert in P1; P2 must enforce the off gate even though
their page is now always reachable.

**Verification:** release build succeeds; `settings_screen` and `menu_payload` both pass. The
updated existing screen regression compares compiled On/Off definitions byte for byte, checks
all three action rows in both contexts and validates destinations with the switch off.
`git diff --check HEAD` passes. Documentation findings remain **43 errors / 94 warnings**, with
no introduced findings. Evidence: `out/_swap/music-nav/p1-always-visible/` (`build-release.log`,
`tests-release.log`, `docs-before.json`, `docs-after.json`). No game UI was driven; visual
confirmation remains manual. After turning the switch Off, revisit Controls → Interface Mapping
without restarting: the fifth-row submenu, its three commands and Back route must remain present;
an explicit off-flag boot must also retain them.

## 13. Loading/playback acceptance follow-up (2026-10-26)

The owner's last boot, `out/build/win-amd64-release/logs/rb_blitz_362.log`, exposes two false
watchdog failures in the existing background-music implementation, independent of P1's UI:

- Load staged `EdT_SU_BEP` at 12:26:12.832. At 12:26:40.838 the watchdog rejected it with
  **stream=0**, before the title called Start. The startup deadline now begins at Start,
  rather than charging the panel's deferred startup to decoding. Repeated Start calls do not
  reset an already active watchdog.
- `traininvain` was audible at 12:26:47.551. During graphics preparation the same stream
  (`408305A8`, state 3, position 2392064) was rejected at 12:27:39.980 and replaced by
  `gimmesometruth`, just before the gate released at 12:27:41.179. Native loading polls and the
  active loading gate now defer startup/stall deadlines while retaining the stream, sample
  position, selection and session cursor. A renderer call can itself block, so the ShellMusic
  poll checks the gate after preparation calls as well. EOF/replacement detection remains live;
  normal 15-second startup and 3-second stall limits resume after loading.

Cache publication only warms successors; it never starts a replacement merely because a package
is ready. The existing temporary title-fallback recovery still applies to a genuinely failed
source. The production-hook regression covers deferred native startup, ready successors plus a
loading stall, and resumption on the original stream without another Load/Start/destructor.
SDK-free checks cover watchdog recovery, subsequent real failure, EOF and foreign-stream rejection.

**Double audio:** the log proves successive track replacements, not simultaneous audible copies.
The native handoff destroys and clears the prior stream before starting its successor. P1 adds no
audio engine or runtime input handler. A listening check is still needed to establish the owner's
reported effect; neither code inspection nor synthetic native callees prove the actual audio mix.

**Slow boot, separate work:** the menu-music startup mount took **531 ms**, with one package.
The same log spans 12:24:31.401 to a usable menu at 12:27:41.299 (about **190 s**). About 92 s
elapsed between the pool becoming ready and the first guest music request (101 s from launch); this interval precedes
the rolling worker. The native content pass took **45.794 s**, enumerating 1,777 packages twice
(3,554 successful entries), with zero metadata reads and a 38 ms song-cache read. Graphics preparation
then took **5.663 s**, including a repaired recipe layer and native pipeline-cache restore.
The store's separate boot index worker (`src/store/store_library.cpp`, `Build`/`InstallStoreLibrary`)
was opening the full DLC library throughout startup, independent of the bounded music worker.
That is a concrete source of competing disk I/O; its exact contribution to the unusually slow
pre-menu interval is unmeasured. Do not attribute that whole interval to it without profiling.
Persistent/deferred store indexing and native enumeration/startup profiling belong to the separate
performance session requested by the owner; no store/content/graphics optimization is included here.

**Host verification:** the release build succeeds with the final changes; all nine targeted
music/menu ctests pass, including `menu_music_runtime`. Documentation findings are identical
before/after this follow-up (43 existing errors / 89 warnings), and `git diff --check` passes.
Evidence: `out/music-followup-build-final.log`, `out/music-followup-tests.log`,
`out/music-followup-docs-before.json`, `out/music-followup-docs-after.json`. Game/UI testing awaits
the owner's choice; the listening report remains unconfirmed.


## 14. P2 implementation record (2026-10-26)

**Implemented; live audio/UI acceptance pending.** This supersedes P1's command-only
surface and earlier statements that runtime wiring is future work. The switch remains
restart-scoped. No game UI has been driven for this implementation.

- Every producer posts to the P1 mailbox; only the ShellMusic poll executes commands.
  It revalidates modal/capture/loading ownership and producer scope, then reports applied
  after the replacement stream is observed playing. Cold targets are pinned and remounted
  asynchronously without moving the cursor or replacing the current audible metadata.
  Failures, stop and shutdown cancel or fail pending requests explicitly. Automatic
  advances publish through the same owned snapshot.
- Next/Previous share `MusicSession` with automatic advancement. Previous restarts at
  220,500 samples of progress since the first playing observation (five seconds) or the first entry; otherwise it follows history.
  Forward after Previous follows the existing history. Manual handoff clears old-song
  grace in Play Current Song without changing an already captured selection.
- Navigator-only boots allocate guest path buffers before R9's early exits. The native
  title path is adopted as the first of three title sources, with native looping and
  neutral display metadata. No title source becomes a DLC Play Current Song selection.
  The staged watchdog fixes in §13 remain intact.
- Raw physical pad/key action bindings are sampled before General Mapping. Supported
  sources are consumed until release; held inputs do not fire on menu/device/configuration
  boundaries. The splash option list must be drawn, and input yields to modal/capture
  owners. Keys obey Keyboard Support/focus. Unavailable pause leaves Start untouched.
- `[menu_music]` in the existing launcher profile owns `next`, `previous`, `pause`,
  separate from `[remap]`. Defaults: RB/MediaTrackNext, LB/MediaTrackPrevious,
  Start/MediaPlayPause. Three guest binding rows open Assign / Reset pages
  in both contexts. Existing release-first capture and its three-second countdown
  support cancellation without a write. First assignment replaces defaults; later
  assignments append distinct inputs. Saves rebase on disk and publish a new immutable
  snapshot. All pages remain visible Off; commands cannot bypass the runtime off gate.
- SDK patch **0041-media-keys.patch** and the host key latch translate all three media
  keys. `drive_ui.ps1` adds `media_next`, `media_prev`, `media_play_pause` with extended
  scan codes. `background_music_nav_probe=next|previous|play_pause` posts once after
  `background_music_nav_probe_delay` seconds (default 5) on the main-menu option list.
- **Pause limitation (D5 fallback):** no guessed state-4/vtable write ships. Silence,
  frozen samples and same-position resume require a live measurement. Capability is
  false; the guest row/action says **Play/Pause: Unavailable**, the command returns
  false and Start retains retail behavior. P3 must disable its center control with
  this explanation unless a native seam is subsequently verified. No fake paused
  state or mute/restart-as-resume is published.

Evidence: `out/_swap/music-nav/p2-runtime/`. Host tests call production hooks with
synthetic native callees; they do not measure the actual audio mixer or navigate the UI.
Windows media-key delivery/arbitration, audible continuity, physical-pad input, page layout
and assignment/restart routes remain unmeasured. Footer/player/artwork remain P3.


**Owner acceptance and corrections (2026-10-26).** The owner reports the buttons work,
but Previous could not return to the boot song. `rb_blitz_364.log` shows successful Next
requests through cursors 1/2, then repeated Previous restarts at cursor 2 and busy replies
to presses during each approximately 2.3-second replacement load. History was not removed.
Static native inspection shows `ShellMusic::Start` calls the random-offset selector
`0x82757D40` (the offset vector is at ShellMusic +156/+160), and `StandardStream +0xA0`
includes the initial offset and decoded-ahead reserve (`0x823E52B0`, `0x823E3D60`). Thus an
absolute cursor is not a measure of how long this selection has played.

`PlaybackAge` now uses positive sample deltas since the first playing observation, resetting
on every native/manual handoff even if the allocator reuses the stream address. Manual
Next/Previous/restart alone override the native offset selector to zero; automatic/native
starts retain their existing offsets. New diagnostics record raw cursor, elapsed samples,
source cursor and target. Synthetic regressions include a 30-second native random offset,
six seconds of initial decoded reserve, consecutive back/forward moves reaching entry zero,
and same-address restart. The fix still needs an owner listening/retest; no live run of this
revision is claimed. At this revision, a press while another command was pending was busy rather than queued;
§15 supersedes that behavior after the owner's next test.

The owner also removed the execution button from Interface Mapping: each music binding now
opens only **Assign / Reset**. This supersedes the earlier Play Now and P1 one-shot command
surface descriptions in R5/D9 and §5. The internal command channel remains for shared runtime
execution, but mapping pages configure inputs and do not execute playback actions.


**Host verification.** The release build succeeds. All **13 targeted release tests** pass
with the owner corrections, including the production-hook runtime fixture, navigator,
input bindings, settings surface and profile round-trip. An earlier full release run
passed **85/86**, including the SDK patch-stack audit; its sole failure is the existing
`path_policy` assertion at `tests/path_policy_tests.cpp:88`. The final targeted run covers
the subsequent sample-age/random-offset and Assign/Reset corrections; the full roster
was not repeated after those changes. Documentation remains at **43 existing errors /
87 warnings**, with no introduced check/file/detail identities. `git diff --check` passes.
Builds, tests, doc snapshots and the owner log excerpt are in the P2 evidence directory.

The final Vulkan-off debug subset also passes **6/6**: settings_screen, menu_music_pool,
menu_music_nav, menu_music_session, profile and input_bindings (`tests-debug-final.log`).
The final release rebuild includes the owner corrections; no game UI was driven by the agent.


## 15. Previous during loading: bounded command bursts (2026-10-26)

The owner reports that Previous still restarted the current song, including rapid double/triple
presses. The next owner log, `out/build/win-amd64-release/logs/rb_blitz_365.log`, separates the
accepted requests from the lost presses. At 13:40:07.113 the accepted restart had 327,680 elapsed
samples (about 7.43 seconds); the next press at 13:40:07.891 returned busy before that restart
finished at 13:40:09.461. At 13:40:17.729 another restart had 376,832 elapsed samples (about
8.55 seconds), and both follow-ups at 13:40:18.023 / 13:40:18.221 returned busy before completion
at 13:40:20.080. The earlier sample-baseline correction did not preserve those presses.
These readings do not establish a single accepted before-five-seconds request in that log;
the owner's reported behavior remains the acceptance requirement.

Production `nav::Post` now opts into a FIFO of **four waiting requests plus one active request**.
Each accepted press gets its own id and session epoch. Snapshots expose the waiting requests;
posting does not claim completion or change the audible identity. Strict bridge callers retain
the old busy-on-pending API unless they explicitly opt into queuing. A full FIFO returns busy.

A Previous posted during another navigation or a loading transport captures step-back intent.
When the guest executes it, delayed package/decoder work cannot reinterpret it as another
five-second restart. An ordinary Previous while playing retains the sample-age rule. Thus a
double Previous after five seconds means restart then step back; a triple can reach two earlier
entries, including the boot song. Execution remains on the guest poll, one completed handoff
at a time. Failed/cancelled commands, session reset, teardown, lost focus, modal/capture/loading
ownership and producer scope invalidate the pending burst. No UI was driven by the agent.

**Verification:** release build succeeds; all **seven music ctests** pass. The production-hook
fixture simulates a decoder that stays loading across polls, queues two Previous presses,
confirms the restart, then delays execution past five seconds and still reaches history entries
1 and 0. It also checks focus-loss cancellation. SDK-free bridge checks cover FIFO order,
request ids, capacity, unchanged audible metadata, reset and failure cancellation. Evidence:
`out/_swap/music-nav/p2-previous-burst/`. Owner listening/retest of this revision is pending.


**Owner-driven runtime follow-up:** the owner launched the successfully built executable
while the final comment-only cleanup was being checked. `rb_blitz_366.log` confirms a
Previous pressed at 13:49:21.911 during request 3's restart was accepted (status 0).
After request 3 completed at 13:49:22.815, request 4 selected target **0** from cursor 1
at 13:49:22.833 (`elapsed_samples 0`), loaded the boot song `suddenlyisee` and applied
at 13:49:25.199. This is live execution evidence for the formerly dropped follow-up;
it is not a listening confirmation or a claim that the agent navigated the UI.
The current executable includes all functional changes. A redundant relink after header
comment cleanup hit Windows' running-executable lock; no additional behavior change was
waiting to be built. The debug navigator suite also passes, and documentation retains
43 existing errors / 87 warnings with no introduced findings. `git diff --check` passes.


## 16. Native pause/resume trial (2026-10-26)

**Historical trial record; §17 supersedes the default-off/unavailable status below.**

The owner's P2 completion review highlighted that the unavailable fallback is not
working pause. Navigation/bindings are implemented; normal Play/Pause remains
unavailable until live transport acceptance succeeds.

Static inspection of the decrypted retail image and StandardStream RTTI vtable
`0x82077594` found a concrete seam, superseding the earlier state-4 candidate:

- Slot 7 (`+28`, `0x823E3FD8`) calls `0x823E96B0` for every channel and changes
  stream state **3 → 5**. The channel routine calls its virtual pause method with
  argument 1; it does not destroy or seek the stream.
- Slot 6 (`+24`, `0x823E4808`) resumes channels through `0x823E9640`, whose paused
  channel branch calls that same method with argument 0, and sets stream state **3**.
- The ShellMusic poll's `!IsPlaying && IsReady` branch at `0x82758834–0x827588A0`
  would immediately call Play on state 5. An ownership guard in the Play hook now
  suppresses that call only for the navigator's explicitly paused stream. An explicit
  resume bypasses the guard; unrelated streams retain native Play behavior.

The default-off diagnostic flag `--background_music_nav_pause_trial` enables the
candidate through the existing pad/media action bindings and command bridge. The
normal binding page still says Unavailable: this flag is a measurement tool, not
a claim that release pause acceptance has passed. No new setting or overlay was
added. Pause/resume commands run on the guest owner, preserve selection/history,
publish the observed state, and report a failed native state transition as failed.
The watchdog is deferred throughout owned state 5 and on resume; a manual skip
retires pause ownership and starts its successor normally. Stop/teardown also clears
ownership. Title-fallback recovery cannot replace an intentionally paused source.

Logs tagged `music_nav: pause trial` record the pause/resume stream, state, raw
sample cursor and paused baseline, plus a hold observation each second. The live
test must verify silence, at least five seconds of frozen progress despite native
polls, then continuity from the same position after resume, for DLC and title-only
sources. No live silence/frozen-progress/resume result is claimed yet. The agent
asked the owner to choose who performs the game/UI test, as required by AGENTS.md;
the owner chose to test the pause trial themselves. Live acceptance remains pending.

**Host verification:** `cmake --build --preset win-amd64-release` succeeds, and all
13 targeted release tests pass. The production-hook fixture covers both source
types, native poll auto-resume attempts, a watchdog aged by 60 seconds, unchanged
identity/cursor/sample position, resume, failed pause, Next from pause, teardown
and unrelated Play calls. Its native audio callees are synthetic; these tests do
not prove the actual mix is silent or that the real sample cursor freezes.
After adding explicit native Load/Start ownership resets and the supported-Start
edge regression, the final release rebuild and both affected tests
(`menu_music_nav`, `menu_music_runtime`) also pass. Static seam dumps and the final
test log are under `out/_swap/music-nav/p2-pause-trial/`. `git diff --check` passes.


## 17. Owner pause acceptance and normal enablement (2026-10-26)

The owner tested the trial themselves and reported **“it works!”**. Their
`out/build/win-amd64-release/logs/rb_blitz_367.log` supplies the native evidence:

- At 14:38:20.905, stream `40830638` changed state **3 → 5**, cursor **3555328**.
  Seven one-second hold observations retained that exact cursor and state 5.
  At 14:38:28.190 it resumed **5 → 3**, still at **3555328**: a **7.285-second**
  hold without a reload, restart, watchdog failure or guest-poll auto-resume.
- Three subsequent pause/resume pairs used the same stream. One pause allowed
  one in-flight decoder block (4096 samples) to finish before the cursor settled;
  it remained stable thereafter and resumed at that settled position. This is
  not a promise that asynchronous decoding stops mid-block.

Native pause/resume is now enabled whenever Menu Music Navigation owns an eligible
playing/paused source; no trial flag is required. The legacy
`--background_music_nav_pause_trial` flag remains accepted solely to request
per-second paused-cursor diagnostics. The default mapping row now shows
**Play/Pause: Start, Media Play**, including the in-song mapping page. The existing
off gate and main-menu input scope still apply.

`PlaybackAge::Hold` excludes the final paused decoder block from Previous's
five-second progress counter. A failed native resume retains pause ownership
and publishes failure plus the actual paused state, so the guest cannot silently
auto-resume after a rejected command. Tests cover both behaviors with synthetic
callees, plus normal pause without the diagnostic flag and the existing title/DLC
transport, skip, watchdog and teardown cases.

**Verification:** the final release build succeeds, and all **13 targeted release
tests** pass, including settings rows and production runtime hooks. Evidence:
`out/_swap/music-nav/p2-pause-trial/tests-accepted-release.log` and
`owner-367-pause.log`. No game UI was driven by the agent. The log demonstrates
DLC playback; a separate title-only live boot, physical-pad/media-device attribution,
and the broader assignment/restart/layout matrix are not established by this report.
P2 implementation is complete; those acceptance limits remain explicit, and P3's
footer/player/artwork work remains outstanding.
The final caption/boot-log cleanup was rebuilt successfully; `settings_screen`
and `menu_payload` were rerun and both pass (`tests-final-labels.log`). Documentation
validation remains at 44 existing errors / 87 warnings, with no introduced finding
identities, and `git diff --check` passes. The additional existing link error is
the concurrently moved button-layout plan, outside this music change.


## 18. P3 implementation and owner test handoff (2026-10-26)

**Implemented; live visual/mouse acceptance awaits the owner.** The owner selected
“I’ll test the build myself” after the release build succeeded. No game/UI was driven by the
agent for P3. The historical P1/P2 “footer/player/artwork remain P3” statements above are
superseded by this section; earlier one-shot mapping-page and unavailable-pause descriptions
remain historical, superseded by §§14 and 17.

- `help_overlay.cpp` draws shoulder footer text before the caption early-out. The separate
  `menu_music_player.cpp` dialog draws a compact translucent bottom-right panel, square cover,
  owned title/artist, tooltips, UTF-8-safe ellipsis and three transport controls. It shares the
  player-facing font and does not depend on Settings Labels. Main-menu/list/source, switch,
  focus, modal/capture and loading gates are checked at draw and event time. The center icon
  follows confirmed transport; loading-state pause is disabled. Accepted commands show pending,
  never an optimistic transport change; failures return through the existing snapshot.
- Mouse events route at the native listener before raw latches, guest hover/Select/Back/wheel
  queues and keyboard/mouse emulation. Press/release on the same enabled left control posts once;
  hold, drag-out, disabled and right controls cannot post. A consumed sequence owns its release
  across visibility/focus/modal/session boundaries. Layout/DPI changes invalidate stale bounds.
  Mouse Support off leaves the player visible with disabled pointer controls. No new execution
  button was added to the owner’s Assign / Reset mapping pages.
- Exact package-relative covers are resolved from the full stream directory and basename,
  retaining actual entry spelling in the stable catalog. Preparation skips the artwork walk
  when navigation is off. The bounded worker reads through existing cache devices with short
  native File leases; artwork never creates another package mount or evicts active audio.
  Cold reads defer to playback’s remount. It copies at most 2 MiB encoded bytes, validates input
  dimensions before decode (1024×1024 maximum), caches eight 256×256 RGBA covers (2 MiB), and
  queues one latest job behind one running job. Epoch/song/art-key tags reject late completions.
  Hide/reset/off clears pending work and owned visible pixels/texture. Missing/invalid art and
  title tracks use the neutral disc cover. There is no network/store dependency.
- Real local probes found version-1 Xbox covers, exposing a shared-codec limitation before UI
  testing. `texture_codec.cpp` now reads the v1 signature at byte 1, with dimensions four bytes
  before v2’s fields; the envelope remains 32 bytes. A single-song package decoded **1/1** cover,
  and the merged Blitz package decoded **25/25**, all to 256×256. Every read’s File count was
  **1 → 0**. Decoded +44 and Some Nights covers were visually inspected outside the game.
  This proves local resolution/read/decode, not GPU rendering.
- SDK patch **0042-imgui-image-textures.patch** exposes drawer-owned image create/release and
  generation APIs. It delegates RGBA upload to the same `ImmediateDrawer::CreateTexture` used by
  the font atlas and rendering to the existing ImGui `ImmediateTexture*` draw-list path. D3D12
  and Vulkan already defer destruction to last-use submission fences. Calls run on UI draw;
  replacement clears textures and advances the generation. The API limits eight 512×512 images;
  the player holds one 256×256 image and reuploads owned pixels after replacement. The numbered
  patch passes exact pristine-HEAD stack replay; no untracked SDK edit is required.

Evidence is under `out/_swap/music-nav/p3-acceptance/`: release build logs, music/full test logs,
real-package cover probe logs and decoded samples, SDK replay audit, and before/after docs reports.
The host player suite covers pointer geometry/DPI, one post per release, disabled controls,
drag/session/resize/cancel boundaries, exact path resolution, v1/v2 decoding, corrupt/bounded
inputs, delayed completion rejection, latest-job replacement, disable/reset, retry and LRU eviction.
The production ShellMusic fixture continues to cover guest command completion and native transport
with synthetic callees.

**Still required from the owner:** actual player/GPU cover rendering for both package shapes,
automatic/manual metadata changes, Previous on both sides of five seconds, pause/resume clicks,
no click-through/double-posts, outside-player routing, focus/modal/mouse-off/nav-off behavior,
16:9/21:9/small-window/non-default-DPI captures, and repeated-skip/refresh/renderer-teardown soak.
Footer baseline and prompt-cluster constants remain provisional until those captures calibrate
them. A title-only live boot and the broader P2 physical-pad/media/assignment/restart matrix also
remain unmeasured. Do not move this plan to `finished/` or claim P3 live acceptance on host tests.


**Final host verification:** `cmake --build --preset win-amd64-release` succeeds with the
final DPI guard and production artwork-read regression. All **eight music suites** pass;
the full release roster is **86/87**, with only the previously recorded `path_policy`
assertion at `tests/path_policy_tests.cpp:88` failing. The obsolete version-1 refusal
expectation was replaced with actual v1 parse/decode coverage; `store_art` passes. The
Vulkan-off debug subset (`menu_music_player`, `menu_music_nav`, `store_art`) is **3/3**.
Documentation reports **43 existing errors / 87 warnings**, with no new check/file/detail
identities against the 44-error / 87-warning baseline (an unrelated plan edit removed one
existing finding). Both working-tree and staged `git diff --check` pass. Evidence:
`build-final.log`, `tests-full-final.log`, `tests-debug-final.log`, `docs-before.json`,
`docs-after.json`, `art-single.log` and `art-merged.log` in the P3 evidence directory.
The owner closed the running game for the final relink; the current binary includes all
functional changes. No commit/push was performed by this agent.


## 19. Owner P3 review: search theme and native footer (2026-10-26)

The owner reports **“It mostly works”** and confirms the album cover is present live even
though their capture shows the neutral fallback. They requested no hover text, a lower player,
the search interface’s Blitz styling and a **real guest footer modification**, superseding D7’s
original host labels and §18’s initial blue player/footer implementation.

- The player now reads the existing keyboard/search theme directly: condensed/bold fonts,
  dark translucent fill, three layered red rails/glow, pale text and red button feedback.
  The reference bottom inset changes **105 → 24 pixels at 720p**. Drawing and pointer bounds
  use the same updated layout. All transport and metadata hover tooltips are removed.
- Host footer drawing is deleted. `AddNativeMusicFooter` extends the authored splash panel’s
  existing `continue.ihp` / `back.ihp` action lists through the native InlineHelp API. The lists
  become Previous/PageUp, Confirm and Cancel, Next/PageDown. Original `inline_help_continue`
  (verified locale text **SELECT**) and `inline_help_back` remain; new locale entries supply
  PREVIOUS / NEXT. Native glyphs/font/layout draw the prompts. Only availability transitions
  update them; source/menu/modal loss and exit remove shoulder actions and retain A/B. Off boots
  add no footer script. The `rbbz-play/footer` read-only query never posts a playback request.
- Static native inspection verified `SetActionToken` (`0x824DABE8`) searches existing actions,
  updates or appends, and calls the label refresh; `ClearActionToken` (`0x824D9F20`) removes and
  refreshes. The real splash scene declares both InlineHelp widgets. Guest lifecycle tests
  round-trip the authored DTB, execute availability/on/off/repeated-poll/exit paths, check action
  order, preserve provider handlers and reject incompatible/duplicate authoring atomically.
  Production runtime tests cover footer query gates without generating transport commands.

**Verification:** the release rebuild succeeds; all **12 targeted release suites** pass.
A read-only payload build against the extracted game records passes **2,654 checks**, all eight
store/messages/recommendations combinations, with **zero refused transforms**. The Vulkan-off
debug player/menu-payload suites also pass. Evidence: `build-owner-followup.log`,
`tests-owner-followup.log`, `guest-records-owner-followup.log` and
`tests-debug-owner-followup.log` under `out/_swap/music-nav/p3-acceptance/`. Earlier full-release
validation remains 86/87 (existing path-policy failure); it predates this follow-up. The agent
has not navigated the game. Native prompt spacing/glyph layout and the lowered theme now await
the owner’s next visual test; broader DPI/soak acceptance remains pending.


## 20. Owner review: quiet player, native spacing and cursor (2026-10-26)

The owner found the popup’s red glow too prominent and requested a black player, dark-gray
buttons and no border. They also reported native prompts stacking at a retail anchor and the
mouse cursor disappearing over the player. The last “LB” in the requested LB/A/B/LB sequence
is treated as RB/Next, consistently with the feature’s existing bindings and the stated RB/LB
request. No new overlay was added; the footer continues to use guest widgets exclusively.

**Cause and correction:** static native inspection found `InlineHelp::DrawShowing`
(`0x824D85E8`) advances label i by `i × spacing`, transformed by the widget’s scale. It never
advances by the preceding label’s width. Both splash InlineHelp instances are horizontal with
**spacing 0**, because each originally carries one action. Appending tokens alone stacks them.
The parsed local scene matrices have uniform scale **1.2**, A.x **−47.934883**, B.x **78.832283**,
and the same `splash` parent. The measured parent interval is **126.767166**; dividing by scale
produces native spacing **105.639301**. The corrected four anchors are approximately
**−174.702049, −47.934883, 78.832283, 205.599449**. Runtime authoring derives these values from
the live widgets rather than hard-coding this sample, retaining A/B’s original positions.

`AddNativeMusicFooter` now assembles all four actions on the first existing native widget,
clears the second widget’s Cancel token, and restores its token plus the first widget’s original
position, spacing and orientation when unavailable or exiting. Native `get_local_scale`
(`0x821F0F78`), `get_local_pos` / `set_local_pos`, and `InlineHelp` spacing-property synchronization
were checked against the local retail image. The native draw and property source excerpts and
raw-scene measurements are in `out/_swap/music-nav/p3-footer-layout/`. No host footer text or
host footer draw function remains.

The player keeps the condensed/bold fonts, black translucent fill and gray controls, with no
red rails, glow or border. Cursor hiding was a separate source-gate bug: player hover correctly
clears native mouse focus to prevent click-through, but the old cursor rule interpreted that as
having no pointer surface. `CursorVisible` now accepts player ownership independently of native
focus/ImGui capture; Mouse Support off still hides it, and native routing remains suppressed.

Regression tests execute the authored guest footer, verify four distinct native positions with
a non-unit scale and original zero spacing, preserve A/B anchors and verify teardown restoration.
Input-gate tests cover player-only cursor visibility and the mouse-off boundary. Read-only payload
builds exercise the real records. Live game/UI testing stays with the owner; this revision’s
visual spacing and cursor behavior require their next check.


**§20 host verification:** release build succeeds with the final native spacing and cursor
changes. All **14 targeted release suites** pass; the Vulkan-off debug `menu_payload`,
`menu_music_player`, `input_gates` subset is **3/3**. The actual extracted-record payload
build passes **2,661 checks** across all eight feature combinations with **zero refused
transforms**. Working-tree and staged diff checks pass. Documentation retains the previous
**43 errors / 87 warnings**, with no new finding identities. Evidence is under
`out/_swap/music-nav/p3-footer-layout/`: `build-release.log`, `tests-release.log`,
`tests-debug.log`, `guest-records.log`, `geometry-measured.json` and `docs.json`.
No live game/UI test was performed by the agent; the new build is ready for the owner’s
native-footer alignment and pointer-visibility check.

## 21. Owner removes shoulder prompts; RB handoff crash (2026-10-26)

The owner withdrew the LB/RB footer additions because they clutter the menu. This supersedes
§§19–20's native footer work: `AddNativeMusicFooter`, its build hook, locale tokens, query branch
and footer-specific tests are removed. The retail A/B layout is no longer changed. Pad LB/RB,
media keys and player controls retain their transport behavior. The black borderless player,
dark-gray buttons and cursor correction remain.

The owner confirms Start pauses, but RB Next crashed. `rb_blitz_372.log` records request 7
loading `menumusic1:\songs\treatyoubetter\treatyoubetter` at 16:01:07.555, the stream reserve
hook at 16:01:07.627, and a guest read of `0x00000004` at 16:01:07.655. It has no stack trace;
the only available Windows dumps predate this run. Read-only package inspection finds a v10
MOGG with an Ogg/Vorbis identification header at the declared offset (3396), ten channels and
44.1 kHz. That does not establish a decoder fault or prove the asset valid end to end.

A bounded first-chance logger now records near-null guest faults with module/RIP and host
frames, plus the active PPC context and stage when inside ShellMusic. Scratch contexts are
tracked through retire, stop, load and start. The logger returns `EXCEPTION_CONTINUE_SEARCH`;
it neither suppresses a fault nor resumes a damaged stream. It also covers decoder faults
outside ShellMusic on other guest threads. No game/UI test was driven by the agent. RB remains
unresolved until a reproduced fault identifies its function and a correction is verified.

Validation: `cmake --build --preset win-amd64-release` succeeded. All 14 targeted release tests
passed (music, payload, settings, input and store art/payload). The extracted-record integration
check passed 2612 checks over eight menu configurations with zero refusals. `git diff --check`
passed. The documentation checker retained its baseline 43 errors/87 warnings with no new
identities. Evidence: `out/_swap/music-nav/p3-skip-crash/` (`build-release.log`,
`tests-release.log`, `records.log`, `docs.json` and read-only package-header logs).
