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
were dumped from the retail entry on this machine **[cited]** (§6).

## 2. The file

`splash.dtb` — 8,808 bytes in the payload ark, 7,148 in the retail one **[cited]** (§6). Both arks
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

## 6. Evidence

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
| `tests/menu_options_tests.cpp` **[tree]** | 59 checks over a synthetic fixture: the grammar round trip, the default list, a row inside the block, a row outside it, an absent row, an empty list, a name the file only uses as structure, the bytes a read block carries behind the file, and the refusal paths. No retail content is used (D14) |

## 7. Where it lives

| Piece | File |
| --- | --- |
| the edit (SDK-free, tested) | [src/ui/menu_options.h](../../src/ui/menu_options.h), [src/ui/menu_options.cpp](../../src/ui/menu_options.cpp) |
| the read hook, the checksum row, the cvars' boot read | [src/hooks/menu_filter.h](../../src/hooks/menu_filter.h), [src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp) |
| the toggles | [src/enhancements.cpp](../../src/enhancements.cpp) (`enhancements_hide_menu_options`, `enhancements_hidden_menu_options`) |
| the launcher rows | [launcher/config/settings.toml](../../launcher/config/settings.toml) (General → "Main menu") |
| the tests | [tests/menu_options_tests.cpp](../../tests/menu_options_tests.cpp) |

## 8. Open

- **A read that does not carry the whole file is not patched.** The block the ark reads is the unit
  the hook sees; a file split across two reads is left alone (the log says
  `the node stream runs past the bytes available`). Measured: both shipped files arrive whole.
- **The names, not the path, are the identity.** Any file carrying an array with `splash_start` and
  `splash_exit` *and* the named rows is edited. No other file in either ark is one, but a future
  content pack could be; the log names the file it patched (size + digest) so that is visible.
- **The list needs a conditional to pay for the freed bytes.** A file whose option array has no
  `#ifdef` is refused (`the option list carries no conditional to take the freed bytes`), because the
  edit has nowhere to put them. Both shipped files have one.
- **The trial target is not this feature's to fix.** With `--license_mask=0` the *stock* payload
  faults at boot, before R5's read hook is involved (measured while choosing a launch target, not
  re-measured here). R5 neither causes nor fixes it.
- **The row list is a string cvar**, so a typo hides nothing and says why
  (`none of the named rows are in the option list`). It is restart-scoped: the edit happens at load.
