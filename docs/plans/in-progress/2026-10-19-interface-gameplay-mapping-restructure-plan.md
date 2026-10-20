# Interface and Gameplay Mapping restructure

**Status: in progress, 2026-10-20. P1 and P2 landed (below); P3–P5 not started.**

This plan restructures the in-game Controls surface so that **Interface Mapping** owns only the
interface, **Gameplay Mapping** is always active, and controller gameplay follows the selected
retail preset when the player has not customised it. It also makes binding rows show the actual
binding, replaces the listening dialog's hold-to-cancel gesture with a short countdown, and adds
mouse-wheel inputs to the Mouse & Keyboard defaults.

## 1. Request, normalized

| Id | Requirement |
| --- | --- |
| R1 | Rename **General Mapping** to **Interface Mapping** and make its purpose explicit: it changes the interface only. |
| R2 | Gameplay Mapping is always enabled. Remove the two `Custom` switches. Infer Custom from whether any binding row exists. |
| R3 | When Controller Custom is off, the gameplay mapping comes from the selected **Controller Presets** entry. When Custom is on, custom rows override it. |
| R4 | Reorganise Gameplay Mapping: `Controller` has `Controller Presets` and `Custom Mapping`; `Mouse & Keyboard` (renamed) goes straight into its custom rows. |
| R5 | Reorganise the custom mapping rows into `Notes`, `Move Lane`, `Pause & Power-up`, and `Reset All`. |
| R6 | Show the actual binding in each binding row, e.g. `First Note: A` or `First Note: Q, Right Click`. Long lists become `Multiple` when they cannot fit. |
| R7 | Replace the "hold to cancel" listening gesture with a **3 → 2 → 1** countdown. A valid input captures immediately; an invalid input is ignored; the only cancel is the countdown reaching zero. |
| R8 | Update the Mouse & Keyboard defaults and add mouse-wheel bindings: First Note `Q` + right click, Second Note `W` + left click, Move Right `Arrow Right` + scroll up, Move Left `Arrow Left` + scroll down, Power-up `Space` + scroll click, Pause `Escape`. |
| R9 | Pausing in a song keeps the interface under Interface Mapping and stops the gameplay tables. |
| R10 | Update tests, acceptance harnesses, and the documents this change owes. |

## 2. What exists today

| Area | Current state | Where |
| --- | --- | --- |
| Interface Mapping | `General Mapping`, 16 non-Guide controls, one Assign/Reset page per control, table-level Reset All. | `src/ui/settings_screen.cpp:708-750`; title at `:579` |
| Gameplay Mapping | `Controller` with `Default Presets` (disabled while Custom is on), `Custom`, `Custom Mapping`; `Keyboard & Mouse` with `Custom`, `Custom Mapping`. | `src/ui/settings_screen.cpp:751-833`; titles at `:580-584` |
| Custom switches | `input.custom_controller` and `input.custom_keyboard_mouse` are real switches in the profile (`gameplay_mapping_controller`, `gameplay_mapping_keyboard_mouse`). | `src/profile/input_rows.h:13-14`; `src/input/input_store.h:57-65` |
| Gameplay routing | Boot reads the two switches; a table acts only while its switch is on. | `src/input/remap.cpp:468`, `:542-553` |
| Controller defaults | Frozen to native preset 0 only. | `src/profile/gameplay_remap.cpp:79-100` |
| Keyboard/mouse defaults | Frozen to the shipped keyboard preset 0 (1/7/A/F/L …), with no mouse bindings. | `src/profile/gameplay_remap.cpp:102-118` |
| Binding description | `<label>: <sources> (custom/compiled default). Assign adds an input; Reset restores the default.` | `src/input/input_store.cpp:641-662` |
| Listening dialog | Holds for 700 ms to cancel, or times out after 20 s. The row text is static. | `src/input/input_capture.h:90`; `src/input/input_capture.cpp:23`, `:180`, `:292-303` |
| Row text | A row's text is a locale entry keyed by its symbol, baked into the menu payload at boot; arbitrary live strings are not supported. | `src/settings/menu_payload.cpp:161-176`; `src/ui/settings_screen.cpp:263-265` |
| Input sources | `kPad`, `kKey`, `kMouse`; mouse buttons are left/right/middle/x1/x2. No wheel directions. | `src/profile/remap.h:209`; `src/input/input_capture.cpp:82-91` |

### 2.1 The four retail controller presets

