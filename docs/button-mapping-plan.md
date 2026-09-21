# Custom button mapping — scoping plan

**Status: scoping only.** No code in this document. Nothing here is a commitment, and nothing here
gates the Windows recompilation milestones.

**Evidence convention**, same as [vr-port-plan.md](./vr-port-plan.md) and
[av-settings-plan.md](./av-settings-plan.md):

- **[tree]** — read directly out of this working tree, with `file:line`.
- **[cited]** — observed from a running artifact: the built game, its logs, its captures.
  Reproduce with `scripts/drive_ui.ps1`.
- **[assumed]** — reasoned, not yet verified. Every **[assumed]** is a task for milestone IM0.

---

## 1. The request

Let the player build their own control layout by hand, emulator-style: choose an action, press
**Listen**, and have the game take the next press from **either a controller or a keyboard**. The
four control presets that already ship must keep working — a custom layout is an *addition*, never a
replacement. Requested UI shape: controllers move to their own panel with a **Custom
enabled/disabled** switch, and below that switch one button for **Preset** and another for **Manual**.

To plan that honestly, the document has to answer three separate questions:

1. What can be rebound **today**, and by whom? (§1.2, §1.3)
2. Where can a new binding layer actually attach, given that both the game and the host each own
   half of the mapping? (§2)
3. What does "custom" have to *mean* so that the existing presets cannot be broken? (§3)

The short answer, which the rest of this document supports, is that the guest only ever sees a
synthetic Xbox pad — so a single host-side layer that decides **which physical source drives which
pad field** is enough to deliver the whole feature, and it can be built without touching a single
guest asset.

### 1.1 The two binding universes

| | Universe A — the host | Universe B — the guest |
| --- | --- | --- |
| Owner | `rexglue-sdk` input drivers + this repo's cvars | the game's own Flow/Liquid content |
| Unit | one *logical pad control* (A, LB, left stick…) | one *action* ("Play Left Note") |
| Mapping | physical source → logical control | logical control → action |
| How many | 25 keyboard bindings, plus hard-coded pad tables | **4 fixed presets** you cannot edit |
| Editable today? | yes, via cvars (no UI, no capture, keyboard only) | **no** — the screen is read-only |

Both halves are needed to answer "what does pressing LB do?", and neither half can see the other.
Everything below is about adding a third table — **physical source → logical control, for pads as
well as keyboards** — without disturbing either.

### 1.2 Universe A — the host: 25 keyboard bindings and a hard-coded pad

The host exposes exactly one rebindable family, `keybind_*`, all 25 defined in one block:
`rexglue-sdk/src/input/mnk/mnk_input_driver.cpp:33-59` **[tree]**.

| Logical control | cvar | Default **[tree]** |
| --- | --- | --- |
| `A` | `keybind_a` | `Semicolon,Space` (`:33`) |
| `B` | `keybind_b` | `Quote,Backspace` (`:34`) |
| `X` | `keybind_x` | `L` (`:35`) |
| `Y` | `keybind_y` | `P` (`:36`) |
| Left trigger | `keybind_left_trigger` | `Q,I` (`:37`) |
| Right trigger | `keybind_right_trigger` | `E,O` (`:38`) |
| Left shoulder | `keybind_left_shoulder` | `1` (`:39`) |
| Right shoulder | `keybind_right_shoulder` | `3` (`:40`) |
| Left stick up/down/left/right | `keybind_lstick_*` | `W` / `S` / `A` / `D` (`:41-44`) |
| Left stick press | `keybind_lstick_press` | `F` (`:45`) |
| Right stick up/down/left/right | `keybind_rstick_*` | `Up` / `Down` / `Left` / `Right` (`:46-49`) |
| Right stick press | `keybind_rstick_press` | `K` (`:51`) |
| D-pad up/down/left/right | `keybind_dpad_*` | `Shift+Up` / `Shift+Down` / `Shift+Left` / `Shift+Right` (`:52-55`) |
| Back | `keybind_back` | `Z,Tab` (`:57`) |
| Start | `keybind_start` | `X,Return` (`:58`) |
| Guide | `keybind_guide` | *(empty — unbound by default)* (`:59`) |

This family is the complete set of "one pad button, one keyboard key" remaps that exists. Four
properties of it matter for the plan, and all four are *good news*:

- **The syntax is already a lists-of-alternatives grammar.** `IsBindPressed` splits a value on `','`
  (`:203`, loop at `:215`) and each token is tried independently, so `"Z,Tab"` means "Back is either
  Z or Tab". That is exactly the shape a custom profile needs.
- **Modifiers exist and match exactly.** `TakeModifiers` recognises `Shift` / `Ctrl` / `Control` /
  `Alt` (`:91`) and `TokenPressed` requires an exact match (`:125-126`), which is why `Up` and
  `Shift+Up` can coexist as right-stick-up and D-pad-up.
- **The keyboard names are a reusable table.** `keybinds.cpp:22` holds `kKeyNames`, and
  `ParseVirtualKey` (`:146-148`) turns a name into a `VirtualKey`, returning `kNone` for anything
  unknown. There is even a reverse lookup for display at `:152`.
- **The shipped defaults are conflict-free** — no key name appears in two different bindings
  **[tree]**, derived from the table above. A custom profile can therefore be *seeded* from the
  defaults and edited, rather than authored from nothing.

What the host does **not** have: any notion of a preset, a layout, a profile, a controller type, or
"the user's pad". The pad half is compiled in (see §2.4).

### 1.3 Universe B — the guest: four presets on one screen

The game has its own controller screen, reached from `HELP & OPTIONS → Controls`. It is driven
entirely by guest-side content; this repository contains none of it. Captured from the running build
**[cited]** (`out/drive-ui/`, gitignored; re-capture with `scripts/drive_ui.ps1`):

- Header string `CONTROLLER`, one preset-name row, a read-only action table below it, footer
  `SELECT` only — **no `BACK`**, because `A` on this screen is confirm **and exit**.
- The preset-name row is the screen's **only** interactive element.

**There are exactly four presets, in a cycle of period four.** Cycled right seven times, capturing
after each step, and the capture after step 3 was byte-identical to the one after step 7:

| Steps right from the initial state | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Rendered preset | Shoulders | Typewriter | Default | Freakish | Shoulders | Typewriter | Default |

So the ring is `Freakish → Shoulders → Typewriter → Default → Freakish` (period 4), and a second
test — three right presses from `Default`, which landed on `Typewriter` — is consistent with it.
There is no fifth entry and no "Custom".

**The presets do not merely move buttons: they change the action set.** Two of the four merge the
two track-switch actions into one:

