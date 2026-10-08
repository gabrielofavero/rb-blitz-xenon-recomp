# launcher/ — the settings launcher

The launcher is the second executable of this build tree: a small SDL3 + ImGui window
that owns the installed game's settings and starts `rb_blitz.exe` with them. The design,
the decisions and the order the work happens in are in
[`../docs/plans/launcher-plan.md`](../docs/plans/launcher-plan.md); this file is about
what is in this directory *today* and how to change it.

Today this directory holds the **launcher executable** with its **three-tab shell**
([A1](../docs/plans/launcher-plan.md)), its **settings schema**, and the profile module that
both executables compile. The General tab is real (B1): it detects the Ultimate payload, edits
the launch target and the two locations and says why a value was refused — and when the payload
is missing, the Ultimate entry becomes the *Install Ultimate* action ([B8](../docs/plans/launcher-plan.md)),
which drives the installer's own helper. Saving, the precedence badge, import/export and the
settings location are real too (B4), the Audio / Video tab's rows are editable (B2), and the
Controller tab's button-mapping block rebinds the pad for real (D16) — the launcher writes
`[remap]` and `rb_blitz.exe` reads it back through the shared vocabulary in
[`../src/launcher/remap.h`](../src/launcher/remap.h). The window's own chrome is finished
(A2): the bar's own controls - *Update*, *Close*, *Save* and *Launch Game* - live on the bottom, under the focused row's own help
— its tooltip and how to operate it — and Launch Game is B7's contract: it saves what is unsaved,
builds the command line from the schema and starts the game. The controller navigation (A3) is
real too, so a pad shows and edits every schema row alongside the keyboard, which stays a peer
rather than a fallback. What exists here now is a launcher that shows and edits every schema row,
installs the Ultimate payload, saves what it changed, rebinds the pad, starts the game, and is
drivable with the keyboard, the mouse or a controller.

| Path | What it is |
| --- | --- |
| `main.cpp` | The entry point: opens the window, loads the profile for the window geometry, loads the UI font, runs the frame loop, saves the size on the way out (P0.4, A1, B4) |
| `src/schema_view.{h,cpp}` | The generated table turned into tabs, groups and rows — no ImGui, no SDL (A1) |
| `src/nav.{h,cpp}` | The focus ring and the one `NavSource` seam a device plugs into (A1, D6) |
| `src/pad_nav.{h,cpp}` | The pad's rules — the binding table, the deadzone, the repeat — with no SDL in them at all (A3, D6) |
| `src/pad_source.{h,cpp}` | The pad as a device: which pads are open, what they are called, and what they are asking for (A3) |
| `src/virtual_pad.{h,cpp}` | A scripted virtual pad, so the pad path is checkable on a desk with no controller on it (A3, `--test-pad`) |
| `src/focus_log.{h,cpp}` | The `--focus-log` trace: what the ring and the input devices did, written only when it changes (A3, A2) |
| `src/shell.{h,cpp}` | The tab shell: the strip, the scrolling body, the bottom bar and its help region, the ring, and one editable widget per `kind` (A1, A2, B2) |
| `src/settings_edit.{h,cpp}` | The per-kind editor that writes a row's change into the profile (B2) |
| `src/row_ui.{h,cpp}` | The row pieces both the shell and the General tab draw with, and the precedence badge (A1, B1, B2, B4) |
| `src/general_tab.{h,cpp}` | The General tab: the launch target, the two locations, the pickers, and the Ultimate install (B1, B8) |
| `src/controller_tab.{h,cpp}` | The Controller tab's button-mapping block: the control list, the three-second capture and the resets (D16) |
| `src/game_launch.{h,cpp}` | Contract 3's argv, and the process the launcher starts with it — no ImGui, no SDL (B7) |
| `src/install_ultimate.{h,cpp}` | B8's action: runs `rb_blitz_setup_helper.exe install-ultimate` and reports its progress, with the manual route when it fails |
| `src/ultimate_state.{h,cpp}` | D5's four payload states, the target fallback and where the game is — no ImGui, no SDL (B1) |
| `src/path_validate.{h,cpp}` | The schema's `validate` rules, reusing `src/fs/path_policy.h`, `src/fs/dlc_layout.h` and `src/fs/dlc_library.h` (B1) |
| `src/profile_session.{h,cpp}` | The write path: what a save would change, a reset, import/export, the settings location, and the precedence rule — no ImGui, no SDL (B4) |
| `src/game_config.{h,cpp}` | A read-only reader for the game's own `rb_blitz.toml`, which outranks the profile (B4, D3) |
| `src/profile_ui.{h,cpp}` | The profile block at the end of the General tab (B4) |
| `src/general_report.{h,cpp}` | What `--dump-general` and `--dump-profile` print (B1, B4) |
| `rb_blitz_launcher.rc` | The exe icon — `assets/blitz.ico`, through the same resource mechanism as the game |
| `config/settings.toml` | The schema: one `[[setting]]` per row the launcher shows, one `[[group]]` per category (Contract 1) |
| `tools/embed_settings.cpp` | Compiles — and validates — the schema into the header below |
| `out/generated/settings_table.h` | Generated at build time and ignored by git; the launcher includes it by path |
| [`../src/launcher/profile.h`](../src/launcher/profile.h) | The `launcher.toml` reader/writer (Contract 2) |
| [`../src/launcher/profile_path.h`](../src/launcher/profile_path.h) | Where that file lives (D2) |
| [`../src/launcher/remap.h`](../src/launcher/remap.h) | The remap vocabulary: `[remap]`'s grammar and the rewrite — no ImGui, no SDL, no SDL3 (D16) |

Nothing is generated by hand and nothing is committed from `out/`: the table is a build
product of the `.toml`, which is the single source of the rows.

## The window, and why SDL_GPU

`rb_blitz_launcher.exe` draws with **SDL_GPU**, through `imgui_impl_sdlgpu3`. That is
[D1](../docs/plans/launcher-plan.md)'s preference, and the reasons are in this tree:

- `SDL_RENDER` is compiled out of the vendored SDL3 on purpose
  (`rexglue-sdk/thirdparty/CMakeLists.txt`), so `imgui_impl_sdlrenderer3` is not an
  option here.
- SDL_GPU is core SDL3 and therefore already inside the `SDL3-static` the game links: the
  launcher gains a renderer with no new dependency and no surface of its own. On Windows
  SDL_GPU selects D3D12 at runtime, which is why the executable's import table stays at OS
  DLLs plus the CRT — no `d3d12.dll`, no `SDL3.dll`, no `rexruntime.dll`.
- `imgui_impl_dx11` stays the fallback D1 named. If SDL_GPU ever fights the vendored
  ImGui, the swap is `imgui_impl_sdlgpu3` → `imgui_impl_dx11` and the device setup in
  `main.cpp`; the window and the event loop do not change.

The target links the SDK's `imgui` OBJECT target and `SDL3::SDL3`, and deliberately not
`rex::runtime` (D1) — a settings dialog has no use for the emulator. The payload's
import-derived DLL check (P0.5) is what keeps that honest.

```
cmake --build out/build/<preset> --target rb_blitz_launcher
out\build\<preset>\rb_blitz_launcher.exe
```

It is written to the build root, next to `rb_blitz.exe` (D1): one directory holds what
a payload snapshot copies (P0.5), and one place is what a user is told to run from.

The window is a WIN32-subsystem executable, so a bring-up failure reports itself in a
message box and returns non-zero instead of printing to a console nothing owns.

### The window size, the font, and DPI

Two things make the window readable rather than merely present, and both are in `main.cpp`:

- **The face is the machine's, not ours.** ImGui's built-in font is a 13px pixel face;
  the launcher asks the OS for a real outline font instead (`segoeui.ttf`, then
  `tahoma.ttf`, then `arial.ttf`, from `%SystemRoot%\Fonts`) and falls back to the built-in
  face only when none of them is there. Nothing is redistributed with the launcher and no
  font file is added to the repository — absence is a supported configuration, exactly as it
  is for the cover art (D11).
- **The size is kept in points and scaled by the display.** SDL sizes a window in the same
  units ImGui measures the UI in, and the UI is scaled by the display's content scale, so a
  window whose geometry is stored in points has to be multiplied by that scale at creation
  time: on a 300% display, 1280×840 points becomes a 3840×2520-pixel window rather than a
  window a third the size with tiny text. `SDL_GetDisplayUsableBounds` clamps the result to
  the work area, and `SDL_GetWindowBordersSize` takes the frame off that so the window still
  has a maximize box; the launcher then sets a **minimum size** (`kMinWindowWidth`/
  `kMinWindowHeight`, 720×520 points) rather than a fixed one, so the window is draggable,
  maximizable and restorable like any other, and cannot be dragged smaller than the layout
  can be read at.
- **It opens expanded.** `SDL_MaximizeWindow` runs after the window is shown — maximizing a
  *hidden* window is a request Windows answers when the window appears, and it did not, which
  left the window merely clamped to the work area with the maximize box already used up. A
  maximized window reports the work area rather than the size the user chose, so the exit-time
  geometry write is skipped while it is: what the profile remembers is the size to restore to,
  not "as big as this display".
