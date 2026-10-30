# The main menu — option list, labels, offline prompts, refresh row, background music

Current refresh behavior is described under **At a glance**. Dated R7 sections
below retain the old persisted-cache and sentinel measurements as history.

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
- **The refresh row** re-points `splash_dlc` to the title's native discovery screen.
  The shortcut defaults Off. When enabled, later discovery passes start a host
  worker rescan before enumerating.
- **The background music** is `ShellMusicPanel`, fed by the ark's `config/synth.dtb` `shellmusic`
  block (three of the title's own tracks). DLC songs cannot be a pool entry, so R9 hooks the pool
  loader and the per-frame poll instead — **on by default since 2026-10-14**, when the feature got a
  visible setting again ("DLC Menu Songs", Help & Options → Downloadable Content) and its cvar became
  that row's carrier.
- **The Play Current Song row** (2026-10-14) is a row this project *adds* to the option array,
  directly after PLAY, plus a case whose body names the menu's own background song — so it needs the
  pool above, and it exists only while that pool does (§7.2).

All five are byte-neutral or read-time edits delivered at the read that carries the file, except R9's
hooks and the Play Current Song row's probe channel, which are host-side.

**Fatal song-load recovery (2026-10-26):** a replacement process carries a
one-shot recovery reason. Authored `splash_panel` polling chooses pad 0's local
user and enters `server_connect_screen` automatically once the screen is stable;
no simulated button press is required. Discovery and the normal background-song
preparation still run, respecting DLC Menu Songs and its launch overrides.
Recovery skips offline prompts for this boot and requests the existing native
error popup once discovery returns to the stable main menu. The owner confirmed
the shorter boot and successful Can't Hold Us playback, then confirmed restored
background music and subsequent playback on the timed-out-load recovery boot
after removing the initial recovery-only disable. See the
[load-guard evidence](../plans/finished/2026-10-13-load-guard.md) part 4 and
[the authored lifecycle](../../src/settings/menu_payload.cpp).
The owner subsequently chose the same restart route for overdue song loads:
Load Guard's Off/30s/60s policy now feeds the external deadline, and a one-shot
30/60-second reason selects the corresponding native timeout message instead of
the crash message. The stable-menu and once-only presentation gates are shared.

**Settings restarts (2026-10-26):** the validated restart handoff enables the same
short boot: select the local user and start discovery automatically, skipping
the splash confirmation and offline prompts. Only crash/timeout recovery shows
an error popup. The offline-prompt bypass lasts for this boot and leaves the
saved setting editable; ordinary launches still use the saved preference.

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
[scripts/assets/hmx_ark.py](../../scripts/assets/hmx_ark.py), [assets.md](../guides/assets.md) § "Compiled DTA". The
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
`enhancements_rename_mod_settings` has nothing to do and Controller Settings does not show the row.

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

**Current behavior (2026-10-28).** When the optional Refresh Songs shortcut is
enabled, its `splash_dlc` action enters `server_connect_screen`, the same native
discovery route the title uses at startup. A later native enumeration pass starts
a host worker rescan and waits for its result before making guest enumerators.
The shortcut defaults Off; the retail Download Content row is a separate
visibility choice. Scan threads remain configurable. The persisted host cache
and the `songcache:/rbbz_dlc_refresh` file-sentinel action were removed.

#### Historical R7 implementation and measurements

The following cache, sentinel and cache-dependent label details describe
builds before 2026-10-28. They are retained to explain the earlier design.

R7's refresh is asked for from the menu, and the row that asks is the same `splash_dlc` case R5
*removes* by default. `TapRefreshCache` rewrites that case's action into two sibling statements and a
padding value:

```
{splash_dlc {ui goto_screen server_connect_screen} {file_exists "songcache:/rbbz_dlc_refresh"} "zzz…"}
```

The first statement is the **discovery pass the title's own start button enters** (`SELECT_MSG`'s
`start.but` branch does the same call), so the press visibly re-runs the discovery - the connect
screen, its bar, the mounting - exactly as a first boot's start does. The second is the signal — the
engine asks the host a file question, and
[src/hooks/dlc_refresh.cpp](../../src/hooks/dlc_refresh.cpp) watches the kernel's file entry points for
that name — and the string after it pads the case's bytes back to exactly what they were.

Two measured constraints shaped this shape (2026-10-15). **Siblings, not a `{do …}` block**: a `do`
wrapper only ever ran its first child in-game — with the call first the screen was never entered
(`out/_swap/menu-run/refresh2`), with the navigation first the call never fired (`refresh3`) — while
retail's own multi-statement cases (`splash_exit`, `splash_achievements`) are sibling statements.
**The navigation first**: the sentinel is answered by no device the emulator serves, so it fails at
the file layer, and nothing after the failure is guaranteed to run. The refresh itself is never the
part that is dropped: a row whose action is too tight for the navigation keeps the call alone and
still refreshes (silently).

The press **waits (bounded at 15 s) for the re-scan**, so the pass enumerates the fresh list: the
pass's aggregate enumerators snapshot the content list when they are **created** — measured, the
creates run before the pass's first message, from guest code the mod cannot wait on — so the waiting
lives in the press, before any of the pass has run ([dlc.md](../guides/dlc.md) §4.2,
[src/hooks/dlc.cpp](../../src/hooks/dlc.cpp) `WaitForRefreshToLand`).

Three properties make the shape safe:

| Property | Why it matters here |
| --- | --- |
| The case's action is data, and the switch only takes a case whose row is selected | giving the row a new action cannot affect any other row, and the old action (an `if_else` on the store) is what the replacement is measured against |
| The call is `file_exists`, a builtin the title registers itself (`DataFunc`) | no new file is needed, nothing is written, and a missing file is the answer the guest already handles |
| The sentinel is a *device* path the guest's own file layer has to resolve | the host sees the name in the path it is handed, so no guest state has to be read to recognise it |

**Why the device.** A bare name and a `d:/rbbz_dlc_refresh` path both reached nothing — both answered out of data the title already had (the option list's symbol table; the boot ark). `songcache:` is a device the title owns and opens files through (`songcache:\songcache` is one of the five paths a warm boot's file calls amount to), so a miss on it becomes an `NtQueryFullAttributesFile`/`NtCreateFile`. The recompiled title imports no CRT file entry point at all (no `GetFileAttributesA`, no `FindFirstFileA`) — `generated/default/rb_blitz_funcs.h` — so the `Nt` calls are the only way a guest file question reaches the host, and the hook watches those three.

The row is reachable only with `enhancements_dlc_cache` on: R5's compiled default hides `splash_dlc`, so the boot turning R7 on also drops that name from the hidden list — a row that refreshes the cache is not an offline-dead row.

**P6 supersedes the dated status/progress behavior below (2026-10-14); a 2026-10-15 change
retires P6's overlay.** An actual host-cache hit now changes the English
`server_connect_enumerating_content` record to **Loading Cached Content List**, at the
whole-record settings-provider stage (`BuildMenuRecords`). The provider logs the precise path,
key, value and hit state; Off/cold/refresh retain the discovery label. **The discovery bar is the
title's own again** — the host pushes P6's aggregate enumerator counts through the engine's own
push and draws no overlay
([native-discovery-bar.md](../plans/finished/2026-10-15-native-discovery-bar.md),
[dlc.md](../guides/dlc.md) §4.4). `UIManager::Poll` (`native_menu.cpp`) observes the usable menu
before restoring pacing. The following 2026-10-10 measurements are retained as the history of the
old implementation.

Split-read checksum fix: `PatchFile` stages digest edits; `CompleteSplitFile` first proves the
already-consumed head is unchanged, delivers the tail, and only then commits those edits.
Previously it changed the guest digest before rejecting an undeliverable edit, producing a
dirty-disc failure. `NtReadFile` uses the completed IOSB status/length (the SDK can return pending
after completing the read); scatter reads are not treated as contiguous file data.

