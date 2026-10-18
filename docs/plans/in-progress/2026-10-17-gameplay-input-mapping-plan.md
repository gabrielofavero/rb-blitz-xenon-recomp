# General and Gameplay Mapping

**Status: in progress, 2026-10-18. P0 is closed for the action seam and P1 has landed (§10, §11);
P2-P6 are not started.** This document records the requested behavior, a source review at
`ca8fa2a`, proposed implementation decisions, and the investigation required before choosing guest
hooks. Configuration and its settings surface exist as of P1; no input is sampled for the gameplay
tables and no gameplay action is dispatched yet, so nothing about a song is affected by the new
switches.

Planning pass 2 (2026-10-17) added the supersession record (§2), the settings-schema and
focus-ring constraints, the build-target registration, the development sequencing guard, the
pad-axis vocabulary gap, and the named documentation/script obligations. It added no runtime
measurements; every new claim is a `[tree]` reading of the file cited beside it.

Planning pass 3 (2026-10-17) split the single Gameplay Mapping switch into two per-input-type
switches, **Controller** and **Keyboard + Mouse**, each owning its own hidable action table and
accepting only its own source kinds. Native-preset bypass and native Controller-category hiding
now follow the Controller switch alone; the Keyboard + Mouse switch leaves the preset and its
category untouched. This pass also added no runtime measurements.

## 1. Requested behavior

Separate two mappings in Controller Settings:

- **General Mapping** is the existing button mapping: one or more physical inputs emulate a
  controller button. It continues to operate the UI. When the matching Gameplay Mapping switch
  is off, it also feeds the game's native controller preset during play.
- **Gameplay Mapping** binds physical inputs directly to gameplay actions. It is split into two
  independent switches, one per input type:
  - **Controller** (default off). When enabled, physical controller input drives the six actions
    directly, the saved native controller preset is bypassed for gameplay (including Pause), and
    the native Controller category in Help & Options is hidden. When disabled, controller input
    feeds the native preset as today and the category stays visible.
  - **Keyboard + Mouse** (default on). When enabled, keyboard and mouse sources drive the six
    actions directly. The native controller preset and its category are preserved and remain
    authoritative for controller input, because the two are different input types. When disabled,
    keyboard and mouse keep their existing General Mapping path.

Each switch owns its own action table. The gameplay rows, in display order within each table, are
**First Note**, **Second Note**, **Move Lane Right**, **Move Lane Left**, **Powerup**, and
**Pause**. Multiple enabled input devices remain usable together; there is no exclusive keyboard,
mouse, or controller mode.

| Requirement | Acceptance condition |
| --- | --- |
| R1. Input switches | Mouse Support and Keyboard Support both default to on. Keyboard Support is new. Turning either off blocks that source throughout the game's menus and gameplay. |
| R2. General Mapping | Keep the current controller-button vocabulary, multiple bindings, table styling, capture, and reset behavior. |
| R3. Gameplay switches | Controller Gameplay Mapping defaults to off; Keyboard + Mouse defaults to on. Each enabled table accepts multiple inputs per action, independently of General Mapping and of the saved native preset. |
| R4. One gameplay authority per input type | Controller on: only the Controller table produces controller gameplay actions; the native preset cannot also act. Controller off: the native preset interprets controller input as today. Keyboard + Mouse on: only its table produces keyboard/mouse gameplay actions, and General Mapping no longer feeds keyboard/mouse input to gameplay. Keyboard + Mouse off: General Mapping feeds the native preset as today. The two input types may act simultaneously. |
| R5. Preserve native preferences | Enabling controller Gameplay Mapping neither changes nor clears the saved controller preset; disabling it restores use of that same preset. The Keyboard + Mouse switch never touches the preset. |
| R6. Native menu visibility | Hide the native Controller/Controls category in Help & Options whenever controller Gameplay Mapping is effective, in both main-menu and paused-game entry paths. With only Keyboard + Mouse enabled, keep the category visible and functional. |
| R7. Disabled sources stay visible | In either gameplay table, strike through each disabled keyboard/mouse binding and render it with the subtitle color. Keep enabled bindings in the same row normal. Do not delete bindings. |
| R8. Disabled tables | Each switch hides its own table when off, retains its bindings, and removes hidden controls from navigation/capture. The section, description, both switches, and help remain visible. |
| R9. Utility isolation | Disabling Keyboard Support or Mouse Support does not disable Controller Settings' own navigation or binding capture. The utility stays operable with any device and can re-enable the source. |
| R10. Vocabulary completeness | P0 proves the six actions cover every gameplay input the native preset produces. Any additional native gameplay path is listed and given an explicit inert-or-bind decision before direct mode ships. |
| R11. Input-type separation | The Controller table accepts only `pad:` sources; the Keyboard + Mouse table accepts only `key:` and `mouse:` sources. A captured or hand-written source of the wrong type is refused and reported, never silently stored or applied. |

Here, “Controller” means the game's preset configuration category (or, in the switch name, the
controller input type); the current merged menu routes the category under the symbol `controls`.
“Gameplay Mapping” is separate from the existing in-game **Gameplay** category for Ultimate
features. No additional modifier-key/chord feature is specified by this request; the two mode
switches and their action bindings are the scope of this plan.

## 2. What the source establishes

These are **[tree] findings from 2026-10-17**, not new runtime measurements. Paths below are the
implementation owners; function names identify the inspected portions.

| Area | Evidence and consequence |
| --- | --- |
| Binding model | [src/profile/remap.h](../../../src/profile/remap.h) and [remap.cpp](../../../src/profile/remap.cpp), `Target`, `DefaultSources`, `Table::Effective`, and `Apply`: 16 targets excluding Guide; `pad:`, `key:`, and `mouse:` sources; absent rows use defaults and explicit empty rows bind nothing. Pad sources read the pre-remap state. Reuse source parsing per table, but give gameplay its own action vocabulary and restrict each gameplay table to its input type (Controller: `pad:` only; Keyboard + Mouse: `key:`/`mouse:` only). |
| Runtime filter | [src/input/remap.cpp](../../../src/input/remap.cpp), `InstallPadRemap`: rewrites merged buttons/triggers through `SetStateFilter`, then merges fixed native-menu mouse pulses and updates packet identity. It has no gameplay-action dispatch or native-preset bypass. Its no-readable-profile branch preserves SDK input rather than applying the table's keyboard defaults. |
| Existing mouse behavior | [src/input/mouse_ui.cpp](../../../src/input/mouse_ui.cpp) defines `mouse_ui_nav=true`; [src/input/native_menu.cpp](../../../src/input/native_menu.cpp), `Context`, distinguishes `game_screen` from its focused `pause_panel`. `RemapContext::SourceDown` currently rejects all raw `mouse:` bindings when Mouse Support is on, including in songs. Conversely, switching it off permits raw mouse bindings. This must change to satisfy R1 and direct mouse gameplay input. |
| Keyboard bypass risk | [SDK input_system.cpp](../../../rexglue-sdk/src/input/input_system.cpp), `CreateDefaultInputSystem`, adds the optional MnK driver. [mnk_input_driver.cpp](../../../rexglue-sdk/src/input/mnk/mnk_input_driver.cpp) has `mnk_mode=false` by default, but when enabled produces keyboard/mouse-derived buttons, triggers, sticks, and keystrokes. A `key:` check in the title's remapper alone cannot disable that path. |
| Connection dependency | SDK `InputSystem::GetState` returns disconnected before calling the filter if no device contributes. The title's `MousePad::EnumerateDevices` only publishes a synthetic device when Mouse Support is on. Keyboard-only play with mouse off and no pad needs a connection independent of native mouse navigation. |
| Settings UI | [controller_tab.cpp](../../../controller-settings/src/controller_tab.cpp), `ControllerTab::Draw`, renders a single button table; `BindingText` flattens all sources to one string. The `?` currently shows a hover tooltip. [shell.cpp](../../../controller-settings/src/shell.cpp) draws schema rows first (`DrawTab`) and the custom mapping block after them. `ControllerTab::kRowCount` is a compile-time count and `Shell`'s constructor sizes `rings_` once (`shell.cpp:191-206`). Consequence: neither gameplay switch can be a schema row and land between the tables, and a hidden table keeps dead focus entries unless the ring is rebuilt. |
| Profile storage | [profile.h](../../../src/profile/profile.h) and [profile.cpp](../../../src/profile/profile.cpp) model `[settings]`, `[remap]`, and `[nav]`, preserving unrelated data. [profile_session.cpp](../../../controller-settings/src/profile_session.cpp), `MergeRows`/`RebaseOntoDisk`, merges concurrent edits by table. New gameplay bindings need full read/write/merge/reset support, not only a parser. |
| Runtime setting ownership | [src/settings/cvars.h](../../../src/settings/cvars.h), `kControllerCvarAllowlist`, currently permits `mouse_ui_nav` and `input_backend`. New switches must enter this allowlist or they can save successfully without reaching a standalone game launch. |
| Native category | [src/ui/settings_screen.cpp](../../../src/ui/settings_screen.cpp), `AllPanels`, routes `controls` to `controller_config_screen`. `PauseRootRows` and `MergePauseListRows` populate the paused game's inline list using `pause_controls`. Hiding only the main-menu row is incomplete. |
| Menu configuration | [settings_screen.h](../../../src/ui/settings_screen.h), `MenuConfiguration`, and [settings_payload.cpp](../../../src/hooks/settings_payload.cpp) already pass boot configuration into menu generation. Carry the effective Controller switch through this seam rather than modifying the user's installed menu files; the Keyboard + Mouse switch does not affect menu visibility. |
| Preset forcing is not a solution | [src/enhancements.cpp](../../../src/enhancements.cpp) declares `enhancements_force_controller_scheme` / `enhancements_controller_scheme`, explicitly marked not implemented. Neither disables native interpretation or provides direct actions. |
| Prior rules this supersedes | [controller-settings/config/settings.toml](../../../controller-settings/config/settings.toml) line 652 says "Keyboard support is always on and is not a setting (R13)", and [docs/plans/finished/2026-09-27-launcher-plan.md](../finished/2026-09-27-launcher-plan.md) R13 binds it. [docs/engine/toggles.md](../../engine/toggles.md) line 72 calls the mouse "hidden and inactive during gameplay"; [docs/reference/known-issues.md](../../reference/known-issues.md) line 23 says clicks and wheel are "disabled during gameplay and restored on pause"; [input-flow.md](../../engine/input-flow.md) says raw mouse sources are suppressed "including during gameplay". This plan supersedes those current-state statements. The finished plan is not edited; the live documents and schema text are the ones to update. |
| Build registration | The game's source list is `RBBLITZ_SOURCES` in [CMakeLists.txt](../../../CMakeLists.txt); [controller-settings/CMakeLists.txt](../../../controller-settings/CMakeLists.txt) compiles `src/profile/profile.cpp`, `profile_path.cpp` and `remap.cpp` directly, and [installer/CMakeLists.txt](../../../installer/CMakeLists.txt) compiles `profile.cpp`/`profile_path.cpp` too. A new `src/profile/gameplay_remap.*` must be registered in the game and Controller Settings targets; keep `Profile` a storage-only row table so the installer's copy needs no new dependency. |
| In-game settings spec | [docs/settings/plan.md](../../settings/plan.md) §3.2 and its row-position statement say the Controls page keeps its inventory and "nothing about them changes". Conditional removal contradicts that when controller Gameplay Mapping is on; the owning spec, not only `menu.md`, must record the new rule. |