- **A tab taller than the window scrolls.** The tab strip sits in a fixed header, the rows
  live in a scrolling child under it (`kBodyId` in `src/shell.cpp`), and the bottom bar is
  pinned below that, so the wheel and the ring's `SetScrollHereY` move the rows and never the
  chrome. The Audio / Video tab and the Controller tab are both taller than a small window,
  which is what that is for.

`--dump-display` prints all of it — the usable bounds, the content scale, the size the
window would open at, the window title and the face that was loaded — so the DPI story is
checkable on a build machine rather than only from a screenshot of someone's monitor.

**The window title carries the release version** — `Rock Band Blitz Launcher (v0.1.0)` — and
nothing else: not the tab, because the strip already says which tab is up. The number is
`RBBLITZ_LAUNCHER_VERSION`, which `CMakeLists.txt` reads out of `installer/config/pins.toml`, the
record the setup executable is named from as well, so a launcher and the installer that shipped it
cannot claim to be different releases. A release that stamps its version some other way passes it:
`-DRBBLITZ_VERSION=1.2.3`. `--dump-display` prints the title, so which release a window belongs to
is answerable without a picture of a title bar.

## The shell — tabs, rows and the focus ring (A1)

The shell is the model's, not ImGui's. Every row it draws comes from the generated table
(`src/schema_view.cpp`), so the launcher holds no per-setting knowledge and adding a row
changes no C++ at all:

- **Four tabs**, in the table's own order — General, Interface, Audio / Video, Controller — because
  the tab list is data (R9), each labelled
  the way a person reads it (`General`, not the lowercase key the schema spells —
  `DisplayTabName`, which is also where `graphics` becomes **Audio / Video**: the tab holds
  the audio rows too, and its schema key is the schema's business rather than the user's).
  The strip is ours rather than `ImGui::BeginTabBar`, so the selection
  changes on the frame the key is read and nothing moves it but the ring (A3's pad included),
  and so the strip can stay put while the tab body scrolls under it.
- **One row per `[[setting]]`**, grouped by `group` in the table's order, with one
  widget per `kind` — `bool`, `int`, `float`, `enum`, `string`, `path_dir`, `path_file`.
  A1 drew them read-only; B2 makes them editable: a `bool` is a checkbox, an `int`/`float`
  with `min`/`max` is a slider, an `enum` is a row of radios (one *option* each: Left and
  Right walk them), and a string is a text field. A kind without an editor — a path outside
  the General tab — is drawn read-only rather than ignoring the click, and its value comes
  from the profile.
- **A restart row says so**: a row whose `applies` is `restart` carries a quiet
  `(needs restart)` after its widget, because a change that only lands on the next boot
  must not look like it just happened (D12).
- **A group this build has nothing for is hidden**, not drawn as its name plus the
  group's own `note`: empty categories (*Ultimate (advanced)*, *Developer*, *Devices*,
  *Keyboard*, *Manual*) are declared in the schema and shown by `--dump-layout`, but the
  tabs leave them out rather than spending the window on text that offers nothing. A group
  whose only rows were hidden by a `visible` rule is the same case, and is left out too.
- **A row can carry a `visible` rule**, and two do: *Audio / Video → Window → Monitor* declares
  `visible = "multi_monitor"` (a machine with one display has nothing to choose between, so it gets
  no row), and *Interface → Main menu → Rename Mod Settings* declares
  `visible = "ultimate_installed"` (a row about the Ultimate mod's own screen has nothing to say
  where the mod is not installed). The rule is applied in `schema_view` when the layout is built, not
  by the code that draws, because the ring that decides what is focusable and the loop that draws are
  two different pieces of code — the only way they cannot disagree about a hidden row is for the
  row to be gone before either of them sees it. `--dump-layout` counts the rows each rule kept
  out, so a rule that hides more than it meant to is visible there, and it answers the Ultimate
  question from this install (`DetectUltimateState`) rather than assuming.
- **One focus ring per tab** (`src/nav.cpp`): an ordered list of the tab's rows, each with the
  options it offers side by side, and the flat entry the ring is on. Up and Down move between
  rows, Left and Right between a row's options; both wrap. Keys are translated in exactly one
  place and become `NavAction`s; a pad produces the same vocabulary through its own source
  (`src/pad_source.cpp`, A3), and the shell does not know which device answered.
- **The bottom bar (A2)** is the window's, not a tab's, and it is where a session ends: the
  state of the settings file on the left, *Update*, *Close*, *Save* and *Launch Game* on the right, and
  under them the focused row's own help — its tooltip, and how to operate it. There is no title
  text and no key legend at the top: the window's own title bar names the launcher and the release
  it came from, and the tab strip says which tab is up.
- **Window geometry** is read from the profile at startup and written back on the way out,
  and only when it changed — so a launcher nobody resized neither creates `launcher.toml`
  nor touches its mtime. A profile that does not parse is never written over (D2). The
  number is in logical points and is multiplied by the display's content scale when the
  window is created, so a 300% monitor gets a window three times as large in pixels rather
  than one a third the size; `--dump-display` prints the work area, the scale, the size the
  window would open at and the font face it loaded.

| Keys | What they do |
| --- | --- |
| `Down` / `Up` (`Tab` / `Shift+Tab`) | Move the ring down / up a **row** |
| `Right` / `Left` | Move the ring along the focused row's **options**: an enum's choices, a row of buttons (Import/Export/Reset), a slider's value, or the bottom bar's Update/Close/Save/Launch |
| `Home` / `End` | First / last row |
| `PageDown` / `PageUp` | Next / previous tab (`LB`/`RB` on a pad) |
| `Enter` / `Space` | Operate the focused option, or the focused row: toggle a checkbox, choose an enum entry, step a slider, open the folder picker, or start the Ultimate install |
| `Esc` / `B` | Leave, or cancel whatever is modal |
| `Ctrl+S` | The bar's *Save*, so a keyboard alone can finish a session |
| `Ctrl+Enter` | The bar's *Launch Game*: it saves first, then starts the game |
| `Ctrl+Shift+C` | The precedence badge's *Copy the effective value*, for the row the ring is on — the bar names it on the rows that carry a badge |

The keys are the launcher's own and are **not** configurable: a hand-edited profile cannot lock a
user out of the window that would fix it, which is the trap a rebindable table invited. The two
rules the vocabulary still follows, both in `src/nav_bindings.cpp`:

- A key on its own and the same key with `Ctrl`, `Shift` or `Alt` are different bindings, and a
  chord is resolved before a bare key. That is what keeps `Shift+Tab` meaning "back" while `Tab`
  means "forward", and what lets `Ctrl+S` save on a keyboard where `S` is bound to nothing.
  A bare key is the key whatever else is held with it, so `Shift+Down` has always moved the ring.
- A *move* repeats while it is held and a decision does not. `Tab` did not repeat and the arrows
  did; now every move repeats and nothing else does — a held `Enter` would press *Install
  Ultimate* again.

The bottom bar's own three controls — *Close*, *Save* and *Launch Game* — are the last row of the
ring, so pressing `Up` on the first row (or `Down` on the last) lands on them; `Left` and `Right`
choose between them and `Enter`/`A` presses one. That is how a controller reaches *Close* without
a mouse.

| Pad | What it does |
| --- | --- |
| D-pad / left stick | Up and down walk the rows; left and right walk the options inside the focused row (`src/pad_nav.cpp`, with `kStickDeadzone` of 8000 as the band around the centre that means nothing) |
| `A` | Operate the focused row — the same thing `Enter` does |
| `B` / `Back` | Leave, or cancel |
| `LB` / `RB` | Previous and next tab |
| `Start` | Launch the game: the bar's *Launch Game* without the mouse, refused in the same cases the button is disabled in |
| Right stick | Scrolls the tab body, so a long tab can be skimmed without moving the ring |
| Hold a direction | It repeats after 0.45 s and then every 0.12 s, so a held D-pad walks the list. Nothing else repeats: a held `A` would press *Install Ultimate* again |

The mouse is a device too: clicking a row focuses it, and hovering adopts the ring, so the
pointer and the keyboard never disagree about the selection (D6). A hovered *button* adopts it
too — the bar must describe whatever the pointer is on, not the last row the ring left behind.
"Hovering" means the pointer *moved* onto the row: the launcher measures that on the desktop
(`SDL_GetGlobalMouseState`, once a frame in `main.cpp`) rather than from ImGui's delta inside the
window, because a window that is created, maximized or restored under a still pointer produces the
same delta as a real move — measured, the ring used to jump to the row under the pointer a moment
after the launcher opened, and a harness that captured two frames with nothing pressed saw the help
strip and the body move under it.

