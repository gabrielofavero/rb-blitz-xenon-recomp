# P8 — Controller Settings: the launcher reduction, and the Guide prohibition

Status: **executed** (host tests green, runtime evidence captured). The **full launcher is still the
default surface**: the reduced one exists behind `--controller-only` and the cutover is P11's, per
`docs/plans/in-game-settings-contracts.md` §9.3. No installer or native-menu file was touched (P7 and
P5b own those and were running in parallel).

Contract version: P0's frozen contracts as recorded in `in-game-settings-contracts.md` (§2.3 Guide,
§8 controller allowlist, §9.1/§9.3 ownership and the development gate). No contract shape changed;
this pass adds one default-off cvar and one `LaunchOptions` parameter, both named below.

Read with: `docs/plans/in-game-settings-plan.md` §3.6 (retired interface), §9.3 (reduction), §11
P8's prompt; `in-game-settings-contracts.md` §2.2/§2.3 (the controller inventory and every Guide
location).

---

## 1. What P8 was asked for, and what this pass did

| Requirement (plan §11, P8) | Where it landed | Evidence |
| --- | --- | --- |
| Keep every controller function except Guide, bespoke widgets included | `launcher/src/controller_tab.{h,cpp}`, `src/launcher/remap.{h,cpp}`, `launcher/src/shell.{h,cpp}` | §3.2, §4.3 |
| A single controller-focused screen | `Shell` in Controller Settings mode; new `--controller-only` gate | §3.1, §4.2 |
| Preserve keyboard/pad navigation, capture, mouse and trigger bindings, presets/resets, nav rebind, safe mode, help | untouched and re-walked | §4.2, §5 |
| Remove non-controller tabs/rows and their unused wiring | the reduced surface draws none of them; the deletion itself is P11's (gate §9.3) | §3.1, §7 |
| Keep filenames; rename visible text | `rb_blitz_launcher.exe` and `launcher.toml` kept; window title/heading/help/README renamed | §3.4 |
| Route Update and Install Ultimate to the installer's workflow | no Update button and no check on the reduced surface; documented | §3.3 |
| A Play action with no native-setting flags | `LaunchOptions::settings_rows` in `launcher/src/game_launch.{h,cpp}` | §3.5, §4.4 |
| Guide: no source, no target, no capture, no pass-through | `src/launcher/remap.cpp`, `launcher/config/settings.toml`, `launcher/src/controller_tab.cpp`, `src/input/remap.cpp` | §2 |
| Old Guide entries inert with a clear diagnostic | `remap::IsRefusedTargetName`/`RefusedRowNames`; panel warning; game log | §2.3 |
| Guide cleared from the final guest state on every input path | `remap::WithoutProhibitedButtons` + an always-installed filter | §2.2, §4.5 |
| Do not disable unrelated triggers or OS Guide behaviour | no SDK patch, no cvar consumer changed; only our own filter's output | §2.4 |
| Do not overwrite native settings or paths | the launcher writes only `launcher.toml`; `paths.toml`/`settings.toml` untouched | §6.2 |
| Legacy profiles and shortcuts stay usable | `launcher.toml` name/format unchanged; `[remap]` rows preserved | §6.1, §6.2 |

## 2. The Guide prohibition

### 2.1 The vocabulary (shared by both executables)

* `remap::Target` lost `kGuide`; `kTargetCount = 16`; `kTargetInfo` lost the `guide` row, so
  `ParseTarget("guide")` and `ParseSource("pad:guide")` return nothing. A `[remap]` row that spells
  it is therefore *not a binding*: `Table::FromRows` keeps it verbatim in `unknown_` and
  `Table::ToRows` writes it back, which is exactly "inert preservation data" - a save neither drops
  a person's old row nor applies it.
* `remap::kGuideButtonBit` (0x0400 = `X_INPUT_GAMEPAD_GUIDE`) and
  `remap::WithoutProhibitedButtons(state)` are new. `Apply` strips the bit from the copy it starts
  from, so no binding can put it back and every caller of `Apply` is covered.
* `remap::IsRefusedTargetName` / `RefusedRowNames(rows)` name the refusal, so a caller can say *why*
  a preserved row does nothing instead of reporting it as a row it does not understand.