The existing mouse timing and packet-delivery constraints are documented in
[input-flow.md](../../engine/input-flow.md). Preserve them. A successful UI-only mapping test is
not evidence that note timing, native gameplay suppression, or Pause are correct.

## 3. Implementation decisions

The decisions below are the proposed implementation contract. P0 resolves the guest ABI and
action semantics before P3 is implemented; it must not replace them with guessed addresses.

### D1. Three independent destinations, one physical source layer

Reuse the existing source grammar and source labels. Introduce an SDK-free gameplay action enum
with stable serialized names:

| Label | Stored action id |
| --- | --- |
| First Note | `first_note` |
| Second Note | `second_note` |
| Move Lane Right | `move_lane_right` |
| Move Lane Left | `move_lane_left` |
| Powerup | `powerup` |
| Pause | `pause` |

General Mapping continues to use `[remap]`. Gameplay actions live in two sibling tables whose
source vocabularies are disjoint:

| Table | Switch | Accepts | Rejects |
| --- | --- | --- | --- |
| `[gameplay_remap_controller]` | Controller | `pad:` targets | `key:`, `mouse:` |
| `[gameplay_remap_keyboard_mouse]` | Keyboard + Mouse | `key:`, `mouse:` | `pad:` |

Both tables use the same six action row names. Never reinterpret an existing button target as an
action or feed gameplay from the output of General Mapping. For example, changing `a = "key:K"`
must not change First Note in either table. The two tables may use the same physical input only
in the sense that a key and a pad control are different devices; a source of the wrong type is
refused by its table (R11).

Within one gameplay table, one source may also name two actions. Allow it: both actions fire,
matching General Mapping's OR semantics, and the table shows a non-blocking note listing the
shared actions. Do not silently drop the second binding. If P0 finds an action pair that must not
co-fire, enforce that at the native consumption boundary rather than rejecting the user's binding.
The same physical input in General Mapping and in a gameplay table is not a conflict because the
contexts differ.

The current pad vocabulary is the 16 `Target` names; it has no directional stick source such as
`pad:lstick_left`. P0 must state whether lane movement or a note action needs an analog direction.
If it does, extend `Source` with stable directional stick names, pin one threshold constant, and
add capture for it; if it does not, state that only the listed discrete pad controls are bindable
and refuse an axis name rather than guessing. Do not silently accept a vocabulary the game cannot
apply.

Read physical pad state before remapping or synthetic input merging. A `pad:` source must not
become active because an SDK keyboard binding synthesized the same pad bit, and a `key:`/`mouse:`
source must not become active because General Mapping or the native preset synthesized a pad
button. Neither General Mapping's pad remaps nor its keyboard emulation may transform a physical
control into a different `pad:` source for the Controller table. Keyboard and mouse gameplay
sources are sampled from raw device state and events, never from the remapped pad state. Preserve
simultaneous pad/keyboard/mouse use and existing player-slot assignment. Host keyboard/mouse
actions belong to the active single-player input slot; do not inject them into every polled guest
slot.

### D2. Source gates precede all interpretation

Use `keyboard_support=true` and retain `mouse_ui_nav=true` as the compatible stored mouse switch,
with **Mouse Support** as its label. Expand that switch to gate every mouse-to-game path. Avoid
introducing a second user-facing mouse toggle that disagrees with it.

The two gameplay switches are routing selectors, not device gates. **Keyboard + Mouse** off keeps
keyboard and mouse usable for UI navigation, General Mapping, and Controller Settings; it stops
only the direct gameplay actions. **Controller** off keeps the pad available to the native menu,
the native preset, and Controller Settings; it stops only the direct controller actions. Turning
a gameplay switch off must never disable a device for the utility or the UI.

Both gates are runtime cvars read at poll time, like `mouse_ui_nav` in
[native_menu.cpp](../../../src/input/native_menu.cpp), not profile booleans read once by one
consumer. The profile feeds them through the controller allowlist, so config/env/CLI precedence
still wins and the input filter, the menu generator, and the Controller Settings preview observe
one effective value. Log the effective switch values and their sources at boot.

The source gates apply to default bindings, explicit bindings, direct Keyboard + Mouse gameplay
actions, native menu pointer input, legacy SDK emulation, and guest-facing text/keystroke paths if
the title uses them. The gameplay switches apply only to their own direct action table, with the
Controller switch additionally selecting the native-preset bypass and category visibility.
Audit in-process game-control hotkeys too; disabling keys must not leave a second keyboard route
to Pause or a gameplay action. OS shortcuts and input to the separate Controller Settings utility
remain available so the user can edit and re-enable support.

Do not clear a disabled source's corresponding button bits after devices have merged: that would
also erase an enabled controller pressing the same button. Prefer title-owned physical-source
sampling and exclusion of SDK MnK synthesis from this title's input factory. If a narrow SDK seam
is necessary to preserve provenance, deliver it through the existing patch workflow under
[patches/](../../../patches/README.md), with SDL and XInput coverage. Do not silently allow
`mnk_mode`/`keybind_*` to bypass the gates; explicitly define and log their compatibility handling.

Generalize the synthetic connection currently supplied by `MousePad`: keyboard-only and
mouse-only configurations must remain connected without a physical pad. A synthetic connection
contributes no unrequested gameplay actions and must not displace the physical controller's slot
or change rumble ownership.

### D3. Context decides which mapping can act

| Controller switch | Keyboard + Mouse switch | Context | Action source |
| --- | --- | --- | --- |
| Off | Off | Menus, pause menus, and dialogs | General Mapping plus the existing native pointer navigation when mouse support is enabled. |
| Off | Off | Playing a song | General Mapping -> native saved controller preset, as today. No direct actions. |
| Off | On | Menus, pause menus, and dialogs | General Mapping plus native pointer navigation. Both gameplay tables are inactive. |
| Off | On | Playing a song | Controller input -> native saved preset. Keyboard/mouse -> Keyboard + Mouse table. General Mapping no longer feeds keyboard/mouse input to gameplay. |
| On | Off | Menus, pause menus, and dialogs | General Mapping plus native pointer navigation. Both gameplay tables are inactive. |
| On | Off | Playing a song | Controller -> Controller table; the native preset is bypassed. Keyboard/mouse keep only their General Mapping path, which cannot reach gameplay while the Keyboard + Mouse switch is off. |
| On | On | Playing a song | Controller -> Controller table; keyboard/mouse -> Keyboard + Mouse table. The native preset is bypassed. |
| Either | Either | Unfocused window, input captured by a host modal, or a transition without an input destination | Release/reset the affected input state; discard queued actions that belong to the previous context. |

Use an authoritative guest gameplay state and ownership of the focused UI, not a test for
`game_screen` alone. Pause menus, nested settings, loading/start prompts, countdowns, tutorials,
and end-of-song screens must each be classified in P0. Keep context observation independent of
Mouse Support: turning the mouse off must not freeze gameplay routing.

With mouse support on, keep native menu hover/left-click/right-click/wheel behavior. Those gestures
have one owner in menu contexts and cannot also activate a custom general binding there. During
active gameplay, mouse buttons are eligible for the Keyboard + Mouse table when that switch is on
(and for General Mapping when it is off); cursor navigation and queued menu clicks are inactive.
Document this context rule in help. With Mouse Support off, both native navigation and bound mouse
buttons are inert.

Keep the OS cursor hidden during active gameplay: [mouse_ui.cpp](../../../src/input/mouse_ui.cpp)
shows it only when a native menu is available or ImGui wants pointer capture. A gameplay `mouse:`
source is sampled from SDL button state, not from the native-menu queue, so the click cannot hover,
press, or scroll a menu on its way to the action.

Pause is a gameplay action while playing. Once paused, General Mapping handles Resume, Back,
and navigation. A held pause input must not immediately resume or select an item after that
context change. Require release before rearming a source that crosses such a boundary.

### D4. Replace the native input producer, preserve the gameplay consumer

Find the point where the selected native preset translates controller input into the six actions.
When the Controller switch is enabled, bypass that translation and supply the Controller table's
action state or events to the same validated downstream action handlers. Keyboard and mouse
actions enter at that same validated action boundary without altering the controller path, so the
Keyboard + Mouse switch composes with (rather than replaces) the native preset. Keep the engine's
note judgement, calibration, lane animation, powerup eligibility, and pause lifecycle intact.

The bypass must cover digital buttons, analog sticks/triggers, held input, repeat paths, and any
separate Start/Pause handler. Disabling only one button-down listener or clearing only button
bits is insufficient. Do not force a “neutral” preset or synthesize a fixed preset's buttons as the
permanent architecture: either approach still couples the custom scheme to native interpretation.

Suppression is scoped to gameplay consumers. Do not blank all controller state globally, because
menus, system connectivity, calibration, and paused UI still need input. Controller switch off
must call the original path without injecting direct controller actions. Keyboard + Mouse switch
off must not inject keyboard/mouse actions but must leave the native preset path untouched. When
the Controller switch is on, General Mapping's synthesized pad bits must not be mistaken for
physical pad input and must not reach either gameplay table. Never write the saved preset field as
part of enable/disable or hide the native category as a substitute for disabling its behavior.

Dispatch guest actions on the appropriate guest execution thread using the established hook/call
conventions. Window callbacks only publish input. Do not call guest routines from SDL events or
assume the SDK input poll and UI update share a thread.

### D5. Preserve action timing and multiple-source semantics

Use one synchronized source snapshot/event stream for every mapping evaluator. Define ownership
and consumption so polling the guest state twice cannot dispatch an action twice, and a short
press/release between render frames cannot disappear. Retain ordered edges needed by the native
action consumer; do not implement gameplay through the menu mouse's multi-frame button pulses.