A pad and the keyboard are peers, not alternatives: the first device to answer wins the frame,
and either can take over mid-session (`--no-gamepad` opens no pad for navigation at all, which
is the recovery switch for a pad holding a direction down by itself). Two attached pads are
merged into one state, so pressing either moves the ring; the buttons the bar names are the
last pad's that was used, so a pad that says it is a PlayStation one is told to press *Cross*
rather than *A* (`SDL_GetGamepadButtonLabel`, and the family's own names for the shoulders and
`Start` where the database has none — the face labels are the database's answer, the rest are
this project's short table).

Inserting or removing a pad mid-session is handled by polling: `SDL_GetGamepads` is read once a
frame, pads that appeared are opened and pads that have gone are closed, and a button that was
held when a pad left simply arrives as a state with nothing in it — there is no release event to
wait for. This matters more than it looks: **SDL reads a pad's state only through a handle
somebody opened**, and `SDL_GetGamepadFromID` answers with nothing for a pad nobody has, so a
capture that read SDL directly would see silence that looks exactly like a pad at rest. Both
readers — the ring's source and the remap block's listen — go through `PadRegistry`
(`src/pad_source.h`) for that reason.

### The focused row's help (A2, D7)

The bar's second and third lines are the row under the ring: the row's own `tooltip` from the
schema table, and the hints for it — key names or the pad's button names, following the last
device used. Three things about it are deliberate:

- **Every row has one, by construction.** `tools/embed_settings.cpp` refuses to build a row
  without a tooltip, so the bar's text cannot be missing; the rows that are not settings — B4's
  four buttons, D16's mapping rows and the bottom bar's three — carry theirs beside the code that
  draws them (`ProfilePanel::HelpText`, `ControllerTab::HelpText`, the shell's own), and the shell
  asks *them* rather than keeping a second table to fall out of step. The walk from a ring entry to
  a row is one function (`schema_view.cpp`'s `SettingForEntry`), counting entries the same way the
  ring was sized, which is why the two cannot disagree; a tab with no rows at all is D7's explicit
  `—`.
- **The tooltip is re-wrapped, not repeated.** The line breaks in `settings.toml` are the file's
  own wrapping, and the bar's width is a number the file cannot know, so the text is flattened
  (`FlattenHelpText`) and drawn into the width the hints leave. Two lines, always reserved even
  when one would do, because the body was given the rest of the window *before* the bar was
  drawn: a bar that measured itself afterwards could not have told the body how much room it had.
  A sentence taller than two lines is clipped; the tooltips are written to fit, and the right
  stick drives the body's own scrollbar rather than the bar.
- **The hints say what the row does, not what the window does.** `RowActionVerb` is one word per
  kind — *Toggle*, *Adjust*, *Edit*, *Choose*, *Browse* — and the two global hints follow. The
  hints are dropped from the end when the window is too narrow for all of them, so the row's own
  verb is the last thing to go.

`--dump-layout` prints the tabs, groups and rows the shell would draw, without opening a
window — the headless half of A1's verification, and what says out loud if a row ever
reaches the table without a tooltip. An enum row also prints its `choices`, which is the
only way to ask a payload "does this build offer Vulkan?" without a screen.
`--dump-general` prints what the General tab would
decide for a given game root and profile, `--dump-profile` prints B4's write path and the
**precedence audit** — one line per row the game's own `rb_blitz.toml` decides, in the words
the badge uses — `--dump-prefill` prints D4's first-run decision (the manifest it read and what
it would seed), `--dump-display` prints what the launcher made of the display, which is
the only way to check the DPI story without a screenshot, and `--print-command` prints the
exact command line *Launch Game* would run (B1/B4/B2/B7's verification):

```
rb_blitz_launcher.exe --dump-layout=out\layout.txt      # or no =<path> for stdout
rb_blitz_launcher.exe --dump-general=out\general.txt --game_data_root="D:\Games\rb_blitz" --launcher_profile=out\p.toml
rb_blitz_launcher.exe --dump-profile=out\profile.txt --launcher_profile=out\p.toml
rb_blitz_launcher.exe --dump-prefill=out\prefill.txt --launcher_profile=out\p.toml
rb_blitz_launcher.exe --dump-display
rb_blitz_launcher.exe --print-command --launcher_profile=out\p.toml
```

Three switches are about the launcher's own input rather than about a setting (A3):

```
rb_blitz_launcher.exe --focus-log=out\focus.txt --launcher_profile=out\p.toml
rb_blitz_launcher.exe --no-gamepad
rb_blitz_launcher.exe --test-pad="family=sony;a;down;down:1200;right;detach"
```

`--focus-log=<path>` writes one line whenever the ring moves or the input device changes —
`tab=graphics entry=0/24 rowindex=0/17 row=resolution enter=Choose`, `device gamepad name=… confirm=Cross …`,
`pads count=2 name="Virtual Pad"`. A screenshot can say which tab is up and nothing more, so
this is how "the pad moved the ring, mid-session, without disturbing anything" is read back: a
diff of a small text file instead of a screenshot nobody can inspect twice. It writes only on a
change, and nothing reads it back.

`--no-gamepad` opens no pad *for navigation* (D6's recovery switch). A pad holding a direction
down — a snapped stick, a cushion on the D-pad — would otherwise walk the ring forever; the pads
are still opened, because the Controller tab's listen is asked for one press at a time and a
launcher that cannot be remapped is a worse trap than one that cannot be moved by a pad.

`--test-pad=<script>` attaches SDL's virtual joystick and presses it on a schedule, because the
pad path is otherwise only testable on a desk with a controller on it: a virtual pad lives
inside the process that attached it, so nothing outside this one could press it. Steps are
`;`-separated — `attach`, `detach`, `up`/`down`/`left`/`right`, `a`/`b`/`x`/`y`/`lb`/`rb`/
`start`/`back`, `lstick=X,Y`, `family=xbox|sony` — each held for `:ms` (120 by default) and
starting 400 ms after the one before. The pad arrives 1.2 s in, so "plugged in mid-session" is
what the default schedule means. The virtual pad brings its own mapping (installed before it is
opened, because a mapping only reaches an opened pad through a reload), which is what makes
`family=` able to say what the pad claims to be — and what makes a run independent of what the
mapping database happens to have for a hardware id that matches nothing.

## The General tab (B1)

The launch target and the *Paths* group — the save location, the DLC location, and the two DLC
library rows (*Cache DLC library scan*, *DLC scan threads*) — which is M1's General scope (§1.1,
D4). *Verify installation* and the game-directory override are later (D4 says so); nothing here
pretends otherwise.

**Only the two path rows draw a widget of their own** — a typed field with a *Browse* button. Every
other row in the tab is the shell's own editor for its `kind`, the same checkbox, slider or radio
line the other tabs draw: the tab owns where a path is picked, not which controls exist. The two
DLC library rows are ordinary schema rows in the *Paths* group, not decorations.

**The launch target** is a stack of radios under the group heading, one per choice — *Rock Band
Blitz*, *Rock Band Blitz (Trial)* and *Rock Band Blitz Ultimate* — each its own focus-ring row, so
Up and Down move between them. What each one is lives in the row's tooltip rather than in a
description under it.
It is never a dead option (D5). What the files say is a *state*
(`src/ultimate_state.cpp`), read from the same header the runtime's own check uses
(`src/hooks/ultimate_plan.h`), so "the launcher says ready" and "the game mounts it" cannot
drift:

| State | What the files look like | What the tab shows |
| --- | --- | --- |
| Ready | the payload pair under `<game root>\ultimate\gen\` | the three radios, Ultimate included |
| Also present | the same pair at the game root's top level ("merged") | the three radios, Ultimate included |
| Missing | neither location has it | two radios and an *Install Ultimate* action instead of a greyed-out third |
| Damaged | `patch_xbox.hdr` without `patch_xbox_0.ark` | the same — the install is the repair |

Ultimate stays selectable in every state; what changes is the **default**: a stored
"Ultimate" with nothing to mount comes up as the retail game, and installing the payload
later restores the choice without the user having to remember it (`FallbackTarget`). The
four states are pinned by `tests/launcher_ultimate_state_tests.cpp` from directory fixtures.

***Install Ultimate*** (B8) is what replaces the disabled option, so there is nothing to
explain and nothing to grey out, and it stands apart from the two radios above it so it does
not read as a third choice in the same list. It runs the helper the installer put in the
install folder:

```
rb_blitz_setup_helper.exe install-ultimate --dest "<game root>" --from-pinned
                          --progress <tmp> --summary <tmp> --log <tmp>
```

- The child runs **without a window and without blocking the frame loop**; the modal shows
  the helper's own progress file (percent, then the current step) and can be cancelled. A
  cancelled run kills the helper and removes `<game root>\.staging`, because the mod is
  staged there and only moved into place after it verifies — cancelling leaves the game's
  own files alone and leaves no half-written payload.
- When it is done, the summary's `ok=1` closes with a confirmation and the tab's file-system
  probe simply re-runs, so the third radio comes back on the next frame.
- When it fails — the helper is missing, the build has no pinned URL, the download failed —
  the modal shows the helper's `error=` line verbatim and then the two manual steps: the
  pinned release URL (read out of the helper itself with `version --summary`, so the launcher
  never duplicates the pin) and the folder to unpack the archive's `Xbox` directory into,
  `<game root>\ultimate`, with buttons that open each.

The launcher never bundles the mod and never downloads it itself; that stays the installer's
helper and the installer's pin (D5, §4.6).

**The two locations** are path rows with a typed field and a `Browse` button. The ring lands on
*Browse* rather than on the title above the row — the title is a label, and the button is the thing
a keyboard or a pad can press — and `Enter`/`A` opens the same picker. The field shows the path the
game will *use*, not the one that was typed: an empty profile value means the game's own default,
and a blank field would hide where the saves are about to go. Those defaults are the game's own
arithmetic - `Documents\Rock Band Blitz` for the save folder (`src/rb_blitz_app.h`), `<game
root>\dlc` for DLC (`src/fs/dlc_layout.h`) - so the launcher repeats them rather than inventing a
third answer. Editing the field is still how a value is overridden.

Every value — picked, typed, or already in the profile — is
judged by the row's own `validate` string from the schema (`src/path_validate.cpp`), so the
rules are the runtime's and not the tab's:

- `inside_game_root:forbid` refuses a folder inside the game data, because the runtime
  redirects its writable roots to the platform user folder and the row would silently do
  something else (D4).
- `dlc_layout` accepts both roots the runtime mounts (D4's one rule, one implementation):
  a structured `<title_id>/<content_type>/<package>` folder, refused with `src/fs/dlc_layout.h`'s
  own wording when it is malformed, and a flat library of loose CON/LIVE containers — a dumped
  song folder — which the runtime reads where it lies (`src/fs/dlc_library.h`). An absent or
  empty folder is fine and says so — an empty DLC folder is the normal case.
- A rule this build does not know is refused rather than waved through, so a schema that
  grows one fails loudly instead of silently accepting anything.

A refusal keeps the value the profile already holds and states the reason under the row; a
value already in the profile is judged the same way, so a hand-edited file is flagged rather
than trusted. Editing a row changes the session's profile, and a change reaches the disk when
the profile block below the rows is saved (B4).

D5's target fallback is a **display** decision, not a stored one: a stored "Ultimate" with
nothing to mount is shown and started as the retail game, and the profile keeps what the user
chose, so installing the payload later needs no remembering and no save can record a fallback
the user never picked.

**Where the game is** (D4's first-run detection, and the reason the target row knows what to
offer). The launcher's own folder first — the installer puts the data at `<launcher>\game` —
then the launcher's folder itself, for a dump that sits beside it, and then **up the tree**:
`out\build\<preset>` is a development tree, and the checkout's own `game` is three levels
above it. Six levels is the limit, and the nearest match wins. Before that search existed the
launcher run from a build folder reported "Ultimate is not installed" however complete the
payload was. `--game_data_root=<path>` still overrides everything, because a user who names a
folder has said where the game is and is not asking to be second-guessed.

```
D:\Coding\decomps\360\rb-blitz-xenon-recomp> out\build\win-amd64-release\rb_blitz_launcher.exe --dump-general
game root      : D:\Coding\decomps\360\rb-blitz-xenon-recomp\game (found)
ultimate root  : D:\Coding\decomps\360\rb-blitz-xenon-recomp\game\ultimate
ultimate       : ready
```


## The Interface tab

The tab for edits to **the game's own screens** — what the player sees and navigates — as opposed to
the install (General), the picture (Audio / Video) and the controls (Controller). It exists because
those edits have nothing to do with the launch target they happen to sit beside: they change the
guest's own data, in either target, and more of them are planned (R2's UI scale, R4's icons and R9's
menu songs all belong here when they land).

Its groups today are *Startup* and *Main menu*:

| Row | What it does |
| --- | --- |
| *Hide offline mode prompts* (`enhancements_skip_offline_dialog`) | answers the title's two failed-connect questions — "Cannot connect to Rock Central" and "Proceed in Offline Mode?" — as they arrive, so one A on the title screen goes straight to the menu in offline mode. Ships **on**: with no Rock Central to reach, both questions have one answer each, and this is the row that asks for them back |
| *Hide offline menu items* (`enhancements_hide_menu_options`) | removes the main menu's Leaderboards, Achievements and Downloadable Content rows — the three whose screens need Rock Central |
| *Menu items to hide* (`enhancements_hidden_menu_options`) | which rows that means, by the title's own names |
| *Rename Mod Settings* (`enhancements_rename_mod_settings`) | draws the Ultimate mod's own screen as "Ultimate Settings"; ships **on**, and its row declares `visible = "ultimate_installed"`, so it is not shown at all where the payload is missing — a row about the mod's own screen has nothing to say there, and the runtime's own `DetectUltimateState` answers that question rather than a second guess at the file layout |

What the three edits are, why each has to keep a file's length, and the runs that prove them are in
[main-menu-flow.md](../docs/engine/main-menu-flow.md) (§7 is the offline prompts, R3). The rows are the
project's own cvars, not the runtime's, so the tab passes them like any other row that is not on its
compiled default.

*Startup* is the group for what happens before the menu is drawn, which is why the offline-prompt row
sits there rather than beside the menu rows it saves the player from: the edit is to the panel the
title opens when a game is started, and the menu rows are what that panel then leads to.

## The Audio / Video tab (B2)

Every row here is editable, and every one is a projection of a cvar — the launcher owns no
graphics state of its own. The widgets come from the schema, so the tab has no per-setting
code:

| Kind | Widget | Notes |
| --- | --- | --- |
| `bool` | checkbox | `Enter`/`Space` toggles it; `Left`/`Right` sets it off/on |
| `int` / `float` with `min`/`max` | slider | the bounds are the cvar's own `.range(...)`; the ring lands on the slider and `Left`/`Right` steps it |
| `enum` | a row of radios | one option per choice, so `Left`/`Right` walks the choices and `Enter`/`A` picks one |
| a row with no editor | read-only | drawn disabled rather than ignoring the click |

*Anisotropic filtering* is an `int` slider over the cvar's own `-1` to `5` range: `-1` keeps the
game's own value, `0` disables it, and `1`–`5` force 1x up to 16x.

Changes land in the session's profile, and the bottom bar's **Save** is what writes it; the
bar's own left half is the feedback, showing "Unsaved changes" while a save would write and
nothing at all when there is nothing to say. A row whose `applies` is `restart` carries
`(needs restart)` after the widget — the six `live` rows (`fullscreen`, `vsync`,
`present_letterbox`, the two safe areas, `audio_mute`) do not, and their tooltips name what
makes the change land.

The Display group is one row, **Resolution**, an `enum` over the presets the runtime's own
parser accepts. It is deliberately not a free field: see "What is deliberately not here"
below for why the free-form size, the guest video mode's dimensions and the guest refresh
rate are all absent.

**Renderer.** `gpu_backend` is this *project's* cvar (`src/main.cpp`), not the SDK's, and
`RbBlitzApp::SelectGpuBackend` is what acts on it: the SDK loads the GPU plugin with its own
"any" backend, and this names one instead — returning without touching anything when the value
is empty or `any`, so nothing changes for a user who never touches the row.

Two things have to be true before a named backend is used, and each degrades to the working
renderer rather than refusing to boot — loudly, because the log names what could not be
provided:

- **The payload must have the backend.** A backend that is not compiled into `rexgpu-xenos.dll`
  fails to load and leaves the SDK's own load to pick the one that is. This is what the choices
  below are about.
- **The machine must be able to run it.** The SDK loads `vulkan-1.dll` at run time rather than
  linking it, so on a box with no Vulkan driver the plugin happily builds a Vulkan graphics
  system and the failure only lands later, inside presentation setup — by which point the
  backend is committed and there is nothing left to fall back to. `SelectGpuBackend` therefore
  checks the loader (and that it exports `vkGetInstanceProcAddr`) first. Whether it has a usable
  *device* is left to the provider, which has to initialize Vulkan anyway.

The row's choices are **what the build compiled in**, which is why they come from CMake rather
than from the `.toml` (`choices_from = "gpu_backends"`, and `--backends=d3d12,vulkan` on the
embed step). A D3D12-only payload offers one renderer, because offering Vulkan would be
offering a renderer that cannot run. `--dump-layout` prints every enum's choices, so which
renderers a payload offers is answered without opening a window.

This tree now compiles **both** backends (`REXGLUE_USE_D3D12=ON` and `REXGLUE_USE_VULKAN=ON`),
which is what turned the row into a real choice:

```powershell
git -C rexglue-sdk submodule update --init --recursive `
    thirdparty/vulkan-headers thirdparty/vulkan-memory-allocator `
    thirdparty/spirv-headers thirdparty/glslang thirdparty/spirv-tools
cmake --preset win-amd64-release -DREXGLUE_USE_VULKAN=ON
cmake --build out/build/win-amd64-release
rb_blitz_launcher.exe --dump-layout      # Renderer = d3d12  choices: d3d12, vulkan
```

Verified on this machine (AMD Radeon, driver 0x800161): with `--gpu_backend=vulkan` the log
shows `Vulkan instance API version 1.4.309` and a `VulkanPresenter: Created 3840x2160
swapchain`, and a capture of the title screen renders through it; with `--gpu_backend=d3d12`
the DXGI/D3D12 path is unchanged. The fallback was exercised too, by putting a `vulkan-1.dll`
that is not a Vulkan loader beside the executable: the log then reads `graphics backend
'vulkan' was asked for, but vulkan-1.dll is not usable here` and the game boots on D3D12.

**Anti-aliasing** is two rows, because the runtime has two mechanisms and they are
independent: `native_2x_msaa` resolves the title's own multisampled render targets on the host,
and `swap_post_effect` (`none` / `fxaa` / `fxaa_extreme`) runs a post-process pass over the
finished frame. Merging them into one "quality" row would have made one setting quietly change
two mechanisms.


## The first run (D4)

A launcher that has just been installed has no profile. Rather than asking the user to type a
path the installer already wrote down, the first run reads `install-manifest.toml` beside the
launcher (`[install] game_directory`, `[game_data] ultimate_installed`) and **seeds** the
General tab from it: the game folder, and the launch target that the install's Ultimate state
implies (Ultimate when the mod was installed, the retail game otherwise). It also records the
`payload_commit` for a bug report.

Nothing is written by the prefill. The profile is left **dirty**, so the panel shows *Unsaved
changes* and the user's **Save** is the confirmation that creates the first file — the same rule
the window geometry follows on the way out, which writes the profile as the file last had it and
therefore cannot persist an unsaved prefill by the back door.

The cases the launcher handles rather than pretending they do not exist
(`launcher/src/prefill.{h,cpp}`, unit-tested over fixture manifests in
`tests/launcher_profile_session_tests.cpp`):

| The folder holds | What happens |
| --- | --- |
| a manifest whose paths exist | the manifest's game folder and Ultimate state seed the profile |
| no manifest (a hand install, or an installer older than the manifest) | the file system, exactly as B1 already detects the game root |
| a manifest whose paths are gone | the missing folders are named and the file system is re-scanned; what is named is what the report (`--dump-prefill`) shows |
| a profile that already exists | nothing: a second launcher install shares the one per-user file, and overwriting it would be a reset the user did not ask for |

`--dump-prefill` prints this decision without opening a window — the manifest it read, whether
the prefill applies, and the game folder and target it would seed — which is how an install the
launcher has never seen is checked on a build machine:

```
rb_blitz_launcher.exe --dump-prefill=out\prefill.txt --launcher_profile=out\p.toml
```

## The profile — `launcher.toml`

The profile is the file the launcher saves and the **game** reads, so a launcher launch and
a double-click see the same settings ([D3](../docs/plans/launcher-plan.md)). It is why the
module lives under `src/` rather than here: `rb_blitz.exe` compiles it too.

D2's order for the file's own path, first match wins:

| # | Where | When |
| --- | --- | --- |
| 1 | `--launcher_profile=<path>` | always wins — an explicit path is never second-guessed |
| 2 | `RBBLITZ_LAUNCHER_PROFILE` | for tests and for isolating an acceptance run |
| 3 | `<settings dir>\launcher.toml` | while `%APPDATA%\rb_blitz\settings_dir.txt` names that folder |
| 4 | `<exe folder>\launcher.toml` | only while `<exe folder>\rb_blitz_launcher.portable` exists |
| 5 | `%APPDATA%\rb_blitz\launcher.toml` | the default |

So the install folder is never a candidate by accident — the uninstaller deletes it, and a
per-user install can live under `%LOCALAPPDATA%\Programs`. Step 3 is the folder the user chose
in the General tab's *Change settings location*, which is why the pointer file lives in the
default folder: it is the one location that is always there, whichever folder the settings were
moved to. Step 4 is the older marker, still resolved for an install that already had one, and a
chosen folder supersedes it (the marker is removed) because two answers to "where do the
settings live" is one too many. `src/launcher/profile_path.{h,cpp}` is the whole of it, and
`rb_blitz.exe` resolves the same way through the same code — with `launcher_profile` as its own
spelling of step 1 — so the launcher and the game cannot disagree about which file is theirs.

What a save promises: it patches the keys the module owns and leaves the rest of the
document alone, so comments, keys and whole tables that version does not know survive, and
a load-then-save with no edits reproduces the file byte for byte. A file that does not
parse is reported with a reason and left on disk untouched — the launcher refuses to save
over it until it is fixed (D2: "never lose a hand-edited file").

`tests/launcher_profile_tests.cpp` pins both halves: `ctest -R launcher_profile`.

The tables in it, and who reads them:

| Table | Written by | Read by |
| --- | --- | --- |
| `[launcher]`, `[window]` | the launcher: the format's version, portable mode, the window size (A1) | the launcher only |
| `[launch]` | the launch target and the game-directory override | the launcher (the game gets the target on its command line, Contract 3) |
| `[settings]` | every other row, keyed by cvar — the two path rows included, under `user_data_root` / `dlc_root`, which is also what `--print-command` reads them from. A folder an older build saved in `[launch] user_data_dir` / `dlc_dir` is carried across to these keys once, on load, so a pick made before the change still reaches the game | the launcher, and the game as its own config source (D3 rank 4, `src/launcher/profile_apply.cpp`) |
| `[remap]` | the Controller tab (D16) | the game (B8's wrapper of the pad state) |
| `[nav]` | the launcher's own key bindings (A5) | the launcher only |
| `[update]` | `declined_version`, the release the user answered *Not now* to (D19) | the launcher only |

`[update]` exists only while it has something in it, like `[nav]`, and a key beside
`declined_version` that this build does not know survives a save like any other unknown key.


## Saving, the precedence badge and the settings location (B4)

**Save is the bottom bar's**, next to *Close* and *Launch Game*, because that is where a user
looks for "did that stick?" — it is not a property of the settings-file block, and a Save
button at the end of one tab's rows was both easy to miss and easy to mistake for "save this
tab". Below the General tab's three settings are the rest of the write path, and they are in
the focus ring like everything else — *Reset to defaults*, *Import*, *Export* and *Change
settings location*. `src/profile_session.{h,cpp}` decides; `src/profile_ui.{h,cpp}` is buttons
and wording, and it is dependency-free on purpose so all of this is testable without a window
(`tests/launcher_profile_session_tests.cpp`, `ctest -R launcher_session`).

**A save writes what changed and nothing else.** A setting equal to its compiled default is
*dropped* from `[settings]` rather than written out, so the table is the list of what differs,
and a save whose bytes would be identical does not open the file at all — an unchanged profile
keeps the mtime of its last real change. The window size is the one thing the launcher keeps
without being asked (A1), it is written from the profile as the file last had it, and a size
that did not change is not written, so a launcher nobody resized neither creates the profile
nor touches it.

**Nothing is written over a file that did not parse.** The bottom bar says so, Save refuses
with the reason, and *Import* stays live — reading a profile you trust is the way out of a
broken one.

**A reset names exactly what it will remove.** The confirmation lists the `[settings]` keys it
will drop (by label and by key name, so a key this build does not know is still named) and the
launch fields it will clear — including every button binding, back to the pad as it came — and
says the window size is not one of them. The reset happens in the session, so the file it
changes is the user's to write with Save.

**The precedence badge (D3).** The game applies its own `rb_blitz.toml` *after* the launcher's
profile at equal rank, so for a row the launcher leaves at its compiled default — and therefore
never passes on the command line — the game's file is what the game will use. Those rows carry
a line saying so, in the words `OverrideNoteText` produces, with a *Copy the effective value*
button that adopts the game's value into the launcher's own file. The game's file is never
written: it belongs to the game and to the F4 overlay's *Save to config* (D2). With a pad or a
keyboard the row can be set to match by hand instead; the button itself is mouse-only, because a
second focusable target per row is a focus-model change and belongs with A5.

**The settings location** is a folder the user chooses, and the button that changes it sits
beside Export. It is not a setting inside the profile, because where the profile *is* cannot be
recorded in the profile: a pointer file does that job, and it is what step 3 of D2's order above
reads. Its rules are the ones the profile's own write path already had, because the choice is
the same kind of decision:

Choosing a folder **carries the settings over** rather than leaving an empty folder behind, and
the file in the old one is kept: switching where settings live is not the place to delete a
settings file. If the new folder cannot be written the pointer is put back exactly as it was
and the failure is reported, because settings in a folder that cannot be written is worse than
settings where they were. Choosing the default folder removes the pointer rather than recording
it, so "the default" has one representation on disk. An override that named the file
(`--launcher_profile`, `RBBLITZ_LAUNCHER_PROFILE`) refuses the change with a reason, because a
pointer that has no effect would make the next run disagree with this one.

The **bottom bar's left half** carries the "unsaved changes" warning whenever a save would
write, because a badge's action or a row's edit can happen on any tab and "did that stick?"
must not depend on looking at a different one. It says nothing when there is nothing to say —
no file path, no "Saved" — and prints only what needs the user: a file it could not read,
changes that are not written yet, or the last action's outcome.

## Launching the game (B7)

*Launch Game*, on the right of the bottom bar, is Contract 3:

```
rb_blitz.exe
  --game_data_root="<game root>"
  [--user_data_root="<save folder>"]              # only where the profile overrides it
  [--dlc_root="<dlc folder>"]                     # likewise
  --ultimate_mode=0|1                             # the stored target, D5's fallback applied
  [--license_mask=0]                              # the demo target only
  --launcher_profile="<resolved profile path>"
  + every managed row whose value differs from its compiled default
```

The command line is built by `BuildLaunchCommand` (`src/game_launch.{h,cpp}`), which is pure
string work — no process, no window — and `--print-command` prints exactly what it produces.
That is deliberate: the contract is then a unit test
(`tests/launcher_launch_tests.cpp`, `ctest -R launcher_launch`) and the dry run and a real
start are the same code path.

Three rules come from the contract and are worth stating where they are implemented:

- **A row is passed only where the profile moved it off its compiled default.** The whole
  registry is never passed: for every other row the game's own `rb_blitz.toml` outranks the
  profile (D3), and passing a value the user never chose would take that away.
- **A value that is text is quoted**, and every path is quoted, with the Windows rule for
  backslashes before a quote. A folder with a space in it is one argument.
- **The executable is the first token of the command line**, even though `CreateProcessW` also
  takes it separately: the C runtime reads `argv` from that one string, and without `argv[0]`
  the game would read `--game_data_root` as its first argument.

Starting it is the other half: the launcher **saves first** when something is unsaved (a run
that did not see the change on screen would be a bug nobody could explain), checks that what the
game is about to read is usable, refuses politely when the game's executable or the game data is
missing, quotes everything through `CreateProcessW` with the install folder as the working
directory, and then keeps the process handle so the button reads *Game is running* and stays
down until the game exits — a second copy of the title writing one save folder is not something
to discover by trying.

**Before spawning, two things are checked** (`LaunchReadiness`), because the game applies the
profile's `[settings]` rows itself and treats a file it cannot open exactly like one that is not
there: it starts on the compiled defaults and says so only in its own log. So the profile the
game is about to read must be readable, and the folder holding it must be writable — a settings
folder that is not writable is reported before the game starts, not silently after it. A profile
that has never been written is not an error: there is nothing to read, and the defaults are the
right answer.

**The profile is a config source as well as an argv one** (D3's rank 4,
`src/launcher/profile_apply.cpp`). The launcher's own *Launch Game* passes every moved row as
`--<key>=<value>` (rank 1), so it alone would cover every launch made through this window — but R4
also promises a game started on its own sees the launcher's settings, and a double-click with no
launcher in front of it does not carry that argv. So the game applies the profile's `[settings]`
rows at the config rank before the SDK loads its own `rb_blitz.toml`, in
`RbBlitzApp::OnConfigurePaths`. The ranks then read as D3 describes: a command-line flag outranks
the profile, the profile and the game's file are equal rank and the game's file is applied second,
so it wins — the same rule the precedence badge shows.

**The exact command line is always available.** With the *Copy command line* bar button gone — the
bar is a place to finish a session, not a developer console — `FormatLaunchCommand`'s bytes are
shown where a developer would want them: the failed-start modal. When a start fails, the whole
detail is shown as a modal instead of one bar line, because it carries three things: the reason,
the command line, and the folder the game writes its own log into (`<install folder>\logs`, a file
named like `rb_blitz_001.log`) — the only record a WIN32-subsystem start leaves.

The launcher itself stays open on a failure, so the user can fix the folder and try again.
`Start` on a pad is the same launch without the mouse (A3), refused in the same cases the button
is disabled in.

## Checking for updates (D19)

The bar's leftmost control, *Update*, is the one thing in the launcher that talks to the
network, and it is built to be forgettable: it checks on every start, it never reports its own
failures, and it asks the user exactly once per release.

**The check.** One HTTPS GET of a release manifest, on a worker thread so the window appears
whether or not the network answers (`src/update_launcher.cpp`; WinHTTP with short timeouts and
the machine's own proxy configuration). The URL is `manifest_url` in
[`installer/config/pins.toml`](../installer/config/pins.toml), compiled in at configure time, so
a launcher and the installer that shipped it cannot point at different releases. An empty
`manifest_url` — a build with no release channel — checks nothing.

The URL names the *latest* release's asset, which is what makes the check work forever without
being rebuilt: GitHub resolves `/releases/latest/download/update.toml` to the newest release.

**The rule** (`src/update_check.cpp`, pure and unit-tested) is four questions:

1. Did the fetch and the parse succeed? A missing network, a 404, a manifest with a schema this
   build does not understand — all of them end the same way: *nothing to offer*, and nothing is
   said about it. This is why the tooltip's "you are running the newest release" is not a
   promise: a launcher that cannot check cannot tell.
2. Is the manifest's `version` newer than the version of the launcher that is running
   (`RBBLITZ_LAUNCHER_VERSION`, from the same pins file)? After an update the payload has
   replaced the launcher, so the launcher *is* the newer build and the check settles on its own
   — there is no "installed version" to keep in step.
3. Does the release name a payload an updater could install? A release built with its payload
   inside the setup executable (`[payload] url` empty) publishes no `payload_url`, so there is
   nothing to install and no update is offered.
4. Was this release already declined? `[update] declined_version` in `launcher.toml` records the
   last version the user answered *Not now* to. The question is asked when the available release
   is *newer* than that record, which gives D19's two behaviours exactly: never twice for the
   same release, and once for each new one.

**The button.** It is always in the bar — it is the last row of every tab's ring like Close,
Save and Launch — and only its *state* changes: disabled while checking and when there is
nothing to install (the tooltip says which of those it is), enabled when there is. A bar that
grew a button a second after it opened would move everything beside it. Pressing it opens the
prompt.

**The prompt.** One modal, one question, and the answer is remembered:

- *Update now* starts the updater and the launcher **leaves** — the updater has to be able to
  replace the executable this window is running in, and it opens the launcher again when it is
  done.
- *Not now* (or Escape) writes the version into the profile. The button stays available for the
  rest of the session and every later one; the question does not come back for that release, and
  does come back for a newer one.
- If there is no updater to run — a launcher installed by hand, or by an installer that predates
  this — the release page opens instead and the version is recorded the same way: the user has
  been told, which is what "never asked twice" is about.

**The updater** is `%LOCALAPPDATA%\rb_blitz\update\RockBandBlitzUpdater.exe`: a second build of
the installer wizard, put there by the setup executable and deliberately not in the install
folder ([installer/README.md](../installer/README.md), section "Updating"). It fetches the same
manifest for itself, installs the build it names, and asks for the game files again only when the
release says it has to (`requires_game_data`). The launcher never runs it with arguments: what to
install is the release's own business.

**The check is inspectable.** `--dump-update[=<path>]` runs the same three steps in the
foreground and prints what the launcher would decide — the manifest's version, whether an update
would be offered, whether the user would be asked, what the button would say, and where the
updater is — including the reason a check produced nothing. It is the answer to "why did the
launcher not offer me the update", and a build that has to exercise the whole path without a
release existing points itself at a test server:

```powershell
cmake --preset win-amd64-release -DRBBLITZ_UPDATE_URL=http://127.0.0.1:8731/update.toml
rb_blitz_launcher.exe --dump-update=out\update-report.txt
```

## The Controller tab, and button mapping (D16)

The two schema rows — *Mouse support* and *Guide button pass-through* — are the controller
settings the game has. Below them is the remap, which is not a setting but a table:
`[remap]` in the profile, one row per pad control the user rebound and nothing at all for the
ones they did not.

There is no input-source choice anywhere, deliberately. The game is single-player, so the
launcher listens to **every** connected pad, the keyboard and the mouse in the same frame, and a
keyboard assigned here is what makes the keyboard press a pad button at all — the SDK's SDL
driver maps no keys to pad buttons. The pads it reads are the ones the launcher has open
(`PadRegistry`, A3): SDL reports a pad's state only through an opened handle, so a capture that
queried SDL for itself would read silence and look exactly like a pad nobody was touching.

**Every control a 360 pad has is listed**, bound or not, because the list is the answer to "what
can I rebind?". A row shows its control, what the control answers to, and two buttons — three
columns that each take an equal share of the width, so the three land at even intervals down the
whole list rather than at whatever distance their own text happens to make. The buttons sit about
two thirds across, which is where the schema tabs put the widget their own rows drive, and well
clear of the scrollbar this list always brings:

- **Assign** listens for three seconds and adds the first input it sees, from any device.
  Adding rather than replacing is what makes *several inputs, one button* work: press Assign,
  press the pad button; press Assign again, press the key. Nothing captured leaves the row
  alone, and the countdown is on screen the whole time.
- **Reset** removes the user's own row, which puts the control back to the default below.
  *Reset all bindings* does that for every control at once.

**Every control already answers to its own pad button *and* a keyboard key**, so a keyboard is a
complete stand-in for the pad without editing anything: the D-pad mirrors WASD for the left hand,
the four face buttons mirror the pad's diamond on `I`/`J`/`K`/`L` for the right, the shoulders are
`Q`/`E`, the stick clicks are `V`/`N`, and Back/Start are Backspace/Return. That is
`remap::DefaultSources` (`../src/launcher/remap.h`), and it is what the game applies: a `pad:`
source reads the pad's state *before* any rewriting, so the pad behaves exactly as it did and the
key is an addition. The two analog triggers and Guide have no default — a keyboard cannot stand in
for a trigger's travel, and Guide belongs to the platform.

The bindings are stored only when the user changes one, so a profile nobody has rebound has no
`[remap]` table at all and the defaults are what both processes read. A control bound to an *empty*
list is "nothing presses it", which is writable by hand (the grammar allows it, the panel shows it
in warning colour) even though the panel does not offer it.

The capture is polled rather than event-driven, because the launcher's event pump already
belongs to ImGui: SDL is asked what is down each frame and a capture is a **rising edge** over
the previous frame. That is also what keeps the click that started the listen from assigning
itself — the mouse button is already down when the listen begins, so it has no edge left — and
while a listen runs, Escape cancels it and Enter/Space capture rather than activating whatever
the ring is on.

### The `[remap]` grammar

Shared by both executables ([`../src/launcher/remap.h`](../src/launcher/remap.h)); the launcher
writes it and the game reads it, so the two cannot disagree about what a binding means.

| Part | Spelling | Meaning |
| --- | --- | --- |
| key | a control name: `a`, `b`, `x`, `y`, `left_shoulder`, `right_shoulder`, `left_trigger`, `right_trigger`, `back`, `start`, `guide`, `left_thumb`, `right_thumb`, `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right` | the control being rebound |
| value | a `, `-separated list of sources | what presses it. Empty means nothing does |
| `pad:` source | a control name, as above | the pad's own control, read from the pad's state *before* any rewriting |
| `key:` source | an SDL scancode name: `Space`, `F1`, `W` | a keyboard key |
| `mouse:` source | `left`, `right`, `middle`, `x1`, `x2` | a mouse button |

```toml
[remap]
y = "pad:x, key:Space"   # Y answers to X and to Space; the pad's own Y no longer reaches the game
left_trigger = ""        # nothing presses the left trigger
```

Two properties of the rewrite matter and are pinned by `tests/launcher_remap_tests.cpp`:

- **Every binding reads the pad as it was**, so a pair that swaps two controls both sees the
  original state and neither can feed the other — `a = "pad:b"` and `b = "pad:a"` is a swap
  rather than a chain.
- **A trigger target is driven to the end of its range**, not to "on": the guest reads the
  trigger as an analog value, so there is no bit to set. An unbound trigger keeps its travel.
  A `pad:left_trigger` *source* uses the same threshold the SDK does for its own digital view of
  it (`kTriggerThreshold`, the SDK's `HID_SDL_TRIGG_THRES`), so a binding and the SDK agree
  about what "the trigger is pressed" means.

A row whose key or value this build does not understand is kept in the file verbatim and left
alone, which is how a newer launcher's binding survives a round trip. A value that is only
*partly* understood is kept verbatim too: applying `pad:x` and quietly dropping the source beside
it would look like a binding that works when half of it does not.

### How the game applies it

`src/input/remap.cpp`, installed from `OnPreSetup` beside the mouse's driver. The seam it needs
is one the SDK grew for it — `InputSystem::SetStateFilter`, in
[`../patches/rexglue-sdk/0010-input-system-state-filter.patch`](../patches/rexglue-sdk/0010-input-system-state-filter.patch)
— because it *cannot* be a driver: `GetState` merges every device assigned to a user and merging
only ever adds input, so a driver can add a button but cannot take one away. The filter runs once
on the merged state, on its way to the guest, and rewrites the buttons the profile binds.

The game resolves the profile with the launcher's own `ResolveProfilePath`, so `launcher_profile`
(the launcher's `--launcher_profile`) and `RBBLITZ_LAUNCHER_PROFILE` mean the same thing to both
processes. A missing or unparsable profile is not an error: the pad is left alone.

One known limitation, deliberately not papered over: `GetKeystroke` reports the *pad's* own
transitions, so a remapped button emits a keystroke for the control the pad has rather than the
one the guest will see. Nothing in this game reads keystrokes for selection — the menus are
polled — and fixing it would mean reimplementing the SDK's keystroke queue.


## Building the table

```
cmake --build out/build/<preset> --target rb_blitz_launcher_settings
```

The target builds `rb_blitz_embed_settings`, runs it over `config/settings.toml`, writes
the header and prints what it produced:

```
embed_settings: 21 rows in 13 groups (general 3, graphics 16, controller 2)
```

A schema the tool refuses fails the build. It refuses, deliberately:

- a **duplicate `key`** — two rows would fight over one profile entry and one flag;
- a **missing or empty `tooltip`** — the bottom bar names the focused row's tooltip by
  construction ([D7](../docs/plans/launcher-plan.md));
- **`applies = "live"` without an evidence comment** — a live claim nothing supports;
- a **`group` that is not declared** for that tab, an unknown `tab`, `kind` or `argv`,
  an `enum` without `choices` (or whose default is not one of them), a `path_*` row
  without `validate`, and a `tab = "experimental"` (there is no such tab,
  [D14](../docs/plans/launcher-plan.md));
- **`min`/`max` on a non-numeric row, one of the two without the other, a `min >= max`, or a
  default outside the range** — the bounds are what the widget is built from, so a bad pair
  would be a slider that cannot show the row's own default.

## The fields

| Field | Meaning |
| --- | --- |
| `key` | The cvar name; also the profile key and, when `argv = "flag"`, the `--flag` the launcher passes to the game |
| `tab` | `general`, `interface`, `graphics` or `controller` — which is also the tab order. The key is the schema's own name for the tab; `DisplayTabName` is what the strip shows (`graphics` reads as **Audio / Video**) |
| `group` | Must match a `[[group]]` declared for the same tab |
| `label` | The row's text in the tab |
| `kind` | `bool`, `int`, `float`, `enum`, `string`, `path_dir`, `path_file` |
| `default` | The compiled default, as written. The launcher compares the profile against it to decide what to pass on the command line; an explicitly quoted empty string (`default = ""`) is a real default, as it is for the two path rows |
| `applies` | `restart` or `live` — see below |
| `tooltip` | The user-facing sentence shown in the bottom bar. Never empty |
| `argv` | `flag` emits `--<key>=<value>`; `none` is a launcher-level row the launcher translates itself |
| `choices` | `enum` only: the allowed values |
| `choices_from` | `enum` only, optional: where the choices really come from when the *build* decides them. `gpu_backends` is the only rule there is — the backends CMake compiled in, passed to the embed step as `--backends=` — and such a row declares no `choices` of its own |
| `min` / `max` | `int` / `float` only, optional: the range the row's slider is bounded to. Both or neither, and the default must sit inside it. They are the cvar's own `.range(...)` where it has one, so the slider cannot offer a value the runtime would clamp |
| `max_from` | `int` / `float` only, optional: a rule that lowers `max` to a fact about the machine, never above it. `cpu_cores` is the only rule there is — the running machine's logical processor count, applied at draw time because the build cannot know it. *General → Paths → DLC scan threads* uses it, so the slider stops at the cores the machine has. Such a row still declares `min`/`max` |
| `visible` | Optional: a rule the *machine* or the *install* has to satisfy for the row to exist at all. Two rules exist — `multi_monitor` (*Audio / Video → Window → Monitor* is hidden when one display is attached, because there is nothing to choose between) and `ultimate_installed` (*Interface → Main menu → Rename Mod Settings* is hidden when the Ultimate payload is not installed, because the row it configures belongs to the mod) |
| `validate` | `path_dir` / `path_file` only: `exists`, `dlc_layout` (the structured DLC tree or a flat song library of loose containers) or `inside_game_root:forbid`, separated by `\|` |

`argv = "none"` exists for exactly one row: `launch.target` is not a cvar, and the
launcher turns it into `--ultimate_mode` / `--license_mask` itself
([D4](../docs/plans/launcher-plan.md)). The tool refuses `argv = "flag"` on a key that
is not a cvar name, because passing `--launch.target=ultimate` to the game would be an
unknown key at boot.

Tooltips are the row's own help text, rephrased for a user rather than a developer. They
are multi-line TOML strings, so they read as sentences in the file and become `\n`-joined
`string_view`s in the header.

## Groups, and the empty-group rule

A group with no rows yet is declared `status = "unavailable"` together with the single
line that names it:

```toml
[[group]]
name   = "Manual"
tab    = "controller"
status = "unavailable"
note   = "Per-button remapping is not available in this build."
```

That is how an unbuilt category is recorded without drawing a disabled widget that looks
like a setting ([D14](../docs/plans/launcher-plan.md)). A group that *has* rows must not be
`unavailable`; the tool refuses that combination. The five groups declared this way today are
the General tab's *Ultimate (advanced)*, the Audio / Video tab's *Developer*, and the Controller
tab's *Devices*, *Keyboard* and *Manual* — the work
[§1.1](../docs/plans/launcher-plan.md) defers past M1.

**The tabs do not draw them.** A group with nothing in it is left out of its tab entirely — the
name and the note included — so the window is spent on rows that do something. Both stay in the
schema and in `--dump-layout`'s output, marked `unbuilt`, which is where a reader goes to see
what is not built yet. The one place a "this is missing" message earns its space is the Ultimate
*install* action, which is a button that does something rather than a sentence about a payload
that is not there.

## `live` vs `restart`

`applies` is the row's real answer to "does changing this need a restart?", and it is
evidence-backed, because a cvar's lifecycle tag is not a promise that a value applies
live ([D12](../docs/plans/launcher-plan.md)). A `live` row carries a comment naming what
makes the change land — a registered change callback, or the code that re-reads the value
on every use:

```toml
applies = "live"   # live: RegisterChangeCallback("fullscreen", ...) in rex_app.cpp:353 calls Window::SetFullscreen
```

Everything else is `restart`, including rows whose cvar is registered hot-reloadable but
whose value the runtime only consumes while starting up. The six `live` rows today, and
their evidence, are:

| Row | What makes it land |
| --- | --- |
| `fullscreen` | `RegisterChangeCallback("fullscreen", …)` in `rex_app.cpp:353` |
| `vsync` | The GPU vsync worker re-reads it every iteration (`graphics_system.cpp:164`) |
| `present_letterbox` | Re-read on every paint (`presenter.cpp:925`, `:960`) |
| `present_safe_area_x` | Re-read on every paint, when overscan cutoff is on (`presenter.cpp:944`) |
| `present_safe_area_y` | Re-read on every paint, when overscan cutoff is on (`presenter.cpp:908`) |
| `audio_mute` | Re-read for every buffer the SDL audio callback fills (`sdl_audio_driver.cpp:198`) |

`input_backend` is where that row *used to be*, and it is worth keeping the reason: its cvar is
hot-reloadable, but the input system is built once from the factory, so the row was `restart` and
said so. It is gone now for a different reason — with the remap in place the launcher listens to
every device at once (D16), so "which backend" is not a user-facing choice any more, and the
`input_backend` cvar keeps its own default.

## If something goes wrong (A5)

Two things a settings file can do to a launcher, and what to do about each. Both switches are
command-line only on purpose: they are for the case where the window is not usable enough to
change a setting in it, so a switch reachable only from the window would be no switch at all.

**The window opens at a nonsense size.** A `launcher.toml` whose `[window]` size is smaller than
the content can use - or larger than the display - leaves the window unusable. The keys are the
launcher's own now, so a bad binding can no longer lock the ring, and the size is the one thing a
profile can still set that can. Recover with:

```
rb_blitz_launcher.exe --safe-mode
```

Safe mode starts the launcher on the compiled default for its **own** behaviour: the size the
window opens at. Everything else still loads - the keys are always the fixed defaults, and the
settings rows, the launch target and `[remap]` load as usual - and the bottom bar says safe mode
is on rather than leaving you to wonder which window you are looking at.

It is still a launcher you can *save* from, and that is deliberate. The one write it does skip is
A1's window size - the window opened at the default size, so the size it ends at is not a choice
you made, and a switch for reading a damaged profile should not be the thing that writes to it.

**The settings file cannot be read at all.** A `launcher.toml` with a TOML error in it is
reported on the bottom bar and never written over (D2: a hand-edited file is never lost to a
Launcher that did not understand it). The ways out, in the order worth trying:

- **Fix the line.** The bar prints the path and the line's own complaint.
- **Import a good profile** - *Import* on the General tab replaces what is on screen with a file
  you choose, and it stays live when the file on disk did not parse, because reading a profile
  you trust is the way out of a damaged one.
- **`--safe-mode`, then Save.** With the switch on, Save *replaces* the file instead of refusing
  to touch it: what is written is the compiled defaults plus whatever is on screen, and the bar
  says so before the button is pressed. This is the recovery for a user with no second copy, and
  it is the only case where safe mode can write something the ordinary path would refuse.

`--launcher_profile=<path>` points the launcher at a profile somewhere else entirely, which is
also how a profile that is not in the settings folder at all can be used without moving the
settings folder to it. `--dump-profile` prints what the launcher made of the file - where it is,
whether it can be written, whether a save would change it, and every row the game's own
`rb_blitz.toml` decides - without opening a window, which is the fastest way to find out *why*
something is wrong.

`--no-update-check` makes no network request for the run (D19), so the bar's *Update* control is
inert and says so. It is what a capture harness passes - a button whose state depends on whether
a release exists cannot be part of a pixel comparison - and it is the switch for a machine that
cannot reach the release page anyway: a firewall, a proxy that answers with a login page, or a
metered link. The check itself is written to be invisible on such a machine, so the switch is
about not waiting for it rather than about what it would say.

## How it is checked

Four ways, in the order of what they cost:

- **The unit tests**, named after the module they cover — `ctest -R launcher`. The seven
  targets, what each one pins and where the host harness lives are in
  [build-and-run.md §3](../docs/build-and-run.md); the pattern is the same as the game's:
  no window, no SDL, no game image.
- **The headless reports.** `--dump-layout`, `--dump-display`, `--dump-profile`,
  `--dump-general`, `--dump-prefill`, `--dump-update` and `--print-command` each write what the
  launcher decided to a file and exit, before any window is created. They are the reason a claim
  about a row, a path, a target, an update or a command line is asserted on *text* on a build
  machine instead of read out of a screenshot by eye.
- **The captures.** [scripts/capture_launcher.ps1](../scripts/capture_launcher.ps1) starts
  the launcher on a fixture profile, drives it with synthetic keys, captures the client
  area and measures crops of it against each other with `frame_diff.ps1`. Its claims are
  the three the text cannot carry: a press moves the ring on, a press that
  leaves a row rewrites the bar's help line, and a tab switch changes the body — with a
  session that pressed nothing as the control, so "everything is different" cannot pass as
  "that action worked".
- **The installed run, end to end.**
  [scripts/acceptance_launcher.ps1](../scripts/acceptance_launcher.ps1) silent-installs the
  built setup into a scratch folder, then boots the game from the installed launcher's own
  command line four times: the retail route with no payload on disk, the Ultimate route
  after the install's helper adds the mod, the demo trial, and a save folder overridden into
  `out/`. It is the only one of the four that checks the *installed* artefacts and the game
  they boot (what each leg asserts is in
  [build-and-run.md §4](../docs/build-and-run.md)).

The capture harness is also where the ring's shape is visible. Up and Down walk the *rows*, and
Left and Right walk a row's *options*: a row that offers a choice is one row with one option per
choice (the launch target's common/trial/ultimate are rows 0, 1 and 2 of the General tab), so a
Down press steps over the whole row and leaves the bar's help saying the same sentence — the
sentence belongs to the row. The walkthrough leg therefore laps each tab's *rows* and checks the
trace's `rowindex=`, and the harness asserts the presses that crossed a row boundary rather than
counting entries. `-SkipWindow` runs the two text layers alone, which needs no desktop.

What the installed run measures about how settings travel is worth knowing when changing
any of this: a row reaches a game the launcher starts as a `--key=value` argument
(Contract 3, `src/game_launch.cpp`), and the game applies the profile's `[settings]` rows
itself (D3 rank 4) for the launches without that argv, while it reads the profile only for
`[remap]` besides. A profile's *top-level* key still reaches no cvar — the game applies the
`[settings]` table — and the acceptance legs assert the `[settings]` spelling the launch
command reads, because that is what a launcher-launched row really travels as. The two
findings E3 measured — a profile that was not applied at all, and the two path rows whose
General-tab store (`[launch] user_data_dir` / `dlc_dir`) disagreed with what the launch
command reads (`[settings] user_data_root` / `dlc_root`) — are both closed;
[the plan's open questions](../docs/plans/launcher-plan.md) §11 records the resolution.

## How to add a row

1. Check the setting really exists — a cvar in the SDK or in `src/`. A row for a feature
   that does not exist (master volume) or is compiled out (`present_effect` and the
   CAS/FSR parameters behind `REXGLUE_ENABLE_FIDELITYFX=OFF`) does not belong in the
   table: no row is added to fill a category ([D14](../docs/plans/launcher-plan.md)).
2. Read the setting's own change callback or its use site and decide `applies` from it,
   not from the lifecycle tag. If the cvar declares a `.range(...)`, put it in `min`/`max`
   so the row's slider is bounded by the runtime's own limits.
3. Add the `[[setting]]` to `config/settings.toml` under a declared group, with a
   tooltip written for the person reading the bottom bar. The tab is the group's, and the tab
   set is fixed by the schema compiler (`tools/embed_settings.cpp`): General (the install and
   the launch), Interface (the game's own screens), graphics ("Audio / Video"), controller.
   A row whose setting is none of those is a new tab, which is one entry in that list plus
   the groups it holds — not a row.
4. Rebuild `rb_blitz_launcher_settings` and check the printed row count moved.

That is the whole change: the shell renders whatever the table says, in the table's order,
so a new row appears in its tab, in its group, with its `kind`'s widget — no launcher code
is edited to add one (A1, B2).

If the setting's category is not built yet, declare the group with
`status = "unavailable"` and a `note` instead of adding a placeholder row; the tab will hide
it and `--dump-layout` will still name it.

## What is deliberately not here

- **Compiled-out rows.** `present_effect` and the CAS/FSR parameters exist only behind
  `REXGLUE_ENABLE_FIDELITYFX`, which is `OFF` in this build (`present_effect` itself
  collapses to `bilinear`). Hiding them is the rule in
  [D12](../docs/plans/launcher-plan.md): a build flag is not something a user can act on
  from the launcher.
- **The renderer, as a hand-written list.** The Renderer row exists, but its choices are not
  written anywhere by hand: `choices_from = "gpu_backends"` takes them from CMake, so the
  launcher cannot offer a backend the payload was not built with. A D3D12-only build (this one
  by default) therefore shows one choice rather than a Vulkan entry that would be refused at
  boot. `gpu_plugin` is a different thing — it selects the GPU *emulation plugin*, not the
  renderer, and the game already sets it to `xenos` itself.
- **A free resolution field.** Resolution is an `enum` over exactly the
  presets the runtime's own parser accepts
  (`rexglue-sdk/include/rex/graphics/video_mode_util.h`: 720p, 1080p, 1440p, 4k), because
  those are the sizes known to be safe — the free-form `1280x720`-style value, the guest
  video mode's own width/height, and a guest refresh rate are all absent on purpose.
- **A frame-rate field.** Unlocking the frame rate changes how the title paces itself and is a
  project non-goal, so nothing offers to change it — the same reason the guest refresh rate is
  not a row.
- **A window-size row.** The window's own size is remembered from the last time the user
  dragged it (A1), and the resolution preset sizes the game's startup surface
  (`window_sdl.cpp`); a row for either would be a second control for something a gesture
  already does.
- **Master volume.** The SDK has no `audio_volume` cvar yet, so the Audio group has mute
  and buffer size and nothing else.
- **An input-source row.** *Controller → Input*'s `input_backend` row is gone: the remap
  listens to every device at once, so there is nothing to select (D16).
