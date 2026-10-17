# General and Gameplay Mapping

**Status: planning, 2026-10-17. Implementation has not started.** This document records the
requested behavior, a source review at `ca8fa2a`, proposed implementation decisions, and the
investigation required before choosing guest hooks. No gameplay dispatch hook or new setting has
been implemented or tested by this planning pass.

Planning pass 2 (2026-10-17) added the supersession record (§2), the settings-schema and
focus-ring constraints, the build-target registration, the development sequencing guard, the
pad-axis vocabulary gap, and the named documentation/script obligations. It added no runtime
measurements; every new claim is a `[tree]` reading of the file cited beside it.

## 1. Requested behavior

Separate two mappings in Controller Settings:

- **General Mapping** is the existing button mapping: one or more physical inputs emulate a
  controller button. It continues to operate the UI. With Gameplay Mapping off, it also feeds
  the game's native controller preset during play.
- **Gameplay Mapping** binds physical inputs directly to gameplay actions. When enabled, it
  replaces the native preset's interpretation of input for gameplay, including Pause.

The gameplay rows, in display order, are **First Note**, **Second Note**, **Move Lane Right**,
**Move Lane Left**, **Powerup**, and **Pause**. Multiple enabled input devices remain usable
together; there is no exclusive keyboard, mouse, or controller mode.

| Requirement | Acceptance condition |
| --- | --- |
| R1. Input switches | Mouse Support and Keyboard Support both default to on. Keyboard Support is new. Turning either off blocks that source throughout the game's menus and gameplay. |
| R2. General Mapping | Keep the current controller-button vocabulary, multiple bindings, table styling, capture, and reset behavior. |
| R3. Gameplay Mapping | The enable switch defaults to on. Each of the six actions accepts multiple inputs independently of General Mapping and of the saved native preset. |
| R4. One gameplay authority | Enabled: only Gameplay Mapping produces the six gameplay actions. Disabled: General Mapping feeds the native preset, as in the existing gameplay path. |
| R5. Preserve native preferences | Enabling Gameplay Mapping neither changes nor clears the saved controller preset. Disabling it restores use of that same preset. |
| R6. Native menu visibility | Hide the native Controller/Controls category in Help & Options whenever Gameplay Mapping is effective, in both main-menu and paused-game entry paths. Restore it when disabled. |
| R7. Disabled sources stay visible | In both tables, strike through each disabled keyboard/mouse binding and render it with the subtitle color. Keep enabled bindings in the same row normal. Do not delete bindings. |
| R8. Disabled gameplay table | Turning Gameplay Mapping off hides its table, retains its bindings, and removes hidden controls from navigation/capture. Its section, description, help button, and switch remain visible. |
| R9. Utility isolation | Disabling Keyboard Support or Mouse Support does not disable Controller Settings' own navigation or binding capture. The utility stays operable with any device and can re-enable the source. |
| R10. Vocabulary completeness | P0 proves the six actions cover every gameplay input the native preset produces. Any additional native gameplay path is listed and given an explicit inert-or-bind decision before direct mode ships. |

Here, “Controller” means the game's preset configuration category; the current merged menu routes
to it under the symbol `controls`. “Gameplay Mapping” is separate from the existing in-game
**Gameplay** category for Ultimate features. No additional modifier-key/chord feature is specified
by this request; the new mode switch and action bindings are the scope of this plan.

## 2. What the source establishes

These are **[tree] findings from 2026-10-17**, not new runtime measurements. Paths below are the
implementation owners; function names identify the inspected portions.