P0 must determine, per action, whether the game consumes a press edge, held state, release, or
native repeat. The action evaluator then follows that contract:

- Within each table, action bindings keep the existing OR behavior across that table's enabled
  sources. The two tables are unioned at the action boundary.
- Held action state stays down until every contributing source releases, including a pad source
  and a key source bound to the same action. Disabling one source or one table cannot release an
  action still held by another enabled source.
- Strike actions must preserve intentional repeated presses from different bindings, including
  alternating keyboard/controller presses across the two tables. Do not lose them by applying
  one aggregate held bit if the native consumer requires individual strike edges.
- Simultaneous bindings for the same action must not accidentally double-trigger it, including
  one pad binding and one key binding pressed together. Establish deterministic coalescing at
  the native consumption boundary without merging distinct rapid taps; verify this against the
  measured consumer.
- First Note and Second Note can act together. For simultaneous opposite lane directions, define
  and test a deterministic policy after measuring native behavior; proposed fallback is neutral
  while both are held, with no replay of stale movement on release.

Clear pending gameplay edges and held/repeat state on focus loss, disconnection, context changes,
and shutdown. No key repeat, stalled frame, or reconnect may leave a stuck action or replay a
burst. Keep packet identity based on the final delivered general pad state; add an independent
action sequence/consumption identity if the chosen guest hook requires it.

### D6. Configuration, defaults, and migration

Store `keyboard_support`, `gameplay_mapping_controller`, and
`gameplay_mapping_keyboard_mouse` in the Controller Settings profile's `[settings]` table.
Compiled defaults: `keyboard_support = true`, `gameplay_mapping_controller = false`,
`gameplay_mapping_keyboard_mouse = true`. Keep `mouse_ui_nav` there too. Add the new switches to
the runtime controller allowlist and apply the existing config/env/CLI precedence. The gameplay
resolvers and menu generator must use the **same effective switch values**, including overrides.

Proposed profile shape; bindings here illustrate serialization and per-table source kinds, not
finalized defaults:

```toml
[settings]
mouse_ui_nav = true
keyboard_support = true
gameplay_mapping_controller = false
gameplay_mapping_keyboard_mouse = true

[remap]
a = "pad:a, key:K"

[gameplay_remap_controller]
first_note = "pad:left_trigger, pad:a"
second_note = "pad:right_trigger, pad:b"
move_lane_right = "pad:right_shoulder, pad:dpad_right"
move_lane_left = "pad:left_shoulder, pad:dpad_left"
powerup = "pad:x"
pause = "pad:start"

[gameplay_remap_keyboard_mouse]
first_note = "key:J"
second_note = "key:K"
move_lane_right = "key:D"
move_lane_left = "key:A"
powerup = "key:Space"
pause = "key:Return"
```

Missing action rows use one compiled default table per gameplay table; explicit empty strings mean
unbound. The Keyboard + Mouse table ships enabled, so its keyboard defaults must be complete
before the default flips to on. The Controller table ships disabled but still needs a complete pad
scheme so enabling it is useful without first editing the table. P0/P1 must publish and test the
actual defaults; the user has not specified those keys. Prefer a recognizable native layout for
controller defaults, translated once into explicit action sources, and an ergonomic keyboard
scheme. Do not dynamically follow the saved native preset while the Controller switch is enabled.
Mouse bindings in the Keyboard + Mouse table should be opt-in (a click in gameplay should not
strike a note by default) even though Mouse Support itself defaults to on.

Old profiles retain their existing `[remap]` and explicit Mouse Support value. Missing
`keyboard_support` and `gameplay_mapping_keyboard_mouse` resolve to on; missing
`gameplay_mapping_controller` resolves to off, which preserves the native preset for existing
players. Enabling a feature is not contingent on first opening Controller Settings. Explain in
help that General Mapping customizations continue to control menus, while each enabled gameplay
table supplies its own scheme for its input type; turning the Controller switch off restores use
of the saved preset, and turning the Keyboard + Mouse switch off returns keyboard/mouse input to
General Mapping. The old behavior in which Mouse Support off allowed raw mouse bindings is
intentionally superseded by the new global source gate.

The UI ownership follows the schema's draw order: **Mouse Support** and **Keyboard Support** are
rows in the existing `Input` group, while the **Controller** and **Keyboard + Mouse** switches are
drawn by the custom block. [shell.cpp](../../../controller-settings/src/shell.cpp) draws schema
rows first (`DrawTab`) and the block after them, so a schema row for either switch would land
above the General table. P4 either keeps that split or adds ordered group interleaving; it must
not leave the switches at the top.

The schema's own note at line 652 ("Keyboard support is always on and is not a setting (R13)") is
superseded here, as is the finished launcher plan's R13. Rewrite the note and the `mouse_ui_nav`
tooltip when P4 lands; do not edit the finished plan. The unbuilt `Keyboard` group's note ("Keyboard
mapping is not editable in this build") stays true: it is about General Mapping, not this
Keyboard + Mouse gameplay table; P4 should keep the two distinct in help.

Sequencing guard: until P3's controller bypass passes acceptance, force the effective Controller
switch off (its compiled default is already off) so no build hides the native Controller category
before direct actions work. Until P3's keyboard/mouse dispatch passes acceptance, keep the
effective Keyboard + Mouse switch off in development builds even though its compiled default is
on. Shipping defaults are Controller off and Keyboard + Mouse on once acceptance passes.

Implement both `[gameplay_remap_controller]` and `[gameplay_remap_keyboard_mouse]` across parsing,
surgical serialization, fresh-file serialization, dirty comparison, rebased saves, profile-wide
reset, and table-scoped resets. `Profile` gains one row vector per table (the profile parser
matches section names literally). Preserve unknown tables/rows and malformed or future binding
syntax without silently converting it into valid actions. A syntactically valid source of the
wrong kind for its table is an invalid row for that table: report it, keep it verbatim for a
round trip, and do not apply it. Distinguish invalid input from a deliberately empty binding.
The reader and UI must agree on the fallback used for a rejected row. Additive tables should not
require a profile version bump if older readers preserve them; verify that rather than assuming.

Use compiled input defaults when no profile exists, without creating a profile merely to play.
Handle an unreadable profile consistently with the existing non-fatal boot policy and diagnostics.
Do not silently disable controller Gameplay Mapping for missing data while still hiding the native
category.

### D7. Apply on the next game launch; preview edits immediately

Proposed first implementation: keep input configuration launch-scoped, matching the current
Mouse Support row and the remapper's one-time profile read. Save persists changes; Play Game
saves and launches with them. A game already running keeps its current configuration until restart,
including the native category's visibility. State this in the settings help/status rather than
implying an immediate runtime change.

Within Controller Settings, edited switch values immediately update strikethroughs, per-table
visibility, and focus layout. The currently running game uses one immutable effective configuration
for routing and menu visibility. No profile watcher or mid-song reconfiguration is required for
this first implementation. If live application is added later, update both consumers atomically
at a safe boundary and flush input before switching.

## 4. Controller Settings presentation

Render exactly these sections in this order:

```text
Input
  Mouse Support                         [On]
  Keyboard Support                      [On]

General Mapping
  Map inputs to controller buttons. When a gameplay mapping is on,
  these bindings control menus only.     [?]
  [existing controller-button table]

Gameplay Mapping
  Bind inputs directly to gameplay actions. Each table accepts only
  its own input type.                    [?]
  Controller                            [Off]
  [six-action table, pad sources only, visible only when Controller is on]

  Keyboard + Mouse                      [On]
  [six-action table, key:/mouse: sources only, visible only when Keyboard + Mouse is on]
```

Both `?` controls open a persistent explanation on click or keyboard/controller activation;
hover may also preview it. Add them to the focus model and support Back/Escape to close without
editing bindings. The explanations cover:

| Help | Content |
| --- | --- |
| General Mapping | Inputs emulate controller buttons; several sources may be bound; while a gameplay mapping switch is on, these bindings control menus only; with the matching switch off they also feed the saved native preset during play; native pointer gestures own menu mouse interaction; Assign/Reset/Save behavior. |
| Gameplay Mapping | Two independent switches. Controller: pad input triggers the six actions directly, native presets are inactive, and their category is hidden while it is on; the saved preset is retained and restored when it is off. Keyboard + Mouse: key/mouse input triggers the six actions directly while the native preset remains authoritative for the controller. Each table accepts only its own input type; disabled device bindings are kept and crossed; Pause changes context to General Mapping; changes take effect on next game launch. |

Extract a shared table/binding renderer instead of copying `ControllerTab::Draw`; instantiate it
once per gameplay table with a source-kind filter. Keep the three-column arrangement, wrapping,
Assign, Reset, and a table-scoped Reset all. Use unique ImGui IDs across the three tables and one
capture session across all of them, owned by the table that started it: the capture accepts only
that table's source kinds and ignores a wrong-type press with a visible hint, staying armed until
a valid source or cancel. Capture adds a source to the effective existing list, deduplicates it,
and does not steal the activation click/press. Check the current `CommitCapture` use of
`Bindings()` versus `Effective()` so first assignment does not unexpectedly drop displayed
defaults.

Both `BindingText` and `ImGui::TextUnformatted` flatten a row into one string; replace them with a
span renderer that measures every source and draws the strike per wrapped line. There is no strike
helper in `row_ui`/`schema_view` today, so add one and unit-test its width and line arithmetic. The
current `?` is a `SmallButton` plus an `IsItemHovered` tooltip outside the focus ring; the
persistent help needs real ring entries and a modal owner in the shell.

Render bindings as individually styled spans with measured text widths and strike lines, including
wrapped lines. In the Keyboard + Mouse table, use `ImGuiCol_TextDisabled`, the subtitle color, for
a source gated off by Keyboard Support or Mouse Support. The Controller table has no device gate,
so its rows render normally. Separators and enabled sources must remain readable. A multi-source
row becomes normal enabled text plus a crossed disabled source, not one wholly disabled string. A
short tooltip can name the disabled source switch. Binding presence and input availability are
separate facts; keep editing/capture available in the utility even when that source is disabled
for the game. Each table refuses the other type's sources at capture and at parse time (a
hand-written `pad:` row in the Keyboard + Mouse table is reported, kept verbatim, and not
applied).

Build the focus entries and draw order from one visible layout model. Hiding either gameplay
table must cancel capture in that table, move focus to its switch if necessary, and preserve
bottom-bar navigation. The switch that hides a table stays focusable while the table is hidden.
Add help-modal ownership to the shell's existing modal/listening checks. Test small windows, long
binding lists, both app surfaces, and all three navigation devices.