`assets/game/360/config/gen/beatmatch_controller.dtb` carries four joypad presets. They were decoded
on 2026-10-17 into `out/_swap/p0/defs/beatmatch_controller.txt` (gitignored evidence). The mappings
below use the project's `pad:` spellings.

| Preset | First Note | Second Note | Move Right | Move Left | Power-up | Pause |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | L-stick all + D-pad all | A + R-stick all | RT, RB | LT, LB | X | Start |
| 1 | LT, LB | RT, RB | L-stick right, R-stick right, D-pad right | L-stick left, R-stick left, D-pad left | X | Start |
| 2 | LB | RB | RT | LT | X | Start |
| 3 | A | B | L-stick right, D-pad right | L-stick left, D-pad left | X, LB, LT | Start |

`Start` is not a `kBMAction` in the retail preset; it is the game-screen Pause path. The direct
table therefore keeps its Start overlay.

## 3. Decisions

| Id | Decision |
| --- | --- |
| **D1** | **Interface Mapping layout.** Top page: `Navigation` (submenu), `Confirm` (target A), `Go Back` (target B), `Music Library Options` (submenu), `Reset All`. `Music Library Options`: `Search` (target Start), `Shortcuts` (target Y), `Sort` (target Back/Select). `Navigation`: `Up`, `Down`, `Left`, `Right` (the four D-pad targets). The user chose `Confirm`/`Go Back` to avoid confusing the face button B with the Back/Select button. |
| **D2** | **Hidden Interface targets.** The other seven `[remap]` targets (X, shoulders, triggers, thumb clicks) keep their compiled defaults and are not shown. `Reset All` still resets **all 16** `[remap]` targets. |
| **D3** | **Gameplay is always on.** Both `gameplay_mapping_controller` and `gameplay_mapping_keyboard_mouse` stop being routing switches. The old `Custom` rows and tokens disappear. "Custom" is inferred per table: `BoundCount() > 0`. |
| **D4** | **Controller custom off = native preset.** When the Controller table has no custom row, the native preset is left active and drives gameplay. When any custom row exists, the direct table bypasses the native preset and uses the detected preset's sources for every action without a custom row. This preserves the game's own preset behaviour (repeat, Start/Pause) when nothing is customised and avoids duplicating it. This is how "gameplay mapping is always enabled" is implemented for the Controller: the effective mapping is always defined; the native preset is the implementation when nothing is custom. |
| **D5** | **Controller Presets.** Rename `Default Presets` to `Controller Presets`. Keep routing to retail's `controller_config_screen` / `controller_config_pause_screen`. The row is always available; it is no longer gated by a Custom switch. |
| **D6** | **Mouse & Keyboard.** Rename `Keyboard & Mouse` to `Mouse & Keyboard`. Its Gameplay Mapping row enters the custom rows directly; there is no Custom switch. |
| **D7** | **Custom mapping groups.** Both custom pages show `Notes` → First Note, Second Note; `Move Lane` → Move Right, Move Left; `Pause & Power-up` → Pause, Power-up; `Reset All`. This keeps every page within the six-visible-row budget. |
| **D8** | **First Assign replaces the default.** On any binding row (Interface and both gameplay tables), the first Assign against a default row replaces the default list with the captured source. Later Assigns append. Reset restores the default. The caption says `Default binding from <Preset N>. Click to Replace.` for a Controller default row, `Default binding. Click to Replace.` for a Mouse & Keyboard or Interface default row, and `Custom binding. Click to add an input.` for any custom row. |
| **D9** | **Reset All is a plain action.** It always reads `Reset All`; it does not draw `: Nothing` or any state suffix. Per-row Reset keeps its current state spelling until a separate decision changes it. |
| **D10** | **Full binding text in the row.** Add a runtime label mechanism. The preferred implementation is a host-drawn overlay over the settings list row, because row text is locale-baked at boot. It draws `<base label>: <binding list>` for every visible binding row and updates after a capture. If the measured text does not fit, draw `Multiple` (the owner's original allowance). If the overlay cannot align or update reliably, the fallback is to hook the locale lookup for the `settings_bind_*` symbols; this must be measured, not assumed. |
| **D11** | **Listening countdown.** A capture arms, waits for a neutral frame, then counts **3, 2, 1** once per second. A valid new press is captured immediately; an invalid press is ignored and the count continues; reaching zero leaves the dialog with no change. There is no early cancel gesture. The dialog's row/caption shows the live count. |
| **D12** | **New Mouse & Keyboard defaults.** First Note `key:q`, `mouse:right`; Second Note `key:w`, `mouse:left`; Move Right `key:right`, `mouse:wheel_up`; Move Left `key:left`, `mouse:wheel_down`; Power-up `key:space`, `mouse:middle`; Pause `key:escape`. The old shipped keyboard defaults are replaced, not extended. |
| **D13** | **Power-up spelling.** Use the game's own spelling, `Power-up`, from the English locale (`Deploy Power-up`, `POWER-UP`). The profile action name stays `powerup`. |
| **D14** | **Pause context.** While paused, the gameplay tables are inactive and Interface Mapping handles the interface. This is already the measured P8a behaviour; the change must keep it and add a regression test. |
| **D15** | **Migration.** Old `custom_controller` / `custom_keyboard_mouse` rows and `gameplay_mapping_*` values are no longer written by the UI. They are read as legacy values but do not decide routing; custom is inferred from the tables. Existing custom rows are preserved. |
| **D16** | **Docs are part of each prompt.** Every prompt updates the documents its change touches; the final prompt runs the full suite, the docs check, and live acceptance. |

## 4. Implementation prompts

| Prompt | Context | Goal | Main deliverable | Evidence |
| --- | --- | --- | --- | --- |
| **P1** | Runtime row text | Prove and implement full binding text in rows. | A host overlay (or measured locale hook) that draws `Label: binding list` and `Multiple` when too wide. | Before/after screenshots of one default row, one 2-source custom row, one long row → `Multiple`; a capture update without leaving the page. |
| **P2** | Input model | Always-on gameplay, inferred Custom, preset-aware controller defaults, first-Assign replacement, new defaults, wheel sources, migration. | Updated `profile::gameplay_remap`, `InputStore`, routing gates, and capture grammar. | Unit tests for four presets, inference, assign/replace, reset, wheel parsing; boot log lines. |
| **P3** | Interface Mapping | Rebuild the General Mapping page as Interface Mapping. | New page tree, labels, routes, reset, hidden targets, pause regression. | Harness route: Controls → Interface Mapping → Navigation / Music Library Options → bind/Reset; paused-song arm. |
| **P4** | Gameplay Mapping | Rebuild the Gameplay Mapping tree and remove the Custom switches. | Controller Presets + Custom Mapping; Mouse & Keyboard direct; three submenus; plain Reset All; `Power-up` spelling. | Harness route through both branches; 6-row budget check; labels and states. |
| **P5** | Listening and proof | Replace hold-to-cancel with the 3→1 countdown; run the end-to-end proof and doc sweep. | Updated capture state machine and acceptance harness; full docs check. | Countdown screenshots; invalid-input ignored; timeout leaves untouched; capture writes the row; full ctest and live routes. |

### P1 — Runtime binding text

**Why first.** Every page this plan builds wants the same presentation. Row text is a locale entry
(`src/ui/settings_screen.cpp:263-265`), and the setting's value is carried only by which symbol is
in the list. The current help caption can show the bindings, but the requirement is the row itself.

**Approach.**

1. Add a pure `BindingSummary(token)` to the input store/profile layer: the display spelling of one
   source and the joined list for a row.
2. Publish the open settings panel, the visible row symbols, and the list geometry to the host.
   `src/input/native_menu.cpp` already measures the focused list (`Geometry`) and hooks
   `UIList::ScrollStart`; extend that seam rather than inventing a second geometry reader.
3. Draw the full row line in the existing ImGui overlay (`src/ui/help_overlay.cpp`) over the guest
   row's text area while one of our binding pages is open. Draw the static label if the overlay has
   no binding; cover the guest label with the overlay's own background so two texts cannot overlap.
4. Measure the width with the caption font. If the full line does not fit, draw `Multiple`.
5. Prove a capture changes the row text without leaving/re-entering the page.
6. If the overlay cannot be aligned or layered reliably, stop and measure the locale-lookup hook
   before building P3/P4. Do not ship a half-dynamic row.

**Files:** `src/input/native_menu.{h,cpp}`, `src/ui/help_overlay.{h,cpp}`,
`src/hooks/settings_bridge*.{h,cpp}`, `src/input/input_store.{h,cpp}`,
`src/profile/input_rows.{h,cpp}`, `src/ui/settings_screen.{h,cpp}`, `tests/input_bindings_tests.cpp`.

**Semantics:** the summary is the *effective* row: a preset/compiled default when no custom row
exists, the custom list when one does. The base label stays the action's own name (`First Note`,
`Search`, …), and the suffix is `: <list>`.

