# In-game settings — the surfaces (menu, rows, Controller Settings, installer)

Four per-package records of what a player sees and presses: the merged Help & Options screen (P5b), the
menu-compatibility decisions behind it (P10), the Controller Settings Controller Settings surface and the Guide
prohibition (P8), and the two installer folders the game reads (P7). Each part keeps its source's own
section numbers, demoted one level, so an old reference to `docs/settings/menu.md (part 1, the merged menu)`
§3.2 reads as Part 1 §3.2 here. Measurements, guest addresses, evidence paths, defects and open items are
preserved; repeated baseline/build/test prose and duplicated evidence lists were condensed. The engineering
records are [architecture.md](architecture.md), [plan.md](plan.md), [contracts.md](contracts.md) and
[status.md](status.md).

## Sources

| Part | Original document | Package | Status |
| --- | --- | --- | --- |
| 1 | `docs/plans/in-game-settings-merged-menu.md` | P5b | Executed for the main-menu entry; the in-song entry was fixed later by deferring the screen set until the pause panels load (2026-10-12 correction) |
| 2 | `docs/plans/in-game-settings-menu-compat.md` | P10 | Executed (2026-10-11) |
| 3 | `docs/plans/in-game-settings-controller-settings.md` | P8 | Executed (host tests green, runtime evidence captured) |
| 4 | `docs/plans/in-game-settings-installer.md` | P7 | Complete as a host-tested and silently-run lane |

---

## Part 1 — The merged Help & Options surface (was `docs/plans/in-game-settings-merged-menu.md`, P5b)

