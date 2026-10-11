# In-game settings — the native panel/list API and the screen-registration gap

Status: **investigation result, one blocker resolved by measurement, no screen built yet.**
This closes the "engine's panel/list script API is untraced" item of
`docs/plans/in-game-settings-bridge-findings.md` §9–§10 and the "how a panel name resolves to its
definition file" blocker of §5 here: §8 measures the rule, and §9 measures the delivery route that
makes a new screen reachable without editing shipped content. What is left for G1 is the authored
definition and the bridge, not a reachability question.

Evidence labels: **[cited]** = measured from a file in this checkout, **[tree]** = this working
tree, **[boot]** = measured from a real run of the game (log paths under `out/_swap/p1b/`).

Read with: `docs/plans/in-game-settings-plan.md` §3/§5.2 (what the screen must be),
`docs/plans/in-game-settings-contracts.md` (frozen types), `docs/engine/main-menu-flow.md` (the
compiled-DTA format and the read-hook delivery).

---

## 1. A panel is authored DTA, and the rows are data

Decoded from the locally extracted panel definitions **[cited]**
(`out/_swap/dtb_tree.py`, `assets/game/360/ui/...`). The pause panel, `ui/pause/gen/pause.dtb`
(6,828 bytes), in the order the file declares it:

```
{new UIPanel pause_panel
  {file pause.milo}                                   ; the widget scene this panel draws
  {focus pause.lst}                                   ; the list the focus starts on
  {enter {do {this populate_menu}
             {options.lst set_data (pause_tutorial pause_controls calibration
                                    audio_video_options mod_settings credits)}
             {this update_focus {get_focus}}}}
  {populate_menu {do {var pause_array {array (pause_resume splash_upgrade pause_options
                                            #ifdef HX_XBOX pause_achievements #endif
                                            pause_restart pause_quit pause_main_menu
                                            pause_exit_game)}}
                     {if {! {trial_mgr get_trial_mode}} {remove_elem pause_array splash_upgrade}}
                     {pause.lst set_data {var pause_array}}}}
  {get_focus {{this loaded_dir} get focus_component}}
  {restart_song {do ...}} ...}
```

The main menu's panel, `ui/splash/gen/splash.dtb` (8,808 bytes in the payload ark), is the same
shape: `{new UIPanel splash_panel {file splash.milo} {state_on_enter start} {enter {...}}
{exit {...}} ...}`, and its row list is the array `docs/engine/main-menu-flow.md` already documents.

**So a native screen is three pieces**, and only the first is authored:

| Piece | What it is | Who supplies it |
| --- | --- | --- |
| Panel definition | a compiled DTA (`.dtb`) declaring `{new UIPanel <name> {file <milo>} {focus <list>} {enter ...}}` | **us** (this is the "authored definition") |
| Widget scene | the `.milo_xbox` the panel draws from (`pause.milo`, `splash.milo`) | retail, reusable |
| List resource | the `*.lst` the panel focuses and sets data on (`pause.lst`, `start.lst`) — declared in `ui/resource/lists/gen/list_*.milo_xbox` | retail, reusable |

## 2. The row lifecycle (measured)

| Step | DTA | Note |
| --- | --- | --- |
| Populate | `{<list>.lst set_data <array>}` | rows are **data**: an array of symbols. The panel builds the array in a `{do {var x {array (...)}}}` block. |
| Conditionally drop | `{remove_elem <array> <symbol>}` | already used by both panels; the title tolerates a name that is not there. |
| Read selection | `{switch {<list>.lst selected_sym} {<row-symbol> {...}} ...}` | one `switch` case per row **name** — the identity is the symbol, not an index. |
| Focus | `{focus <list>.lst}`, `{<this> update_focus {get_focus}}`, `{get_focus}` → `focus_component` | the engine owns scroll/focus. |
| Navigate | `{ui goto_screen <screen_name>}` | e.g. `{splash_leaderboard {ui goto_screen career_leaderboard_screen}}`. |

**Consequence for the feature.** A row is a symbol in an array, and its action is a case keyed by
that symbol. Adding a row and its action is therefore a *data* change — which is exactly what
`HideRows`/`RenameLabel` already do in reverse for `splash.dtb`.

## 3. Where screens and panels are loaded

