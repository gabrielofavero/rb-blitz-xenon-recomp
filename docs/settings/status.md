# In-game settings — status, evidence and remaining work

This is the single living record of where the in-game settings work stands: what ships today, the
owner punch list that landed on 2026-10-13, every open item by package, the 2026-10-12 black-startup
investigation, and the handover that preceded it. It consolidates three documents written in
`docs/plans/` (see [Sources](#sources)). Parts 2 and 3 keep the research history and are superseded by
Part 1 wherever they disagree.

## At a glance

- **Refresh Songs default (2026-10-28):** the main-menu shortcut defaults Off in both the settings
  schema and its cvar. Existing saved On choices still take effect.

- **Song Library (2026-10-28):** Cache Song Library was retired after matched
  boots showed no meaningful menu-load gain. Scan Threads remains. Refresh Songs
  enters native discovery and starts a host rescan on its worker before guest
  enumeration; the row no longer depends on the persisted cache.

- **Controller mapping (2026-10-28):** Controls lists Controller Presets, Layout, Interface Mapping,
  and Gameplay Mapping. Input Types was removed; keyboard and mouse input stay enabled even with
  legacy off values. Gameplay Mapping lists each action once and displays only its keyboard/mouse
  bindings beside each action name. Mapping pages hide the bottom settings caption. Add Key
  appends, Replace starts the binding list over, and Reset restores defaults.
  The first controller button capture asks for confirmation before saving; after confirmation,
  custom controller bindings disable preset input for every unconfigured action. Reset All restores
  both gameplay tables and the selected controller preset.
- **Layout selector (button-layout P3, owner correction 2026-10-27):** the native selector sits
  directly on Controls as `Layout: <name>` in both contexts. The boot-discovered choices,
  `settings.toml` token, missing-package fallback, and queued GPU texture refresh are implemented.
  The first live boot exposed a timing bug: the menu was authored before package discovery, leaving
  only Xbox 360 selectable. Discovery now runs before menu authoring. A second owner test confirmed
  the choices cycle but the A/B footer art stayed retail. The GPU log showed selection changes with
  no button-sheet composition: a retail boot had never marked the original sheet as a cache refresh
  candidate. The provider now tracks matching original button, pad, and icon textures even while
  Xbox 360 is selected. The owner retest shows One Glyph replacing A with the magenta fixture while
  B stays retail, One Pad leaving both footer glyphs retail, Test Markers replacing both, and Xbox
  360 restoring both. In `out/build/win-amd64-release/logs/rb_blitz_397.log`, the first One Glyph
  selection invalidated one texture candidate and composed/overrode the button sheet; the commits
  report no pending restart. The owner then confirmed One Pad turns Freakish's pad magenta while
  Default stays original (`rb_blitz_398.log`: `xbox_1` override, seven mips), and One Glyph survives
  relaunch (`rb_blitz_399.log`: selected and composed at boot, with a magenta start prompt).
  Five focused host tests pass. The separate layout page, keyboard/mouse info and next-start info
  rows were removed at the owner's request. P3 is delivered and owner-accepted on Vulkan.

- **Settings organization, Launch, and linked confirmations (2026-10-26):** General → Launch
  groups Game Version and Startup (Normal/Fast, default Normal). Main Menu has Base Game Items
  and Extra Items, including the default-visible Music Player with its own preference. Audio owns
  Background Music (Default/DLC) and Menu Music Navigation; Display owns Overscan. Music-player
  visibility and pad/key music navigation work independently. Changes affecting linked music,
  cache/refresh, RhythmVerse shortcut/store, or HUD settings ask for confirmation before saving;
  Cancel changes nothing, and re-enabling a prerequisite does not restore its children.
  The combined release build and 21 related suites pass, as do 2,612 extracted-record checks across
  eight store/header states, with no refused edits. These are offline checks; the new settings
  and confirmation UI have not been navigated in-game. See [the current organization and rules](menu.md#current-organization-2026-10-26).

- **Load Guard (2026-10-26):** General has a native
  Off/30s/60s selector, default Off (`general.load_guard`, stable id 10).
  A committed value updates the host atomic policy for the next load. An overdue
  attempt uses the owner-authorized external restart, with a native timeout dialog;
  same-process cancellation is not claimed. Strict persistence and both menu
  contexts pass host tests; the owner confirmed timeout restart/dialog, enabled
  background music and subsequent playback. See
  [the load-guard record](../plans/finished/2026-10-13-load-guard.md) part 5.

- **Menu-music navigator P1–P3 delivered (2026-10-26; owner accepted):** the default-On,
  restart-scoped switch is on Audio/Video → Audio. Background Music Play remains
  Interface Mapping's fifth row, with effective bindings and Assign / Reset
  pages in both contexts. `[menu_music]` persists action-specific pad/key sources.
  Next/Previous execute on the guest poll for DLC music and its temporary title-track fallback, publish snapshots
  and keep Play Current Song aligned. Native pause/resume is enabled without a trial
  flag after owner listening acceptance and a measured 7.285-second frozen-cursor hold.
  The Play/Pause row displays its configured bindings. P3 adds an independent bottom-right
  player with mouse transport and local package covers. The owner confirms the player/cover and
  Start pause. The borderless black panel has dark-gray buttons and no hover text; hover retains
  the OS cursor. The latest review removes all LB/RB footer additions and spacing edits, retaining
  retail A/B. The owner subsequently confirmed the full navigator works, including RB Next after
  the earlier successor-load crash. Its exact cause was not independently isolated; specific DPI,
  hardware and prolonged-soak arms remain unmeasured. See the
  [navigator record](../plans/finished/2026-10-22-background-music-navigator-plan.md) §22.

- **Guest binding rows (2026-10-25):** the host binding overlay is removed. Interface and gameplay
  lists receive current text on entry and after Reset All; the title draws every row. See the
  [implementation plan](../plans/in-progress/2026-10-22-in-game-binding-rows-plan.md).

- **HUD placement rewritten (2026-10-24; gameplay validation pending):** the
  post-present repair and transform-message probes are replaced by a WorldXfm
  hook, applying the aspect correction before rendering. Missing/hidden HUD
  elements cannot trigger rediscovery; authored local transforms and parent
  inheritance are preserved. Host regressions exercise all 64 anchor subsets,
  repeated recomputation and scene reuse. **Owner follow-up:** the left cluster
  was still at 16:9; draw-time preparation now covers clean cached anchors and
  invalidates cached children only when a parent matrix changes. The new cached
  draw regressions pass; the next gameplay check remains with the owner. See
  [graphics.md](graphics.md) part 3 §9.2.7. **2026-10-25 comparison follow-up:**
  the personal-best meter's draw group bypassed the named leaderboard dir;
  pre-draw preparation now follows ownership and transform ancestry. The new
  direct-submission regression reproduces the omission; gameplay confirmation
  of the correction is pending (graphics.md part 3 §9.2.8).

- **Ultimate settings format fixed (2026-10-24):** the host rejected the mod's own
  top-level DTA pairs as trailing content, causing Gold Star Progress to look changed
  in-session but revert at restart. The adapter now reads native pairs and legacy host
  wrappers, and always saves native pairs without losing unknown values. Host regression
  and real-payload reload coverage are in `tests/ultimate_adapter_tests.cpp`; evidence
  and the pending game restart check are in [architecture.md](architecture.md) part 5 §4.

- **Two rows renamed, the refresh entry back on by default (2026-10-22, superseded 2026-10-28):** the main
  menu's store entry reads **RhythmVerse** (was *RhythmVerse Customs*) and the refresh row reads
  **Refresh Songs** (was *Refresh Music Library*); the Downloadable Content page's own switch is
  renamed with it. `general.refresh_music_library` now defaults **On** again (it was On when it
  shipped on 2026-10-15 and Off from 2026-10-20), in the schema row and in its cvar carrier, which
  must agree. The setting id and the cvar name are unchanged, so a profile that named the key keeps
  its value. The renamed labels are `kRhythmVerseLabel` (`src/ui/settings_screen.h`) and
  `kDlcRowRefreshLabel` (`src/ui/menu_options.h`), which the main-menu locale rewrite, the payload's
  own records and the Interface → Main Menu mirror all read, so no second spelling exists to drift.
  **Pinned:** `rb_blitz_settings_screen_tests` asserts `Refresh Songs: On` and the row's 28-character
  line budget; **not yet measured:** a boot OCR of the renamed menu rows (the store acceptance's
  Enable/Store legs are the harness that reads them). **2026-10-23, owner pass:** the restored
  Download Content row is spelled **`Download content`** on Interface → Main Menu (sentence case,
  like the mirror's other live rows; the shipped `Download Content` spelling survives only as the
  string the locale rewrite searches for), and the mirror no longer offers the refresh row while
  `general.refresh_music_library` is off — the menu does not carry it then, and
  `Refresh Songs: Visible` beside a menu without the row is a lie
  (`MenuConfiguration::refresh_row_on`, pinned by `settings_screen`). The acceptance fixture no
  longer writes `refresh_music_library`, so its Enable leg proves that default from the compiled
  value and checks the boot did not commit it.

- **Independent header switches (2026-10-23):** General ▸ Interface ▸
  Music Library & Store (second row, immediately after Main Menu) now has
  **Show Messages** and **Show Recommendations**,
  defaulting to **Messages On / Recommendations Off** (owner's final defaults,
  2026-10-23); both are restart-scoped. The owner confirmed the preceding
  combined header correction worked, then requested this split. A legacy
  `show_messages_recommendations` value supplies either new key only while
  that key is absent. The tab ring is **Your Songs → RhythmVerse → Messages →
  Recommended**, with LT reversing it; every screen skips disabled entries.
  The RhythmVerse tab follows its master, and the new-message badge follows
  Messages. LT/RT styling is disabled only when all three optional tabs are Off.
  All eight states compile from extracted retail records without refusals
  (`out/_swap/split-headers/records/state-0..7/`); destination, arrow and preview
  cleanup assertions run on those records. **Subsequent owner tests** exposed
  reversed swipes when skipping tabs and holes where hidden tabs had been.
  Header triggers now pass an explicit direction; the guest header's animation
  keys pack enabled labels and the badge consecutively in cyclic order. Ordinary
  transitions reset the override and retain retail's selector. The updated
  eight-state checker passes 1,550 assertions, including animation and positions
  (`out/_swap/header-animation/records/state-0..7/`), and the full release build
  succeeds. The owner manually confirmed the corrected swipes and compact layout
  work. **Reopened after an intermittent Search report:** with Recommendations Off,
  its restored caption overlapped Your Songs. The shared background now reapplies
  disabled-label visibility on entry and poll, covering the browser entry that
  skips the front menu's header setup. The updated eight-state record check passes
  1,982 assertions (`out/_swap/header-search-visibility/records/state-0..7/`).
  **Closed 2026-10-24:** the owner's own in-game check reads the two switches and the
  header bar as working, the Search overlap included - P9-4 is Done (RhythmVerse
  plan §9/P9-4 close-out).

- **Historical combined Show Headers (2026-10-22, superseded by the split above):** General ▸ Interface now ends with
  **Music Library & Store**, containing **Show Headers: On | Off**. Default On;
  `interface.show_messages_recommendations` (id 86) is restart-scoped. Off hides
  MESSAGES and RECOMMENDED on Your Songs and the store, including the new-message
  badge, through their shared guest background panel. The caption names both tabs;
  the full Off label is 17 characters. Both menu and pause contexts carry the page.
  **2026-10-23 correction:** the owner's test found an Off crash and no trigger
  navigation from Your Songs while On. The nested lookup was replaced with
  retail's direct scene-label commands, the song-list offline gate is bypassed,
  and Off skips the hidden pages when navigating between Your Songs and the store.
  All four switch combinations build from the real extracted records without
  refusals and round-trip byte-identically. **In-game retest pending with the owner**;
  the crash is not claimed fixed from host tests alone. *(Superseded 2026-10-23: the
  owner confirmed the corrected behavior on the rebuilt binary, and the split block
  above records it.)* The owner's subsequent
  screenshots exposed a separate presentation gate: Your Songs still chose the
  offline header Flow (dim tabs, no LT/RT), while the store chose the full header.
  The song screen now selects the full header whenever either switch is On.
  The host matrix asserts this entry call in the actual generated retail records;
  visual confirmation remains pending with the owner. *(Superseded 2026-10-23: the
  owner confirmed the corrected swipes and compact layout.)*

- **The RhythmVerse store's settings are in the game (2026-10-20, RhythmVerse plan P6).** Under
  **General ▸ Downloadable Content ▸ RhythmVerse** (first row, P9-3): `RhythmVerse`
  (default **On** since the owner pass of 2026-10-22, plan §9/P9-1 - it was off in the P6 delivery) and `Filters` (titled
  `RHYTHMVERSE FILTERS` since the P9-11 renaming), a submenu of five instrument booleans
  (Guitar/Bass/Drums/Vocals On, Keys Off; On = that chart is required, Off = no condition, all off = no constraint). The two menu-music rows moved under a
  `DLC Menu Music` submenu. P9-3 makes Downloadable Content five rows, with RhythmVerse first;
  its two-row page backs to Downloadable Content and the filter backs to RhythmVerse in both
  contexts. The five **filter** rows are
  `hot`: the applier replaces the store's in-process settings snapshot, which its workers read
  before every request. The **master is `restart`** since the owner pass of 2026-10-22 (plan
  §9/P9-2): the switch also owns what the boot drew - the main-menu *RhythmVerse* row (labelled
  *RhythmVerse Customs* until the 2026-10-22 rename) and
  the store's heading - so a change opens the restart confirmation when Help & Options is left, and
  *Close* finishes the leave with the running process still on its pre-change value. **Evidence:** `rb_blitz_settings_screen_tests` (136,641 checks) pins the page order,
  labels and both contexts, and pins the master's compiled default to On against
  `schema::FindRow(kRhythmVerse)`; `rb_blitz_store_client_tests` pins the row → snapshot mapping
  (the snapshot's own default is On, a commit bumps the generation, an equal commit does not). The
  workers read only that snapshot, so Off makes no request by construction — and since 2026-10-22
  that is **measured**: the acceptance harness's control leg (`scripts/acceptance_store.ps1`,
  RhythmVerse plan §5/P8) writes the switch off and finds no
  `store_catalogue`/`store_fetch`/`store_library`/`store_download`/`store_search` request line in the
  log, while the main menu's own refresh row still dispatches. The harness's Enable leg is what
  proves the default: its profile never names the setting, the page draws `RhythmVerse: On`, the
  boot adds no key to the profile, and the same boot's main menu already carries the store row.
  **The restart rule is measured too (P9-2, 2026-10-22)**: `scripts/probe_master_restart.ps1` flips
  a filter leaf and then the master through the page — the leaf is applied live
  (`hot_apply: general.rhythmverse_guitar = false`) and leaving asks for nothing, the master is never
  applied live and leaving draws `settings_restart_screen` once (`Restart now` / `Close`), *Close*
  finishes the leave with the menu still drawing the store row, and the next boot, on the file the
  game committed, has no store row and makes no store request
  (`out/_swap/master-restart/20261022-214004/session.log`,
  `out/_swap/master-restart/20261022-214555/next-boot.log`, 9 steps / 0 failures).
- **RhythmVerse search is typed on the title's own keyboard (2026-10-23, RhythmVerse plan P9-11).**
  The store's **Search** row opens `virtual_keyboard show_keyboard` - the native
  `.?AVVirtualKeyboard@@` the Ultimate payload's own song search drives (retail's `song_select` does
  not use it, which is what P4's 2026-10-20 scan measured; the class is in the retail image all the
  same). The first argument is a **User object** (`{user_mgr get_participating_user}`, then pad 0's
  as the fallback), because the native show reads the player's name from it - the bare symbol was a
  null read in game. The payload writes two screen methods onto `store_menu_screen`
  (`store::AddSearchKeyboard`): `open_store_search`, which writes `rbbz-store/search/open` and opens
  the keyboard with `$this` as the result's receiver, and `virtual_keyboard_result_msg ($ok $text)`,
  which writes `{sprintf "rbbz-store/search/text/%s" $text}`, submits with `rbbz-store/search/done`
  **and opens a focused loading popup** using the retail error popup's visual. B/Back cancels;
  its `poll` waits for the entire search to finish, closes the popup, and requests the results
  using the shipped row's own `set_source` + `request "/eng/search/"` + `update_loading_status`.
  Repeated requests replace the cached parsed page, so each new search lists its own answer.
  Cover paths remain available while images load; the selected song gets priority, keeps its
  placeholder until ready, then links its cover without reloading the results page.
  There is **no second press** and no blocking host wait; empty/failed/short searches show a notice.
  While the system keyboard is open, game input and native mouse navigation are blocked, and the
  closing press is consumed through release. Editing guidance can still appear on the Help &
  Options caption bar **even with Settings Labels off**; loading belongs to the modal and
  completed searches dismiss the caption before opening results or a terminal notice. **The ten
  authored key pages per context are gone** (page count 71 → 61, both contexts), and so is P4's
  `store_search_diagnostic`; `store_search_seed` stays as the way a scripted boot reaches a request
  without typing on the on-screen keys (the screen asks `rbbz-store/search/seed` and submits the
  field while one is installed). The store's own **Start** button is captioned `FILTERS` since the
  same day (it was `SEARCH FILTERS`), and the page it opens is titled `RHYTHMVERSE FILTERS`.
  **Evidence:** the transforms run against the shipped record
  (`rb_blitz_store_payload_tests store-edit <store.dtb> search` prints both methods, the routed
  `SELECT_MSG` and the panel `exit`), `rb_blitz_store_search_tests` (287 checks, the raw-UTF-8
  grammar), `rb_blitz_store_payload_tests` / `rb_blitz_settings_screen_tests` pins, the measured
  one-press flow (`out/_swap/search-probe/20261023-175633`: `waited 2730 ms`, `15 offer(s)`,
  `store_browser_screen`), and the plan's P9-11 record. The owner's own boot (2026-10-23) typed on
  the console keyboard and confirmed the route end to end.
- **The Help & Options tree was reorganised and retitled (2026-10-19).** The root is
  `How to Play, Calibration, General, Controls, Audio/Video, Gameplay`; Interface moved under General
  (`Main Menu`, `Music Library & Store`, `Skip Offline Prompts`, `Help & Options Labels`;
  Music Library & Store moved directly after Main Menu on 2026-10-23, and the offline row was
  renamed from `Offline Prompts: Hidden|Visible` to `Skip Offline Prompts: On|Off` the same day -
  same setting, presentation only); HUD moved under Gameplay and is split into
  `HUD Elements` (five `Visible`/`Hidden` rows) plus the `Note Streak`/`Input Viewer` switches. The
  4:3 Letterboxing row is removed, the Main Menu page has one `Visible`/`Hidden` row per hideable
  entry with no master, and Target draws `Rock Band Blitz / Ultimate / Demo`. The scene's `header.lbl`
  is a `UILabel`, so each page of ours sets its title token on entry; the retail Audio page is
  retitled `AUDIO` and its list is moved onto the merged list's position (`get_local_pos` /
  `set_local_pos`). **The Audio correction is boot-verified (2026-10-19)**: the first row occupies
  the parent page's own band (y=368..427 at 993 client height) and Overscan stays On
  (`out/audio-fix/probe3/`); the earlier `local_xfm` spelling did not move the list, and the
  owner's capture from it showed Overscan off on entry.
  **Evidence:** the owner's manual captures of 2026-10-19, the probe boots, and
  `rb_blitz_settings_screen_tests` (107,708 checks; see [menu.md](menu.md) Part 1).
- **The external settings application is gone (P8b, 2026-10-19).** `rb_blitz_controller_settings.exe`,
  its target, sources, schema, tests, capture/acceptance scripts and payload entry were removed;
  the installer ships `rb_blitz.exe` alone, a fresh/upgrade/repair install has no settings
  executable or *Open Controller Settings* action, and an upgrade deletes the installer-owned binary
  and the two obsolete shortcuts. Every player setting it edited is an in-game Help & Options row;
  the two shared host suites kept their checks as `profile` and `profile_remap`. The gameplay-mapping
  plan's §19 has the evidence and its §17.1 inventory says where each external row went.
- **The feature is wired through the real boot and the guest UI**: settings load in
  `OnConfigurePaths` before the GPU plugin, title data and guest rendering, the renderer is chosen
  before the plugin loads, and VSync/filtering/anti-aliasing/resolution scale apply in `OnPreSetup` —
  not when the player presses A.
- **1080p is the title's own 960×540 at backend draw scale 2**; the experimental native-1080p
  guest-height route was removed (it corrupted title-screen artwork before the menu).
- **The merged Help & Options tree works from the main menu and the in-song pause route** — the
  in-song construction-order fault was fixed by deferring those screens until the pause panels exist,
  and the in-song root, child pages, Back and resume were exercised in a running game.
- **The menu payload extends the mounted archive in place (one part in, one part out)**, so
  Ultimate-shadowed records survive; provider choices come from the installed payload, its settings
  file is redirected into user storage, `unlockall` is stored by the native service, and the
  achievements action invokes the provider action.
- **All eleven owner punch-list changes landed (2026-10-13)** — mod's own main-menu entry removed, A
  advancing/wrapping a horizontal select, the restart prompt only when leaving the root, the demo
  named in Target, the DLC page regrouped (*Refresh Music Library* now on the main menu), On/Off
  booleans, `Overscan: On`, Gameplay regrouped, Depth of Field moved to Graphics, a new Display row
  (Windowed/Borderless/Full Screen, [patch 0016](../../patches/README.md)), Video regrouped — **plus
  Controller Settings cutover (P11b)**: `kGameOwnedRows` no longer travel on the command line, the target
  moves through migration (not `--ultimate_mode`/`--license_mask`), and
  `profile_controller_rows_only` defaults true.
- **Vulkan is selectable now** (`REXGLUE_USE_VULKAN=ON` in the Windows presets; Renderer offers
  `D3D12, VULKAN` with d3d12 still the active/fallback entry), the payload ships `vulkan-1.dll` by
  owner decision, and a missing loader is refused on both sides.
- **Refresh row defaults off; Download Content restored (2026-10-20)** — the owner asked for three
  adjustments. `general.refresh_music_library` now defaults **off** (so a fresh install has no
  refresh row; the cache itself stays on), the retail *Download Content* main-menu entry is restored
  as `splash_dlc_store` behind `interface.hide_download_content` (**default Hidden**, offered on
  Interface → Main Menu), and *Play Current Song* stays on by default but its settings row is hidden
  when the boot found no DLC at all. **Superseded 2026-10-22** for the default and the row's name:
  the owner asked for the refresh entry to default **On** again and for the row to read *Refresh
  Songs* (see the first bullet above); the restored *Download Content* row and the *Play Current
  Song* rule are unchanged. `src/hooks/settings_payload.cpp` reads the catalog from
  `dlc::LibraryPackagePaths()`; the row clone is built in `src/ui/settings_screen.cpp` before
  `SetRefreshRowAction` reroutes `splash_dlc`.
- **Boot speed (2026-10-13)**: the wait is the DLC discovery, not the A/V load; the defaults now cache
  the library, zero `deferred_overlapped_delay_ms` and free the frame rate, so the walk is ~0.3 s and
  2,804 items enumerate in 39 s (was ~6 minutes). Its *pacing* half — put the frame rate back at the
  last item, the engine's own completion, or a two-second idle — is superseded by the P6 bullet below
  (restoration now waits for the verified usable menu).
- **Row descriptions (the help captions)** — the highlighted row's canonical symbol is published over the
  settings channel, retail/in-song pages are resolved by a host observer, and a host overlay draws the
  caption. Since **2026-10-14** (P4 of the installed-game review) the caption is a system sans at a
  display-proportional size on a much lighter panel, sits above the A/B prompts rather than in their
  strip, is **absent** on the calibration wizard and on every informational/confirmation page, and has
  its own row — **Interface → Settings Labels**, default On — which applies **live**, with no restart
  prompt. Seen by eye on a running screen on that date: 1280x720 and this machine's 21:9 primary
  (2560x1080 render, 2354x993 client). Since **2026-10-19** a mouse hover publishes the same caption
  from the engine's `SCROLL_MSG` (see "Mouse hover publishes the row caption" below), so pointing at a
  row updates the description without a click.
- **Historical DLC cache, status text and loading progress (P6 of the installed-game review, 2026-10-14; host cache retired 2026-10-28)**: the
  host cache now names its hit/miss reason and per-phase timings, validates its body with a checksum,
  replaces itself atomically and refuses to persist a partial scan; a **real** hit is what makes the
  English `server_connect_enumerating_content` record read **Loading Cached Content List** (cold, Off
  and refresh keep *Discovering Downloadable Content*); since **2026-10-15** the loading numbers
  live in the title's own bar — the host pushes the aggregate enumerator counts through the
  engine's own push and draws no overlay (the P6 overlay is retired;
  [native-discovery-bar.md](../plans/finished/2026-10-15-native-discovery-bar.md)) — and V-Sync
  returns at the verified usable menu. Measured in the installed copy: the first comparison
  removes **103.4 s → 17.7 s** of post-full-bar work, and a warm host cache removes **92.3 %** of the
  host's own phase time but only **0.43 %** end-to-end — the title still enumerates and still
  mounts/metadata-reads **742** of the 1,773 packages. The guest's *own* songcache is a separate file:
  the same run wrote a 16-byte stub with **0 packages** in it, so a host hit says nothing about it.
  Contract, limits and the trial table: [dlc.md](../guides/dlc.md) §4.4 and the review plan's P6
  record ([installed-game-fix-prompts.md](../plans/finished/2026-10-14-installed-game-fix-prompts.md)).