`ControllerTab::kRowCount` is a compile-time count and `Shell` sizes `rings_` once in its
constructor (`shell.cpp:191-206`), so a hidden table would keep dead focus entries. Replace that
with a runtime row count and a `Shell::RebuildRings()` (or equivalent) called on either switch's
change, Reset/import, and profile reload; move focus to the switch before removing the row it
occupied. `Escape` closes help before it closes the window, through the existing modal/listening
checks. Keyboard Support off affects only the game runtime: Controller Settings' own `[nav]`
keys and capture stay usable, and both gameplay tables stay editable.

## 5. Hide the native controller category

Add the effective **Controller** switch value to `MenuConfiguration`, supplied before menu
records are built. Filter the `controls` route from the merged root and `pause_controls` from the
paused inline list using that value alone. Apply it to both provider-active and base lists, and to
generated in-song counterparts. The **Keyboard + Mouse** switch never changes menu visibility:
the native preset still owns controller input in that mode, so its category stays visible and
functional. Keep the pause panel's existing focus-based Help & Options route; the historical
reasons are recorded in [settings/menu.md](../../settings/menu.md), part 1.

This is an automatic ownership rule, independent of Hide Items and Ultimate availability. Do not
remove the controller configuration assets, mutate installed archives, or change the saved preset.
Turning the Controller switch off regenerates the original category in its original order. Update
help observations/navigation assumptions that depend on fixed row indices, and test reopening the
list, Back, selection clamping, and returning to gameplay. If an alternate retail/provider route
can still expose controller presets, cover it in the same visibility policy.