> **Current-state correction (2026-10-12):** the in-song construction-order issue below was fixed by
> deferring this screen set until the pause panels are loaded; the merged root and its children now open
> from the in-song pause menu, Back returns to pause, and Back from pause resumes play. See
> [status.md](status.md).
>
> **Current-state correction (2026-10-18, P7):** root index 1 (`Controls`) is no longer retail's
> controller page. It opens the in-game Controls menu (§3.3 below), and retail's page moved under
> Gameplay Mapping → Controller → Controller Presets (renamed from `Default Presets` by P4,
> 2026-10-20); the pause record's `pause_controls` case is re-pointed at the pause twin of that menu.
>
> **Current-state correction (2026-10-19, the owner's surface overhaul):** the root is now
> `How to Play, Calibration, General, Controls, Audio/Video, Gameplay`; **Interface is no longer a root
> row** and lives under General with `Main Menu`, `Offline Prompts` and `Help & Options Labels`. HUD
> moved under Gameplay. `4:3 Letterboxing` is removed (it fights the non-16:9 HUD repositioning), and
> the Main Menu page is a set of per-entry `Visible`/`Hidden` rows with no master. The scene's
> `header.lbl` is a **UILabel**, so every page of ours retitles itself on entry. The retail Audio page
> is retitled `AUDIO` and its list is moved onto the merged list's position with
> `get_local_pos`/`set_local_pos` by an appended `av_options_panel` enter statement; boot-verified
> 2026-10-19 (`out/audio-fix/probe3/`), where the first row occupies the parent page's own band
> (y=368..427 at 993 client height) and Overscan stays On. The earlier `local_xfm` spelling did not
> move the list, and the owner's capture from it showed Overscan off on entry.
> Owner-driven manual captures on 2026-10-19 confirmed the titles and the tree.
>
> **Current-state correction (2026-10-23, owner request):** the Interface page's offline row reads
> **`Skip Offline Prompts: On | Off`**, renamed from `Offline Prompts: Hidden|Visible` because the
> visibility spelling made the player translate a negation before flipping it. The stored setting is
> unchanged (`interface.hide_offline_prompts`, true = the prompts are skipped, default true), so the
> change is presentation only; the page's row order is `Main Menu`, `Music Library & Store`,
> `Skip Offline Prompts`, `Help & Options Labels`.

**Downloadable Content (2026-10-22, P9-3):** General ▸ Downloadable Content has five
rows in order: **RhythmVerse**, **Cache DLC Library**, **Refresh Songs**, **DLC Scan
Threads**, **DLC Menu Music**. (The third row was **Refresh Music Library** until the owner renamed
it on 2026-10-22, the same request that made it default On.) RhythmVerse opens a page containing the
**RhythmVerse: On | Off** master and **Search Song Filter**. The filter title is
**SEARCH SONG FILTER** and its five instrument switches remain available while the
master is Off. Back from the filter returns to RhythmVerse; Back from RhythmVerse
returns to Downloadable Content. The main-menu and pause contexts share this tree.

**Current Music Library & Store (2026-10-23):** General ▸ Interface's second
row, immediately after Main Menu, opens **Show Messages** and **Show Recommendations**, each with On/Off
states, requiring a restart. **Show Messages defaults On; Show Recommendations
defaults Off** (owner's final defaults, 2026-10-23). Either can be On while the other is Off.
The former combined switch's value is inherited by either new key while that
key is absent; explicit new values are independent. Both rows and their full
values fit the line budget, and the page has two rows in both contexts.

The navigation ring is Your Songs → RhythmVerse → Messages → Recommended;
LT reverses it. Disabled tabs are hidden and skipped, including RhythmVerse
while its master is Off. The Messages badge follows Show Messages. LT/RT
are absent only when RhythmVerse, Messages and Recommendations are all Off.
The owner's split-control tests exposed reversed swipes when skipping tabs and
holes where hidden labels occupied fixed positions (2026-10-23). Header triggers
now pass their direction explicitly, and the guest's header animation keys pack
enabled labels consecutively, leaving unused space at the right. All eight states
are host-verified; the owner manually confirmed the corrected swipes and compact
layout work (RhythmVerse plan P9-4, animation and layout correction).
An intermittent Search overlap was subsequently reported with Recommendations
Off. The shared background now also reapplies disabled-caption visibility at
entry and poll, since the browser's entry skips the front menu's header setup.
This follow-up passes the eight-state host check; the owner's visual retest is
pending (RhythmVerse plan P9-4, Search overlap record).

**Earlier combined implementation (2026-10-22, superseded by the split above):** General ▸ Interface
has four rows: **Main Menu**, **Offline Prompts**, **Help & Options Labels**, then
**Music Library & Store**. The new page has **Show Headers: On | Off** (default On),
with a caption naming the MESSAGES and RECOMMENDED headers in Your Songs and the store.
The original proposed label exceeded the settings skill's 28-character line budget;
`Show Headers: Off` uses 17 characters. The boolean persists as
`interface.show_messages_recommendations`, requires a restart, and Off hides those
two guest-rendered labels and their new-message badge in the shared background panel.
Both settings contexts have the page, with Back returning to Interface.

**2026-10-23 correction:** the owner's manual test found Your Songs could not
navigate the visible headers and Off crashed. The Off handler now uses retail's
direct scene-label commands. Your Songs bypasses its offline trigger gate;
with headers Off its triggers open the enabled store, whose RT returns to
Your Songs instead of the hidden Inbox. Both switches Off leave only the arrow
pulse. Host checks pass; the owner chose to perform the in-game retest. A second pair
of owner screenshots showed the remaining presentation gate: Your Songs still
sent its online-signin check to `set_offline_mode_headers`, dimming the other
tabs and hiding LT/RT. Its entry now sends FALSE, like the store, whenever either
Show Headers or the RhythmVerse master is On; only both Off select offline styling.
The updated visual behavior is pending the owner's retest. *(Superseded 2026-10-23:
the owner confirmed the corrected swipes and compact layout on the rebuilt binary.)*

Status: **executed for the main-menu entry**, one measured engine constraint left open for the in-song
entry. Evidence is real boots against the pinned retail image (§4); every "measured" line names its log and
capture. Supersedes P5's `advanced_settings` tree (that row, its locale entry and its screen are gone).
P1's bridge, P3's store and P4's capability schema are consumed through their frozen interfaces only.

### 1. What the package owns

| File | What it is |
| --- | --- |
| `src/ui/settings_screen.{h,cpp}` | Authored definition of the merged surface: 14 panels, their screens, all four row kinds, the retail screens it routes to, the requests it sends, the labels, and the two record edits (main-menu row, pause row). SDK-free and pure, so the harness pins every invariant without a boot. |
| `tests/settings_screen_tests.cpp` (ctest `settings_screen`) | 10,460 checks: definition round trip, root row order, list ownership, back targets, request grammar, the two record edits, the label table, the two *measured* generator bugs (a `{do}` that takes an array does nothing; a declaration emitted as a command crashes on entry), and the emit modes (`emit-append`, `emit-ui`, `emit-splash`, `emit-pause`, `emit-locale`). |
| `out/_swap/p5b/make_payload.py` | Generates the patch payload **from the installed records**: our definitions are appended to the installed `ui/options_menu/gen/options_menu.dtb`, the owned scene is the installed `options_menu.milo_xbox` copied under a project-owned name, and the entry rows are re-pointed in the installed `splash.dtb`. No retail bytes are stored in the repository. |

Not here: the real Ultimate provider (P6), the restart flow (P9), hide-list/refresh/pool (P10), first-run
graphics policy wiring (P4/P11) and packaging (P11); those rows exist and are routed, their providers are
the stub bridge.

### 2. Two decisions the boots settled

#### 2.1 The merged root has its own list resource

Retail's `options_panel` and any panel of ours naming `options_menu.milo` share one loaded dir and one
`options.lst` — why P1's prototype had rows and selection overwriting retail's and Back landing on `credits`
(bridge findings §12.3b). Our panel uses **`{file settings_menu.milo}`**, a project-owned scene record whose
bytes are the installed retail scene copied at boot under a name of ours
(`ui/options_menu/gen/settings_menu.milo_xbox`); a different path is a different loaded dir is a different
`options.lst`, so "no row dispatches through retail `options.lst`" is true by construction, and
`TestListOwnership` fails if any declaration names retail's scene again. The copy must sit **beside** the
retail scene, because the scene refers to neighbours by relative path
(`../resource/lists/list_options_main.milo`, `neon_lights.milo`).

#### 2.2 Where a definition of ours may live

Our definitions **ride inside a retail record** (measured in P1, re-confirmed here). Appending them to the
installed `ui/options_menu/gen/options_menu.dtb` works; the same declarations in a record of our own reached
by `[include]` parse but their panel is unusable (guest faults with `write of guest 0x00000200`). The panel
also needs `{<panel> load}` in `ui/gen/ui.dtb`'s init block, the way retail creates `options_panel`, or it
has no focus component and its list never takes the direction keys; `AddPanelLoad` does that idempotently,
immediately before `{set {var first_screen} ...}`.

### 3. The one open item: the in-song entry

Retail's in-song HELP & OPTIONS is **not a screen jump**: `pause_panel`'s `{enter}` fills a *second list in
the same scene* with the five help rows, and the pause row switches focus to it.

```dta
{enter {do
   {this populate_menu}
   {options.lst set_data (pause_tutorial pause_controls calibration audio_video_options credits)}
   {this update_focus {get_focus}}}}

{switch {get_focus selected_sym}
  {pause_options {this update_focus options.lst}}     ; <- Help & Options, retail's own body
  {pause_options_back {this update_focus pause.lst}}
  {calibration ...}{audio_video_options {transition_to av_options_pause_screen}}
  {pause_controls {ui goto_screen controller_config_pause_screen}} ...}
```

Every body that **leaves the pause list** instead faults the guest with `read of guest 0x00000024` on
Confirm:

| Arm | Body for `pause_options` | Result |
| --- | --- | --- |
| A | `{transition_to settings_pause_screen}` — retail's own `audio_video_options` primitive | fault at Confirm |
| B | `{ui goto_screen settings_pause_screen}` — the main menu's primitive | fault at Confirm |
| C | `{do {this update_focus options.lst} {ui goto_screen settings_pause_screen}}` — switch then jump, one frame | fault at Confirm |
| D | as A, with the in-song screen hosting the **retail** `av_options_panel` instead of ours | fault at Confirm |
| E | the same payload with `ui/pause/gen/pause.dtb` **left pristine** | route runs clean; Confirm opens retail's in-song page (How to Play / Controls / Calibration / Audio/Video / Credits) and B returns |

D rules out our panel and scene; E rules out the route, driver and environment. The fault is the edit:
taking focus away from `pause.lst` while the pause state machine still drives it leaves that machine reading
a list it no longer has. **So the shipped payload does not edit the `pause_options` focus switch**
(`make_payload.py` skips it unless `--pause` is passed for an experiment), and `PauseRouteBody()` reproduces
retail's own statement so the edit is a no-op rather than a crash; the in-song page stays retail's own for
now. **P7 supersedes this for one row:** the existing `pause_controls` *jump* case is re-pointed at the
pause twin of the Controls menu (§3.3); the focus switch is still untouched.

#### 3.1 The next experiment, in order

1. **Merge into the inline list.** Replace the five symbols `pause_panel`'s `{enter}` fills `options.lst`
   with (keeping retail's `pause_controls` / `calibration` / `audio_video_options` for rows that already
   have working cases; adding our `settings_general`, `settings_gameplay`, `settings_interface`) and add
   the missing cases to that switch. Needs one new edit primitive beside `RerouteRow`: **add** a case array
   to a switch (today the module can only replace a case body).
2. **Pause-context child screens.** Retail pairs its pages (`av_options_screen` / `av_options_pause_screen`,
   same panel, `{panels game_panel pause_panel background_night_panel <panel>}`, `{back_screen game_screen}`);
   our submenus need the same pair. The generator already builds a screen per panel, so this is a second
   screen per panel with a context-dependent panel set and back target.
3. Only then re-point `pause_options`, if the merge is to keep the retail pause list's row count. Until
   step 1 lands, the row stays retail's.

#### 3.3 P7 reroutes the Controls row without touching the focus switch (2026-10-18)

The rule this section measured stands: **no edit may take focus away from `pause.lst`.** P7 did
not need to break it. Retail's `pause_controls` case was already a plain jump
(`{ui goto_screen controller_config_pause_screen}`), so replacing its *target* with the pause
context's own Controls screen keeps the shape the guest already accepted:

```dta
{pause_controls {ui goto_screen settings_controls_pause_screen}}
```

That screen and its children are the in-song twins `settings_screen.cpp` builds for every page of
ours (`InSongPanels()`: pause panel set, `_pause_screen` names, back targets mapped to the same
context), so the whole nested Controls menu exists in song with its Back chain returning through
Controller Presets → Controller → Gameplay Mapping → Controls → the pause list. The main-menu root's
`controls` row opens the same tree with `settings_controls_screen`.

**What is measured and what is not.** The screen definitions, the twins and the reroute target are
asserted by `rb_blitz_settings_screen_tests` (89,484 checks, 2026-10-18), and the main-menu half was
driven live end to end (`out/gameplay-mapping/in-game/p7-live4/`, 56 steps, 0 failures — plan §16.4).
The **in-song** half has not been driven in a boot; the next in-song experiment is that page, and
it is P8's. If it faults, this note is the record that the jump shape — not the focus switch — is
what the reroute kept.

#### 3.2 What the in-song page can be, measured (P5b-r2)

P5b-r2 implemented §3.1 (list merge, appended cases, in-song twins) and measured it in ten boots: the
dispatch half works, the *page* half is where the fault lives. Every arm used the same staged route
(`out/_swap/p5b/run.ps1 -Route p11a-v|p11a-5|p11a-lab*`) and a shot at every step, so "the press did nothing"
and "the page did not appear" can be told apart.

| Arm | Row pointed at | Live? | Log | On screen |
| --- | --- | --- | --- | --- |
| v2 (probe 4) | retail's `controller_config_pause_screen`, plain jump | yes, 0 faults | `q1`, `q2` | CONTROLLER PRESETS |
| v6 (probe 4) | our `settings_interface_pause_screen` twin, plain jump | **fault `read of guest 0x00000024`** | `s1`, no `s2` | - (its panel never entered: no reads) |
| v3 | our `settings_general_pause_screen` twin, `EnterScreen` | **fault 0x24** | - | - |
| v7 (probe 5) | our `settings_pause_screen` root twin | **fault 0x24** | - | - |
| lab1 (probe 6) | variant A: our **in-song** panel, **menu** panel set | entry fine; **fault `read of guest 0x00000000` on B** | `provider.active` | **HELP & OPTIONS + our five rows, in song** |
| lab2 (probe 6) | variant B: our **menu** panel, **in-song** panel set | **fault 0x24** | - | - |
| lab3 (probe 7) | in-song set **minus `game_panel`** | **fault 0x24** | - | - |
| split1 | the twin, declared in the **pause record** instead | **fault 0x24** | - | - |

As narrowly as the evidence allows:

* **A screen of ours cannot list the in-song panels** (`pause_panel`, `game_fade_out_panel`, and the
  `game_panel` that group shares with `game_screen`). Any such screen faults the guest with
  `read of guest 0x00000024` *while it is constructed* — before its own panel's `{enter}` runs, which is why
  no bridge read from that panel is ever logged, while variant A's panel reads its gate at once. Retail's own
  in-song screens (`controller_config_pause_screen`, `av_options_pause_screen`, `pause_credits_screen`) list
  the same panels and open fine, so the limitation is ours. It is not the scene copy (variant A hosts our
  in-song panel), not the panel's scene, not the declaring record (`split1` rode the twins in
  `ui/pause/gen/pause.dtb` and still faulted), and not the `{do {<twin> set back_screen {ui current_screen}}
  ...}` statement (`v6` is a plain jump). What the matrix says is "the screen lists one of our panels *and*
  an in-song panel"; it does not yet say which of the two, or why.
* **The forward half is reached.** Variant A — our in-song root panel, the *menu* panel set — draws the
  merged root inside a song, entered by pressing retail's own HELP & OPTIONS row, game still behind it
  (`lab1`, `shots-lab1/06-variant-a.png`): the first time the merged page has been on screen during a song.
* **Back is not.** With the in-song panels unhostable, the game screen's panels are torn down when our page
  replaces it, so `B` — retail's in-song pages answer `{ui goto_screen game_screen}` — faults
  (`read of guest 0x00000000`). Keeping the pause state alive across the page is exactly what hosting those
  panels is for.