| Preset | Rows rendered | Track switching |
| --- | --- | --- |
| `Default` | 5 | `Track Switch Left`, `Track Switch Right` *(separate)* |
| `Shoulders` | 5 | `Track Switch Left`, `Track Switch Right` *(separate)* |
| `Typewriter` | 4 | `Track Switch Left/Right` *(merged)* |
| `Freakish` | 4 | `Track Switch Left/Right` *(merged)* |

The five-row sets are, in order: `Track Switch Left`, `Play Left Note`, `Track Switch Right`,
`Deploy Power-up`, `Play Right Note`. Any host-side editor that offers "the actions" has to decide
which of these two action sets it is describing — see §3.7.

**Up and down do nothing on this screen.** Pressing down, then pressing up three times, produced
frames whose only difference from the baseline is the animated background band
(`0,69–1359,170`, 4.951 % of pixels, identical for both moves) **[cited]**. So there is no vertical
cursor and no scrolling: the action table cannot be selected, let alone rebound, in-game today.

**Selection is remembered in-session but was not written to the guest's storage while we watched.**
Setting `Typewriter`, pressing `A`, backing out to the main menu and re-entering `Controls` still
showed `Typewriter` **[cited]**; the guest's `globaloptions` container (1024 bytes, see
[av-settings-plan.md](./av-settings-plan.md) §1.2) kept its previous modification time through the
change, so either the write is deferred to exit or the preset lives somewhere else [assumed].

**It was also not written at exit.** The same session was then closed gracefully (`CloseMainWindow`),
and the container still carried a timestamp from **hours before the run started**, not one from
shutdown **[cited]**. So "deferred to exit" is now the weaker half of that hypothesis. The remaining
ambiguity is that the whole session ran in **Offline Mode**, so "not written" cannot yet be separated
from "no profile to write into" — which is exactly what IM0 would have to control for.

### 1.4 How the guest's presets are built

The preset names appear **nowhere** in the shipped data as plain text: searching the install for
`typewriter`, `freakish` or `shoulders` returns nothing even with ignore rules disabled, while a
control string (`blitz`) is found in the same files. The names are therefore locale or text-token
driven, and the guest's own locale bundle `resource/lists/list_locale.milo` does exist in the same
archive **[tree]**. (The retail image `game/default.xex` has no readable plain text at all — it is
compressed or encrypted — while `game/ultimate/default.xex` does [assumed]; either way the preset
table is not statically readable by grepping the base image.)

What *is* reachable is the archive's name table and the internal strings of the options-menu bundle
in `game/gen/main_xbox_0.ark` **[tree]**, which name the implementation directly:

| Asset | What it implies |
| --- | --- |
| `controller_config_panel` | the screen itself, a `PanelDir` |
| `controller_mapping_state.ep` | a `PropertyEventProvider` with properties `last_map`, `direction`, `showing`, `text_token`, `none`. `last_map` is the selected preset; `text_token` is how the *name* is looked up, confirming §1.4's locale conclusion. |
| `controller_preset_0` … `controller_preset_3` | **four preset objects, and no `controller_preset_4`** |
| `set_0.grp` … `set_3.grp`, `all.grp` | one group per preset |
| `ps_0.tex` … `ps_3.tex`, `xbox_0.tex` … `xbox_3.tex` (+ `ps.mat`/`xbox.mat`, `ps.mesh`/`xbox.mesh`) | per-preset glyph atlases for **two controller families**, PlayStation and Xbox |
| `arrow_lt(_glow).*`, `arrow_rt(_glow).*` | the left/right arrows that step the preset row |
| `header.lbl`, `IndexNum.lbl` | the `CONTROLLER` header and the index indicator |
| `deploy.lbl`, `deploy_2_lt.lbl`, `deploy_2_rt.lbl` | power-up labels, unified and split |
| `switch_track_lt_0/2.lbl`, `switch_track_rt_0/2.lbl`, `switch_track_uni_1/2.lbl` | track-switch labels, split **and** unified |
| `smash_lt_0..3.lbl`, `smash_rt_0..3.lbl` | the note labels, indexed 0–3 |
| `UpdateControllerLayout.flow`, `UpdateControllerLayout1.flow`, `glow_lt.flow`, `glow_rt.flow` | the script that rebuilds the table when `last_map` changes |

Two things fall out of that naming, and both are load-bearing for this plan:

1. **`{row}_{presetIndex}.lbl` confirms four slots** — 4 rows × 4 presets of labels, and no index 4
   anywhere. Adding a fifth preset is not a data edit; it is new content in a shipped archive.
2. **The action set is per-preset data, not per-preset code** — the split (`_lt`/`_rt`) and unified
   (`_uni`) label families both exist, which is exactly the 5-row versus 4-row difference §1.3
   measured. An index that carries *both* families (index 2 does) is anomalous, so the
   index ↔ displayed-name mapping is **not yet established** and is a task for IM0.

Finally, the sibling fact from [rb3-references.md](./rb3-references.md): the Rock Band 3 ports
remapped controls by editing a data file, `config/joypad.dta`, whose `button_meanings` block maps
actions to buttons (`:274-276` **[tree]**), and a missing block there caused a silent total input
failure. **Blitz has no such file.** There is no `joypad` entry and no `.dta` mapping table anywhere
in the shipped data or the archive's name table **[tree]**. Blitz's mapping is compiled into the
guest and into the Flow above, so the RB3 data-override route does not exist here and cannot be
borrowed.

### 1.5 Why the two universes do not meet — and the one thing that does

Normally "the host cannot see the guest" is a wall. Here it is the opposite, for one specific
reason.

The keyboard/mouse driver does not hand the guest keys. It **synthesizes a pad**, field by field —
`IsBindPressed(...)` per button (`mnk_input_driver.cpp:278-298`), triggers as `0xFF`/`0`
(`:309-310`), stick axes from the per-direction binds (`:314-331`) — into the same
`X_INPUT_GAMEPAD` structure a real controller produces. And the second backend does the literal
opposite, passing hardware through untouched (`xinput_input_driver.cpp:219`). The guest's input API
therefore has exactly one shape:

```
any physical device ──► [ drivers: keyboard→pad, pad→pad ] ──► one X_INPUT_GAMEPAD ──► guest
```

Three consequences decide this whole plan:

1. **The guest cannot tell a keyboard from a controller.** It has no such concept, so "listen for
   either" is not something we ask the guest to support — it is something we implement once, in the
   layer that produces the pad.
2. **A host-side remap layer is therefore *complete*.** Every action the guest can perform arrives
   through a pad field, so rebinding "which source produces pad field X" covers 100 % of the
   reachable input surface, with no guest cooperation and no guest assets.