#### P1 — landed (2026-10-19)

**What shipped.**

- `profile::SourceDisplayName`, `BindingListText` and `BindingRowText`
  ([input_rows.h](../../../src/profile/input_rows.h)): the display spelling of one source
  (`pad:left_shoulder` → `LB`, `pad:left_thumb` → `L3`, `pad:lstick_up` → `L-Stick Up`,
  `key:Return` → `Enter`, `key:right` → `Arrow Right`, `mouse:right` → `Right Click`), the joined
  list, and the complete `Label: list` line. The profile grammar is unchanged.
- `ui::screen::ScreenBindingRows(screen)` ([settings_screen.h](../../../src/ui/settings_screen.h)):
  each Controls page's rows as `input.*` tokens in list order, both contexts, with an empty
  string for a row that is not a binding and no answer at all for any other screen. It leans on
  the page invariant the tests now pin: a binding page draws exactly one symbol per row.
- `input::MenuRowGeometry` / `VisibleMenuRows` ([native_menu.h](../../../src/input/native_menu.h)):
  the focused list's visible rows, each with its **label box** (the guest's rendered text
  rectangle) and its **cell** (the row's full clickable width). Published from the same capture
  the mouse hit test uses and cleared at every screen/focus boundary. The capture now runs
  regardless of the mouse preference, so a pad-only machine gets the row text too.
- `MenuPresentedRect` / `MenuPointToWindow` ([native_menu.h](../../../src/input/native_menu.h)):
  the presenter rectangle `MenuPointer` inverts, now also inverted the other way, so a normalized
  guest row lands in the drawer's `DisplaySize` coordinates at every resolution and with
  letterboxing or overscan.
- The overlay ([help_overlay.cpp](../../../src/ui/help_overlay.cpp)) draws the line centred over
  the guest label, with an opaque cover, and logs
  `help_overlay: binding rows <screen>: <token> = <line> | ...` once per change plus one
  `help_overlay: row text fit ...` geometry line per page.

**Measured (2026-10-19; evidence `out/interface-mapping/p1-rows/p1-h/`, screenshots
`p1-h-shots/`).**

- The guest's label rectangle **is** its rendered text rectangle: `A: Custom` projects to
  162.1 px while its glyphs occupy 167 px at `display=2354x993`. That makes the label box both
  the cover and (with the row cell) the fit budget.
- A row's full width is only carried on frames where the highlight mesh is drawn (the label box
  alone is 162 px against a 624 px row), so the snapshot keeps the widest cell seen for the page
  rather than the latest one. Reading the capture's moved-from `result.rows` instead of its
  `geometry.rows` silently paired nothing (`p1-f`); the fit line now names both boxes.