The shipped payload therefore still leaves `ui/pause/gen/pause.dtb` alone (`make_payload.py` writes it only
under `--pause`), and the in-song HELP & OPTIONS keeps working exactly as retail ships it.

#### 3.3 The next experiment, in order

1. **Narrow the panel-set limitation.** The cheapest split left is one variant with *retail's*
   `controller_config_panel` in the same list as the in-song panels: it decides between "a screen listing one
   of *our* panels cannot host an in-song panel" and something about the transition itself.
   `RBBZ_PAUSE_PROBE=7` is the arm to extend — it already emits per-panel variants, and `dump-def` /
   `dump-record` read any definition back as text (§5).
2. **Then Back.** If the in-song panels can be hosted, retail's own shape is available and Back is
   `game_screen`. If not, the candidates are `{ui pop_screen}` (leave the game screen alive underneath) and
   Cancel on our page calling the resume retail's own `pause_panel` calls (`{game_screen resume}`).
3. Only then re-point `pause_options` in the shipped payload.

### 4. Evidence

Boots: `rb_blitz.exe --game_data_root=<install> --ultimate_mode=2
--ultimate_payload_root=out/_swap/p5b/payload ...`, disposable `--user_data_root` and `--launcher_profile`,
driven with `scripts/drive_ui.ps1` through `out/_swap/p5b/run.ps1 -Route <r>`. Payload: 5 records, ark
238,542 bytes (`ui/options_menu/gen/options_menu.dtb` 132,333 bytes / +29 roots, `ui/gen/ui.dtb` with 14
`{<panel> load}`, `ui/options_menu/gen/settings_menu.milo_xbox` 11,966 bytes, `ui/splash/gen/splash.dtb`
7,150 bytes, `ui/locale/eng/gen/locale_keep.dtb` 84,172 bytes / +117 labels).

| Proof | Route | Evidence | Result |
| --- | --- | --- | --- |
| **2026-10-19 tree and titles** | manual (owner) | owner's 2026-10-19 captures: root, INTERFACE, DLC rows, AUDIO/VIDEO | root is `How to Play / Calibration / General / Controls / Audio/Video / Gameplay`; INTERFACE reads `Main Menu / Offline Prompts: Hidden|Visible / Help & Options Labels: On`; Audio/Video reads `Audio / Video`; the Audio page is retitled `AUDIO`. **Not a harness boot** — see the 2026-10-19 correction above. **Superseded for the offline row (2026-10-23):** it now reads `Skip Offline Prompts: On|Off` — see the 2026-10-23 correction; the capture is kept as the record of what that build drew. |
| **Audio page alignment + overscan** (2026-10-19) | `probe` | `out/audio-fix/probe3/shots/02-av.png` (parent), `03-audio-mouse.png` and `04-audio-key.png` (Audio page) | Both entries draw the Audio page's first row in the parent page's own band (y=368..427 at 993 client height) and read `Overscan: On`; `probe1`/`probe2` measured the one-row pitch (63 px) and the `get_local_pos`/`set_local_pos` pair. |
| Base install shows **five** rows, no dead `Gameplay` (superseded layout) | `baseroot` | `baseroot.log`, `shots-baseroot/00-root.png` | Old root `General / Controls / Calibration / Audio/Video / Interface`, General selected |
| Down and up walk the list and **clamp at both ends** | `baseroot` | `shots-baseroot/01..05-clamp.png`, `06..10-up-clamp.png` | INTERFACE stays selected through the 5th down; GENERAL the same going up |
| B leaves the merged root to the **screen it came from** | `baseroot` | `shots-baseroot/11-back-menu.png` | the main menu (not `credits`) — the P1 defect is gone |
| A child row enters a retail screen and B returns | `nav` | `shots-nav/04-confirm.png`, `05-back.png` | Audio/Video opens the retail VIDEO/AUDIO page; B lands back on the merged root |
| A list **longer than five rows** draws, scrolls, clamps | `basescroll` (Interface -> Hide Items -> Main Menu, 8 rows) | `shots-basescroll/05-main-menu-0.png` (row 0 + 6 visible), `09.png` (scrolled to LEADERBOARD..EXIT GAME, HELP & OPTIONS selected), `12-last.png` (EXIT GAME), `15-back-root.png` | 8 rows navigate with correct selection and focus; 5 is a retail list size, not a cap |
| Ultimate = **six** root rows, own long list navigates | `sixscroll` with `REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE=1` | `sixscroll.log` (`provider gate forced active`), `shots-sixscroll/00-root.png` (+ OCR), `02-gameplay-0.png`, `04.png`, `07-last.png`, `08-back-root.png` | six rows with `Gameplay`; the panel fills (`Precision Mode: OFF`, `Disable Random Track Wiggles`, `Camera Angle: Default`, `Game Speed: 100%`, `Disable Depth of Field: OFF`, `Plots & Skyboxes`, `Gems`) and walks to its last row; B returns to the root |
| In-song entry, **retail's page**, no fault | `pause` | `pause.log` (0 guest faults), `shots-pause/03-pause-options-row.png`, `04-in-song-root.png` (GAME PAUSED + How to Play / Controls / Calibration / Audio/Video / Credits), `05-back-to-pause.png` | reachable and correct while §3 is open |
| The installed retail records are never modified | any | `records-before.sha256` vs a re-hash after the boots | identical (3 records: `default.xex`, `main_xbox.hdr`, `main_xbox_0.ark`) |
| **Interface carries a fourth row**, and its caption appears/disappears live | `interface-labels` (2026-10-14, the P4 reproducer `out/_swap/p4/run.ps1 -Label labels-off`) | `out/_swap/p4/labels-off/shots/02-interface-labels.png`, `03-labels-off.png`, `labels-off.log` | base install draws `Hide Items` + **`Settings Labels: On`** (the two Ultimate rows are gated out); A commits, the log records `hot_apply: settings_help_overlay = false (was true), applied to the running game`, the caption is gone on the next draw and **no** restart prompt appears |

The provider arm uses the bridge's diagnostic gate (`REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE=1`), seeding
`provider.active = true`; that proves the **row visibility and list length** rules of the plan's §3, not that
the real Ultimate adapter works (P6's) — its rows talk to the stub service. **Consequence:** a machine that
*does* have Ultimate installed shows five rows today, because `Gameplay` appears when the **adapter reports
the provider active**, which nothing answers for real until P6 lands; don't file the five-row root as a
defect before then.

#### 4.1 Labels

A capture of the first working build showed the owner-caught defect: our rows drew `GENERAL` / `GAMEPLAY` /
`INTERFACE` in capitals beside retail's `Controls` / `Calibration` / `Audio/Video`, because those strings are
ours and were typed in the main menu's capitals, while the retail rows come from the options *page* locale
strings. They now read `General` / `Gameplay` / `Interface`, and the same correction applied to every other
label of ours (`Downloadable Content`, `Anti-Aliasing`, `Precision Mode`, `Anisotropic Filtering: 16x`,
`Native (2x)`, ...). Two deliberate exceptions stay in capitals: the **acronyms** `HUD` / `FXAA`, and the static fallback of the **Main Menu mirror** (`Hide Items ->
Main Menu`), which types the menu's entries (`PLAY`, `LEADERBOARDS`, `ACHIEVEMENTS`, `HELP & OPTIONS`,
`UPGRADE`, `EXIT GAME`, mirroring incl. `PLAY`/`LEADERBOARDS` where an earlier table had
`START`/`LEADERBOARD`). The mirror normally redraws itself from the live menu, and those rows carry the
locale's own casing (`Leaderboards`, `Refresh Songs`); its restored row reads **`Download content`**
since 2026-10-23 (owner request), and with the refresh entry off the mirror does not list that row at
all, because the menu does not have it. `TestLabels` now fails if any other label of ours
carries no lowercase letter; boolean rows still render their state in capitals (`Cache DLC Library: OFF`).

### 5. Reproduce

```powershell
cmake --build out/build/win-amd64-release --target rb_blitz_settings_screen_tests
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe        # 13,285 checks, exit 0
ctest --test-dir out/build/win-amd64-release -R settings_screen

# Read a definition (or a retail record) back as text: `Compile` encrypts the stream, so compiled files
# are not greppable; these decrypt and print (how the in-song arms were read).
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-def settings_general
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-record out\_swap\p5b\retail_cc.dtb
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-record out\_swap\p5b\retail_ui.dtb init

python out\_swap\p5b\make_payload.py payload              # from the installed records; nothing retail stored
python out\_swap\p5b\make_payload.py payload-r2 --pause   # the in-song experiment, never shipped

pwsh -File out\_swap\p5b\run.ps1 -Label baseroot   -Route baseroot   -Seconds 300
pwsh -File out\_swap\p5b\run.ps1 -Label basescroll -Route basescroll -Seconds 300
$env:REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE='1'
pwsh -File out\_swap\p5b\run.ps1 -Label sixscroll  -Route sixscroll  -Seconds 300
pwsh -File out\_swap\p5b\run.ps1 -Label pause      -Route pause      -Seconds 300
```

