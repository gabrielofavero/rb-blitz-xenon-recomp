# The main menu — option list, labels, offline prompts, refresh row, background music

## At a glance

The title's main menu is **four pieces of data plus one loader path**, and each is edited (or hooked)
in place, never repacked:

- **The option list** is one compiled DTA, `splash.dtb`, in each ark; its option array is copied into
  `start.lst`. Rows are identified by the symbol the title gives them (`splash_dlc`, …) and hidden by
  making one `#ifdef`'s macro name match nothing — R5, off by default.
- **The mod's label** is a locale entry in the payload ark's own `ulti/locale/gen/locale_mod.dtb`,
  renamed by paying for the extra bytes out of an unused macro name — R10.
- **The two offline prompts** are states of `server_connect_panel` in `ui/net/gen/server_connect.dtb`,
  answered by writing the file's own transitions into its `update_state`, paid for by eight unused
  state constants — R3, on by default.
- **The refresh row** re-points `splash_dlc`'s action at the title's own `file_exists` on a sentinel
  device path, which the host hears — R7, off by default.
- **The background music** is `ShellMusicPanel`, fed by the ark's `config/synth.dtb` `shellmusic`
  block (three of the title's own tracks). DLC songs cannot be a pool entry, so R9 hooks the pool
  loader and the per-frame poll instead — off by default.

All five are byte-neutral or read-time edits delivered at the read that carries the file, except R9's
hooks, which are host-side.

## Sources

| Part | Original document | Status |
| --- | --- | --- |
| Part 1 — The option list | `docs/engine/main-menu.md (part 1, the option list)` | Merged into this file |
| Part 2 — The background music | `docs/engine/main-menu.md (part 2, the background music)` | Merged into this file |

## Part 1 — The option list (was `docs/engine/main-menu-flow.md`)

**The question** ([2026-10-03-customization-plan.md](../plans/finished/2026-10-03-customization-plan.md)
U1, R5): where are the main menu's options built and filtered — the list's source, the per-option
identity, the flag that would let one be hidden — and how do you hide one without stranding navigation
(D12)?

**The answer.** The list is **data**: one compiled DTA, `splash.dtb`, in each ark, whose option array
the panel copies into its own row list. A row's identity is the name the title gives it
(`splash_leaderboard`, `splash_achievements`, `splash_dlc`, …); the flag is the guest's own `#ifdef` —
an element whose macro name matches nothing is *skipped* by the DTA loader. Hiding a row is a
byte-neutral rewrite of that one file, delivered at the read that carries it: nothing is repacked,
nothing in `game/` is written, the retail content is not needed to build (D14).

**The feature.** `enhancements_hide_menu_options` + `enhancements_hidden_menu_options`
([toggles.md](toggles.md), R5), in [src/ui/menu_options.cpp](../../src/ui/menu_options.cpp) (the pure
edit), [src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp) (the read hook and the checksum
row) and [src/enhancements.cpp](../../src/enhancements.cpp) (the cvars). Off by default.

**The second edit (§7).** R3 takes the two questions the title asks before it draws its menu on an
offline install: same kind of rewrite as R5 (bytes moved inside one compiled DTA, delivered at the read
that carries it), but of `server_connect.dtb` (the panel a game start opens) and for a different reason
— the questions cannot be answered when there is nothing to connect to, so they are answered as they
arrive. On by default.

**Evidence convention** (both Parts): **[tree]** = this working tree with `file:line`; **[cited]** = a
build product (log line or capture from a run here); **[assumed]** = reasoned, not measured.

### 1. The panel

The main menu is a DTA panel object. From the shipped retail file (`check_trial`; `$NAME` is
`#ifdef NAME`, and a lone `0` is the `#endif` the compiled form stores as a scalar):

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

Four properties make the feature safe:

| Property | Why it matters here |
| --- | --- |
| The row list is a copy of the array (the panel writes it, then `start.lst set_data options_array`) | editing the file edits the list the guest navigates, so the drawing and the highlight both follow — D12's requirement |
| `remove_elem options_array splash_upgrade` (full) / `… splash_dlc` (trial) already deletes rows from that list, and a name that is not there is tolerated | removing a row is an operation the panel performs on itself; a shorter list is a shape it is written for |
| The row's action is a `switch` on `start.lst selected_sym`, one case per row **name** | a case whose row is gone is never taken, so the action table need not change with the list — and we match the identity the switch matches |
| The achievements case's own failure path is a popup (`achievements_unavailable`) | the three rows this feature's default list names are the ones whose screens need Rock Central; offline they are a notice, not a screen |

**[tree]** the module's reading of that shape: [src/ui/menu_options.cpp:268-282](../../src/ui/menu_options.cpp)
(the locator), [src/ui/menu_options.cpp:440-457](../../src/ui/menu_options.cpp) (the removal); the quoted
statements were dumped from the retail entry on this machine **[cited]** (§7).

### 2. The file

`splash.dtb` — 8,808 bytes in the payload ark, 7,148 in the retail one **[cited]** (§7). Both arks
carry it; the payload's copy wins when the Ultimate overlay is mounted.

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

The body is xored with `dtbcrypt`'s keystream: `key = CryptRound(seed)`, one byte per node byte, where
`CryptRound` is the Park–Miller generator with Schrage's division as the title spells it (`0x1F31D` /
`0x41A7` / `0xB14`, `+0x7FFFFFFF` when the result is not positive) **[tree]**
[scripts/hmx_ark.py](../../scripts/hmx_ark.py), [assets.md](../guides/assets.md) § "Compiled DTA". The
keystream is its own inverse, so one routine reads and writes the file.