- The drawer renders the caption face below the size it is asked for (the legacy atlas path), and
  the label box's height is the guest's tight text box (39.8 px here). Asking for the label height
  and scaling by the face's own `GetFontSize()` answer puts the line on the guest's height
  (`size=29.7`).
- `Multiple`: the Controller table's First Note default (eight stick/D-pad directions) reports
  `input.controller.first_note = Multiple`; the lane and power-up rows fit their lists.
- Capture: a seeded `[remap] a = 'key:g, key:h'` row read `A: G, H`; a tap of J in the listening
  dialog made the page behind it read `A: G, H, J`, with no relaunch and no root re-entry.
- The earlier arms are kept as the record of what failed: `p1-a` (a narrower centred box left the
  guest's tail visible), `p1-b`/`p1-c` (label height mistaken for the em; the row's true width
  unknown), `p1-d` (guest width estimated through the caption face: 80 px against 167 px),
  `p1-g` (alpha 236 let the guest text ghost through the cover).

**Acceptance.** `scripts/acceptance_binding_rows.ps1` (new, tracked) — 24 steps, 0 failures,
evidence above. The P1 steps were added to `scripts/acceptance_controls.ps1` too, whose route was
re-pointed at the current root order (Controls is root row 3); that harness now runs 62 steps, 0
failures (`out/gameplay-mapping/in-game/p1-rows-2026-10-19b/`). Host suites: 37/37 `ctest`
(`input_bindings`, `settings_screen` and `native_mouse` extended).