| Area | Evidence and consequence |
| --- | --- |
| Binding model | [src/profile/remap.h](../../../src/profile/remap.h) and [remap.cpp](../../../src/profile/remap.cpp), `Target`, `DefaultSources`, `Table::Effective`, and `Apply`: 16 targets excluding Guide; `pad:`, `key:`, and `mouse:` sources; absent rows use defaults and explicit empty rows bind nothing. Pad sources read the pre-remap state. Reuse source parsing, but give gameplay its own action vocabulary. |
| Runtime filter | [src/input/remap.cpp](../../../src/input/remap.cpp), `InstallPadRemap`: rewrites merged buttons/triggers through `SetStateFilter`, then merges fixed native-menu mouse pulses and updates packet identity. It has no gameplay-action dispatch or native-preset bypass. Its no-readable-profile branch preserves SDK input rather than applying the table's keyboard defaults. |
| Existing mouse behavior | [src/input/mouse_ui.cpp](../../../src/input/mouse_ui.cpp) defines `mouse_ui_nav=true`; [src/input/native_menu.cpp](../../../src/input/native_menu.cpp), `Context`, distinguishes `game_screen` from its focused `pause_panel`. `RemapContext::SourceDown` currently rejects all raw `mouse:` bindings when Mouse Support is on, including in songs. Conversely, switching it off permits raw mouse bindings. This must change to satisfy R1 and direct mouse gameplay input. |
| Keyboard bypass risk | [SDK input_system.cpp](../../../rexglue-sdk/src/input/input_system.cpp), `CreateDefaultInputSystem`, adds the optional MnK driver. [mnk_input_driver.cpp](../../../rexglue-sdk/src/input/mnk/mnk_input_driver.cpp) has `mnk_mode=false` by default, but when enabled produces keyboard/mouse-derived buttons, triggers, sticks, and keystrokes. A `key:` check in the title's remapper alone cannot disable that path. |
| Connection dependency | SDK `InputSystem::GetState` returns disconnected before calling the filter if no device contributes. The title's `MousePad::EnumerateDevices` only publishes a synthetic device when Mouse Support is on. Keyboard-only play with mouse off and no pad needs a connection independent of native mouse navigation. |
| Settings UI | [controller_tab.cpp](../../../controller-settings/src/controller_tab.cpp), `ControllerTab::Draw`, renders a single button table; `BindingText` flattens all sources to one string. The `?` currently shows a hover tooltip. [shell.cpp](../../../controller-settings/src/shell.cpp) draws schema rows first (`DrawTab`) and the custom mapping block after them. `ControllerTab::kRowCount` is a compile-time count and `Shell`'s constructor sizes `rings_` once (`shell.cpp:191-206`). Consequence: the Gameplay switch cannot be a schema row and land between the two tables, and a hidden table keeps dead focus entries unless the ring is rebuilt. |
| Profile storage | [profile.h](../../../src/profile/profile.h) and [profile.cpp](../../../src/profile/profile.cpp) model `[settings]`, `[remap]`, and `[nav]`, preserving unrelated data. [profile_session.cpp](../../../controller-settings/src/profile_session.cpp), `MergeRows`/`RebaseOntoDisk`, merges concurrent edits by table. New gameplay bindings need full read/write/merge/reset support, not only a parser. |
| Runtime setting ownership | [src/settings/cvars.h](../../../src/settings/cvars.h), `kControllerCvarAllowlist`, currently permits `mouse_ui_nav` and `input_backend`. New switches must enter this allowlist or they can save successfully without reaching a standalone game launch. |
| Native category | [src/ui/settings_screen.cpp](../../../src/ui/settings_screen.cpp), `AllPanels`, routes `controls` to `controller_config_screen`. `PauseRootRows` and `MergePauseListRows` populate the paused game's inline list using `pause_controls`. Hiding only the main-menu row is incomplete. |
| Menu configuration | [settings_screen.h](../../../src/ui/settings_screen.h), `MenuConfiguration`, and [settings_payload.cpp](../../../src/hooks/settings_payload.cpp) already pass boot configuration into menu generation. Carry effective gameplay mode through this seam rather than modifying the user's installed menu files. |
| Preset forcing is not a solution | [src/enhancements.cpp](../../../src/enhancements.cpp) declares `enhancements_force_controller_scheme` / `enhancements_controller_scheme`, explicitly marked not implemented. Neither disables native interpretation or provides direct actions. |
| Prior rules this supersedes | [controller-settings/config/settings.toml](../../../controller-settings/config/settings.toml) line 652 says "Keyboard support is always on and is not a setting (R13)", and [docs/plans/finished/2026-09-27-launcher-plan.md](../finished/2026-09-27-launcher-plan.md) R13 binds it. [docs/engine/toggles.md](../../engine/toggles.md) line 72 calls the mouse "hidden and inactive during gameplay"; [docs/reference/known-issues.md](../../reference/known-issues.md) line 23 says clicks and wheel are "disabled during gameplay and restored on pause"; [input-flow.md](../../engine/input-flow.md) says raw mouse sources are suppressed "including during gameplay". This plan supersedes those current-state statements. The finished plan is not edited; the live documents and schema text are the ones to update. |
| Build registration | The game's source list is `RBBLITZ_SOURCES` in [CMakeLists.txt](../../../CMakeLists.txt); [controller-settings/CMakeLists.txt](../../../controller-settings/CMakeLists.txt) compiles `src/profile/profile.cpp`, `profile_path.cpp` and `remap.cpp` directly, and [installer/CMakeLists.txt](../../../installer/CMakeLists.txt) compiles `profile.cpp`/`profile_path.cpp` too. A new `src/profile/gameplay_remap.*` must be registered in the game and Controller Settings targets; keep `Profile` a storage-only row table so the installer's copy needs no new dependency. |
| In-game settings spec | [docs/settings/plan.md](../../settings/plan.md) §3.2 and its row-position statement say the Controls page keeps its inventory and "nothing about them changes". Conditional removal contradicts that when the mode is on; the owning spec, not only `menu.md`, must record the new rule. |