**The grammar is pinned by a round trip.** `HideRows` re-serializes every node it parsed and refuses
the file unless the bytes come back identical
([src/ui/menu_options.cpp:414-424](../../src/ui/menu_options.cpp)): a file it cannot reproduce byte for
byte is one it does not understand, and it says so instead of editing it.

### 3. The loader

From the engine's own `DataArray::Load` (public tree, `system/obj/DataArray.cpp`):

| Directive | Effect |
| --- | --- |
| `#ifdef NAME` | pushes `DataGetMacro(NAME) != nullptr` onto a **global** conditional stack |
| `#else` | flips the top of that stack |
| `#endif` | pops it |
| any node read while a stack entry is false | **skipped** (`size--; continue;`), directives included |

Two consequences, each a fault first (§7):

- A **dangling `#endif`** pops an empty stack: conditionals must stay balanced, so a directive is never
  a removal candidate, even when it looks like a stray scalar in a dump.
- An `#ifdef` whose macro name matches **nothing** silently drops everything up to its own `#endif` —
  the lever: an unknown macro name is a skip, not an error.

### 4. The two option arrays, verbatim

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

`HX_XBOX` is defined in an Xbox build, so the achievement row is *present* as shipped — the block the
edit borrows. The payload adds `mod_settings`; neither lists the trial's rows, and `splash_upgrade` is
removed by the panel itself. Both arrays carry `splash_start` **and** `splash_exit` as direct elements,
which is how the module finds them: an array holding both is the main menu's, and no other array in
either file is one.

### 5. The edit

`HideRows` makes four moves and refuses the file unless the result is **exactly the same length**:

1. **Remove** the named name-carrying elements — 71 bytes for the compiled default:
   `splash_leaderboard` 26 + `splash_achievements` 27 + `splash_dlc` 18 (a text node is
   `4 + 4 + strlen`).
2. **Unwrap** the array's first conditional: its block's elements move in front of its `#ifdef`, which
   does not change the order (a directive is not a row with an action of its own).
3. **Grow** that `#ifdef`'s macro name by exactly the 71 freed bytes (`HX_XBOX` → `HX_XBOX` + 71 `z`s),
   so the loader skips from the directive to its `#endif` — and since step 2 left that block empty,
   nothing the player asked to keep is inside it.
4. **Re-encode** the nodes after the 7-byte root header with the file's own seed and compare lengths.

Byte-neutrality is not optional: **the ark index fixes every entry's offset and size**
(`main_xbox.hdr` / `patch_xbox.hdr`), so a different-length file is a damaged archive, not a patched
one. Step 2 exists because without it, hiding a row *outside* the block would take the achievement row
with it.

Two smaller pieces complete the delivery:

- **The read hook** ([src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp)) replaces
  `__imp__NtReadFile` / `__imp__NtReadFileScatter` and, after the real read, looks for the file in the
  buffer the guest just filled. Reads are 64 KiB ark blocks, so the entry sits at an arbitrary offset
  (7,970 and 19,299 in the two runs below); the scan tests each candidate's header for the 7-byte shape
  above, and the edit's own validation makes a false positive harmless.
- **The content-checksum row.** The title holds `{name; sha1[20]}` rows in its image and refuses a file
  whose digest moved (`XamShowDirtyDiscErrorUI`). When the *original* digest of the patched file is in
  the image, the hook rewrites that row to the patched digest — the retail file's row, at guest
  `0x82804364` **[cited]**. The payload's digest is not in the image at all: the mod's files come
  through the overlay device the database does not describe.

### 6. The label (R10)

The mod's settings row is a locale entry, and its text is data too: the panel's row is the symbol
`mod_settings`, and the engine draws whatever that key resolves to.

```
{mod_settings  "Mod Settings"}
```

The entries live in `ulti/locale/gen/locale_mod.dtb` — the mod's own locale file, inside the payload
ark — each an array of exactly two elements, `[symbol key][string text]`, the shape every locale file
in this title uses. A run without the payload never reads that file, so
`enhancements_rename_mod_settings` has nothing to do and the launcher does not show the row.

**"Ultimate Settings"** is five bytes longer, and the length is the ark's, so the bytes must come from
inside the file — from the one thing in it that can give them up without changing anything the guest
does: **the name of an `#ifdef` whose macro this build does not define.** The file carries the other
platform's strings behind `#ifdef HX_PS3`; the loader skips that branch here because the name matches
no macro, and *that* is what the skip depends on, not which name it is.

| Move | Bytes |
| --- | --- |
| `"Mod Settings"` → `"Ultimate Settings"` | +5 |
| `HX_PS3` → `H` (the unused name pays) | −5 |

Nothing else moves, and the branch decisions cannot: an undefined macro is undefined whatever it is
called. A *shorter* label gives bytes back the other way (the name grows by the difference — R5's move
in reverse), and a same-length label needs no donor. The module takes bytes only from the one name it
knows is not defined in this build (`kSpareMacro` in
[src/ui/menu_options.cpp](../../src/ui/menu_options.cpp)) — never from an arbitrary conditional, since
shortening a *defined* macro's name would throw away the branch it guards.

The edit is refused, with the reason logged, when the pair is not in the file, when there is no such
`#ifdef`, or when the name is too short to pay for the new text: a mod version whose strings changed
ships unchanged.

### 7. The offline prompts (R3)

**The questions.** A game start runs the connect process, which offline can only fail, and its panel
asks twice before the menu draws: `Cannot connect to Rock Central. To connect, sign in to an Xbox
LIVE-enabled profile, …`, then `Proceed in Offline Mode? …`. Both are the same panel's states, both
answered with A — R3 answers them as they arrive, so a start goes straight to the menu.

