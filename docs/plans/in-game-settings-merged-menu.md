# P5b — the merged Help & Options surface (handoff)

Status: **executed for the main-menu entry**, with one measured engine constraint left open for the
in-song entry (below). Evidence is a set of real boots against the pinned retail image, listed in
§4. Nothing in this document is inferred: every "measured" line names the log and the capture that
shows it.

Supersedes P5's `advanced_settings` tree (that row, its locale entry and its screen are gone). P1's
bridge, P3's store and P4's capability schema are consumed through their frozen interfaces only.

## 1. What the package owns

| File | What it is |
| --- | --- |
| `src/ui/settings_screen.{h,cpp}` | The whole authored definition of the merged surface: 14 panels, their screens, all four row kinds, the retail screens it routes to, the requests it sends, the labels it needs, and the two record edits (main-menu row, pause row). SDK-free and pure, so the harness can pin every invariant without a boot. |
| `tests/settings_screen_tests.cpp` (ctest `settings_screen`) | 10,460 checks: definition round trip, the root's row order, list ownership, back targets, request grammar, the two record edits, the label table, and the two *measured* generator bugs (a `{do}` that takes an array does nothing; a declaration emitted as a command crashes on entry). Also the emit modes the payload is generated with (`emit-append`, `emit-ui`, `emit-splash`, `emit-pause`, `emit-locale`). |
| `out/_swap/p5b/make_payload.py` | Generates the patch payload **from the installed records**: our definitions are appended to the installed `ui/options_menu/gen/options_menu.dtb`, the owned scene is the installed `options_menu.milo_xbox` copied under a project-owned name, and the entry rows are re-pointed in the installed `splash.dtb`. No retail bytes are stored in the repository. |

Deliberately **not** in this package: the real Ultimate provider (P6), the restart flow (P9), the
hide-list/refresh/pool behaviour (P10), first-run graphics policy wiring (P4/P11) and packaging
(P11). The rows for those exist and are routed; their providers are the stub bridge.

## 2. Two decisions the boots settled

### 2.1 The merged root has its own list resource

Retail's `options_panel` and any panel of ours that named `options_menu.milo` share one loaded dir
and therefore one `options.lst` object, which is why P1's prototype had rows and selection
overwriting retail's and Back landing on `credits` (bridge findings §12.3b).

Our panel uses **`{file settings_menu.milo}`**, a project-owned scene record whose bytes are the
installed retail scene copied at boot under a name of ours
(`ui/options_menu/gen/settings_menu.milo_xbox`). A different path is a different loaded dir is a
different `options.lst` instance, so "no row dispatches through retail `options.lst`" is true by
construction - and `TestListOwnership` fails if any declaration ever names retail's scene again.
The copy has to sit **beside** the retail scene, because the scene refers to its neighbours by
relative path (`../resource/lists/list_options_main.milo`, `neon_lights.milo`).

### 2.2 Where a definition of ours may live

Measured in P1 and re-confirmed here: our definitions **ride inside a retail record**. Appending
them to the installed `ui/options_menu/gen/options_menu.dtb` works; the same declarations in a
record of our own reached by `[include]` parse but their panel is unusable (the guest faults with
`write of guest 0x00000200`). The panel also needs `{<panel> load}` in `ui/gen/ui.dtb`'s init block,
the way retail creates `options_panel`, or it has no focus component and its list never takes the
direction keys. `AddPanelLoad` does that, idempotently, immediately before `{set {var
first_screen} ...}`.

## 3. The one open item: the in-song entry

Retail's in-song HELP & OPTIONS is **not a screen jump**. `pause_panel`'s `{enter}` fills a *second
list in the same scene* with the five help rows, and the pause row switches focus to it:

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

Every body that **leaves the pause list** instead faults the guest with
`read of guest 0x00000024` on the pad's Confirm. Four arms, same fault:

| Arm | Body written for `pause_options` | Result |
| --- | --- | --- |
| A | `{transition_to settings_pause_screen}` - the primitive retail's own `audio_video_options` row uses from that panel | fault at Confirm |
| B | `{ui goto_screen settings_pause_screen}` - the main menu's primitive | fault at Confirm |
| C | `{do {this update_focus options.lst} {ui goto_screen settings_pause_screen}}` - retail's switch first, then the jump, in one frame | fault at Confirm |
| D | as A, with the in-song screen hosting the **retail** `av_options_panel` instead of ours | fault at Confirm |
| E | the same payload with `ui/pause/gen/pause.dtb` **left pristine** | the route runs clean; Confirm opens retail's in-song page (How to Play / Controls / Calibration / Audio/Video / Credits) and B returns to the pause menu |