Evidence lands in `out/_swap/p5b/` (`<label>.log`, `shots-<label>/`); `out/_swap/` is ignored and its decoded
retail/Ultimate material is research, not a deliverable.

### 6. Handover

* **P6** owns the frozen row identity and destinations in `AllPanels()`: every provider row is marked
  `provider_only`, its `setting` the stable `<category>.<id>` token on top of the contracts' §5.1 ids;
  provider rows currently read and write through the stub bridge.
* **P9** owns the per-row `hot`/`restart` strategy and the restart flow; the `Target` rows (`kChoose`) only
  set `general.target` and leave the screen, because the restart prompt is P9's.
* **The `Target` row's choices are a fact about the install, not about the current boot.**
  "Rock Band Blitz" and "Rock Band Blitz Demo" are always offered; "Rock Band Blitz Ultimate" is
  offered whenever the payload is on disk (`provider.payload_present`, which is
  `InspectInstallation(<game>/ultimate)` succeeding), *including* a boot whose stored target is
  `common`. That indirection is load-bearing: the boot applies the stored target as
  `ultimate_mode` (0 for common/demo, 1 for ultimate), so deriving the inspected root from the mode
  would hide the only control that can select Ultimate as soon as the target was anything else.
  Measured 2026-10-18: a fresh profile lists all three (`out/_swap/p0/target-row-fixed-shots/02-choices.png`),
  choosing Ultimate stores `general.target = 'ultimate'`, and the next flag-free boot reports
  `ultimate: provider compatible` + `provider active` and mounts the overlay.
* **P10** owns the hide list, the 50-song pool and the refresh protection; the Main Menu panel already lists
  every actual row by stable token and keeps HELP & OPTIONS present and inert.
* **P11** owns packaging: the payload generator here is the *shape* required (generate the patch record from
  the installed records at boot), incl. the owned scene copy.
* Nobody should re-point `pause_options` until §3.3 step 3; §3.2 is what the row's own arms measured, and it
  is why the payload generator still leaves the pause record pristine by default.
* The **whole plan's** remaining work (every package, every open item, one numbered list) is
  [status.md](status.md); the in-song entry is item 13 there and the only open item blocking gate G1b.

---

## Part 2 — Menu compatibility: the hide list, the pool, the rename scope (was `docs/plans/in-game-settings-menu-compat.md`, P10)

Status: **P10 executed** (2026-10-11). The hide matrix §3.5 states in words is now a decision function with
tests, the effective menu is read from the record the title actually loads, the pool is fixed at 50 songs,
and the retired pool size says out loud that it is ignored. Five boot arms are in `out/_swap/p10/`. Parent
plan: [plan.md](plan.md) §3.5, §3.6, §10.1 (P10). Baseline: `b940bd1` plus the P9 working tree. Nothing
committed.

### 1. What this package is

`Hide Main Menu` used to be a CSV of hardcoded names behind a branch in `src/hooks/menu_filter.cpp`, and the
pool size used to be a cvar — both places where the product's rule lived in code only a boot could check.
P10 turns each into a decision over plain data and reads the menu it is talking about from the file the
title loads.

| Piece | File | What it owns |
| --- | --- | --- |
| The decisions | `src/settings/menu_compat.{h,cpp}` (new, SDK-free) | The hide matrix, `min(50, library)` pool, the retired count's diagnostic, the rename's scope |
| The effective menu | `src/ui/menu_options.{h,cpp}` (`MenuInventory`) | The option array of the record the title will load: every row, in draw order, with its conditionality |
| The hook that applies it | `src/hooks/menu_filter.cpp` | Reads the decision, patches the splash record, logs what it decided and why |
| The pool | `src/hooks/shell_music.cpp` | Fixed cap; the retired value is reported, never obeyed |

### 2. The hide matrix, decided rather than branched

```text
DecideHiddenRows(HideRequest{master, selection_explicit, selected, defaults, protected_row,
                             refresh_row_owns_refresh}) -> HideDecision{hidden, refused,
                             refresh_row_left_visible, alternate_refresh_needed, note}
```

The four rules, each of which was a bug or a silent behaviour before:

| Input | Result | Why it matters |
| --- | --- | --- |
| master off | nothing hidden; selection untouched | §3.5 "Off disables filtering while preserving checked choices" |
| master on, no selection (absent) | the compiled default trio | today's behaviour, kept |
| master on, **explicitly empty** selection | **nothing hidden** | §3.5 "On with an empty selected list hides nothing" — the old code fell back to the defaults, so clearing the field re-hid the rows the player had just removed |
| master on, custom selection | exactly those rows, de-duplicated, in the selection's order | an entry that is not a menu row simply does not match, which the inventory (§3) makes visible to the player |
| any selection naming `splash_options` | refused, **and reported** by name | the merged product's only settings entry must never become unreachable; a silent refusal is how a user loses their settings screen |
| the default trio with R7 active | `splash_dlc` **kept**, with the reason | §3.5 "Default offline rows must not hide Refresh Song Library when `splash_dlc` has that job" |
| an explicit selection naming `splash_dlc` with R7 active | hidden, and `alternate_refresh_needed` set | the user's explicit choice wins; the caller is told the refresh has no row left |

`selection_explicit` is the whole point: on the cvar path it is `configured != kDefaultRows` — an absent
setting is the compiled trio and *anything* written, including nothing, is the user's. The native store
expresses the same distinction properly (`[interface] hidden_main_menu` is a list whose compiled default is
`kNone`, "never chosen"; an empty list is a real empty list), which P11 wires.

### 3. The menu that is actually there

`menu_options::MenuInventory(file, available)` walks the option array of the splash record it is handed and
reports every row in draw order with `conditional` set when the row sits inside an `#ifdef`/`#ifndef` block.
Two deliberate limits, both in the header: it does not resolve the locale (a row's *label* lives in
`ui/locale/*/gen/locale_keep.dtb`, a different record — the caller pairs token and label, because the caller
has both), and it does not drop conditional rows, because "this row only exists with macro X" is exactly the
availability the list has to show. The inventory feeds the row's choices (`schema::SetMenuRows`), so Hide
Main Menu lists the menu's real rows rather than a hardcoded copy — including any row a mod or our payload
adds. The test in `tests/menu_options_tests.cpp` reads a synthetic record and asserts the row order and
which rows are conditional.

### 4. The pool is fixed at 50

`menu_compat::PoolCap(usable) = min(50, usable)`, and `shell_music` reads no size setting any more:
`enhancements_menu_dlc_song_count` is consulted only to report that it is ignored (`LegacyPoolCountNote`),
and the boot says both facts before deciding whether to mount anything at all. Old profile values therefore
cannot re-enable configurability, and a hand-edited config is not silently obeyed either.

### 5. The rename, scoped