3. **The guest's preset is a separate axis and stays that way.** The four presets map pad fields to
   actions; the custom layer maps sources to pad fields. They compose instead of competing, which is
   precisely how the "[do not] kill the presets" requirement is satisfied — see §3.1.

The one thing that does *not* follow is visibility: nothing today lets the host read the guest's
`last_map`, so a host panel cannot show which preset the game thinks is active, nor set it. §4
treats that as a costed option (H4/H5), not a founding premise.

---

## 2. What the SDK already gives us

This section exists so the milestones can be costed. The short version: *the vocabulary, the
liveness, and the exact insertion point are all already in place; the capture UI and the pad-side
grammar are the work.*

### 2.1 The vocabulary we can reuse

| Piece | Where | What it gives us |
| --- | --- | --- |
| Key-name table | `rexglue-sdk/src/ui/keybinds.cpp:22` | `kKeyNames`, the full name → `VirtualKey` table |
| Parse / reject | `:146-148` | `ParseVirtualKey`, returns `kNone` for unknown names — already fails closed |
| Reverse lookup | `:152` | key → name, i.e. how to *display* a binding |
| Alternatives + modifiers | `mnk_input_driver.cpp:91`, `:203-219` | `Shift+`-style prefixes, comma-separated alternatives |
| Existing 25 bindings | `:33-59` | a working, conflict-free default set to seed a profile from |

### 2.2 Keybinds are live, by construction

There is no cache. Every poll re-reads the cvar — `REXCVAR_GET(keybind_a)` and friends inside
`GetDeviceState` (`mnk_input_driver.cpp:278-331`) **[tree]**. Changing a binding takes effect on the
next poll, with no restart and no callback registration. (`RegisterChangeCallback` is not used
anywhere in input code, and it would be pointless here.)

This is the single most encouraging fact in the document: the existing design is already
"re-evaluate the mapping every frame", which is what a capture UI needs.

### 2.3 There is no capture mode — but the hook points exist

Nothing in the input layer listens for "the next press". The machinery it would attach to is
present:

| Source | Hook point | Notes |
| --- | --- | --- |
| Keyboard | `MnkInputDriver::OnKeyDown` (`mnk_input_driver.cpp:483`), `OnKeyUp` (`:491`), both filling `key_down_[256]` via `SetKeyState` (`:477`) | the natural capture point; timing-sensitive (see §2.5) |
| Mouse | same driver, mouse button paths around `:388-477` | mouse buttons are bindable today as `LMB`/`RMB`/`MMB` |
| SDL pad | `SDLInputDriver::ProcessEventLocked` (`sdl_input_driver.cpp:423`), event switch at `:434` (`SDL_EVENT_GAMEPAD_BUTTON_DOWN`, …) | arriving via the `:418` call site; gives button *and* axis events |
| XInput pad | none — it is a poll, not an event (`xinput_input_driver.cpp:219`) | capture must diff successive `X_INPUT_STATE`s instead |

Two backends with two different capture shapes is a real cost, and it is why §4 prefers a layer that
sits *after* both of them.

### 2.4 Physical pad buttons cannot be remapped today

Two hard-coded tables decide which guest field a physical button drives:

- SDL: `xbutton_lookup` (`sdl_input_driver.cpp:531-…`, `X_INPUT_GAMEPAD_A` at `:533` and `:556`;
  applied at `:572-577`) plus a fixed axis `switch`, and a `guide_button` gate at `:580`.
- XInput: a straight copy — `out_state->gamepad.buttons = native_state.state.Gamepad.wButtons`
  (`xinput_input_driver.cpp:219`; capabilities likewise at `:176`).

The only knobs are `guide_button` (`input_system.cpp:30`) and `hid_mappings_file`
(`sdl_input_driver.cpp:23`, an SDL GameControllerDB path applied at `:99-109`). Neither moves A onto
B: the GameControllerDB path renames *raw hardware* into SDL's names, and the guest is handed the
resulting fixed Xbox layout regardless. Note also `:660`, where the guest is told every button
exists (`0xF3FF`) — the guest believes it has a full pad, which is what makes a custom layer
necessary rather than merely convenient.

### 2.5 Where the guest actually receives the state — the choke point

Every guest poll of a pad ends in one function:

`rexglue-sdk/src/kernel/xam/xam_input.cpp:96` — `XamInputGetState_entry`, which normalizes the user
index (`:109-116`, folding `0xFF`/`XINPUT_FLAG_ANY_USER` onto user 0) and then calls
`is->GetState(actual_user_index, input_state)` at `:116` **[tree]**.

That call writes the final pad state straight into guest memory, so a mutation immediately after it
is what the guest reads, for **every** backend and **every** device, in one place.

Better still, **this repository already patches exactly this function.** Patch
`patches/rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch` adds a trace in
`XamInputGetState_entry` that logs `user=`, `flags=`, `result=` and **the button word the guest was
handed** (`:123-129` in the patched file) **[tree]**. That means the remap layer's verification
method already exists: exercise an input, read the logged button word, assert the bit. No
screenshots, no OCR — the same style of log-derived verdict the project already uses for pacing
([known-issues.md](./known-issues.md) `:17`).

Also present in the same file: `XamInputGetKeystroke_entry` (`:152`). If any guest text-entry or
menu path reads keystrokes rather than pad state, a state-only remap has a hole there — an IM0 task,
not an assumption.

### 2.6 Device assignment and merging

| Piece | Where | Behaviour |
| --- | --- | --- |
| `DeviceAssignment` (abstract) | `include/rex/input/device_assignment.h:20`, query at `:28` | "which devices feed guest user N" |
| `SlotAssignment` | `:32` | device ordinal N feeds guest user N; synthetic devices feed user 0 |
| `SharedAssignment` | `:43` | every device feeds user 0 |
| Selection | `input_system.cpp:354` | `SetDeviceAssignment(std::make_unique<SlotAssignment>())` — **programmatic, not a cvar** |
| Merge | `include/rex/input/state_merge.h:24-26` | `MergeInto`: buttons OR, triggers max, stick axes by larger magnitude, newest packet number |
| Active-device tracking | `state_merge.h:32` | `ActiveDeviceTracker` remembers the most recent non-neutral device |
| Backend choice | `input_system.cpp:27` | `input_backend` = `sdl` \| `xinput` |

Note the comment at `input_system.cpp:36` about synthetic devices and slot routing: the keyboard
driver's synthetic pad is routed deliberately. A per-device custom profile has to respect that
routing, which is why §3.4 recommends keying profiles by device rather than by guest slot.

### 2.7 The UI surface

