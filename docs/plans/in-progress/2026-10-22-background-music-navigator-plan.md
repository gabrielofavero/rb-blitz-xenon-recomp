# Background music navigator — implementation plan

**Status:** P1 implemented (2026-10-26), manual acceptance pending (§11). The session/mount-cache
foundation was delivered separately. P1 adds the default-on restart setting, always-visible Controls
command page and SDK-free navigator/snapshot core. Command execution, pause, pad/media-key
wiring, footer and clickable music-player overlay remain P2/P3. Owner request of 2026-10-22 is
normalized in §1; the design is §4, the three execution prompts are §5.

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
| R4 | The main-menu footer, today guest-drawn as `A Select / B Back`, must read `LB Previous | A Select | B Back | RB Next`. |
| R5 | Controls → **Interface Mapping** always carries a **Background Music Play** submenu as the page's fifth row (between *Music Library Options* and *Reset All*, so Reset All stays last; the page goes 5 → 6 rows, which is the visible budget), opening a three-row page: **Next Track / Previous Track / Play/Pause**. The three are one-shot **commands** — a press fires `rbbz-play/next|previous|pause` through the guest's `file_exists` channel — not `[remap]` bindings: that table's targets are physical controls, while these are feature actions, so the player triggers them here but does not assign them. With the setting **off**, both the row and page remain visible; runtime commands are unavailable while navigation is disabled. |
| R6 | When the setting is on, a navigation must keep **Play Current Song** pointing at the song that is now audible, so pressing it starts exactly what is playing in the background. |
| R7 | With the setting off, behaviour is exactly today's: no interception, no footer prompts, no player overlay and no player artwork requests. Background Music Play remains visible; its commands cannot bypass the off gate. |
| R8 | Scope of the pad/media buttons and player overlay: the **main menu only** (the owner's footer), with the option list drawn and a menu-music source available. The player hides during gameplay, loading and other screens; it yields to modal dialogs/input capture. The three Controls-page actions work wherever the menu music is playing (they are the same commands) while the setting is on; with it off the page remains visible, but commands are unavailable. |
| R9 | Docs are part of the change (toggles, menu study, input-flow, settings plan/status, plan index). |
| R10 | Keep the LB/RB footer and add a **bottom-right music-player overlay**: square album cover, song title, artist and `<< / Play-Pause / >>` controls. Use a compact translucent panel, readable text and hover/pressed/disabled feedback, scaled and inset from the viewport edges without covering the guest footer or menu rows. |
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
| **D4** | **The 5-second rule uses the stream's sample counter** (`Stream+0xA0` / 44,100), not wall time. A track that is still loading counts as "< 5 s". "First song" means cursor 0 → restart. Next/Previous always resume playback if paused. |
| **D5** | **Pause is proven or reported, never faked.** **P2** must find a seam that (a) silences the stream, (b) freezes the reported position, (c) resumes at the same position, and (d) is not undone by the guest's own poll. Candidate ladder: the `StandardStream` message/vtable pause-and-resume call; else the stream state word (`Stream+0x14`, value 4 = the usual paused state) under a measured write; else the Play/Pause action is drawn as unavailable (`<label>: Unavailable`) and Start does nothing, with the limitation in `known-issues.md`. During a pause the `Playback` watchdog must be suspended or it marks the track failed after 3 s. |
| **D6** | **Buttons are consumed on the main menu only** (`splash_screen` *and* its option list drawn), while the setting is on and a menu-music source exists. Bits: LB `0x0100`, RB `0x0200`, Start `0x0010`; media keys ride the existing keyboard gate (`keyboard_support`). One command per physical press (rising edge), keyboard hold does not repeat. |
| **D7** | **The footer is host-drawn beside retail's, never a payload/locale edit.** `help_overlay.cpp` draws `LB Previous` immediately left of the guest's A prompt and `RB Next` right of B, same baseline and face, only while the setting is on, the main menu is current and a menu-music source exists. The guest's `A Select / B Back` is left untouched (its button glyphs are guest art; strings are ours and are recorded as such). |
| **D8** | **Play Current Song follows the navigated song through the existing probe, with one fix.** The probe answers at press time, so it already names the audible song; the 2 s "recent" grace in `PlaySelection` must be cleared on a *manual* transition or a press right after Next/Previous would name the song that just left. |
| **D9** | **Commands travel as host state, not guest calls.** The input filter only *posts* a command (`rbbz-play/next|previous|pause` from the Controls page, or a rising edge in the filter). The per-frame poll applies it on the guest thread, the same place R9's handover already runs. |
| **D10** | Nothing is committed or pushed without the owner's approval. |
| **D11** | **The authorized player supplements the footer.** A separate host ImGui player lives at bottom right, gated by the same navigator switch/main-menu/source facts and independent of `settings_help_overlay` or caption text. Keep the footer and guest A/B prompts readable. No extra player switch in this scope. |
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

The playback pause seam remains unproven. Instruments for P2, in order:

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

### P3 — The footer, clickable player, artwork, soak and documentation

**Goal.** Deliver the footer plus the authorized bottom-right music player, prove its two-way
bridge and local artwork path, complete the acceptance sweep and update the docs.

**Deliverable**

1. `src/ui/help_overlay.cpp`: `DrawMainMenuMusicFooter(io)` before the caption early-out; draws
   `LB Previous` / `RB Next` on the guest prompt band's baseline, only with the setting on, the
   main menu current and a source available; x offsets are the P3 calibration constants (measure
   the prompt cluster at 16:9 and 21:9 in one boot each).
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
   really limited: Start is consumed on the main menu; the footer's shoulder labels are host text;
   the media keys may be claimed by another Windows media app), this plan's status record.
7. Index: `docs/README.md` and `docs/plans/in-progress/README.md` already point at `in-progress/`
   (corrected 2026-10-26); when P1–P3 are green, `git mv` the plan to `finished/` and re-point every
   reference with `move_docs.py`.

**Acceptance.** One full boot with a capture read by OCR: the footer reads
`LB PREVIOUS … A SELECT … B BACK … RB NEXT` (OCR may drop the glyphs; assert on the words), the
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
| `p3-footer` | on / off | on | host footer / retail footer alone |
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
| OQ2 | Does the guest's own poll restart a paused stream or tear it down? | P2 boot: read `Stream+0x14`/`+0xA0` across 5 s while paused. |
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