Decoded `ui/gen/ui.dtb` (2,221-byte body; the UI framework's own data) **[cited]**:

```
{init
  {include init.dta}
  {app_label_register}
  {pause_panel load} {pause_exit_game_panel load} {game_fade_out_panel load} {error_popup_panel load}
  {options_panel load} {store_browser_panel load} {background_panel load} ...   ; ~25 loads
  {set {var first_screen} splash_screen}
  {ui my_init}
  {ui goto_screen {var first_screen}}}
```

So the boot path is: `ui.dtb`'s `init` **loads each panel by name**, sets `first_screen`, and calls
`{ui goto_screen}`. Panels are objects loaded by name; the *screen* is reached through
`goto_screen`.

`ui/gen/init.dtb` is only `include`s, and `ui/gen/ui_objects.dtb` declares panel **types and their
properties** (`PanelDir` → `types` → `options_panel`, `loading_panel`, …) — a schema for the script
type system, **not** a name → file table.

The engine's method names are interned in the image **[cited]** (`out/_swap/blitz_image.bin`,
offset = address − 0x82000000):

| Method | Address |
| --- | --- |
| `goto_screen` | `0x8209BDA8` |
| `set_data` | `0x8209F52C` |
| `selected_sym` | `0x8209F584` |

and the DTA method-name dispatcher that interns them is at `generated/default/rb_blitz_recomp.39.cpp`
around line 18980 **[tree]** — one function comparing the requested symbol against a chain of
interned names, each leading to its handler. That is the **single best hook target** for a runtime
observation of "who sets the main-menu list, with what".

## 4. What this unlocks

- The authored definition's *shape* is now known precisely (§1), so it can be written today. The
  authoring capability exists: `src/ui/dtb_author.{h,cpp}` produces well-formed compiled DTA from a
  node tree, and its test round-trips **six shipped definitions byte for byte**
  (`splash.dtb`, `pause.dtb`, `ui.dtb`, `ui_objects.dtb`, `global.dtb`, `exit_game_confirmation.dtb`).
- The four requested row kinds are **row-widget** choices, not script ones: a boolean, a horizontal
  select, a submenu entry and a compound row are drawn by the list resource's row type, and
  `docs/plans/in-game-settings-plan.md` §3 already names the retail references (Ultimate's compound
  row is the model; HUD is a submenu; Gems is a horizontal select). Binding a row symbol to a widget
  type is the remaining unknown *inside* this area and belongs with the first attempt at a screen.

## 5. The gap, stated exactly

Two things are still unknown, and both are about **reachability**, not about the screen's contents:

1. **How a panel name resolves to its definition file.** `{pause_panel load}` finds
   `ui/pause/gen/pause.dtb`, but no shipped table maps the two, and neither `ui.dtb` nor
   `ui_objects.dtb` carries one. Until that rule is known, a new panel name cannot be given a file.
2. **Reaching a screen we add.** The `load` list lives *inside* `ui/gen/ui.dtb`, and the ark index
   fixes that file's length, so adding `{advanced_settings_panel load}` is a length change. The
   existing byte-neutral machinery can only pay for bytes it first frees *inside the same file*
   (`menu_options.cpp`: rows → an unused `#ifdef` macro name); `ui.dtb` is 2,221 body bytes with no
   removable rows to spend.

## 6. The narrowed next experiments (in order)

1. **Trace the load path.** Hook the read path that resolves `pause_panel`'s definition
   (`menu_filter.cpp` already owns `NtReadFile`; a diagnostic that logs the *path* of each read
   during `ui my_init` answers this in one boot) — **[evidence: a boot log naming the file read for
   a known panel name]**.
2. **If the rule is a convention** (e.g. `ui/<group>/gen/<file>.dtb`), a name we choose may resolve
   to a path we can serve from the read hook as a **project-owned virtual file** — the parent plan's
   §5.2 fallback — with no edit to any shipped file at all.
3. **If instead the load list must be extended**, do it byte-neutrally: `ui.dtb`'s `init` is an
   array of commands, and the same `#ifdef`-name donor trick `HideRows` uses can pay if a donor
   exists in that file; otherwise this route needs a bootstrap byte budget the parent plan §5.2
   requires to be independently proven.
4. Only then: the four row kinds, entry/cancel/back with keyboard and pad, and G1.

## 7. Honest status

| Item | State |
| --- | --- |
| Panel/list script API | **Known** (§1–§3), measured from shipped files |
| Authored definition capability | **Exists and is verified** — `src/ui/dtb_author.*`, six real-file round trips, `ctest dtb_author` |
| How a panel name resolves to a file | **Answered** (§8) — a panel name is a script object; the file is a `[include]`, and the include rule is measured |
| Reaching a screen we add | **Answered** (§8.2) — the patch layer takes new records and a longer `init.dtb`; no byte budget |
| An authored definition loaded by the engine | **Done** (§8.4) — two healthy boots with a new include and a new record |
| A screen on screen | **Not built**; seven boots were run in §8, none of them drew a new screen |
| Row-widget binding | **Untried** — the last unmeasured piece of the screen (§4, §8.3)