The existing mouse timing and packet-delivery constraints are documented in
[input-flow.md](../../engine/input-flow.md). Preserve them. A successful UI-only mapping test is
not evidence that note timing, native gameplay suppression, or Pause are correct.

## 3. Implementation decisions

The decisions below are the proposed implementation contract. P0 resolves the guest ABI and
action semantics before P3 is implemented; it must not replace them with guessed addresses.

### D1. Two independent destinations, one physical source layer

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

General Mapping continues to use `[remap]`; store actions in a distinct `[gameplay_remap]` table.
Never reinterpret an existing button target as an action or feed gameplay from the output of
General Mapping. For example, changing `a = "key:K"` must not change First Note while Gameplay
Mapping is on. Both tables may intentionally use the same physical input because they own
different contexts.

Within `[gameplay_remap]`, one source may also name two actions. Allow it: both actions fire,
matching General Mapping's OR semantics, and the table shows a non-blocking note listing the
shared actions. Do not silently drop the second binding. If P0 finds an action pair that must not
co-fire, enforce that at the native consumption boundary rather than rejecting the user's binding.
A source may appear in both tables without a conflict warning because the contexts differ.

The current pad vocabulary is the 16 `Target` names; it has no directional stick source such as
`pad:lstick_left`. P0 must state whether lane movement or a note action needs an analog direction.
If it does, extend `Source` with stable directional stick names, pin one threshold constant, and
add capture for it; if it does not, state that only the listed discrete pad controls are bindable
and refuse an axis name rather than guessing. Do not silently accept a vocabulary the game cannot
apply.

Read physical pad state before remapping or synthetic input merging. A `pad:` source must not
become active because an SDK keyboard binding synthesized the same pad bit. Preserve simultaneous
pad/keyboard/mouse use and existing player-slot assignment. Host keyboard/mouse actions belong
to the active single-player input slot; do not inject them into every polled guest slot.

### D2. Source gates precede all interpretation

Use `keyboard_support=true` and retain `mouse_ui_nav=true` as the compatible stored mouse switch,
with **Mouse Support** as its label. Expand that switch to gate every mouse-to-game path. Avoid
introducing a second user-facing mouse toggle that disagrees with it.

Both gates are runtime cvars read at poll time, like `mouse_ui_nav` in
[native_menu.cpp](../../../src/input/native_menu.cpp), not profile booleans read once by one
consumer. The profile feeds them through the controller allowlist, so config/env/CLI precedence
still wins and the input filter, the menu generator, and the Controller Settings preview observe
one effective value. Log the effective triple and its source at boot.

The gates apply to default bindings, explicit bindings, direct gameplay actions, native menu
pointer input, legacy SDK emulation, and guest-facing text/keystroke paths if the title uses them.
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

| Effective gameplay mode | Context | Action source |
| --- | --- | --- |
| Off | Menus, pause menus, and dialogs | General Mapping plus the existing native pointer navigation when mouse support is enabled. |
| Off | Playing a song | General Mapping -> native saved controller preset. Direct gameplay bindings are inert. |
| On | Menus, pause menus, and dialogs | General Mapping plus native pointer navigation. Gameplay bindings are inactive. |
| On | Playing a song | Gameplay Mapping -> validated guest action input. Native preset input and General Mapping cannot also produce gameplay actions. |
| Either | Unfocused window, input captured by a host modal, or a transition without an input destination | Release/reset the affected input state; discard queued actions that belong to the previous context. |

Use an authoritative guest gameplay state and ownership of the focused UI, not a test for
`game_screen` alone. Pause menus, nested settings, loading/start prompts, countdowns, tutorials,
and end-of-song screens must each be classified in P0. Keep context observation independent of
Mouse Support: turning the mouse off must not freeze gameplay routing.

With mouse support on, keep native menu hover/left-click/right-click/wheel behavior. Those gestures
have one owner in menu contexts and cannot also activate a custom general binding there. During
active gameplay, mouse buttons are eligible for the active mapping table; cursor navigation and
queued menu clicks are inactive. Document this context rule in help. With support off, both
native navigation and bound mouse buttons are inert.

Keep the OS cursor hidden during active gameplay: [mouse_ui.cpp](../../../src/input/mouse_ui.cpp)
shows it only when a native menu is available or ImGui wants pointer capture. A gameplay `mouse:`
source is sampled from SDL button state, not from the native-menu queue, so the click cannot hover,
press, or scroll a menu on its way to the action.

Pause is a gameplay action while playing. Once paused, General Mapping handles Resume, Back,
and navigation. A held pause input must not immediately resume or select an item after that
context change. Require release before rearming a source that crosses such a boundary.