**The two labels.** The row ships as `Download Content` and the connect panel's status line as
`Discovering Downloadable Content`, both in the game's English locale
(`ui/locale/eng/gen/locale_keep.dtb`). R7 rewrites both: the row becomes `Refresh Songs` (renamed
from `Refresh Music Library` on 2026-10-22), the status line `Loading Cached Content List` when the
host answered this boot's enumeration out of the cache (`dlc: CacheServedThisBoot()`) - no
discovery to announce. **Both are built into the
payload's generated locale records since 2026-10-15** (`SetRefreshRowLabel` / `SetDiscoveryCacheLabel`,
[src/settings/menu_payload.cpp](../../src/settings/menu_payload.cpp) - the payload's records carry no
split-read length constraint), because the in-flight `menu_filter` relabel cannot reach a locale that
spans more than two archive reads: with a provider installed the ~87 KB English locale sits across
**three** read blocks and the relabel was silently refused (measured 2026-10-15: the installed copy
drew `Download Content` while the same build without the provider drew the refresh label). The
in-flight relabel remains as the no-payload fallback; its constraints (a file spanning two reads,
written back into the second block only, paid out of the **last** `#ifdef HX_PS3`) are §10's
split-read story ([dlc.md](../guides/dlc.md) §4.2,
[2026-10-10-song-cache-plan.md](../plans/cancelled/2026-10-10-song-cache-plan.md) §11).

**The progress bar.** The same panel carries one (`progress_bar.grp`, `content_percent.lbl` in
`ui/net/gen/server_connect.milo_xbox`, both platforms), driven by a Flow that reads `current_content`
and `total_content`. Nothing in the DTA sets them, so the engine's own object does (§9's
`content_progress.cpp`). Measured 2026-10-10: the engine calls `SetTotal` once, **after** the
enumeration, with a total of **0**, and never calls `Increment` — the Flow sees `-1` and hides the bar
for the whole 46.7 s. With `enhancements_dlc_cache` on the host counts the items itself (the 2,807 XAM
app messages the enumeration is made of, hooked through
`XMsgStartIORequestEx`/`XMsgInProcessCall`/`XMsgSystemProcessCall`) and pushes them with the engine's
own push, so the bar animates 0 → 100%. See [dlc.md](../guides/dlc.md) §4.2,
[2026-10-10-song-cache-plan.md](../plans/cancelled/2026-10-10-song-cache-plan.md) §12 — and
**2026-10-15**: the host drive is restored on top of P6's model — the title's own bar fills
0 → 100 % during the enumeration and the engine refills it for its mounting phase; no overlay is
drawn ([native-discovery-bar.md](../plans/finished/2026-10-15-native-discovery-bar.md)).

**Why 46.7 s, and what shortens it.** One item per *presented* frame ⇒ the screen lasts 2,807 of the
title's frames: 16.66 ms at 60 Hz (46.7 s), 5.68 ms with the frame rate free (15.96 s). Measured
2026-10-10 with R7's `enhancements_dlc_uncap_enumeration`: the host turns the SDK's own `vsync` down
when the first item is counted and restores the player's value when the enumeration ends — 1 ms after
the last item, 104 ms before the engine's own completion, so no menu frame is presented uncapped —
finishing the walk in **15.96 s**. Writes happen on a host thread (a guest thread must not touch the
cvar registry), and the option is off by default: the *V-Sync* row belongs to the player
([dlc.md](../guides/dlc.md) §4.3,
[2026-10-10-song-cache-plan.md](../plans/cancelled/2026-10-10-song-cache-plan.md) §14).

**What is refused.** A file with no `splash_dlc` case, a case whose action is shorter than the call
(a row that can hold the call but not the navigation keeps the call and refreshes without the
visible pass), an action that already carries the sentinel (a second pass), and any file that is
not a `.dtb` this module can re-serialize: each leaves the file exactly as it was and logs why.

**The press also stands the menu's music down** (2026-10-16): the wait below blocks the guest
thread, and a stream left playing through it repeats its last buffered slice. The track is taken
down before the block and the rotation's next song starts on the first menu frame after it - part 2
§4 has the mechanism and the measurement, and `out/_swap/menu-run/music3` is the arm.

**The entry's own switch (2026-10-15).** Help & Options → Main Menu → Extra Items → *Refresh Songs*
(`general.refresh_music_library`, carrier `enhancements_refresh_music_library`, **off by default
since 2026-10-28**) shows that row
on the menu. On, it is a property of the feature rather than of the hide list: off hides `splash_dlc` with the hide master on **or** off and whatever the
Interface ▸ Hide Items selection says, it does not raise the hide list's "the refresh lost its row"
warning (that warning is for a player who hid it *by accident*), and the payload's own locale stops
repainting the row (`refresh_row_active` follows `enhancements_dlc_cache &&
enhancements_refresh_music_library`). The boot's scan is untouched by the switch — a library whose
tree changed is still re-read at the next boot — and with `enhancements_dlc_cache` off the row is
the title's own online entry again, over which this switch has no say ([dlc.md](../guides/dlc.md)
§4.1). The Interface → Main Menu mirror follows the switch too (2026-10-23): with it off the menu
has no `splash_dlc` row, so the page does not offer a visibility toggle for it
(`MenuConfiguration::refresh_row_on`) — the switch here is where it comes back.

**The restored Download Content row (2026-10-20).** Because R7 repurposes `splash_dlc`, the retail
row's shipped label and action were no longer reachable. The payload now clones that shipped case
into `splash_dlc_store` (the row right after `splash_dlc` in the option array) and gives it the
*Download content* locale entry (sentence case since 2026-10-23, owner request), but only on a boot
whose `interface.hide_download_content` row is
set to *Visible*. That row is on Interface → Main Menu and defaults to **Hidden**: a fresh install
does not offer an entry that only ever worked online, and the player can turn it back on one page
away. With the cache off the retail `splash_dlc` row is the Download Content entry itself, so the
separate switch is not offered. The clone copies the case verbatim, so its action is still the
title's own online-store branch — the refresh sentinel belongs to `splash_dlc` alone.

### 7.2 The Play Current Song row (2026-10-14)

**The feature.** The main menu's **second** entry starts whichever song the menu is playing as its
background music (R9, part 2): with Ultimate installed it lands on `powerup_select_screen` — the
screen the game's own flow uses with `unlockall`, where the player picks power-ups and presses PLAY
SONG — and on the offline base target it goes straight to the loading screen, exactly as the song
list's own Confirm does. On by default (`general.play_current_song`, "Play Current Song" on Help &
Options → Downloadable Content), and *built only by a boot that has a pool*: with the DLC music off,
or a library that yielded no song, the menu entry is absent — a row that exists must work. **A boot
that found no DLC at all hides the settings row itself (owner request, 2026-10-20)** rather than
drawing it disabled, because there is nothing it could ever start; with a library present but the
DLC music off, the row stays and reads `Needs DLC Menu Songs`. **The landing is now measured
(2026-10-15):** press → the song the menu is playing is resolved → `loading_screen` → the game's
first screen → `game_screen`, with the visible song matching the audible one; the arms are in
[play-current-song.md](../plans/finished/2026-10-14-play-current-song.md) §Evidence.

**The two halves.**

