# The main menu's option list

**The question** ([plans/customization-plan.md](../plans/customization-plan.md) U1, R5): where are the
main menu's options built and filtered — the list's source, the per-option identity, and the flag that
would let one be hidden — and what does it take to hide one without stranding navigation (D12)?

**The answer.** The list is **data**: one compiled DTA, `splash.dtb`, inside each ark, whose option
array the panel copies into its own row list. Its per-option identity is the name the title gives the
row (`splash_leaderboard`, `splash_achievements`, `splash_dlc`, …), and the flag is the guest's own
`#ifdef`: an element of that array whose macro name matches nothing is *skipped* by the DTA loader.
Hiding a row is therefore a byte-neutral rewrite of that one file, and it is delivered at the read
that carries it — nothing is repacked, nothing in `game/` is written, and the retail content is not
needed to build anything (D14).

**The feature.** `enhancements_hide_menu_options` + `enhancements_hidden_menu_options`
([toggles.md](toggles.md), R5), implemented in
[src/ui/menu_options.cpp](../../src/ui/menu_options.cpp) (the pure edit),
[src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp) (the read hook and the checksum row) and
[src/enhancements.cpp](../../src/enhancements.cpp) (the cvars). Off by default.

**The second edit, and §7.** R3 takes the two questions the title asks before it will draw its menu
off an offline install's path. It is the same kind of rewrite as R5 — bytes moved inside one compiled
DTA, delivered at the read that carries it — but of a different file (`server_connect.dtb`, the panel
the title opens when a game is started) and for a different reason: the questions cannot be answered
when there is nothing to connect to, so they are answered as they arrive. It is on by default, and
§7 is what it took.

**Evidence convention** (as in the plan): **[tree]** is this working tree with `file:line`;
**[cited]** is a build product — a log line or a capture from a run on this machine; **[assumed]** is
reasoned and not yet measured.

## 1. The panel

The main menu is a DTA panel object. Read out of the shipped retail file
(`check_trial`, statement of the panel; `$NAME` is `#ifdef NAME` and a lone `0` is the `#endif` the
compiled form stores as a scalar):

```
{check_trial
  {if (== {this loaded_dir get state} main_menu)
     {do {options_array = {array (splash_start splash_leaderboard $HX_XBOX splash_achievements 0
                                  splash_options splash_upgrade splash_dlc $HX_XBOX splash_exit 0)}}}
     {if_else {trial_mgr get_trial_mode}
        {do {remove_elem options_array splash_dlc}   {start.lst set_data options_array}
            {achievements set_allow_achievements FALSE}}
        {do {remove_elem options_array splash_upgrade} {start.lst set_data options_array}}}}}
```

and one `switch` case per row, keyed by the row's own name:

```
{switch {start.lst selected_sym}
  {splash_start       {this goto_main_hub}}
  {splash_leaderboard {ui goto_screen career_leaderboard_screen}}
  {splash_achievements {… display_achievements … {error_popup_panel show_popup achievements_unavailable splash_panel}}
  …}
```

Four properties of that shape are what make the feature safe:

| Property | Why it matters here |
| --- | --- |
| The row list is a copy of the array (the panel writes it, then `start.lst set_data options_array`), not the array itself | editing the file edits the list the guest navigates, so the drawing and the highlight both follow — D12's requirement |
| `remove_elem options_array splash_upgrade` (full mode) / `… splash_dlc` (trial mode) already deletes rows from that list, and the title tolerates a name that is not there | removing a row is an operation the panel performs on itself; a shorter list is a shape it is written for |
| The row's action is a `switch` on `start.lst selected_sym`, one case per row **name** | a case whose row is gone is never taken, so the action table does not have to change with the list — and the identity we match is the same one the switch matches |
| The achievements case's own failure path is a popup (`achievements_unavailable`) | the three rows this feature's default list names are the ones whose screens need Rock Central; offline they are a notice, not a screen |