### 2.2 The game's filter

`src/input/remap.cpp` used to install nothing at all when the profile was missing or unreadable.
It now installs the state filter on every path:

* profile readable → `remap::Apply(context->table, …)` (which strips the bit);
* no profile, or one this build cannot read → `remap::WithoutProhibitedButtons(pad)` only, so the
  pad keeps the SDK's own reporting and Guide still cannot reach the guest.

That covers contracts §2.3's four paths: profile present, absent, malformed, and an SDK cvar
(`guide_button` / `keybind_guide`) that raised the bit before the filter runs. Nothing in the SDK
changed: the cvars still exist and still do what they do; our filter is the seam that takes the bit
back out, exactly as it does for a remap.

### 2.3 The diagnostics

* **On screen**: the Controller Settings mapping block prints "Guide is not remappable: the 'guide'
  row in your settings file is kept but ignored, and Guide is never passed to the game." whenever
  `session.profile().remap` contains such a row.
* **In the log**: `remap: Guide is not remappable, so the profile's guide row is ignored (and Guide
  is kept out of the pad state the guest sees)`.
* **In the schema**: the `guide_button` row is gone from `launcher/config/settings.toml`, replaced by
  a comment recording why it must not come back. The generated table is 27 rows (was 28).
* **In migration**: unchanged from P3 - `guide_button` and `keybind_guide` are already in
  `IsRetiredKey`, so a legacy row becomes inert preservation data rather than a new preference.

### 2.4 What was deliberately not done

The SDK's `guide_button`, `keybind_guide` and their drivers are untouched, and no `patches/rexglue-sdk`
entry was added: disabling them would change behaviour for a user who hand-edits `rb_blitz.toml`, and
the requirement is about what the *guest* receives, which the filter owns. The OS's own Guide handling
is likewise untouched - the project never had a global hook for it.

## 3. Controller Settings

### 3.1 The switch

`--controller-only` (parsed in `launcher/main.cpp`, held as `ShellEnvironment::controller_settings_only`).
Default off: the full launcher stays the default surface until the coordinator approves G4/P11, as
§9.3 requires. Three decisions follow from it, all in the launcher:

1. `Shell::layout_` is `BuildTabLayout(kController)` instead of `BuildLayout()`. One tab means the
   strip is not drawn (a strip holding one button is a control that does nothing); the screen's own
   heading takes its place.
2. `Shell::bar_` is `{Close, Save, Play}` instead of `{Update, Close, Save, Play}`. The bottom bar
   was refactored from four fixed indices to an ordered vector of `BarOption`s, because the ring
   size, the help lookup, the focus trace and the activation all have to agree about which controls
   exist. (This also fixed a latent out-of-bounds read: the focus trace indexed a three-element name
   array with a four-valued index.)
3. `CurrentLaunchCommand` passes `LaunchOptions{settings_rows = false}`, and the target's fallback
   reads `DetectUltimateState` directly rather than the General tab's state, which no tab has set on
   a surface that draws no General tab.

### 3.2 What the screen is

One heading, the schema's one remaining controller row (*Mouse support*), and the mapping block: 16
controls × (Assign, Reset) plus *Reset all*. The walk is 19 rows / 37 ring entries. Everything the
block does is the code D16 already had - the three-second multi-source listen (pad, key, mouse), the
rising-edge capture, *Assign* adds rather than replaces, *Reset* returns a control to its default,
*Reset all* clears the table - and the ring is driven by the same `NavSource`s, so keyboard and pad
both walk the whole screen.

### 3.3 Update and Install Ultimate

Neither is on the reduced surface: no Update button, no prompt, and `UpdateChecker::Start("")` so no
check runs at all. The documented routes are the ones that already exist - the updater the setup
executable leaves in `%LOCALAPPDATA%`, and `rb_blitz_setup_helper.exe install-ultimate` - reached from
the installer or by hand (`installer/README.md`, `docs/distributable.md`; P7 owns their text). The
full surface keeps both until the cutover, so nothing is lost in the meantime.

### 3.4 Naming