### D4. Replace the native input producer, preserve the gameplay consumer

Find the point where the selected native preset translates controller input into the six actions.
When Gameplay Mapping is enabled, bypass that translation and supply the custom action state or
events to the same validated downstream action handlers. Keep the engine's note judgement,
calibration, lane animation, powerup eligibility, and pause lifecycle intact.

The bypass must cover digital buttons, analog sticks/triggers, held input, repeat paths, and any
separate Start/Pause handler. Disabling only one button-down listener or clearing only button
bits is insufficient. Do not force a “neutral” preset or synthesize a fixed preset's buttons as the
permanent architecture: either approach still couples the custom scheme to native interpretation.

Suppression is scoped to gameplay consumers. Do not blank all controller state globally, because
menus, system connectivity, calibration, and paused UI still need input. Gameplay mode off must
call the original path without injecting direct actions. Never write the saved preset field as
part of enable/disable or hide the native category as a substitute for disabling its behavior.

Dispatch guest actions on the appropriate guest execution thread using the established hook/call
conventions. Window callbacks only publish input. Do not call guest routines from SDL events or
assume the SDK input poll and UI update share a thread.

### D5. Preserve action timing and multiple-source semantics

Use one synchronized source snapshot/event stream for both mapping evaluators. Define ownership
and consumption so polling the guest state twice cannot dispatch an action twice, and a short
press/release between render frames cannot disappear. Retain ordered edges needed by the native
action consumer; do not implement gameplay through the menu mouse's multi-frame button pulses.

P0 must determine, per action, whether the game consumes a press edge, held state, release, or
native repeat. The action evaluator then follows that contract:

- General button mappings keep their existing OR behavior across enabled sources.
- Held action state stays down until every contributing source releases. Disabling one source
  cannot release an action still held by another enabled source.
- Strike actions must preserve intentional repeated presses from different bindings, including
  alternating keyboard/controller presses. Do not lose them by applying one aggregate held bit
  if the native consumer requires individual strike edges.
- Simultaneous bindings for the same action must not accidentally double-trigger it. Establish
  deterministic coalescing at the native consumption boundary without merging distinct rapid
  taps; verify this against the measured consumer.
- First Note and Second Note can act together. For simultaneous opposite lane directions, define
  and test a deterministic policy after measuring native behavior; proposed fallback is neutral
  while both are held, with no replay of stale movement on release.

Clear pending gameplay edges and held/repeat state on focus loss, disconnection, context changes,
and shutdown. No key repeat, stalled frame, or reconnect may leave a stuck action or replay a
burst. Keep packet identity based on the final delivered general pad state; add an independent
action sequence/consumption identity if the chosen guest hook requires it.

### D6. Configuration, defaults, and migration

Store `keyboard_support` and `gameplay_mapping_enabled` in the Controller Settings profile's
`[settings]` table, with compiled defaults of true. Keep `mouse_ui_nav` there too. Add the new
switches to the runtime controller allowlist and apply the existing config/env/CLI precedence.
The gameplay resolver and menu generator must use the **same effective mode**, including overrides.

Proposed profile shape; bindings here illustrate serialization, not finalized default controls:

```toml
[settings]
mouse_ui_nav = true
keyboard_support = true
gameplay_mapping_enabled = true

[remap]
a = "pad:a, key:K"

[gameplay_remap]
first_note = "pad:left_trigger, key:J"
second_note = "pad:right_trigger, key:K"
move_lane_right = "pad:right_shoulder, key:D"
move_lane_left = "pad:left_shoulder, key:A"
powerup = "pad:x, key:Space"
pause = "pad:start, key:Return"
```

Missing action rows use one shared compiled default table; explicit empty strings mean unbound.
Provide a complete keyboard and controller scheme for all six actions before enabling the feature
by default. P0/P1 must publish and test the actual defaults; the user has not specified those keys.
Prefer a recognizable native layout for pad defaults, translated once into explicit action sources,
and an ergonomic keyboard scheme. Do not dynamically follow the saved native preset while the new
mode is enabled. Mouse bindings may be opt-in while Mouse Support itself defaults to on.

Old profiles retain their existing `[remap]` and explicit Mouse Support value. Missing new switches
resolve to on, as requested; enabling the feature is not contingent on first opening Controller
Settings. Explain in help that General Mapping customizations continue to control menus, while
gameplay now uses its own scheme; turning the new switch off restores use of the saved preset.
The old behavior in which Mouse Support off allowed raw mouse bindings is intentionally superseded
by the new global source gate.