Reused wholesale from [av-settings-plan.md](./av-settings-plan.md) §2.3 **[tree]**:

- The **F4 settings overlay** (`rexglue-sdk/src/ui/overlay/settings_overlay.cpp`) enumerates every
  registered cvar automatically, so an added `remap_*` family becomes visible and editable for free.
  It is also the only writer: `SaveConfig` at `:506`.
- **Hotkeys** are registered through `rex::ui::RegisterBind` (`include/rex/ui/keybinds.h:64`,
  implementation `keybinds.cpp:171`) inside `ReXApp::SetupOverlays`
  (`rexglue-sdk/src/ui/rex_app.cpp:387-427`) — F3 debug, Backtick console, F4 settings, F7
  achievements. Existing binds are a *reserved key* list for the capture UI (§3.6).
- **A new dialog** needs `virtual void OnCreateDialogs(ui::ImGuiDrawer*)`
  (`include/rex/rex_app.h:108`, called at `rex_app.cpp:427`) — and this repo does not implement it
  yet, overriding only `OnPreSetup`, `OnConfigurePaths` and `OnPostLoadXexImage`
  (`src/rb_blitz_app.h:36`, `:50`, `:73`) **[tree]**. Adding a panel costs one virtual plus an
  `ImGuiDialog` subclass (`OnShow`/`OnClose`/`OnDraw`).

### 2.8 Prior art this repo already catalogued

| Technique | Source | Why it matters here |
| --- | --- | --- |
| `ControllerHook` at `0x825320B4` | [rb3-references.md](./rb3-references.md) `:155` — a midafter-instruction hook on `r11`, catalogued but **not implemented** | the one existing candidate for observing guest controller code; relevant to H5 (reading `last_map`), not needed for H1 |
| `button_meanings` / `config/joypad.dta` | `:274-276` | the RB3 approach, **absent from Blitz** (§1.4) — recorded so the plan does not try to borrow it |
| A git-tracked DTA overlay shadowing extracted assets on read | `:274` | a general "override shipped data" mechanism; would apply to H4, and is subject to checksum validation |
| `mnk_mode` in the build-tree-local profile | `:318`, plus [build-and-run.md](./build-and-run.md) `:182-183`, `:199` | the precedent for *where* this feature's configuration lives: `out/build/<preset>/rb_blitz.toml`, gitignored |
| Arrow keys are the right stick in this title | [build-and-run.md](./build-and-run.md) `:386` | matches §1.3's observation that menus use the left stick, and constrains which sources are worth binding |
| Pad input is the one hand-verified item | [known-issues.md](./known-issues.md) `:18` | every scripted route injects **keys**; a custom pad mapping has no scripted coverage today |

---

## 3. Binding model — what "custom" has to mean

### 3.1 Two layers, and the rule that protects the presets

The whole design in one table. Layer G is the game's; layer H is ours.

| | Layer G — actions | Layer H — sources *(new)* |
| --- | --- | --- |
| Maps | logical pad control → game action | physical source → logical pad control |
| Owned by | the guest, in `controller_preset_0..3` + Flow | this project |
| Values | 4 fixed presets, read-only in-game | `OFF` (default) or a user profile |
| Editable in game | no | yes — the requested panel |
| Breaks if wrong | the shipped game | nothing upstream of the pad |

**The rule:** layer H is *off* by default, and when it is off it must reproduce the pad state
byte-for-byte. With custom disabled, the guest receives exactly what it receives today, so all four
presets behave identically — the requirement is met by construction, not by care.

This is also why the host's "Preset" button cannot be the guest's preset selector: that would be
editing layer G from layer H. The recommended reading is below in §3.7, and the alternatives that
cross the line are costed as H4/H5 in §4.

### 3.2 Source grammar

A binding today is a string of comma-separated alternatives. The grammar needs one new token class;
everything else is already parsed by shipped code.

| Source class | Example tokens | Exists today? |
| --- | --- | --- |
| Keyboard key | `Semicolon`, `Space`, `Return`, `Tab` | yes — `kKeyNames` (`keybinds.cpp:22`) |
| Keyboard key + modifier | `Shift+Up`, `Ctrl+A`, `Alt+Return` | yes — `TakeModifiers` (`mnk_input_driver.cpp:91`) |
| Mouse button | `LMB`, `RMB`, `MMB` | yes |
| Mouse axis | — | no. `mnk_mouse`/`mnk_sensitivity` (`:27`, `:30`) drive the right stick, unrebindable |
| **Pad button** | `PadA`, `PadB`, `PadX`, `PadY`, `PadLB`, `PadRB`, `PadLS`, `PadRS`, `PadBack`, `PadStart` | **no** |
| **Pad d-pad direction** | `PadUp`, `PadDown`, `PadLeft`, `PadRight` | **no** |
| **Pad stick direction** | `PadLS-Up`, `PadRS-Left`, … | **no** |
| **Pad trigger as a button** | `PadLT`, `PadRT` | **no** — triggers are analog fields |
| Overlay hotkey (`F3`/`F4`/`` ` ``/`F7`) | — | deliberately excluded, §3.6 |

So the deliverable is a *second* parser for the `Pad*` class, plus a serializer, alongside the
existing key parser rather than replacing it. Design note: do **not** extend the SDK's `keybind_*`
cvars with pad tokens. It would be tidier in one table, but it changes the meaning of upstream's own
public cvars, and H1 does not need it — the keyboard half keeps using `keybind_*` unchanged and the
custom layer *reads* those same values, so a profile stays a superset of today's configuration
rather than a parallel one.

### 3.3 Capture semantics

The emulator behaviour, spelled out, because the details are where this feature is usually bad:

| Aspect | Recommendation | Why |
| --- | --- | --- |
| Arming | click `Listen` on one action row; that row is the only one armed | one press must resolve one row |
| Accepts | next pad button, pad axis past threshold, pad trigger past threshold, **or** keyboard key/mouse button | the request: "either controller or keyboard" |
| Resolution | first qualifying event wins; capture disarms immediately | avoids a racing second event stealing the binding |
| Second source | a row may hold **multiple** alternatives (`"PadX,Semicolon"`), added with repeated captures | matches the shipped `,`-alternatives grammar, lets keyboard and pad share an action |
| Timeout | disarm after ~2 s idle and say so in the row | otherwise a mis-click leaves input swallowed |
| Cancel | `Esc`, or clicking `Listen` again | |
| Conflict | a source already bound to another action prompts **Replace / Swap / Cancel** | the emulator convention, and it keeps the table injective |
| Analog → button | require a threshold **plus hysteresis**, configurable | sticks rest off-centre; without hysteresis a binding fires continuously |
| Triggers | treat as buttons with the same threshold rule; the guest's own fields stay analog | `X_INPUT_GAMEPAD.left_trigger` (`input.h:61`) is a byte, not a bit |

While a row is armed, the captured event must be **consumed** so it cannot also reach the game
(§5 IM0 task 2 verifies whether that is even necessary — it may already be true whenever a host
dialog holds focus).

### 3.4 Per device, or per player slot?

Recommendation: **per physical device**, with an optional "applies to any pad" fallback profile.

- A custom layout describes a *controller*, and `SlotAssignment` (`device_assignment.h:32`) maps
  device ordinal → guest user, so two devices are two different physical things by construction.
- `DeviceInfo` already carries `id`, `ordinal`, `name`, `guid`, `synthetic`
  (`include/rex/input/device.h`) — enough to key a profile by `guid` (hardware, survives replug) with
  `name` for display, falling back to `ordinal`.
- The keyboard is not a device in the controller sense; it is the synthetic pad from §1.5 **[assumed]**,
  so "Custom" for the keyboard is simply whether `keybind_*` overrides apply — which they already do,
  live (§2.2). Presenting it as one more row in the same panel is a UI convenience, not a new
  subsystem.

The user-facing reading of "Custom enabled/disabled" follows: **a switch per device row.** Disabled
means "this device uses the shipped behaviour" — for a pad, pass-through; for the keyboard, the
shipped `keybind_*` defaults.

### 3.5 Persistence

Two independent stores already exist and the plan should not invent a third:

| Store | Written when | Holds |
| --- | --- | --- |
| `out/build/<preset>/rb_blitz.toml` (`rb_blitz.toml` next to the exe) | only on **F4 → "Save to config"** (`settings_overlay.cpp:506`) **[tree]** | host cvars — the natural home for a custom profile, already the home of `mnk_mode` |
| `Documents\rb_blitz\…\00000001\globaloptions\globaloptions` (1024 B, opaque) | by the guest | the guest's own state, including (probably) the active preset — not integrity-checked, learned only by observation ([av-settings-plan.md](./av-settings-plan.md) §1.2) |

Recommendation: a `remap_*` cvar family (so the F4 overlay lists it for free, §2.7) plus a
`Save to config` action, matching how every other host setting works. The guest's container stays
untouched. One honest caveat to surface in the UI: a player who expects the game's own `Controls`
screen to reflect their custom layout will not find it there, because layer G is not ours to write.

### 3.6 Safety rails

The failure mode to design against is not a wrong binding, it is a game that cannot be un-broken
from a controller. Therefore:

- **Reserved keys.** Overlay hotkeys (`F3`, `` ` ``, `F4`, `F7`) and `Esc` (the capture cancel) are
  not offerable as targets. They are read by a separate registry (`RegisterBind`) from the same key
  events [assumed], so binding one would fire both.