G1 remains unmet. What changed is the *reason*: the blocker is no longer "we do not know how a
screen is written" (we do, §1–§2) but "we do not know how the engine finds the file for a new panel
name, and the shipped list that names panels cannot grow without a byte budget" (§5).

---

## 8. The resolution rule, measured [boot]

The blocker of §5 is answered, and the answer is simpler than the question: **a panel name never
resolves to a file at all.** `{pause_panel load}` is a *method call on a script object*, and the
object exists because `ui/gen/init.dtb` **includes** the file that defines it. So the only rule to
learn was the one behind `[include]`, and the delivered content is a compiled `gen/<name>.dtb`.

**Rule [boot].** For an include of `<dir>/<name>.dta` executed from a script at `<root>/gen/<file>.dtb`,
the engine reads **`<root>/<dir>/gen/<name>.dtb`**; an include with no directory keeps the including
script's own directory (`<root>/gen/<name>.dtb`), and `..` resolves against `<root>`. `<root>` is the
directory that contains `gen/`, i.e. `dirname(dirname(including file))`.

Retail correlations that fit (all of `ui/gen/init.dtb`):

| Include | File | Root arithmetic |
| --- | --- | --- |
| `global.dta` | `ui/gen/global.dtb` | no dir → the including script's own dir |
| `pause/pause.dta` | `ui/pause/gen/pause.dtb` | root `ui` + `pause` |
| `store/store_info.dta` | `ui/store/gen/store_info.dtb` | root `ui` + `store` |
| `../config/cheats_funcs.dta` | `config/gen/cheats_funcs.dtb` | root `ui` + `..` → `config` |
| `../ulti/mod_settings.dta` (the mod) | `ulti/gen/mod_settings.dtb` | root `ui` + `..` → `ulti` |
| `run ulti/ulti_init.dta` (the mod) | `ulti/gen/ulti_init.dtb` | same rule for `run` |

### 8.1 The experiment that proved it

A v6 patch archive was built by hand from the retail ark (`out/_swap/p1b/make_payload.py`, which
reuses `scripts/hmx_ark.py`'s writer) and mounted as the payload
(`--ultimate_mode=2 --ultimate_payload_root=<arm>`). Each arm differs in one record. All five boots
used the same disposable user root, `--log_level=debug`, and the project's own UI driver.

| Arm | The patch archive holds | Result |
| --- | --- | --- |
| `arkonly` | `ui/gen/init.dtb` = the retail bytes, byte-identical | **healthy**, 5.18 MB log, 0 violations |
| `shadow` | `ui/demo/gen/demo.dtb` = 64 bytes of `0xAB` | **crashes** early (18 KB log): the record shadowed the base one |
| `probemiss` | `init.dtb` with the 13-byte include `demo/demo.dta` renamed to `p1b/demos.dta`, no target anywhere | **crashes** identically: the include *is* executed, and a missing definition is fatal |
| `probe` | the rename plus `p1b/gen/probe.dtb` = garbage | **crashes** (sizes the same as `probemiss`) |
| `relocate` | the rename plus **`ui/p1b/gen/demos.dtb`** = the real `ui/demo/gen/demo.dtb` bytes | **healthy**, 5.20 MB log, 0 violations, the main menu is reached |

Read together, `arkonly` proves the hand-built archive is well formed; `shadow` proves the patch
layer really replaces a base record **by name**; `probemiss` proves the guest executes the patch
copy of `ui/gen/init.dtb` and runs its `include`; and `relocate` proves the name-to-path rule and
that a **brand-new content record** loads through it. The `relocate` boot reaches the menu
(`shell_music: R9 off; ...`, the R5 row line) instead of dying in the script load.

### 8.2 Why the byte budget stops mattering [boot]

The archive carries its own index, so a patch record's length is its own. The Ultimate payload
proves the same thing: its `ui/gen/init.dtb` is **2,216 bytes** against retail's **2,045** (decoded
`root_arity` 68 against 62 — its six extra nodes are the two `ulti` autoruns, the two `ulti`
includes and the two `pers_tex` ones). Nothing is paid for; the file simply grew.

So the two routes §5 called unreachable are back on the table without a byte-neutral edit:

1. **Extend `init.dtb`** in a patch record and add our own include. No donor, no freed bytes.
2. **Add our definition as a new record**, e.g. `ui/rbbz/gen/advanced_settings.dtb`, and let that
   include name it.