The UI ownership follows the schema's draw order: **Mouse Support** and **Keyboard Support** are
rows in the existing `Input` group, while the Gameplay Mapping switch is drawn by the custom block.
[shell.cpp](../../../controller-settings/src/shell.cpp) draws schema rows first (`DrawTab`) and the
block after them, so a schema row for the switch would land above the General table. P4 either
keeps that split or adds ordered group interleaving; it must not leave the switch at the top.

The schema's own note at line 652 ("Keyboard support is always on and is not a setting (R13)") is
superseded here, as is the finished launcher plan's R13. Rewrite the note and the `mouse_ui_nav`
tooltip when P4 lands; do not edit the finished plan. The unbuilt `Keyboard` group's note ("Keyboard
mapping is not editable in this build") stays true: it is about mapping, not the support gate; P4
should keep the two words distinct in help.

Sequencing guard: until P3's bypass passes acceptance, force the effective mode off (or keep the
compiled default false in development builds) so no build hides the native Controller category
before direct actions work. The shipping default flips to true only with P3/P5 acceptance.

Implement `[gameplay_remap]` across parsing, surgical serialization, fresh-file serialization,
dirty comparison, rebased saves, profile-wide reset, and table-scoped resets. Preserve unknown
tables/rows and malformed or future binding syntax without silently converting it into valid
actions. Distinguish invalid input from a deliberately empty binding and report invalid rows.
The reader and UI must agree on the fallback used for a rejected row. An additive table should
not require a profile version bump if older readers preserve it; verify that rather than assuming.

Use compiled input defaults when no profile exists, without creating a profile merely to play.
Handle an unreadable profile consistently with the existing non-fatal boot policy and diagnostics.
Do not silently disable gameplay mapping for missing data while still hiding the native category.

### D7. Apply on the next game launch; preview edits immediately

Proposed first implementation: keep input configuration launch-scoped, matching the current
Mouse Support row and the remapper's one-time profile read. Save persists changes; Play Game
saves and launches with them. A game already running keeps its current configuration until restart.
State this in the settings help/status rather than implying an immediate runtime change.

Within Controller Settings, edited toggle values immediately update strikethroughs, gameplay-table
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
  Map inputs to controller buttons. When Gameplay Mapping is on,
  these bindings control menus only.     [?]
  [existing controller-button table]

Gameplay Mapping
  Bind inputs directly to gameplay actions, replacing the game's
  controller preset.                    [?]
  Enable Gameplay Mapping               [On]
  [six-action table, visible only when enabled]