The mod-row rename (`R10`) targets the mod's own locale key, `mod_settings`, and nothing else:
`menu_compat::RenameCoversRow(key)` is the guard, tested against the protected entry, our panel name and the
empty key. It is "the retired transform must not overwrite the merged root's label" in mechanical form;
retiring the *user-facing* toggle (Controller Settings row, and the mod row's routing) belongs to the cutover, and
is listed in §7.

### 6. Evidence

Harness: `out\_swap\p10\run.ps1 -Label <name> [-HideRowValue <csv>] [-DlcCache] [-SongCount <n>]`. Each arm
boots `rb_blitz.exe` against a disposable `--user_data_root`, waits for the decision lines and stops the
game. The hide selection travels in the **environment** (`REX_ENHANCEMENTS_HIDDEN_MENU_OPTIONS`), because a
command line cannot express "present and empty" — an empty `--flag=` makes the SDK's parser take the *next*
argument as the value (measured, and how the first attempt ended up asking to hide
`--enhancements_menu_dlc_song_count=64`).

| Arm | Log | Observation |
| --- | --- | --- |
| `empty` (master on, nothing selected) | `out/_swap/p10/empty/empty.log` | `menu_filter: R5 on: hiding 3 row(s): splash_leaderboard, splash_achievements, splash_dlc`, byte-neutral edit applied (71 bytes into the array's `#ifdef` name) — the *absent* case, i.e. the compiled default |
| `custom` (master on, two rows) | `custom/custom.log` | `hiding 2 row(s): splash_start, splash_leaderboard`, edit applied (46 bytes) |
| `protected` (master on, `splash_options` + one row) | `protected/protected.log` | `hiding 1 row(s): splash_start; refused to hide the protected entry (splash_options)` — the edit hides exactly one row |
| `refresh` (master on + default trio + R7) | `refresh/refresh.log` | `hiding 2 row(s): splash_leaderboard, splash_achievements; splash_dlc stays because it refreshes the DLC library`, plus `the downloadable-content row refreshes the DLC cache and is drawn as "Refresh Song Library" (R7)` and `... now refreshes the DLC cache (323 bytes of its action paid for the call)` — **the refresh path is unbroken** |
| `poollgcy` (`--enhancements_menu_dlc_song_count=64`) | `poollgcy/poollgcy.log` | `shell_music: the menu's DLC pool holds up to 50 song(s) (fixed, plan 3.5)` and `the retired menu-pool size (64) is ignored; the pool is fixed at 50 songs` |

**Not measured in a boot: the explicitly-empty selection.** Both remaining spellings are blocked outside the
product: an empty environment variable reads as *unset*, and a trailing
`--enhancements_hidden_menu_options=` produced a boot that never saw `--game_data_root` (the harness's own
argument passing). The rule is unit-tested (`empty selection hides nothing`) and has a product spelling the
store already models; §9.3 of the plan's acceptance matrix can exercise it once P11 wires the store.

Tests: **34/34** `ctest` green. New target `menu_compat` (17 checks in 11 cases: the whole hide matrix, the
pool cap and its retired value, the rename's scope, and the hide-list seam where an empty selection differs
from no selection), plus the inventory case appended to `menu_options`.

### 7. What P10 did not do

* **Controller Settings-side row removal.** The retired rename toggle and the pool-size row are still rows in
  `controller-settings/config/settings.toml`; they are now inert in the game (P10 reads neither), but removing them from
  Controller Settings surface is the remaining P8 work and the P11 cutover.
* **The alternate in-menu refresh action.** When a user explicitly hides `splash_dlc` while it carries the
  refresh, the refresh has no main-menu row; the boot warns, and the alternate action (a row under
  Downloadable Content that calls the same sentinel) is §3.5's own follow-up. No second refresh
  implementation was added, which the plan forbids.
* **Reviewing the whole menu model against the merged tree.** The plan's P10 prompt also mentions "both menu
  variants" and the protected root in the *descriptor* list; the seam exists and is tested, but the boot
  wiring that fills it from the live menu (and therefore a menu screenshot with the Hide Items list in it)
  belongs to P11.
* **Package-handle lifetime for a 50-song pool.** The cap grew from 3 to 50, so a single-song-package library
  now mounts up to 50 containers at once. `shell_music`'s walk already bounds the mounts by the same number
  and skips what it cannot mount, but a real 50-package library has not been booted since the change — the
  acceptance item, called out rather than assumed.

### 8. Reproduce

```powershell
cmake --preset win-amd64-release
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure            # 34/34

pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label custom    -HideRowValue 'splash_start,splash_leaderboard'
pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label protected -HideRowValue 'splash_options,splash_start'
pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label refresh   -DlcCache
pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label poollgcy  -SongCount 64
```

---

## Part 3 — Controller Settings + the Guide prohibition (was `docs/plans/in-game-settings-controller-settings.md`, P8)

> **Superseded for the live product (2026-10-18), and the application itself removed (2026-10-19).**
> The in-game Help & Options → Controls menu
> replaces this application's controller surface (Part 1 §3.3 and
> [the gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md) §16), and
> **P8b removed `rb_blitz_controller_settings.exe`**, its target, tests, scripts, payload entry and
> installer shortcuts on 2026-10-19 (same plan §17-§19). The Guide prohibition and the vocabulary
> notes below remain true of the game's own input path; the parts that describe the app, its window,
> `--controller-only`, `--dump-layout`, its shortcuts and its update plumbing are history as of that
> removal.

Status: **executed** (host tests green, runtime evidence captured). The **full surface is still the default
surface**: the reduced one exists behind `--controller-only`, and the cutover is P11's per `contracts.md`
§9.3. No installer or native-menu file was touched (P7 and P5b own those and ran in parallel). Contract
version: P0's frozen contracts as recorded in `contracts.md` (§2.3 Guide, §8 controller allowlist, §9.1/§9.3
ownership and the development gate); no contract shape changed, and this pass adds one default-off cvar and
one `LaunchOptions` parameter (both named below). Read with `plan.md` §3.6, §9.3, §11 P8's prompt;
`contracts.md` §2.2/§2.3.

### 1. What P8 was asked for, and what this pass did

Plan §11 P8 asked for a controller-only surface and the Guide removal; both landed. Controller functions
kept except Guide, bespoke widgets included (`controller-settings/src/controller_tab.{h,cpp}`,
`src/profile/remap.{h,cpp}`, `controller-settings/src/shell.{h,cpp}` — §3.2, §4.3); one controller-focused screen
behind `--controller-only` (§3.1, §4.2); keyboard/pad navigation, capture, mouse and trigger bindings,
presets/resets, nav rebind, safe mode and help untouched and re-walked (§4.2, §5); non-controller tabs/rows
no longer drawn, deletion itself P11's (gate §9.3 — §3.1, §7); filenames kept (`rb_blitz_controller_settings.exe`,
`launcher.toml`) while window title/heading/help/README were renamed (§3.4); Update/Install Ultimate routed
to the installer's workflow, no Update button or check on the reduced surface (§3.3); Play carries no
native-setting flags, only `LaunchOptions::settings_rows` (`controller-settings/src/game_launch.{h,cpp}` — §3.5, §4.4).
Guide: no source, target, capture or pass-through (`src/profile/remap.cpp`, `controller-settings/config/settings.toml`,
`controller-settings/src/controller_tab.cpp`, `src/input/remap.cpp` — §2); old entries inert with a clear diagnostic
(`remap::IsRefusedTargetName`/`RefusedRowNames`, panel warning, game log — §2.3); Guide cleared from the
final guest state on every input path (`remap::WithoutProhibitedButtons` + an always-installed filter —
§2.2, §4.5); no unrelated trigger or OS Guide behaviour disabled (no SDK patch, no cvar consumer changed,
only our own filter's output — §2.4); Controller Settings writes only `launcher.toml`, leaving
`paths.toml`/`settings.toml` untouched (§6.2); `launcher.toml`'s name/format and its `[remap]` rows preserved
(§6.1, §6.2).

### 2. The Guide prohibition

#### 2.1 The vocabulary (shared by both executables)

* `remap::Target` lost `kGuide`; `kTargetCount = 16`; `kTargetInfo` lost the `guide` row, so
  `ParseTarget("guide")` and `ParseSource("pad:guide")` return nothing. A `[remap]` row that spells it is
  *not a binding*: `Table::FromRows` keeps it verbatim in `unknown_` and `Table::ToRows` writes it back —
  "inert preservation data" (a save neither drops a person's old row nor applies it).
* `remap::kGuideButtonBit` (0x0400 = `X_INPUT_GAMEPAD_GUIDE`) and `remap::WithoutProhibitedButtons(state)`
  are new; `Apply` strips the bit from the copy it starts from, so no binding can put it back and every
  caller of `Apply` is covered.
* `remap::IsRefusedTargetName` / `RefusedRowNames(rows)` name the refusal, so a caller can say *why* a
  preserved row does nothing instead of reporting it as an unknown row.

#### 2.2 The game's filter

`src/input/remap.cpp` used to install nothing when the profile was missing or unreadable; it now installs the
state filter on every path: profile readable → `remap::Apply(context->table, …)` (which strips the bit); no
profile, or an unreadable one → `remap::WithoutProhibitedButtons(pad)` only, so the pad keeps the SDK's own
reporting and Guide still cannot reach the guest. That covers contracts §2.3's four paths (profile present,
absent, malformed, and an SDK cvar — `guide_button` / `keybind_guide` — that raised the bit before the filter
runs). Nothing in the SDK changed: the cvars still exist and still do what they do; our filter is the seam
that takes the bit back out, exactly as for a remap.

#### 2.3 The diagnostics

* **On screen**: the Controller Settings mapping block prints "Guide is not remappable: the 'guide' row in
  your settings file is kept but ignored, and Guide is never passed to the game." whenever
  `session.profile().remap` contains such a row.
* **In the log**: `remap: Guide is not remappable, so the profile's guide row is ignored (and Guide is kept
  out of the pad state the guest sees)`.
* **In the schema**: the `guide_button` row is gone from `controller-settings/config/settings.toml`, replaced by a
  comment recording why it must not come back; the generated table is 27 rows (was 28).
* **In migration**: unchanged from P3 — `guide_button` and `keybind_guide` are already in `IsRetiredKey`, so
  a legacy row becomes inert preservation data, not a new preference.

#### 2.4 What was deliberately not done

The SDK's `guide_button`, `keybind_guide` and their drivers are untouched and no `patches/rexglue-sdk` entry
was added: disabling them would change behaviour for a user who hand-edits `rb_blitz.toml`, and the
requirement is about what the *guest* receives, which the filter owns. The OS's own Guide handling is
likewise untouched — the project never had a global hook for it.

### 3. Controller Settings

#### 3.1 The switch

`--controller-only` (parsed in `controller-settings/main.cpp`, held as `ShellEnvironment::controller_settings_only`),
default off: the full surface stays the default surface until the coordinator approves G4/P11 (§9.3). Three
decisions follow, all in Controller Settings:

1. `Shell::layout_` is `BuildTabLayout(kController)` instead of `BuildLayout()`: one tab, so the strip is not
   drawn (a strip holding one button is a control that does nothing) and the screen's own heading takes its
   place.
2. `Shell::bar_` is `{Close, Save, Play}` instead of `{Update, Close, Save, Play}`. The bottom bar was
   refactored from four fixed indices to an ordered vector of `BarOption`s, because the ring size, help
   lookup, focus trace and activation all have to agree about which controls exist. (Also fixed a latent
   out-of-bounds read: the focus trace indexed a three-element name array with a four-valued index.)
3. `CurrentLaunchCommand` passes `LaunchOptions{settings_rows = false}`, and the target's fallback reads
   `DetectUltimateState` directly rather than the General tab's state, which no tab sets on a surface that
   draws no General tab.

#### 3.2 What the screen is

One heading, the schema's one remaining controller row (*Mouse support*), and the mapping block: 16 controls
× (Assign, Reset) plus *Reset all*. The walk is 19 rows / 37 ring entries. Everything the block does is the
code D16 already had — the three-second multi-source listen (pad, key, mouse), the rising-edge capture,
*Assign* adds rather than replaces, *Reset* returns a control to its default, *Reset all* clears the table —
and the ring is driven by the same `NavSource`s, so keyboard and pad both walk the whole screen.

#### 3.3 Update and Install Ultimate

Neither is on the reduced surface: no Update button, no prompt, and `UpdateChecker::Start("")` so no check
runs at all. The documented routes already exist — the updater the setup executable leaves in
`%LOCALAPPDATA%`, and `rb_blitz_setup_helper.exe install-ultimate` — reached from the installer or by hand
(`../../installer/README.md`, `../guides/distributable.md`; P7 owns their text). The full surface keeps both
until the cutover.

**Superseded 2026-10-15.** The check is gone from *both* surfaces, not just the reduced one: the
`Update` control, the prompt, the manifest URL compiled into the executable and the two modules
behind them (`controller-settings/src/update_check.{h,cpp}`, `src/updater.{h,cpp}`) were removed, and
Controller Settings now makes no network request at all - it links no HTTP client. Installing a
release is the installer's business on every surface (`../../controller-settings/README.md`, section
"Updates: this window has none"). `UpdateChecker::Start("")` above is history: the class it
names no longer exists.