1. **The record edit** ([src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp),
   `AddPlayCurrentSongRow`): `splash_play_current` is inserted directly after `splash_start` in the
   option array (the array holding both `splash_start` and `splash_exit`, §1), and a case is appended
   to the `{switch {start.lst selected_sym} ...}` table that dispatches it. The localised text is a
   locale entry of its own (`AddLocaleEntry`, §6's shape), added only on the boot that builds the
   row. Nothing is byte-neutral here: our payload record is a new record in the patch ark, so it may
   grow (§ "Content resolution" in [architecture.md](../settings/architecture.md)).
2. **The body** is made of statements shipped records already make, which is what keeps it honest:
   `{song_mgr get_song_id_from_short_name <name>}` → `{song_mgr data <id>}` →
   `{loading_screen set_song <data>}` + the content mount are `quick_start_song`
   (`ui/pause/gen/pause_to_new_game.dtb`), and the branch that follows is the song list's own
   `song_chosen` (`ui/song_select/gen/song_select.dtb`, on `song_select_screen`).

**Which song is playing is the host's fact.** The row captures a selection with
`file_exists("rbbz-play/begin")`, then reads `read_file("rbbz-play/song")`. The latter
returns a native Symbol DataNode through the title's own intern constructor, preserving
the metadata key's spelling and SongMgr's type contract. This replaces the boot-baked
per-index name list on 2026-10-26: songs appended by background preparation work without
rewriting or reloading the active menu record. The indexed `rbbz-play/current/<index>`
queries remain available for diagnostics.

The key comes from `songs.dta`/`songs.dtb`: the top-level song Symbol whose
`(song (name ...))` matches the complete audio path. The stream basename is a fallback
when metadata is absent/unreadable; readable metadata excludes unmatched or ambiguous
streams. Begin/read/result queries belong to the menu-music feature and work independently
of the settings-channel switch. During a silent handoff the selection keeps the last
heard song for up to **2 s**, then uses the pending candidate. Once the new stream
advances, its song wins immediately. Failed candidates, title fallback and screen exit
invalidate their selections. Appending pool entries preserves every existing index and
the selection captured by a press.

**Why the lookup answered 0, and the fix (measured 2026-10-15).** The 2026-10-14 reading below was
correct about *what* happened and wrong about the cause: `SongMgr`'s native handler (`0x8244938C`,
interning its argument through `0x823F94A0`) reads a **Symbol** payload, and the body handed it a
**String** — the lookup then never matched, for the pool's own key and for an on-disc name alike, and
`song_mgr data 0` handed the loading screen a null song. The fix supplied Symbol nodes instead of Strings. Since 2026-10-26 the live
read returns that same native Symbol type (`menu_music_guest_tests` pins its result
cell, metadata spelling and calling context), so the lookup contract remains intact:
`song_select_panel set_current_song` takes the resolved metadata before the `loading_screen` hand-off,
so the song list's own current-song state agrees with what plays. A press with **no** selectable track
(for example, title fallback) or a name the manager does not
carry draws an explicit message — *"The background song is not available to play. Please try again
when another song starts."* (`settings_play_unavailable_screen`) — instead of the old silent no-op.

**Measured (2026-10-15)** — `out/_swap/music-fix/final-press` (two-package library, disposable profile,
windowed D3D12). *Play Current Song* was pressed while `highvoltage_live` was audibly playing: the host
answered the probe (`Play Current Song: the menu is playing pool 1 of 2 ("highvoltage_live")`), the
game's own teardown logged `the game took this menu track down; the rotation stops watching` (the deck
did **not** advance), one second later `loading_screen` mounted the song the press resolved
(`Loading STFS header file: …\ACDC - High Voltage (Live)`), then the game's first screen (*"In Rock
Band Blitz, it's all about earning big points… PRESS (A) TO BEGIN"*) and, on the A, `game_screen` —
captured in `final-press/shots/{row2,loading,game2,gameplay}.png`. **Zero guest faults**, and the song
the loading screen took is the song that was playing. The 2026-10-14 arms that dead-ended at the
manager's "0" are kept in [play-current-song.md](../plans/finished/2026-10-14-play-current-song.md)
§Evidence; the one open sibling is the title's **own** song list, which faults for a cause this fix
does not claim to have touched ([known-issues.md](../reference/known-issues.md)).

**Evidence [boot]** — the boot lines to look for are
`settings_menu: "DLC Menu Songs" is on; N menu-music song(s); the Play Current Song entry is built`,
`shell_music: Play Current Song request: selected=... audible=... pending=... title_fallback=...`,
`Play Current Song result: resolved` (or the older indexed diagnostic
`Play Current Song: selected pool i of N ("<key>")`),
`no-track`, `missing-name` or `missing-data`. Selection alone does **not** prove guest lookup success.
On an unavailable press the `settings_play_unavailable` page remains; arms and captures are in
[play-current-song.md](../plans/finished/2026-10-14-play-current-song.md) §Evidence.

### 7.3 The RhythmVerse row (2026-10-20; shortened 2026-10-22)

**The feature.** The main menu's second store entry, and the way into the retail
`store_menu_screen` without Xbox LIVE: the retail row's own case is gated by
`{user_mgr is_participating_user_online}`, which offline always answers false. The row is built only
while `general.rhythmverse` was on at boot; its label is `RhythmVerse`
(`rbbz_rhythmverse`, a locale entry of its own on that boot only) - `RhythmVerse Customs` until the
owner shortened it on 2026-10-22.

**The edit.** `ui::screen::AddRhythmVerseRow` ([src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp))
inserts the symbol into the option array directly after the restored *Download Content* row when
that boot built it, otherwise after `splash_dlc`, and appends a case to the main menu's own
`{switch {start.lst selected_sym} ...}` table. The case has **no online check and no Marketplace
call**: it enters `store_menu_screen` only while `{file_exists "rbbz-store/enabled"}` answers true — a
one-bit query the host answers from the store's settings snapshot
([src/store/store_settings.h](../../src/store/store_settings.h), hooked in
[settings_bridge_hooks.cpp](../../src/hooks/settings_bridge_hooks.cpp) beside the play probes, so it
works even on a boot that turns the settings channel off). R7's `splash_dlc` refresh keeps its own
row, action and label untouched.

**Limits.** The menu record is baked once per boot, so the master switch's *presence* effect is a
next-start matter: off stops a press immediately (the case re-reads the gate on every entry) but the
row stays drawn until the next boot, and turning the store on needs a restart for the row to appear.

**Measured 2026-10-20, and the two defects that measurement found** (boots under
`out/_swap/store-probe/`, plan §5/P2 record). With the master on: the menu reads **PLAY / PLAY
CURRENT SONG / HELP & OPTIONS / EXIT GAME / RHYTHMVERSE CUSTOMS** (that boot's label - the row has
read **RHYTHMVERSE** since 2026-10-22), the entry opens the retail
`store_menu_screen` (MUSIC STORE / RECOMMENDED / YOUR SONGS … `LOADING`), and B returns to the same
menu. Two things had to be fixed before that sentence was true:

1. **The row could not be drawn above EXIT GAME by choosing another insert index.** Appended after
   `splash_dlc` (the retail store row's own position in the shipped option array, which follows
   `splash_exit`) it is selectable; inserted before `splash_exit` it drew its label and the list's
   navigation skipped it (3 and 4 taps both landed the highlight on EXIT GAME). This was recorded
   as the reason the row was drawn last; since 2026-10-23 it is drawn third, because the array is
   re-ordered after every insertion (§7.4) — the insert index stays the one position measured to
   navigate, and the draw order is a separate step.
2. **R5's hide decision has to be baked into the generated record.** The in-flight edit logs
   `hid 3 row(s)` and is then not delivered: `an edit to a file spanning two reads (ark offset
   16041952) reaches into the first 14368 byte(s), which this read does not carry and the guest has
   already copied`. With LEADERBOARDS and ACHIEVEMENTS still drawn, a new row is pushed off the
   visible page (measured: the seventh row dispatched the store and was not on screen). The decision
   now also rides the generated payload (`ui::screen::HideMenuRows`, fed by
   `menu_filter::HiddenRows()`), where there is no byte budget; the in-flight edit stays as the
   fallback for a boot that builds no payload.