**[tree]** the module's own reading of that shape is in
[src/ui/menu_options.cpp:268-282](../../src/ui/menu_options.cpp) (the locator) and
[src/ui/menu_options.cpp:440-457](../../src/ui/menu_options.cpp) (the removal); the quoted statements
were dumped from the retail entry on this machine **[cited]** (§7).

## 2. The file

`splash.dtb` — 8,808 bytes in the payload ark, 7,148 in the retail one **[cited]** (§7). Both arks
carry it; the payload's copy is the one that wins when the Ultimate overlay is mounted.

```
[u32 seed][body]
body  = 0x01 | u16 root_arity | u16 line | u16 deprecated | node*
node  = [u32 tag] + payload
```

| Tag | Kind | Payload |
| --- | --- | --- |
| `0x00` int, `0x01` float, `0x06` unhandled, `0x08` else, `0x09` endif, `0x24` autorun | scalar | `[u32 value]` |
| `0x02` var, `0x03` func, `0x04` object, `0x05` symbol, `0x07` ifdef, `0x12` string, `0x20` define, `0x21` include, `0x22` merge, `0x23` ifndef, `0x25` undef | text | `[u32 length][bytes]` |
| `0x10` array `( … )`, `0x11` command `{ … }`, `0x13` property `[ … ]` | block | `[u16 arity][u16 line][u16 deprecated]` + children |

The body is xored with `dtbcrypt`'s keystream: `key = CryptRound(seed)`, one byte per node byte, and
`CryptRound` is the Park–Miller generator with Schrage's division as the title spells it
(`0x1F31D` / `0x41A7` / `0xB14`, `+0x7FFFFFFF` when the result is not positive)
**[tree]** [scripts/hmx_ark.py](../../scripts/hmx_ark.py), [docs/assets.md](../assets.md) § "Compiled
DTA". The keystream is its own inverse, so one routine reads and writes the file.

**The grammar is pinned by a round trip, not by trust.** `HideRows` re-serializes every node it parsed
and refuses the file unless the bytes come back identical
([src/ui/menu_options.cpp:414-424](../../src/ui/menu_options.cpp)). A file this module cannot
reproduce byte for byte is a file it does not understand, and it says so instead of editing it.

## 3. The loader

From the engine's own `DataArray::Load` (public tree, `system/obj/DataArray.cpp`) — the behaviour the
edit is designed around:

| Directive | Effect |
| --- | --- |
| `#ifdef NAME` | pushes `DataGetMacro(NAME) != nullptr` onto a **global** conditional stack |
| `#else` | flips the top of that stack |
| `#endif` | pops it |
| any node read while a stack entry is false | **skipped** (`size--; continue;`), directives included |

Two consequences that the experiments in §6 confirmed the hard way:

- A **dangling `#endif`** pops an empty stack — the file has to keep its conditionals balanced, so a
  directive is never a candidate for removal, even when it looks like a stray scalar in a dump.
- An `#ifdef` whose macro name matches **nothing** silently drops everything up to its own `#endif`.
  That is the lever: an unknown macro name is not an error, it is a skip.

## 4. The two option arrays, verbatim

Payload (`game/ultimate/gen/patch_xbox_0.ark`):

```
( splash_start  splash_leaderboard  #ifdef HX_XBOX  splash_achievements  #endif
  splash_options  mod_settings  splash_upgrade  splash_exit  splash_dlc )
```

Retail (`game/gen/main_xbox_0.ark`):

```
( splash_start  splash_leaderboard  #ifdef HX_XBOX  splash_achievements  #endif
  splash_options  splash_upgrade  splash_dlc  #ifdef HX_XBOX  splash_exit  #endif )
```

`HX_XBOX` is defined in an Xbox build, so the achievement row is *present* as shipped, and it is the
block the edit borrows. The payload adds `mod_settings`; neither lists the trial's rows, and
`splash_upgrade` is removed by the panel itself. Both arrays carry `splash_start` **and** `splash_exit`
as direct elements, which is how the module finds them: an array holding both is the main menu's, and
no other array in either file is one.