#### 3.4 Naming

`WindowTitle(bool)` now returns "Rock Band Blitz Controller Settings (vX)" for the reduced surface and keeps
Controller Settings' own name for the development surface; the `Fatal` message box follows it, and so does
`--dump-display`'s report. `rb_blitz_controller_settings.rc` carries no
description strings, so there was nothing to rename there — the installer's shortcut names and setup text are
P7's, and this pass did not touch `installer/`.

**Superseded 2026-10-14 by
[`../plans/finished/2026-10-14-controller-settings-rename.md`](../plans/finished/2026-10-14-controller-settings-rename.md).**
This paragraph used to end "the executable is still `rb_blitz_launcher.exe` and the profile still
`launcher.toml`, deliberately (shortcuts and profiles keep working)". The *executable* was renamed with
the rest of the application (`rb_blitz_launcher.exe` → `rb_blitz_controller_settings.exe`, `launcher/`
→ `controller-settings/`, `src/launcher/` → `src/profile/`, one window title for both surfaces). The
*profile* kept its name, and so did the flag and the cvar that name it: those are a file-on-disk and a
command-line contract. Nothing had shipped, so no released shortcut depended on the old executable
name.

#### 3.5 Play

`LaunchOptions{settings_rows}` is the seam: the full surface passes every row it moved off its default and
the game does not own (`--user_data_root`, `--mouse_ui_nav`, …), while Controller Settings passes
`--game_data_root` and `--launcher_profile` and nothing else. What the screen edits still reaches the game,
because `[remap]` and the controller cvars are read from the profile by the runtime itself (contracts §8),
not from a command line; §4.4 has the printed proof.

**Updated by the P11b cutover (2026-10-13)**, which this section predates: the full surface no longer passes
the rows the in-game Help & Options tree owns (`kGameOwnedRows` in `controller-settings/src/game_launch.{h,cpp}`), and
the **target flags are gone from both surfaces** — the profile's `[launch] target` is imported into the
game's own settings on the first boot instead, so the in-game Target row stays editable (Controller
Settings' identity-only command line is unchanged by that). The game-side reading of the profile is narrowed
to the controller rows and the host-only host rows (`settings::ProfileRowsTheGameApplies`); the rest of
the table is preservation data. Cutover status: [status.md](status.md).

### 4. Evidence

All commands run from the repository root; `out/_swap/p8/` is scratch and ignored.

#### 4.1 Host tests

```powershell
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

`100% tests passed, 0 tests failed out of 29` (3.3 s). New and changed checks:

| Test | Added |
| --- | --- |
| `controller_settings_remap` (251 checks) | the 16-target vocabulary and the 0xF3FF bit union; `guide` refused as target and as source; the preserved row, `RefusedRowNames`, `ToRows` round trip; the bit cleared through `Apply` and through `WithoutProhibitedButtons` |
| `controller_settings_launch` (68 checks) | the identity-only command, with the same session proving the full builder still passes its rows |
| `controller_settings_session` (298 checks) | the disk-merge save: a concurrent `[settings]` edit and a new table survive, this window's edits are written, a removal is still made, and the geometry/update writes follow the same rule |
| `settings_service` (89 checks) | `ControllerProfileRows`: the controller rows are kept, the retired Guide pair and the native rows are not |

#### 4.2 The two surfaces, driven

`out/_swap/p8/run.ps1` launches each surface against a scratch profile, foregrounds the window, walks the
ring with Controller Settings' own bound keys, captures a picture per step, and asserts on `--focus-log`. Both
surfaces exit 0 and all ten assertions pass (`out/_swap/p8/controller-focus.txt`,
`out/_swap/p8/full-focus.txt`):

| Surface | Trace |
| --- | --- |
| Controller Settings | `tab=controller entry=0/37 rowindex=0/19 row=mouse_ui_nav` → `… rowindex=18/19 row=bar:close` → `bar:save` → `bar:play` → `mouse_ui_nav` → `rowindex=1/19 row=button-mapping-block` |
| Full Controller Settings | `tab=general entry=0/15 rowindex=0/9 row=launch.target` → `… row=bar:update` → `bar:close` → `bar:save` → `bar:play` |

So the reduced surface really has one tab, three bar controls and no Update, while the full surface still has
four tabs and four bar controls. Pictures: `out/_swap/p8/controller-*.png` (title bar *Rock Band Blitz
Controller Settings (v0.1.0)*, the 16 controls, a bar of Close / Save / Play Game) and `full-*.png`.

#### 4.3 Headless layout

```powershell
rb_blitz_controller_settings.exe --dump-layout=<file>                    # full surface: 4 tabs, no guide row
rb_blitz_controller_settings.exe --controller-only --dump-layout=<file>  # one screen, 1 row, 3 unbuilt groups
```

`out/_swap/p8/layout-full.txt` and `layout-controller.txt`; both report `every row carries a tooltip`, and
neither contains a Guide row.

#### 4.4 The two launch commands, from one profile

`out/_swap/p8/command-full.txt` and `command-controller.txt`, from the same
`out/_swap/p8/cmd-profile/launcher.toml` (`target = common`, `fullscreen = false`, `mouse_ui_nav = true`,
`user_data_root = "D:\Saves Here"`, `a = "pad:b, key:space"`):

```text
full       : … rb_blitz.exe --game_data_root="…\game" --user_data_root="D:\Saves Here"
             --fullscreen=false --mouse_ui_nav=true --ultimate_mode=0 --launcher_profile="…"