- **A minimum viable layout.** Refuse to save a profile that leaves `Start` or `Back` unbound, or
  that leaves the menu-navigation sticks unbound, since the guest's UI needs them.
- **A panic reset that needs no controller.** A cvar (`remap_profile = ""`) plus a command-line flag
  that starts with all custom mapping bypassed. This is the recovery path when a profile is saved
  by mistake, and it must be documented next to the feature.
- **Never persist a half-armed capture.** A profile is written only in a coherent state.

### 3.7 The panel, as requested

The requested shape — controllers on their own panel, a Custom enabled/disabled switch, and below it
one button for Preset and one for Manual — maps onto this:

```
┌ INPUT ────────────────────────────────────────────────────────────┐
│ Devices                                                           │
│                                                                   │
│  ● XInput Controller #1        ordinal 0 · active                 │
│        Custom mapping          [ OFF ]                            │
│        [ Preset ▸ Default ]    [ Manual ▸ … ]                     │
│                                                                   │
│  ○ Keyboard (synthetic pad)    → guest user 0                     │
│        Custom mapping          [ ON  ]                            │
│        [ Preset ▸ Default ]    [ Manual ▸ … ]                     │
│                                                                   │
│  Game's own preset:  Shoulders   (read-only — see note)           │
└───────────────────────────────────────────────────────────────────┘
```

- **`Custom: OFF`** — this device behaves exactly as it ships today (§3.1). The default for every
  device, and the reason the presets survive this feature.
- **`[Preset ▸]`** — chooses which of the game's action sets the manual editor *describes*, i.e.
  which row list you are about to edit: the five-row set (`Default`/`Shoulders`) or the four-row set
  with merged track switching (`Typewriter`/`Freakish`), per §1.3. Selecting one **seeds** the
  device's sources from the shipped `keybind_*` defaults and leaves the guest's own preset alone.
- **`[Manual ▸]`** — the per-action capture list:

```
┌ Manual — actions modelled on: Shoulders ──────────────────────────┐
│ Action                 Sources                   Capture          │
│ Track Switch Left      PadLB                     [ Listen ]       │
│ Play Left Note         PadX                      [ Listen ]       │
│ Track Switch Right     PadRB                     [ Listen ]       │
│ Deploy Power-up        PadY                      [ Listen ]       │
│ Play Right Note        PadB                      [ Listen ]       │
│                                                                   │
│ [ Reset to preset ]  [ Clear all ]  [ Custom mapping: OFF ]        │
└───────────────────────────────────────────────────────────────────┘
```

Action names are the guest's own strings from §1.3, deliberately, so the mental model matches the
screen the player already knows.

**The read-only note is the honest part.** The guest's active preset lives in
`controller_mapping_state.ep`'s `last_map` (§1.4) and nothing in this project can read or write it
today, so the panel shows the *modelled-on* set it is editing, and — only if IM0 succeeds — the
guest's actual selection beside it. Showing a control we cannot operate would be worse than showing
none.

---

## 4. Design options

Five families. H1 is where the value is; the rest are alternatives or later refinements.