## 5. The edit

`HideRows` makes four moves, and refuses the file unless the result is **exactly the same length**:

1. **Remove** the named name-carrying elements. 71 bytes for the compiled default:
   `splash_leaderboard` 26 + `splash_achievements` 27 + `splash_dlc` 18 (a text node is
   `4 + 4 + strlen`).
2. **Unwrap** the array's first conditional: its block's elements are moved in front of its `#ifdef`,
   which does not change the menu's order (a directive is not a row with an action of its own).
3. **Grow** that `#ifdef`'s macro name by exactly the 71 freed bytes (`HX_XBOX` → `HX_XBOX` + 71 `z`s).
   The name now matches no macro, so the loader skips from the directive to its `#endif` — and since
   step 2 left that block empty, nothing the player asked to keep is inside it.
4. **Re-encode** the nodes after the 7-byte root header with the file's own seed and compare lengths.

Why byte-neutrality is not optional: **the ark index fixes every entry's offset and size**
(`main_xbox.hdr` / `patch_xbox.hdr`), so a file of a different length is a damaged archive, not a
patched one. Why step 2 exists: without it, hiding a row *outside* the block would take the
achievement row with it.

Two smaller pieces complete the delivery:

- **The read hook** ([src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp)) replaces
  `__imp__NtReadFile` / `__imp__NtReadFileScatter` and, after the real read, looks for the file in the
  buffer the guest just filled. Reads are 64 KiB ark blocks, so the entry sits at an arbitrary offset
  in the block (7,970 and 19,299 in the two runs below); the scan tests each candidate's header for
  the 7-byte shape above and the edit's own validation is what makes a false positive harmless.
- **The content-checksum row.** The title holds `{name; sha1[20]}` rows in its image and refuses a file
  whose digest moved (`XamShowDirtyDiscErrorUI`). When the *original* digest of the file being patched
  is present in the image, the hook rewrites that row to the patched digest — the retail file's row,
  at guest `0x82804364` **[cited]**. The payload's digest is not in the image at all: the mod's files
  come through the overlay device the database does not describe.

## 6. The label (R10)

The mod's settings row is a locale entry, and its text is data too: the panel's row is the symbol
`mod_settings`, and the engine draws whatever that key resolves to.

```
{mod_settings  "Mod Settings"}
```

The entries live in `ulti/locale/gen/locale_mod.dtb` — the mod's own locale file, inside the payload
ark — and each is an array of exactly two elements, `[symbol key][string text]`, which is the shape
every locale file in this title uses. A run without the payload never reads that file, so
`enhancements_rename_mod_settings` has nothing to do and the launcher does not show the row.

Making that label read **"Ultimate Settings"** is five bytes longer, and the length is the ark's, so
the same rule applies: the bytes have to come from inside the file. They come from the one thing in
it that can give them up without changing anything the guest does — **the name of an `#ifdef` whose
macro this build does not define.** The file carries the other platform's strings behind
`#ifdef HX_PS3`; the loader skips that branch here because the name matches no macro, and *that* is
what the skip depends on, not which name it is. So:

| Move | Bytes |
| --- | --- |
| `"Mod Settings"` → `"Ultimate Settings"` | +5 |
| `HX_PS3` → `H` (the unused name pays) | −5 |