D is what rules out our panel and our scene; E is what rules out the route, the driver and the
environment. The fault is the edit itself: taking the focus away from `pause.lst` while the pause
state machine still drives it leaves that machine reading a list it no longer has.

**So the shipped payload does not edit the pause record at all** (`make_payload.py` skips it unless
`--pause` is passed for an experiment), and `PauseRouteBody()` reproduces retail's own statement so
that the edit is a no-op rather than a crash. The in-song page is therefore retail's own for now -
correct, and reachable - while the merge of it is the next experiment.

### 3.1 The next experiment, in order

1. **Merge into the inline list.** Replace the five symbols `pause_panel`'s `{enter}` fills
   `options.lst` with (keeping the retail spellings `pause_controls` / `calibration` /
   `audio_video_options` for the rows that already have working cases, adding our
   `settings_general`, `settings_gameplay`, `settings_interface`) and add the missing cases to that
   switch. This needs one new edit primitive beside `RerouteRow`: **add** a case array to a switch
   (today the module can only replace a case body).
2. **Pause-context child screens.** Retail pairs its pages (`av_options_screen` /
   `av_options_pause_screen`, same panel, `{panels game_panel pause_panel background_night_panel
   <panel>}`, `{back_screen game_screen}`). Our submenus need the same pair, or the in-song rows
   have nowhere to go. The generator already builds a screen per panel, so this is a second screen
   per panel with a context-dependent panel set and back target.
3. Only then re-point `pause_options` itself, if the merge is to keep the retail pause list's row
   count. Until step 1 lands, the row stays retail's.

### 3.2 What the in-song page can be, measured (P5b-r2)

P5b-r2 implemented §3.1 (the list merge, the appended cases, the in-song twins) and then measured it
in ten boots. The dispatch half works; the *page* half is where the fault lives, and it is not where
the four arms above guessed. Every arm below used the same staged route
(`out/_swap/p5b/run.ps1 -Route p11a-v|p11a-5|p11a-lab*`) and a shot at every step, so "the press did
nothing" and "the page did not appear" can be told apart.

| Arm | What the row was pointed at | Live? | Log | On screen |
| --- | --- | --- | --- | --- |
| v2 (probe 4) | retail's `controller_config_pause_screen`, plain jump | yes, 0 faults | `q1`, `q2` | CONTROLLER PRESETS |
| v6 (probe 4) | our `settings_interface_pause_screen` twin, plain jump | **fault `read of guest 0x00000024`** | `s1`, no `s2` | - (its panel never entered: no reads) |
| v3 | our `settings_general_pause_screen` twin, `EnterScreen` | **fault 0x24** | - | - |
| v7 (probe 5) | our `settings_pause_screen` root twin | **fault 0x24** | - | - |
| lab1 (probe 6) | variant A: our **in-song** panel, **menu** panel set | entry fine; **fault `read of guest 0x00000000` on B** | `provider.active` | **HELP & OPTIONS + our five rows, in song** |
| lab2 (probe 6) | variant B: our **menu** panel, **in-song** panel set | **fault 0x24** | - | - |
| lab3 (probe 7) | in-song set **minus `game_panel`** | **fault 0x24** | - | - |
| split1 | the twin, declared in the **pause record** instead | **fault 0x24** | - | - |

Stated as narrowly as the evidence allows:

* **A screen of ours cannot list the in-song panels** (`pause_panel`, `game_fade_out_panel`, and the
  `game_panel` that group shares with `game_screen`). Any such screen faults the guest with
  `read of guest 0x00000024` *while it is constructed* - before its own panel's `{enter}` runs, which
  is why no bridge read from that panel is ever logged, while variant A's panel reads its gate at
  once. Retail's own in-song screens (`controller_config_pause_screen`, `av_options_pause_screen`,
  `pause_credits_screen`) list the same panels and open fine, so the limitation is ours. It is not
  the scene copy (variant A hosts our in-song panel), not the panel's scene, not the declaring record
  (`split1` rode the twins in `ui/pause/gen/pause.dtb` and still faulted), and not the
  `{do {<twin> set back_screen {ui current_screen}} ...}` statement (`v6` is a plain jump). What the
  matrix does say is "the screen lists one of our panels *and* an in-song panel"; what it does not yet
  say is which of the two, or why.
* **The forward half of the goal is reached.** Variant A - our in-song root panel, the *menu* panel
  set - draws the merged root inside a song, entered by pressing retail's own HELP & OPTIONS row, with
  the game still behind it (`lab1`, `shots-lab1/06-variant-a.png`). That is the first time the merged
  page has been on screen during a song.
* **Back is not.** With the in-song panels unhostable, the game screen's panels are torn down when our
  page replaces it, so `B` - which retail's in-song pages answer with `{ui goto_screen game_screen}` -
  faults (`read of guest 0x00000000`). Keeping the pause state alive across the page is exactly what
  hosting those panels is for, and it is why retail's pages do it.