| # | Option | How it would work | Cost | Honest verdict |
| --- | --- | --- | --- | --- |
| **H1** | Remap at the **guest poll boundary** | Post-process the `X_INPUT_STATE` right after `is->GetState(...)` at `xam_input.cpp:116`, driven by a `remap_*` profile that names sources (pad **and** keyboard, §3.2). Applied identically for both backends. | **Medium** — one new repo-owned module, one SDK patch (the pattern already exists: patches 0001–0005, applied by `scripts/apply_sdk_patches.ps1`), plus the capture UI | **The recommendation.** Sees every device and every backend in one place; needs no guest changes; verification is a log assertion on an existing trace (`:123-129`). |
| **H2** | Extend `keybind_*` only | Add a capture UI that writes the existing 25 cvars, and add pad tokens to the same grammar. | **Low** | **Necessary, not sufficient.** Keyboard rebinding becomes real almost immediately, but pad buttons stay hard-coded (§2.4), so "either controller or keyboard" fails for controllers. Fold into H1 as its keyboard half. |
| **H3** | SDL-level remap | Use `hid_mappings_file` / a GameControllerDB to reassign hardware names. | Low | **Does not solve it.** It renames raw hardware into SDL's model; the guest still receives the fixed Xbox layout, and it is SDL-only. Useful only for exotic pads that are mis-detected today. |
| **H4** | New **guest-side preset** | Author `controller_preset_4`, `set_4.grp`, `smash_*_4.lbl`, a locale token, a glyph atlas, and edit `UpdateControllerLayout*.flow` inside the shipped archive. | **High** | The only route to a truly *guest-visible* custom preset. Needs archive editing, the checksum story from §2.8, and tooling this project does not have — and it would still leave capture to us. Gate on IM0; expect to defer. |
| **H5** | Read/write the guest's `last_map` | Hook or patch the guest so the resident preset index can be read (and possibly set), making the host panel preset-aware. | **Medium, blocked on addresses** | A refinement, not a foundation. The unblocking workflow is routine here: an unregistered address surfaces as `[FATAL] Call to invalid or unregistered function at guest address 0x…`, fixed by adding `[functions."0x…"]` to [config/functions.toml](../config/functions.toml) and re-running codegen (consumed via `rb_blitz_manifest.toml`'s `includes`). `ControllerHook 0x825320B4` ([rb3-references.md](./rb3-references.md) `:155`) is a catalogued starting point. |

### 4.1 Recommendation

**H2 → H1 → the panel (§3.7) → H5 if and only if IM0 can reach `last_map`; H4 only if a player-visible
preset is judged worth its cost.**

Sequence rationale: take H2 first because it is days of work that produce a genuinely working
keyboard-capture UI against machinery that is already live (§2.2) — and because it flushes out the
capture-semantics problems (§3.3) cheaply. Then H1, which is what turns the feature from "keyboard
rebinding" into "keyboard **or** controller", by adding the pad half at the one place that sees
every device at once. The panel last, so it is drawn against a completed model rather than a
speculative one.

The two options this plan deliberately does **not** recommend as a foundation: H3, because it does
not rebind anything the guest sees; and H4, because it edits shipped content to obtain something H1
obtains for free, at a fraction of the risk. Both are worth documenting as "considered and why not".

### 4.2 Input arbitration — the question this plan inherits

[av-settings-plan.md](./av-settings-plan.md) §4.2 and its AV1 milestone both flag the same unknown:
**does the guest keep consuming pad input while a host ImGui dialog holds focus?** For this feature
the stakes are higher than for the AV screen, because the capture UI *depends* on swallowing the
event it just captured, whereas an AV panel merely has to avoid double-handling a stick.

So IM0 answers it once, for both plans, and the answer shapes §3.3: if the guest is already blocked
while a host dialog is up, capture is simple; if not, H1 needs an explicit suppression flag around
the armed window, and the AV plan's option B needs the same.

---

## 5. Milestones

Each milestone has a kill gate. A failed gate means **stop and report**, not "push on and hope".

### IM0 — Evidence and feasibility (discovery only, no product code)

1. **Prove a host-side remap reaches the guest.** Temporarily swap two buttons immediately after
   `is->GetState(...)` (`xam_input.cpp:116`), then check two things: the existing trace at `:123-129`
   logs the *swapped* button word, and the guest's menu accept/cancel conventions actually
   invert. **Kill gate:** if the guest's behaviour does not follow the mutated state, the whole H1
   premise is wrong — stop and reconsider H4.
2. **Does the guest consume input while a host dialog is open?** Answers §4.2 for both this plan and
   the AV plan. Method: open the overlay, inject a key that would move the guest's menu, close the
   overlay, compare captures.
3. **Can `last_map` be reached?** Establish whether `controller_mapping_state.ep`'s `last_map` is
   readable (and, separately, writable) and which `controller_preset_N` corresponds to which
   displayed name — §1.4 leaves the index ↔ name mapping open and notes index 2 is anomalous.
   Deliverable: either an updated §1.4 table or an explicit "not reachable" statement.
4. **Does the guest read keystrokes rather than pad state anywhere?** `XamInputGetKeystroke_entry`
   (`xam_input.cpp:152`) exists; if a menu or text path uses it, a state-only remap has a hole.
5. **Does the preset survive a relaunch?** The in-session round trip is proven (§1.3). Check the
   guest's container across exit/restart.
6. **Confirm there is no fifth preset slot** — already strongly indicated by the asset names
   (`controller_preset_0..3`, `smash_*_0..3`, no index 4) and by the period-4 cycle.
7. **Tooling prerequisite: fix `drive_ui.ps1`'s D-pad defect.** Its `Send-Key` uses only the
   virtual-key map, so `dpad_up/down/left/right` are sent **without Shift** — bit-identical to the
   bare arrows, which are the *right stick* in this title ([build-and-run.md](./build-and-run.md)
   `:386`). Until that is fixed (or a new action is added), no scripted test can exercise a D-pad
   binding, so every D-pad row would be untestable by construction. Note also that `-Actions` takes
   **one comma-joined string**, not an array.

### IM1 — The remap core, headless

Deliverable: a repo-owned module (e.g. `src/input/remap.{h,cpp}`) plus a `remap_*` cvar family, wired
in at the choke point, with unit tests in [tests/](../tests) alongside the four existing host-side
targets ([known-issues.md](./known-issues.md) `:18`).

Acceptance, in the project's own idiom — both from the log, not from screenshots:

1. With custom **disabled**, the button word logged by patch 0004's trace is **unchanged** across the
   whole existing acceptance route. This is the regression test that proves the presets are intact
   (§3.1), and it is the single most important check in the plan.
2. With custom **enabled** and a deliberate swap, the same route logs the swapped word and the guest
   responds to the new button.

**One caveat observed in practice:** patch 0004's traces are high volume. A single afternoon session
left **sixteen 5 MB rotations behind it — about 84 MB in roughly seven minutes** **[cited]** — so an
acceptance check must read the *current* log (the rotation index keeps moving) rather than assume one
stable file, and any long gameplay run must be bounded or run with the trace off. The existing
`scripts/audit_pacing_input.ps1` already lives with this; do not design an acceptance step that does
not.

### IM2 — Keyboard side, grammar, persistence

Extend the source vocabulary with the `Pad*` class (§3.2) in our own parser; reuse `kKeyNames`,
`ParseVirtualKey` (`keybinds.cpp:146-148`) and the reverse lookup (`:152`) for display; implement
conflict detection (§3.3) and the `Save to config` path (§3.5).