**Re-verified 2026-10-20.** The record above was re-run on `4b5d059` (the Audio-page refactor
that followed P1) with no code change: `scripts/acceptance_binding_rows.ps1 -Label
p1-rows-2026-10-20` = 24 steps, 0 failures, evidence in
`out/interface-mapping/p1-rows/p1-rows-2026-10-20/` and its `-shots/` directory — `A: G, H`
and `Left bumper: LB` on the General Mapping page, a tap of J in the listening dialog grew the
row to `A: G, H, J` one Back later, and the Controller table shows `Multiple` on First Note and
Second Note while the lane, power-up and pause rows draw their lists. Host suites 37/37
`ctest`; `check_docs.py` 0 errors / 47 known warnings. The release build was rebuilt first
(the Audio refactor touched `settings_screen.cpp`); building the debug test tree clobbers the
release tree's SDK objects, so re-run `cmake --preset win-amd64-release` before the next
release build.

**Carried.** A replacement line wider than the guest's label box grows the cover up to the row's
cell (the fit test bounds it); no live fixture exercises that width yet. The row text is drawn for
the page that owns the focused list only — retail's controller-preset page and the listening
dialog are different screens and draw nothing.

### P2 — Input model and defaults

**Scope.**

- `profile::gameplay_remap`: add a `ControllerPreset` enum (0–3) and `PresetSources(preset, action)`
  from §2.1. `Table` gets `SetControllerPreset(int)` and `Custom()` (`BoundCount() > 0`).
  `Effective(action)` returns the bound row when present, otherwise `PresetSources(active_preset,
  action)` for the Controller table, or the compiled default for Mouse & Keyboard.
- `InputStore`: remove the two switch tokens from `InputRowKind`, `ParseInputRowToken`,
  `InputRowSymbol`, `StageBool`, and `Summary`; infer custom from the tables. Keep reading the old
  profile keys as legacy values (D15), but never write or honour them as routing.
- `CompleteCapture`: if the row is not custom, start from an empty list (replace); if it is custom,
  start from its stored list (append). Keep the duplicate check.
- `input::remap` / `source_gates`: routing no longer reads the gameplay switches. The Controller
  table is active when `gameplay && controller.Custom()`; the Mouse & Keyboard table is active
  whenever `gameplay`. `NativeActionsSuppressed()` is true only while the Controller table is
  active. The native preset stays active when the Controller table is not custom.
- Preset detection: read the controller object's preset value (`+80`, the slot the P0 probe used in
  `src/diag/gameplay_probe.cpp:388`) in the `NewController` hook and publish it to the store before
  the first gameplay dispatch. Measure whether the same value can be read while the settings page
  is open; if not, document that the Controller Presets page's selection is reflected on the next
  song and use the last observed value for the captions.
- Defaults: replace `KeyboardMouseDefault` with D12. Add `mouse:wheel_up` and `mouse:wheel_down` to
  `SourceKind`/`ParseSource`/`FormatSource` and to the capture's press detection. Wheel detents are
  one-poll pulses for the action hold; they must produce a press and a release, not stay held.
- Display names: add the source-to-text mapping used by P1 (`A`, `Right Click`, `Scroll Up`, `L3`,
  `D-pad Up`, …). Keep the profile grammar unchanged.

**Tests:** `tests/gameplay_remap_tests.cpp` (four preset tables, effective/fallback, inference),
`tests/profile_remap_tests.cpp` (wheel parse/format), `tests/input_gates_tests.cpp` (always-on
routing, controller custom active vs native), `tests/input_bindings_tests.cpp` (first assign replaces,
second adds, reset, wheel capture), `tests/settings_bridge_tests.cpp` (old switch tokens gone).

**Boot evidence:** one line naming the active preset and whether each table is custom; keep the
existing `keyboard_support`/`mouse_ui_nav` lines.

#### P2 — landed (2026-10-20)

**What shipped.**

- `profile::gameplay_remap`: the four retail presets (`PresetSources(preset, action)`,
  `PresetLabel`), `Table::SetControllerPreset/Preset/Custom()` and a preset-aware `Effective`
  (`src/profile/gameplay_remap.{h,cpp}`). `ActionLabel` now says `Power-up` (D13) and the Move Lane
  labels are `Move Right`/`Move Left`.
- `remap`: `mouse:wheel_up` / `mouse:wheel_down` are known mouse names with a direction
  (`IsMouseWheelSource`, `MouseWheelDirection`); nothing else about the grammar changed.