**Not yet measured**: ≥5 consecutive boots, cache On/Off, store On/Off after a restart, Ultimate
absent, and Refresh Songs' sentinel in the same run. `rb_blitz_settings_screen_tests` pins
the insertion (after the store row), the baked hide decision, the label, the gate, the refusal and a
reparse; `rb_blitz_store_client_tests` pins the query answers. The 2026-10-20 order above is
superseded by §7.4's: the row now reads third, and that sequence is still **unproven on screen**.

### 7.4 The menu's row order (2026-10-23)

**The order.** Play, Play Current Song, RhythmVerse, Refresh Songs, then retail's own rows (the two
the hide decision removes by default, Download content when the visibility switch is on, Help &
Options, Upgrade), then **Exit Game, always last** — so a player reaching for the exit never reads
past the rows this project added. With the stock switches the menu draws **PLAY / PLAY CURRENT SONG /
RHYTHMVERSE / REFRESH SONGS / HELP & OPTIONS / EXIT GAME** (six rows, the visible page's budget).

**The edit.** `ui::screen::OrderMainMenuRows`
([src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp)) permutes the option array's own
elements among the slots they already occupy, by the rank table next to it; the payload calls it
last, after every insertion and after the hide decision
([src/settings/menu_payload.cpp](../../src/settings/menu_payload.cpp)). Two properties make it safe:

| Property | Why it matters |
| --- | --- |
| A *slot* is one row element **or one whole `#ifdef ... #endif` block whose contents are only row symbols** | retail's record parks `splash_exit` inside `#ifdef HX_XBOX` (the payload's lists it as an element of its own, `out/_swap/retail-splash.dtb` vs the payload's `split-headers/records` dump). The block travels with its directives, so the conditional stack stays balanced — a dangling `#endif` faults at load (§3, §8) |
| A row the table does not name keeps its relative place after the named ones, and Exit Game's rank is last in every shape | an unknown payload row (`mod_settings`, a future provider entry) is neither dropped nor promoted, and the exit cannot be pushed off the end by one |

The array keeps its length (a permutation, not an insert), so nothing about the byte-neutral
in-flight edit changes. Both shipped shapes are pinned by `rb_blitz_settings_screen_tests`
(`TestMainMenuOrder`: the exact drawn sequences, idempotence, and the refusal on a record without an
option array).

**Unproven on screen.** The 2026-10-20 measurement above is the one thing to watch: a row *inserted*
before EXIT GAME was drawn and skipped by navigation. The reorder is not an insertion — the array's
element count and every element's identity are unchanged, only their places move — but whether the
list navigates a permuted array straight is a boot's question, not a test's. When measuring, count
rows by the logged bounds (`native_mouse: row=… bounds=…`), never by taps: one `lstick_down` moves
two rows on this title (the duplicate-press note in
[diagnostics.md](diagnostics.md)).

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

**R5 as OCR.** `scripts/diagnostics/observe_ui.ps1 -State main-menu -Ocr` with the toggle **on**, both
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
`scripts/diagnostics/observe_ui.ps1 -State main-menu -Ocr` with R3 on (its default) **[cited]**: passed — the
state's own needle (`DOWNLOAD CONTENT`) is on the screen after the route's **single** accept — the
boot-flow change the toggle makes. `scripts/diagnostics/observe_ui.ps1 -State "offline prompt" -Ocr
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
| the settings rows | [src/ui/settings_screen.cpp](../../src/ui/settings_screen.cpp) (Interface → Hide Items → Main Menu, and R3's own row under Interface) |
| the tests | [tests/menu_options_tests.cpp](../../tests/menu_options_tests.cpp), [tests/settings_screen_tests.cpp](../../tests/settings_screen_tests.cpp) |
| the harness | [scripts/diagnostics/ui_states.ps1](../../scripts/diagnostics/ui_states.ps1) (the `RBBLITZ_SKIP_OFFLINE_PROMPTS` knob and the `offline-prompt` state), [scripts/diagnostics/observe_ui.ps1](../../scripts/diagnostics/observe_ui.ps1) (`-SkipOfflinePrompts`) |

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
- **Controller Settings row from a startup snapshot.** `DetectUltimateState` is read with the launch target's
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

`0x827586D8` is the per-frame poll: the panel calls it every frame (from `0x822B7290`, `0x822B71F0`) and it applies the track's volume. In retail the stream loops — the panel's `loop=1`/`play_from_buffer=1` arguments and the config's own fields (`ShellMusic+48`, `+60`) make it restart before its end — so the poll's end-of-track path (`0x82758904`: clear `ShellMusic+44`, stop the channels) is not reached on the menu, and the observable motion is the stream's own play position (`Stream+0xA0`, samples, advancing with the audio at ~44,100 counts a second). R9's handoff forces those loop fields off, and then the end of a song is the stream's own `StandardStream` **state 6** (`IsFinished`, `0x823E30D8`) — which the poll hook observes before the guest's teardown runs (2026-10-15; the earlier reading of a position rewind is kept in the P5 paragraph below as measured but superseded).

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
   different song of exactly the same kind — and the original `Load` runs with it (the same gain and
   FX the panel chose; the panel's `loop`/`play_from_buffer` arguments and the config's own loop
   fields are forced off, so a streamed DLC song has one play and one end);
5. hooks the poll (`0x827586D8`). While a song R9 started plays it watches the stream's own state:
   the end is `StandardStream` **state 6** (`IsFinished`, `0x823E30D8`), and when it arrives R9
   retires the finished stream (its destructor, `ShellMusic+44` cleared) and loads + starts the next
   song of the rotation onto the same `ShellMusic`, so the menu walks its pool instead of repeating
   one track ([src/hooks/menu_music_playback.h](../../src/hooks/menu_music_playback.h) is that
   state machine). A track that never produces a sample is a load that failed: that song sits out the
   pass and the next candidate is tried, with a bounded number of tries before the rotation gives up
   for this boot and hands the menu back to the title's own tracks. A guest `Stop`/`StopStreams`/
   destructor on that `ShellMusic` is **navigation** (the loading screen taking the audio over, the
   menu being left): the rotation stops watching, keeps its history, and does **not** start another
   song — measured 2026-10-15 (§ the correction below).
6. keeps the rotation honest over the **pool** it drew, not over the library: every distinct song of
   the pool is heard once before any of them is heard again, no song ever follows itself — including
   across a reshuffle, a menu re-entry or a pool rebuilt by a library refresh, because the order is
   remembered by song *identity* rather than by position — and a pool that can only offer the song
   that just played is not a rotation at all: that song plays once and the menu is then given one of
   the title's own three tracks, rather than looping one song forever
   ([src/hooks/menu_music_pool.h](../../src/hooks/menu_music_pool.h), tests/menu_music_pool_tests.cpp).

Nothing is copied, extracted or written under `game/`, and with R9 off (or no library) nothing is
mounted and the hook calls the guest unchanged.

**R7's refresh press stands the menu down, and starts the next song itself (2026-10-16).** The press
blocks the guest thread while the library scan catches up ([src/hooks/dlc.cpp](../../src/hooks/dlc.cpp)'s
bounded wait), and a stream left playing with nothing advancing it makes the audio device repeat its
last buffered slice - the owner heard it as a stuck record. So the press takes the menu's current
track down **before** it blocks (`shell_music::StandDownForRefresh`, called from the refresh
sentinel's file hook [src/hooks/dlc_refresh.cpp](../../src/hooks/dlc_refresh.cpp)): the same teardown
a screen change does - the stream's own destructor, `ShellMusic+44` cleared, `StopStreams`, all under
`g_setting_up_music` so it cannot be read as the game navigating. It is deliberately scoped to a boot
that can start music again (**R9 on and a pool mounted**): with R9 off the track belongs to the
title, whose own screens restart it, and this layer must not leave the menu silent for music it did
not play.

The resume is this layer's job, because the menu's own re-entry after the pass does **not** reload
the pool (measured: no `the main menu asked for` line follows the return). The poll starts the
rotation's next song on the first menu frame at least **1.5 s** after the stand-down - the delay
keeps a poll that slips through the screen transition from starting music into a screen that is
already leaving - and the interrupted song counts as played, so the music comes back as a
**different** one. Measured 2026-10-16 (`out/_swap/menu-run/music3`, 300 pre-linked packages, Ultimate,
windowed, `-PreLink 300`): `anything` audibly playing, press at 00:16:47.362 → `the refresh press
takes this menu track down ("anything")` 56 ms later → the fresh 300-package list handed over 1.04 s
after the press, the pass enumerating it (`added 300 items`, attempt 2) → `the menu is back after the
refresh; the rotation starts the next song` at 00:16:48.925 and `justthewayyouare` audibly playing at
00:16:49.351 - **music is back 1.9 s after the press, already on the discovery screen** - and no
`the game took this menu track down` line for it afterwards. That arm's window ended while the pass
was still finalising; `out/_swap/menu-run/refresh16` is the arm where the same press runs the pass to
its end (`usable menu attempt=2 total_ms=27966`, the fresh list enumerated as 300 items on both
aggregate passes).
The test harness grew the arm it needed: `refresh-check.ps1 -PreLink N` links packages **before** the
boot (so the menu really has an R9 song playing) and `-LinkCount -1` adds none afterwards; it also
waits for the title's own `content_pipeline: usable menu attempt=1` before driving the menu, because a
fixed wait pressed through the loading screen and missed the row (`music1`).

**Measured.** An in-game run shows the substitution — `shell_music: the main menu asked for
"sfx/shell/samples/shellmusic_1"; playing "menumusic9:\songs\themonster\themonster.mogg" instead (R9)`
— so the engine appends `.mogg` to a path it is given, and a DLC mixdown streams through the same
`Stream` a stereo one does. It also confirmed the two old defects: the pool was the same packages in the
same order every boot (the old index stride), and the menu asked for a new track only on re-entry — with
the stream looping, one song played as long as the menu stayed open. The rotation signal was read live:
`Stream+0xA0` advances at about 44,100 counts a second and restarted near zero at the seam; three
different songs followed in a row, about a song-length apart
(`14:34:14 rihannashutupanddrive` -> `14:37:55 ringoffire3` -> `14:41:34 funhouse`), with no menu
re-entry between them. **Superseded in part (2026-10-15):** the "three different songs followed" is the
same symptom the P5 paragraph below records — the host's next-track `Load` did not replace the playing
stream, so those three names rotated in the log while the first song's audio kept playing. The
substitution itself (the log line quoted here) remains measured and true.

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

**Measured — the rotation and its transition cost (2026-10-14, P5; superseded where it disagrees
with the correction below — its log lines are real, but they observed one repeating stream and the
host's bookkeeping, not a rotation).** A fixture library of three
single-song packages (`Rancid - Maxwell Murder`, `FIDLAR - Wake Bake Skate`,
`The Beatles - I'll Follow the Sun`, symlinked, so the pool **is** three songs) was held on the main
menu for two passes with the instrumentation in [shell_music.cpp](../../src/hooks/shell_music.cpp)
logging the seam and the next track's first samples. One arm's sequence was
`MaxwellMurder` -> `wakebake` -> `far_ifollowthesun` -> `MaxwellMurder`, the last pick logging
`opening a new pass over the 3 song(s) of the pool`: every distinct song was heard before any of them
repeated, no song followed itself, and the song that had just played did not open the new pass.
Transition cost — measured as **seam -> the next track's first samples** — was a median of **81 ms**
(81-83 ms over three transitions; an arm with the same fixtures and the earlier index-based deck
measured 86 ms median and 250 ms maximum). The `ShellMusic::Load` call inside that window took 0-2 ms,
so the number is the guest's own stream startup plus the audio device, not host work: nothing was
prebuffered, no load was moved off the guest thread, and no song duration is guessed.

**What the seam was taken to be (open 2026-10-14; answered 2026-10-15 below).** The trigger is the
guest restarting its own stream, seen as the stream's sample counter (`Stream+0xA0`, verified
advancing at ~44,100 counts a second) moving backwards. Its period was **not** proven to be the
file's true end: the same three fixture packages appeared to wrap after **109.6 s** in one boot and
**93.5 s** in another, which a per-file constant would rule out, while the 2026-10-10 measurement
recorded ~221 s for a 3:33 commercial track. The reading most consistent with all three was that the
guest loops the stream at the point its own streaming buffer has reached, so the menu can play only
the buffered prefix of a track before R9 replaces it (with R9 off, the title loops exactly that
prefix).