Verify with `scripts/drive_ui.ps1` for keyboard bindings and with the hand-pad procedure for pad
bindings — noting that pad input is currently the one hand-verified item in this project
([known-issues.md](./known-issues.md) `:18`), which IM1's log assertion is intended to change.

### IM3 — The panel

Implement `OnCreateDialogs` (`include/rex/rex_app.h:108`) and one `ImGuiDialog`
(`OnShow`/`OnClose`/`OnDraw`) drawing §3.7: device rows, the per-device Custom switch, `[Preset ▸]`
and `[Manual ▸]`, the capture list, conflict prompts, and the safety rails of §3.6.

Acceptance: with custom **off** for every device, all four guest presets are still selectable and
usable; with custom **on**, a swapped mapping works in menus **and** during gameplay; a deliberately
broken profile is recoverable using only the documented panic path.

### IM4 — Optional: a guest-visible preset *(only if IM0 task 3 succeeded and IM3 shipped)*

New `controller_preset_4` content in the archive (labels, group, glyph atlas, locale token) plus
`UpdateControllerLayout*.flow` edits, delivered the way the project already delivers modified guest
data. **Kill gate:** the checksum path (§2.8) and the absence of Milo/Flow tooling. Expect this to be
deferred; H1 delivers the feature without it.

---

## 6. Non-goals

- **Modifying the guest's `Controls` screen, or its four presets.** Layer G stays the game's (§3.1).
  H4 is the only option that changes this, and it is explicitly last and optional.
- **Making the guest aware of the keyboard.** The guest sees a pad and always will (§1.5); the panel
  must not imply otherwise.
- **Per-song or per-mode layouts**, per-axis analog curves, deadzone/shape tuning beyond the
  threshold needed for capture (§3.3).
- **Rebinding the host's own hotkeys** (F3/F4/F7/Backtick) — separate registry, deliberately
  reserved (§3.6).
- **Mouse-look or mouse-as-stick rebinding.** `mnk_mouse`/`mnk_sensitivity` (`:27`, `:30`) stay as
  they are; no axis tokens in v1 (§3.2).
- **Fixing the `drive_ui.ps1` D-pad defect as a feature.** It is a one-line test-tooling correction
  named in IM0 because bindings are unverifiable without it — not part of the deliverable.
- **Games' other input surfaces** — `XamInputGetKeystroke` (`xam_input.cpp:152`) is investigated in
  IM0 and only extended if a real path needs it.
- **Multiple controller backends, Linux/macOS/ARM**, per the project's existing deferral
  (`DECOMPILATION_PLAN.md`) — this plan assumes the two backends that exist (`input_backend`,
  `input_system.cpp:27`).

---

## 7. Risks

| # | Risk | Impact | Mitigation |
| --- | --- | --- | --- |
| R1 | The guest may consume the event we just captured | capture feels broken; a press does two things | IM0 task 2; explicit suppression window inside IM1 if needed (§4.2) |
| R2 | Keyboard input needs a genuine focus transition, and the driver clears state on focus loss | bindings appear dead after alt-tabbing | already documented as a standing limit ([known-issues.md](./known-issues.md) `:17`, [bringup-log.md](./bringup-log.md) `:472`, `:1508-1509`); restate it in the feature's documentation rather than fighting it |
| R3 | The guest's active preset is invisible to us | panel could mislead about which action set is in force | show the *modelled-on* set only; add the guest's value only if IM0 task 3 succeeds (§3.7) |
| R4 | Two devices, `SlotAssignment`, and a synthetic keyboard pad all feeding users | a second player's profile could alter user 0's input | profiles keyed per device (§3.4); verify routing per `input_system.cpp:36`'s synthetic-device note |
| R5 | Analog sources as buttons can stick or chatter | continuous unintended input | threshold **plus** hysteresis, and never bind a resting axis (§3.3) |
| R6 | Two persistence stories (host TOML vs guest container) | player confusion about where the setting lives | only the host store is used; state it in the UI (§3.5) |
| R7 | `drive_ui.ps1` mirrors the SDK defaults and cannot press a D-pad distinctly | false test passes/failures | IM0 task 7 |
| R8 | H4 requires editing shipped archive content and touches the checksum path | couples the feature to payload-style content handling | keep H4 optional and gated (IM4); H1 needs no asset changes |
| R9 | A saved profile can leave the game unusable from a controller | worst-case support burden | reserved keys, minimum viable layout, panic reset that needs no pad (§3.6) |
| R10 | Patch-based delivery means the remap lives in a patch file, applied by script | a re-checkout silently loses the feature | follow the existing convention: `patches/rexglue-sdk/000N-*.patch` + `patches/README.md` row + `scripts/apply_sdk_patches.ps1`, which already reports *already applied* / *applied now* / *failed* |

---

## 8. Questions only the running game can answer