- `input::remap`: the two `gameplay_mapping_*` cvars and their allowlist entries are gone;
  `GameplaySwitches` keeps only the two device gates, and `GameplayRouting` is derived per poll
  (`controller = config->controller.Custom()`, `keyboard_mouse = true`). `direct_routing` is now
  `gameplay`, so a song always sees the physical pad and the direct tables own keys/mouse
  (`GeneralSourceAllowed` refuses them there unconditionally).
- `input::effective_input`: `ObserveControllerPreset`/`ObservedControllerPreset` - the retail
  preset is observed from the song controller's `+80` slot at `NewController`
  (`src/hooks/gameplay_input.cpp`) and stamped into every published snapshot.
- Wheel sampling: the window listener latches detents (`HostTakeMouseWheel`), the sampler consumes
  them once per poll into `PhysicalInput.wheel_detents`, and the capture sees them
  (`Observation.wheel_detents`) as an instant press.
- `input::InputStore`: the custom switch tokens are gone (`InputRowKind`, `ParseInputRowToken`,
  `InputSwitchToken`, `StageBool`, `Matches`, `Summary`), `CompleteCapture` **replaces** a default
  row on the first Assign (D8), `Describe` speaks D8's captions, and `Summary` names the preset and
  each table's custom-ness.
- `profile::input_rows`: the two `input.custom_*` tokens are gone and the wheel reads `Scroll Up` /
  `Scroll Down`.
- Settings surface (minimal, P4 owns the tree): the Controller page is `Default Presets` (a plain
  retail route, always available) + `Custom Mapping`; the Keyboard & Mouse page enters
  `Custom Mapping` directly (its rename to `Mouse & Keyboard` is P4's); `Kind::kRouteGated` is
  removed.
- Harnesses: `acceptance_controls.ps1` drops the three Custom-switch steps and checks that the
  preset page opens; `acceptance_binding_rows.ps1` and `acceptance_controls_in_song.ps1` are
  re-pointed at the new row indices; the in-song pause key moves from Return to Escape
  (`acceptance_song.ps1`, `probe_gameplay.ps1`, `ui_states.ps1`).

**Measured (2026-10-20).**

- Model, boot lines: `out/gameplay-mapping/p2-2026-10-20/`. A profile whose only gameplay key is
  the retired `gameplay_mapping_controller = true` reports
  `[gameplay_remap_controller] 0 row(s) (no custom row: the native preset drives)`; a profile with
  one row reports `(custom: the direct table drives the pad)`; both boots print
  `gameplay mapping: routing is always on (restructure plan D3)` and only the two device gates.
- Preset observation: `gameplay input: song controller …, controller preset Preset 1 observed`.
  **Open question 1 is answered**: a paused song's Controls pages read the live value
  (`help_overlay: settings page … live-preset=Preset 1`, every page, same run), so a caption can
  name the selection while a song is current. Before any song there is no controller, and the page
  can only show the last observed value (`controller preset=not observed yet` in the boot line) -
  recorded in `known-issues.md`.
- **A defect the new defaults exposed, found and fixed:** Escape pauses from the Mouse & Keyboard
  table, the pause menu it opens is the menu context, and the same still-held Escape then became
  Interface Mapping's `Go Back` - the game resumed 26 ms after it paused (`p2-in-song-2026-10-20`,
  trace `out/_swap/p2-pause/pause-trace/`). `CollectGameplayConsumed` + `SourceDownThunk` now
  suppress a source an active gameplay table is holding until it is released (unit case in
  `input_gates`).
- Live arms: `acceptance_controls.ps1 -Label p2-controls-2026-10-20b` = **57 steps, 0 failures**
  (`out/gameplay-mapping/in-game/p2-controls-2026-10-20b/`); `acceptance_controls_in_song.ps1
  -Label p2-in-song-2026-10-20d` = **23 steps, 0 failures**
  (`out/gameplay-mapping/in-game/p2-in-song-2026-10-20d/`, paused Controls path intact - D14);
  `acceptance_binding_rows.ps1 -Label p2-rows-2026-10-20b` = **24 steps, 0 failures**
  (`out/interface-mapping/p1-rows/p2-rows-2026-10-20b/`). `ctest` 37/37 (`gameplay_remap`,
  `profile_remap`, `input_gates`, `input_bindings`, `settings_screen` extended).
- The in-song harness's route needed one fix that is **not** P2's: the in-song root's `Controls`
  row is index 3 today (How to Play, Calibration, General, Controls, Audio/Video), and the script
  still clicked 1.