**Measured — the rotation that actually changes the song (2026-10-15).** What the P5 arms could not
show, and did not, is that the audio changed: `ShellMusic::Load` only stages a source
(`0x82757F50`); the stream is built by `ShellMusic::Start` (`0x82758B98`), and Start **returns after
a fade** when a stream is already playing (`0x82758BC4`) — so the old next-track `Load` left the
first song's stream and audio running while the logs, and "Play Current Song", moved on. The
handoff now retires the old stream through its own destructor, clears `ShellMusic+44`, stages the
new source and calls `Start` ([src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp),
`LoadOnGuest`). The handoff drives Load's own `loop` argument (r7, which Load stores into
`ShellMusic+48` and Start turns into the stream's jump at `0x82758D64`): **off** for a DLC song,
which must end so the rotation can advance; **on** for a title fallback, which loops as the title's
own music does (the config's `play_from_memory` field, `ShellMusic+60`, is a separate knob and is
left as Load reads it). And
the end of a song is `StandardStream` **state 6** (`IsFinished`, `0x823E30D8`; `IsPlaying` is state
3, `0x823E30F0`): the poll observes it before the guest's own teardown. The same state is also what
the title reaches when it takes the menu music down on a screen change, so the hooks treat a guest
`Stop` (`0x82758998`), `StopStreams` (`0x827583F8`) or destructor (`0x82758490`) on the menu
`ShellMusic` as **navigation** — the rotation stops watching without advancing — and only a stream
that reaches its own end rotates.