The shipped payload therefore still leaves `ui/pause/gen/pause.dtb` alone: `make_payload.py` writes it
only under `--pause`, and the in-song HELP & OPTIONS keeps working exactly as retail ships it.

### 3.3 The next experiment, in order

1. **Narrow the panel-set limitation.** The cheapest split left is one variant with *retail's*
   `controller_config_panel` in the same list as the in-song panels: that decides between "a screen
   listing one of *our* panels cannot host an in-song panel" and something about the transition
   itself. `RBBZ_PAUSE_PROBE=7` is the arm to extend - it already emits per-panel variants, and
   `dump-def` / `dump-record` read any definition back as text (see §5).
2. **Then Back.** If the in-song panels can be hosted, retail's own shape is available and Back is
   `game_screen`, as retail has it. If they cannot, the two candidates are `{ui pop_screen}` (leaving
   the game screen alive underneath instead of replacing it) and Cancel on our page calling the
   resume retail's own `pause_panel` calls (`{game_screen resume}`).
3. Only then re-point `pause_options` in the shipped payload.

## 4. Evidence

All boots: `rb_blitz.exe --game_data_root=<install> --ultimate_mode=2
--ultimate_payload_root=out/_swap/p5b/payload ...`, disposable `--user_data_root` and
`--launcher_profile`, driven with `scripts/drive_ui.ps1` through `out/_swap/p5b/run.ps1 -Route <r>`.
Payload: 5 records, ark 238,542 bytes (`ui/options_menu/gen/options_menu.dtb` 132,333 bytes / +29
roots, `ui/gen/ui.dtb` with 14 `{<panel> load}`, `ui/options_menu/gen/settings_menu.milo_xbox`
11,966 bytes, `ui/splash/gen/splash.dtb` 7,150 bytes, `ui/locale/eng/gen/locale_keep.dtb` 84,172
bytes / +117 labels).

| Proof | Route | Evidence | Result |
| --- | --- | --- | --- |
| Base install shows **five** rows, no dead `Gameplay` | `baseroot` | `baseroot.log`, `shots-baseroot/00-root.png` | `General / Controls / Calibration / Audio/Video / Interface`, General selected |
| Down and up walk the list and **clamp at both ends** | `baseroot` | `shots-baseroot/01..05-clamp.png`, `06..10-up-clamp.png` | INTERFACE selected at the 4th down and stays through the 5th; GENERAL the same going up |
| B leaves the merged root to the **screen it came from** | `baseroot` | `shots-baseroot/11-back-menu.png` | the main menu (not `credits`) - the P1 defect is gone |
| A child row enters a retail screen and B returns to the parent | `nav` | `shots-nav/04-confirm.png`, `05-back.png` | Audio/Video opens the retail VIDEO/AUDIO page; B lands back on the merged root |
| A list **longer than five rows** draws, scrolls and clamps | `basescroll` (Interface -> Hide Items -> Main Menu, 8 rows) | `shots-basescroll/05-main-menu-0.png` (row 0 + 6 visible), `09.png` (window scrolled to LEADERBOARD..EXIT GAME, HELP & OPTIONS selected), `12-last.png` (EXIT GAME), `15-back-root.png` | 8 rows navigate with correct selection and focus; 5 is a retail list size, not a cap |
| Ultimate present = **six** root rows, and its own long list navigates | `sixscroll` with `REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE=1` | `sixscroll.log` (`provider gate forced active`), `shots-sixscroll/00-root.png` (+ OCR), `02-gameplay-0.png`, `04.png`, `07-last.png`, `08-back-root.png` | six rows with `Gameplay`; the Gameplay panel fills (`Precision Mode: OFF`, `Disable Random Track Wiggles`, `Camera Angle: Default`, `Game Speed: 100%`, `Disable Depth of Field: OFF`, `Plots & Skyboxes`, `Gems`) and walks to its last row; B returns to the root |
| In-song entry, **retail's page**, no fault | `pause` (project's measured pause route) | `pause.log` (0 guest faults), `shots-pause/03-pause-options-row.png` (Help & Options selected), `04-in-song-root.png` (GAME PAUSED + How to Play / Controls / Calibration / Audio/Video / Credits), `05-back-to-pause.png` | reachable and correct while §3 is open |
| The installed retail records are never modified | any | `records-before.sha256` vs a re-hash after the boots | identical (3 records: `default.xex`, `main_xbox.hdr`, `main_xbox_0.ark`) |

The provider arm uses the bridge's own diagnostic gate
(`REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE=1`), which seeds `provider.active = true`. That proves the
**row visibility and list length** rules of the plan's §3; it is not a claim that the real Ultimate
adapter works - that is P6's, and its rows currently talk to the stub service.