`WindowTitle(bool)` now returns "Rock Band Blitz Controller Settings (vX)" for the reduced surface and
keeps the launcher's own name for the development surface; the `Fatal` message box follows it, and so
does `--dump-display`'s report. The executable is still `rb_blitz_launcher.exe` and the profile is
still `launcher.toml`, deliberately (shortcuts and profiles keep working). `rb_blitz_launcher.rc`
carries no description strings, so there was nothing to rename there - the installer's shortcut names
and setup text are P7's, and this pass did not touch `installer/`.

### 3.5 Play

`LaunchOptions{settings_rows}` is the seam. The full surface passes every row it moved off its default
and the game does not own (`--user_data_root`, `--mouse_ui_nav`, …); Controller Settings passes
`--game_data_root` and `--launcher_profile`, and nothing else. What the screen edits still reaches the
game, because `[remap]` and the controller cvars are read from the profile by the runtime itself
(contracts §8) - not from a command line. See §4.4 for the printed proof.

**Updated by the P11b cutover (2026-10-13)**, which this section predates: the full surface no longer
passes the rows the in-game Help & Options tree owns (`kGameOwnedRows` in
`launcher/src/game_launch.{h,cpp}`), and the **target flags are gone from both surfaces** - the
profile's `[launch] target` is imported into the game's own settings on the first boot instead, so the
in-game Target row stays editable. Controller Settings' identity-only command line is unchanged by
that. The game-side reading of the profile is narrowed to the controller rows and the launcher-only
host rows (`settings::ProfileRowsTheGameApplies`); the rest of the table is preservation data. The
status of the cutover is in
[in-game-settings-remaining-work.md](in-game-settings-remaining-work.md).

## 4. Evidence

All commands run from the repository root in this checkout. `out/_swap/p8/` is scratch and ignored.

### 4.1 Host tests

```powershell
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

`100% tests passed, 0 tests failed out of 29` (3.3 s). New and changed checks:

| Test | Added |
| --- | --- |
| `launcher_remap` (251 checks) | the 16-target vocabulary and the 0xF3FF bit union; `guide` refused as target and as source; the preserved row, `RefusedRowNames`, `ToRows` round trip; the bit cleared through `Apply` and through `WithoutProhibitedButtons` |
| `launcher_launch` (68 checks) | the identity-only command, with the same session proving the full builder still passes its rows |
| `launcher_session` (298 checks) | the disk-merge save: a concurrent `[settings]` edit and a new table survive, this window's edits are written, a removal is still made, and the geometry/update writes follow the same rule |
| `settings_service` (89 checks) | `ControllerProfileRows`: the controller rows are kept, the retired Guide pair and the native rows are not |

### 4.2 The two surfaces, driven

`out/_swap/p8/run.ps1` launches each surface against a scratch profile, foregrounds the window,
walks the ring with the launcher's own bound keys, captures a picture per step, and asserts on
`--focus-log`. Both surfaces exit 0 and all ten assertions pass
(`out/_swap/p8/controller-focus.txt`, `out/_swap/p8/full-focus.txt`):

| Surface | Trace |
| --- | --- |
| Controller Settings | `tab=controller entry=0/37 rowindex=0/19 row=mouse_ui_nav` → `… rowindex=18/19 row=bar:close` → `bar:save` → `bar:play` → `mouse_ui_nav` → `rowindex=1/19 row=button-mapping-block` |
| Full launcher | `tab=general entry=0/15 rowindex=0/9 row=launch.target` → `… row=bar:update` → `bar:close` → `bar:save` → `bar:play` |

So: the reduced surface really has one tab, three bar controls and no Update; the full surface still
has its four tabs and four bar controls. Pictures: `out/_swap/p8/controller-*.png` (the screen, with
its title bar reading *Rock Band Blitz Controller Settings (v0.1.0)*, the 16 controls, and a bar of
Close / Save / Play Game) and `full-*.png`.

### 4.3 Headless layout

```powershell
rb_blitz_launcher.exe --dump-layout=<file>                    # full launcher: 4 tabs, no guide row
rb_blitz_launcher.exe --controller-only --dump-layout=<file>  # one screen, 1 row, 3 unbuilt groups
```

`out/_swap/p8/layout-full.txt` and `layout-controller.txt`. Both report `every row carries a
tooltip`; neither contains a Guide row.

### 4.4 The two launch commands, from one profile

`out/_swap/p8/command-full.txt` and `command-controller.txt`, produced from the same
`out/_swap/p8/cmd-profile/launcher.toml` (`target = common`, `fullscreen = false`,
`mouse_ui_nav = true`, `user_data_root = "D:\Saves Here"`, `a = "pad:b, key:space"`):

```text
full       : … rb_blitz.exe --game_data_root="…\game" --user_data_root="D:\Saves Here"
             --fullscreen=false --mouse_ui_nav=true --ultimate_mode=0 --launcher_profile="…"