Nothing else moves, and the branch decisions cannot: an undefined macro is undefined whatever it is
called. A *shorter* label gives the bytes back the other way (the name grows by the difference, which
is R5's move in reverse), and a label of the same length needs no donor at all. The module takes bytes
only from the one name it knows is not defined in this build (`kSpareMacro` in
[src/ui/menu_options.cpp](../../src/ui/menu_options.cpp)) — never from an arbitrary conditional, since
shortening a *defined* macro's name would throw away the branch it guards.

The edit is refused, with the reason logged, when the pair is not in the file, when there is no such
`#ifdef`, or when the name is too short to pay for the new text: a mod version whose strings changed is
left exactly as it ships.

## 7. The offline prompts (R3)

**The questions.** Starting a game runs the title's connect process. On an install with no Rock
Central to reach, that process can only fail, and the panel that owns it asks the player twice before
the title draws its menu: `Cannot connect to Rock Central. To connect, sign in to an Xbox
LIVE-enabled profile, …`, and then `Proceed in Offline Mode? …`. Both are the same panel's states, and
both are answered with A — R3 answers them as they arrive, so a start goes straight to the menu.

**The file.** `ui/net/gen/server_connect.dtb` (**11,646 bytes**, at offset **316,968,392** of
`game/gen/main_xbox_0.ark` **[cited]**, and present in the retail ark only: the Ultimate payload does
not carry it). It defines the panel object `server_connect_panel`, a `ServerConnectPanel`, and its
handlers are `enter` (`start_connect_process`), `update_state ($state)`, `get_save_data`,
`name_updated` and `BUTTON_DOWN_MSG`. The states are the file's own `#define kServerConnectPanel_*
(n)` run, and `update_state` is what the panel's class calls on every state change: the state arrives
as that handler's `$state` argument, and the handler selects the text (a `switch` on `$state`) and the
buttons (the `set_showing` statements beside it) that the state draws.

**What the two accepts do.** They are in the file already, in `BUTTON_DOWN_MSG`'s first
`kAction_Confirm` case, which is a `cond` over the state the panel is in:

```
{cond ( {|| {== $state kServerConnectPanel_OfflineMode} {== $state kServerConnectPanel_Connected} } )
      { {splash_panel loaded_dir} set state main_menu }
      { ui goto_screen splash_screen } }
{cond ( {|| {== $state kServerConnectPanel_Failed} {== $state kServerConnectPanel_NoValidLoginCandidate} } )
      { $this set_state kServerConnectPanel_OfflineMode } }
```

So the first accept is a state move — a failed login becomes `OfflineMode` — and the second is the
screen change that leaves the panel for the menu. Both are statements the file already has, so the
edit writes them into `update_state`'s own statement list, where the state is already known:

| Added to `update_state` | Bytes |
| --- | --- |
| `{if {|| {== $state kServerConnectPanel_Failed} {== $state kServerConnectPanel_NoValidLoginCandidate}} {$this set_state kServerConnectPanel_OfflineMode}}` | 267 |
| `{if {== $state kServerConnectPanel_OfflineMode} {{splash_panel loaded_dir} set state main_menu} {ui goto_screen splash_screen}}` | 251 |

**The payment.** 518 bytes, and they come from inside the file, as R5's and R10's do: eight state
constants that nothing in the game refers to.

| Move | Bytes |
| --- | --- |
| eight `#define NAME (n)` pairs removed — `StartProcess`, `StartSongCache`, `EndSongCache`, `StartPostLogin`, `StartEnumeratingContent`, `CheckingFacebookPermission`, `RequestingFacebookToken`, `WaitingForTrialEnumeration` | −518 |
| the two transitions above | +518 |

The claim is checked, not assumed: every `.dtb` in the retail ark and in the payload (131 + 42) was
decoded and searched for each name, and so was the decrypted `default.xex` — no file and no string in
the image carries one, so a file without them runs the same. They name transient steps of the connect
process which the file's own handler never labels, and the arithmetic is exact: the retail file pays
to the byte, `removed = added = 518`, and no leftover has to be parked. A file that pays *more* parks
the difference inside the `#ifdef HX_PS3` command the loader skips (as R5 and R10 park theirs).

**Three rules the file taught, each of them a fault first.** Two are about the shape of a compiled
DTA, and both were found by hand, on a boot that faulted:

1. **A command's children are its arguments.** The first attempt appended the two statements *inside*
   the handler's first statement, `{status.lbl set text_token {switch $state …}}` — which is a call:
   `status.lbl.set(text_token, <switch>)`. Two more children made it a call with more arguments, and
   the guest faulted reading a null object (`read of guest 0x00000008`) at the first state change —
   whatever the added statement said. A *verbatim copy of a shipped statement* faulted the same way,
   which is what proved the placement and not the text was wrong. The statements belong in the
   handler's own list, as siblings of that call.
2. **A condition is a command, not an array.** `{if {== $state X} …}` is a command node (tag `0x11`),
   the shape the file's own line-89 `if` uses; `(== $state X)` is an array (tag `0x10`), which is a
   *list value*. Written as arrays the same edit faulted differently (`write of guest 0x7014FED0`),
   so the two mistakes are distinguishable in a log.
3. **The root count in a compiled DTA's header counts nodes, not statements.** A `#define NAME (n)` is
   two root nodes, so writing the number of *statements* into the header desynchronizes the parse: the
   guest read 14 phantom nodes past the end of the tree and faulted. The edit writes the node count
   (`84 → 68` here) and refuses any file that grows or shrinks.

**When it is refused.** Every refusal writes nothing and logs its reason: the file is not the panel's;
one of the eight constants is missing (a file whose names changed); the transitions cost more than the
constants pay; a leftover exists and the handler carries no `#ifdef HX_PS3` command to park it in; the
handler already carries the skip, so a second pass over a patched file says so; the rebuilt node
stream is not exactly as long as the input; or the root array outgrew what the header can count.

## 8. Evidence

| Run | What was seen |
| --- | --- |
| `scripts/hide-test.ps1` (the ark-side bed: the payload's entry edited before boot) | alive, no fault; OCR of the main menu read `PLAY / HELP & OPTIONS / MOD SETTINGS / EXIT GAME` |
| the same, with the retail file's transform built into the payload | alive; OCR `PLAY / HELP & OPTIONS / EXIT GAME` |
| single-row deletions (leaderboard, achievements, mod_settings, upgrade) | all alive — a configurable row list is a shape the title takes |
| a dangling `#endif`, a removed directive, an INT pad | faults at load: the conditional stack, not the row, is what the file cannot survive |
| `rb_blitz.exe --enhancements_hide_menu_options=true`, Ultimate payload **[cited]** | `menu_filter: hid 3 row(s) … (71 bytes moved into the list's #ifdef macro name)`; the payload's file (**8,808 bytes**, `f7a5e178…` → `1df9dac7…`) patched at byte 7,970 of a 65,536-byte read, and the retail one (**7,148 bytes**, `8a743f06…` → `fb7e1f3a…`) at byte 19,299 |
| the same, vanilla (`--ultimate_mode=0`) **[cited]** | the retail file patched, and `content checksum row at guest 0x82804364 now holds the patched digest`; no dirty-disc screen |
| `scripts/observe_ui.ps1 -State main-menu -Ocr` with the toggle **on**, both targets **[cited]** | OCR `BLITZ ALTIMAT PLAY HELP & OPTIONS MOD SETTINGS EXIT GAME` (Ultimate) and `BLITZ PLAY HELP & OPTIONS EXIT GAME` (vanilla) — `LEADERBOARDS` and `ACHIEVEMENTS` are gone from both, `DOWNLOAD CONTENT` from the vanilla one, and the harness's own stock needle (`DOWNLOAD CONTENT`) no longer matches |
| the same runs with the toggle **off** (the control) **[cited]** | OCR `PLAY LEADERBOARDS ACHIEVEMENTS HELP & OPTIONS DOWNLOAD CONTENT EXIT GAME` (vanilla — the harness's needle matches and the state passes, so the stock screen is unchanged) and `PLAY LEADERBOARDS ACHIEVEMENTS HELP & OPTIONS MOD SETTINGS EXIT GAME` (Ultimate: that payload's own menu draws no DLC row); the log carries `menu_filter: off (R5)` and nothing else, so the read path is not even scanned |
| `tests/menu_options_tests.cpp` **[tree]** | over a synthetic fixture: the grammar round trip, the default list, a row inside the block, a row outside it, an absent row, an empty list, a name the file only uses as structure, the bytes a read block carries behind the file, the label rename (grown, shortened, same length, refused), and every refusal path. No retail content is used (D14) |
| the same transform run against the real `locale_mod.dtb`, off-tree **[cited]** | 4,094 bytes in, 4,094 out, `Mod Settings` → `Ultimate Settings`, `HX_PS3` → `H`, the achievement strings untouched, and both files parse to the end with the same root arity (82) |
| `rb_blitz.exe` with default cvars (R10 on, R5 off), Ultimate payload **[cited]** | `menu_filter: relabelled "Mod Settings" to "Ultimate Settings" (5 bytes taken from an unused macro name)`, then `4094 bytes, digest fdf135ad… -> 72359ac0…` and `patched a file at byte 44974 of a 65536 byte plain read`; the main-menu OCR reads `… HELP & OPTIONS ULTIMATE SETTINGS EXIT GAME` |
| the same, the mod's Other Settings page, R10 **on** vs **off** **[cited]** | both runs OCR `OTHERSETTINGS / Unlock All / Unlock Online Only Achievements` — the branch the edit borrows from did not move, and it is the branch the guest already took |
| the same, `--enhancements_rename_mod_settings=false` (the control) **[cited]** | the main-menu OCR reads `… HELP & OPTIONS MOD SETTINGS EXIT GAME`; the log carries `menu_filter: R10 off`, no patch line, and no digest |
| the R3 transform run against the real `server_connect.dtb`, off-tree **[cited]** | 11,646 bytes in, 11,646 out, `removed=added=518`, no padding, root `84 → 68`, `017ced37… → 40aa89a7…`; the patched file parses *exactly* to its end (11,642 of 11,642 body bytes) and the 4,096 bytes the read carries behind it are untouched |
| `rb_blitz.exe` with the default cvars (R3 on, R5 and R10 off), vanilla **[cited]** | `menu_filter: the offline prompts are skipped (518 bytes of unused state constants paid for 518 bytes of transitions): a start goes straight to the menu in offline mode` and `content checksum row at guest 0X82804154 now holds the patched digest`; one A reaches the menu |
| `scripts/observe_ui.ps1 -State main-menu -Ocr` with R3 on (its default) **[cited]** | passed: the state's own needle (`DOWNLOAD CONTENT`) is on the screen after the route's **single** accept — the boot-flow change the toggle makes |
| `scripts/observe_ui.ps1 -State "offline prompt" -Ocr -SkipOfflinePrompts 0` (the control) **[cited]** | passed: OCR `BLITZ ANNOT CONNECT TO ROCK CENTRAL. TO CONNECT, SIGN IN TO AN XBOX LIVE-ENABLED PROFILE, …`, and the log carries `menu_filter: R3 off; the failed-login and offline-mode prompts are shown` |
| the same pair driven by hand on the real title **[cited]** | on: one A, alive, menu OCR `BLITZ … LEADERBOARDS ACHIEVEMENTS HELP & OPTIONS ULTIMATE SETTINGS EXIT GAME`; off: one A, alive, the failed-connect dialog |
| `tests/menu_options_tests.cpp` (the R3 cases) **[tree]** | over a synthetic panel fixture: the file is skipped, its length and its root count still add up, the eight constants are gone, the added statements are the handler's last two commands, the label-drawing command's own argument count is untouched, the payment can be exact (no filler), a file that cannot pay is refused with both sides of the arithmetic reported, and every refusal path |

## 9. Where it lives

| Piece | File |
| --- | --- |
| the edits (SDK-free, tested) | [src/ui/menu_options.h](../../src/ui/menu_options.h), [src/ui/menu_options.cpp](../../src/ui/menu_options.cpp) (`HideRows`, `RenameLabel`, `SkipOfflinePrompts`) |
| the read hook, the checksum row, the cvars' boot read | [src/hooks/menu_filter.h](../../src/hooks/menu_filter.h), [src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp) |
| the toggles | [src/enhancements.cpp](../../src/enhancements.cpp) (`enhancements_hide_menu_options`, `enhancements_hidden_menu_options`, `enhancements_rename_mod_settings`, `enhancements_skip_offline_dialog`) |
| the launcher rows | [launcher/config/settings.toml](../../launcher/config/settings.toml) (the *Interface* tab's "Main menu" group, and R3's own row in its "Startup" group) |
| the tests | [tests/menu_options_tests.cpp](../../tests/menu_options_tests.cpp), [tests/launcher_launch_tests.cpp](../../tests/launcher_launch_tests.cpp) (R3's argv) |
| the harness | [scripts/ui_states.ps1](../../scripts/ui_states.ps1) (the `RBBLITZ_SKIP_OFFLINE_PROMPTS` knob and the `offline-prompt` state), [scripts/observe_ui.ps1](../../scripts/observe_ui.ps1) (`-SkipOfflinePrompts`) |

## 10. Open

- **A read that does not carry the whole file is not patched.** The block the ark reads is the unit
  the hook sees; a file split across two reads is left alone (the log says
  `the node stream runs past the bytes available`). Measured: both shipped files arrive whole.
- **The names, not the path, are the identity.** Any file carrying an array with `splash_start` and
  `splash_exit` *and* the named rows is edited. No other file in either ark is one, but a future
  content pack could be; the log names the file it patched (size + digest) so that is visible. The
  label edit is anchored the same way, on `mod_settings` **and** the text it ships with, so a mod
  version whose strings changed is left alone rather than half-renamed.
- **The list needs a conditional to pay for the freed bytes.** A file whose option array has no
  `#ifdef` is refused (`the option list carries no conditional to take the freed bytes`), because the
  edit has nowhere to put them. Both shipped files have one.
- **The label needs that unused macro name.** `ulti/locale/gen/locale_mod.dtb` has one (`#ifdef
  HX_PS3`, six characters, five of which pay for "Ultimate"); a rebuild of the mod without it, or one
  whose label already reads differently, is refused with the reason logged and drawn as it ships.
- **The launcher's row is shown from a startup snapshot** of whether the payload is installed
  (`DetectUltimateState`), taken where the launch target's own state is read. Installing Ultimate
  from the launcher *during* a session therefore shows the row on the next start, not in that one —
  the same restart-scoped promise the row itself makes about its value.
- **The trial target is not this feature's to fix.** With `--license_mask=0` the *stock* payload
  faults at boot, before R5's read hook is involved (measured while choosing a launch target, not
  re-measured here). R5 neither causes nor fixes it.
- **The row list is a string cvar**, so a typo hides nothing and says why
  (`none of the named rows are in the option list`). It is restart-scoped: the edit happens at load.
- **R3's edit is coupled to this build's two statements.** The payment is the file's own constants, so
  the edit is byte-neutral, but the *statements* it writes are this build's: a file whose `update_state`
  no longer has a first command, or whose eight constants changed name, is refused rather than
  half-edited. Measured on this machine's retail file only — an Ultimate payload carries no
  `server_connect.dtb` at all, so there is one file to be right about.
- **R3's first read of the file is left alone.** The log carries
  `menu_filter: skip offline prompts left a .dtb read alone: the node stream does not parse` once,
  roughly 100 ms before the read that is patched. It is a read this edit does not recognise (the
  grammar of that read is not a compiled DTA), the caller still gets its bytes untouched, and the boot
  that follows is the patched one — recorded here because the line looks like a failure and is not.