**Consequence worth stating plainly.** Because the gate is still the stub's answer, a machine that
*does* have Ultimate installed shows five rows today: `Gameplay` is not "an Ultimate row" in the
sense of "appears when Ultimate is installed", it is a row that appears when the **adapter reports
the provider active**, and nothing answers that question for real until P6 lands. Anyone testing
the merged screen on an Ultimate install before then should expect the five-row root and not file
it as a defect.

### 4.1 Labels

A capture of the first working build showed the defect the owner spotted: our own rows drew
`GENERAL` / `GAMEPLAY` / `INTERFACE` in capitals beside retail's `Controls` / `Calibration` /
`Audio/Video`. The cause was the label table: those strings are ours, and they were typed in the
main menu's capitals, while the retail rows beside them come from the options *page* locale
strings. They now read `General` / `Gameplay` / `Interface`, and the same correction was applied
to every other label of ours (`Downloadable Content`, `Anti-Aliasing`, `Precision Mode`,
`Anisotropic Filtering: 16x`, `Native (2x)`, ...).

Two deliberate exceptions stay in capitals:

* the **Main Menu mirror** (`Hide Items -> Main Menu`) lists the menu's own entries
  (`PLAY`, `LEADERBOARDS`, `ACHIEVEMENTS`, `HELP & OPTIONS`, `UPGRADE`, `DOWNLOAD CONTENT`,
  `EXIT GAME`) and therefore mirrors the menu's own spelling - including `PLAY` and `LEADERBOARDS`,
  which the earlier table had as `START` and `LEADERBOARD`;
* the acronyms the game itself uses: `HUD`, `FXAA`.

`TestLabels` now fails if any other label of ours carries no lowercase letter, so the all-caps page
cannot come back. Boolean rows still render their state in capitals (`Cache DLC Library: OFF`),
matching the state suffix retail's own pages use; say the word if you would rather read
`: Off` / `: On`.

## 5. Reproduce

```powershell
# The module and its checks (SDK-free).
cmake --build out/build/win-amd64-release --target rb_blitz_settings_screen_tests
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe        # 13,285 checks, exit 0
ctest --test-dir out/build/win-amd64-release -R settings_screen

# Reading a definition (or a retail record) back as text. `Compile` encrypts the stream, so the
# compiled files are not greppable; these decrypt and print, which is how the in-song arms were read.
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-def settings_general
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-record out\_swap\p5b\retail_cc.dtb
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-record out\_swap\p5b\retail_ui.dtb init

# The payload (regenerated from the installed records; nothing retail is stored).
python out\_swap\p5b\make_payload.py payload

# The payload that carries the in-song merge as well - an experiment, never shipped.
python out\_swap\p5b\make_payload.py payload-r2 --pause

# The boots. One at a time: the harness refuses to start while rb_blitz is running.
pwsh -File out\_swap\p5b\run.ps1 -Label baseroot  -Route baseroot  -Seconds 300
pwsh -File out\_swap\p5b\run.ps1 -Label basescroll -Route basescroll -Seconds 300
$env:REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE='1'
pwsh -File out\_swap\p5b\run.ps1 -Label sixscroll -Route sixscroll -Seconds 300
pwsh -File out\_swap\p5b\run.ps1 -Label pause     -Route pause     -Seconds 300
```

Evidence lands in `out/_swap/p5b/` (`<label>.log`, `shots-<label>/`). `out/_swap/` is ignored and
its decoded retail/Ultimate material is research, not a deliverable.

## 6. Handover

* **P6** owns the frozen row identity and destinations in `AllPanels()`: every provider row is
  marked `provider_only`, and its `setting` is the stable `<category>.<id>` token on top of the
  contracts' §5.1 ids. Provider rows currently read and write through the stub bridge.
* **P9** owns the per-row `hot`/`restart` strategy and the restart flow. The `Target` rows
  (`kChoose`) deliberately only set `general.target` and leave the screen: the restart prompt is
  P9's, not this module's.
* **P10** owns the hide list, the 50-song pool and the refresh protection; the Main Menu panel
  already lists every actual row by stable token and keeps HELP & OPTIONS present and inert.
* **P11** owns packaging: the payload generator here is the *shape* required (generate the patch
  record from the installed records at boot), including the owned scene copy.
* Nobody should re-point `pause_options` until §3.3 step 3; §3.2 is what the row's own arms
  measured, and it is why the payload generator still leaves the pause record pristine by default.
* The **whole plan's** remaining work - every package, every open item, in one numbered list - is
  [in-game-settings-remaining-work.md](in-game-settings-remaining-work.md). The in-song entry is item
  13 there and is the only open item that blocks gate G1b.