controller : … rb_blitz.exe --game_data_root="…\game" --ultimate_mode=0 --launcher_profile="…"
```

### 4.5 What the runtime does with the state (source-level, not runtime-proven)

The filter change is in the game executable, and the game was running for the whole pass (the
owner's session), which holds `out\build\win-amd64-release\rb_blitz.exe` open: `ninja` therefore
stops at the link with `failed to write output 'rb_blitz.exe': permission denied`, and any further
`cmake --build` does the same until the game is closed. Everything else is verified:

* every translation unit compiles - `ninja CMakeFiles/rb_blitz.dir/src/input/remap.cpp.obj
  CMakeFiles/rb_blitz.dir/src/launcher/profile_apply.cpp.obj` exits 0;
* the link itself is sound: the same `clang++` command with a scratch output name
  (`@CMakeFiles\rb_blitz.rsp -o rb_blitz_p8check.exe`) exits 0 and produces a 40,260,096-byte
  executable, so nothing is missing from the link;
* the launcher, both test executables and the game's other objects are current, and `ctest` is 29/29.

To finish it in place, with the game closed:

```powershell
cmake --build out\build\win-amd64-release --target rb_blitz
```

The runtime claim that is therefore still **unverified** is the end-to-end one: a boot with
`guide_button = true` (or a `guide = pad:guide` row) in the profile, and a log line plus a pad read
showing the guest never sees bit 0x0400. The unit-level chain is proven (vocabulary refuses it,
`Apply` strips it, the no-profile path strips it, the filter is installed on both paths); the boot is
the coordinator's or P12's check.

## 5. Files changed

| File | Change |
| --- | --- |
| `src/launcher/remap.{h,cpp}` | Guide out of the vocabulary; `kTargetCount`, `kGuideButtonBit`, `WithoutProhibitedButtons`, `IsRefusedTargetName`, `RefusedRowNames`; `Apply` strips the bit |
| `src/input/remap.{h,cpp}` | the filter is installed on every path; the no-profile arm strips the prohibition bits; the Guide diagnostic |
| `launcher/config/settings.toml` | the `guide_button` row removed, with the reason written down |
| `launcher/src/controller_tab.{h,cpp}` | 16 controls, no Guide capture, the refusal warning, the tooltip corrected |
| `launcher/src/game_launch.{h,cpp}` | `LaunchOptions`; the settings block is skippable |
| `launcher/src/profile_session.{h,cpp}` | `RebaseOntoDisk`, `NoteWritten`, and the three writers using them |
| `launcher/src/shell.{h,cpp}` | `ShellEnvironment::controller_settings_only`; one-tab layout and heading; the dynamic bottom bar; the Play options; the target probe; the update check skipped; the "Play" hint; the focus-trace bar names |
| `launcher/main.cpp` | `--controller-only`; `WindowTitle(mode)`; `Fatal(mode, …)`; the dump and print modes describe the surface they are asked for |
| `src/settings/cvars.{h,cpp}` | `ControllerProfileRows`; `IsControllerCvar`/`ControllerProfileRows` inline (the game's source list does not carry `src/settings/cvars.cpp`) |
| `src/launcher/profile_apply.{h,cpp}` | `profile_controller_rows_only` (default off) and `ProfileApplyReport::rows_ignored` |
| `tests/launcher_remap_tests.cpp`, `launcher_launch_tests.cpp`, `launcher_profile_session_tests.cpp`, `settings_service_tests.cpp` | the cases in §4.1 |
| `launcher/README.md` | the new "Controller Settings (P8)" section; the Controller tab, grammar and intro text corrected |

No file outside the launcher lane was touched except `src/input/remap.*`, `src/settings/cvars.*` and
`src/launcher/profile_apply.*`, all of which contracts §9.1 assigns here. `installer/`,
`src/ui/settings_screen.*` and `src/rb_blitz_app.h` were not modified (P7, P5b and the coordinator
were working in parallel).

## 6. What is left for others

### 6.1 The coordinator

1. **Flip `profile_controller_rows_only` to true — but only together with the boot wiring that gives
   the game its own settings file.** Contracts §8 narrows `ApplyLauncherProfileSettings` to the
   controller allowlist "once migration is complete"; today the launcher profile is still where
   `user_data_root` and `dlc_root` come from, so narrowing before the native store owns them would
   lose the DLC library and the save location. The switch is the hands-off version of that change:
   `src/launcher/profile_apply.cpp`, default false, with `ProfileApplyReport::rows_ignored` saying how
   many retired rows were left out. Nothing else needs to change there.
2. **Add `src/settings/cvars.cpp` to `RBBLITZ_SOURCES`** when the rest of the cvars module is wired;
   the two helpers P8 uses are inline in the header precisely so that a caller in the game target does
   not depend on that being done first.
3. **The cutover itself** (§9.3, P11): make Controller Settings the default surface, then delete the
   non-controller tabs, the General tab's settings-file block, `install_ultimate.*`, the update prompt
   and `Update`'s plumbing out of `launcher/`. What that deletion must not take with it: the updater
   and installer entry points (they are the installer's, and are documented), the profile's
   `[remap]`/`[nav]`/`[update]` tables, and `--dump-*`/`--print-command`.
4. **One game boot** to close §4.5: a profile carrying `guide = "pad:guide"` (and a boot with
   `--guide_button=true`), then a pad read or the log line showing the guest's state never carries
   0x0400.

### 6.2 P12 (acceptance)

* `scripts/acceptance_launcher.ps1` still asserts the settings-as-argv contract for the **full**
  surface, which this pass deliberately did not change. The reduced surface is a different contract;
  extend the harness with a `--controller-only` leg (the walk, the three-button bar, "no Update") and
  change the argv expectations only at the cutover.
* `scripts/capture_launcher.ps1` walks four tabs and expects `bar:update` first; a reduced-surface leg
  belongs beside it, and `out/_swap/p8/run.ps1` is the working prototype (foreground, scan-coded keys,
  trace assertions).

### 6.3 P5b / P6 / P10 (recorded here because they touch the same surface)

* The merge retires the Advanced Settings row and routes Ultimate's `mod_settings` into Help &
  Options; the reduced launcher screen is reached by the installer's shortcut instead of by a row in
  the game's menu, so nothing in this pass conflicts with that.
* The `[remap]` refusal is part of the merged screen's vocabulary too: the in-game Interface/HUD rows
  never had Guide fields, and the panel API (`in-game-settings-panel-api.md`) should keep it that way.

## 7. Known limitations, stated plainly

* **The reduced surface is not the default.** That is the gate, not an oversight (§9.3): the full
  launcher, its schema and its argv contract stay the product until G4/P11.
* **The non-controller UI is still compiled in.** "Remove the dead wiring" is satisfied for the screen
  (it draws none of it and its ring has no entries for it) but not for the binary, because deleting it
  before the replacement passes acceptance would leave the product with no settings surface at all.
* **`src/settings/cvars.cpp` is still not in the game's source list**, so the native cvars module is
  only exercised by its tests. The allowlist filter P8 added lives in the header for that reason.
* **No test binary exercises the Guide bit end to end** (§4.5). `src/input/remap.cpp` needs SDL, an
  `InputSystem` and a guest to be meaningful; the parts that decide are pure and are covered.
* **The window title of the reduced surface is new text** and is not asserted by any harness yet -
  `--dump-display` prints it, so a check is a one-line addition for P12.