**Carried into P3-P5.** The page trees themselves (Interface Mapping's Navigation / Confirm /
Go Back / Music Library Options; the three gameplay submenus) and D9/D10's row presentation are
P3/P4's; the listening dialog still holds to cancel (P5); a preset changed on retail's page and
then carried into a song is unmeasured (the slot's meaning is P0's measurement); the physical-pad
arms remain unmeasured on this machine.

### P3 — Interface Mapping tree

**Page tree.**

```
Controls
  Input Types
  Interface Mapping
    Navigation
      Up / Down / Left / Right -> the four D-pad targets
    Confirm                -> target A
    Go Back                -> target B
    Music Library Options
      Search               -> target Start
      Shortcuts            -> target Y
      Sort                 -> target Back/Select
    Reset All
  Gameplay Mapping
```

**Work.**

- Rename the panel/screen/title from General Mapping to Interface Mapping.
- Replace the 16-row page and its 16 child pages with the 9 editable rows above; keep the
  `[remap]` tokens stable per target (`input.general.a`, `input.general.b`, …).
- Keep one Assign/Reset child page per visible row, with the new labels and the P1 row text.
- `Reset All` resets all 16 `[remap]` rows (D2); its display is plain `Reset All` (D9).
- Remove the `RouteGatedRow`/`input.custom_controller` machinery; it has no remaining user.
- Update help text to say this page changes the interface only, and that pausing returns the
  interface to these bindings (D14).
- Update `InSongPanels`/`InSongDestination` automatically through the existing suffix rules; verify
  the paused route uses the same page tree.

**Tests / acceptance:** `tests/settings_screen_tests.cpp` (row order, titles, symbols, labels,
6-row budget, back targets), `scripts/acceptance_controls.ps1` and
`scripts/acceptance_controls_in_song.ps1` (new route names and labels; no Custom switch).

### P4 — Gameplay Mapping tree

**Page tree.**

```
Gameplay Mapping
  Controller
    Controller Presets       -> retail controller_config screen
    Custom Mapping
      Notes
        First Note
        Second Note
      Move Lane
        Move Right
        Move Left
      Pause & Power-up
        Pause
        Power-up
      Reset All
  Mouse & Keyboard
    Notes
      First Note
      Second Note
    Move Lane
      Move Right
      Move Left
    Pause & Power-up
      Pause
      Power-up
    Reset All
```

**Work.**

- Remove `settings_custom_controller` and `settings_custom_keyboard_mouse`; remove the
  `Kind::kRouteGated` row for `Default Presets`.
- Rename `Default Presets` → `Controller Presets`; route it through the existing
  `controller_config_screen` / `controller_config_pause_screen` mapping.
- Rename `Keyboard & Mouse` → `Mouse & Keyboard` in the label, page title, and `TableKindLabel`.
- Build the three submenus with the action rows; use `ActionLabel` values `First Note`,
  `Second Note`, `Move Right`, `Move Left`, `Pause`, `Power-up` (D13).
- Keep one Assign/Reset child per action; the binding page's caption follows D8.
- Make both `Reset All` rows plain actions (D9).
- Keep every page ≤ 6 rows; the custom page has 4, each action page has 2.
- Update the help text: Controller Presets is always available; a custom row switches the
  Controller table away from the preset; Mouse & Keyboard is always active.

**Tests / acceptance:** `tests/settings_screen_tests.cpp` (tree, titles, no custom tokens, row
labels, 6-row budget), `scripts/acceptance_controls.ps1` (both branches, presets row always opens,
submenus, reset), `docs/settings/plan.md` §3.

### P5 — Listening countdown, acceptance, and docs

**Capture state machine.**

- Keep the neutral-frame rule: the press that opened the dialog cannot be captured.
- Replace `kHoldToCancel` and the 20 s timeout with a 3-second armed countdown. Publish the
  remaining seconds to the dialog caption/row once per second.
- Valid input: capture immediately (release not required for the binding decision), close the
  dialog through the existing host Back pulse.
- Invalid input: ignore it for binding; keep the countdown running. The current wrong-kind message
  may be shown once, but it must not reset or stop the count.
- Countdown reaches zero: leave the dialog, leave the row untouched.
- Remove `still_held`/hold logic that exists only for the old cancel gesture; keep any release
  oracle needed by the new capture path or delete it if unused.
- Update `StatusText` and the help overlay text: no "hold to cancel" wording.

**Acceptance and docs.**