**The file.** `ui/net/gen/server_connect.dtb` (**11,646 bytes**, at offset **316,968,392** of
`game/gen/main_xbox_0.ark` **[cited]**; retail ark only — the Ultimate payload does not carry it),
defining the panel object `server_connect_panel` (`ServerConnectPanel`) with handlers `enter`
(`start_connect_process`), `update_state ($state)`, `get_save_data`, `name_updated`, `BUTTON_DOWN_MSG`.
The states are the file's `#define kServerConnectPanel_* (n)` run; `update_state` is called on every
state change, receives the state as `$state`, and selects the text (a `switch` on `$state`) and the
buttons (the `set_showing` statements beside it).

**What the two accepts do.** They are already in the file, in `BUTTON_DOWN_MSG`'s first
`kAction_Confirm` case, a `cond` over the state the panel is in:

```
{cond ( {|| {== $state kServerConnectPanel_OfflineMode} {== $state kServerConnectPanel_Connected} } )
      { {splash_panel loaded_dir} set state main_menu }
      { ui goto_screen splash_screen } }
{cond ( {|| {== $state kServerConnectPanel_Failed} {== $state kServerConnectPanel_NoValidLoginCandidate} } )
      { $this set_state kServerConnectPanel_OfflineMode } }
```

The first accept is a state move (a failed login becomes `OfflineMode`); the second is the screen change
that leaves the panel for the menu. Both statements already exist, so the edit writes them into
`update_state`'s own statement list, where the state is already known:

| Added to `update_state` | Bytes |
| --- | --- |
| `{if {|| {== $state kServerConnectPanel_Failed} {== $state kServerConnectPanel_NoValidLoginCandidate}} {$this set_state kServerConnectPanel_OfflineMode}}` | 267 |
| `{if {== $state kServerConnectPanel_OfflineMode} {{splash_panel loaded_dir} set state main_menu} {ui goto_screen splash_screen}}` | 251 |

**The payment.** 518 bytes, from inside the file as R5's and R10's are: eight state constants nothing
in the game refers to.

| Move | Bytes |
| --- | --- |
| eight `#define NAME (n)` pairs removed — `StartProcess`, `StartSongCache`, `EndSongCache`, `StartPostLogin`, `StartEnumeratingContent`, `CheckingFacebookPermission`, `RequestingFacebookToken`, `WaitingForTrialEnumeration` | −518 |
| the two transitions above | +518 |

Checked, not assumed: every `.dtb` in the retail ark and the payload (131 + 42) was decoded and searched
for each name, and so was the decrypted `default.xex` — nothing carries one, so a file without them
runs the same. They name transient connect-process steps the handler never labels, and the arithmetic
is exact: `removed = added = 518`, no leftover parked. A file that pays *more* parks the difference
inside the `#ifdef HX_PS3` command the loader skips (as R5 and R10 park theirs).

**Three rules the file taught, each a fault first:**

1. **A command's children are its arguments.** Appending the two statements *inside* the handler's first
   statement, `{status.lbl set text_token {switch $state …}}` — a call,
   `status.lbl.set(text_token, <switch>)` — made it a call with more arguments and faulted the guest
   reading a null object (`read of guest 0x00000008`) at the first state change, whatever the statement
   said; a *verbatim copy of a shipped statement* faulted the same way, proving the placement was
   wrong, not the text. They belong in the handler's own list, as siblings of that call.
2. **A condition is a command, not an array.** `{if {== $state X} …}` is a command node (tag `0x11`),
   the shape the file's own line-89 `if` uses; `(== $state X)` is an array (tag `0x10`), a *list value*
   — written as arrays the same edit faulted differently (`write of guest 0x7014FED0`), so the two
   mistakes are distinguishable in a log.
3. **A compiled DTA's header root count counts nodes, not statements.** A `#define NAME (n)` is two root
   nodes, so writing the *statement* count desynchronizes the parse (the guest read 14 phantom nodes
   past the tree and faulted). The edit writes the node count (`84 → 68` here) and refuses any file
   that grows or shrinks.

**When it is refused.** Every refusal writes nothing and logs why: not the panel's file; a constant
missing (names changed); the transitions cost more than the constants pay; a leftover with no `#ifdef
HX_PS3` command to park it in; the handler already carries the skip (a second pass); the rebuilt stream
is not exactly as long as the input; or the root array outgrew the header's count.

### 7.1 The refresh row (R7)

R7's refresh is asked for from the menu, and the row that asks is the same `splash_dlc` case R5
*removes* by default. `TapRefreshCache` rewrites that case's action into a call to the title's own
`file_exists` on a name no file has:

```
{splash_dlc {do {file_exists "songcache:/rbbz_dlc_refresh"} "zzz…"}}
```

The call is the signal — the engine asks the host a file question, and
[src/hooks/dlc_refresh.cpp](../../src/hooks/dlc_refresh.cpp) watches the kernel's file entry points for
that name — and the string after it pads the case's bytes back to exactly what they were.
`file_exists` answers false, so nothing the player sees changes; the host hears the question
([dlc.md](../guides/dlc.md) §4.2).

Three properties make the shape safe:

| Property | Why it matters here |
| --- | --- |
| The case's action is data, and the switch only takes a case whose row is selected | giving the row a new action cannot affect any other row, and the old action (an `if_else` on the store) is what the replacement is measured against |
| The call is `file_exists`, a builtin the title registers itself (`DataFunc`) | no new file is needed, nothing is written, and a missing file is the answer the guest already handles |
| The sentinel is a *device* path the guest's own file layer has to resolve | the host sees the name in the path it is handed, so no guest state has to be read to recognise it |