`docs/settings/plan.md` §3.2 and its row-position statement ("Controls at index 1; nothing about
them changes") describe the always-visible case and must be updated with this rule. The route-driven
scripts that assume fixed indices (`New-UiHelpRoute 1|2|3`) keep their calibration and audio/video
targets, but a run with the Controller switch on must not assume the Controls row exists.

## 6. Work packages and dependencies

| Package | Work and likely owners | Completion evidence |
| --- | --- | --- |
| P0. Prove the gameplay seam | Trace native presets and all six consumers from local guest code/assets. Record address, calling convention, owning object/thread, context, timing, and press/hold/release semantics. Exercise buttons, triggers, sticks, and Start through both source types. Check base and Ultimate, tutorial gameplay, and Pause separately. Prove the six-action vocabulary is complete and list every extra input path (tutorial skip, calibration confirm, song end) with an inert-or-bind decision. Prove that direct keyboard/mouse actions can be merged while the native controller preset remains active (the shipping Keyboard + Mouse default), and that a bypassed native preset cannot be reached by synthesized pad bits. Decide whether an analog stick direction is needed as a bindable `pad:` source. Owners: `src/input/`, prospective `src/hooks/gameplay_input.*`, `config/functions.toml` only for justified symbol names, `docs/reference/symbols.md` for newly named symbols. | A per-action evidence table including vocabulary completeness, before/after traces proving a candidate bypass suppresses every native gameplay producer, a keyboard/mouse-only trace proving the native controller path still works beside it, and a prototype direct action that preserves native judgement. Record unresolved paths explicitly; P3 cannot ship on an unproven bypass or an unproven coexistence path. |
| P1. Add shared configuration | Add the two gameplay action tables/parsers and per-table defaults, the two switches with their allowlist entries, profile round trips and rebased save/reset, wrong-type row rejection, diagnostics, and old-profile behavior. Likely new `src/profile/gameplay_remap.*`; extend `src/profile/profile.*` (one row vector per table), `src/settings/cvars.h`, `src/profile/profile_apply.*`, `controller-settings/src/profile_session.*`. Register the new source in `RBBLITZ_SOURCES` and `controller-settings/CMakeLists.txt`; keep `Profile` storage-only so the installer's `profile.cpp` copy needs no new file. Add `keyboard_support` as a schema row in the `Input` group beside the existing Mouse Support row (`argv = "flag"`), draw both gameplay switches in the custom block, rewrite the R13 note, and log the effective switches with the binding count per table. | SDK-free tests for absent/empty/invalid/unknown rows, wrong-kind sources per table, multi-source bindings, independence of the three tables, defaults, explicit false values, concurrent saves, and a no-profile boot configuration. Freeze actual default bindings after P0. A standalone boot's log names the effective switches and each table's binding count. |
| P2. Unify physical input and gates | Share physical snapshots/edges, focus and capture policy, player ownership, and synthetic connectivity; classify every event by source kind so each gameplay table sees only its own; isolate legacy MnK. Extend `src/input/remap.*`, `mouse_ui.*`, `native_menu.*`, and input-factory wiring in `src/rb_blitz_app.h`. Add an SDK patch only if needed; `patches/README.md` gains its row when one lands. | Keyboard-off and mouse-off tests across all producers, mixed device input, no-controller startup, SDL/XInput, final packet identity, source provenance, wrong-kind exclusion per table, no stale input after boundaries, the OS cursor stays hidden in gameplay, and a gameplay mouse press never reaches the native-menu queue. |
| P3. Dispatch direct gameplay actions | Implement the P0-proven guest adapter, the controller native bypass (only when the Controller switch is on), the keyboard/mouse action injection that composes with the native preset, action-specific edge/hold behavior, pause routing, and context lifecycle. Keep each switch's off path on the original behavior. Integrate with `CMakeLists.txt`; do not edit generated recompiler output. | All six actions work through both tables with arbitrary per-type bindings; the controller table never leaks through a conflicting native preset while enabled; the Keyboard + Mouse table coexists with the native controller preset; simultaneous sources and rapid notes behave correctly; saved preset remains unchanged. |
| P4. Build the settings surface | Add the Keyboard Support schema row and the ordered sections with two per-table switches; share rendering/capture with per-table source filters; add clickable help; strike gated sources; hide each table independently; rebuild focus layout. Owners: `controller-settings/config/settings.toml`, `controller_tab.*`, `shell.*`, `schema_view.*`, `row_ui.*`, and shared UI helpers as needed. Replace the one-time ring sizing with a runtime layout, add the help modal to the shell's focus model, and update `--dump-layout` plus the capture/acceptance scripts that assert old rows. | Screenshots and navigation traces for all four switch combinations and all device-support combinations; wrong-type capture is refused visibly; capture/reset/Save/Play persists exactly what runtime reads; hidden rows are never focusable; the utility stays navigable with a source disabled. |
| P5. Apply native category visibility | Pass the effective Controller switch into menu generation; filter both `controls` and `pause_controls` on that value, keeping provider gating independent and leaving the category visible when only Keyboard + Mouse is on. Owners: `src/ui/settings_screen.*`, `src/hooks/settings_payload.cpp`, `docs/settings/plan.md` §3.2, relevant help observers/tests. | Generated menu-tree tests and live main-menu/pause screenshots across the Controller switch on/off for base and Ultimate, plus a Keyboard + Mouse-only arm proving the category is preserved; preset value and Back routes preserved; the row-position spec and its scripts agree with the conditional list. |
| P6. Integrated acceptance and docs | Build game and utility, run targeted suites and live song checks, verify launch/restart/profile behavior and installed payload, then update owning docs. | Acceptance matrix below passes with evidence under `out/gameplay-mapping/`. Update, as behavior lands: `docs/engine/input-flow.md` (raw-mouse paragraph, keyboard line, per-device gameplay-table rules, a new live-acceptance section), `docs/engine/toggles.md` (`mouse_ui_nav` row: no longer "hidden and inactive during gameplay"; add the gameplay switches and `keyboard_support` if they join the toggle registry), `docs/reference/known-issues.md` (R6 row), `docs/settings/contracts.md` §8 (allowlist), `docs/settings/plan.md` §3.2 and the row-position rule, `docs/settings/menu.md` and `docs/settings/status.md`, `docs/reference/symbols.md` (P0 names), `docs/README.md` (index), `controller-settings/README.md`, and `patches/README.md` if a patch lands. |

Order: **P0 -> P1 -> P2 -> P3**, with **P4 after P1** and **P5 after the shared switch contract in
P1**. P6 requires all packages. The main uncertainty is P0/P3; do not describe this as only adding
two mapping tables. P5 must not be effective while P3 is unproven: the Controller switch is forced
off until then, so a build cannot hide the native category without a working replacement. During
development, the Keyboard + Mouse switch is forced off until its dispatch is proven. The shipping
defaults (Controller off, Keyboard + Mouse on) wait until all six actions, per-type exclusive
routing, the native-preset coexistence path, and both menu entry paths pass acceptance together.

P0 status (2026-10-18): **closed for the action seam, with named residuals** — §10.6 measures live
native presses for all six actions, the full four-producer bypass with before/after pairs, a
direct-dispatch judgement proof, the track repeat's owner and stop path, and the Pause/song-end/
Ultimate/no-pad/tutorial-menu/calibration-menu contexts. Guest keystrokes are closed statically (the
title imports no keystroke API). Four items stay open and belong to later packages: per-action edge/hold
semantics (O2, P3), the tutorial's and calibration's interactive segments (O4, P3), raw SDL source
provenance (O5, P2), and a direct pause (O10, P3). Do not claim those. **P1 can start.**
P1 status (2026-10-18): **landed, and P2 can start** — §11 records the two tables, the frozen
defaults, the stick-direction vocabulary, the profile storage and reset/merge rules, the three
switches and their allowlist, the settings surface's first half, and the boot log that names them.
P1 added no consumer: the switches gate nothing and dispatch nothing until P2 (source gates) and P3
(direct dispatch), so the sequencing guard above stays in force — a build must not act on either
gameplay switch before its acceptance passes.
## 7. Validation and release criteria

### 7.1 Automated checks

Extend meaningful existing tests for `controller_settings_profile`, `controller_settings_session`,
`controller_settings_remap`, `controller_settings_nav`, `settings_service`, `settings_screen`,
`menu_payload`, and `native_mouse`. Add a focused pure gameplay evaluator/source-gating suite
for the new semantics. Register it in CMake. Include:

- Per-table sources: a source of the wrong kind is refused at parse and capture; a pad control
  and a key bound to the same action fire per the context table; one source on two actions in
  one table fires both and is reported; simultaneous note strikes, rapid alternating sources,
  opposite lanes, and repeated guest polls.
- Keyboard/mouse gates on defaults and explicit bindings; physical pad input survives a disabled
  keyboard mapped to the same button; legacy MnK cannot impersonate `pad:` and cannot reach the
  Controller table.
- A General Mapping remap cannot affect either direct table. A native preset cannot fire any of
  the six actions while the Controller switch is on; an unused physical stick cannot fire them
  in either switch state. Each switch's off state never calls its direct dispatcher.
- Default configuration: fresh profile, no pad, Keyboard + Mouse on and Controller off; keyboard
  actions work while the native controller path and category remain untouched.
- Focus loss, host capture, disconnection, and pause/resume clear pending events and require
  appropriate rearming. UI acceptance cannot leak into the first gameplay frame or vice versa.
- Profile defaults, explicit empty bindings, unknown/malformed rows, wrong-table rows, old
  profiles, no profile, concurrent edits, table-scoped resets, and ordinary direct launch without
  the utility.
- Utility isolation: with keyboard or mouse support off, Controller Settings' own keys and capture
  still work, and Save persists the rows. Both tables stay editable with either gameplay switch
  off.
- Layout/focus: hidden rows are never focusable; toggling either switch rebuilds the ring; the
  help modal owns Escape; the bottom bar stays reachable.
- Sharing policy: one source on two actions in one table fires both and is reported; the same
  physical key in General Mapping and a gameplay table follows the context table.
- Cursor/pointer: a gameplay mouse press does not enter the native-menu queue and the cursor stays
  hidden in gameplay.
- Schema/launch: `--dump-layout` names the new rows, both switches round-trip through the profile,
  and a standalone boot receives them through the allowlist.
- Main and pause category lists across controller-switch state x provider state, including a
  Keyboard + Mouse-only arm where the category must remain; valid Back and selection behavior.
  Profile/CLI override tests prove menu visibility and input use one switch value.

Run the configured build and targeted CTest suites using
[build-and-run.md](../../guides/build-and-run.md). Confirm the chosen build tree actually registers
tests with `ctest --test-dir <build-dir> -N`; [input-flow.md](../../engine/input-flow.md) records a
Vulkan-enabled configure that disabled `BUILD_TESTING`. P6 also requires real gameplay; passing
pure evaluator or generated-menu tests alone cannot validate the guest hooks.

### 7.2 Live acceptance matrix

| Scenario | Required observation |
| --- | --- |
| Fresh profile, no physical pad | Keyboard + Mouse defaults are on and Controller defaults off, so keyboard can boot, navigate, play all six actions, pause, resume, and exit without opening Controller Settings; the native controller category remains visible. |
| Controller mapping off with a pad | The saved native preset plays, exactly as before this change; the Controller table is inert and its bindings are retained. |
| Controller mapping on | Custom controller bindings deliberately conflict with each saved native preset in turn; only the custom actions occur, including with sticks/triggers/Start; the native category is hidden; turning the switch off restores the same saved preset and category. |
| Keyboard + Mouse only on | Keyboard/mouse direct actions work while the pad still plays through the native preset; the native category stays visible and editable; saved preset remains unchanged. |
| Keyboard + Mouse only off | Keyboard/mouse return to General Mapping and the native preset path; the Keyboard + Mouse table is inert and retained. |
| Both gameplay switches off | Native controller play and General Mapping behave as before; no direct action is possible. |
| Wrong-type binding | A hand-written `pad:` row in the Keyboard + Mouse table (and a `key:` row in the Controller table) is reported, preserved verbatim, and not applied; capture in either table ignores the wrong device and stays armed. |
| Mouse off, keyboard on, no pad | Keyboard remains connected and usable; all mouse buttons, motion, and wheel have no game effect, including mouse rows in the Keyboard + Mouse table. |
| Keyboard off, mouse/pad on | Keys cannot navigate, strike, move lanes, trigger powerup, or pause; equivalent physical pad inputs and mouse gameplay actions still work. |
| Both keyboard and mouse off | Controller gameplay and UI remain usable; every keyboard/mouse binding is retained and crossed in the Keyboard + Mouse table. |
| Mixed inputs | Pad, keyboard, and mouse can perform different actions together, and alternative sources for one action obey the measured edge/hold rule without duplicates or stuck releases. |
| Pause boundary | A custom Pause binding from either table pauses once. General Mapping controls the menu. Holding/clicking across Pause/Resume does not fire a note, powerup, menu confirm, or second pause. |
| Menu visibility | Native Controller/Controls is absent in main-menu and paused Help & Options when the Controller switch is on, present in both when it is off, on base and Ultimate; a Keyboard + Mouse-only arm keeps it present. Other categories and Back still work. |
| Utility editing | Three tabs/sections, both help buttons, two switches, mixed crossed/normal bindings, independent table hide/show, per-table capture with wrong-type refusal, reset isolation, wrapped text, keyboard/controller focus, and Save/Play all behave as specified. |
| Utility with a source disabled | Controller Settings navigates and captures with Keyboard Support off (a separate process); Save persists; the game ignores those keys. |
| Mouse in gameplay | With support and the Keyboard + Mouse switch on, the cursor stays hidden, a bound mouse button performs its action without moving a menu selection, and native hover returns on pause. |
| Layout and help | Hiding/showing either table while focused, opening/closing both help modals, and `--dump-layout` row counts all behave; no hidden row receives focus; each switch stays reachable. |
| Runtime boundaries | Alt-tab, modal capture, controller unplug/replug, loading, song end, and repeated songs leave no held inputs or stale queued actions. |
| Save and launch | Explicit false values and custom rows in both tables survive restart; direct launch and utility launch agree; existing General Mapping and native save data remain intact. |
| Timing | Compare the native preset and each direct table using the same song/calibration and reproducible input sequence; log sampling/dispatch timing and note outcomes. No added frame wait, lost short taps, extra repeats, or altered calibration. |

Use [drive_ui.ps1](../../../scripts/drive_ui.ps1),
[capture_controller_settings.ps1](../../../scripts/capture_controller_settings.ps1), and
[acceptance_controller_settings.ps1](../../../scripts/acceptance_controller_settings.ps1) where
they fit. Extend tooling for the new mapping/focus rows and action traces rather than relying
on old menu indices. Keep source events, guest consumption traces, screenshots, configuration,
game variant, preset, and build revision beside each live result; use an isolated test profile/save.

## 8. Open investigation items

| Id | Unproven item | Required resolution |
| --- | --- | --- |
| O1 | Exact action consumers and full native-preset bypass | P0 supplies concrete ABI and live evidence for every action and alternate analog/pause path. No implementation address is assumed here. |
| O2 | Meaning and event semantics of First/Second Note | Confirm their relationship to the two note positions, press/hold/release needs, simultaneous strikes, and alternating source behavior before freezing the action evaluator. |
| O3 | Initial action bindings | P0/P1 publishes complete defaults for both tables: a pad scheme for the Controller table and a keyboard scheme for the Keyboard + Mouse table (mouse bindings opt-in). The sample TOML is not a default recommendation or evidence of a native preset. |
| O4 | Context coverage | Classify tutorials, countdown/loading prompts, nested pause screens, calibration, song end, and Ultimate behavior from guest state; confirm no UI-only route can trigger gameplay. |
| O5 | Source provenance and backend compatibility | P2 proves how physical pad state stays separate from SDK MnK and fixed menu pulses on SDL/XInput, with keyboard-only connectivity. |
| O6 | Guest keystrokes and alternate controls | Trace whether any gameplay/UI consumer uses `GetKeystroke`, direct keyboard input, or a separate pause shortcut; gate or reroute every observed consumer. |
| O7 | Vocabulary completeness | P0 confirms the six actions cover every native gameplay input and records any extra path with an inert-or-bind decision. |
| O8 | Analog direction sources | P0 decides whether a stick direction must be bindable; if yes, the vocabulary, threshold, capture, and serialization are designed before P1 freezes defaults. |
| O9 | Native-preset coexistence | The default configuration keeps the native controller preset active beside direct keyboard/mouse actions. Prove with a live song that a held native pad action and a simultaneous direct key action neither double-fire nor starve each other, and that General Mapping's synthesized pad bits cannot be read as physical pad input. |
| O10 | Direct-table interaction at Pause | Confirm that a pause from either table enters the same pause context, that General Mapping alone owns the paused UI, and that releasing a held pause binding after resume cannot immediately pause again or leak a note. |

The per-input-type split, each table accepting only its own source kinds, and the visibility rule
(controller switch hides the native category; Keyboard + Mouse preserves it) are requested
behavior. The launch-scoped apply policy, the exact binding defaults, the duplicate-source policy
(allow and note), the UI-only General Mapping rule while a direct table is on, and the development
sequencing guard are proposed product decisions. All other decisions above implement the requested
separation, source toggles, native-preset preservation, and visibility rules.

## 9. Planning-pass verification

Source inspection only; no game build, song run, or timing measurement was performed for this
document. The pass-2 run on 2026-10-17 reported 0 errors and 29 known warnings (the allowlisted
historical links) after the plan was normalized to CRLF and added to the index; the pass-3 edit
changed no evidence claim, so re-run the same checks after editing:

```powershell
python .github/skills/documentation/scripts/check_docs.py
git diff --check
```

P0 implementation started on 2026-10-17, so this file lives in `in-progress/`. Maintain P0-P6
evidence here without treating planned checks as passed.

## 10. P0 evidence — the gameplay seam (2026-10-18)

P0 is **closed for the action seam and open in four named places** (§10.5): the boundary, both source
types, the lookup bypass, native/direct coexistence, the full producer bypass, a judgement proof and
the alternate contexts are proven live; what remains is listed beside each item and belongs to P2/P3. The
instrument is `src/diag/gameplay_probe.cpp` (cvars `probe_gameplay`, `probe_gameplay_bypass`,
`probe_gameplay_inject`, `probe_gameplay_inject_track`; each has the `REX_<UPPERCASE>` environment
override). The live arms are driven by `out/_swap/p0/run.ps1` on the release build with
`--mnk_mode=1`, which maps Space to pad A, L to X, P to Y, and Return to Start.

### 10.1 Native model (static, 2026-10-17)

`assets/game/360/config/gen/macros.dtb` defines the gameplay vocabulary: `kBMAction_None 0`,
`Lane0 1`, `Lane1 2`, `Lane2 3`, `TrackUp 4`, `TrackDown 5`, `UsePowerUp 6`, `Swing 7`,
`AwardJackpot 8`. `assets/game/360/config/gen/beatmatch_controller.dtb` holds four joypad presets
and three keyboard presets (decoded 2026-10-17 in `out/_swap/p0/defs/beatmatch_controller.txt`);
every preset row uses only `Lane0`, `Lane1`, `TrackUp`, `TrackDown`, and `UsePowerUp`. No shipped
preset row references `Lane2`, `Swing`, or `AwardJackpot`. The joypad block also carries
`delay_until_repeat_ms` and `repeat_rate_ms` (both `#1`), which is consistent with the held-action
repeat observed in §10.3.
The physical button ids come from the `kPad` enum in
`assets/game/360/dotdot/dotdot/system/run/config/gen/macros.dtb`: LT0, RT1, LB2, RB3, Y4, B5, A6,
X7, Back8, LS9, RS10, Start11, DUp12, DRight13, DDown14, DLeft15, LStickUp16, LStickRight17,
LStickDown18, LStickLeft19, RStickUp20, RStickRight21, RStickDown22, RStickLeft23.

Preset 0 (the live configuration, `cfg=0xd`) resolves to: LT/LB → TrackDown, RT/RB → TrackUp,
X → UsePowerUp, any LStick or D-pad direction → Lane0, A or any RStick direction → Lane1. Start
(button 11) resolves to no `kBMAction`; Pause is a separate game-screen path (§10.3).

### 10.2 Seam (all guest functions take `(PPCContext&, uint8_t* base)`)

| Symbol | Role and register use | Evidence |
| --- | --- | --- |
| `sub_82435EB0` | Controller factory (`NewController`): reads `beatmatcher.controllers.beatmatch_controller_mapping`, builds `sub_82443A70` (112-byte joypad) or `sub_82442EC8` (120-byte keyboard) | `out/_swap/p0/native/native.log` — `NewController -> 0x… (cfg=0xd)`; lookups for buttons 15 and 13 are logged in the same tick |
| `sub_82443A70` | JoypadController constructor; vtable `0x820874C4`; overrides +20 `sub_82443710`, +88 `sub_82443440`, +92 `sub_82443A30`, +96 `sub_82443400`, +104 `sub_82443A04` | static |
| `sub_82443A30` | JoypadController vtable +92, per-frame update; called during a song at roughly 30 ms cadence | probe timer seam, §10.3 |
| `sub_82443478` | ButtonDown: button at `[msg+4]` → `sub_824432A0` → `sub_824364B0(controller, action, 0)` | `native.log` down/dispatch pairs |
| `sub_82443528` | ButtonUp: button → `sub_824432A0` → `sub_824367E0(controller, action)` | `native.log` up/release pairs |
| `sub_824432A0` | Button→action lookup over `[this+84]` index and `[this+88]` mapping vector; 0 for unmapped | bypass point, §10.3 |
| `sub_824364B0` | **Gameplay action boundary** (`r3`=controller, `r4`=action, `r5`=arg): returns unless `[this+52]`; lanes 1/2/3 send lane `action-1`; 4 sends +1 track, 5 sends −1, 6 power-up, 7 swing, 8 jackpot. For 4/5 also calls `sub_8243F298(target, ±1)` and `sub_82396D40(target, arg)` | live dispatch/release, §10.3 |
| `sub_824367E0` | Release (`r3`=controller, `r4`=action): handles lanes 1/2/3, swing 7, power-up 6; **no case for 4/5/8** | static; direct release of tracks has no effect |
| `sub_82436A28` | Message-event producer: resolves a symbol and calls `sub_824364B0(controller, action, 0)` | static — a second producer into the same boundary |
| `sub_82442B30` | Keyboard controller ButtonDown analogue; resolves a symbol and reaches the boundary through `sub_82442718` | static |
| `sub_82436BF8` | Held-action auto-repeat: `[+68]` action (0 = idle), `[+56]/[+60]` interval, `[+64]` rate, `[+72]` accumulator, `[+76]` active; calls `sub_824364B0(this, action, 1)` when the accumulator passes | static; matches the 108 ms live repeat, §10.3 |
| `sub_8238A0F0` | `Game::update_controls` (Game vtable `0x82063310` +0x68), called once at song start, not a frame seam | `native.log` — logged once |

### 10.3 Live arms (2026-10-18, base song, release build)

| Plan action | kBMAction | Producer seen | Consumer call | Release | Verdict |
| --- | --- | --- | --- | --- | --- |
| First Note | Lane0 = 1 | preset maps LStick/D-pad directions; no live native press yet | direct `sub_824364B0(…,1,0)` | `sub_824367E0(…,1)` | direct path proven; native edge is static-only |
| Second Note | Lane1 = 2 | live A (button 6) | native `dispatch action=2 arg=0`, then direct `action=2` | native and direct `release action=2` | proven both ways |
| Move Lane Right | TrackUp = 4 | preset maps RT/RB | direct `action=4`, arg 0 then 1 | no release case | consumer reached; held/repeat semantics unresolved |
| Move Lane Left | TrackDown = 5 | preset maps LT/LB | direct `action=5`, arg 0 then 1 | no release case | consumer reached, then a native repeat ran for the rest of the arm |
| Powerup | UsePowerUp = 6 | preset maps X; no live native X yet | direct `action=6 arg=0` | direct `release action=6` | direct path proven |
| Pause | none (`kAction_Start`) | live Start (button 11) | none — `lookup … button=11 -> 0` | n/a | Start paused the game (`pause-shots/05-pause.png`); Pause is a game-screen DTA path, not a `kBMAction` |

Evidence files: `out/_swap/p0/native/native.log` (native A → Lane1), `inject/inject.log` (native press
and direct 1/2/6 in one session), `bypass/bypass.log` (lookup suppressed, direct 1/2/6 still land),
`track/track.log` (native repeat), `pause/pause.log` (button 11 → 0), with shots under
`inject-shots/` and `pause-shots/`.

The direct calls are emitted by the probe's timer on `sub_82443A30`: Lane0 at tick 60 (release 70),
TrackUp at 120 (`arg=1` at 125, gated by `probe_gameplay_inject_track`), Lane1 at 180 (release 190),
TrackDown at 240 (`arg=1` at 245, same gate), UsePowerUp at 300 (release 310). Consecutive injected
actions measured ~26 ms of probe time per tick in both arms, so the sequence spans about 8 s; the
tick numbers are a probe clock, not a game contract.

**Bypass.** With `probe_gameplay_bypass=true`, both the down and up lookups for button 6 log
`bypass lookup` and return 0; no native dispatch or release follows, while the direct 1/2/6 actions
still dispatch. The bypass is therefore sufficient for the lookup consumer, but it is **not** a full
producer bypass: `sub_82436A28`, `sub_82442B30`, and the repeat `sub_82436BF8` reach
`sub_824364B0` without going through `sub_824432A0`. P3 must suppress all four producers (or replace
the controller object) and prove it.

**Coexistence.** `inject/inject.log` contains the native `down button=6 → dispatch action=2 arg=0` and
its `release`, plus direct `action=1` and `action=6` dispatches/releases in the same song. Each press
produced exactly one dispatch; no double-fire and no starvation. This is the shipping Keyboard +
Mouse shape (native preset authoritative for the pad, direct actions beside it) at probe fidelity.

**Track repeat (important).** `track/track.log` shows the direct action-5 dispatch at 00:36:59.5,
then 208 native-originated `dispatch … action=5 arg=1` calls from 00:37:04.5 to 00:37:26.8
(~108 ms apart, ~9.3/s), continuing until the process ended. Action 4 did not start a repeat. The
cadence matches `sub_82436BF8`; `sub_824367E0(5)` is not a stop path. Direct mapping must model
Move Lane Left/Right as a held/repeating source and own the stop, or the native repeat outlives the
binding. Whether the repeat is the controller's own held-state object or the game's track-scroll
state is **not resolved**; the live trace only proves the handler is re-entered natively.

*Superseded by §10.6 (still measured, better understood): the repeat **is** the controller's own
held-action slot, and the release **is** the stop path — `sub_824364B0` stores the action at `[+68]`
on `arg == 0`, `sub_82436BF8` re-dispatches from `[+64]`, and `sub_824367E0` clears `[+68]`. The
`track` arm's slot trace reads `0 → 4 → … → 0` around the press/release pair. This arm's 208
repeats ran because its TrackDown dispatch was never released.*

### 10.4 Verdicts for the open items

Updated 2026-10-18 by the second evidence pass (§10.6). An item is *closed* only where a live arm
shows the thing it asked for; the reason for each still-open item is written beside it.

- **O1 (consumers/bypass). CLOSED.** The consumer set is named (§10.2) and the boundary is proven.
  The full producer bypass is proven by the `challenge-block` arm: one run suppresses all four
  producers — `from=button_down`/`button_up`, `from=message` (reached by the guest-stack fixture
  through `sub_82436A28`), `from=keyboard_down`/`keyboard_up` (through `sub_82442B30`) — and the
  `block` arm adds `from=repeat` (`sub_82436BF8`, `lr=0x82443a48`). The same arm shows the
  *unblocked* control run reaching the boundary from every one of those producers
  (`challenge`), so each suppression has its before/after pair.
- **O3 (initial bindings). CLOSED for the vocabulary, open for the final spelling.** Native preset 0
  gives the Controller defaults: First Note = `pad:lstick_up/down/left/right` +
  `pad:dpad_up/down/left/right`; Second Note = `pad:a` + `pad:rstick_up/down/left/right`; Move Lane
  Right = `pad:rt`, `pad:rb`; Move Lane Left = `pad:lt`, `pad:lb`; Powerup = `pad:x`; Pause =
  `pad:start`. §10.6 confirms every one of those six on a *live* native press. For the keyboard
  table, the shipped keyboard preset 0 (`out/_swap/p0/defs/beatmatch_controller.txt`) is the
  evidence-based proposal: First Note = 1/7/a/f/l; Second Note = 2/8/s/j/;; Move Lane Right =
  Right/k/x/.; Move Lane Left = Left/d/z/,; Powerup = Space; Pause stays the host's Start binding.
  P1 freezes the final spelling and defaults.
- **O7 (vocabulary). CLOSED.** The five preset-produced actions plus Pause cover every gameplay input
  the native preset produces. `Lane2`, `Swing`, and `AwardJackpot` are defined but not reachable from
  any shipped preset: **inert** (no row, no default). Tutorial skip, calibration confirm, and song-end
  inputs stay **inert for the gameplay tables** and owned by the game/General Mapping: the tutorial
  and calibration *menus* were reached and produced no dispatch at all (§10.6), and the end-of-song
  screen is a UI screen reached after the song controller is gone.
- **O8 (analog direction). CLOSED.** Stick directions are native bindable `kPad` values 16–23, preset
  0 binds them, and the `dpad` and `ultimate3` arms show them arriving as live native presses
  (button 16 → `lane0`, button 12/13–15 → `lane0`). The vocabulary needs `pad:lstick_left/right/up/down`
  and `pad:rstick_*`; the Controller default reproduces preset 0 by binding all four directions, even
  though the native preset treats any direction as the same action.
- **O9 (coexistence). CLOSED at probe fidelity.** Proven in §10.3 and repeated in `ultimate3`. The
  synthesized-pad provenance question (a `pad:` source must not fire because the SDK's keyboard
  emulation set the same bit) is **not** answered by a pad-only run: it stays P2 work, and the
  `sources` arm that would have measured raw SDL sampling never ran (§10.6).
- **O6 (guest keystrokes). CLOSED.** The import table has no keystroke API at all (§10.5), so no guest
  consumer exists to gate; the keyboard producer is unreachable without fabricated messages.
- **O10 (pause). OPEN.** Start is not a `kBMAction`; the live pause came through the game-screen path,
  and in the `pause` arm the `Start` press resolves to `action=0 (none)` on both the down and the up.
  Pause routing is therefore a context rule, not a dispatch action: P3 must route a direct Pause
  binding into the same pause screen and prove that a held binding cannot pause again on resume.
  Nothing in this pass measures a *direct* pause.

### 10.5 What P0 still owes

Closed by §10.6 (2026-10-18): every item this list carried on the first pass.

1. **Live native presses** — proven for all six: Lane0 (button 16 = LStickUp, and 12–15 = the D-pad
   directions, `dpad`), Second Note (button 6 = A), TrackUp (button 3 = RB, button 1 = RT),
   TrackDown (button 2 = LB, button 0 = LT), Powerup (button 7 = X). `native-hold`, `dpad` and
   `ultimate3`.
2. **Judgement proof** — `spam` dispatches alternating Lane0/Lane1 directly for ~50 s and the HUD
   score rises 0 → 470 → 1,760 → 2,720 (`spam-shots/score-sheet.png`), so a direct dispatch is judged
   by the game's own note pipeline, not merely accepted by the handler.
3. **Track stop and repeat owner** — the repeat is the *controller's own held-action state*:
   `sub_824364B0` stores the action at `[+68]` when a track press arrives with `arg == 0`, the action
   is re-dispatched by `sub_82436BF8` after `[+60]` and then every `[+64]`, and
   `sub_824367E0` clears `[+68]` on release. The `track` arm logs the slot `0 → 4 → … → 0` around a
   press/release pair, and the `ultimate`/`ultimate3` arms show the native repeat stopping when the
   button is released. A direct track action must therefore be **released**, not abandoned.
4. **Full bypass of all four producers** — `challenge-block` + `block` (§10.4 O1).
5. **The alternate contexts** — measured except the tutorial's and calibration's *interactive*
   segments: `Pause` (`pause`, `pause-block`), song end (`song-end`, the results screen),
   `Ultimate` (`ultimate3`), no pad (`no-pad`, 25 dispatches with `--input_backend=none`), and the
   two context menus (`tutorial3` → *How to Play*, `calibration` → *Calibrate System*, both with
   zero dispatches).

What this pass does **not** close, and P3 must carry:

- **O2, action edge/hold semantics per action.** The probe dispatches with an explicit release and
  the judgement proof shows score, but no arm measures press-edge vs held-state per action, nor the
  double-fire question for a pad binding and a key binding on one action.
- **O4, the tutorial's and calibration's interactive segments.** Their menus are measured; entering
  *Basic Tutorial* and playing it, and running the *Calibration Wizard*, were not.
- **O5, source provenance.** The `sources` arm ran on 2026-10-18 (`out/gameplay-mapping/p0/sources/`, argv
  carries `--probe_gameplay_sources=true`) and produced **no** `source=` and no direct-action lines, so
  raw SDL sampling is still unmeasured: "a `pad:` source cannot fire because the SDK's keyboard emulation
  set the same bit" stays unproven, and the first question for P2 is why the sampler saw no edges (keyboard
  focus, or the SDL keyboard state read from a non-video thread). `no-pad` proves a keyboard-only boot is
  *connected* and can play, which is the connectivity half only.