1. Does mutating the pad state after `is->GetState(...)` actually change guest behaviour? (§5 IM0.1 —
   the plan's load-bearing assumption.)
2. Does the guest keep polling and consuming input while a host dialog is open? (§4.2, shared with
   the AV plan.) At ~402 polls/s and a 4.10 ms median poll gap ([bringup-log.md](./bringup-log.md)),
   there is no quiet window to hide in.
3. Can `last_map` be read — and is it an index 0–3? Which index is `Default`?
4. Is the guest's preset persisted across a relaunch, and if so, where? The in-session round trip is
   proven; the container's timestamp did not move while we watched — **nor on a graceful exit hours
   later** (§1.3). Note that session ran offline.
5. Does clicking `A` on the guest's `Controls` screen *commit* the preset, or merely exit? The footer
   advertises one button for both.
6. Do the four presets map one-to-one onto `controller_preset_0..3` in display order, and why does
   index 2 carry both split and unified track-switch labels? (§1.4)
7. Which guest user does a second pad feed under `SlotAssignment`, and does the guest read users
   above 0 at all? ([bringup-log.md](./bringup-log.md) records 191 247 non-user-0 polls in one run.)
8. Is the D-pad a *distinct* logical control in this title, or unused? Menus use the left stick and
   the right stick is inert in them; the D-pad's role is untested **because the test script cannot
   currently press it distinctly** (R7).
9. Does the guide button reach the guest? `guide_button` defaults to false and the SDL path gates it
   (`sdl_input_driver.cpp:580`), while the capability mask tells the guest it exists (`:660`).
10. Does any guest path read `XamInputGetKeystroke` (`xam_input.cpp:152`) rather than pad state?

---

## 9. Sources

**Read in this tree [tree]**

- `rexglue-sdk/src/input/mnk/mnk_input_driver.cpp` — `mnk_mode`/`mnk_mouse`/`mnk_sensitivity`
  (`:26`, `:27`, `:30`), the 25 `keybind_*` definitions (`:33-59`), modifier parsing (`:91`), exact
  token matching (`:125-126`), `,`-alternative parsing (`:203-219`), the per-poll
  `REXCVAR_GET` reads (`:278-331`), `SetKeyState`/`OnKeyDown`/`OnKeyUp` (`:477`, `:483`, `:491`).
- `rexglue-sdk/src/kernel/xam/xam_input.cpp` — `XamInputGetState_entry` (`:96`), user-index folding
  (`:109-116`), the `is->GetState` call (`:116`), the button-word trace (`:123-129`),
  `XamInputGetKeystroke_entry` (`:152`).
- `rexglue-sdk/src/ui/keybinds.cpp` — `kKeyNames` (`:22`), `ParseVirtualKey` (`:146-148`), reverse
  lookup (`:152`), `RegisterBind` (`:171`); declaration at `include/rex/ui/keybinds.h:64`.
- `rexglue-sdk/src/input/sdl/sdl_input_driver.cpp` — `hid_mappings_file` (`:23`, applied `:99-109`),
  event dispatch (`:418`, `:423-434`), `xbutton_lookup` (`:531-…`), `guide_button` gate (`:580`),
  capability mask `0xF3FF` (`:660`).
- `rexglue-sdk/src/input/xinput/xinput_input_driver.cpp` — capabilities (`:176`), pad passthrough
  (`:219`).
- `rexglue-sdk/src/input/input_system.cpp` — `input_backend` (`:27`), `guide_button` (`:30`),
  synthetic-device routing note (`:36`), `SetDeviceAssignment` (`:74`, used at `:354`), backend
  branching (`:329`, `:337`).
- `rexglue-sdk/include/rex/input/input.h` — `X_INPUT_GAMEPAD_BUTTON` (`:35`, `A = 0x1000` at `:47`),
  `X_INPUT_GAMEPAD` (`:61`).
- `rexglue-sdk/include/rex/input/state_merge.h` — `MergeInto` (`:24-26`), `ActiveDeviceTracker`
  (`:32`).
- `rexglue-sdk/include/rex/input/device_assignment.h` — `DeviceAssignment` (`:20`), `DevicesForUser`
  (`:28`), `SlotAssignment` (`:32`), `SharedAssignment` (`:43`); `include/rex/input/device.h` for
  `DeviceInfo`.
- `rexglue-sdk/src/ui/rex_app.cpp` (`SetupOverlays` `:387-427`),
  `src/ui/overlay/settings_overlay.cpp` (`SaveConfig` `:506`), `include/rex/rex_app.h`
  (`OnCreateDialogs` `:108`).
- `patches/rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch` — the `XamInputGetState`
  button-word trace that IM1's acceptance rests on; `patches/README.md` and
  [patches/](../patches) for the 0001–0005 delivery convention, applied by
  `scripts/apply_sdk_patches.ps1`.
- `src/rb_blitz_app.h` (`:36`, `:50`, `:73` — `OnCreateDialogs` not yet overridden),
  `src/hooks/ultimate.cpp:100-109` (this repo's own cvar definitions — the pattern to copy).
- `config/functions.toml` + `rb_blitz_manifest.toml` — the guest-address registration path H5 would
  need.
- `tests/` — the four host-side test targets and what they cover.
- `docs/rb3-references.md` — `ControllerHook 0x825320B4` (`:155`), `button_meanings` /
  `config/joypad.dta` (`:274-276`), `mnk_mode` placement (`:318`).
- `docs/build-and-run.md` — patch workflow (`:24`, `:41`), `mnk_mode` in the local profile (`:182-183`,
  `:199`), "arrow keys are the right stick" (`:386`).
- `docs/known-issues.md` — the four standing frame-pacing/input limits incl. the focus dependency
  (`:17`), pad input as the one hand-verified item and the test/acceptance inventory (`:18`).
- `docs/bringup-log.md` — "the MnK driver needs a genuine focus transition" (`:472`), the
  `OnLostFocus`/`GetAsyncKeyState` root cause (`:1508-1509`), poll rate and latency figures.
- `docs/av-settings-plan.md` — shared settings/host surface, §2.3 (overlay, dialogs), §4.2 (input
  arbitration), §1.2 and §2.6 (the guest container and DTA prior art).
- `docs/ultimate-compat.md` — the project's posture on payloads and derived content (relevant only to
  H4).
- `DECOMPILATION_PLAN.md` — milestone framing, the deferred-items list, and the existing
  "multiple controller backends and keyboard bindings" deferral.

**Observed from the running build [cited]**

- Captures in `out/drive-ui/` (gitignored; re-capture with `scripts/drive_ui.ps1`): the `Controls`
  screen's header and footer, the preset-name row, the four-preset period-4 cycle over seven
  captures, the per-preset row sets (5 rows versus 4 with merged track switching), the frame-identical
  up/down presses, the in-session persistence of a changed preset, and `A` acting as confirm-and-exit.
- The guest's `globaloptions` container, unchanged in size and modification time across a preset
  change.
- Main-menu inventory: `PLAY / LEADERBOARDS / ACHIEVEMENTS / HELP & OPTIONS / MOD SETTINGS / EXIT GAME`,
  navigated with the left stick.
- `game/gen/main_xbox_0.ark` name table and the internal strings of the options-menu bundle:
  `controller_config_panel`, `controller_mapping_state.ep` (`last_map`, `direction`, `showing`,
  `text_token`, `none`), `controller_preset_0..3`, `set_0..3.grp`, `ps_0..3.tex` / `xbox_0..3.tex`,
  `arrow_*`, the `deploy` / `switch_track` / `smash` label families, and
  `UpdateControllerLayout*.flow`.
- Absence of `typewriter` / `freakish` / `shoulders` as plain text anywhere in the shipped data,
  validated against a control string that *is* present; and the absence of any `joypad` or `.dta`
  mapping table.

**Tooling used**

- `scripts/drive_ui.ps1` (including its D-pad defect and its one-comma-joined-string `-Actions`
  requirement), `scripts/ocr_image.ps1`, `scripts/frame_diff.ps1`, `scripts/capture_window.ps1`.
- `scripts/measure_pacing_input.ps1` / `scripts/audit_pacing_input.ps1` — the existing rig that turns a
  trace log into pass/fail checks, the model for this plan's log-based acceptance criteria.
- `scripts/apply_sdk_patches.ps1` — the delivery route for the SDK-side half of H1.