**Why the device.** A bare name and a `d:/rbbz_dlc_refresh` path both reached nothing — both answered out of data the title already had (the option list's symbol table; the boot ark). `songcache:` is a device the title owns and opens files through (`songcache:\songcache` is one of the five paths a warm boot's file calls amount to), so a miss on it becomes an `NtQueryFullAttributesFile`/`NtCreateFile`. The recompiled title imports no CRT file entry point at all (no `GetFileAttributesA`, no `FindFirstFileA`) — `generated/default/rb_blitz_funcs.h` — so the `Nt` calls are the only way a guest file question reaches the host, and the hook watches those three.

The row is reachable only with `enhancements_dlc_cache` on: R5's compiled default hides `splash_dlc`, so the boot turning R7 on also drops that name from the hidden list — a row that refreshes the cache is not an offline-dead row.

**The two labels.** The row ships as `DOWNLOAD CONTENT` and the connect panel's status line as
`Discovering Downloadable Content`, both in the game's English locale
(`ui/locale/eng/gen/locale_keep.dtb`). R7 rewrites both (`RenameLabel`, src/ui/menu_options.h for the
constants): the row becomes `Refresh Song Library`, the status line `Loading Song Cache` when the host
answered this boot's enumeration out of the cache (`dlc: CacheServedThisBoot()`) — no discovery to
announce. That locale is the file §10's split-read rule exists for: it spans two 64 KiB reads, the
second landing in **a different guest buffer** (192 KiB from the end of the first) with the first
already copied out, so it is assembled host-side, identified by its *archive* offset, written back into
the second block only, and paid out of the **last** `#ifdef HX_PS3` so both labels and their donor fall
inside the patchable block (log lines in §8) ([dlc.md](../guides/dlc.md) §4.2,
[2026-10-10-song-cache-plan.md](../plans/finished/2026-10-10-song-cache-plan.md) §11).

**The progress bar.** The same panel carries one (`progress_bar.grp`, `content_percent.lbl` in
`ui/net/gen/server_connect.milo_xbox`, both platforms), driven by a Flow that reads `current_content`
and `total_content`. Nothing in the DTA sets them, so the engine's own object does (§9's
`content_progress.cpp`). Measured 2026-10-10: the engine calls `SetTotal` once, **after** the
enumeration, with a total of **0**, and never calls `Increment` — the Flow sees `-1` and hides the bar
for the whole 46.7 s. With `enhancements_dlc_cache` on the host counts the items itself (the 2,807 XAM
app messages the enumeration is made of, hooked through
`XMsgStartIORequestEx`/`XMsgInProcessCall`/`XMsgSystemProcessCall`) and pushes them with the engine's
own push, so the bar animates 0 → 100%. See [dlc.md](../guides/dlc.md) §4.2,
[2026-10-10-song-cache-plan.md](../plans/finished/2026-10-10-song-cache-plan.md) §12.

**Why 46.7 s, and what shortens it.** One item per *presented* frame ⇒ the screen lasts 2,807 of the
title's frames: 16.66 ms at 60 Hz (46.7 s), 5.68 ms with the frame rate free (15.96 s). Measured
2026-10-10 with R7's `enhancements_dlc_uncap_enumeration`: the host turns the SDK's own `vsync` down
when the first item is counted and restores the player's value when the enumeration ends — 1 ms after
the last item, 104 ms before the engine's own completion, so no menu frame is presented uncapped —
finishing the walk in **15.96 s**. Writes happen on a host thread (a guest thread must not touch the
cvar registry), and the option is off by default: the *V-Sync* row belongs to the player
([dlc.md](../guides/dlc.md) §4.3,
[2026-10-10-song-cache-plan.md](../plans/finished/2026-10-10-song-cache-plan.md) §14).

**What is refused.** A file with no `splash_dlc` case, a case whose action is shorter than the call
plus its padding, an action that already carries the sentinel (a second pass), and any file that is
not a `.dtb` this module can re-serialize: each leaves the file exactly as it was and logs why.

### 8. Evidence