- **The in-song entry is measured now (P8a, 2026-10-18).** The pause panel's `pause_controls` row
  jumps to the pause twin of the in-game Controls menu, which lists no in-song panel and therefore
  does not hit the `read of guest 0x00000024` construct-order fault; a paused song drives pause →
  Help & Options → Controls → Input Types and a bind page → hold-to-cancel → back to the pause panel
  → resume, with no guest fault (`out/gameplay-mapping/in-game/p8a-in-song3/`). Item 13 below is
  closed by that measurement.
- **Verifiably open: the interactive acceptance matrix has not been re-run** since the punch list, so
  items 2, 3, 6/7 and 10 are asserted by tests and the payload's records, not by a person looking at
  the screen.
- **Verifiably open: no Windows preset can build a test right now** (`REXGLUE_USE_VULKAN=ON` makes the
  SDK's glslang guard write `BUILD_TESTING=OFF` before `include(CTest)`); a Vulkan-off tree configured
  with `-DBUILD_TESTING=ON` builds everything and runs **39/39** (**38/38** until `content_progress`
  was added by P6 on 2026-10-14, 37/37 until `window_policy`). Building that tree **clobbers the
  Vulkan-on SDK objects** — the two configure
  into the same `rexglue-sdk/out/win-amd64` — so re-run `cmake --preset win-amd64-release` before the
  next release build.
- **The window follows the resolution now (P2 of the installed-game review, 2026-10-14)**: the boot
  writes `video_mode_width/height` (the presenter scales into a region of the frame's own shape),
  `window_width/height` when the decorated window would not fit the desktop's usable area, and
  `window_resizable = false` ([patch 0017](../../patches/rexglue-sdk/0017-fixed-size-window.patch)) —
  measured: a first run with **nothing stored** reaches 2560x1080 on this machine's 21:9 primary with
  a 2354x993 client and no `WS_THICKFRAME`. The game window is deliberately one size; the Controller
  Settings utility is untouched. See [graphics.md](graphics.md) part 3 §11.
- **The restart confirmation can be left now (P3 of the installed-game review, 2026-10-14)**: its own
  Cancel returned to the **merged root it was leaving**, so Help & Options could not be left at all —
  every leave re-opened the confirmation — and a restart refused before the boot's handler ran left
  `restart.ok` true, so *Restart now* was a silent no-op. The confirmation now inherits the leave
  target the root was about to use, every answer resolves that bit, and a failed save/lock names the
  path and the OS reason instead of "another writer or access denied". Driven before and after in
  `out/_swap/p3/` (`repro3-close` vs `after-close`, `after-restart`); the host suite was 38/38 then
  (39/39 once P6's `content_progress` landed the same day).
- **Settings can be saved again in an installed copy (2026-10-15)**: the flat-panel pair
  (`forceplot`/`which_skybox`) was spelled as a boolean by the adapter's file codec while the menu,
  the store and the request all carry the payload's **token**, so a press on either row staged a value
  the commit could not encode — and that refusal landed in the commit journal, which recovery could
  then never finish, so **every** later edit answered `io error` and the menu blamed the settings
  folder (item 18 below, now fixed). The pair is a select over the payload's inventory whose *file*
  value is the chosen **position**, an uncompletable journal is parked
  (`<settings>.commit-journal.stale`) instead of refusing every save, and the save-error page names
  the reason it saw. Driven with the owner's own journal seeded in `out/_swap/settings-save/` (the
  edit commits; a seeded journal is *finished* into `ulti_settings.dta` as `(forceplot 1)
  (which_skybox 2)`); 39/39 host tests.
- **A failed graphics boot no longer down-sizes the game silently (measured from the installed copy, fixed 2026-10-15)**: the per-profile `settings.toml.graphics-boot.toml` journal replaced the boot's graphics values with last-known-good **for as long as the request stayed the same** — a stale journal left by an earlier build was enough — while the menu kept showing the requested value. On this machine's 21:9 primary that meant Resolution on **Auto** booted 1280x720 on every launch, and a resolution change followed by *Restart now* booted the old size in the child. The journal now keys on the player's **choice** (`Auto` is its own request), recovers one failed boot, **retries that request exactly once**, and clears when the retry presents; a clean close before the menu frame does not spend the retry. Measured with the owner's own journal copied verbatim into a disposable root (`out/_swap/gfx-journal/`; the pre-fix boots are kept in its `owner/`): retry → 2560x1080; `Auto` → 2560x1080; change → *Restart now* → the child boots the new size (960x540 x2 = 1920x1080); and the prompt's value-based gate means Auto → the size it already resolves to asks for no restart at all. `settings_graphics` = 234 checks; full tree suite 38/38 (`out/build/rename-tests`).
- **Verifiably fixed**: the 2026-10-12 black startup (the generated payload made the patch archive
  two-part, which this title never reads — Part 2) and graphics recovery (the marker, fallback and
  safe mode all ran; older statements, including item 11 below, are wrong).
- **The next three things to do**: (a) drive the merged menu once after the punch list (A
  advance/wrap, On/Off in every language, Borderless) — the restart prompt's leave path is measured
  now (the P3 bullet above); (b) the hardware-only arms: a physical pad's capture and
  hold-to-cancel, stick directions, the recovery's pad-connected case, and an in-song page on the
  Ultimate payload ([known-issues.md](../reference/known-issues.md)); (c) make a preset that can
  build a test again, so the suite count is reproducible from the presets.

## Part 1 — Current status and remaining work (was `docs/plans/in-game-settings-remaining-work.md`)

> **Update 2026-10-13 (latest).** The owner drove the merged Help & Options surface in a running game
> and gave an eleven-item punch list; all of it is implemented, and so is Controller Settings cutover it
> required (P11b) and the retirement of the second main-menu entry to the mod's own screen. The
> release tree is **37/37 green** (`ctest --test-dir out\build\win-amd64-release`), subject to the
> preset caveat below; the interactive acceptance matrix has still not been re-run.
>
> Statements this session measured to be wrong: graphics recovery **is** wired (the marker, the
> fallback and safe mode all ran); the Ultimate boot **does** present and **does** carry the merged
> root. The feature has not passed final acceptance.
>
> **On the numbered list in §3.** "What is left, by package" is the whole plan's open-item ledger: one
> number per item, cited elsewhere by number (item 13 is the in-song entry, the one open item blocking
> gate G1b), so the numbers are kept exactly as written and open items are not deleted. The item's own
> italic citations name documents now consolidated as [plan.md](plan.md),
> [architecture.md](architecture.md), [menu.md](menu.md), [graphics.md](graphics.md) and
> [contracts.md](contracts.md).

### Implemented

The settings feature is wired through the real boot and guest UI:

* Native settings are read from `settings.toml` in `OnConfigurePaths`, before the GPU plugin, title
  data and guest rendering (the path file and legacy migration in the same early stage), and renderer
  selection precedes the plugin load. After the plugin registers its cvars, VSync, filtering,
  anti-aliasing and the resolution scale are applied in `OnPreSetup`, before presentation and render
  resources are initialized.
* Resolution uses the measured 960×540 or 1280×720 title guest modes times an integer backend draw
  scale (1920×1080 is 960×540 at 2×); the experimental native 1080p guest-height route was removed
  because it corrupted title-screen artwork before the menu.
* The generated Help & Options tree works from the main menu and the in-song pause route — the in-song
  construction-order fault was fixed by deferring those screens until the pause panels are available,
  and the in-song root, child pages, Back and resume were exercised in a running game.
* The menu payload extends the mounted archive, so Ultimate-shadowed records are retained; provider
  choices come from the installed payload, its settings file is redirected into user storage,
  `unlockall` is stored by the native service, and the achievements action invokes the provider action.
* Save errors, restart confirmation and restart coordination are connected; the child command builder
  no longer duplicates the game-data-root argument (a previous launch exposed that defect).

### Owner review punch list (2026-10-13)

The owner drove the merged surface in a running game (Ultimate installed, live DLC library) and gave
eleven changes. All eleven are in `src/ui/settings_screen.{h,cpp}` and the records it edits, with the
game-side and Controller Settings-side consequences listed after them:

1. **The main-menu entry to the mod's own screen is gone.** "Help & Options is enough": with a
   compatible provider installed, the payload's own `mod_settings` row is *removed* from the splash
   root (`RemoveRow`, [menu_payload.cpp](../../src/settings/menu_payload.cpp)) instead of being
   rerouted, so the only way to the provider's pages is the merged Help & Options tree. A payload
   that never had the row refuses the removal and that refusal is not fatal.
2. **A horizontal select advances and wraps.** A (`kAction_Confirm`) on a row whose widget is
   `kHorizontalSelect` moves to the next choice and wraps to the first after the last
   (`(index + 1) % choices`), commits it through the bridge, and shows the new value — the arrows
   keep their own step-and-commit behaviour.
3. **The restart prompt only opens when the player leaves Help & Options.** The root screen's Cancel
   checks the bridge's sticky `restart.pending` flag, which is set when a commit succeeded and the
   row needs a restart ([bridge_sink.cpp](../../src/settings/bridge_sink.cpp)); a change that needs
   one no longer interrupts the player mid-edit, and leaving with nothing pending does not ask.
4. **Target lists the demo by name.** "Rock Band Blitz Demo" is a choice again
   ([settings_payload.cpp](../../src/hooks/settings_payload.cpp)); the recorded trial-boot fault is a
   separate matter, and the external application's radio said the same thing until it retired. **Fixed 2026-10-18:** the
   Ultimate choice had disappeared from that row, because `ultimate::Configure` derived the provider
   root from `ultimate_mode` and the boot applies the stored target as that cvar — so a profile whose
   target was "Rock Band Blitz" inspected the game root, found no payload, and stopped offering the
   one control that selects Ultimate. The payload is now inspected where it lives, and the mode only
   decides whether it is applied; a fresh profile lists all three choices.
5. **The DLC page and the refresh action.** *Cache DLC Library* reads `On`/`Off` instead of the
   "Externally configured" marker it used to show — the external application's `--enhancements_dlc_cache` row
   outranked it, which made it read-only, and the cutover below removed that flag; *Refresh
   Downloadable Content* left the settings tree to become **Refresh Music Library** on the main menu.
   **Superseded 2026-10-20:** the refresh entry's own row defaults **off** now, and the retail
   *Download Content* entry was restored as a separate row (`splash_dlc_store`) behind an
   Interface → Main Menu switch that defaults Hidden, so a fresh install has neither the refresh
   row nor the store row until the player asks for one. *Play Current Song* stays on by default and
   is hidden entirely when the boot found no DLC. The 2026-10-14 note below still describes the DLC
   Menu Songs revival.
   **Superseded 2026-10-14:** *DLC Menu Songs* was retired here (the pool always took what the library
   yielded) and is a **visible row again** — `general.dlc_menu_songs`, on by default, carrying
   `enhancements_menu_dlc_songs` (the owner asked for the R9 toggle in-game, and the *Play Current
   Song* row beside it is gated on it); the *pool size* row (`enhancements_menu_dlc_song_count`) stays
   retired and its description says it is ignored. See
   [play-current-song.md](../plans/finished/2026-10-14-play-current-song.md) D1.
6. **Boolean rows read On / Off, not the locale's ENABLED / DISABLED.** The payload overrides the two
   locale tokens the retail checkboxes use (`LocaleOverrides()`), per language, without touching any
   other string.
7. **Audio & Video's two banners became suffixes.** Overscan and Bass Boost read `Overscan: On` /
   `Bass Boost: On` — the `| ENABLED` spelling is gone — by the same locale-override mechanism plus
   the label rule that appends `: On`/`: Off`.
8. **Gameplay regrouped**: Camera Angle, Game Speed, **Tracks & Gems** (Precision Mode, Disable
   Random Track Wiggles, Gems), Plots & Skyboxes.
9. **Depth of Field** moved to Audio & Video → Video → Graphics (it is a graphics row, not a
   gameplay one).
10. **A Display row exists** (Windowed / Borderless / Full Screen) — a new setting
    (`SettingId::kDisplay`), a new SDK cvar and the third window mode ([patch 0016](../../patches/README.md)),
    applied through `DisplayToCvars`.
11. **Video regrouped**: the Display group (Display, Resolution, V-Sync), the Graphics group
    (Anti-Aliasing, Anisotropic Filtering, Depth of Field), then Renderer.

#### Renderer: Vulkan is an option now (owner request, 2026-10-13, later the same day)

Two things were missing and both are in:

* The build sets `REXGLUE_USE_VULKAN=ON` in the Windows presets (`windows-base`); the SDK's own
  Windows default is **OFF**, so the measured tree was D3D12-only and every Renderer row offered one
  choice.
* The game's row had hardcoded one backend (`"not probed: the one backend this project ships"`); the
  boot now builds one probe per `RBBLITZ_BACKENDS` entry ([CMakeLists.txt](../../CMakeLists.txt), the
  same switches the plugin reads) and checks for `vulkan-1.dll` at run
  time. d3d12 stays the active/fallback entry — Vulkan is offered, not recommended — and a missing
  loader means the row is not offered and `--gpu_backend=vulkan` / a stored `vulkan` choice refuses
  and boots D3D12.
* **The payload ships the loader (owner decision)**: `installer/tools/make_payload.ps1` copies it
  (`-VulkanLoaderDir`, else `$env:VULKAN_SDK\Bin`, `C:\VulkanSDK\*\Bin`, `$env:WINDIR\System32`; a
  miss is fatal unless `-AllowMissingVulkanLoader`). Measured: 9-file snapshot,
  `vulkan-1.dll 1,452,408 bytes`, `payload renderer backends: d3d12, vulkan`; the game logs
  `settings_menu: Renderer offers 2: D3D12, VULKAN`.

#### Renderer row: drawn only when there is a backend to choose (owner decision, 2026-10-14)

The owner asked for the row to be hidden where it has nothing to offer, after an installed build appeared to
offer D3D12 alone. That appearance was a **packaging** fault rather than the machine's: the setup
executable had been built from `installer/out/payload` as it stood before `REXGLUE_USE_VULKAN=ON`, so it
shipped a one-backend `rexgpu-xenos.dll`. `build.ps1` now verifies a snapshot against the build tree
before reusing it, so that cannot happen silently again
([installed-game-fix-prompts.md](../plans/finished/2026-10-14-installed-game-fix-prompts.md) P1).
The row rule is kept on its own merits:

* `src/ui/settings_screen.cpp` drops the row from the Video page when the configured renderer list has
  fewer than two entries. It is *not* drawn as `Renderer: Unavailable` (that reads as a failure) and not
  left as a one-choice select, which is what `graphics.md` §10 originally specified — that entry is
  superseded.
* The state is not silent: `src/hooks/settings_payload.cpp` still logs the list, and says what happened to
  the row — `settings_menu: Renderer offers 1: D3D12 (the row is hidden: one backend is not a choice)`.
* Measured on this machine, where both backends are usable, nothing changes: the reinstalled copy logs
  `settings_menu: Renderer offers 2: D3D12, VULKAN`, creates the Vulkan device and presents a menu frame
  (`out/_swap/p1/installed2-vulkan.log`). The hidden case is pinned by `TestRendererRowVisibility`
  (`tests/settings_screen_tests.cpp`); `settings_screen` is 27,772 checks green, and the test-capable
  tree is **39/39** since P6 added `content_progress` on 2026-10-14 (38/38 that morning;
  `out/build/rename-tests` — the Windows release preset cannot build a test at all, §"How the
  test tree stands").

#### Controller Settings cutover (P11b)

Items 4 and 10 are only honest if the in-game rows own their settings, so on the owner's decision
("Controller Settings stops passing game-owned settings; only paths/target/profile/controller flow"):

* `kGameOwnedRows` (the retired application's `game_launch.cpp`) — sixteen keys from
  `enhancements_dlc_cache`/`dlc_scan_threads` through `resolution_scale`, `fullscreen`, `vsync`,
  `gpu_backend`, `swap_post_effect`, `native_2x_msaa`, `anisotropic_override` and `present_letterbox`
  — no longer travels on the command line (the tabs still write them to the profile), and the target
  pair (`--ultimate_mode`/`--license_mask`) is replaced by migration of `[launch] target` on the first
  boot (only when it differs from the compiled default `common`), which keeps the in-game Target row
  editable.
* `profile_controller_rows_only` now defaults **true**
  ([profile_apply.cpp](../../src/profile/profile_apply.cpp)): the game applies the controller rows and
  the host-only host rows (monitor, safe areas, dither, overscan cutoff, two audio rows — no
  in-game descriptor) and logs the rest as inert preservation data. Removing those tabs is P8's.
* The retired application's `capture_controller_settings.ps1` and
  `acceptance_controller_settings.ps1` asserted its exact command line, and each leg deleted its
  fixture's `<profile stem>.game-settings.toml` first (the Ultimate leg's licence line read
  `(configured)`). Both scripts went with the application on 2026-10-19; the in-game surface is
  driven by `scripts/acceptance_controls.ps1`.

#### Gameplay Mapping tables on the Controller tab (P4, 2026-10-18)

> **Superseded 2026-10-18 by P7, and removed 2026-10-19 by P8b.** This was the external
> application's tab; the live surface is Help & Options → Controls
> ([`settings_screen.cpp`](../../src/ui/settings_screen.cpp)), whose rules P7/P8a measured. The
> record below is kept for its measurements; its file names now live only in git history.

The Controller tab drew the two gameplay tables P1's profile stored
([gameplay-input-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md) §4/§15):

* **Three tables, one renderer.** General Mapping and both gameplay tables were drawn by
  `binding_table.cpp`; each gameplay table owned one
  input type (`pad:` for Controller, `key:`/`mouse:` for Keyboard + Mouse) and refuses the other at
  capture and at parse. One capture session is shared by all three tables.
* **Each switch hides its own table**, both switches stay focusable while their table is hidden, and
  the tab reported a layout change so the shell rebuilt the ring
  (`controller_block.cpp`); a hidden table
  contributed no focus entries. The focus trace now records a ring-size change.
* **A binding whose device is off stays listed and is crossed out** in the Keyboard + Mouse table
  (`binding_text.cpp`), and the utility's own
  navigation and capture still worked with Keyboard Support or Mouse Support off (R9).
* **The two `?` buttons are focus entries** opening persistent help pages; the shell owns Escape
  while one is up, so Escape closes the page and a second Escape leaves.
* **`--dump-layout` names the block**: `block  <n> rows, Controller on/off, Keyboard + Mouse
  on/off` and one `block row` line per row, so a harness could check which table is visible without a
  window. The retired `capture_controller_settings.ps1`
  headless leg asserted it (20/20 on 2026-10-18).

Evidence: `controller_settings_nav` 2,838 checks and 40/40 ctest; the live matrix under
`out/_swap/p4/` (four switch combinations, both device-support arms, the help page, a wrong-kind
row and a capture→Save) with its traces. What this does **not** change: P5/P6 were cancelled on
2026-10-18 (the change of course), so no build hides the native category; P7 replaces it with the
in-game **Controls** menu and disables Default Presets while Controller Custom is On. The tracked
windowed capture legs still target the four-tab surface that P8's gate moved off by default, and
P8 owns re-pointing them.

#### Controls in the game (P7, 2026-10-18)

The input surface is in the game now (plan §16). Help & Options → Controls opens our own nested
menu, so the external app is no longer the only place to change a binding:

* **Controls** → `Input Types` (Controller: Always on; Keyboard `keyboard_support`; Mouse
  `mouse_support`), `Interface Mapping` (P3, 2026-10-20: the interface's nine rows in three
  groups — `Navigation` (Up/Down/Left/Right), `Confirm`, `Go Back`, `Music Library Options`
  (Search/Shortcuts/Sort) and a plain `Reset All`; each row has an Assign/Reset child page and
  draws its binding line in place — see the next bullet) and `Gameplay Mapping` (P4, 2026-10-20:
  `Controller` = Controller Presets — retail's page, always available — and Custom Mapping;
  `Mouse & Keyboard` lists the same three groups and Reset All directly; the two Custom switches
  are gone since P2).
* **The guest draws binding rows (2026-10-25).** Interface and gameplay binding lists request
  `rbbz-settings/text/<panel>` through `read_file` on every population. The host composes
  `profile::BindingRowText` from one effective snapshot and returns a native reference-counted
  DataArray of interned Symbols; ordinary rows retain their locale symbols. Selection, movement
  and scroll captions dispatch by `selected_pos`, from the same authored row order. Capture return
  and Reset All repopulate immediately without a restart. The host binding-row drawing, opaque
  covers, `Multiple` fallback, and UI-thread geometry copy are deleted; Settings Labels remains.
  Both main-menu and pause definitions have host coverage. **Gameplay verification is pending:**
  isolated D3D12/Vulkan attempts stalled before the menu and never requested binding text. The
  owner deferred further driving; the game and harness are stopped. Details and evidence are in
  the [binding-row plan](../plans/in-progress/2026-10-22-in-game-binding-rows-plan.md).
* **The listening dialog is a screen** (`settings_listen_screen`): a capture waits for neutral,
  then counts **3, 2, 1** and leaves the row untouched if nothing is pressed (P5, 2026-10-20,
  replacing the 700 ms hold-to-cancel and the 20-second timeout). A valid press binds on its own
  frame - release not required - a wrong-kind press names the kind and the count keeps running
  beside it, and the dialog leaves by a host Back pulse either way. A completed
  capture is saved through the profile (`launcher.toml`, the same tables P1 froze) and applied on
  the next poll: the switches are live, not launch-scoped.
* **Gameplay Mapping is always on (P2, 2026-10-20).** The two `gameplay_mapping_*` cvars are
  retired (the allowlist no longer names them; an old profile's rows are preserved and inert).
  The Mouse & Keyboard table drives gameplay whenever a song is current, and the Controller table
  acts exactly when it has a custom row - with none, the title's native preset drives the pad, and
  with one, unmodified actions follow the preset the player selected on retail's Controller
  Presets page (observed from the song controller's `+80` slot at `NewController`). The Mouse &
  Keyboard defaults are the restructure plan's D12 list (Q/right click, W/left click, arrows +
  wheel detents, Space/middle click, Escape), a wheel detent is a one-poll press, the first
  Assign on a default row **replaces** it (`Default binding from Preset N. Click to Replace.`),
  and a key a gameplay table is holding is not echoed into Interface Mapping until it is
  released (the Escape that paused no longer resumes the game it just paused). Evidence:
  `out/gameplay-mapping/p2-2026-10-20/` (boot lines), `out/gameplay-mapping/in-game/p2-in-song-2026-10-20d/`
  (paused song, live preset line) and `out/gameplay-mapping/in-game/p2-controls-2026-10-20b/`
  (Controls tree, 57 steps).
* **Interface Mapping owns the interface (P3, 2026-10-20).** `General Mapping` is rebuilt as
  `Interface Mapping`: five rows on the top page (`Navigation`, `Confirm` → A, `Go Back` → B,
  `Music Library Options`, `Reset All`), four in `Navigation` (the D-pad's Up/Down/Left/Right) and
  three in `Music Library Options` (Search → Start, Shortcuts → Y, Sort → Back). The `[remap]`
  tokens did not move (`input.general.a` is `Confirm`), so an existing profile binds the same
  controls; the seven targets the page no longer shows (X, the shoulders, the triggers, the two
  stick clicks) keep their compiled defaults and are still reset by `Reset All`, which now draws
  as a plain action with no `: Nothing` state. The row labels, the overlay's replacement line and
  the help caption read one spelling ([`InterfaceTargetLabel`](../../src/profile/input_rows.h)).
  Live: `scripts/acceptance_controls.ps1 -Label p3-final-controls` (78 steps, 0 failures,
  `out/gameplay-mapping/in-game/p3-final-controls/`), `scripts/acceptance_controls_in_song.ps1
  -Label p3-final-in-song` (23 steps, 0 failures, `.../p3-final-in-song/`) and
  `scripts/acceptance_binding_rows.ps1 -Label p3-final-rows` (24 steps, 0 failures,
  `out/interface-mapping/p1-rows/p3-final-rows/`).
* **A consumed source stays consumed until it is released (P3, 2026-10-20).** P2's per-poll set
  emptied the moment the context became a menu, so a key that opened the menu while still down
  could be read by Interface Mapping: the P3 in-song arm reproduced the pause bouncing (the panel
  appeared 22 ms after the Escape press, the release landed after it). `CollectGameplayConsumed`
  now takes a caller-owned latch - a source a gameplay table took survives the context change
  until it is physically released - which is what makes a pause stay paused
  (`out/gameplay-mapping/in-game/p3-final-in-song/`; the pre-fix bouncing run is preserved as
  `p3-in-song-2026-10-20/`).
* **Gameplay Mapping is two tables of three groups (P4, 2026-10-20).** `Controller` keeps the
  game's own page as `Controller Presets` - always available, no Custom switch above it - beside
  `Custom Mapping`; `Mouse & Keyboard` enters the same groups directly, so the extra `Custom
  Mapping` level and the `Default Presets` name are gone. Both tables draw `Notes` (First Note,
  Second Note), `Move Lane` (Move Right, Move Left) and `Pause & Power-up` (Pause, Power-up) over
  a plain `Reset All`, so no page is wider than four rows and every action keeps its Assign/Reset
  child - which now returns to the group that lists it, not to the top of the table. The
  `gameplay_remap_*` tokens did not move, `TableKindLabel` reads `Mouse & Keyboard`, and the
  retired `settings_keyboard_mouse_actions_*` pages are gone. Live:
  `scripts/acceptance_controls.ps1 -Label p4-controls-b` (93 steps, 0 failures,
  `out/gameplay-mapping/in-game/p4-controls-b/`), `acceptance_controls_in_song.ps1 -Label
  p4-in-song` (23 steps, 0 failures) and `acceptance_binding_rows.ps1 -Label p4-rows` (26 steps,
  0 failures, `out/interface-mapping/p1-rows/p4-rows/`). The Controls arm proves both branches,
  the always-available preset page, the three groups, the wrong-kind refusal, a Mouse & Keyboard
  capture read back in the row, and that table's plain Reset All restoring the default pair.
  A profile keeps an empty section header it already knew (`[gameplay_remap_keyboard_mouse]`
  survives a reset the way an empty `[remap]` does), so the arm asserts the captured value is
  gone rather than the section.
* **The listening dialog counts 3, 2, 1 (P5, 2026-10-20).** The 700 ms hold-to-cancel and the
  20-second timeout are gone: after the press that opened the page is released, the caption shows
  `Closing in 3` and counts down once per second. A valid press binds on its own frame (no release
  wait), a wrong-kind press prints its sentence while the count keeps running, and zero closes the
  page with the row untouched. The hold oracle (`Observation::still_held`) and its two helpers were
  deleted with the gesture. Live: `scripts/acceptance_controls.ps1 -Label p5-controls-b` (103
  steps, 0 failures, `out/gameplay-mapping/in-game/p5-controls-b/`),
  `acceptance_controls_in_song.ps1 -Label p5-in-song` (23 steps) and
  `acceptance_binding_rows.ps1 -Label p5-rows` (26 steps). The Controls arm proves the countdown
  leaves the row untouched, a key binds, a wrong kind does not, and a wheel detent (a press with
  no release) binds and appends.
* **The profile keeps only differences**: a switch set back to its compiled default is dropped
  instead of written (the two device gates are the only switches left; `gameplay_mapping_*` is no
  longer written at all).
* **Retail's controller page is the Controller Presets destination**, not a deleted screen, and the
  in-song list's `Controls` row now jumps to the pause twin of this menu. The in-song route was
  driven by P8a (below), not only unit-tested.
* **The compiled keyboard defaults are interface-oriented (2026-10-19).** Gameplay Mapping's
  Keyboard + Mouse table is on by default, so General Mapping's keys are what drives the menus:
  A = `key:Return` or `key:A`, B = `key:Escape` or `key:B`, X = `key:X`, Y = `key:Y`,
  Start = `key:Space`, Back = `key:Backspace`, and the D-pad is both `W/A/S/D` and the arrow
  keys. The shoulders and
  stick clicks keep their pad control with no keyboard stand-in; the analog triggers stay
  pad-only. The table is `DefaultSources` in [src/profile/remap.cpp](../../src/profile/remap.cpp),
  and [tests/profile_remap_tests.cpp](../../tests/profile_remap_tests.cpp) pins every row -
  including that the A key sets both A and D-pad left, a consequence of the requested WASD
  layout. A profile that already holds a `[remap]` row still wins over these defaults. Live
  check: `scripts/acceptance_recovery.ps1 -Label default-keys-2026-10-19` (11 steps, 0 failures,
  `out/gameplay-mapping/in-game/default-keys-2026-10-19/`) reopens a closed keyboard gate with
  F10 and drives the title with `key_enter`, which reaches A through General Mapping; the other
  rows are pinned by the unit test. One harness caveat: with the legacy `mnk_mode` emulation on
  (the acceptance scripts pass it), that driver still maps Space to A and Return to Start, so a
  key can raise both its General Mapping target and the emulation's own; normal boots have
  `mnk_mode=false`.

Evidence: `rb_blitz_settings_screen_tests` 89,484 checks and `rb_blitz_input_bindings_tests` 80
checks, 41/41 ctest; one tracked driven boot, 56 steps, 0 failures
(`out/gameplay-mapping/in-game/p7-live4/`, `p7-live4-shots/`). What P7 did **not** do: retire
the executable, move the remaining non-input settings, drive the in-song page or a pad, or
recover a saved profile with every source already off (plan §16.5). Those are P8's.

#### Recovery, the in-song route, and parity (P8a, 2026-10-18)

P8a landed the three remaining in-game pieces (plan §18); the executable and packaging are still
P8b's, and no build, installer or payload file changed here. A profile that leaves no usable route
(Keyboard and Mouse both off, no pad) now says so on the caption and reopens the keyboard on
**one 2 s hold of F10** — the single exception to the "off blocks that device everywhere" rule,
changing exactly one stored switch. The notice is drawn even with Settings Labels off, and the
observation runs from the settings overlay as well as the input filter, because the filter does
not run while no device enumerates. The in-song Controls route is measured: a paused song reaches
pause → Help & Options → Controls → Input Types and a General Mapping bind page, the listening
dialog's hold-to-cancel works there, and the route returns to the pause panel and resumes with no
guest fault. The parity inventory is verified against the build (plan §17.1/§18.5): every
external-app row has an in-game destination or a written retirement.

The executable kept shipping until **P8b** (2026-10-19, below), which removed
`rb_blitz_controller_settings.exe`, its target, tests, scripts, payload entry and installer
shortcuts. The four presentation and two audio host rows stay
profile-carried with `rb_blitz.toml`/CLI as their documented editor; `present_letterbox` is a
developer cvar only, and `monitor` is obsolete window geometry (the game fits its one window from
the system DPI and the primary display). The seven-row profile allowlist is in
`src/settings/cvars.h`.

Evidence: `scripts/acceptance_recovery.ps1` (11 steps, 0 failures; `p8a-recovery2` and the
final-binary re-run `p8a-recovery3`) and `scripts/acceptance_controls_in_song.ps1` (23 steps, 0
failures; `p8a-in-song3` and `p8a-in-song4`), all under `out/gameplay-mapping/in-game/`;
`input_gates` 276 checks; 41/41 ctest. **Still open:** a physical
pad (capture, hold-to-cancel, stick directions, and the recovery's pad-connected arm), an in-song
page on the Ultimate payload, and a live refusal of a switch locked by the command line
([known-issues.md](../reference/known-issues.md)).

#### Retiring the executable (P8b, 2026-10-19)

P8b removed the external settings application and finished its acceptance obligations (plan §19).
The deliverable is now one executable:

* **Build.** `controller-settings/` is gone — target, `main.cpp`, the `.rc`, `config/settings.toml`,
  `src/`, `tools/` and `README.md` — and so is `add_subdirectory(controller-settings)` in the root
  `CMakeLists.txt`. The two app-only test targets (`controller_settings_general`,
  `controller_settings_session`) and the app-only scripts (`capture_controller_settings.ps1`,
  `acceptance_controller_settings.ps1`) went with it. The two suites that test game code kept their
  checks under game-owned names: `profile` (`rb_blitz_profile_tests`) and `profile_remap`
  (`rb_blitz_profile_remap_tests`).
* **Installer and payload.** `make_payload.ps1` and `payload_provenance.ps1` list `rb_blitz.exe`
  and the DLLs only; `build.ps1` lost `-SkipControllerSettings`/`-ControllerSettingsTarget` and its
  Controller Settings step; `install.h` expects one payload executable; `setup.iss` lost the
  executable define, the *Open Controller Settings* finish-page radio and the
  `RUNATEND=controller`/`launcher` values. **An upgrade removes** the installer-owned
  `rb_blitz_controller_settings.exe` and the *Rock Band Blitz Controller Settings* Start-menu/
  desktop shortcuts through a narrow `[InstallDelete]`; profiles, `paths.toml`, saves and game data
  are not touched.
* **Profiles still work.** `launcher.toml` remains the game's input profile (the P1/P7 tables), and
  the legacy path/portable-marker names stay honoured (`docs/settings/contracts.md` §8). The seven
  host-only rows stay profile-carried with `rb_blitz.toml`/CLI as their documented editor.

Evidence (2026-10-19): `ctest --test-dir out/build/win-amd64-debug` **37/37 pass** (41/41 before
the pass: four app-only suites removed, two renamed); the release tree configures and builds
`rb_blitz.exe` with no settings target; `ctest --preset installer-release` **419 checks, 0 failed**;
a fresh payload snapshot is **8 files / 59.0 MiB** with no settings executable and
`install-manifest.toml` says `executables = ["rb_blitz.exe"]`; `installer/build.ps1` produced
`RockBandBlitzSetup-0.1.0.exe` (24,134,728 bytes); a silent install into a scratch folder exited
0 with no settings binary, a reinstall over a folder that held one removed it, and the installed
game booted to `settings_boot: presented menu frame` with the input store live
(`out/p8b-install/`); `/RUNATEND=controller` is refused (exit 7, nothing installed);
`scripts/audit_distributable.ps1` is **7/7 pass**. The plan's §19 has the full table and,
plainly, the two legs not run on this machine (a photographed finish page and a hand-created
pre-2026-10-14 shortcut pair).

#### Boot speed and one frozen picture (owner report, 2026-10-13, later)

* **The freeze** (seen once) is a GPU command-processor packet desync: the log ends with
  `ExecutePacketType3 overflow …` / `**** PRIMARY RINGBUFFER: Failed to execute packet.` and no frame
  is presented again. The session had the audit harness pressing keys through the boot and the start
  while discovery ran, and the title re-entered the A/V load three times (13–24 s apart); five later
  boots, two replaying that route, completed — a **rare desync**, not a settings bug
  ([known-issues.md](../reference/known-issues.md) has the signature).
* **The wait was the DLC discovery, not the A/V load** (~2 s on an empty library; ~6 minutes on the
  owner's 1,402 packages, because of the SDK's 100 ms emulated mount latency per completion, a full
  host walk every boot, and one-item-per-frame enumeration). All three are now fast by default:
  `enhancements_dlc_cache` **on**, `deferred_overlapped_delay_ms` **0 every boot**
  (`src/hooks/dlc.cpp`), `enhancements_dlc_uncap_enumeration` **on** — measured walk ~0.3 s and
  **39 s for 2,804 items** with the frame rate free.

#### Row descriptions — a caption for the highlighted row (owner request, 2026-10-13, latest)

While a row is highlighted, a one-or-two-line description appears near the bottom of the screen for
**every** row — merged, Ultimate and the adopted retail pages alike. It is an event, not a guess: the
generated DTA publishes the highlighted row's canonical symbol over the settings channel
(`rbbz-settings/help/<symbol>/-`, the new one-way `help` op; `help/-/-` when the page is left), so the
caption cannot lag the highlight.

* **Our pages**: the text lives beside the row definition (~70 descriptions in
  [settings_screen.cpp](../../src/ui/settings_screen.cpp), as `screen::HelpTextFor`), authored into the
  generated tree (`HelpWrite`/`EnterHelpWrite`).
* **Retail and in-song pages**: [help_observer.cpp](../../src/hooks/help_observer.cpp) reads the
  frame's screen and focused list once per frame (state [native_menu.cpp](../../src/input/native_menu.cpp)
  already publishes) and resolves it against [help_topics.cpp](../../src/ui/help_topics.cpp); it clears
  only captions it published, so the guest's own entry event is never erased.
* **The overlay**: [help_overlay.cpp](../../src/ui/help_overlay.cpp), an SDK `ImGuiDialog` (the
  achievement toast's mechanism) bottom-centre, wrapped at 72% of the display size, with
  `settings_help_overlay` (default on) re-read every frame.

Evidence: `settings_screen` **26,518 checks** and `settings_bridge` **343 checks**, both exit 0.
**Not yet seen by a human**: the caption still has to be driven on a running screen.

##### P4 of the installed-game review: appearance, the caption-free pages, and the switch (2026-10-14)

The owner's report (R3/R4/R7 of
[2026-10-14-installed-game-fix-prompts.md](../plans/finished/2026-10-14-installed-game-fix-prompts.md))
was that the caption read as small, technical and boxed; that it competed with the calibration wizard
and with the restart/save-error/confirmation messages; and that the achievement action's wording was
wrong. All three are now measured in a boot rather than asserted.

**What the caption is now** (`src/ui/help_overlay.cpp`):

| | Before | Now | Why |
| --- | --- | --- | --- |
| Typeface | the drawer's own ProggyTiny at 10 px | a system sans — `segoeui.ttf`, then `tahoma.ttf`, then `arial.ttf` under `%WINDIR%\Fonts` — with the SDK's font as the fallback | the caption is player-facing text, not a debug readout |
| Size | fixed 10 px | **2.8% of the display height**, floored at 16 px and capped at the 40 px the atlas bakes | a proportion keeps the caption's size relative to the game's own UI at 720p, 1080p, ultrawide and at every DPI scale |
| Panel | `SetNextWindowBgAlpha(0.72)` | `0.32`, with rounding and padding derived from the drawn size | it is a subtitle, not a slab the size of a paragraph |
| Position | bottom margin `max(24, 6% of height)` — inside the A/B prompt strip | bottom margin `max(56, 13.5% of height)` | clear of the A/B prompts, still below the menu rows |
| Wrapping | 72% of the display, min 320 px | unchanged | long captions wrap the same way; only the type grew |

The face is added to the drawer's own atlas in the app's new `OnConfigureFonts` override, because that
is the only moment an `ImFontAtlas` may still be added to. The atlas is baked at 40 px and the caption
is drawn **smaller**; that is deliberate, not arbitrary: this build's ImGui backend is a **legacy** one
(no `ImGuiBackendFlags_RendererHasTextures`), so `ImFontAtlasBuildLegacyPreloadAllGlyphRanges` bakes
every font at its `LegacySize` and a different requested size is *scaled from the nearest bake* —
baking large and drawing small is what keeps the text crisp, and drawing larger than the bake would
soften it. Boot evidence: `help_overlay: caption font 'segoeui.ttf' baked at 40 px`, in every run of
`out/_swap/p4/`.

**The caption-free pages.** A page whose definition carries a message token — the restart
confirmation, the save-error report and the achievements confirmation, each in both contexts, six in
all — now publishes the *hide* on entry and on every move instead of a row's description, and its
presses announce nothing. That covers the stale-caption case the prompt names: entering the
confirmation from the root's Cancel used to leave the departed row's description on screen over the
message. Calibration is the other exclusion: `cal_welcome_screen` was dropped from
`help_topics.cpp`'s resolver **and** from `KnowsScreen`, so the observer's transition rule clears the
caption on the way in rather than describing the wizard. Ordinary pages are unaffected, and the tests
assert both halves — the six suppressed pages are counted, and no other page lost its caption.

**Interface → Settings Labels** (`interface.settings_labels`, default **On**). New stable id
`SettingId::kSettingsLabels = 51` (nothing renumbered); schema row on the Interface page; storage
`[interface] settings_labels`; the boot writes it into `settings_help_overlay` at the config rank
(`CvarForSetting`), and the P9 live applier writes the same cvar on commit. It is the surface's
**second `hot` row** — the overlay re-reads its cvar every draw — so the effect is immediate and no
restart prompt appears. Measured in `out/_swap/p4/labels-off/`:

| Observation | Value |
| --- | --- |
| The row and the caption, before | `shots/02-interface-labels.png`: **Settings Labels: On** highlighted, caption drawn |
| The press | `labels-off.log`: `hot_apply: settings_help_overlay = false (was true), applied to the running game` and `settings_bridge: applied 'interface.settings_labels' to the running game; the row carries no pending state` |
| The effect | `shots/03-labels-off.png`: **Settings Labels: Off** and **no caption**, in the same screen |
| No restart | the log has **no** `request_restart` line; the route leaves the root and reaches the main menu (`shots/04-back-root.png`, `05-main-menu.png`) |
| Durable | `settings.toml` gains `[interface] settings_labels = false` + `'interface.settings_labels' = 'manual'` |
| Survives the relaunch | a **second** run of the same route on the same root draws **no** caption at the root (`shots-2/01-root.png`), and turning the row back on restores it live (`shots-2/03-labels-off.png`, `hot_apply: settings_help_overlay = true (was false)`) |

**The wording (R7).** The action row and its confirmation button both read exactly
**Unlock Online Achievements** now (`src/ui/settings_screen.cpp`, `src/settings/schema.cpp`); the old
`Unlock Online Only Achievements` was 31 characters, over the 28-character line budget, so its row
was drawn without its value. `[tree]` the action id, its bridge token (`invoke
general.unlock_online_achievements`) and the grant itself are unchanged. Boot evidence:
`out/_swap/p4/achievements/shots/02-unlock-online.png` (Ultimate target) shows the page with
**Unlock All: On** and **Unlock Online Achievements** beside their captions. The retired wording is
still the title's own locale string (`os_online_acc`), which `menu_options` handles and which this
change deliberately does not rewrite.

**Honest limits.** No in-song (pause) capture was taken, so the caption-free behaviour of the
confirmation pages' *pause* twins is pinned by the tests (their definitions are generated and
asserted) rather than seen. No 1920x1080 shot; the two resolutions captured are 1280x720 and the
21:9 primary's 2560x1080 (2354x993 client). The fallback path — no system sans readable — was not
exercised on this machine (all three candidates exist); it is a `REXLOG_WARN` plus the SDK's font at
the same derived size, pinned by inspection rather than by a run. The save-error confirmation is
generated but was not driven (producing a real write failure on demand is its own experiment). The
reproducer is `out/_swap/p4/run.ps1 -Label captions|labels-off|achievements`.

##### Mouse hover publishes the row caption (measured 2026-10-19)

The native mouse hover changed the selected row by calling `UIList::SetSelected` and then
`ScrollStart`/`ScrollComplete` directly, so the generated panel's `move_up`/`move_down` methods - the
only place a caption was published - never ran. The highlight followed the pointer while the overlay
kept the departed row's description: hovering **Calibration** left *Watch the game's how-to-play
tutorials.* on screen over it.

The fix is the engine's own selection-scroll notification. Every ordinary panel now carries a
`SCROLL_MSG` member (`ScrollHelpCases` in [settings_screen.cpp](../../src/ui/settings_screen.cpp))
whose switch maps `{options.lst selected_sym}` to the row's canonical help symbol. Retail's
`powerup_select_panel` updates its description from the same message and `selected_sym`
(`out/_swap/powerup_select.tree.txt`, decoded 2026-10-19), and the host's hover already calls
`ScrollStart`. The restart/save-error/achievements pages carry no member, so a scroll there cannot
put a description over their message. `rb_blitz_settings_screen_tests` pins one `SCROLL_MSG` table per
ordinary page, none on the six suppressed pages, and one `HelpWrite` per selectable symbol (107,654
checks, exit 0).

Measured against the release build, base install, isolated `--user_data_root`, at the merged root:
hovering **Calibration** changed the caption to *Line up the audio and video so notes hit in time
with the music.*, hovering **Audio/Video** to *Audio, video, and graphics settings.*, and on General
hovering **Interface** to *Main-menu visibility and the settings caption.* - each without a click.
Evidence: `out/live-scrollmsg/scrollmsg-hover-calibration.png`,
`scrollmsg-hover-av.png` and `scrollmsg-hover-interface.png`.


#### How the test tree stands after the Vulkan preset change (measured 2026-10-13, later)

* **No Windows preset can build a test right now**: `REXGLUE_USE_VULKAN=ON` makes the SDK's glslang
  guard (`set(BUILD_TESTING OFF CACHE BOOL "" FORCE)` in
  [rexglue-sdk/thirdparty/CMakeLists.txt](../../rexglue-sdk/thirdparty/CMakeLists.txt)) write
  `BUILD_TESTING=OFF` into the cache, and the SDK is configured
  ([CMakeLists.txt](../../CMakeLists.txt) line 36) *before* `include(CTest)` (line 341), so **37/37
  cannot be reproduced from the presets**; a Vulkan-off tree configured with `-DBUILD_TESTING=ON`
  builds every target and runs **37/37**.
* **Three settings tests were stale about `general.cache_dlc_library` (fixed)**: the row defaults to
  true ([schema.cpp](../../src/settings/schema.cpp)) and a commit writing the value a row already
  holds is `kNoChanges` ([service.cpp](../../src/settings/service.cpp), line 549), so `settings_service`,
  `settings_migration` and `settings_bridge_service` now stage the row's *other* value (the migration
  case expects one more import). `settings_bridge_service` used to index an empty change list and
  **abort**; it now passes with 91 checks.
* **One flake**: `game_restart`'s child-wait case measured 0 ms once during a parallel build and
  passed 185/185 twice on an idle machine.
* **2026-10-14 (P6 of the installed-game review)**: `content_progress` is the **39th** test — the
  pipeline's own model (one count per real aggregate enumerator, terminators/pending/failures
  excluded, the bar's two numbers summed per attempt across the batches, reset per attempt — since
  2026-10-15 pushed into the title's own bar, not an overlay) is now pinned rather than described.
  The Vulkan-off tree is **39/39** re-run on the final P6 sources (`ctest --test-dir
  out\build\rename-tests`), and `dlc_cache` grew the v2-checksum, refusal and writer cases. The 37/37
  and 38/38 counts elsewhere on this page are those days' measurements, not today's.

### 3. What is left, by package

#### P1 — retail screen + bridge feasibility

1. **The `read_file` response channel is not built.** The guest can be asked questions today;
   producing a *string*/*array* answer needs the guest DataNode build API and its allocator/refcount
   rules, which are untraced — the largest ABI risk left in the transport. *`architecture.md` §5,
   §10 step 5* — do not build it until `sub_824193A8`'s return path is traced and exercised with a
   disposable guest call.
2. **The row-widget binding is unstarted** — which `list_*.milo_xbox` row type draws each of the four
   row kinds. *`architecture.md` §10 step 4; `architecture.md` §9* — no step recorded.
3. **Appearance parity with Ultimate's own layout is a spike, not a result** — whether retail list
   resources can reproduce it without its custom MILO. *`plan.md` §2.2, §13*.

#### P2 — exact render-resolution feasibility

4. **No GPU capture**, and none taken since: it is the independent confirmation the instrumentation
   substitutes for. *`graphics.md` §7 ("Not done")*.
5. **Vulkan is instrumented but unproven** — every P2 measurement is D3D12; Vulkan is not even built
   here (`REXGLUE_USE_VULKAN=OFF`). *`graphics.md` §7; `graphics.md` §9*.
6. **A plugin-owned cvar cannot be set from `rb_blitz.toml` or the CLI** (config is applied before
   `rexgpu-xenos.dll` registers), which is why `resolution_scale` travels through the environment or a
   deferred write. *`graphics.md` §7* — P4/P11 own the route.

#### P4 — graphics capabilities, exact modes, first-run policy

7. **Variant B's `[[midasm_hook]]` is one coordinator entry away from proof**: `config/functions.toml`
   needs the entry at guest `0x823F382C` (`li r11,720` → 1080) plus the `RenderHeightHook` handler,
   then `out\_swap\p4\run-probe.ps1 -Label variantb` should show `guest_output=1920x1080`,
   `draw_scale=1x1`. *`graphics.md` §7, §9*.
8. **No host probe writes the capability snapshot** — the code that fills `CapabilityInputs` (logical
   processors, SDL display query, renderer build/detect/usable) does not exist; the interface is
   frozen in `capabilities.h`. *`graphics.md` §9; `architecture.md` §6* — coordinator, root
   `CMakeLists.txt`.
9. **The AA/FXAA visual matrix was never run**, so "what None/FXAA actually look like at 1080p" is
   unproved, and the AA row's labels rest on the policy, not on a capture. *`graphics.md` §9;
   `plan.md` §3.3*.
10. **No song was played at 1080p.** The 1080p evidence is the title screen and the draw stream; a
    song and a restart at 1080p are not captured. *`graphics.md` §9; `graphics.md` §5.2*.
11. **Graphics recovery is policy plus a marker shape, not a boot** — `RecoverGraphics` /
    `GraphicsBootMarker` are tested, but nothing writes the marker at boot yet. *`graphics.md` §9*.
12. **The per-backend AA/filtering ceiling matrix and the full menu/song/AA/restart matrix are
    untaken.** *`graphics.md` §5.2; `graphics.md` §9*.

#### P5b — the merge into Help & Options

13. **The in-song entry (§2). — CLOSED 2026-10-18 by P8a.** The shipped route does not build a
    screen that lists the in-song panels at all: retail's `pause_controls` case jumps to the pause
    twin of the in-game Controls menu (`src/settings/menu_payload.cpp`), which hosts no
    `pause_panel`/`game_panel` and therefore never hits the construct-order fault. A paused song was
    driven to Input Types, a bind page, hold-to-cancel, back to the pause panel and resume
    (`scripts/acceptance_controls_in_song.ps1`, 23 steps, `out/gameplay-mapping/in-game/p8a-in-song3/`).
    The original order below is kept as the research record; (a) and (b) were not needed.
    *`menu.md` §3.2, §3.3; gameplay-mapping plan §18.4*.
14. **The in-song child pages are unverified — CLOSED for Controls (P8a).** Controls → Input Types
    and a General Mapping bind page were seen on screen inside a song; the *other* categories'
    in-song pages (General/Interface/etc.) are still only unit-tested. *same doc, §3.3*.
15. **G1b's other half is only partly proven for the *in-song* context — CLOSED for Controls
    (P8a).** Back from the in-song Controls pages reaches the pause panel and B resumes the song
    (`panel=game_panel`); the main menu's own route was already measured. *`plan.md` §10.4*.

#### P6 — Ultimate adapter and saved settings

16. **The achievements action is not wired.** `general.unlock_online_achievements` is a one-shot guest
    action; the bridge answers `invoke` without calling the provider and says so at boot. It must not
    be presented as working until it is. *`architecture.md` §6.1* — blocked by P1 item 1 (a
    host-initiated guest call needs the allocator/refcount contract).
17. **The combined payload's shadowed records.** P5b's generator edits base `ui/gen/ui.dtb` and
    `ui/splash/gen/splash.dtb`, while the payload also carries its own copies; a patch record built
    from the base ark would wipe the mod's edits. The record must be extended from the **mounted**
    payload. *`architecture.md` §6.2; `plan.md` §2.4, §11*.
18. **`forceplot` / `which_skybox`: fixed 2026-10-15** (was: "the screen's static list is not the
    payload's inventory"). The token half was the smaller half. The two rows are the payload's
    *boolean-with-submenu* pair, this build draws the whole choice set as one select, and the
    provider's file stores the chosen **position** - so a codec that spelled them as booleans could
    not encode the value the menu had just written. On an installed copy that refusal landed in the
    commit journal, where recovery could never finish it, and **every** later edit answered "settings
    could not be saved" with the folder as its explanation. Fixed by `RowBinding::position_value`
    (token ⇄ position, both against the payload's inventory), by parking an uncompletable journal
    instead of refusing every save, and by a reason token so the page names the failure it saw.
    *`architecture.md` part 5 §3, §4; `docs/plans/finished/2026-10-14-installed-game-fix-prompts.md`
    (P7)*.
19. **`unlockall`'s write path does not exist** — its value belongs under native
    `[ultimate_overrides]`, which the bridge does not own; `EncodeProviderValues` skips it by design.
    *`architecture.md` §6.2; `architecture.md` §4, §6* — P11 owns it.
20. **Seven provider rows have no provider value** (no default, no file pair) and are reported absent
    — inventing one would overrule the payload. *`architecture.md` §6.2* — P11 decides whether the
    compiled default is seeded through the service.
21. **A merged install (mod files copied over the game root) is reported as absent** — it has no
    payload directory to inspect and no overlay to redirect through. *`architecture.md` §6.2*.
22. **The provider's second writer is unimplemented** — `ulti/gen/ulti_funcs.dtb`'s
    `ulti_settings_writer` writes a different shape to a path variable nothing inspected ever sets.
    *`architecture.md` §6.2*.
23. **A guest-side write through the redirect is unproven in a boot** (proven host-side and by
    construction). *`architecture.md` §5.2* — waits on the combined payload (item 17).
24. **The "verbatim" claim is not byte-exact for a nested-array value** (whitespace normalized); every
    measured file is atoms/symbols/strings/floats and stays byte-exact. *`architecture.md` §6.2*.

#### P7 — installer paths and terminology

25. **The wizard page needs eyes** — the two new rows' geometry was never looked at at 100/200/300 %
    DPI; fix `LayoutLocationFields` if the page overflows. *`menu.md` §5 item 1*.
26. **The updater leg was not run** — it needs the release manifest over the network, so "an update
    preserves an existing `paths.toml`" is unproven. *`menu.md` §5 item 2*.
27. **`settings::ResolvePathValue` decodes a stored path as ANSI** (`fs::path(value)`) while the
    helper writes `paths.toml` as UTF-8, so a non-ASCII save folder resolves differently in game vs
    installer. One-line fix: `fs::path(Widen(value))` in P3's file. *`menu.md` §5 item 3;
    `plan.md` §10.1 (P7)*.
28. **The acceptance scripts' output-directory assumptions are unre-checked** against the merged
    screen. *`menu.md` §5 item 4*.

**2026-10-14, after the owner's installer review** (not a numbered item: the numbers are the standing
list). The installer is administrative and defaults to `C:\Program Files\Rock Band Blitz`, so
`scripts/acceptance_controller_settings.ps1` now passes `/CURRENTUSER` to stay unattended; both folder
boxes default to `Documents\Rock Band Blitz\...` and cannot be left empty; the wizard creates two
shortcuts, both the game, so `startmenucontroller` / `controllericon` / `/LAUNCHERICON` /
`/CONTROLLERICON` are gone; and the mod page says how to add the mod later. Item 25 got *worse* rather
than better - the page gained a wider header gap, a wider status gap and a note label without the DPI
check it asks for ever having been run - and item 26 (the updater leg) now also has to answer a UAC
prompt, because the updater is administrative too. The narrowing of the legacy-profile prefill is in
[`../reference/known-issues.md`](../reference/known-issues.md); the new shape is in
`../../installer/README.md`.

#### P8 — Controller Settings reduction and Guide removal

29. **The full surface is still the default surface**; the reduced one is behind `--controller-only`,
    and the cutover is P11's. *`contracts.md` §9.3; `menu.md` (Status)*.
30. **The non-controller tabs/rows are not deleted** (the reduced surface merely draws none of them).
    *`menu.md` §1, §3.1; `plan.md` §10.1 (P8)*.
31. **The Guide filter is not runtime-proven** — the change is in the game executable, and the owner's
    running session held `rb_blitz.exe` open so it could not be relinked. *`menu.md` §4.5* — rebuild
    and run the filter.

#### P9 — hot apply and restart coordinator

32. ~~**The authored restart prompt does not exist**~~ — **built, and repaired 2026-10-14 (P3 of the
    installed-game review)**: `settings_restart_screen` exists, the merged root's Cancel asks
    `snapshot/restart.pending` to open it, and its *Restart now* row is `Kind::kRestart`. P3 fixed the
    two defects in the first version: the confirmation's Cancel returned to the **root it was leaving**
    (so Help & Options could not be left — reproduced in `out/_swap/p3/repro3-close/`, fixed in
    `after-close/`) and a refused restart left `restart.ok` true, which made the row silently inert.
    *`installed-game-fix-prompts.md` P3; `architecture.md` §6.1*. **Still open: only the main-menu
    context has been driven** — the in-song twin uses the same path but no boot has been taken from a
    song, and a *genuine* refusal (a child that cannot be started) is pinned by host tests rather than
    by a run.
33. **The Target row's soft restart is offered by leaving, not by the row** — the row commits and
    leaves; the prompt then appears when Help & Options is left, which is now a path the player can
    complete (item 32) — so item 32 unblocked this one in the practical sense, and the row still does
    not prompt by itself. *`installed-game-fix-prompts.md` P3; `plan.md` §3.1*.
34. ~~**`src/main.cpp` never calls `platform::RunBootstrap`, and nothing calls
    `RestartCoordinator::Request`.**~~ — **both wired** (P11a): `RestartBootstrap` runs before
    `wWinMain`, and the boot's restart handler drains the DLC refresh, requests the child on the UI
    thread and defers the close. Verified driven 2026-10-14 (`out/_swap/p3/after-restart/`): one child,
    the parent exits 0.2 s later, a new boot 0.4 s after that. *`architecture.md` §6.2*.
35. **The live apply still runs against P1's `MemorySink`**; it must move onto
    `Service::last_commit_changes()` now that the real store is wired. *`architecture.md` §6.3*.
36. **Coordinator limits to state honestly:** the parent/child handshake proves only that the spawn
    succeeded; `WaitForExit` is bounded at 30 s and a timeout refuses the start; a reused pid is
    refused by comparing creation time. *`architecture.md` §6.4*.

#### P10 — merged-menu compatibility

37. **Controller Settings-side rows are still there** — the retired rename toggle and the pool-size row live
    on in `controller-settings/config/settings.toml`, inert in game and removed from neither. *`menu.md` §7;
    `plan.md` §10.1 (P10)* — waits on P8/P11.
38. **The alternate in-menu refresh action is unbuilt** — hiding `splash_dlc` while it carries the
    refresh leaves the refresh with no main-menu row (the boot warns). *`menu.md` §7; `plan.md` §3.5*
    — a row under Downloadable Content calling the same sentinel.
39. **The descriptor list is never filled at runtime** — `MenuInventory` is tested but uncalled, so
    the Hide Items menu has never drawn from the live menu. *`menu.md` §7; `architecture.md` §6* —
    the caller must hold `menu_filter.cpp`'s read buffer; P11 wires it.
40. **A 50-package pool has never been booted** since the cap moved from 3 to 50, so package-handle
    lifetime at that size is unproven. *`menu.md` §7; `plan.md` §10.1 (P10)*.

#### P11 — integration and default cutover

41. **P11b is the whole remaining cutover** — the new path becomes the default, the old full surface
    UI goes, packaging/patch-record generation, and consistent boot/settings/installer behaviour.
    *`plan.md` §10.1 (P11), §13* — P5b–P10 first.
42. **The merged root's delivery is not packaged**: the patch record must be generated by extending an
    installed retail record, and the list resource must survive a base-only install with no Ultimate
    folder. *`plan.md` §11 (P11)* — blocked by item 17.
43. **Resolution and Anti-Aliasing are stored but not applied at boot**, `unlock_all` is not written,
    and the prompt never issues a restart request — the boot log says each of these out loud.
    *`architecture.md` §4, §6* — unblocked by items 7, 8, 19, 32.

#### P12 — release acceptance and documentation

44. **Nothing in the acceptance section is marked passed.** The §12.1 host matrix and the §12.2
    runtime/installation matrix (merged root, hot vs restart rows, exact graphics, Ultimate
    persistence, restart A/B, installer, Controller Settings, portable, integrity) are all unrun.
    *`plan.md` §12* — blocked by P11; the doc already fixes the record shape (command, build/config
    identity, expected/actual, evidence path).

#### Cross-cutting, not owned by a package

45. **Trial disposition** — prior work reports Trial crashing before menu entry; it must be
    reproduced, recorded, and either repaired or kept unavailable, never silently shipped.
    *`plan.md` §3.1, §13; `contracts.md` §12*.
46. **Locales** — the existing transforms are English-specific; project-owned keys need English
    fallbacks and the supported locales need a run with no missing-token crash. *`plan.md` §13*.
47. **Three placement decisions to confirm** (each has a documented default): the **Renderer** row
    (omit from the tree, or keep in Video as `restart`), **Overscan** (moved into Video; record the
    deviation if the retail page cannot lose it cleanly), and the **achievements action's** placement
    (keep both in the same submenu). *`plan.md` §3.7* — owner decision.
48. **AA translation on both backends, and the Disable-Plots vs Force-Plot precedence** — to be
    verified against provider behaviour before any different rule. *`plan.md` §3.3, §3.4*.
49. **The first-run recommendation policy is still policy, not a measurement** — a versioned policy
    backed by repeatable measurements is what makes the feature finished. *`plan.md` §7.4*.

### Remaining acceptance work

1. **Run the final acceptance matrix when requested.** The suites are **37/37 green** (2026-10-13,
   later) in the Vulkan-off Debug tree the note above describes — captions and the three repaired
   settings tests included — but cannot be measured from the presets as they stand. What has not
   happened is the *interactive* pass: no one has driven the merged menu since the punch list, so
   items 2, 3, 6/7 and 10 are asserted by tests and records, not by a person at the screen. The matrix
   still needs both base and Ultimate launches, settings save and direct-boot readback, restart A/B,
   target change through the in-game row, and the 1080p path.
2. **Graphics recovery is not wired.** The first-run and last-known-good policy functions exist, but
   boot does not maintain a durable "attempted/presented" marker, so a failed graphics boot cannot
   automatically restore the safe 720p/no-AA combination.
3. **Release checks remain.** Packaged archive generation against base-only and Ultimate installs,
   installer path handling, Controller Settings launch/update behavior, and the no-copied-content
   constraint on the final distributable — integration checks, not blockers to the early settings
   load path.
4. ~~**Refresh-row edge case.**~~ **Superseded 2026-10-13**: the refresh action is the main menu's
   *Refresh Music Library* row now (owner decision), not a settings row, so "hiding `splash_dlc` still
   leaves the settings-side refresh reachable" is no longer a question — action and row are the same
   row. What still needs a populated-library look is that the row refreshes the library after the
   *DLC Menu Songs* retirement — **superseded 2026-10-14**: the row is no longer retired (§5), so the
   look is owed on a boot whose DLC page shows both rows
   ([play-current-song.md](../plans/finished/2026-10-14-play-current-song.md)).

### Boot ownership

| When | Work |
| --- | --- |
| `OnConfigurePaths` | Resolve identity and path roots; migrate/load the store; apply target, renderer selection, and native settings. |
| `OnPreSetup` | Load the selected GPU plugin, apply its saved flags, plan the title guest mode and draw scale, and set the guest-height hook before graphics setup. |
| `OnPostLoadXexImage` | Inspect the mounted provider, install the generated menu archive, configure the bridge, and attach restart handling. |
| Guest menu interaction | Read/edit settings through the typed channel; commit durably before acknowledging; invoke live VSync or offer restart for restart-scoped changes. |

The retail "Loading Rock Band Blitz audio/video settings…" string belongs to the game's own menu
flow. This feature does not introduce an A-button-time settings-load screen: the native store is
already loaded before game processing begins.

For historical experiments and acceptance scripts, see [plan.md](plan.md), [menu.md](menu.md),
[graphics.md](graphics.md) and [architecture.md](architecture.md).

## Part 2 — Session status, 2026-10-12 (was `docs/plans/in-game-settings-status-2026-10-12.md`)

Supersedes nothing; complements the handover in Part 3. That document's ledger is still accurate
except where this one says otherwise.

**Headline: the black startup was a two-part archive.** The generated Help & Options payload grew the
title's patch archive by *adding a second part*, and no archive this title ships or reads has ever had
more than one. The guest then re-read the generated part forever. Extending the mounted part in place
instead — one part in, one part out — fixes it: **an Ultimate install now boots and opens the merged
Help & Options screen with the provider active.**

### What this session changed (working tree, uncommitted)

| File | Change |
| --- | --- |
| `src/ui/settings_screen.cpp` | `RequestPaths()` lists `rbbz-settings/cancel/-/-` (fixes `settings_screen`). |
| `tests/controller_settings_general_tests.cpp` | `Scratch::Make()` returns `.lexically_normal()` (fixes `controller_settings_general`). |
| `src/hooks/menu_filter.cpp` | `HandleRead()` captures `length`/`buffer`/`offset_ptr` **before** the original `NtReadFile` (caller-saved registers; the scan is linear in `length`). |
| `src/settings/menu_payload.{h,cpp}` | `ExtendMenuArchive()` carries the mounted part's bytes verbatim, appends our records and keeps **one part** (same name, new size, existing offsets unchanged); with no mounted part it appends a part, as before. |
| `src/hooks/settings_payload.cpp` | Reads the mounted part through the mount view; writes the extended part where the archive names it. |
| `tests/menu_payload_tests.cpp` + `CMakeLists.txt` | New `menu_payload` ctest (35 checks): one part in/out, verbatim carry, every record inside its part, mismatched sizes left alone — parsing the header with the product's own v6 reader. |

`ctest` is **36/36 green** on the current sources, and the game and Controller Settings link.

### The black startup: cause and fix

#### Reproduction (disposable, no real profile touched)

`out/_swap/settings-audit/stall-probe.ps1` boots the current build against a fresh
`--user_data_root`, an **empty pinned** `--launcher_profile`, and empty DLC roots, then samples the
game's threads once the log goes quiet. `boot-check.ps1` is the same shape with window captures, and
`menu-check.ps1` drives the merged screen.

```powershell
pwsh -File out\_swap\settings-audit\stall-probe.ps1 -Target ultimate -Resolution 1280x720 -Label x
pwsh -File out\_swap\settings-audit\menu-check.ps1  -Target ultimate
```

**`--launcher_profile` is not optional in a disposable arm.** Without it the boot loads the
developer's own `%APPDATA%\rb_blitz\launcher.toml`, whose save-location row outranks
`--user_data_root` — the first run here read *and rewrote* the real `settings.toml` (a saved
resolution switched to Auto; the file is intact otherwise, and Auto resolves to the same pixels on
this display). Always pin an empty profile.

#### The isolation

| Arm | Result |
| --- | --- |
| base, 720p and Auto | presents the title screen, ~53 s |
| Ultimate, 720p and Auto | never presents; kills at 200–300 s |
| Ultimate, all four menu-filter toggles off | still never presents |
| Ultimate, provider-row reroute disabled | still never presents |
| Ultimate, each edited record left installed in turn | still never presents |
| Ultimate with the menu payload install skipped | presents in 47 s |
| **Ultimate with the payload installed as one extended part** | **presents in 50 s, merged root opens** |

So the install step was the cause and the part count the defect inside it. The base game installs the
same 14 records over an empty provider index — one part either way — and was never affected.

#### What the guest does while it hangs

The guest thread spins at ~85% of a core while it hangs. Resolving sampled RIPs against a link map
(`/MAP`, added temporarily and reverted; helper `out/_swap/settings-audit/resolve_map.py`) put the loop
inside our own `rb_blitz::menu_options::LooksLikeDtb`, called from `menu_filter`'s `HandleRead`;
instrumenting the hook showed the shape: **~540,000 reads, ~65 KiB each, 34 GB scanned**, one call
site, one file handle and a *constant* byte offset per arm (`0`, `1114112` = `0x110000`, inside the
generated `settings_xbox_0.ark`, 1,138,458 bytes). The title re-reads one record of the extended
archive forever and never reaches `VdSetInterruptCallback`. Two separate things follow:

1. **The guest's retry loop is the bug** — Ultimate-only, and it appeared with the settings payload.
2. **Our scan multiplies it** — `HandleRead` tests every byte with a non-inlined call, ~0.7 ms per
   64 KiB read: invisible in a healthy boot, fatal under a retry loop, which is why the symptom
   looked like a total hang rather than a slow boot.

#### Why one part

Every archive this title reads declares exactly one part (measured 2026-10-12): `main_xbox.hdr` is
`ARK v6, 1 part(s), 1613 entries` (`main_xbox_0.ark`, 361,769,177 bytes) and the provider's
`patch_xbox.hdr` is `ARK v6, 1 part(s), 62 entries` (`patch_xbox_0.ark`, 14,171,207 bytes) —
`python scripts/hmx_ark.py list --hdr <file>` prints both. The runtime's reader
(`settings/ultimate_adapter.cpp`) and the offline tool both implement the multi-part *layout*
correctly, and every count echoed in our header was well-formed, so the defect was never a malformed
header: the title's patch loader does not follow a second part, and a boot that offers it one cannot
finish. The base-mode payload (`out/_swap/p5b/make_payload.py`) had always written a one-part archive;
only the runtime extension introduced a second. After the change:

```
cache/settings-menu/<pid>/gen/patch_xbox.hdr    ARK v6, 1 part(s), 73 entries
cache/settings-menu/<pid>/gen/patch_xbox_0.ark  15309674 bytes   (14171207 mounted + 1138467 ours)
```

73 rather than 76 because three of our records already exist in the provider's payload and are replaced
in place; every record fits inside the single part, which is what the new test pins.

#### Not the fix (kept as hardening)

`HandleRead()` used to read `ctx.r8/r9/r10` *after* calling the original `NtReadFile` — caller-saved
registers, and the scan is quadratic in `length`, so a stale `0xFFFFFFFF` there is a
four-billion-iteration loop. Capturing them before the call is correct regardless, but it is not what
fixed this boot (the hang was unchanged, and the measured reads were a legitimate 64 KiB each).

### What is verified now

* 36/36 ctest on the current sources; `rb_blitz` and `rb_blitz_controller_settings` link.
* **Base mode** presents in ~54 s with the payload installed
  (`settings_menu: mounted 14 generated records over 0 provider records`).
* **Ultimate mode** presents in ~50 s over the provider's own archive
  (`mounted 14 generated records over 62 provider records in 411 ms`), the provider still reported as
  `6/6 records, 8 panels, 10 values` and the bridge live (`typed guest channel on`,
  `provider active - 9 row value(s) seeded`).
* **The merged Help & Options screen opens in Ultimate**: from the title screen, `HELP & OPTIONS`
  lands on the merged root with all six rows — General, Controls, Calibration, Audio/Video, Gameplay,
  Interface — General selected (`out/_swap/settings-audit/menu-ultimate/shots/04-merged-root.png`);
  moving down and pressing A enters the page and Back is offered (`05-child.png`: retail's
  `CONTROLLER PRESETS`, which is what the Controls row reaches).
* The early-resolution path still runs before graphics setup (`resolution_scale`, `native_2x_msaa`,
  `swap_post_effect`, `anisotropic_override` written pre-presentation; the graphics-boot marker ends
  `pending = false` with `last_good` filled in), and the safe-graphics route and recovery marker
  behave as Part 3 describes.

### What is NOT verified (and is the remaining work)

Ordered by what blocks a release:

1. **The interactive matrix** — nothing has been driven in a running game in either target:
   horizontal-selector editing and the editing indicator, boolean save, live VSync, the restart
   prompt's close/accept/failure paths, target changes, direct-boot readback, provider saves,
   Unlock All timing, and in-song navigation through every child screen and back. `menu-check.ps1` is
   the template, `scripts/drive_ui.ps1` + `capture_window.ps1` the tools.
2. **In-song entry** — the pause context is the one P5b measured as not re-pointable; the pause record
   is still left pristine.
3. **Restart input guarding** — the prompt defaults to Close, not the planned A=restart / B=close with
   a release guard.
4. **Auto vs restart-needed semantics** — `requested Auto` is `0x0` while the boot-effective size is
   non-zero, so a naive comparison can mark Auto as needing a restart; review with the guard code.
5. **Provider transaction/recovery audit** — same-key conflict checking, merged installs,
   inactive-provider detection, and whether the production bridge consults
   `Service::RequestRestart()`.
6. **Safe-graphics recovery with an induced failure** (no endless retries, no preference destruction,
   no false recovery after an ordinary crash or a clean close).
7. **Product cleanup** — Controller Settings cutover and dead UI/schema, labels/localisation,
   hidden-DLC refresh access, installer/distribution checks; the ~15 MB generated part written into
   the cache root per boot is deliberate (no retail bytes shipped) but belongs in a performance pass.
8. **Docs** — the tracker still carried claims this session contradicts (it said graphics recovery
   "is not wired"; it is).

### Housekeeping

* The real `%APPDATA%\rb_blitz\settings.toml` was touched by one early misconfigured arm: a saved
  manual resolution was switched to `auto` with automatic provenance and the
  `.graphics-boot.toml`/lock siblings were created. On this 2560x1080 display Auto resolves to the
  same pixels; set it back by hand if a manual entry is wanted.
* Temporary investigation code (a `/MAP` link option, the `RBBZ_SKIP_*` gates, the read/open
  diagnostics) was **removed**. The two ctest fixes and the `HandleRead` capture are committed as
  `460c01e`; the part-count fix, the new `menu_payload` test and the corrections to these docs are
  the uncommitted diff.
* Scratch: `out/_swap/settings-audit/` (one directory per arm with its log and captures). Nothing
  here is acceptance evidence.

## Part 3 — Handover, 2026-10-12 (was `docs/plans/in-game-settings-handover-2026-10-12.md`)

**This part is superseded by Part 1 wherever the two disagree** (the two ctests it lists as failing
were fixed, the Ultimate boot hang was fixed at its cause, and the merged screen now opens). Its
still-true content — the owner's intent, the operational rules, the verification limitations and the
order to continue in — is kept below.

**The feature is unfinished. Do not report release acceptance or a clean test suite.** The user
explicitly requested this handover because the session was running low on credits. Stop
implementation for this handover; the next agent should resume from the working tree.

### User intent and latest decisions

The original request was to finish the in-game settings feature described by the planning MDs and make
startup-critical settings apply before game processing; the user noticed the retail "Loading settings"
message *after* pressing A and questioned whether resolution was applied too late. This audit found
substantial missing wiring and bugs. The resolution behavior superseded the old benchmark
requirement: resolution defaults to Auto, which picks the best size for the current display and
applies it immediately at startup; a compatible manual choice survives later boots, and one that
becomes incompatible (for example, 4K saved but now a 1080p display) switches back to Auto.

No GPU benchmark is wanted — a display compatibility policy, not a performance claim — and manual
graphics preferences still survive temporary recovery/fallback. Another session's ultrawide work in
this workspace must be **preserved**, and edits were committed/staged while this audit ran: do not
reset, unstage, or replace the working tree from an older commit.

### Workspace and operational details

- Repository: `<checkout>`; PowerShell
  (`exec_command` with `login: false` avoids a slow shell startup).
- HEAD at handover: `a3fd698` (ultrawide resolution/diagnostics), preceded by `ebd7c27` and `6df0359`
  (graphics boot management/recovery) — recheck before editing, HEAD moved externally.
- Tests are authorized for this task (an old "only test when requested" sentence in the tracker is
  stale). No applicable AGENTS.md was found; do not spawn subagents without explicit authorization.
- Never use the real save/profile for testing; scratch lives in `out/_swap/settings-audit/`. Game data
  is `game/`, the separate Ultimate payload `game/ultimate/`. Both test processes this session
  launched (22600, 20760) were stopped — check processes before a new run and stop only your own.
- Do not edit generated recompiled sources; production hooks belong in handwritten sources/config.

### What changed in this audit

This audit's per-area changes (early boot / resolution / recovery, persistence and service, UI and
provider integration) are the implementation state Part 1 records; the file-level detail is in the
source documents consolidated as [architecture.md](architecture.md) and [menu.md](menu.md). The
still-true points Part 1 does not carry: preferences load in `OnConfigurePaths` and plugin flags in
`OnPreSetup`; `FinishInit()` moved before menu authoring and now scans rows without a saved preference,
decoding compound target/resolution/filtering/AA cvars as groups (**needs runtime checking**); Auto is
a zero-size `Value` on disk as `resolution = 'auto'`, resolved to the largest supported size that fits
the display with an incompatible manual choice converted and persisted under automatic provenance;
first-run policy version 3 (no benchmark); `.graphics-boot.toml` carries last-good graphics plus a
lifetime lock. **Runtime-unverified**: the nonblack `CaptureGuestOutput()` success poll
(blocking/threading), `--settings_safe_graphics=true`, the partial-save/replay/revision service test,
and the store edit rejecting nonpositive/overflowing resolution arrays. **Needs an audit**:
interruption/concurrent-writer recovery, provider same-key conflict checking, and whether the
production bridge consults `Service::RequestRestart()`.

### Verification state — exact limitations

#### Build and host tests

Build directory `out/build/win-amd64-release`:

```powershell
cmake --build out/build/win-amd64-release --config Release -j 4
ctest --test-dir out/build/win-amd64-release -C Release --output-on-failure
```

The game and Controller Settings built successfully, but **the source is newer than that build** (last service
test, store validation, live-apply changes). One intervening link failed only because the test game
still held the executable open — not a compile error. The toolchain warns that installed Clang
19.1.7/CMake 4.4.0 differ from frozen 23.1.1/4.4.3; the build allows this and **no strict-toolchain
pass is claimed**.

Latest full CTest at handover: **33/35** — `settings_screen` (`RequestPaths()` omitted the newly
emitted `rbbz-settings/cancel/-/-`) and `controller_settings_general` (the deeply nested empty-root fixture's
mixed separators, `bare/a/b/...` vs backslashes); both were fixed on 2026-10-12 (Part 2). The earlier
`path_policy` failure was sandbox temporary-path containment, not a product bug; migration now expects
legacy explicit 720p to be imported because new profiles default to Auto. Logs:
`out/_swap/settings-audit/{build,tests}.log` (overwritten by later runs).

#### Runtime checks did NOT pass yet

Native settings read/application **did** happen early (the marker recorded `resolution = 'auto'`
resolving to **2560x1080** before graphics setup on this display), which proves the selection path ran
but **not** correct visible rendering. The latest screenshot, `runtime-ui/menu.png`, is a **black game
window** and the log was still around Ultimate overlay initialization, so investigate whether startup
is stalled, slow or failing — do not assume a successful launch or blame the UI edits without
isolating it. Compare base vs Ultimate, Auto vs explicit 720p and the last working build; then check
provider attachment/journal locking and the new presentation probe; capture debugger stacks/logs if
necessary. (The black window was later explained by the two-part archive — Part 2.)

**Order to continue in:** resume from the working tree rather than a clean checkout, settle the runtime
checks above first (base vs Ultimate, Auto vs explicit 720p, the last working build), then the
provider-attachment/journal-locking review and the presentation probe. The terminated test processes
left their graphics markers pending, so use a fresh scratch profile or account for the recovery.

Windows automation details: a sandbox-launched game lived on a different desktop (escalated automation
could not see its SDL window; sandbox automation could not foreground it), so drive it on the
interactive desktop with scratch profile/user/cache/log paths and `--mnk_mode=true
--fullscreen=false`; Windows mistook an IME overlay for `Process.MainWindowHandle`, so
`drive_ui.ps1`/`capture_window.ps1` enumerate the process's `SDL_app` window, and `drive_ui.ps1` maps
A to Space, B to Backspace and navigation to `lstick_*` (it picks the first rb_blitz process — run only
one test game).

## Sources

| Part | Original document | Date | Status |
| --- | --- | --- | --- |
| 1 | `docs/plans/in-game-settings-remaining-work.md` | 2026-10-13 | Live tracker; the eleven punch-list items and the 49-item ledger are kept intact |
| 2 | `docs/plans/in-game-settings-status-2026-10-12.md` | 2026-10-12 | History; superseded where Part 1 says otherwise |
| 3 | `docs/plans/in-game-settings-handover-2026-10-12.md` | 2026-10-12 | History; superseded by Part 1 where they disagree |

Part 1's numbered ledger ("What is left, by package", items 1–49) is the open-item list from the
preceding revision of `docs/settings/status.md (part 1, current status)` (2026-10-12), whose numbers are
cited by other documents (item 13 is the in-song entry); the current revision of that path carries the
rest of Part 1.