### 8.3 What this does not settle

- The row-widget binding of §4 (which list resource draws a boolean / select / submenu / compound
  row) is still untried. It is now the *only* unmeasured piece of the screen.
- Where the patch archive comes from in the product is an implementation choice, not a question:
  the mod ships one; we must either generate one at boot from the game's own records (the mirror of
  `scripts/hmx_ark.py`, plus the DTB cipher `src/ui/menu_options.*` already implements) or ship our
  own tiny one. **No retail bytes are shipped in either case** — the record contents are read from
  the installed ark, only the edits are ours.
- The `kPatchDiskError`/`kPatchContentDevice` edits and `--ultimate_mode` are how a *payload* is
  mounted today (`src/hooks/ultimate.cpp`). A product route that is not spelled "Ultimate" should
  mount the same device from the settings bootstrap instead of reusing that switch.
- Nothing in this pass touched, wrote to, or distributed any retail or Ultimate file: the archives
  live under `out/_swap/p1b/` and are rebuilt by the script that made them.

### 8.4 An authored definition is loaded [boot]

The parent plan §5.2 item 2 asks for "one authored minimal definition through a recognized virtual
resource or verified engine loader". It is now demonstrated, with no byte budget and no retail file
modified on disk:

| Arm | The patch archive holds | Result |
| --- | --- | --- |
| `authoredmin` | `init.dtb` with a **real include added** (`[include] rbbz/min.dta`) + `ui/rbbz/gen/min.dtb` = 41 bytes compiled by `src/ui/dtb_author.cpp` (one `[define]` root) | **healthy**, 4.97 MB log |
| `authored` | the same, with `ui/rbbz/gen/advanced_settings.dtb` = 373 bytes of the `PanelFixture` — `{new UIPanel advanced_settings_panel {file pause.milo} {focus settings.lst} {enter {...}}}` | **healthy**, 4.96 MB log |

The added include is honest about the budget: `emit-init` reports `2045 -> 2079 bytes, 62 -> 63
roots`, and the patch index carries the new length. The include lands after the file's own
`#ifdef UI_INIT_DTA` guard, which is what the appended root does (§8.3).

The reading follows from the arms above: a *missing* target crashes the boot (`probemiss`) and a
garbage target crashes it (`probe`), so a healthy boot means the record **was found, read and
parsed** as compiled DTA. The two new roots are therefore loaded by the retail engine.

### 8.5 The authoring tool grows an emit mode [tree]

`tests/dtb_author_tests.cpp` now answers three non-test arguments, so the verified encoder can
produce a file a boot can be pointed at:

```
rb_blitz_dtb_author_tests emit-panel <out.dtb>                  # the UIPanel fixture
rb_blitz_dtb_author_tests emit-min   <out.dtb>                  # one [define] root
rb_blitz_dtb_author_tests emit-init  <in.dtb> <out.dtb> <name>  # append [include] <name>
```

The unit tests are unchanged and still pass (`ctest -R dtb_author`).

## 9. Evidence index [boot]

| Path | What |
| --- | --- |
| `out/_swap/p1b/make_payload.py` | reads the retail records, applies the byte-neutral include rename, writes the seven payloads |
| `out/_swap/p1b/run.ps1` | boots one arm against a disposable root and prints the interesting log lines |
| `out/_swap/p1b/{arkonly,shadow,probemiss,probe,relocate,authoredmin,authored}.log` | the seven boots |
| `out/_swap/p1b/init_base.dtb`, `init_probe.dtb`, `init_add_*.dtb` | the retail record, the renamed one, and the two with an include added |
| `out/_swap/p1b/{advanced_settings,min}.dtb` | what `dtb_author` emitted |

## 10. What is left for G1

| Step | State |
| --- | --- |
| 1. The panel/list script API | done (§1–§3) |
| 2. An authored definition loads | **done** (§8.4) |
| 3. Register the screen: `{advanced_settings_panel load}` in `ui/gen/ui.dtb` and the row + `{ui goto_screen}` case in `ui/splash/gen/splash.dtb` | not started; both are now ordinary patch records, so the row can *grow* instead of being paid for out of freed bytes |
| 4. The row-widget binding (which `list_*.milo_xbox` row type draws each of the four kinds) | not started — the last unknown |
| 5. The bridge hooks (`file_exists`/`write_file`, forwarding exactly once) and the typed round trip | host half done, hooks unwritten (`in-game-settings-bridge-findings.md` §4, §8) |