```

Both `?` controls open a persistent explanation on click or keyboard/controller activation;
hover may also preview it. Add them to the focus model and support Back/Escape to close without
editing bindings. The explanations cover:

| Help | Content |
| --- | --- |
| General Mapping | Inputs emulate controller buttons; several sources may be bound; enabled Gameplay Mapping limits this table to UI; disabled Gameplay Mapping lets the saved native preset interpret these buttons during play; native pointer gestures own menu mouse interaction; Assign/Reset/Save behavior. |
| Gameplay Mapping | Inputs trigger the six actions directly; native presets are inactive and their category is hidden while enabled; the saved preset is retained; disabling restores it; Pause changes context to General Mapping; disabled device bindings are kept; changes take effect on next game launch. |

Extract a shared table/binding renderer instead of copying `ControllerTab::Draw`. Keep the
three-column arrangement, wrapping, Assign, Reset, and a table-scoped Reset all. Use unique ImGui
IDs across tables and one capture session across both. Capture adds a source to the effective
existing list, deduplicates it, and does not steal the activation click/press. Check the current
`CommitCapture` use of `Bindings()` versus `Effective()` so first assignment does not unexpectedly
drop displayed defaults.

Both `BindingText` and `ImGui::TextUnformatted` flatten a row into one string; replace them with a
span renderer that measures every source and draws the strike per wrapped line. There is no strike
helper in `row_ui`/`schema_view` today, so add one and unit-test its width and line arithmetic. The
current `?` is a `SmallButton` plus an `IsItemHovered` tooltip outside the focus ring; the
persistent help needs real ring entries and a modal owner in the shell.

Render bindings as individually styled spans with measured text widths and strike lines, including
wrapped lines. Use `ImGuiCol_TextDisabled`, the subtitle color, for disabled sources. Separators
and enabled sources must remain readable. A pad-plus-key row becomes normal pad text plus a
crossed key, not one wholly disabled string. A short tooltip can name the disabled source switch.
Binding presence and input availability are separate facts; keep editing/capture available in
the utility even when that source is disabled for the game.

Build the focus entries and draw order from one visible layout model. Hiding the gameplay table
must cancel capture in that table, move focus to its toggle if necessary, and preserve bottom-bar
navigation. Add help-modal ownership to the shell's existing modal/listening checks. Test small
windows, long binding lists, both app surfaces, and all three navigation devices.

`ControllerTab::kRowCount` is a compile-time count and `Shell` sizes `rings_` once in its
constructor (`shell.cpp:191-206`), so a hidden table would keep dead focus entries. Replace that
with a runtime row count and a `Shell::RebuildRings()` (or equivalent) called on a mode change,
Reset/import, and profile reload; move focus to the switch before removing the row it occupied.
`Escape` closes help before it closes the window, through the existing modal/listening checks.
Keyboard Support off affects only the game runtime: Controller Settings' own `[nav]` keys and
capture stay usable.

## 5. Hide the native controller category

Add an effective gameplay-mode value to `MenuConfiguration`, supplied before menu records are
built. Filter the `controls` route from the merged root and `pause_controls` from the paused inline
list using the same decision. Apply it to both provider-active and base lists, and to generated
in-song counterparts. Keep the pause panel's existing focus-based Help & Options route; the
historical reasons are recorded in [settings/menu.md](../../settings/menu.md), part 1.

This is an automatic ownership rule, independent of Hide Items and Ultimate availability. Do not
remove the controller configuration assets, mutate installed archives, or change the saved preset.
Disabling gameplay mode regenerates the original category in its original order. Update help
observations/navigation assumptions that depend on fixed row indices, and test reopening the
list, Back, selection clamping, and returning to gameplay. If an alternate retail/provider route
can still expose controller presets, cover it in the same visibility policy.

`docs/settings/plan.md` §3.2 and its row-position statement ("Controls at index 1; nothing about
them changes") describe the always-visible case and must be updated with this rule. The route-driven
scripts that assume fixed indices (`New-UiHelpRoute 1|2|3`) keep their calibration and audio/video
targets, but a run with the mode on must not assume the Controls row exists.

## 6. Work packages and dependencies

| Package | Work and likely owners | Completion evidence |
| --- | --- | --- |
| P0. Prove the gameplay seam | Trace native presets and all six consumers from local guest code/assets. Record address, calling convention, owning object/thread, context, timing, and press/hold/release semantics. Exercise buttons, triggers, sticks, and Start. Check base and Ultimate, tutorial gameplay, and Pause separately. Prove the six-action vocabulary is complete and list every extra input path (tutorial skip, calibration confirm, song end) with an inert-or-bind decision. Decide whether an analog stick direction is needed as a bindable `pad:` source. Owners: `src/input/`, prospective `src/hooks/gameplay_input.*`, `config/functions.toml` only for justified symbol names, `docs/reference/symbols.md` for newly named symbols. | A per-action evidence table including vocabulary completeness, before/after traces proving a candidate bypass suppresses every native gameplay producer, and a prototype direct action that preserves native judgement. Record unresolved paths explicitly; P3 cannot ship on an unproven bypass. |
| P1. Add shared configuration | Add the gameplay action table/parser and defaults, support switches/allowlist, profile round trips and rebased save/reset, diagnostics, and old-profile behavior. Likely new `src/profile/gameplay_remap.*`; extend `src/profile/profile.*`, `src/settings/cvars.h`, `src/profile/profile_apply.*`, `controller-settings/src/profile_session.*`. Register the new source in `RBBLITZ_SOURCES` and `controller-settings/CMakeLists.txt`; keep `Profile` storage-only so the installer's `profile.cpp` copy needs no new file. Add `keyboard_support` as a schema row in the `Input` group beside the existing Mouse Support row (`argv = "flag"`), draw the Gameplay Mapping switch in the custom block, rewrite the R13 note, and log the effective triple. | SDK-free tests for absent/empty/invalid/unknown rows, multi-source bindings, independent tables, defaults, explicit false values, concurrent saves, and a no-profile boot configuration. Freeze actual default bindings after P0. A standalone boot's log names the effective switches and binding count. |
| P2. Unify physical input and gates | Share physical snapshots/edges, focus and capture policy, player ownership, and synthetic connectivity; isolate legacy MnK. Extend `src/input/remap.*`, `mouse_ui.*`, `native_menu.*`, and input-factory wiring in `src/rb_blitz_app.h`. Add an SDK patch only if needed; `patches/README.md` gains its row when one lands. | Keyboard-off and mouse-off tests across all producers, mixed device input, no-controller startup, SDL/XInput, final packet identity, source provenance, no stale input after boundaries, the OS cursor stays hidden in gameplay, and a gameplay mouse press never reaches the native-menu queue. |
| P3. Dispatch direct gameplay actions | Implement the P0-proven guest adapter and native bypass, action-specific edge/hold behavior, pause routing, and context lifecycle. Keep off mode on the original path. Integrate with `CMakeLists.txt`; do not edit generated recompiler output. | All six actions work through arbitrary bindings, never through a conflicting native preset; simultaneous sources and rapid notes behave correctly; saved preset remains unchanged. |
| P4. Build the settings surface | Add the Keyboard Support schema row and the ordered sections; share rendering/capture; add clickable help; strike individual sources; conditionally hide actions; rebuild focus layout. Owners: `controller-settings/config/settings.toml`, `controller_tab.*`, `shell.*`, `schema_view.*`, `row_ui.*`, and shared UI helpers as needed. Replace the one-time ring sizing with a runtime layout, add the help modal to the shell's focus model, and update `--dump-layout` plus the capture/acceptance scripts that assert old rows. | Screenshots and navigation traces for both mode states and all device-toggle combinations; capture/reset/Save/Play persists exactly what runtime reads; hidden rows are never focusable; the utility stays navigable with a source disabled. |
| P5. Apply native category visibility | Pass effective mode into menu generation; filter both `controls` and `pause_controls`, keeping provider gating independent. Owners: `src/ui/settings_screen.*`, `src/hooks/settings_payload.cpp`, `docs/settings/plan.md` §3.2, relevant help observers/tests. | Generated menu-tree tests and live main-menu/pause screenshots with gameplay mode on/off for base and Ultimate; preset value and Back routes preserved; the row-position spec and its scripts agree with the conditional list. |
| P6. Integrated acceptance and docs | Build game and utility, run targeted suites and live song checks, verify launch/restart/profile behavior and installed payload, then update owning docs. | Acceptance matrix below passes with evidence under `out/gameplay-mapping/`. Update, as behavior lands: `docs/engine/input-flow.md` (raw-mouse paragraph, keyboard line, a new live-acceptance section), `docs/engine/toggles.md` (`mouse_ui_nav` row: no longer "hidden and inactive during gameplay"; add `keyboard_support` if it joins the toggle registry), `docs/reference/known-issues.md` (R6 row), `docs/settings/contracts.md` §8 (allowlist), `docs/settings/plan.md` §3.2 and the row-position rule, `docs/settings/menu.md` and `docs/settings/status.md`, `docs/reference/symbols.md` (P0 names), `docs/README.md` (index), `controller-settings/README.md`, and `patches/README.md` if a patch lands. |

Order: **P0 -> P1 -> P2 -> P3**, with **P4 after P1** and **P5 after the shared mode contract in
P1**. P6 requires all packages. The main uncertainty is P0/P3; do not describe this as only adding
a second mapping table. P5 must not be effective while P3 is unproven: the mode is forced off (or
its compiled default stays false) until then, so a build cannot hide the native category without a
working replacement. During development, enabling the shipping default waits until all six
actions, exclusive routing, and both menu entry paths pass acceptance together.

## 7. Validation and release criteria

### 7.1 Automated checks

Extend meaningful existing tests for `controller_settings_profile`, `controller_settings_session`,
`controller_settings_remap`, `controller_settings_nav`, `settings_service`, `settings_screen`,
`menu_payload`, and `native_mouse`. Add a focused pure gameplay evaluator/source-gating suite
for the new semantics. Register it in CMake. Include:

- Same source in both tables, different actions from concurrent devices, shared-action holds,
  simultaneous note strikes, rapid alternating sources, opposite lanes, and repeated guest polls.
- Keyboard/mouse gates on defaults and explicit bindings; physical pad input survives a disabled
  keyboard mapped to the same button; legacy MnK cannot impersonate `pad:`.
- A General Mapping remap cannot affect gameplay in direct mode. A native preset or unused
  physical stick cannot fire any of the six actions. Off mode never calls the direct dispatcher.
- Focus loss, host capture, disconnection, and pause/resume clear pending events and require
  appropriate rearming. UI acceptance cannot leak into the first gameplay frame or vice versa.
- Profile defaults, explicit empty bindings, unknown/malformed rows, old profiles, no profile,
  concurrent edits, table-scoped resets, and ordinary direct launch without the utility.
- Utility isolation: with keyboard or mouse support off, Controller Settings' own keys and capture
  still work, and Save persists the rows.
- Layout/focus: hidden gameplay rows are never focusable; toggling rebuilds the ring; the help
  modal owns Escape; the bottom bar stays reachable.
- Shared-source policy: one source on two actions fires both and is reported; one source in both
  tables follows the context table.
- Cursor/pointer: a gameplay mouse press does not enter the native-menu queue and the cursor stays
  hidden in gameplay.
- Schema/launch: `--dump-layout` names the new rows, the switches round-trip through the profile,
  and a standalone boot receives them through the allowlist.
- Main and pause category lists across gameplay mode x provider state, including valid Back and
  selection behavior. Profile/CLI override tests prove menu visibility and input use one mode.

Run the configured build and targeted CTest suites using
[build-and-run.md](../../guides/build-and-run.md). Confirm the chosen build tree actually registers
tests with `ctest --test-dir <build-dir> -N`; [input-flow.md](../../engine/input-flow.md) records a
Vulkan-enabled configure that disabled `BUILD_TESTING`. P6 also requires real gameplay; passing
pure evaluator or generated-menu tests alone cannot validate the guest hooks.

### 7.2 Live acceptance matrix

| Scenario | Required observation |
| --- | --- |
| Fresh profile, no physical pad | Keyboard can boot, navigate, play all six actions, pause, resume, and exit; defaults are effective without opening Controller Settings. |
| Mouse off, keyboard on, no pad | Keyboard remains connected and usable; all mouse buttons, motion, and wheel have no game effect. |
| Keyboard off, mouse/pad on | Keys cannot navigate, strike, move lanes, trigger powerup, or pause; equivalent physical pad inputs still work. |
| Both keyboard and mouse off | Controller gameplay and UI remain usable; every keyboard/mouse binding is retained and crossed in the utility. |
| Mixed inputs | Pad, keyboard, and mouse can perform different actions together, and alternative sources for one action obey the measured edge/hold rule without duplicates or stuck releases. |
| Native preset collision | Save each available native preset in turn with mode off, then enable custom bindings that deliberately conflict. Only custom actions occur, including with sticks/triggers/Start. Re-disable and verify the same saved preset resumes control. |
| Pause boundary | A custom Pause binding pauses once. General Mapping controls the menu. Holding/clicking across Pause/Resume does not fire a note, powerup, menu confirm, or second pause. |
| Menu visibility | Native Controller/Controls is absent in main-menu and paused Help & Options when enabled, present in both when disabled, on base and Ultimate. Other categories and Back still work. |
| Utility editing | Three sections, both help buttons, mixed crossed/normal bindings, table hide/show, capture cancellation, reset isolation, wrapped text, keyboard/controller focus, and Save/Play all behave as specified. |
| Utility with a source disabled | Controller Settings navigates and captures with Keyboard Support off (a separate process); Save persists; the game ignores those keys. |
| Mouse in gameplay | With support on, the cursor stays hidden, a bound mouse button performs its action without moving a menu selection, and native hover returns on pause. |
| Layout and help | Hiding/showing the table while focused, opening/closing both help modals, and `--dump-layout` row counts all behave; no hidden row receives focus. |
| Runtime boundaries | Alt-tab, modal capture, controller unplug/replug, loading, song end, and repeated songs leave no held inputs or stale queued actions. |
| Save and launch | Explicit false values and custom rows survive restart; direct launch and utility launch agree; existing General Mapping and native save data remain intact. |
| Timing | Compare native mode and direct mode using the same song/calibration and reproducible input sequence; log sampling/dispatch timing and note outcomes. No added frame wait, lost short taps, extra repeats, or altered calibration. |

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
| O3 | Initial action bindings | P0/P1 publishes complete pad and keyboard defaults. The sample TOML is not a default recommendation or evidence of a native preset. |
| O4 | Context coverage | Classify tutorials, countdown/loading prompts, nested pause screens, calibration, song end, and Ultimate behavior from guest state; confirm no UI-only route can trigger gameplay. |
| O5 | Source provenance and backend compatibility | P2 proves how physical pad state stays separate from SDK MnK and fixed menu pulses on SDL/XInput, with keyboard-only connectivity. |
| O6 | Guest keystrokes and alternate controls | Trace whether any gameplay/UI consumer uses `GetKeystroke`, direct keyboard input, or a separate pause shortcut; gate or reroute every observed consumer. |
| O7 | Vocabulary completeness | P0 confirms the six actions cover every native gameplay input and records any extra path with an inert-or-bind decision. |
| O8 | Analog direction sources | P0 decides whether a stick direction must be bindable; if yes, the vocabulary, threshold, capture, and serialization are designed before P1 freezes defaults. |

The launch-scoped apply policy, the exact binding defaults, the duplicate-source policy (allow and
note), and the development sequencing guard are proposed product decisions, not behaviors
requested explicitly. All other decisions above implement the requested separation, source
toggles, native-preset preservation, and visibility rules.

## 9. Planning-pass verification

Source inspection only; no game build, song run, or timing measurement was performed for this
document. The pass-2 run on 2026-10-17 reported 0 errors and 29 known warnings (the allowlisted
historical links) after the plan was normalized to CRLF and added to the index. Re-run after
editing:

```powershell
python .github/skills/documentation/scripts/check_docs.py
git diff --check
```

Keep this plan in `todo/` until feature implementation starts. When it does, move it to
`in-progress/` and maintain P0-P6 evidence here without treating planned checks as passed.