**The ark-side bed** (`scripts/hide-test.ps1`: the payload's entry edited before boot) — alive, no
fault; OCR of the main menu read `PLAY / HELP & OPTIONS / MOD SETTINGS / EXIT GAME`. The same with the
retail file's transform built into the payload: alive; OCR `PLAY / HELP & OPTIONS / EXIT GAME`.
Single-row deletions (leaderboard, achievements, mod_settings, upgrade): all alive — a configurable
row list is a shape the title takes. A dangling `#endif`, a removed directive, an INT pad: faults at
load — the conditional stack, not the row, is what the file cannot survive.

**R5 in the title.** `rb_blitz.exe --enhancements_hide_menu_options=true`, Ultimate payload **[cited]**:
`menu_filter: hid 3 row(s) … (71 bytes moved into the list's #ifdef macro name)`; the payload's file
(**8,808 bytes**, `f7a5e178…` → `1df9dac7…`) patched at byte 7,970 of a 65,536-byte read, and the
retail one (**7,148 bytes**, `8a743f06…` → `fb7e1f3a…`) at byte 19,299. Vanilla
(`--ultimate_mode=0`) **[cited]**: the retail file patched, and `content checksum row at guest
0x82804364 now holds the patched digest`; no dirty-disc screen.

**R5 as OCR.** `scripts/observe_ui.ps1 -State main-menu -Ocr` with the toggle **on**, both
targets **[cited]**: `BLITZ ALTIMAT PLAY HELP & OPTIONS MOD SETTINGS EXIT GAME` (Ultimate) and
`BLITZ PLAY HELP & OPTIONS EXIT GAME` (vanilla) — `LEADERBOARDS` and `ACHIEVEMENTS` are gone from
both, `DOWNLOAD CONTENT` from the vanilla one, and the harness's own stock needle (`DOWNLOAD
CONTENT`) no longer matches. The same runs with the toggle **off** (the control) **[cited]**: `PLAY
LEADERBOARDS ACHIEVEMENTS HELP & OPTIONS DOWNLOAD CONTENT EXIT GAME` (vanilla — the harness's needle
matches and the state passes, so the stock screen is unchanged) and `PLAY LEADERBOARDS ACHIEVEMENTS
HELP & OPTIONS MOD SETTINGS EXIT GAME` (Ultimate: that payload's own menu draws no DLC row); the log
carries `menu_filter: off (R5)` and nothing else, so the read path is not even scanned.

**Tests** `tests/menu_options_tests.cpp` **[tree]** — over a synthetic fixture: the grammar round
trip, the default list, a row inside the block, a row outside it, an absent row, an empty list, a name
the file only uses as structure, the bytes a read block carries behind the file, the label rename
(grown, shortened, same length, refused), the downloadable-content row's refresh (applied, padded, a
second pass refused, an absent row refused, a too-short action refused, the bytes behind the file left
alone), and every refusal path. No retail content is used (D14).

**R10.** The same transform run against the real `locale_mod.dtb`, off-tree **[cited]**: 4,094 bytes
in, 4,094 out, `Mod Settings` → `Ultimate Settings`, `HX_PS3` → `H`, the achievement strings
untouched, and both files parse to the end with the same root arity (82). `rb_blitz.exe` with default
cvars (R10 on, R5 off), Ultimate payload **[cited]**: `menu_filter: relabelled "Mod Settings" to
"Ultimate Settings" (5 bytes taken from an unused macro name)`, then `4094 bytes, digest fdf135ad… ->
72359ac0…` and `patched a file at byte 44974 of a 65536 byte plain read`; the main-menu OCR reads
`… HELP & OPTIONS ULTIMATE SETTINGS EXIT GAME`. The mod's Other Settings page, R10 **on** vs
**off** **[cited]**: both runs OCR `OTHERSETTINGS / Unlock All / Unlock Online Only Achievements` —
the branch the edit borrows from did not move, and it is the branch the guest already took.
`--enhancements_rename_mod_settings=false` (the control) **[cited]**: the main-menu OCR reads `… HELP
& OPTIONS MOD SETTINGS EXIT GAME`; the log carries `menu_filter: R10 off`, no patch line, and no
digest.

**R3.** The R3 transform run against the real `server_connect.dtb`, off-tree **[cited]**: 11,646 bytes
in, 11,646 out, `removed=added=518`, no padding, root `84 → 68`, `017ced37… → 40aa89a7…`; the patched
file parses *exactly* to its end (11,642 of 11,642 body bytes) and the 4,096 bytes the read carries
behind it are untouched. `rb_blitz.exe` with the default cvars (R3 on, R5 and R10 off),
vanilla **[cited]**: `menu_filter: the offline prompts are skipped (518 bytes of unused state
constants paid for 518 bytes of transitions): a start goes straight to the menu in offline mode` and
`content checksum row at guest 0X82804154 now holds the patched digest`; one A reaches the menu.
`scripts/observe_ui.ps1 -State main-menu -Ocr` with R3 on (its default) **[cited]**: passed — the
state's own needle (`DOWNLOAD CONTENT`) is on the screen after the route's **single** accept — the
boot-flow change the toggle makes. `scripts/observe_ui.ps1 -State "offline prompt" -Ocr
-SkipOfflinePrompts 0` (the control) **[cited]**: passed — OCR `BLITZ ANNOT CONNECT TO ROCK CENTRAL.
TO CONNECT, SIGN IN TO AN XBOX LIVE-ENABLED PROFILE, …`, and the log carries `menu_filter: R3 off;
the failed-login and offline-mode prompts are shown`. The same pair driven by hand on the real
title **[cited]**: on — one A, alive, menu OCR `BLITZ … LEADERBOARDS ACHIEVEMENTS HELP & OPTIONS
ULTIMATE SETTINGS EXIT GAME`; off — one A, alive, the failed-connect dialog.
`tests/menu_options_tests.cpp` (the R3 cases) **[tree]** — over a synthetic panel fixture: the file
is skipped, its length and its root count still add up, the eight constants are gone, the added
statements are the handler's last two commands, the label-drawing command's own argument count is
untouched, the payment can be exact (no filler), a file that cannot pay is refused with both sides of
the arithmetic reported, and every refusal path.

**R7, the press.** `rb_blitz.exe --enhancements_dlc_cache=true`, the row pressed at the main
menu **[cited]**: the retail file patched (`the downloadable-content row now refreshes the DLC cache
(331 bytes of its action paid for the call)`), the press logged `dlc: refresh asked for from the main
menu ("songcache:\rbbz_dlc_refresh")` → `refresh requested, re-scanning the libraries and rewriting
…\dlc_library.cache` → `refreshed the running title's DLC library: 1402 package(s)`, ~15 s later the
cache file's modification time had moved to the press and the game was still alive on the same screen.
The same press with the scan moved behind the frame, 2026-10-10 **[cited]**: `dlc: refresh asked for;
scanning 1 library root(s) on a worker thread so the frame keeps drawing` on the guest thread → the
scan on a worker → `the refresh finished (1402 package(s)); the running title's library is handed over
on the next menu frame` → `refreshed the running title's DLC library: 1402 package(s)` **10 ms**
later, on the guest thread. The press returned in **1 ms** and the menu kept drawing and taking input
for the whole **30.2 s** scan (the highlight moved under a key pressed during it), where the build
before it froze that frame for **31.0 s**; a second press 7 s in logged `dlc: a refresh is already
running; this ask is ignored`; the same tree re-scanned in **77 ms** once warm. Reproduced with R9 off
as well (publish 7 ms after the scan), so the publish point does not depend on R9.

**R7, the frame pacing, 2026-10-10** **[cited]**: `content_progress: the title is enumerating one item
per presented frame; the host lets the frame rate run free for the discovery (R7's frame pacing)` at
20:05:38.050 → `the frame rate is free for the discovery (vsync false, was true)` 7 ms later →
25/50/75/100% at 4.01/3.99/3.95/3.99 s apart → `the last item of the enumeration was counted` at
20:05:53.987 → `the frame rate is the player's own again (vsync = true)` 1 ms later. **2,807 calls
over 15.96 s** (5.68 ms apart) against **2,807 over 46.7 s** with the player's V-Sync, i.e. the same
walk at 3× the rate, with the restore 104 ms before the engine's own completion.

**R7, the two labels, 2026-10-10** **[cited]**: `relabelled the downloadable-content row "Download
Content" to "Refresh Song Library" (4 bytes taken from an unused macro name)` and `relabelled
"Discovering Downloadable Content" to "Loading Song Cache" (-14 bytes …, the enumeration is coming
from the cache (R7))`, then `patched a file spanning two reads at ark offset 315592934 (28442 bytes
before the seam, 65536 after; the edit falls entirely after it)`; the main menu OCRs `… ULTIMATE
SETTINGS EXIT GAME REFRESH SONG LIBRARY` (`out/drive-ui/p3d-20-menu.png`) and the connect panel OCRs
`Loading Song Cache` (`p3d-10-discovery.png`). Before the fix, the same boot drew `DOWNLOAD CONTENT`
and the split-read rule refused the file silently.

### 9. Where it lives

| Piece | File |
| --- | --- |
| the edits (SDK-free, tested) | [src/ui/menu_options.h](../../src/ui/menu_options.h), [src/ui/menu_options.cpp](../../src/ui/menu_options.cpp) (`HideRows`, `RenameLabel`, `SkipOfflinePrompts`, `TapRefreshCache`) |
| the read hook, the checksum row, the cvars' boot read | [src/hooks/menu_filter.h](../../src/hooks/menu_filter.h), [src/hooks/menu_filter.cpp](../../src/hooks/menu_filter.cpp) |
| the refresh itself (R7) | [src/hooks/dlc_refresh.cpp](../../src/hooks/dlc_refresh.cpp) (the guest's file calls), [src/hooks/dlc.cpp](../../src/hooks/dlc.cpp) (`RefreshConfigured`, `PublishPendingRefresh`, `CacheServedThisBoot`, `ContentItemCount`), [src/fs/refresh_state.h](../../src/fs/refresh_state.h) (one at a time, published once) |
| the discovery screen's progress bar (R7) | [src/hooks/content_progress.cpp](../../src/hooks/content_progress.cpp), [src/hooks/content_progress.h](../../src/hooks/content_progress.h) |
| the toggles | [src/enhancements.cpp](../../src/enhancements.cpp) (`enhancements_hide_menu_options`, `enhancements_hidden_menu_options`, `enhancements_rename_mod_settings`, `enhancements_skip_offline_dialog`) |
| the launcher rows | [launcher/config/settings.toml](../../launcher/config/settings.toml) (the *Interface* tab's "Main menu" group, and R3's own row in its "Startup" group) |
| the tests | [tests/menu_options_tests.cpp](../../tests/menu_options_tests.cpp), [tests/launcher_launch_tests.cpp](../../tests/launcher_launch_tests.cpp) (R3's argv) |
| the harness | [scripts/ui_states.ps1](../../scripts/ui_states.ps1) (the `RBBLITZ_SKIP_OFFLINE_PROMPTS` knob and the `offline-prompt` state), [scripts/observe_ui.ps1](../../scripts/observe_ui.ps1) (`-SkipOfflinePrompts`) |

### 10. Open

- **Two-read files.** The ark is read in 64 KiB blocks, so a file larger than one block — the game's
  own English locale, 76 KB — arrives as two reads. The hook remembers the unfinished `.dtb`'s guest
  buffer, candidate start and archive offset, and patches the file whole when the next read continues
  that buffer at the next archive offset. Measured: the locale's two blocks land back to back in one
  guest buffer (`0x12CF0000`, `0x12D00000`, 64 KiB apart) — what makes §7.1's two labels possible.
  One pair of reads only (a file up to 128 KiB, which every locale in the ark is); non-contiguous
  memory or a third block is left alone.
- **Identity is the names, not the path.** Any file with an array holding `splash_start` *and*
  `splash_exit` *and* the named rows is edited (no other file in either ark is one; a future content
  pack could be) — the log names the patched file (size + digest). The label edit anchors on
  `mod_settings` **and** its shipped text, so a mod whose strings changed is left alone rather than
  half-renamed.
- **The list needs a conditional to pay.** An option array with no `#ifdef` is refused (`the option
  list carries no conditional to take the freed bytes`). Both shipped files have one.
- **Labels are English-only.** Both R7 labels (§7.1) live in `ui/locale/eng/gen/locale_keep.dtb`,
  anchored on the key *and* the English text, so a differently-worded locale is refused (reason
  logged) and drawn as it ships; the shipped translations of the same keys are untouched.
- **The label needs the unused macro name.** `ulti/locale/gen/locale_mod.dtb` carries `#ifdef HX_PS3`
  (six characters, five of which pay for "Ultimate"); a rebuild without it, or a differently-worded
  label, is refused (reason logged) and drawn as it ships. The English locale has the construct
  twenty-one times over — what pays for R7's two labels.
- **Launcher row from a startup snapshot.** `DetectUltimateState` is read with the launch target's
  state, so installing Ultimate *during* a session shows the row next start — the restart-scoped
  promise the row itself makes.
- **The trial target is not ours.** With `--license_mask=0` the *stock* payload faults at boot, before
  R5's read hook is involved. R5 neither causes nor fixes it.
- **String cvar.** A typo hides nothing and says why (`none of the named rows are in the option
  list`); restart-scoped, since the edit happens at load.
- **R3 is coupled to this build's two statements.** The file's own constants pay (byte-neutral), but
  the *statements* written are this build's: a file whose `update_state` has no first command, or whose
  eight constants changed name, is refused rather than half-edited. Measured on this machine's retail
  file only — an Ultimate payload carries no `server_connect.dtb`.
- **R3's first read is left alone.** `menu_filter: skip offline prompts left a .dtb read alone: the
  node stream does not parse` appears once, roughly 100 ms before the patched read — a read this edit
  does not recognise (not compiled-DTA grammar); the caller gets its bytes untouched and the boot that
  follows is patched. It looks like a failure and is not.

## Part 2 — The background music (was `docs/engine/menu-music.md`)

**The question**
([2026-10-03-customization-plan.md](../plans/finished/2026-10-03-customization-plan.md) C1/R9): where
do the main menu's songs come from once DLC is loaded, and can a DLC song be played there?

**The answer.** The menu's background music is `ShellMusicPanel`, Blitz's rename of the RB3 engine's
`MetaMusic`. Its pool is **data** — the `shellmusic` block of the synth config (`config/synth.dtb`
inside the ark) — three of the title's *own* streamed tracks
(`sfx/shell/samples/shellmusic_1..3.mogg`); every entry's path is built as `sfx/shell/<name>`
(`0x822B7968`). The panel picks one of the three **at random when it is entered** and starts it with
`loop=1`, so the title plays ONE track per menu visit, looping it; the panel's per-frame poll
(`0x827586D8`) drives that stream and tears it down when it ends, but nothing asks for a next track on
its own. DLC songs are never reachable from that prefix: the title opens them from a mounted content
device (`cntXXXXXXXX:\songs\<key>\<key>.<ext>`). So R9 is not a data edit — the pool file's size is
pinned by the ark index and its names cannot express a content device — but hooks on the loader the
pool goes through and on the poll that drives it, fed by packages this project mounts itself.

**The feature.** `enhancements_menu_dlc_songs` + `enhancements_menu_dlc_song_count`
([toggles.md](toggles.md), R9), implemented in [src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp).
Off by default, and inert without a DLC library. Same evidence convention as Part 1.

### 1. Where the pool is

`config/synth.dtb` — 635 bytes in the retail ark (`game/gen/main_xbox_0.ark`) **[tree]** — carries one
`shellmusic` block, decoded here through the same DTB reader R5 uses:

```
(shellmusic
   (volume -10.0)
   (fade_time 1.0)
   (pan -1.0 1.0)
   (music
      (samples/shellmusic_1 (gain 0.0))
      (samples/shellmusic_2 (gain 0.0))
      (samples/shellmusic_3 (gain 0.0))
   )
)
```

**[cited]** the same file is in the community script dump (`rbblitz/360/config/synth.dta`), which is
where the block's shape is legible. The three entries resolve to ark entries
`sfx/shell/samples/shellmusic_1..3.mogg` (4.5–5.0 MB each) **[tree]**, i.e. thematically the title's
own menu tracks, not its full soundtrack.

### 2. Who reads it

`ShellMusicPanel` and `ShellMusic` / `ShellMusicLoader` are recovered from the image's RTTI (the
codegen trace's `VTableScanner`, run over the decrypted `default.xex`):

| Class | COL | Vtable | Members of interest |
| --- | --- | --- | --- |
| `ShellMusicPanel` | `0x821385F8`, `0x8213864C` | `0x8203ADA4` (22 slots), `0x8203AE04` (16) | ctor `0x822B7B98`; the pool loader `0x822B7968` |
| `ShellMusic` (RB3's `MetaMusic`) | `0x82147B08` | `0x82105484` (22 slots) | ctor `0x82758208`; `Load` `0x82757F50` |
| `ShellMusicLoader` (RB3's `MetaMusicLoader`) | `0x82147ABC` | `0x82105384` (5 slots) | |

`0x822B7968` is the whole pool path — per track it:

1. interns `synth`, `shellmusic`, `music` (strings `0x82000AB8`, `0x8203ACF8`, `0x8203AD04`) and asks the config for their array;
2. picks an element **at random** (`0x822B7550`, a `[0, size)` pick) — one entry per call, and the call is the panel's entry point, so one track per menu visit, not per track;
3. formats the name with `"sfx/shell/%s"` (string `0x8203ACE8`) and reads its `gain` (string `0x8203ACE0`);
4. makes a `ShellMusic` (FX dir `sfx/shell/shell_music_fx.milo`, string `0x8203ACC0`) and calls `ShellMusic::Load(path, gain, loop=1, play_from_buffer=1)`;
5. which streams the path. `Load` has exactly one call site in the image — this one — the single point every menu track goes through.

`0x827586D8` is the per-frame poll: the panel calls it every frame (from `0x822B7290`, `0x822B71F0`) and it applies the track's volume. Its end-of-track path (`0x82758904`: clear `ShellMusic+44`, stop the channels) is never reached on the menu, because the stream loops — the `loop` argument only scales a fade at the seam. What moves is the stream's own play position (`Stream+0xA0`, samples, advancing with the audio, verified live), restarting near zero at the seam. A position that went backwards is the observable end of a track — the point R9 hooks.

`ShellMusic` is RB3's `MetaMusic` under another name: a streamed player with a random start offset and a loop, not a fixed sample bank. A pool entry is therefore a **stream path**, and anything the engine can stream can play in the menu.

### 3. Why a DLC song is not a pool entry

A DLC song's files are not under `sfx/shell/` and are not in the ark: the guest opens them from a
mounted content device. A run on this machine shows the shape **[cited]**:

```
Registered symbolic link: cnt000001d3: => \Device\Content\3\
StfsContainerDevice::ResolvePath(\songs\44155)
NtCreateFile FAILED: path='cnt000001d3:\songs\44155\44155.milo' -> 0xc000000f
```

The device name (`cnt%08x`, string `0x8205D650`) is chosen by the *guest* and registered with
`ObCreateSymbolicLink`; the path inside is `\songs\<key>\<key>.<ext>`. The pool's entries are prefixes
of `sfx/shell/`, so no entry the title's own config can hold names one — and `config/synth.dtb` sits
inside the ark, whose index pins its size, so the array cannot grow past three entries either.

### 4. What R9 does

`rb_blitz::shell_music::Configure()` runs from `OnPostLoadXexImage`, after `dlc::Configure()`:

1. reads the R9 toggle and `enhancements_menu_dlc_song_count`;
2. shuffles the package paths (`rb_blitz::dlc::LibraryPackagePaths()` — the structured root's packages
   and the flat library's), walks them in that random order, and collects **every** `.mogg` each
   mounted package holds — a package is one song or a merged pack of dozens — until there are enough
   songs to fill the pool, mounting no more containers than that;
3. mounts each walked package read-only as a device of its own (`\Device\BlitzMenuMusic\<n>\`,
   reachable as `menumusic<n>:`) and samples the collected songs at random, so the pool is a different
   handful every boot;
4. hooks `ShellMusic::Load` (`0x82757F50`). When the title asks for one of its own
   `sfx/shell/...` pool entries, the path is replaced with `menumusic<n>:\songs\<song>\<song>` — a
   different song of exactly the same kind — and the original `Load` runs with it, unchanged
   otherwise (the same gain, loop and FX the panel chose);
5. hooks the poll (`0x827586D8`). While a song R9 started plays it watches that stream's own position
   (`Stream+0xA0`); at the seam, where the song restarts, the position jumps backwards — the one thing
   the guest offers as "this track ended" — and R9 loads the next song of the deck onto the same
   `ShellMusic`, so the menu walks its pool instead of repeating one track. The pool is a shuffled
   deck: every song is heard before any repeats, the deck reshuffles when it runs out, and the first
   song of a new deck is never the one that just played
   ([src/hooks/menu_music_pool.h](../../src/hooks/menu_music_pool.h), tests/menu_music_pool_tests.cpp).

Nothing is copied, extracted or written under `game/`, and with R9 off (or no library) nothing is
mounted and the hook calls the guest unchanged.

**Measured.** An in-game run shows the substitution — `shell_music: the main menu asked for
"sfx/shell/samples/shellmusic_1"; playing "menumusic9:\songs\themonster\themonster.mogg" instead (R9)`
— so the engine appends `.mogg` to a path it is given, and a DLC mixdown streams through the same
`Stream` a stereo one does. It also confirmed the two old defects: the pool was the same packages in the
same order every boot (the old index stride), and the menu asked for a new track only on re-entry — with
the stream looping, one song played as long as the menu stayed open. The rotation signal was read live:
`Stream+0xA0` advances at about 44,100 counts a second and restarted near zero at the seam; three
different songs followed in a row, about a song-length apart
(`14:34:14 rihannashutupanddrive` -> `14:37:55 ringoffire3` -> `14:41:34 funhouse`), with no menu
re-entry between them.

**Measured — the third thing, and what harvesting changed (2026-10-10).** One package is not always one
song. `game/dlc/` is 20 one-file entitlement stubs — each `5841122D/00009000/*` holds only
`asset_v2.bin` and `icon.png`, and is still skipped — plus one **803 MB** container,
`45410914/00000002/65CC8CBB5AE04F1917CF420B5F6346F93109177445`, holding **25** songs
(`songs/16candles/16candles.mogg`, `songs/always/always.mogg`, ...). The finder used to return the first
`.mogg` in the first folder and stop, so that container entered the pool as one song —
`songs\16candles\16candles` — and the boot drew a pool of **one**: the menu opened on "A Little Less
Sixteen Candles" every boot and repeated it forever, a one-song deck having nothing else to deal.
Harvesting every `.mogg` a package holds turns the same boot into `R9 plays the loaded DLC in the main
menu: 25 song(s) drawn at random from 1 mounted package(s) of 21 (50 requested), walked as a shuffled
deck`, led by `songs\jungleboogie\jungleboogie` — all 25 from the one container, in sample order, the 20
stubs still contributing nothing. The rotation over that container was watched live:
`menumusic0:\songs\spoonman2` -> (4:13) `menumusic0:\songs\jessiesgirl` -> (4:12)
`menumusic0:\songs\bangyourhead`, three different songs with no repeat. A second boot of the same
library opened elsewhere again (`songs\jungleboogie`), so the opening song is not pinned to the
container's first entry any more.

### 5. Where it lives

| Thing | Where |
| --- | --- |
| The pool (data) | `config/synth.dtb` in `game/gen/main_xbox_0.ark` |
| The pool reader + random pick | `0x822B7968`, `0x822B7550` (called on panel entry) |
| The streamed player | `ShellMusic` `0x82758208` (ctor), `0x82757F50` (`Load`) |
| The per-frame poll, and a track's end | `0x827586D8`; the stream's position, `Stream+0xA0` |
| The feature | [src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp) |
| The pool sample and the deck (host logic) | [src/hooks/menu_music_pool.h](../../src/hooks/menu_music_pool.h), `tests/menu_music_pool_tests.cpp` |
| The toggles | [toggles.md](toggles.md) (R9) |
| The library it draws from | [dlc.md](../guides/dlc.md), `rb_blitz::dlc::LibraryPackagePaths()` |