- Run the full build and `ctest`; run the Controls acceptance scripts in the main menu and in a
  paused song. The acceptance arms must prove: the countdown leaves the row untouched; a valid
  press binds; an invalid press does not bind; a 3-second silence leaves; first Assign replaces;
  Controller Presets always opens; Controller Custom off follows the selected preset; Mouse &
  Keyboard defaults are the new list; wheel up/down and middle click bind; Interface Mapping rows
  show the binding; pausing keeps Interface Mapping in charge.
- Update `docs/settings/plan.md` §3, `docs/settings/menu.md` part 1, `docs/settings/status.md`,
  `docs/settings/contracts.md` (row tokens and source spellings), `docs/engine/toggles.md` (remove
  the two gameplay switches; keep `keyboard_support` and `mouse_ui_nav` as device gates),
  `docs/engine/input-flow.md` (always-on routing, preset fallback, wheel), and
  `docs/reference/known-issues.md` (hold-to-cancel superseded, switch removal, any preset-detection
  limitation).
- Add the plan row to `docs/plans/todo/README.md` and `docs/README.md`.

## 5. Acceptance matrix

| Case | Expected |
| --- | --- |
| Interface top page | 5 rows: Navigation, Confirm, Go Back, Music Library Options, Reset All |
| Music Library Options | 3 rows: Search, Shortcuts, Sort |
| Navigation | 4 rows: Up, Down, Left, Right |
| Controller page | 2 rows: Controller Presets, Custom Mapping |
| Custom Mapping (either table) | 4 rows: Notes, Move Lane, Pause & Power-up, Reset All |
| Controller Custom off | No custom rows; native preset drives; caption says default from the selected preset |
| Controller Custom on | At least one custom row; direct table bypasses the preset; unmodified actions use the preset |
| Mouse & Keyboard | Always active; no Custom switch; defaults are D12 |
| First Assign on default | Replaces the default list with the captured source |
| Second Assign | Adds to the custom list |
| Reset / Reset All | Restores default(s); Reset All reads `Reset All` |
| Binding row | Shows `Label: <list>`; too-wide list shows `Multiple` |
| Listening | 3→1; valid input captures; invalid ignored; zero leaves untouched |
| Paused song | Interface Mapping handles the UI; gameplay tables inert |
| Wheel | `mouse:wheel_up` / `mouse:wheel_down` parse, capture, dispatch as one-poll presses |
| Migration | Old switch values do not decide routing; existing custom rows survive |

## 6. Open questions and unproven items

1. **Preset reading before a song. Answered by P2 (2026-10-20).** The preset lives at the song
   controller's `+80` slot and is observed at `NewController`; while a song is paused the Controls
   pages read the **live** value every frame (`help_overlay: settings page … live-preset=Preset 1`,
   `out/gameplay-mapping/in-game/p2-in-song-2026-10-20d/`). Before any song there is no controller
   to read, so the page shows the last observed value and the boot line says `not observed yet`;
   a selection made on retail's page is therefore reflected from the next song, which is also when
   retail applies it. Carried: no boot has changed the selection and then started a song to watch
   the value move.
2. **Row overlay alignment.** The host overlay must align with the guest list at every supported
   resolution and with both the main-menu and pause scenes. P1 measures this before P3/P4 depend on
   it. A failed measurement stops the prompt; the fallback is the locale-lookup hook, not a static
   `Default`/`Custom` row.
3. **Interface mouse sources.** The native pointer owns the mouse in menus, so a mouse source bound
   to an Interface Mapping row would be inert. Recommendation: reject mouse sources for Interface
   Mapping capture rather than ship a dead binding. Owner confirmation is requested.
4. **`Multiple` threshold.** The owner allowed `Multiple` when the list is no longer visible. P1
   fixes the threshold by measuring the actual row width; the full list remains the default.
5. **Legacy keys.** D15 keeps old keys readable but ignored. Whether a later profile save should
   delete them is a separate cleanup; this plan does not delete user data.

## 7. How to reproduce the measurements

```powershell
# Unit tests for the model and the settings surface
cmake --build --preset win-amd64-release --target rb_blitz_gameplay_remap_tests rb_blitz_input_gates_tests rb_blitz_input_bindings_tests rb_blitz_settings_screen_tests
ctest --test-dir out\build\win-amd64-release -R "gameplay_remap|input_gates|input_bindings|settings_screen"

# Docs check
python .github/skills/documentation/scripts/check_docs.py
```

Live evidence lands under `out/_swap/<label>/` (gitignored): one arm for the main-menu Controls
tree, one for a paused song, one for the countdown, and one that changes a preset and starts a song
to read the controller preset value.