- **O6, guest keystrokes. CLOSED (static).** The title's import table
  (`generated/default/rb_blitz_init.cpp`) carries **zero** `XamInputGetKeystroke` (or any keystroke)
  imports; the only keyboard-named import is `XamShowKeyboardUI`, the on-screen text-entry dialog, which
  is not a gameplay path. There is no guest keystroke consumer to gate, and the keyboard controller's
  ButtonDown (`sub_82442B30`) stays reachable only if key messages were delivered - nothing in this port
  delivers them, which is why the `challenge` fixture had to fabricate one.
- **O10, a direct pause.** See §10.4.
- **The analog stick under a real thumb.** The native presses came from the host's keyboard
  emulation mapping a key to a pad bit (that is how `drive_ui` works), so analog *magnitude* and the
  deadzone of a physical stick were not exercised.

P0's action-seam question is answered: the six actions have one validated boundary, every native
producer into it is named and can be suppressed, a direct dispatch is judged by the game, and the
track repeat is owned by the controller's own slot. P3's "all six actions work" acceptance is now
reachable, but it must still show the score response *through a real binding* and close O2/O5/O6/O10
before it can claim live acceptance.

### 10.6 Second evidence pass (2026-10-18) — the arms that closed §10.5, and a picture freeze

All releases at build time 10:15–10:36 on 2026-10-18, base song unless the arm says otherwise. The
harness is `scripts/probe_gameplay.ps1` (the deliverable) with the scratch arm driver
`out/_swap/p0/run.ps1` for the later switches; evidence under `out/_swap/p0/<label>/` (combined log)
and `<label>-shots/`.