controller : … rb_blitz.exe --game_data_root="…\game" --ultimate_mode=0 --launcher_profile="…"
```

#### 4.5 What the runtime does with the state (source-level, not runtime-proven)

The filter change is in the game executable, and the game was running for the whole pass (the owner's
session), holding `out\build\win-amd64-release\rb_blitz.exe` open: `ninja` therefore stops at the link with
`failed to write output 'rb_blitz.exe': permission denied`, and any further `cmake --build` does the same
until the game is closed. Everything else is verified:

* every translation unit compiles — `ninja CMakeFiles/rb_blitz.dir/src/input/remap.cpp.obj
  CMakeFiles/rb_blitz.dir/src/profile/profile_apply.cpp.obj` exits 0;
* the link is sound: the same `clang++` command with a scratch output name
  (`@CMakeFiles\rb_blitz.rsp -o rb_blitz_p8check.exe`) exits 0 and produces a 40,260,096-byte executable;
* Controller Settings, both test executables and the game's other objects are current, and `ctest` is 29/29.

To finish it in place, with the game closed: `cmake --build out\build\win-amd64-release --target rb_blitz`.

The runtime claim still **unverified** is the end-to-end one: a boot with `guide_button = true` (or a
`guide = pad:guide` row) in the profile, and a log line plus a pad read showing the guest never sees bit
0x0400. The unit-level chain is proven (vocabulary refuses it, `Apply` strips it, the no-profile path strips
it, the filter is installed on both paths); the boot is the coordinator's or P12's check.

### 5. Files changed

| File | Change |
| --- | --- |
| `src/profile/remap.{h,cpp}` | Guide out of the vocabulary; `kTargetCount`, `kGuideButtonBit`, `WithoutProhibitedButtons`, `IsRefusedTargetName`, `RefusedRowNames`; `Apply` strips the bit |
| `src/input/remap.{h,cpp}` | filter installed on every path; no-profile arm strips the prohibition bits; the Guide diagnostic |
| `controller-settings/config/settings.toml` | `guide_button` row removed, with the reason written down |
| `controller-settings/src/controller_tab.{h,cpp}` | 16 controls, no Guide capture, the refusal warning, tooltip corrected |
| `controller-settings/src/game_launch.{h,cpp}` | `LaunchOptions`; the settings block is skippable |
| `controller-settings/src/profile_session.{h,cpp}` | `RebaseOntoDisk`, `NoteWritten`, and the three writers using them |
| `controller-settings/src/shell.{h,cpp}` | `ShellEnvironment::controller_settings_only`; one-tab layout and heading; dynamic bottom bar (`BarOptions()`: Close/Save/Play on both surfaces since 2026-10-15); Play options; target probe; "Play" hint; focus-trace bar names |
| `controller-settings/main.cpp` | `--controller-only`; `WindowTitle(mode)`; `Fatal(mode, …)`; dump/print modes describe the surface asked for |
| `src/settings/cvars.{h,cpp}` | `ControllerProfileRows`; `IsControllerCvar`/`ControllerProfileRows` inline (the game's source list does not carry `src/settings/cvars.cpp`) |
| `src/profile/profile_apply.{h,cpp}` | `profile_controller_rows_only` (default off) and `ProfileApplyReport::rows_ignored` |
| `tests/controller_settings_remap_tests.cpp`, `controller_settings_launch_tests.cpp`, `controller_settings_session_tests.cpp`, `settings_service_tests.cpp` | the cases in §4.1 |
| `../../controller-settings/README.md` | new "Controller Settings (P8)" section; Controller tab, grammar and intro text corrected |

No file outside Controller Settings lane was touched except `src/input/remap.*`, `src/settings/cvars.*` and
`src/profile/profile_apply.*`, all assigned here by contracts §9.1. `installer/`, `src/ui/settings_screen.*`
and `src/rb_blitz_app.h` were not modified (P7, P5b and the coordinator ran in parallel).

### 6. What is left for others

#### 6.1 The coordinator

1. **Flip `profile_controller_rows_only` to true — but only with the boot wiring that gives the game its own
   settings file.** Contracts §8 narrows `ApplyProfileSettings` to the controller allowlist "once
   migration is complete"; today the profile is still where `user_data_root` and `dlc_root` come
   from, so narrowing before the native store owns them would lose the DLC library and the save location. The
   switch is the hands-off version of that change: `src/profile/profile_apply.cpp`, default false, with
   `ProfileApplyReport::rows_ignored` saying how many retired rows were left out.
2. **Add `src/settings/cvars.cpp` to `RBBLITZ_SOURCES`** when the rest of the cvars module is wired; the two
   helpers P8 uses are inline in the header precisely so a game-target caller does not depend on that.
3. **The cutover itself** (§9.3, P11): make Controller Settings the default surface, then delete the
   non-controller tabs, the General tab's settings-file block, `install_ultimate.*`, the update prompt and
   `Update`'s plumbing out of `controller-settings/`. It must not take the updater/installer entry points (the
   installer's, documented), the profile's `[remap]`/`[nav]`/`[update]` tables, or
   `--dump-*`/`--print-command`.
4. **One game boot** to close §4.5: a profile carrying `guide = "pad:guide"` (and a boot with
   `--guide_button=true`), then a pad read or log line showing the guest's state never carries 0x0400.

#### 6.2 P12 (acceptance)

* `scripts/acceptance_controller_settings.ps1` still asserts the settings-as-argv contract for the **full** surface,
  which this pass deliberately did not change; the reduced surface is a different contract, so extend the
  harness with a `--controller-only` leg (the walk, the three-button bar, "no Update") and change the argv
  expectations only at the cutover.
* `scripts/capture_controller_settings.ps1` walks four tabs and expects `bar:update` first; a reduced-surface leg
  belongs beside it, and `out/_swap/p8/run.ps1` is the working prototype (foreground, scan-coded keys, trace
  assertions).

#### 6.3 P5b / P6 / P10 (recorded here because they touch the same surface)

* The merge retires the Advanced Settings row and routes Ultimate's `mod_settings` into Help & Options; the
  reduced Controller Settings screen is reached by the installer's shortcut instead of a row in the game's menu, so
  nothing here conflicts.
* The `[remap]` refusal is part of the merged screen's vocabulary too: the in-game Interface/HUD rows never
  had Guide fields, and the panel API (`architecture.md`) should keep it that way.

### 7. Known limitations, stated plainly

* **The reduced surface is not the default** — the gate, not an oversight (§9.3): the full surface, its
  schema and its argv contract stay the product until G4/P11.
* **The non-controller UI is still compiled in.** "Remove the dead wiring" holds for the screen (it draws
  none of it and its ring has no entries for it) but not the binary, because deleting it before the
  replacement passes acceptance would leave the product with no settings surface at all.
* **`src/settings/cvars.cpp` is still not in the game's source list**, so the native cvars module is only
  exercised by its tests; the allowlist filter P8 added lives in the header for that reason.
* **No test binary exercises the Guide bit end to end** (§4.5). `src/input/remap.cpp` needs SDL, an
  `InputSystem` and a guest to be meaningful; the parts that decide are pure and are covered.
* **The window title of the reduced surface is new text** and is not asserted by any harness yet;
  `--dump-display` prints it, so a check is a one-line addition for P12.

---

## Part 4 — The two folders the game reads (installer) (was `docs/plans/in-game-settings-installer.md`, P7)

Status: **P7 complete** as a host-tested and silently-run lane. `installer/src/paths.{h,cpp}`, three new
helper commands, the wizard's install-folder page, the shortcut/finish defaults and the Controller Settings
terminology are all in place; the installer suite is **406 checks green** and the wizard compiles with ISCC.
Nothing outside `installer/` was touched (P5b and P8 ran in parallel in the same tree).

Read order: `plan.md` §9 (what this package owes) → this part (what it built, what was measured, what is
still open) → `../../installer/README.md` (the user-facing half). Evidence labels: **measured** = executed
in this pass; **static** = read from source, not run; **proposal** = not implemented.

> **2026-10-14 - amended by the owner's installer review; the living record is
> `../../installer/README.md` and `plan.md` §9's own note.** Four of the decisions this part
> records changed: the default destination is `C:\Program Files\Rock Band Blitz` (the setup is
> `PrivilegesRequired=admin` now, which is also what narrows the legacy-profile prefill rung to
> `/CURRENTUSER` installs), the two folder boxes default to `Documents\Rock Band Blitz\...` and
> may no longer be left empty, the mod page explains how to add the mod later when the answer is
> "do not install it", and the wizard creates **two** shortcuts, both the game - so
> `startmenucontroller`, `controllericon`, `/LAUNCHERICON` and `/CONTROLLERICON` are gone. The
> tables below still say four tasks and name those parameters; that was true when they were
> written.

### 1. What the package was

Parent plan §9.1/§9.2, on `wpSelectDir` itself: install, DLC and save folders asked for on the same page,
prefilled from explicit parameters > the installation's own `paths.toml` > the legacy Controller Settings profile >
defaults; the two external folders committed to `paths.toml` **only** after a successful install, and
preserved on failure, update and uninstall; validation sharing the game's own path policy and DLC layout
policy; documented unattended path parameters with the omitted-versus-empty rule; every shortcut task
unchecked, explicit task choices honoured, interactive finish default = the game and silent default =
nothing; and the visible Controller Settings text becoming **Controller Settings**.

### 2. What was built

| File | Change |
| --- | --- |
| `installer/src/paths.{h,cpp}` | New. `PathChoice` (present/value), `ReadPathFile`, `ReadLegacyPaths`, `ValidatePaths`, `WritePathFile`. The file format is **not** reimplemented: `settings::LoadPathFile` / `SavePathFile` from the emulator tree are compiled in (§4.4's shared SDK-free storage module). |
| `installer/src/commands.cpp` | `read-paths` (prefill, read-only), `validate-paths` (the check), `write-paths` (the commit); `finalize` gained `--dlc-dir` / `--save-dir`. |
| `installer/src/install.{h,cpp}` | `InstallSummary` records the two answers; the manifest gains a `[paths]` table and the report a **Locations** block. |
| `installer/CMakeLists.txt` | Compiles `src/paths.cpp` plus `src/settings/{model,store,schema,paths}.cpp` and `src/profile/{profile,profile_path}.cpp` from the emulator tree, with two warnings silenced for those files only (clang 19/C++20 sees one unused local and a duplicate `_CRT_SECURE_NO_WARNINGS` the game's build does not). |
| `installer/setup.iss` | The install-folder page grows two rows (DLC, saved games) with their own pickers and a status line; prefill, validation on Next and in `PrepareToInstall`; `write-paths` last; `DisableDirPage=no`, `UsePreviousTasks=no`, all four `[Tasks]` unchecked; `/DLCDIR`, `/SAVEDIR`, `/CONTROLLERICON`; finish default = the game, third choice = Controller Settings; ready memo and report carry the two paths. |
| `installer/tests/installer_tests.cpp` | `TestLocations()` and `TestWizardScript()`: 100 new checks (three of them the script's shape, which has no other harness). |
| `../../installer/README.md` | The page, the shortcut table, the silent parameters, the helper commands, `paths.toml` ownership, and the by-hand checklist the wizard half needs. |

### 3. Measured behaviour

#### 3.1 The file (the contract with the game)

`write-paths --dest <install> --dlc-dir 'D:\Rock Band DLC' --save-dir 'D:\Rock Band Saves'` writes exactly:

```toml
schema_version = 1