Five driven boots measure it (`out/_swap/music-fix/`, disposable roots, D3D12, windowed).
**`rotation3`** held the three-package fixture library on the menu for 30 minutes: eight
transitions, three full passes, **no song ever following itself**, every pass playing all three — and
each song now ending at its own stable length (`far_ifollowthesun` 115.0 s in each of its three
plays, `wakebake` 109.6 s, `MaxwellMurder` 93.5 s), which the superseded paragraph above had misread
as one file wrapping at two lengths. Each handover was audible **82–102 ms** after the previous
track's end (`started playback … ms after the previous track's end`); **`final-rotation`**, the same
pool on the final build (every teardown hook in), reproduced three passes in ~15 minutes with the
same per-song lengths and no repeat. **`final-one`** (one-package
pool) played its song once and logged `the DLC pool has nothing new to play; the menu goes back to
the title's own tracks` — the sole DLC song is played, not looped. Its first pass also showed the
fallback **one-shot** (an earlier cut of the handoff forced the no-loop flag on the fallback too,
and the menu went silent after the title track was through); **`final-one-b`** is the fix — the
title fallback keeps the panel's own `loop = 1`, and the watcher shows it: `the title fallback track
is playing at 241.2 s`, `… looped after 349.9 s of samples`, `… playing at 11.4 s` — the title track
loops at its own length and the menu never runs dry. **`final-press`** pressed *Play Current Song* mid-track: the press resolved the audible song
and the game's own stop was caught one line later — `the game took this menu track down; the
rotation stops watching ("highvoltage_live")` — the deck did **not** advance while the loading
screen took the audio over (§7.2 has the rest of that arm).

**Selection reliability follow-up (2026-10-15).** Startup now gets **8 s** to produce its first
samples; a stream that was already advancing still fails after **3 s** without progress. A successful
start clears prior failure strikes, so three unrelated slow starts separated by successful plays
cannot permanently quarantine a song. Three consecutive unsuccessful starts still retire it. The
2 s selection grace and metadata lookup in §7.2 reduce unavailable presses without changing the
no-immediate-repeat rule or selecting a DLC song while title music is playing. Host tests cover the
boundaries and failed-candidate invalidation. A copied package with renamed assets, retaining its
original song key, resolves and reaches gameplay in `out/_swap/music-fix/reliability-path/`; see the
[follow-up record](../plans/finished/2026-10-14-play-current-song.md#2026-10-15-reducing-unavailable-presses).

### 5. Where it lives

| Thing | Where |
| --- | --- |
| The pool (data) | `config/synth.dtb` in `game/gen/main_xbox_0.ark` |
| The pool reader + random pick | `0x822B7968`, `0x822B7550` (called on panel entry) |
| The streamed player | `ShellMusic` `0x82758208` (ctor), `0x82757F50` (`Load`), `0x82758B98` (`Start`), `0x827583F8` (`StopStreams`), `0x82758998` (`Stop`), `0x82758490` (destructor) |
| The per-frame poll, and a track's end | `0x827586D8`; the end is `StandardStream` state 6 (`0x823E30D8`) |
| The feature | [src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp) |
| The rotation's playback state machine (host logic) | [src/hooks/menu_music_playback.h](../../src/hooks/menu_music_playback.h) |
| Package metadata to song-key mapping | [src/hooks/menu_music_metadata.cpp](../../src/hooks/menu_music_metadata.cpp), `tests/menu_music_pool_tests.cpp` |
| The pool sample and the deck (host logic) | [src/hooks/menu_music_pool.h](../../src/hooks/menu_music_pool.h), `tests/menu_music_pool_tests.cpp` |
| The toggles | [toggles.md](toggles.md) (R9) |
| The library it draws from | [dlc.md](../guides/dlc.md), `rb_blitz::dlc::LibraryPackagePaths()` |


### Startup cost of the DLC music pool (2026-10-24)

Historical implementation; superseded by **Rolling menu-music session and mount cache (2026-10-26)** below.

The last reported slow boot (`rb_blitz_299.log`) spent about 44 seconds preparing
50 menu-music packages serially, before guest execution. Pool preparation now mounts
and reads metadata for up to four independent packages concurrently. The host
registers completed devices and assembles the pool in the original random draw
order; workers do not access the VFS, guest memory, RNG, or playback state. The
50-song cap and the song keys baked into Play Current Song remain unchanged.
`shell_music: pool prepared in ... ms` reports this phase's elapsed time. Startup
also rejects ordinary filenames before querying directory attributes when deciding
whether a DLC root uses the structured layout (the same log measured that layout
check at 6.75 seconds for a flat library). End-to-end timings await the owner's test.

### 6. Background music under loading pressure (2026-10-24)

**Status:** implementation and headless checks; listening validation pending with
an owner-driven run. The owner reports occasional background-music stutters during
song-cache loading and opening RhythmVerse, becoming more frequent during P6's
post-A graphics preparation. The exact reported breaks were not time-correlated
with device underruns, so this does not attribute every break to one cause.

Source inspection found three independent pressure points:

- The native `synth.stream_buf_size` default is **1.2 seconds** of decoded stream
  capacity (`out/system_default.dta.txt`, retained in the installed record).
  `StandardStream` constructor `0x823E5BE0` forwards f2 to `0x823E5748`, which stores
  it at stream +48 and resolves zero through `stream_buf_size`. Channel setup
  `0x823E52B0` sizes the ring from that value × sample rate × 2 bytes, rounded to
  native blocks. ShellMusic's factory calls pass zero for this argument.
- The SDL output queue defaults to **8 × 256 / 48000 = 42.67 ms**, and previously
  allocated/recycled heap buffers while holding a queue mutex through conversion
  and `SDL_PutAudioStreamData`. This gives little tolerance for delayed delivery;
  it does not establish that every music skip is an output underrun.
- Background preparation could use logical-processor-count minus one translation/
  creation workers (Vulkan auto creation: three quarters of CPUs), at normal
  priority. Its command processor also made synchronous driver pipeline calls in
  each service slice; a bounded count is not a bounded wall-clock time.

The changes preserve the title's music player, volume, loop/rotation and scene
lifetime while adding headroom at each boundary:

1. **Menu-only decoded reserve:** `menu_audio_policy.h` and the constructor hook in
   `shell_music.cpp` request at least **6 seconds** while ShellMusic is constructing
   a stream. The scope is thread-local and also covers the DLC handoff's direct
   Start call. Both native title tracks and DLC tracks benefit. An explicit larger
   stream request is retained; gameplay streams keep their original buffer request.
   This is capacity ahead of the music playhead, not a six-second output delay.
   Startup may take longer to accumulate data; the actual duration is unmeasured.
   Its bounded startup grace is now 15 seconds (previously 8); the 3-second
   deadline for a stream that stops advancing after playback begins is unchanged.
2. **Audio scheduling:** SDK patch 0031 raises Audio Worker and XMA Decoder to
   `ThreadPriority::kHighest` (normal process class, not realtime). SDL already gives
   its playback thread time-critical/MMCSS handling. Background preparation workers
   run below normal, with at most four active workers and two logical CPUs left
   outside that budget where hardware permits (one worker on one/two-CPU systems).
   Async preparation delegates driver creation to these workers instead of making
   additional driver calls on the submission/command-processor thread. The blocking
   legacy path and a no-worker fallback can still participate on that thread.
3. **Bounded PCM handoff:** patch 0032 gives each SDL driver a fixed 64-frame ring,
   matching the existing maximum queue setting. The queue owns each submitted
   frame and holds its mutex only for a bounded PCM copy. Mixing and SDL submission
   happen after unlocking; the queue needs no per-frame heap allocation or recycling.
   Device callbacks use stack arrays and return one producer credit per consumed
   frame, including an SDL submission failure. `audio_maxqframes` stays at its
   existing value, preserving output latency and gameplay calibration.

`Audio starvation` warnings count missing 256-sample output frames after the first
submission and report the largest device-callback gap. They are rate-limited to
one per five seconds on the producer, never emitted on the device callback's
normal path. Shutdown prints cumulative submitted/starved counts. Startup silence
before any submission is excluded. These diagnose output delivery; a decoder/source
loop that keeps submitting repeated samples may stutter with no queue underrun.
No interpolation, repeated-frame concealment or audio/video clock drift is added.

Evidence and checks live under ignored `out/audio-stability/`: the owner's
`rb_blitz_328.log` copy records a successful **432/432** P6 preparation in **15,240 ms**,
which confirms the normal menu continuation, not stable audio. The native stream
ABI/config dump is inspected locally; no game bytes are distributed. The new
`audio_stability` test verifies queue ownership, capacity, wraparound and clear,
then runs **20,000 PCM frames** through producer/consumer threads with deliberate
pauses, checking exact sample order and no torn/replayed frames. It also checks
menu/gameplay scope and CPU budgeting. Listening under song-cache loading, graphics
preparation and RhythmVerse remains with the owner; this change has not driven UI.

Validation, 2026-10-24: release rebuild passes for the executable and both graphics
backends. The four directly affected ctests pass after the final startup-grace
change (`ctest-targeted-final.log`); the full roster is **65/66**, with only the
previously failing, unchanged `path_policy` nonexistent-descendant check. SDK exact
replay reports **31 patches, 0 failed, 85 patched files, 0 unexpected**
(`sdk-apply.log`). The docs checker reports 40 pre-existing reference errors;
no audio-follow-up link is among them (`docs-check.json`). All evidence named in
this paragraph is under `out/audio-stability/`. No listening or game/UI test was
performed by the agent; the owner retains that role.


### Rolling menu-music session and mount cache (2026-10-26)

**Cold opening correction (2026-10-28).** With a valid complete catalog, the
first song is still drawn uniformly from every catalog song. Without that cache,
boot shuffles the DLC package list using the session RNG, opens packages in that
random order until one has playable songs, and draws uniformly within that
package. It then prepares three successors from further randomly ordered DLC
packages so Next works while the full catalog is still being built. A Next
request can put another package probe ahead of the catalog's next batch. The
opening needs one successful package mount before music can start. It is
uniform by package, not by individual song: a merged pack's songs each have
lower first-song odds than a singleton package's song. Exact song-wide odds
would require song counts from every package or a valid cached index. The full
catalog continues building after the opener and is saved for later boots.
While the chosen package is loading, or the library has no ready successor,
Library mode stays silent. It never plays the title's shell tracks as a fallback;
Play Current Song has no selection until a library stream advances.

The owner's slow boot, `out/build/win-amd64-release/logs/rb_blitz_350.log`, spent
**75,424 ms** preparing 50 menu-music packages before guest launch. The enumeration
cache was a hit (fingerprint 123.531 ms, load 93.392 ms); later graphics preparation
completed 423/423 pipelines in 7,901 ms. An initial follow-up deferred the 50-song fill;
the owner's approved architecture now replaces that fixed working set entirely.

Boot mounts the first usable package from a uniform full-library shuffle, seeded from
16 `std::random_device` values. Selection is uniform among usable packages; a merged
pack's songs are drawn randomly as its catalog becomes available. Strict uniform odds
across every individual song would require a complete song index. Invalid or ambiguous
sources are skipped; independent boots may legitimately pick the same package.

`MusicSession` owns stable catalog IDs, one ordered playlist and its cursor. Metadata
survives mount eviction. Only three successors are planned ahead; two recent songs are
kept warm alongside the current source. A merged pack can expose hundreds of catalog
records while occupying one mount. Lazy discovery continues beyond 50 songs. No fresh
pass begins until every boot-enumerated package has been considered and the first
pass's eligible songs have been scheduled. A new randomized pass avoids an immediate
repeat; a sole usable song ends into silence instead of repeating forever.
Failure outcomes are distinct from heard/skipped outcomes, and repeated failures are
quarantined. A failed remount marks its package unavailable for this session.

`BeginLoadingAttempt` starts a demand-driven host worker. It owns its package-list
snapshot and standalone devices; it never touches the live VFS, guest memory or RNG.
Native loading and ShellMusic polls publish already-prepared results and request more
work only when the warm window needs it. Gameplay stops new speculative requests; an
in-flight mount may finish. Queue shutdown never joins that mount on the UI thread.

`MusicMountCache` caps published residency at **eight packages**. The active stream,
pending source and captured Play Current Song selection pin their packages. After the
owner stops issuing opens to a cold source, `Device::open_file_count()` must also be
zero before its alias/device is removed. SDK patch
`0040-device-open-file-accounting.patch` counts native File construction/destruction,
including File objects still retained through guest handles or asynchronous work.
If old files keep every residency slot occupied, preparation applies backpressure;
it never evicts a live file or expands residency without a bound. At most one additional
host-prepared package is held by this runtime's demand path pending publication.

Play Current Song reads its captured stable catalog ID as a native Symbol through
`read_file("rbbz-play/song")`. Eviction cannot change the name selected by a press.
The current stream remains in control while successors prepare. At EOF with no ready
successor, a title track fills the gap; the prepared DLC sequence resumes as soon as
a successor is ready. This optional work never gates native content or graphics readiness.

The session foundation exposes shared forward/backward cursor methods; the later navigator
adds buttons, settings, the pause seam and the player. See the
[finished background-music navigator plan](../plans/finished/2026-10-22-background-music-navigator-plan.md).

Host validation covers lazy exhaustion past 50 songs, large merged packs, stable IDs,
shared backward/forward history, bounded warmth and explicit pins, failure quarantine,
request deduplication/pause/shutdown, real native File lifetimes and cache retirement,
and the dynamic Symbol/row contract. Actual boot timing, listening and eviction during
real playback remain manual acceptance; no game UI was driven for this change.

Validation: `cmake --build --preset win-amd64-release` succeeds; all nine targeted
music/menu/loading host tests pass after the final changes. The full host roster is
81/82, with the unchanged SDK-free `path_policy` nonexistent-descendant assertion
still failing. The SDK exact replay audit accepts 39 patches / 99 patched files,
with zero failures or unexpected edits. Documentation checking reports 57 repository
reference errors, none in the documents edited for this architecture. Evidence is
under ignored `out/rolling-music/` (`build-final.log`, `tests-final.log`,
`tests-full.log`, `sdk-patch-check.log`, `docs-check.json`).

Fallback recovery correction (2026-10-26): the owner's `rb_blitz_351.log` selected
a random DLC source in 235 ms, but its startup watchdog expired at 09:33:40.544.
It entered title-track fallback before cache loading began. Three successors were
ready by 09:33:52.723, yet the title's sample counter kept increasing through
09:35:40.656, so waiting for a counter rewind left DLC recovery stuck. Temporary
fallback now yields on the first poll with a ready successor; a failed pre-A startup
also starts the bounded preparation worker immediately. The deliberate fallback
after three consecutive failures retains its circuit breaker. Native failure logs
include stream/state/position for any remaining startup diagnosis. The new
`menu_music_runtime` host test exercises the production poll, stream retirement,
Load/Start handoff and audible Play Current Song selection with stand-in native
callees; no game UI is driven. Evidence is under `out/rolling-music-recovery/`.
The release rebuild and all nine targeted music/audio/menu tests pass; the runtime
regression contributes 48 checks, including pre-A preparation and the deliberate
failure circuit breaker. Actual listening after this correction remains a manual check.

Loading preservation follow-up (2026-10-26): `rb_blitz_362.log` showed the watchdog rejecting the
boot song before native Start (stream 0), then replacing an audible state-3 stream during graphics
preparation. Load now stages the song without arming its watchdog; Start arms it once. Native
loading polls and the active graphics-loading gate defer watchdog deadlines while preserving the
playing stream, sample position and selection. Cache-ready successors do not interrupt that song.
EOF and foreign-stream checks still run, and normal timeout recovery resumes after loading.
The production-hook and SDK-free regressions cover those boundaries; listening remains manual.
The same boot spent 531 ms mounting menu music, 45.794 s in native content enumeration and
5.663 s in graphics preparation. A separate full-library store index ran throughout startup;
its contribution to the long pre-menu interval needs profiling in a separate session. Detailed
timestamps and the unconfirmed double-audio report are in the
[navigator follow-up](../plans/finished/2026-10-22-background-music-navigator-plan.md) §13.


### 7. Navigator P1: surface and shared bridge (2026-10-26)

**Historical P1 record; superseded for runtime/binding behavior by §8 below.**

The default-on `interface.background_music_navigation` (id 53, carrier
`enhancements_background_music_nav`) is a restart-scoped boolean on General → Interface,
directly after Help & Options Labels. Background Music Play is always present as Interface
Mapping's fifth row, before Reset All, regardless of this switch (owner follow-up, 2026-10-26). That three-row page emits the one-shot
`file_exists` commands `rbbz-play/next`, `rbbz-play/previous`, `rbbz-play/pause`, without a settings
write or commit. On and Off retain identical authored trees, including the command page
and in-song twin. The boot log reports the effective switch and always-present page. The
switch's restart strategy remains until P2 proves live runtime gating; visibility requires none.

P1 installs no command handler or input interception: the existing `rbbz-play/` namespace rejects
these unimplemented commands safely. Music playback is unchanged. P2 connects commands to the
guest poll and proves pause; P3 implements the authorized footer/player, art resolution and mouse
routing. Runtime behavior and player visuals remain unproven.
The owner's subsequent request for visible effective bindings and an assignment flow is recorded
in P2's follow-up notes; P1's command-only rows do not implement that flow.

The SDK-free `src/input/menu_music_nav.*` latch samples disabled frames as well as enabled ones,
so held controls do not become synthetic presses when a gate opens. Its shared mailbox lives
with `NowPlayingBridge` (`src/hooks/menu_music_now_playing.*`): one accepted command remains busy
through execution until completion/cancellation, and a synchronized owned snapshot carries
transport/capabilities, stable track identity, art status and the latest command result. Posting
never calls the guest. Session epochs reject stale playback/art updates and request completions;
art results must match the current song and artwork key. P2 must still revalidate producer-specific
screen/capture gates on the guest thread.

`PoolEntry` now owns title/artist plus an optional package-relative artwork locator. Package
preparation copies parsed display metadata by exact stream/key; the catalog keeps it when a mount
is retired. P3 fills the locator from an exact package-relative match (see §9). `PlaySelection::Skipped`
clears automatic handoff grace for future presses while preserving an already captured press.
No second playlist or mounted-cache cursor is introduced.

Host tests and the build/manual validation record are in the
[navigator plan](../plans/finished/2026-10-22-background-music-navigator-plan.md) §11.


### 8. Navigator P2: guest execution and action bindings (2026-10-26)

The independent navigator now posts Next/Previous to the guest ShellMusic poll. Manual and
automatic advancement share `MusicSession`; a cold target is pinned/remounted without
moving the cursor or replacing audible metadata until handoff. Manual transitions clear
Play Current Song's old-song grace. Native title-only Load paths seed a three-track history
using the same cursor rules, native looping and neutral metadata. Filename buffers are
allocated before R9's early returns, so DLC Menu Songs is not a prerequisite.

The owner publishes coherent track/transport/capability snapshots and request results on
Load, poll, automatic/manual handoff, stop and shutdown. Applied means the replacement
stream was observed playing. Controls now draws effective bindings, with Assign / Reset child pages. Its `[menu_music]` profile table is independent of `[remap]`, reuses the
existing capture/countdown/rebase/save flow and publishes the live input snapshot. On/Off
retain the same authored tree and restart-scoped feature switch.

**Native pause/resume is enabled after owner acceptance:** StandardStream's native
pause call (`0x823E3FD8`) uses state 5, and Play (`0x823E4808`) resumes its channels.
The owner confirms the trial works; `rb_blitz_367.log` records a 7.285-second frozen
cursor and same-stream/same-position resume. A Play-hook ownership guard stops the
guest poll from undoing pause, and the watchdog is deferred while paused. The legacy
trial flag now only requests per-second diagnostics. The guest row shows bindings.
Host production-hook tests
cover navigation, cold targets, history/restart, automatic snapshots, title tracks and
cancellation; title-only live pause, media-device attribution, guest layout and physical-pad input remain
unproven in this earlier P2 record. P3 implements player/artwork (§9 below), and the owner
later accepted the full navigator. See the
[P2 record](../plans/finished/2026-10-22-background-music-navigator-plan.md) §§14 and 17 and
`out/_swap/music-nav/p2-runtime/` for verification.


**Rapid Previous follow-up (2026-10-26):** the owner's `rb_blitz_365.log` showed follow-up
Previous presses rejected as busy during approximately 2.3-second restart loads. Production
navigation now retains four waiting requests in addition to the active handoff, with ids/epochs
visible in the snapshot. A Previous made during loading retains step-back intent until execution,
so a delayed command cannot become another restart. The ordinary playing-state five-second
rule remains. Failed/cancelled requests and focus/scope/owner/session boundaries clear the burst.
Seven music host tests pass; the owner later accepted the full navigator. See the
[navigator record](../plans/finished/2026-10-22-background-music-navigator-plan.md) §15.


### 9. Navigator P3: player and local covers (2026-10-26)

The default-on switch gates an independent bottom-right host player. The owner confirmed the
player mostly works and the cover is visible live. Their latest review removes all LB/RB footer
additions: the retail A/B widgets retain their original actions and layout. No native spacing or
transform edit, footer locale token, availability query or host footer drawing remains.

An earlier owner run reported an RB Next crash. `rb_blitz_372.log` reaches the successor load
before a guest read of `0x4`, without a stack. A bounded first-chance trace captures near-null
guest faults, host frames and the active ShellMusic stage/context, without handling the exception.
The owner subsequently confirmed that the full navigator works, including RB Next. The exact
cause of the earlier fault was not independently isolated; see the
[navigator plan](../plans/finished/2026-10-22-background-music-navigator-plan.md) §22.

The player keeps the search interface’s bold/condensed fonts, but the owner’s second review
replaces its prominent popup treatment with a black translucent panel, dark-gray transport
buttons, and no border or glow. The bottom inset stays 24 reference pixels at 720p. It retains
square artwork, UTF-8-safe ellipsis, transport icons and hover/pressed color feedback, without
hover text. The independent dialog still draws with Settings Labels off.

The UI copies `NowPlayingSnapshot` and posts `kPlayer` commands with the observed session epoch.
It never reads guest memory or calls native transport. A pending indicator means accepted work;
the center icon changes only when the playback owner confirms playing/paused state. Results
remain in the shared snapshot/diagnostics; loading-state pause is disabled. Focus, modal/capture,
loading and menu/source gates hide the player and its hit regions. Mouse Support gates pointer
controls; pad/media bindings retain their own gates. Explicit event routing before native mouse
queueing is documented in [input-flow.md](input-flow.md).

Package preparation matches each complete stream path to the actual entry
`<stream-directory>/gen/<stream-basename>_keep.png_xbox`; a basename from another song directory
cannot match. Display strings and exact asset locators remain owned by the stable catalog after
mount retirement. Read-only probes verified this on a single-song package and the title’s merged
25-song package: all 26 covers decoded to 256×256 pixels, and every native File lease returned
from one to zero. Decoded samples were visually inspected outside the game.

`menu_music_artwork.cpp` runs one decode plus one latest waiting job, retains at most eight
256×256 RGBA covers (2 MiB), limits encoded reads to 2 MiB and input dimensions to 1024×1024.
The worker opens under the music-cache lock, copies bytes outside it while the File counter pins
the existing device, and releases the lease before decode. Artwork mounts no package and adds no
residency beyond the existing eight-device cap; a cold device defers the optional read to the
playback remount. The shared texture codec now decodes version-1 headers (signature at byte 1,
without v2’s ID) as well as v2. Invalid/missing covers and title tracks use a neutral disc fallback.
Epoch/song/art-key checks reject delayed results; hiding/disabling clears jobs, pixels and the
visible texture. No network/store lookup is involved.

SDK patch `0042-imgui-image-textures.patch` exposes `CreateImageTexture`,
`ReleaseImageTexture` and a drawer generation. Creation delegates to the same RGBA8
`ImmediateDrawer::CreateTexture` path used by the font atlas; `RenderDrawLists` uses the resulting
`ImmediateTexture*` as its texture ID. Upload/release runs from the dialog’s UI draw thread.
D3D12 and Vulkan already defer destroyed resources to their last-use submission fences. Drawer
replacement clears owned textures and increments the generation, preventing attachment/release
of obsolete IDs; the player uploads again from owned pixels. The bridge patch caps eight host
textures at 512×512, while this player retains one at 256×256. Static backend inspection, builds,
SDK patch replay and host artwork/click tests cover implementation; real GPU rendering, device
reset and lifecycle soak still await the owner’s test.