| Arm | What it measured | Evidence |
| --- | --- | --- |
| `native-hold` | Native presses for A (button 6 → `lane1`), LStickUp (16 → `lane0`), X (7 → `use_powerup`), RB (3 → `track_up`), LB (2 → `track_down`), LT (0 → `track_down`), RT (1 → `track_up`), each with its release; RT held 7 s exercises the controller's own repeat | `native-hold/native-hold.log` |
| `dpad` | The four D-pad ids (12–15) each resolve to `lane0` under preset 0, with the arrows rebound to plain keys first | `dpad/dpad.log`, `dpad-shots/09-dpad.png` |
| `bypass` / `block` / `pause-block` | The lookup bypass suppresses the preset (`bypass lookup … return 0`) while direct actions still land; the dispatch block suppresses every producer including `from=repeat`; the block holds across a pause | `bypass/`, `block/`, `pause-block/` |
| `challenge` / `challenge-block` | The message producer (`sub_82436A28`) and keyboard producer (`sub_82442B30`) reached the boundary in the control run and were blocked in the second, using guest-stack fixtures built by the probe rather than a fabricated `lr` | `challenge/`, `challenge-block/` |
| `track` | `track slot before track_up action=0` → `after track_up press action=4` → native repeats → `after track_down release action=0 (none) repeating=0`: the release clears `[+68]` and the repeat stops. The §10.3 statement that `sub_824367E0` has no case for 4/5/8 was wrong about the *slot*; the release is the stop path | `track/track.log` |
| `spam` | 50 s of alternating direct Lane0/Lane1 dispatches raise the HUD score 0 → 470 → 1,760 → 2,720: a direct dispatch is judged by the game's own pipeline | `spam-shots/score-sheet.png` |
| `no-pad` | `--input_backend=none` boots, is connected, plays, and dispatches 25 actions (keyboard-only connectivity, no physical pad) | `no-pad/no-pad.log` |
| `song-end` | The results screen is reached (`THESE DAYS`, `Finale 400`, `A CONTINUE`) after the song controller is torn down | `song-end-shots/08-song-end.png` |
| `pause` | Start pauses the game through the game-screen path and resolves to `action=0 (none)`; `05-pause.png` / `11-resume.png` | `pause/pause.log` |
| `ultimate3` | The Ultimate payload plays a song: all six native presses, plus the payload's own SELECT POWER-UPS screen on the way in | `ultimate3/ultimate3.log`, `ultimate3-shots/01b-powerups.png` |
| `tutorial3`, `calibration` | *How to Play* and *Calibrate System* reached; the same physical controls that map to gameplay actions produce **zero** dispatch lines in either menu | `out/gameplay-mapping/p0/tutorial3-shots/03-tutorial.png`, `.../calibration-shots/03-calibration.png` |

**The freeze, its cause, and its fix.** `ultimate2` (the same route as `ultimate`, same flags) showed
every one of its eight screenshots byte-identical, the first being the title's "Discovering
Downloadable Content" screen, while the guest kept running behind it. The boot log ends with
`ExecutePacketType0 overflow (read count 00007C44, packet count 00007EF8)` and
`**** PRIMARY RINGBUFFER: Failed to execute packet.`; two earlier ad-hoc Ultimate boots
(`ultimate-nocache`, `ultimate-manual`) stalled the same way with no log kept. The cause is in the
SDK's command processor, not in the probe or the payload: the ring's write pointer is a separate
store from the packet data, so a packet header can be readable before its payload lands. That packet
is *incomplete*, but `ExecutePrimaryBuffer` treated any failed packet as the end of the frame and
returned the write index, discarding every remaining packet — including the frame's `XE_SWAP`. With
the swap gone nothing was presented again, which is the frozen picture.

Patch `0018-command-processor-incomplete-packet-recovery.patch` makes that packet a retry instead:
the read pointer does not advance, `WorkerThreadMain` reloads the write pointer and calls back in,
and the same packet executes once the payload is visible. A packet still incomplete after 64 retries,
or a handler that failed after consuming payload, resynchronises past its header so the rest of the
frame (in particular its swap) still runs, bounded at 8 resyncs per boot. `ExecutePacketType1` gained
the length check its siblings already had — it read two payload words unconditionally.

Measured recovery, same boot shape that froze before (`hunt104006-1`, 2026-10-18):
`ExecutePacketType0 overflow (read count 00002DDC, packet count 00007A30)` followed by
`**** PRIMARY RINGBUFFER: packet at 00000054 is incomplete (11740 byte(s) published); retrying (1 of 64)`,
**no** `Failed to execute packet`, and the picture keeps updating (`after.png` ≠ `after-move.png`;
`after-move.png` is the song list with the Ultimate SELECT POWER-UPS screen behind it). Ten Ultimate
boots on the fixed build (that one plus `hunt*`/`observe*`/`ultimate3`) presented normally.
`docs/reference/known-issues.md` carries the row.