[paths]
dlc_root = 'D:\Rock Band DLC'
user_data_root = 'D:\Rock Band Saves'
```

* Literal strings, so a backslash needs no escaping; read back by `settings::LoadPathFile`.
* A value the caller did not answer **keeps what the file had**; an answered-but-empty value **drops the
  key** (the game's own fallback then applies); a missing file plus no answer writes nothing at all.
* Unknown keys, comments and hand edits survive (the game's own line editor does the patching).
* Identical answers produce **no write**: proved with a read-only file — the unchanged write succeeds, a
  changed one fails and the old bytes stay.
* A `paths.toml` this build cannot read is **never overwritten** (`write-paths` reports "`<file>` was left
  alone: …").

#### 3.2 Validation

| Answer | Result |
| --- | --- |
| `--save-dir <install>\game`, `<install>\GAME\saves`, `<install>\game\sub\..\keep`, `<install>\game\saves\` | refused, `save_inside_game=1`, nothing created |
| `--save-dir <install>\saves` (missing) | accepted, created, write-probed, probe removed |
| `--save-dir <a file>` / `--save-dir <unreachable drive>` | refused with the reason |
| `--dlc-dir <title id>/<content type>/<package>` | `dlc_layout=structured`, package count, no package opened |
| `--dlc-dir <nested folder of files>` | `dlc_layout=flat`, file count, level-1 non-content names counted as rejected |
| `--dlc-dir <empty>` / `<missing>` | accepted — `empty` / `new`, with a note, because the game starts with an empty library |
| no `--dlc-dir` | `dlc_default=1`, `<game data>\dlc`, **no note** (the page does not warn about its own default) |
| `--no-touch` | the same report with nothing created: `save_created=0`, `save_writable=0`, no folder made, no probe written. The check run while the page is merely shown uses it, so a path on its way to being corrected leaves nothing behind; Next (and `PrepareToInstall`) run the real one. |

The containment rule is `fs::IsSameOrInside` from `src/fs/path_policy.h` — the game's own rule, including its
weakly-canonical comparison — and the layout rule is `src/fs/dlc_layout.h`'s `HasStructuredLayout` /
`IsContentRootName`. A junction pointing into the game tree is therefore caught by the same canonicalisation
`tests/path_policy_tests.cpp` covers on the game side.

#### 3.3 The legacy profile as a prefill source

`ReadLegacyPaths` resolves the profile exactly the way the game does (`ResolveProfilePath`: explicit override
→ settings-folder pointer → portable marker beside the executable → `%APPDATA%\rb_blitz`), then reads
`[settings] user_data_root`/`dlc_root` and falls back to `[launch] user_data_dir`/`dlc_dir`. An empty
app-data folder (what an all-users install passes) reads nothing, and a malformed profile is reported, not
guessed at. **Measured on this machine:** a silent install with no folder parameters at all wrote
`dlc_root = 'F:\Games\Rock Band DLCs\360 (CON)\Singles'` — the developer's own `launcher.toml`, found through
that chain. A machine with no legacy profile writes no file in that case.

#### 3.4 The real installer, silently (measured)

`installer/out/dist/RockBandBlitzSetup-0.1.0.exe`, built by ISCC from the modified script (27 s), run against
a scratch installation in `out/_swap/p7/` (archived evidence in `out/_swap/p7/evidence/`: the surviving
`paths.toml`, Inno's setup log and the encoding probe — the manifest and report were removed by the uninstall
that followed, itself the last row below):

| Run | Command | Result |
| --- | --- | --- |
| fresh | `/VERYSILENT /DIR=… /GAMEFOLDER=<repo>\game /DLCDIR="…\DLC — músicas" /SAVEDIR="…\Saves de Gabriel"` | exit 0; 353.7 MB imported and fingerprinted; `paths.toml` written **last** (after the manifest/report), both values; the save folder created; the report's **Locations** block and the manifest's `[paths]` table carry both; **no shortcut created** |
| reinstall, no parameters | `/GAMEMETHOD=installed` | exit 0; the legacy profile was the only source, and was stored |
| reinstall, both answers | `/DLCDIR="…\DLC — músicas" /SAVEDIR="…\Saves do Gabriel 2"` | exit 0; both keys written |
| reinstall, DLC answered empty | `/DLCDIR=` | exit 0; `dlc_root` **dropped**, `user_data_root` kept |
| save folder in the game tree | `/SAVEDIR=<install>\game\saves` | **exit 7**, `paths.toml` byte-identical, no folder created inside the game data; the log carries the sentence the user would have seen |
| uninstall | `unins000.exe /VERYSILENT` | exit 0; `paths.toml` **survives**, the game data is kept, the payload and the registry entry are gone |

Non-ASCII was checked end to end: a `—` (U+2014) and `ú` (U+00FA) in a `/DLCDIR` value arrive in Inno's
command line, through `{param:}`, through the helper's UTF-8 argv and into `paths.toml` unchanged (codepoints
in `out/_swap/p7/evidence/encoding-probe.txt`). The matrix ran against a setup executable built from the
script as it stands, except for the page-time `--no-touch` change made after it: a silent run never shows the
folder page, so `CurPageChanged` (the inspecting check's only caller) is not reached, and `PrepareToInstall`
still runs the full check that produced the exit-7 evidence. Both builds of the script compile with ISCC
(`setup.iss`, and `/DUpdaterMode=1` for the other half).

### 4. Decisions worth keeping

* **`{param:NAME|@absent@}`** tells "not given" from "given empty". Omitted keeps the file; empty means the
  game's own folder. Without it, a silent install could not distinguish "leave it alone" from "reset it".
* **Nothing is answered ⇒ nothing is written.** `PathsAnswered` is false for the updater (no folder page) and
  for a silent install asked nothing *and* with nothing to prefill. A silent install that *did* prefill from
  the legacy profile writes what the folder page would have shown.
* **The wizard reads its answers from globals, not the controls.** Every touch of the two rows is behind
  `gLocationsUi`, so a run whose page never exists (silent, or any future page-less mode) can neither crash
  on a nil control nor depend on the page being built; the controls are only the interactive front end for
  `gDlcValue` / `gSaveValue`.
* **The DLC folder is never scanned beyond a stat walk.** The per-package header read in
  `src/fs/dlc_library.h` costs ~17 ms each; counting entries at the first two levels keeps the page instant on
  a library of thousands, which is why the page can afford to re-check on every Next.
* **Showing a page changes nothing.** The check that runs when the folder page appears takes `--no-touch`: it
  reports and refuses exactly as the real one does, but creates no folder and writes no probe; creating the
  save folder is the job of the check that runs when the user moves on.
* **The free-space line moves, and is dropped when it does not fit.** It is Inno's own sentence about the
  folder above, not about the two rows this release adds; the page's `[Messages]` replacement already says
  the game data is not counted there.
* **`DisableDirPage=no`.** Inno's default hides the folder page when an install is registered — precisely
  when the two external folders most need to be visible. Inno still prefills the folder itself
  (`UsePreviousAppDir`).

### 5. Open items and handoffs

1. **The wizard half still needs eyes.** The two rows' geometry is 100%-scaled arithmetic via
   `ScaleX`/`ScaleY` and was not looked at on a real DPI setting — open the page at 100%, 200% and 300% once
   (`../../installer/README.md`, "The DLC library and the saved games" lists the other three by-hand checks).
   A page that fits at 100% and overflows at 300% is a one-line fix in `LayoutLocationFields`, not a redesign.
2. **The updater leg was not run.** It needs the release manifest over the network. Static: the updater never
   calls `write-paths` (`PathsAnswered` is false when `gUpdater`), so it cannot replace the file — but "it
   preserves an existing `paths.toml` across a real update" is unproven here.
3. **`settings::ResolvePathValue` reads a stored value as a *narrow* string.** `fs::path(value)` on Windows
   decodes that as the ANSI code page, while the helper writes and reads `paths.toml` as UTF-8, so a
   non-ASCII save folder resolves correctly in the installer and *differently* in the game. One-line fix in
   P3's file (`fs::path(Widen(value))`, the conversion `installer/src/util.cpp` uses), and it belongs to the
   coordinator: P7 must not fork the game's own reader, and the alternative — writing ANSI — would be wrong
   everywhere else the helper writes a file.
4. **P11/P12:** the install-folder page is now a three-question page; `acceptance_controller_settings.ps1`'s assumption
   that settings travel as argv is unaffected by P7 (no setting is emitted on a command line), but the
   installer's output directory assumptions in the acceptance scripts should be re-checked when the merged
   Help & Options screen lands.