**Harness changes this pass needed** (`scripts/probe_gameplay.ps1`): the window is no longer started
`-WindowStyle Hidden` (a hidden window cannot be captured or driven); the guest's own title is waited
for by OCR before any key is sent, because input during the boot or the content discovery is what the
packet desync was first seen with; and every menu tap carries its own `wait:1`, since the guest drops
a run of taps with no wait between them.

**Still open** (unchanged by this pass): the tutorial's and calibration's interactive segments (O4), raw
SDL source provenance (O5 - the `sources` arm ran and logged no edges; P2 owns the next question),
per-action edge/hold semantics (O2), a direct pause (O10), and the analog stick under a real thumb.
Guest keystrokes (O6) closed statically: the title imports no keystroke API. See §10.5.

## 11. P1 evidence — shared configuration (2026-10-18)

**P1 landed; P2 can start.** The two gameplay tables, their frozen defaults, the profile storage,
the three switches and the first half of the settings surface exist and are tested. P1 has no
runtime consumer: no input is sampled for a gameplay table and no action is dispatched, so the
sequencing guard in §6 stays in force — a build must not act on either gameplay switch before P2/P3
pass acceptance.

### 11.1 What landed

| P1 item | Where it landed | Evidence |
| --- | --- | --- |
| Action vocabulary and the two tables | New [src/profile/gameplay_remap.h](../../../src/profile/gameplay_remap.h) / [.cpp](../../../src/profile/gameplay_remap.cpp): six actions (`first_note`, `second_note`, `move_lane_right`, `move_lane_left`, `powerup`, `pause`), `Table::FromRows`/`ToRows`/`Bindings`/`Effective`/`Set`/`Reset`/`ResetAll`, and `Rejected()` as the diagnostic list | `tests/gameplay_remap_tests.cpp`, `TestVocabulary` and `TestSourceKinds` |
| Per-table source kinds (R11) | `AcceptsSourceKind`: `[gameplay_remap_controller]` takes `pad:` only, `[gameplay_remap_keyboard_mouse]` takes `key:`/`mouse:` only. A wrong-kind row is kept verbatim, reported with the reason, and not applied — never half-applied | same suite, `TestSourceKinds` |
| Frozen defaults (O3) | `DefaultSources`. Controller reproduces the measured native preset 0: all four LStick and D-pad directions → `first_note`; `pad:a` plus all four RStick directions → `second_note`; `pad:right_trigger`/`right_shoulder` → `move_lane_right`; `pad:left_trigger`/`left_shoulder` → `move_lane_left`; `pad:x` → `powerup`; `pad:start` → `pause`. Keyboard + Mouse reproduces the shipped keyboard preset 0: `1/7/A/F/L`, `2/8/S/J/Semicolon`, `Right/K/X/Period`, `Left/D/Z/Comma`, `Space`, `Return`, with **no mouse binding** (plan D6: opt-in) | same suite, the two default cases |
| Pad vocabulary for directions (O8) | [src/profile/remap.h](../../../src/profile/remap.h)/[.cpp](../../../src/profile/remap.cpp): `PadDirection` (`lstick_up/down/left/right`, `rstick_*`), one pinned `kStickThreshold = 0x4000`, `PadState` gains the four stick axes, `Apply` answers a direction from them, and the axes are copied through untouched. `ParseSourceList`/`FormatSourceList` are public so both tables read one grammar | `tests/controller_settings_remap_tests.cpp`, the direction case |
| Profile storage | `Profile::gameplay_remap_controller` / `gameplay_remap_keyboard_mouse`; reader, surgical patching writer, fresh-document renderer, drop-on-empty, `ResetPlan` + `ResetToDefaults`, and the rebased-save merge in [profile_session.cpp](../../../controller-settings/src/profile_session.cpp) | `gameplay_remap` suite `TestProfileStorage`; `controller_settings_session` `TestGameplayTablesMerge` |
| No schema-version bump | The tables are additive; a `schema_version = 1` document carrying both loads, and the profile module leaves a table it does not model untouched | `gameplay_remap` suite, the version case |
| Switches and allowlist | `keyboard_support` (on), `gameplay_mapping_controller` (off) and `gameplay_mapping_keyboard_mouse` (on) defined in [src/input/remap.cpp](../../../src/input/remap.cpp); all three added to `kControllerCvarAllowlist` in [src/settings/cvars.h](../../../src/settings/cvars.h); `ReadGameplaySwitches()` is the one reading of them | `gameplay_remap` suite `TestSwitchesAreAppliedByABoot`; `TestExplicitValues` |
| Settings surface, first half | Schema row **Keyboard support** in the `Input` group ([settings.toml](../../../controller-settings/config/settings.toml)); the R13 note rewritten; the **Controller** and **Keyboard + Mouse** switches drawn by the Controller tab's custom block under the mapping table, with their own ring rows (`ControllerTab::FocusRows`, which the shell now sizes the ring from), bottom-bar help, and a status line that says the change applies the next time the game starts | generated `controller-settings/out/generated/settings_table.h`; `controller_settings_nav` suite |
| Boot log (D2) | `gameplay mapping: keyboard_support=… (source) mouse_ui_nav=… (source) gameplay_mapping_controller=… (source) gameplay_mapping_keyboard_mouse=… (source)` and `gameplay mapping: [<section>] N user row(s), M action(s) firing of 6; …`, plus a warning per refused row and one for an all-empty table | `[tree]` — see the unproven item below |

### 11.2 What this pass does not claim

- **No consumer.** The switches gate nothing and dispatch nothing: source gating is P2 and the
  direct dispatch is P3. A boot today behaves exactly as it did before P1.
- **No gameplay is exercised.** §11.4 records a short 2026-10-18 boot that printed the two lines;
  it reaches the menu only, so no song, note, lane, power-up or pause path was driven.
- **No table UI.** The six-action tables, per-table capture, the strike-through of a gated source,
  the help modal and the per-table visibility are P4; the native-category hiding is P5. P1 draws
  the switches only.
- **No `--dump-layout` row for the switches.** They are custom-block rows, not schema rows; the
  new schema row (`keyboard_support`) does appear there.

### 11.3 How the tests were run

The release tree in this checkout was configured with `REXGLUE_USE_VULKAN=ON`, whose
`thirdparty/CMakeLists.txt` forces `BUILD_TESTING` off (the hazard [input-flow.md](../../engine/input-flow.md)
records), so `ctest --test-dir out/build/win-amd64-release` cannot register the suite. Each affected
suite was therefore compiled with the preset's own compiler and run directly; the files and flags
are the ones the CMake targets use:

```powershell
clang++ -std=c++23 -O1 -I src tests/gameplay_remap_tests.cpp src/profile/profile.cpp `
  src/profile/gameplay_remap.cpp src/profile/remap.cpp -o out/_swap/p1/tests/gameplay_remap_tests.exe
clang++ -std=c++23 -O1 -I src tests/controller_settings_remap_tests.cpp src/profile/profile.cpp `
  src/profile/remap.cpp -o out/_swap/p1/tests/controller_settings_remap_tests.exe
clang++ -std=c++23 -O1 -I src -I controller-settings/src `
  tests/controller_settings_session_tests.cpp controller-settings/src/game_config.cpp `
  controller-settings/src/prefill.cpp controller-settings/src/profile_session.cpp `
  src/profile/profile.cpp src/profile/profile_path.cpp -o out/_swap/p1/tests/session.exe
clang++ -std=c++23 -O1 -I src tests/controller_settings_profile_tests.cpp src/profile/profile.cpp `
  src/profile/profile_path.cpp -o out/_swap/p1/tests/profile.exe
clang++ -std=c++23 -O1 -I src -I controller-settings/src -I controller-settings/out/generated `
  tests/controller_settings_pad_nav_tests.cpp tests/controller_settings_help_tests.cpp `
  tests/controller_settings_keys_tests.cpp controller-settings/src/pad_nav.cpp `
  controller-settings/src/nav_bindings.cpp controller-settings/src/schema_view.cpp `
  src/profile/profile.cpp -o out/_swap/p1/tests/nav.exe
```

Results on 2026-10-18: `gameplay_remap` 397 checks passed, `controller_settings_remap` 323,
`controller_settings_session` 310 (including the new merge/reset case), `controller_settings_profile`
165, and the nav/help/keys suite 398. The release game and Controller Settings targets build clean
(`cmake --build --preset win-amd64-release`, exit 0); the new source is registered in
`RBBLITZ_SOURCES`, `controller-settings/CMakeLists.txt`, and the `gameplay_remap` test target.

The same suites run under CTest in the **debug** tree, which is the configure this checkout has that
keeps tests on (`-DREXGLUE_USE_VULKAN=OFF -DBUILD_TESTING=ON`; the preset alone forces Vulkan on and
the SDK turns `BUILD_TESTING` off again): `ctest --test-dir out/build/win-amd64-debug` — **39/39
tests pass**, `gameplay_remap` among them (test #27), in 26.1 s. Log:
`out/_swap/p1/p1-ctest-debug.log`.

### 11.4 A live boot names the switches (2026-10-18)

One 20-second release-build boot from `out/build/win-amd64-release`, killed after the input system
was built, with an isolated profile at `out/_swap/p1/boot/launcher.toml` (the three switches written
explicitly; one row in each gameplay table) and `--game_data_root=<checkout>/game
--input_backend=none`. The game reached the menu and the log printed, in this order:

```text
[info] remap: 0 user rebind(s) from the profile; every other control answers to its default pad button and keyboard key (--launcher_profile <path> names another file)
[info] gameplay mapping: keyboard_support=on (config) mouse_ui_nav=on (default) gameplay_mapping_controller=off (config) gameplay_mapping_keyboard_mouse=on (config)
[info] gameplay mapping: [gameplay_remap_controller] 1 user row(s), 6 action(s) firing of 6; [gameplay_remap_keyboard_mouse] 1 user row(s), 6 action(s) firing of 6
```

That is P1's acceptance line: a standalone boot names the effective value of each switch **and where
it came from** (`config` for the three profile rows, `default` for the absent `mouse_ui_nav`), plus
each table's binding count. Source: `out/build/win-amd64-release/logs/rb_blitz_003.log`.

The same boot also proves the profile path end to end for the new keys: the two gameplay rows in the
file were read by the game's own profile apply, the switches beside them arrived as cvars, and the
counts show the compiled defaults filling the five actions the file did not mention. A table with no
rows at all is `gameplay_remap`'s no-rows case: 6 of 6 actions firing in both tables from the
compiled defaults, pinned without a process.
