# In-game settings — architecture (how the system is built)

The in-game settings system is the project's native replacement for Controller Settings' settings surface: a
menu inside the title that reads and writes the same preferences and, where the title owns the value, the
guest's own script world. This is the consolidated implementation record — the six per-package findings
documents merged into one account of how the pieces are built (guest bridge, panel/list API, storage
service, boot wiring, Ultimate provider adapter, hot apply + restart). Each part keeps its source's own
headings and section numbers, demoted one level, so references such as "§3" still resolve. Evidence tags
are the sources' own ([boot], [tree], [cited], [measured]).

## Sources

| Part | Original document | Package | Status |
| --- | --- | --- | --- |
| 1 | `docs/plans/in-game-settings-bridge-findings.md` | P1 | Implemented, host-tested |
| 2 | `docs/plans/in-game-settings-panel-api.md` | P1-b | Investigation complete, host-tested |
| 3 | `docs/plans/in-game-settings-storage.md` | P3 | Implemented, host-tested |
| 4 | `docs/plans/in-game-settings-boot-wiring.md` | P11a | Implemented, host-tested |
| 5 | `docs/plans/in-game-settings-ultimate-adapter.md` | P6 | Implemented, host-tested |
| 6 | `docs/plans/in-game-settings-hot-apply-restart.md` | P9 | Implemented, host-tested |

---

## Part 1 — The guest bridge and the transport decision (was `docs/plans/in-game-settings-bridge-findings.md`, P1)

Status: **P1 partial** — transport decided, host half of the channel implemented/built/tested, **native
screen not built and G1 not met**; the module was not hooked, so a boot behaved exactly as before. Then
**(2026-10-10, P1-b)** closed §9's resource-formatting gap and answered §10 steps 1–3 by measurement (a
panel name is a script object reached through `[include]` in `ui/gen/init.dtb`; the include name-to-file
rule is measured; an authored definition loads through a self-made patch archive — Part 2 §8, §10). And
**(2026-10-10/11, P1-c)** built both halves to different depths: the bridge runs end to end and the screen
is registered and reachable, but a screen of ours focusing a *panel of ours* crashes the guest (§12).

Labels: **[tree]** = working tree, `file:line`; **[cited]** = a build product or a byte measured locally;
**[assumed]** = reasoned, not measured; **[boot]** = measured from a real run. Read with `plan.md`
(requirements, gates, ownership) and `contracts.md` (frozen types and rules).

### 1. What this pass did, and what it did not

**Done.** Measured the guest builtin name table on the pinned retail image (§2.1) and the panel/list script
API the screen will use (Part 2, from retail `pause.dtb` + `splash.dtb`); verified the builtin calling
convention from the recompiled guest code (`r3` result cell, `r4` argument array, stride — §2.2); decided
the transport (§3); implemented, built and tested the host half (`src/hooks/settings_bridge.{h,cpp}` +
`tests/settings_bridge_tests.cpp` — exact namespace, bounded/checksummed frames, structured status,
idempotent request ids, §4); and implemented the authoring half of the compiled-DTA grammar
(`src/ui/dtb_author.{h,cpp}` + `tests/dtb_author_tests.cpp`, real-file case round-trips **six shipped
definitions byte for byte**).

**Not done (and why).** The authored screen (no panel definition loaded, no `Advanced Settings` row, no row
kind navigates — blocker narrowed to how a new panel name resolves to its file plus the row-widget binding,
§9). The no-op forwarded-hook comparison (needs the guest hook, which needs the screen, §10). The
`read_file` response channel (returning a string needs the guest DataNode/array build API, unverified,
§5). End-to-end runtime evidence (no guest call, no capture, §11). The allocator/refcount contract for
producing guest values (§5).

Build/test evidence **[cited]**:

```
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --target rb_blitz            # includes the new source
cmake --build --preset win-amd64-release --target rb_blitz_settings_bridge_tests
ctest --test-dir out/build/win-amd64-release -R settings_bridge       # 1/1 passed
ctest --test-dir out/build/win-amd64-release                          # 23/23 passed
```

### 2. The builtin surface (measured)

#### 2.1 The name table

The script builtins are interned from a NUL-separated string block in the image. Dumped from
`out/_swap/blitz_image.bin` (image base `0x82000000`, file offset = address − base) **[cited]**:

```
0x82080DD0  file_exists\0 write_string_to_file\0 write_file\0 read_file\0 run\0 --\0 ++\0
            reverse_interp\0 eval\0 ...
0x82081040  int\0 foreach\0 last_elem\0 ... func\0 sprint\0 sprintf\0 printf\0 next_name\0
            replace_object\0 File...\0
```

The four addresses the parent plan names are confirmed **exactly**: `file_exists` `0x82080DD0`, `write_file`
`0x82080DF4`, `read_file` `0x82080E00`, `run` `0x82080E0C`.

#### 2.2 The calling convention (verified from the recompiled bodies)

A builtin is an ordinary recompiled guest function `void f(PPCContext& ctx, uint8_t* base)`.

**`file_exists` = `sub_8241DFB0`** **[tree]** `generated/default/rb_blitz_recomp.63.cpp:15291`:

```
r11 = *(u32*)r4            ; r4 = argument array; arg 0 node pointer
r30 = r3                   ; r3 = result storage (kept)
r3  = r11 + 8              ; arg 0 payload starts 8 bytes into the node
r3  = sub_823F9538(r3)     ; node -> string (the path)
r3  = sub_8236D830(r3, 0)  ; the engine's own file-exists test -> boolean
*(u32*)(r30+4) = 0         ; result cell: type field = 0 (int/bool)
r3  = r30                  ; the function also returns the cell pointer
*(u32*)(r30+0) = r3 & 0xFF ; result cell: value field = the boolean
```

**`write_file` = `sub_8241DE48`** **[tree]** `generated/default/rb_blitz_recomp.55.cpp:14926`:

```
r11 = *(u32*)r4
r3  = r11 + 16            ; arg 1 payload -> the data to write
r3  = sub_823F9630(r3)    ; node -> the data value
r11 = *(u32*)r4; r3 = r11 + 8; r4 = the argument array
r3  = sub_823F9538(r3, r4) ; arg 0 payload -> the path
sub_824173A8(path, data, 0); the write
*(u32*)(r30+0) = 0; *(u32*)(r30+4) = 0   ; result cell {0, 0}
r3 = r30
```

Frozen consequences **[cited]**: (1) **`r3` is a hidden 8-byte result cell** `{u32 value, u32 type}`, owned
by the *caller* (its stack); the function returns the same pointer so a caller can chain. (2) **`r4` points
at the argument array**: `*(u32*)r4` is the first argument's node pointer, argument *i* at `node + 8 + 8*i`;
a string argument is read with `sub_823F9538`. (3) **Writing the result only into `r3` corrupts the ABI** —
the caller reads the *cell*, as the parent plan §2.3 warned, so any hook answering a builtin must write the
cell. (4) The engine's own existence test is **`sub_8236D830(string, 0)`**; the file-to-array loader is
**`sub_824193A8`** (as the plan says), reached from `run`.

#### 2.3 The registrar picture — partially identified, and that is enough for the transport

The parent plan names `sub_82420658` as "the initializer". Measured: it is **a** registrar, but not the one
for the file builtins — the names it interns come from the *second* block (`0x82081080`, `0x8208108C`, … =
`func`, `sprint`, `printf`, …) and the callbacks it stores are in the `0x8241F4xx` range **[tree]**
`generated/default/rb_blitz_recomp.36.cpp:15754`. The file builtins' names are referenced from `0x82070DD0`
(a second copy of the table — `lis r11,-32249` + `addi 3536`) inside `sub_823C32D0`, a name-resolving
helper, not a registrar **[tree]** `generated/default/rb_blitz_recomp.26.cpp:13425`.

**Consequence.** The registrar that installs the file builtins is still unidentified — a gap only for
*adding our own builtin*; §3's transport does not need it (it rides on builtins the title already has).
Finding it is deferred to the pass that registers the screen's own script entry point (§10 step 3).

### 3. Transport decision

**The guest reaches the host through the title's own `file_exists` / `write_file` builtins, by hooking the
recompiled guest functions and answering only paths in an exact project namespace.**

| Option | Verdict |
| --- | --- |
| **Guest builtin hook (chosen)** | No new service, no network, no new process. Ordinary recompiled guest functions — the pattern already proven three times (`ultimate.cpp` `sub_821D0A18`/`sub_8236C108`; `shell_music.cpp` `sub_82757F50`/`sub_827586D8`; `content_progress.cpp` `sub_82759308` …). Adds **no `Nt*` hook definition**, so the hooks owned by `menu_filter.cpp`/`dlc_refresh.cpp` keep their single strong definitions. |
| A second `Nt*` file hook | Rejected: duplicates a strong symbol (`NtCreateFile`/`NtOpenFile`/`NtQueryFullAttributesFile` = dlc_refresh's; `NtReadFile`/`NtReadFileScatter` = menu_filter's) or bypasses one. Forbidden by the parent plan and contracts. |
| Generalising the DLC sentinel into a command parser | Rejected outright (parent plan §5.3). The DLC refresh sentinel is **substring**-matched on purpose; a settings command must be **exact**-namespace and structured. |
| Full `read_file` response channel in v1 | Deferred: returning a string needs a guest DataNode/array, and the allocator/refcount contract is unverified (§5). The safe subset suffices: `write_file` (no result to build) and `file_exists` (a boolean). |

**Safe subset [cited]:** **Request** = `write_file("rbbz-settings/<hex frame>", <ignored>)` — the builtin's
own result is `{0, 0}`, nothing to construct or leak (or `file_exists` with the frame in the path, for a
request needing no data argument; both carry the same frame). **Acknowledgement** =
`file_exists("rbbz-settings/<hex response>")` — a boolean, trivially safe per the ABI above. A guest script
can build both strings, so no new guest-side facility is needed.

### 4. The channel (implemented)

`src/hooks/settings_bridge.{h,cpp}` **[tree]**, SDK-free so the rules are testable without a boot.

| Element | Value |
| --- | --- |
| Namespace | `rbbz-settings/` — matched **exactly** (`IsNamespacedPath` requires the whole prefix + something after it; `songcache:/rbbz-settings/x` and `rbbz-settings-x/` are **not** ours) |
| Frame | `magic "RBZ1"` \| `u16 version` \| `u16 op` \| `u64 request_id` \| `u32 setting_len` + bytes \| `u8 kind` + value \| `u32 crc32` — little-endian, hex-encoded |
| Ops | `kSnapshot`, `kBeginEdit`, `kSet`, `kCancelField`, `kCommit`, `kCancelEdit`, `kInvoke`, `kRequestRestart` (wire-stable) |
| Statuses | `kOk`, `kUnsupported`, `kBadVersion`, `kMalformed`, `kTooLarge`, `kUnknownOp`, `kStaleRequest`, `kConflict`, `kInvalid`, `kIoError` (wire-stable) |
| Bounds | frame ≤ 8 KiB, path ≤ 32 KiB, string ≤ 4 KiB, list ≤ 256 items — all checked **before** a copy |
| Integrity | CRC-32 over every byte before it; verified **before** any field is interpreted |
| Idempotency | `Dispatcher` remembers the last 16 `(request_id, op)` → response; a repeat returns the first answer and never reaches the sink |
| Message field | printable, ≤ 256 chars — never carries data or a newline into a log |

The sink interface (`RequestSink`) is the seam P3 fills with the real `Service`; `StubSink` is the
deterministic stand-in for the tests and the host-side round trip. `tests/settings_bridge_tests.cpp`
**[tree]** pins namespace exactness (including the non-matching substrings), every op × value-kind round
trip, malformed/truncated/non-hex/bad-checksum/oversized/too-long/too-many-items rejection, response round
trip with message sanitisation/truncation, version/op/service statuses, the idempotency window and eviction,
and a whole stub round trip. **1/1 passes; suite 23/23.**

### 5. Ownership and allocator API — what is known, what is missing

**Known [cited]:** the result cell is 8 bytes, caller-owned, `{value, type}` with type `0` = int/bool;
`file_exists`/`write_file` are safe to produce (their results are a boolean and a constant zero). Node
payloads for string arguments start at `node + 8`, stride 8, via `sub_823F9538`.

**Missing [assumed]:** producing a *string* or *array* result needs the guest DataNode/array construction
and its allocator/refcount rules. Neither is verified: `read_file`'s return path (`sub_824193A8`, then a
DataNode array, then `sub_823F92E0`) was read but not traced and no ghost/refcount contract exists. **Do not
build the `read_file` response channel until that is traced and tested with a disposable guest call.** This
is the largest ABI risk left in the transport.

### 6. Guest lifecycle entry point

- The **transport** needs no lifecycle hook: a recompiled-function hook installs at link time (the
  `extern "C" REX_FUNC(sub_…)` + `__imp__sub_…` forward pattern), so it is live from the first guest call.
- The **screen** does need one, unidentified. Known **[tree]**: configuration points are host-side —
  `OnPostLoadXexImage` (`src/rb_blitz_app.h:232`) calls `menu_filter::Configure`, `local_save::Configure`,
  `ultimate::Configure`, `dlc::Configure`, `content_progress::Configure`, `shell_music::Configure`;
  `OnPreLaunchModule` (`:266`) finalizes the cvar registry. The parent plan forbids installing the native
  menu by blindly calling script functions from `OnPostLoadXexImage`: it needs a guest point *after*
  script/object registration completes — not yet traced (§10 step 1).

### 7. Checksum implications

The transport **touches no file** — it answers builtin calls in host memory, so it has no obligation to the
ark index or content database. That is the opposite of the read-buffer DTB edits **[tree]**
`src/hooks/menu_filter.cpp` / `src/ui/menu_options.cpp`, which must keep a file's length and retarget the
`{name, sha1}` row the title checks; the bridge needs no digest correction.

If the **screen** route ends up replacing a retail DTB on its read (parent plan §5.2 fallback), it inherits
that obligation — byte-neutral edit + checksum-row retarget — which `menu_filter.cpp` already implements
and must own (single strong `Nt*` definition).

### 8. Original forwarding behaviour

For a recompiled guest function the original body is reached through the runtime-provided import **[tree]**
`src/hooks/ultimate.cpp:303`:

```cpp
extern "C" REX_FUNC(sub_8241DFB0) {           // file_exists
  if (!TryAnswerNamespacedPath(ctx, base)) {
    __imp__sub_8241DFB0(ctx, base);           // exactly once
  }
}
```

Keep: forward **exactly once** for every path that is not ours; never silently drop a call; the namespace
test must be exact, not the DLC sentinel's substring search. Hooks unwritten — §10 step 4.

### 9. The resource-formatting gap (why the screen is not built)

Exists **[tree]/[cited]**: the compiled-DTA grammar is fully known and already round-tripped byte-for-byte
by `src/ui/menu_options.cpp` (`splash.dtb` = 7,148 bytes retail, 8,808 in the payload ark) —
`../engine/main-menu.md` §2 and `../guides/assets.md`; the main menu's option array and its
`switch {start.lst selected_sym}` action table are documented, so a *row* and its *action* have a known
home.

Missing (the real gap): **How a panel name resolves to its definition file** — the script API is known (a
screen is `{new UIPanel <name> {file <milo>} {focus <list>.lst} {enter {do ... {<list>.lst set_data
<array>}}}}` with one `switch {<list>.lst selected_sym}` case per row symbol), but the rule behind
`{pause_panel load}` in `ui/gen/ui.dtb` is not, and that list cannot grow without a byte budget (the ark
index fixes the file's length; the byte-*neutral* machinery only pays for bytes freed inside the same file).
And **a row's widget type** — which list-row widget draws a boolean, horizontal select, submenu, compound
row is a property of the list resource (`ui/resource/lists/gen/list_*.milo_xbox`); binding a row symbol to a
widget type is untried. Ultimate's `list_mod_settings.milo_xbox` is **absent from the retail ark** — must not
be copied; retail candidates are `list_options_main.milo_xbox` and `list_splash.milo_xbox`.

So neither the DTB *file format* nor the *panel/list script API* is the gap any more: the gap is
**registering a new panel name** plus the row-widget binding.

### 10. The narrowed next experiment (what P1-b is)

1. **Find the guest lifecycle point** where UI/script registration is complete and the splash panel builds
   its list (parent plan §5.2 item 1). Evidence: a trace of the splash list being built and the call that
   builds it.
2. **Learn how a panel name resolves to its definition file** — the question that blocks a new screen.
   Evidence: a boot log naming the file the engine reads for a known panel name during `ui my_init`
   (`menu_filter.cpp` sees every read). The panel/list script API itself is now measured (Part 2).
3. **Author the definition** — capability exists: `src/ui/dtb_author.{h,cpp}` produces well-formed
   compiled DTA, its test round-trips six shipped definitions byte for byte.
4. **Write the two hooks** (`file_exists`, `write_file`) with §8's rule; prove the **no-op forwarded hook**
   first, then the namespaced answer. Evidence: a boot log line per handled request + a capture of the guest
   reading a response.
5. Only then the four row kinds, entry/cancel/back with keyboard and pad, and G1.

If step 2 yields no rule, the honest fallback is the plan's second route (a narrowly scoped loader
interception for project-owned virtual files), and the third route (byte-neutral bootstrap edit) only with
an independently proven byte budget.

### 11. Honest status against G1

| G1 requirement (contracts doc §10) | Status |
| --- | --- |
| Base-only runtime opens the independent native screen | **Not met** — no screen exists. |
| All four row kinds navigate | **Not met**. |
| One edit round-trips without leaks/crashes | **Not met end to end**; the host half round-trips against the stub (`TestEndToEndStubRoundTrip`). |
| Installed files unchanged | **Met by construction** for the transport (no file written); unverified for the screen. |
| Records transport / ownership / lifecycle / checksum / forwarding / formatting gap | **Met** (§3–§9), with the ownership gap open (§5). |

No boot was performed in this pass; treat every runtime claim above as still owed.

### 12. P1-c: the bridge runs, the screen registers, and a screen of ours that focuses a *panel of ours* crashes

Measured 2026-10-10/11 with the payload device and the project's UI driver. All **[boot]** unless marked.
Scratch `out/_swap/p1c/` (builder `stage.py` → emit modes of `rb_blitz_settings_screen_tests` →
`make_arm.py` per arm; driver `run.ps1 -Route <name>`); captures in `out/_swap/p1c/shots-*/`; one log per arm.

#### 12.1 What is built (host-tested, `ctest`)

| Piece | Where | Evidence |
| --- | --- | --- |
| Typed channel's frames, DTA text grammar, idempotency rule, in-memory store | `src/hooks/settings_bridge.{h,cpp}` | `ctest settings_bridge`: **302 checks** |
| Two guest builtins overridden, exactly-once forwarding, boot line | `src/hooks/settings_bridge_hooks.{h,cpp}`, wired in `src/rb_blitz_app.h` + root `CMakeLists.txt` | boot logs `settings_bridge: typed guest channel on (namespace 'rbbz-settings/', forward-only off)` |
| Authored definition (3 panels, 3 screens, four row kinds) + the two retail edits | `src/ui/settings_screen.{h,cpp}` | `ctest settings_screen`: **210 checks** |
| The definition compiler | `src/ui/dtb_author.*` | `ctest dtb_author`: 37 checks |

Two of the four `read/edit/ack` shapes are exercised by the definition itself: every request path the
authored screen writes is parsed by the bridge's own grammar in a test, so the seam cannot drift silently.

#### 12.2 What a boot proves — and what it does not

| Observation | Evidence |
| --- | --- |
| The authored definition loads: `[include] rbbz/advanced_settings.dta` added to `ui/gen/init.dtb` (2045 → 2079 bytes) | armA/armB, healthy boots |
| The new row enters the live main menu: retail draws **ADVANCED SETTINGS** between HELP & OPTIONS and DOWNLOAD CONTENT | `shots-boot2/01-menu.png`, read back by OCR |
| Its **label resolves through the locale**: text comes from our appended entry in a shadowed `ui/locale/eng/gen/locale_keep.dtb` (76726 → 77464 bytes, 14 keys) | same capture — the row is not a raw symbol |
| A **screen of ours opens** when focusing a retail panel: `{new UIScreen advanced_settings_screen {panels background_night_panel shell_music_panel menu_cache_panel options_panel} {focus options_panel} {back_screen splash_screen} {enter ...}}` — retail `options_screen`'s members verbatim, only the name changed | armF: **no violation**, `shots-armF/01-screen.png` |
| A screen focusing a **panel of ours** crashes: `Unhandled guest access violation: write of guest 0x00000200 on thread 0xF8000028` (a field write through a null pointer) | armA/armB/armG/armH/armI/armJ |
| Not the row symbols: the same panel carrying retail `options_panel`'s own rows (`tutorial controls calibration …`) crashes identically | armH |
| Not the list or the focus: a panel with `{file options_menu.milo}` and nothing else — no `{focus}`, no `{enter}` — crashes identically | armI |
| Not scene reuse: the same empty panel over a *different* retail scene (`pause.milo`) crashes identically | armJ |
| Loading the panel explicitly does not help: `{advanced_settings_panel load}` in `ui.dtb`'s init (and the sub-panels too, chained) still crashes | armE/armG |

#### 12.3 The two questions the bisection answered, and the two bugs it found

**Answer 1 — the delivery site decides whether a panel of ours is usable.** The same declarations work when
they ride **inside a retail record** and crash in **our own record reached by `[include]`**:

| Delivery of the same panel + screen | Result |
| --- | --- |
| Appended to retail `ui/options_menu/gen/options_menu.dtb` (2871 → 11422 bytes) | **opens**, no violation, `shots-armM/02-screen.png` shows all four rows |
| Our own `ui/rbbz/gen/advanced_settings.dtb` + `[include] rbbz/advanced_settings.dta` | `write of guest 0x00000200`, every arm |

Moving the include around inside `ui/gen/init.dtb` does not change it (`after
options_menu/options_menu.dta` still crashes), so it is not include *order*. The product route is therefore
the one parent plan §5.2 already preferred: **generate the patch record by extending an installed retail
record** rather than adding our own; our root `[include]` is dropped.

**Answer 2 — two real bugs in the authored definition, found by measurement.** (1) **The button table
nested a second switch**: the helper that produced the retail Start/Cancel cases returned a whole
`{switch action ...}`, spliced in as a *child* of the panel's switch, so its cases were **never evaluated**
(Start/Cancel did nothing while a file dump still looked correct). Fixed by returning *cases* into one
table; a regression test asserts each panel has exactly one `{switch ... action ...}` whose
`kAction_Start`/`kAction_Cancel` are direct children. (2) **The list's selected symbol lagged the row it
drew**: the first row was highlighted but Confirm ran the **second** row's action (`set
graphics.resolution`), the first's (`set interface.hide_hud`) never running — a list keeps its selected
symbol across a `set_data`. Fixed with retail's own idiom (read from `song_select`, `cheat`,
`recommendations`, `calibration`, not guessed): `{<list>.lst set_selected 0}` paired with every `set_data`,
pinned by a test.

#### 12.3b The round trip, verified by hand, and the Back defect

**Owner-run manual test (2026-10-11).** The owner booted the payload and toggled **Hide HUD** from the
controller — the same trip the host log records (`set` -> `commit` -> re-read returning the new value), so
the typed read/edit/ack round trip is confirmed by a human on the real pad.

**Back leaves towards the wrong screen** (owner-observed: from the screen the retail scene titles "HELP &
OPTIONS", Back lands on **credits**). Our definition asks `{ui goto_screen {advanced_settings_screen get
back_screen}}` and our screen declares `{back_screen options_screen}` — a retail screen the *shared list*
also belongs to. Likely cause: **`options.lst` is not ours** — our panel and retail `options_panel` load the
same scene (`options_menu.milo`) and drive the same list object, so they overwrite each other's rows and
selection, and the retail options screen then dispatches on a selection our 4-row list left behind (its own
last row is `credits`). This is the plan's "appearance parity still requires a spike" item arriving as a
*correctness* problem: this screen needs its **own list resource** (and probably its own scene). The owner
deferred that to a scope change, so it is recorded, not worked around.

#### 12.4 Reproducing

```powershell
python out\_swap\p1c\stage.py                       # read the retail records (never writes them)
out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe emit-screen out\_swap\p1c\def_retail.dtb retail
python out\_swap\p1c\make_arm.py armF-retailctl --defs def_retail.dtb --ui retail
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p1c\run.ps1 -Label armF -Route screen -Payload armF-retailctl
```

`emit-screen <out> <variant>` knows `full`, `bare`, `barefile`, `otherscene`, `retailrows`, `screenshare`
and `retail`; `make_arm.py --<slot> <file|retail|->` fills any of the five record slots and records what it
chose — which made the "this arm had no `ui.dtb` at all" mistake visible instead of silent.

---

## Part 2 — The native panel/list API and the screen-registration gap (was `docs/plans/in-game-settings-panel-api.md`, P1-b)

Status: **investigation result, one blocker resolved by measurement, no screen built yet.** Closes the
"engine's panel/list script API is untraced" item of Part 1 §9–§10 and the "how a panel name resolves to
its definition file" blocker of §5 here: §8 measures the rule, and §9 the delivery route that makes a new
screen reachable without editing shipped content. What is left for G1 is the authored definition and the
bridge, not a reachability question. Labels: **[cited]** = measured from a file in this checkout;
**[tree]** = working tree; **[boot]** = a real run (logs under `out/_swap/p1b/`). Read with `plan.md`
§3/§5.2, `contracts.md`, `../engine/main-menu.md` (compiled-DTA format and read-hook delivery).

### 1. A panel is authored DTA, and the rows are data

Decoded from the locally extracted definitions **[cited]** (`out/_swap/dtb_tree.py`,
`assets/game/360/ui/...`). The pause panel `ui/pause/gen/pause.dtb` (6,828 bytes), in file order:

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

The main menu's panel `ui/splash/gen/splash.dtb` (8,808 bytes in the payload ark) is the same shape:
`{new UIPanel splash_panel {file splash.milo} {state_on_enter start} {enter {...}} {exit {...}} ...}`, its
row list the array `../engine/main-menu.md` already documents. **A native screen is three pieces**, only the
first authored:

| Piece | What it is | Who supplies it |
| --- | --- | --- |
| Panel definition | a compiled DTA (`.dtb`) declaring `{new UIPanel <name> {file <milo>} {focus <list>} {enter ...}}` | **us** (the "authored definition") |
| Widget scene | the `.milo_xbox` the panel draws from (`pause.milo`, `splash.milo`) | retail, reusable |
| List resource | the `*.lst` the panel focuses/sets data on (`pause.lst`, `start.lst`) — declared in `ui/resource/lists/gen/list_*.milo_xbox` | retail, reusable |

### 2. The row lifecycle (measured)

| Step | DTA | Note |
| --- | --- | --- |
| Populate | `{<list>.lst set_data <array>}` | rows are **data**: an array of symbols, built in a `{do {var x {array (...)}}}` block. |
| Conditionally drop | `{remove_elem <array> <symbol>}` | already used by both panels; the title tolerates a name that is not there. |
| Read selection | `{switch {<list>.lst selected_sym} {<row-symbol> {...}} ...}` | one `switch` case per row **name** — identity is the symbol, not an index. |
| Focus | `{focus <list>.lst}`, `{<this> update_focus {get_focus}}`, `{get_focus}` → `focus_component` | the engine owns scroll/focus. |
| Navigate | `{ui goto_screen <screen_name>}` | e.g. `{splash_leaderboard {ui goto_screen career_leaderboard_screen}}`. |

**Consequence.** A row is a symbol in an array and its action a case keyed by that symbol — adding a row and
its action is a *data* change, exactly what `HideRows`/`RenameLabel` already do in reverse for `splash.dtb`.

### 3. Where screens and panels are loaded

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

So the boot path is: `ui.dtb`'s `init` **loads each panel by name**, sets `first_screen`, calls
`{ui goto_screen}`. Panels are objects loaded by name; the *screen* is reached through `goto_screen`.
`ui/gen/init.dtb` is only `include`s, and `ui/gen/ui_objects.dtb` declares panel **types and properties**
(`PanelDir` → `types` → `options_panel`, `loading_panel`, …) — a script-type schema, **not** a name → file
table. Engine method names interned in the image **[cited]** (`out/_swap/blitz_image.bin`, offset = address
− 0x82000000):

| Method | Address |
| --- | --- |
| `goto_screen` | `0x8209BDA8` |
| `set_data` | `0x8209F52C` |
| `selected_sym` | `0x8209F584` |

The DTA method-name dispatcher that interns them is at `generated/default/rb_blitz_recomp.39.cpp` ~line
18980 **[tree]** — one function comparing the requested symbol against a chain of interned names, each
leading to its handler. That is the **single best hook target** for observing "who sets the main-menu list,
with what".

### 4. What this unlocks

- The authored definition's *shape* is known precisely (§1), so it can be written today. The capability
  exists: `src/ui/dtb_author.{h,cpp}` produces well-formed compiled DTA from a node tree, and its test
  round-trips **six shipped definitions byte for byte** (`splash.dtb`, `pause.dtb`, `ui.dtb`,
  `ui_objects.dtb`, `global.dtb`, `exit_game_confirmation.dtb`).
- The four requested row kinds are **row-widget** choices, not script ones: a boolean, a horizontal select,
  a submenu entry and a compound row are drawn by the list resource's row type; `plan.md` §3 names the
  retail references (Ultimate's compound row is the model; HUD a submenu; Gems a horizontal select).
  Binding a row symbol to a widget type is the remaining unknown *inside* this area and belongs with the
  first attempt at a screen.

### 5. The gap, stated exactly

Both unknowns are about **reachability**, not the screen's contents: (1) **How a panel name resolves to its
definition file** — `{pause_panel load}` finds `ui/pause/gen/pause.dtb`, but no shipped table maps the two
and neither `ui.dtb` nor `ui_objects.dtb` carries one; until the rule is known a new panel name cannot be
given a file. (2) **Reaching a screen we add** — the `load` list lives *inside* `ui/gen/ui.dtb`, and the ark
index fixes that file's length, so adding `{advanced_settings_panel load}` is a length change; the
byte-neutral machinery only pays for bytes it first frees *inside the same file* (`menu_options.cpp`: rows →
an unused `#ifdef` macro name), and `ui.dtb` is 2,221 body bytes with no removable rows to spend.

### 6. The narrowed next experiments (in order)

1. **Trace the load path.** Hook the read path resolving `pause_panel`'s definition (`menu_filter.cpp` owns
   `NtReadFile`; a diagnostic logging each read's *path* during `ui my_init` answers it in one boot) —
   **[evidence: a boot log naming the file read for a known panel name]**.
2. **If the rule is a convention** (e.g. `ui/<group>/gen/<file>.dtb`), a name we choose may resolve to a path
   we can serve from the read hook as a **project-owned virtual file** — parent plan §5.2 fallback — with no
   edit to any shipped file.
3. **If the load list must be extended**, do it byte-neutrally: `ui.dtb`'s `init` is an array of commands and
   the same `#ifdef`-name donor trick `HideRows` uses can pay if a donor exists; otherwise this route needs a
   bootstrap byte budget that parent plan §5.2 requires be independently proven.
4. Only then: the four row kinds, entry/cancel/back with keyboard and pad, and G1.

### 7. Honest status

| Item | State |
| --- | --- |
| Panel/list script API | **Known** (§1–§3), measured from shipped files |
| Authored definition capability | **Exists and verified** — `src/ui/dtb_author.*`, six real-file round trips, `ctest dtb_author` |
| How a panel name resolves to a file | **Answered** (§8) — a script object; the file is a `[include]`, rule measured |
| Reaching a screen we add | **Answered** (§8.2) — the patch layer takes new records and a longer `init.dtb`; no byte budget |
| An authored definition loaded by the engine | **Done** (§8.4) — two healthy boots with a new include and record |
| A screen on screen | **Not built**; seven boots in §8, none drew a new screen |
| Row-widget binding | **Untried** — the last unmeasured piece (§4, §8.3) |

G1 remains unmet; the *reason* changed — no longer "we do not know how a screen is written" (we do, §1–§2)
but "we do not know how the engine finds the file for a new panel name, and the shipped panel list cannot
grow without a byte budget" (§5).

### 8. The resolution rule, measured [boot]

**A panel name never resolves to a file at all.** `{pause_panel load}` is a *method call on a script
object*, and the object exists because `ui/gen/init.dtb` **includes** the file that defines it. So the only
rule to learn was the one behind `[include]`.

**Rule [boot].** For an include of `<dir>/<name>.dta` executed from a script at `<root>/gen/<file>.dtb`, the
engine reads **`<root>/<dir>/gen/<name>.dtb`**; an include with no directory keeps the including script's
own directory (`<root>/gen/<name>.dtb`), and `..` resolves against `<root>`. `<root>` = the directory
containing `gen/`, i.e. `dirname(dirname(including file))`.

Retail correlations that fit (all of `ui/gen/init.dtb`):

| Include | File | Root arithmetic |
| --- | --- | --- |
| `global.dta` | `ui/gen/global.dtb` | no dir → the including script's own dir |
| `pause/pause.dta` | `ui/pause/gen/pause.dtb` | root `ui` + `pause` |
| `store/store_info.dta` | `ui/store/gen/store_info.dtb` | root `ui` + `store` |
| `../config/cheats_funcs.dta` | `config/gen/cheats_funcs.dtb` | root `ui` + `..` → `config` |
| `../ulti/mod_settings.dta` (the mod) | `ulti/gen/mod_settings.dtb` | root `ui` + `..` → `ulti` |
| `run ulti/ulti_init.dta` (the mod) | `ulti/gen/ulti_init.dtb` | same rule for `run` |

#### 8.1 The experiment that proved it

A v6 patch archive was built by hand from the retail ark (`out/_swap/p1b/make_payload.py`, reusing
`scripts/hmx_ark.py`'s writer) and mounted as the payload (`--ultimate_mode=2 --ultimate_payload_root=<arm>`).
Each arm differs in one record; all five boots used the same disposable user root, `--log_level=debug` and
the project's UI driver.

| Arm | The patch archive holds | Result |
| --- | --- | --- |
| `arkonly` | `ui/gen/init.dtb` = the retail bytes, byte-identical | **healthy**, 5.18 MB log, 0 violations |
| `shadow` | `ui/demo/gen/demo.dtb` = 64 bytes of `0xAB` | **crashes** early (18 KB log): the record shadowed the base one |
| `probemiss` | `init.dtb` with the 13-byte include `demo/demo.dta` renamed to `p1b/demos.dta`, no target anywhere | **crashes** identically: the include *is* executed, and a missing definition is fatal |
| `probe` | the rename plus `p1b/gen/probe.dtb` = garbage | **crashes** (sizes same as `probemiss`) |
| `relocate` | the rename plus **`ui/p1b/gen/demos.dtb`** = the real `ui/demo/gen/demo.dtb` bytes | **healthy**, 5.20 MB log, 0 violations, main menu reached |

Read together: `arkonly` proves the hand-built archive is well formed; `shadow` proves the patch layer
replaces a base record **by name**; `probemiss` proves the guest executes the patch copy of
`ui/gen/init.dtb` and runs its `include`; `relocate` proves the name-to-path rule and that a **brand-new
content record** loads through it (it reaches the menu — `shell_music: R9 off; ...`, the R5 row line —
instead of dying in the script load).

#### 8.2 Why the byte budget stops mattering [boot]

The archive carries its own index, so a patch record's length is its own. The Ultimate payload proves the
same: its `ui/gen/init.dtb` is **2,216 bytes** against retail's **2,045** (decoded `root_arity` 68 vs 62 —
six extra nodes: two `ulti` autoruns, two `ulti` includes, two `pers_tex`). Nothing is paid for; the file
grew. So the two routes §5 called unreachable return without a byte-neutral edit: (1) **Extend `init.dtb`**
in a patch record and add our own include (no donor, no freed bytes); (2) **Add our definition as a new
record**, e.g. `ui/rbbz/gen/advanced_settings.dtb`, and let that include name it.

#### 8.3 What this does not settle

- The row-widget binding of §4 is still untried — now the *only* unmeasured piece of the screen.
- Where the patch archive comes from in the product is an implementation choice: generate one at boot from
  the game's own records (the mirror of `scripts/hmx_ark.py` plus the DTB cipher `src/ui/menu_options.*`
  implements) or ship a tiny one. **No retail bytes are shipped in either case** — record contents are read
  from the installed ark, only the edits are ours.
- The `kPatchDiskError`/`kPatchContentDevice` edits and `--ultimate_mode` are how a *payload* is mounted
  today (`src/hooks/ultimate.cpp`); a product route not spelled "Ultimate" should mount the same device from
  the settings bootstrap instead of reusing that switch.
- Nothing here touched, wrote to, or distributed any retail or Ultimate file: the archives live under
  `out/_swap/p1b/` and are rebuilt by the script that made them.

#### 8.4 An authored definition is loaded [boot]

Parent plan §5.2 item 2 asks for "one authored minimal definition through a recognized virtual resource or
verified engine loader". Demonstrated with no byte budget and no retail file modified on disk:

| Arm | The patch archive holds | Result |
| --- | --- | --- |
| `authoredmin` | `init.dtb` with a **real include added** (`[include] rbbz/min.dta`) + `ui/rbbz/gen/min.dtb` = 41 bytes compiled by `src/ui/dtb_author.cpp` (one `[define]` root) | **healthy**, 4.97 MB log |
| `authored` | the same plus `ui/rbbz/gen/advanced_settings.dtb` = 373 bytes of the `PanelFixture` — `{new UIPanel advanced_settings_panel {file pause.milo} {focus settings.lst} {enter {...}}}` | **healthy**, 4.96 MB log |

`emit-init` reports `2045 -> 2079 bytes, 62 -> 63 roots`, and the patch index carries the new length; the
include lands after the file's own `#ifdef UI_INIT_DTA` guard (§8.3). A *missing* target crashes the boot
(`probemiss`) and a garbage target crashes it (`probe`), so a healthy boot means the record **was found,
read and parsed** as compiled DTA — the two new roots are loaded by the retail engine.

#### 8.5 The authoring tool grows an emit mode [tree]

```
rb_blitz_dtb_author_tests emit-panel <out.dtb>                  # the UIPanel fixture
rb_blitz_dtb_author_tests emit-min   <out.dtb>                  # one [define] root
rb_blitz_dtb_author_tests emit-init  <in.dtb> <out.dtb> <name>  # append [include] <name>
```

Unit tests unchanged and still pass (`ctest -R dtb_author`).

### 9. Evidence index [boot]

- `out/_swap/p1b/make_payload.py` — reads the retail records, applies the byte-neutral include rename, writes the seven payloads
- `out/_swap/p1b/run.ps1` — boots one arm against a disposable root and prints the interesting log lines
- `out/_swap/p1b/{arkonly,shadow,probemiss,probe,relocate,authoredmin,authored}.log` — the seven boots
- `out/_swap/p1b/init_base.dtb`, `init_probe.dtb`, `init_add_*.dtb` — the retail record, the renamed one, the two with an include added
- `out/_swap/p1b/{advanced_settings,min}.dtb` — what `dtb_author` emitted

### 10. What is left for G1

| Step | State |
| --- | --- |
| 1. The panel/list script API | done (§1–§3) |
| 2. An authored definition loads | **done** (§8.4) |
| 3. Register the screen: `{advanced_settings_panel load}` in `ui/gen/ui.dtb` and the row + `{ui goto_screen}` case in `ui/splash/gen/splash.dtb` | not started; both are now ordinary patch records, so the row can *grow* instead of being paid for out of freed bytes |
| 4. The row-widget binding (which `list_*.milo_xbox` row type draws each of the four kinds) | not started — the last unknown |
| 5. The bridge hooks (`file_exists`/`write_file`, forwarding exactly once) and the typed round trip | host half done, hooks unwritten (Part 1 §4, §8) |

---

## Part 3 — Storage, migration and service (was `docs/plans/in-game-settings-storage.md`, P3)

Status: **P3 complete** as a host-tested lane. `src/settings/{model,schema,store,migration,paths,service}.{h,cpp}`
materialize the frozen G0 interfaces and pass 205 host checks across three new ctest tests. **No production
code was wired into boot** — integration steps are in §6; the coordinator owns `src/rb_blitz_app.h`,
`src/main.cpp` and `RBBLITZ_SOURCES`. No cutover, Controller Settings change, installer change or UI. Read order:
`contracts.md` → this part → `plan.md`. Labels: **measured** = executed this pass; **frozen** = G0 decision;
**P3 addition** = a delta to a frozen shape, recorded for coordinator review.

### 1. Baseline and build record (measured, 2026-10-10)

Baseline `1e40765`, branch `p2-exact-1080p` (P1/P2 share this worktree). New files
`src/settings/{model,schema,store,paths,migration,cvars,service}.{h,cpp}`; new tests
`tests/settings_{store,migration,service}_tests.cpp`; the only shared file touched is the root
`CMakeLists.txt` (three `add_executable`/`add_test` blocks inside `BUILD_TESTING`). **26/26 ctest passed**
(3.27 s) — store 60 + migration 61 + service 84 = **205 checks**. The three targets are SDK-free and compile
from `src/` plus `src/profile/profile.cpp` and `src/profile/profile_path.cpp`; nothing in `src/settings/`
includes `rex/*`, `<windows.h>` or SDL, so no boot is required.

### 2. What each file is (frozen names materialized)

| File | Contents |
| --- | --- |
| `model.h/.cpp` | §5.1–§5.3/§5.8 verbatim: `Category`, `SettingId` (unchanged, no new ids), `ValueKind`, `Value`, `WidgetKind`, `Provider`, `Availability`, `Choice`, `Descriptor`, `Source`, `Origin`, `FieldState`, `DisplayMode`, `RendererCapability`, `ProviderState`, `CapabilitySnapshot`, `kSnapshotSchemaVersion`, `Snapshot`, `CommitStatus`, `CommitResult`, `ValidationResult`, `RestartStatus`, `RestartResult`, `ActionResult`, plus `SettingIdToken`, `StorageKey`, `ValueEquals`, `ValueToString`, `IsAction`, token tables |
| `schema.h/.cpp` | Row table in parent-plan §3 presentation order (target, DLC group, achievements group, graphics, letterbox, HUD group, hide-items group, gameplay), each row's storage key + submenu group; `BuildDescriptors(caps)` (resolves `renderers`/`resolutions`/`processor_count`/`menu_rows`, sets provider-row availability); `ParseTokenToValue`, `LabelForToken`, `StorageKeyFor` |
| `store.h/.cpp` | `toml::Document` (comment/unknown-preserving line editor) + typed `Document`, `StorePaths`, `DocumentLoad`, `SaveRequest`, `SaveOutcome`, `Store`, `ReadFileText`, `WriteFileAtomic` |
| `paths.h/.cpp` | §5.5 verbatim (`PathEntry`, `PathFileResult`, `ResolvePathFile`, `LoadPathFile`, `SavePathFile`) plus `ResolvePathValue`, `SettingsFileForProfile` (§4.1 sibling mapping), `ResolveSettingsFile` (adapts `ResolveProfilePath`) |
| `migration.h/.cpp` | §5.6 verbatim (`MigrationStatus`, `MigrationStats`, `MigrationResult`, 3-argument `Migrate`) plus a `MigrationOptions` overload |
| `cvars.h/.cpp` | §8's `kControllerCvarAllowlist`, `TargetToCvars`, `AntiAliasingToCvars` (§7.1), `FilteringToCvarValue` (§3.2), `CvarForSetting`, `ProviderCvarFor`, `SourceRank`, `OverrideExplanation` |
| `service.h/.cpp` | §5.7's `ProviderReadiness`/`ProviderSaveResult`, the `ProviderSink`/`Reconciler` seams, `Service` with the eight frozen methods verbatim plus `Boot`, `SetCapabilities`, `SetReconciler`, `SetProviderSink`, `SetProviderValues`, `SetOverrides`, `SetMenuRows`, `DescriptorFor`, `RequestedValue`, `EffectiveValue`, `EffectiveSource`, `IsOverridden`, `revision`, `capability_revision` |

### 3. P3 additions to frozen shapes (coordinator review)

Four deltas; none changes a frozen id, token or method signature. (1) **Submenu containers carry no
`SettingId`** — §5.1's enum has ids for leaf rows but not for *Downloadable Content*, *Achievements &
Unlocks*, *HUD*, *Hide Items* or *Plots & Skyboxes*; rather than invent ids, `SchemaRow::group` (a P3-only
field on the P3-only `SchemaRow`, not on the frozen `Descriptor`) names the container;
`Descriptor::submenu_parent` is still used where the frozen shape intends it (`kHideMainMenu` →
`kHiddenMainMenu`). (2) **`choices_source` gains two tokens** — §5.1 froze `"" | "renderers" |
"resolutions" | "provider"`; the DLC scan-thread ceiling depends on the machine (`"processor_count"`, §3.1)
and the Hide Main Menu row list is P10's seam (`"menu_rows"`), both resolved in `BuildDescriptors` /
`Service::SetMenuRows` (an unknown source leaves the static choices alone). (3) **`settings.toml` gains
`[migration] imported = true`** — §4.1 says the file holds a "migration version" but §5.4's illustrative
shape does not spell the key; the marker makes migration idempotent and distinguishes `kAlreadyCurrent`
from `kInterruptedResumed`. (4) **`Migrate` also has a `MigrationOptions` overload** — the 3-argument frozen
form delegates; `apply_paths=false` implements "match the old install identity before applying path fields"
(§4.3 item 3) without changing the frozen signature.

### 4. Behaviour implemented (frozen rules, measured by the tests)

**Write path (§4.4).** A save re-reads the destination, merges only the keys the caller changed, and refuses
a key whose on-disk value differs from the loaded base (`kConflict`, the key listed, *no write*). It writes a
flushed temporary sibling (`fflush` + `_commit` on Windows) and atomically replaces the destination
(`fs::rename` = `MoveFileExW(REPLACE_EXISTING)` on Windows), keeping the old bytes on failure. A save that
would render identical text never opens the file (mtime unchanged). Comments, unknown keys and unknown
tables survive byte for byte.

**Provenance (§7.2).** `[provenance]` is per field, keyed `'<section>.<key>'` (`'general.target'`,
`'graphics.resolution'`). A row is never inferred from value equality; only the rows a save names are
touched, so a save that does not manage provenance cannot erase a marker.

**Empty hidden-row selection (§7.3).** Absent key → the default offline trio applies. Present `[]` → hide
nothing. Present `[…]` → hide exactly these. The old free-form CSV cannot express "empty", so migration
imports nothing when the CSV is empty *or* equals the legacy default trio.

**Migration (§4.3).** Byte-identical backup (`<legacy>.pre-migration.bak`) before the first write; import
only missing fields; target imported (`common|demo|ultimate` preserved through `LaunchTargetName`);
`resolution_scale=n → [1280*n, 720*n]` (never `2 → 1920x1080`); filtering `−1/0/1/2/3/4/5 →
native|none|1x|2x|4x|8x|16x`; the AA pair (off+none → none, on+none → native, any FXAA → that FXAA choice
with the intentional native+FXAA loss recorded as a diagnostic); paths from `[settings]
user_data_root/dlc_root` then legacy `[launch] user_data_dir/dlc_dir`; retired keys (rename, pool size,
monitor, fullscreen, `present_*`, `audio_*`, `guide_button`, `keybind_guide`, `unlockall`) dropped;
controller rows preserved in `launcher.toml`. Cross-file interruption is recovered: settings written + paths
missing → `kInterruptedResumed`, paths finish on the next run without erasing the settings.

**A value equal to the compiled default is not written.** Keeps `settings.toml` minimal and keeps P4's
first-run automatic policy meaningful (origin, not presence, says "the user chose this"). A legacy row at
the *new* model's default imports as `default`; one that differs imports as `manual`, so an existing manual
choice is never reset on value equality. Measured: a default Controller Settings profile imports exactly two rows — the
target (`ultimate`) and the legacy `anisotropic_override = 3` (`4x`), the filtering the game actually ran.

**Service (§5.2/§6).** `requested`/`effective`/`pending` are three values. `BeginEdit` on a stale revision
returns 0; `Set` refuses a stale `capability_revision` (never clamps by index), a hidden or disabled row, an
action, and an overridden row. `Commit` advances `revision` only on a durable write; a combined
native+provider commit writes a `<settings>.commit-journal`, validates both portions before writing either,
and reports `kPartial` (never "nothing was saved") when only one part is durable. `RequestRestart` reports
`kStaleRevision`/`kAlreadyRequested`.

**Recovery is attempted inside every commit, and it cannot refuse the commit forever.** A commit first
tries to *finish* an interrupted one (that is what the journal is for). Since 2026-10-15 an attempt that
cannot succeed - the value cannot be spelled any more, the file moved under us, the provider is not there -
**parks** the journal (`<settings>.commit-journal.stale`, numbered if taken; the bytes are kept) and lets the
player's own save go ahead, recording the reason in `Service::recovery_error()` for the boot log. Before
that, a journal nobody could complete refused *every* later save on the installation, and the menu answered
"check that the settings folder is writable" for a failure that was not about the folder at all (measured on
an installed copy, 2026-10-14; records: part 5 §3/§6.2 and the plan's P7).

**Provider seam (§5.7/§6.2).** `ProviderSink` is P6's; with no sink a commit touching a provider row is
`kPartial` with `settings.provider_unavailable` — P3 never reports an Ultimate save as durable on its own
word. `kUnlockAll` is the one provider row persisted in `settings.toml` (`[ultimate_overrides] unlock_all`),
per §6.2.

**Reconciliation seam.** `Reconciler` is P4's. The default enforces only the row's kind and choice set; P4
supplies the graphics policy. Tested: a stored `dlc_scan_threads = 16` on a machine now exposing 4 logical
processors keeps the request 16, shows effective 4 with a correction reason — and the correction is not
written back.

### 5. Evidence

```powershell
# Repository root
cmake --preset win-amd64-release
cmake --build out/build/win-amd64-release --target rb_blitz_settings_store_tests `
      rb_blitz_settings_migration_tests rb_blitz_settings_service_tests
ctest --test-dir out/build/win-amd64-release --output-on-failure     # 26/26 pass
```

`ctest -N` reports **26** tests: the previous 22, P1's `settings_bridge`, and `settings_store`,
`settings_migration`, `settings_service`; the new tests also pass standalone under `clang++ -std=c++23
-Isrc` (60 + 61 + 84 checks). They pin: a no-op patch renders identical text; a changed key keeps its inline
comment and an unknown table survives; arrays and array-of-strings read back; a missing file is not an
error, no `schema_version` is malformed and untouched, an unknown version refused; provenance and the
absent-vs-empty list both round trip; a conflict writes nothing; a malformed destination and a read-only
destination keep their bytes; two disjoint writers both survive; the §4.1 sibling mapping and the
portable-marker/pointer settings location; the migration translations and interruption recovery; the
override/stale-capability/hidden-row refusals; the partial provider commit; the journal cleared on success;
the restart handshake; the cvar tables.

### 6. Boot wiring recommendations (coordinator; §4.2 ordering)

The service is host-side and needs no guest. Five insertion points, with the exact calls:

1. **`OnConfigurePaths`** — resolve identity and migrate before anything else reads settings.
   ```cpp
   profile::ProfilePathInputs inputs =
       profile::ProfilePathInputsFromEnvironment(rex::filesystem::GetExecutableFolder());
   inputs.command_line_value = REXCVAR_GET(launcher_profile);       // existing cvar
   settings::StorePaths sp;
   sp.settings_file  = settings::ResolveSettingsFile(inputs);
   sp.paths_file     = settings::ResolvePathFile(inputs.executable_dir);
   sp.legacy_profile = profile::ResolveProfilePath(inputs);
   const settings::PathFileResult paths = settings::LoadPathFile(sp.paths_file);   // may be missing
   std::string why;
   auto legacy = profile::ReadProfile(&why);               // existing helper
   settings::Service service(sp);
   settings::MigrationResult migration;
   service.Boot(legacy ? &*legacy : nullptr, paths.paths, &migration, legacy.has_value());
   ```
   Queue `migration.diagnostics` and any `LoadPathFile` error until logging exists.
2. **Before backend/window allocation** (the `OnPreSetup` path, not `OnFinalizePaths`) — after P2/P4 have
   probed, `service.SetCapabilities(caps)` then apply the effective graphics rows at the config rank. Use
   `TargetToCvars` (before `ultimate::Configure`), `AntiAliasingToCvars`, `FilteringToCvarValue`, and
   `CvarForSetting` for simple rows. `Set` with `SetFlagFromConfig`, never `SetFlagByName` at runtime rank.
3. **`OnFinalizePaths`** — recompute the roots from the final cvar sources so a pre-config snapshot cannot
   win: `ResolvePathValue(entry.dlc_root, sp.paths_file, <game_data_root>/dlc)` and the same for
   `user_data_root`.
4. **Around `ultimate::Configure`** — translate the effective target and license policy; after it returns,
   `service.SetProviderSink(<P6 sink>)` (which also pulls `ReadValues()`), and `service.SetProviderValues(...)`
   if the provider is read separately.
5. **Before `FinalizeInit`** — finish every init-only decision. Menu edits after that change
   persisted/pending state, not init-only cvars.

Also: `service.SetOverrides(...)` with the values `rb_blitz.toml` / environment / CLI won, so an overridden
row is shown disabled with `OverrideExplanation`. P8 narrows `ApplyProfileSettings` to
`settings::IsControllerCvar`. P9 consumes `Service::RequestRestart` after `CommitResult::saved_revision`.
The UI gets an immutable by-value `Snapshot` from `GetSnapshot()` and never holds a host reference across a
frame.

### 7. Handoff

| Package | What P3 supplies |
| --- | --- |
| P4 | `Reconciler` (`Validate`/`Reconcile`), `CapabilitySnapshot`, `BuildDescriptors`, `SourceRank`, `OverrideExplanation` |
| P5 | `Service` (snapshot + transactions), `schema::Rows` (order, groups, label keys), `SetMenuRows` |
| P6 | `ProviderSink`, `ProviderReadiness`, `ProviderSaveResult`, the `[ultimate_overrides]` path for `unlock_all`, `ProviderCvarFor` |
| P7 | `paths.h` (`PathEntry`, `SavePathFile`, `ResolvePathValue`) and the §4.1 sibling mapping |
| P8 | `kControllerCvarAllowlist`, `IsControllerCvar`, the migration's Guide drop, the preserved controller rows |
| P9 | `CommitResult::saved_revision`, `Service::RequestRestart`, the commit journal |
| P10 | `SettingId::kHiddenMainMenu` as a three-state list row and `SetMenuRows` |

### 8. Deliberately not done here

No boot wiring, no `RBBLITZ_SOURCES` entry, no `rb_blitz_app.h`/`main.cpp` change (coordinator). No real
provider save, no real graphics policy, no real UI (P6/P4/P5). No new cvar invented: the license masks pass
in as `LicensePolicy`, because §12 keeps the bit values out of P0. `[provenance]`, `[automatic]` and
`[migration]` tables are written only when they have content, so a fresh `settings.toml` is not seeded with
guessed machine defaults.

---

## Part 4 — Boot wiring (was `docs/plans/in-game-settings-boot-wiring.md`, P11a)

> **Current-state correction (2026-10-12):** this handoff records the earlier P11a snapshot. The graphics
> rows are now applied before presentation setup: renderer selection precedes GPU-plugin loading, and
> plugin-owned flags plus the guest resolution/draw-scale plan are applied in `OnPreSetup`. The current
> display query and measured 960×540 × 2 route are wired. See [status.md](status.md) for the live status.

Status: **P11a executed** (2026-10-11). The game boots on the real settings store: the path file and legacy
migration run, the rows the game reads at init are applied at the config rank, the guest channel's edits go
through P3's service into `settings.toml`, the provider's values reach the store, and the restart bootstrap
is in the executable's entry TU. 35/35 ctest green, one new target (`settings_bridge_service`, 81 checks),
five boot arms in `out/_swap/p11a/` plus one merged-menu arm. Baseline `d9e0127` plus this tree; nothing
committed. Read with Part 3 §6, Part 6 §6, `menu.md` §7, Part 5 §6.

### 1. What this package is

P3 left five insertion points to the coordinator; P9 left the channel's store swap and the executable's
bootstrap to the same place; P10 left the seam that fills the hide matrix from the live menu. P11a is those
three handovers and nothing else: no new product rule, no new row, no new file format.

| Piece | File | What it owns |
| --- | --- | --- |
| The boot wiring | `src/settings/boot.{h,cpp}` (new; the only SDK-using file in the pair) | The four insertion points, config-rank writes + read-back verification, the capability snapshot the boot can honestly build, and the process-wide store the hooks reach |
| The channel's real store | `src/settings/bridge_sink.{h,cpp}` (new, SDK-free) | P1's frames and one-bit answers, with native rows going through P3's `Service` and the provider's rows (and gate) kept locally |
| The seam that lets them swap | `src/hooks/settings_bridge.h` | `SinkQueries` (the read/ack half, separate from `RequestSink`) and `Dispatcher::SetSink` |
| The hook side | `src/hooks/settings_bridge_hooks.{h,cpp}` | `InstallSink`; provider seeding, write-through and live apply now go through whichever store is installed |
| The vocabulary | `src/settings/schema.{h,cpp}` | `IdForToken` moved here from P9's runtime applier (the SDK-free sink needs it; exactly one spelling) |
| The entry point | `src/main.cpp` | The restart bootstrap: a static object's constructor that waits for the parent's verified identity *before* the SDK's entry point runs |
| The app's own hooks | `src/rb_blitz_app.h` | Calls the boot at `OnConfigurePaths`, `OnPostInitLogging`, `OnPostLoadXexImage`, `OnPreLaunchModule` |

### 2. The one new seam: `SinkQueries`

P1's `RequestSink` carries a request *to* a store; the guest's read/acknowledgement are queries
`file_exists` answers with one bit, and until this pass the hook code held a `MemorySink` directly. The query
half is now its own interface and both stores implement it:

```text
SinkQueries:  Matches(request) | Acknowledged(id) | Get(token, out) | last_commit_changes()
```

`Dispatcher::SetSink` (new, inline) points the channel at another store without forgetting the idempotency
window, which belongs to the *guest session*, not to a store.

#### 2.1 Which rows go where, and why

| Token space | Example | Where its value lives | Why |
| --- | --- | --- | --- |
| A native row | `general.dlc_menu_songs`, `graphics.vsync`, `interface.hidden_main_menu`, `general.unlock_all` | The `Service` (transaction, `settings.toml`, `[provenance]`, journal) | They are *our* preferences and P3's store owns them |
| A provider row | `interface.hide_hud`, `gameplay.precision_mode`, `interface.letterbox` | The sink's local space | The payload's own file is their source of truth and P6's write-through persists them; routing them through the service too would mean both the service's commit *and* the write-through wrote the same values, and the seeding commit would rewrite the player's `ulti_settings.dta` every boot |
| The provider gate | `provider.active` | The sink's local space | A view of the adapter's readiness, not a preference |
| Anything else | `nonsense.row` | refused (`kInvalid`) | A stale screen must not stage a change that lands nowhere |

The classification is `IsLocalToken`, *not* a fallback: a token the schema describes with a native storage
key goes to the service; one the schema describes as the provider's, an action, or the gate stays here; an
unknown token is refused rather than accepted.

#### 2.2 What the sink adds on top of P1's stand-in

* **A real transaction.** `begin` opens one at the service's revision (or at the guest's own `i:<n>` base,
  which `BeginEdit` refuses when stale); a `set` without a `begin` opens one implicitly, so the text form a
  DTA script writes needs no session bookkeeping. `cancel-field` calls `Service::CancelField`; `cancel`
  calls `Service::Cancel`.
* **Validation.** A wrong-kind value is refused by the row's own descriptor before reaching the service,
  which then applies *its* rules: availability, overrides, capability revision, choice sets.
* **Honest commit status.** `kSaved`/`kNoChanges` → ok, `kConflict`/`kInvalid`/`kIoError` → the same, and
  `kPartial` → an error whose message names the state (plan §6.3 forbids a blanket "nothing was saved" when
  one part is durable).
* **P9's no-op rule, properly.** The service reports the rows the transaction *touched*; the sink compares
  each against the value it held before the session and reports only real moves, so a commit writing a value
  back unchanged cannot re-apply a hot row. The reported token is the spelling the *guest* used, so a log
  line and the hot-apply lookup see the screen's own name.
* **The restart op.** `restart/-/-` is recorded (`restart_requested`, `restart_request_id`) and answered ok;
  starting the child is the boot's job (that is where process identity and launch context live).

### 3. Boot order, with the reason for each position

| # | Hook | What happens | Why there |
| --- | --- | --- | --- |
| 1 | `OnConfigurePaths` | identity → `paths.toml` → legacy profile → migrate → load `settings.toml`; then the capability snapshot, then target/licence, renderer, the DLC and Interface rows (including `interface.settings_labels`, which the caption overlay reads on every draw) and the two path roots, at the config rank | Runs after the legacy profile's rows (which it migrates then outranks at equal rank) and *before* the SDK loads the game's own `rb_blitz.toml` — a later write at the same rank, so it still beats it (§4.2) |
| 2 | `OnPostInitLogging` | the store's boot lines: file + status, the migration, rows applied, rows an explicit source owns, the two open items | First point where a line goes anywhere (same reason Controller Settings' profile report lives here) |
| 3 | `OnPostLoadXexImage` (after `ultimate::Configure`, before `bridge::Configure`) | provider values reach the service; the GPU plugin's own cvars are written now that it registered them; the channel is pointed at the service | Provider state is known only after `ultimate::Configure`; the plugin's cvars (`vsync`, `anisotropic_override`, `native_2x_msaa`, `swap_post_effect`, `resolution_scale`) do not exist at step 1; and `bridge::Configure` *seeds* through the channel, so the store must be installed already |
| 4 | `OnPreLaunchModule` (before `FinalizeInit`) | read every applied cvar back; a row whose value did not stand is recorded as overridden | `SetFlagFromConfig` returning true does not prove the value won (§4.2): a later same-rank write — `rb_blitz.toml` — beats it, and a row the player cannot change must say so |

The path roots are the one thing the app finishes: `ConfigurePaths` returns them and `OnConfigurePaths`
puts them into its `PathConfig` (after the profile and the cvar re-read, so they replace the legacy path
fields, as §9.1 asks). No `OnFinalizePaths` override was needed — the SDK passes the hook-modified config to
it, so the app's values already survive.

### 4. What is applied, and what is deliberately not

| Row | At boot | Note |
| --- | --- | --- |
| Target | `ultimate_mode` + `license_mask`, as a pair | Licence masks are the game's own (`1` purchased, `0` trial); an explicit `--ultimate_mode` outranks the pair and the row is reported overridden (the merged-menu arm passes one) |
| Renderer | `gpu_backend` | Written before `OnPreSetup` reads it — the reason the renderer row is applied at step 1 |
| DLC group | `enhancements_dlc_cache`, `dlc_scan_threads`, `enhancements_menu_dlc_songs` | Read by the mod's own `Configure()` calls later in the same hook |
| Interface group | `enhancements_skip_offline_dialog`, `enhancements_hide_menu_options`, `enhancements_hidden_menu_options` | The hidden-menu list is written *including the explicitly empty list* — the product spelling §3.5 asks for, which a command line cannot express (§5.4) |
| V-Sync, Filtering | `vsync`, `anisotropic_override` | Written at step 3, because those cvars belong to `rexgpu-xenos.dll`; the deferred write is reported and verified like any other |
| Resolution | **not applied** | The exact-render route is P4's Variant B guest-height hook (not compiled in) and the plugin's `resolution_scale` cannot be set from a config-rank write (P2); the value is stored for the boot that has the hook |
| Anti-Aliasing | **not applied** | Both mechanisms are known (P2/P4) but the native path's *sample semantics* have never been verified on this device and §7.1 forbids shipping an unproved mapping; the value is stored |
| Provider rows | none at boot | Their values live in the payload's own file, read by its own loader before the first frame — exactly why every one is a `restart` row (§5.5) |
| `unlock_all` | none | P6's open item: its native `[ultimate_overrides]` write is still not made, and this pass did not pretend otherwise |

Two rules make the table safe: **a row nobody chose is not written** (`Source::kCompiledDefault` means
"never chosen", so the game's own default stands — this keeps an absent hidden-row selection from becoming
explicit, the defect class P10 named, and is why a fresh install writes no file at all); and **every write
is verified** (`FinishInit` reads each applied cvar back; a mismatch is logged as a warning and recorded
with `Service::SetOverrides`, so the UI can disable the row with an explanation instead of saving a value
that cannot take effect).

The capability snapshot the boot publishes is deliberately thin, and the log says so: logical processors
from the host, **no display enumeration** (P4's probe needs SDL on the UI thread at a proven lifecycle
point), the producible size set (guest 1280x720 x the offered scales), and one renderer — the single backend
this project ships and has run — with an honest filtering ceiling and an **empty** native-AA label. P4's
rules do the rest: an unproved label is not offered, and a missing display enumeration publishes the
producible set unchanged rather than erasing a request.

### 5. Evidence (2026-10-11)

Harness `out\_swap\p11a\run.ps1 -Label <name> [-Diagnostic] [-Reuse]`: boots `rb_blitz.exe` against a
disposable `--user_data_root` with a legacy Controller Settings profile, waits for the boot's last line (`N of M
applied value(s) stood`), stops the game, prints the lines that matter and dumps `settings.toml`.

#### 5.1 Migration and the config-rank writes

Arm `migrate2` (`out/_swap/p11a/migrate2/migrate2.log`): `migration done - 2 row(s) imported, 1 translated,
1 carried over`; the legacy profile was backed up byte-identically; `2 row(s) applied at the config rank`
(`ultimate_mode = 1`, `license_mask = 1`); `1 cvar(s) belong to the GPU plugin`; then
`anisotropic_override = 3 (applied once the GPU plugin registered it)` and `3 of 3 applied value(s) stood`.
`settings.toml` was created with `[general] target`, `[graphics] filtering = '4x'`, both provenances and
`[migration] imported = true`. The two imports are P3's measured outcome for a legacy profile (`target`,
from the profile's own structured field — default `ultimate`, what the retired Controller Settings meant by a default
profile — and `anisotropic_override = 3` as `4x`). The `[settings] target = "common"` row the arm writes is
*not* the structured field, so it is preserved verbatim as an unknown row ("import only missing fields,
preserve everything else").

#### 5.2 The guest channel writes through the real store

| Arm | Log | Observation |
| --- | --- | --- |
| `diagnostic` (`REX_SETTINGS_HOT_APPLY_DIAGNOSTIC=1`) | `out/_swap/p11a/diagnostic/diagnostic.log` | `settings_bridge: store the native settings service (settings.toml)`; `hot-apply diagnostic committed 'graphics.vsync' -> ok`; `settings_bridge: applied 'graphics.vsync' to the running game`; `hot_apply: vsync = false (was true), applied to the running game` — and **`settings.toml` gained `vsync = false` with `'graphics.vsync' = 'manual'`**. The commit went through the service: transaction, durable write, journal's rule, provenance |
| `readback` (a later boot on that file) | `out/_swap/p11a/readback/readback.log` | `settings '…' (loaded)`; `migration already current`; `vsync = false (applied once the GPU plugin registered it)`; `4 of 4 applied value(s) stood` — a value the *menu* wrote is consumed by the next boot |

#### 5.3 The merged screen reads the real store

P5b's harness (`out\_swap\p5b\run.ps1`) driven over a `settings.toml` written **before** the boot:

```toml
[interface]
hide_main_menu = true
hidden_main_menu = []          # the explicitly empty selection
```

`-Route basescroll` (Interface → Hide Items → Main Menu) then shows, in `out/_swap/p5b/p11a-store.log`:

```text
settings_boot:   enhancements_hide_menu_options = true
settings_boot:   enhancements_hidden_menu_options =
menu_filter: R5 on: the selection is empty; the master is on and nothing is hidden
settings_bridge: read 'interface.hide_main_menu' -> true
settings_bridge: read 'interface.hidden_main_menu.splash_exit' -> false
settings_boot: 3 of 3 applied value(s) stood; 1 row(s) are owned by an explicit source
```

Three things at once: the master row's state came from the *file* (not a compiled default), every row choice
on that page was answered by the service, and **P10's "not measured in a boot" item — the explicitly empty
selection — is now measured**, in the product spelling the store gives it. The warning is the CLI's
`--ultimate_mode=2` correctly outranking the stored target. Zero guest faults in every arm
(`violation`/`guest access`/`fatal`: no hits), and the installed retail/Ultimate records are untouched.

### 6. What this pass did not do

* **P4's two open items** (display probe, Variant B guest-height hook) and the AA sample verification: the
  Resolution and Anti-Aliasing rows are stored but not applied, and the boot says so. Switching them on
  means the probes and the `[[midasm_hook]]` entry, not a change here.
* **The menu-rows seam is wired but not filled.** `SettingsBoot::SetMenuRows` forwards to both tables
  (`schema::SetMenuRows`, `Service::SetMenuRows`) and `boot::SettingsBoot::Current()` is the handle a hook
  needs — but the *caller* must hold the splash record's bytes, which is `src/hooks/menu_filter.cpp`'s read
  buffer. `MenuInventory` is still uncalled at runtime.
* **The restart prompt and the Target soft restart** (Part 6 §6.1 / plan §8): the request path exists end to
  end (`ServiceSink` records the guest's `restart` op; P9's coordinator is ready) but nothing calls
  `RestartCoordinator::Request` yet, because the prompt that decides *when* is authored DTA work. The
  bootstrap half is done and measured by P9.
* **The provider's `unlock_all` native write** and **the achievements action**: P6's open items; the channel
  answers the action with `kUnsupported` rather than a cheerful lie.
* **Controller Settings rows' removal, the in-song merge and the cutover** — P8/P5b-r2/P11b.
* **A 50-package menu pool boot**, the in-song entry, and the trial target: unchanged open items.

### 7. Tests

```text
ctest --test-dir out/build/win-amd64-release --output-on-failure     # 35/35
```

New target `settings_bridge_service` (81 checks, `tests/settings_bridge_service_tests.cpp`), wholly against
a **real** `Service` over a temporary directory: the text form a DTA script writes and the framed form a
host caller writes both reach the same store; a commit is durable and acknowledged only when the service
wrote, and a replayed id does not reach it twice; a value written back unchanged is not reported (both the
"already the same on disk" and change-then-revert cases); the provider's own rows never appear in
`settings.toml`; the gate is local; an unknown setting, a wrong-kind value and an unknown op are refused;
cancelling restores the field and writes nothing; the restart op is recorded; the wire/model conversions
refuse a mismatched kind while a resolution round-trips as `1920x1080`. Also updated: `settings_hot_apply`
(token vocabulary moved, test still passes), `menu_compat` (unchanged), and the whole suite as a regression
check.

### 8. Reproduce

```powershell
cmake --preset win-amd64-release
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure            # 35/35

pwsh -NoProfile -File out\_swap\p11a\run.ps1 -Label migrate                 # migration + config-rank writes
pwsh -NoProfile -File out\_swap\p11a\run.ps1 -Label diagnostic -Diagnostic -Reuse
pwsh -NoProfile -File out\_swap\p11a\run.ps1 -Label readback -Reuse         # the value the menu wrote

# The merged screen reading the store needs P5b's payload and harness:
pwsh -NoProfile -File out\_swap\p5b\run.ps1 -Label p11a-store -Route basescroll
```

### 9. Handoff

| Package | What P11a supplies |
| --- | --- |
| P5b-r2 (in-song merge) | Nothing new to change, but it now runs with the store live: a screen edit writes `settings.toml` |
| P9-ui (restart prompt) | `ServiceSink::restart_requested()` / `restart_request_id()` is the guest's request; `Service` + P9's `RestartCoordinator` are reachable from the boot; the prompt decides when to call `Request` |
| P11b (cutover) | The store is the default source, so `profile_controller_rows_only` can finally be flipped; Controller Settings' `[settings]` rows other than the controller allowlist become inert preservation data |
| P10's follow-up | `SetMenuRows` + `Current()` are the seam; the caller is the menu filter, which holds the record bytes |
| P4's follow-up | `SettingsBoot::AfterLoadXexImage` is where a real display probe and the Variant B hook land; the two rows' "not applied" branches in `ApplyRow` are the switch |

---

## Part 5 — The Ultimate provider adapter (was `docs/plans/in-game-settings-ultimate-adapter.md`, P6)

Status: **executed** for the adapter, the exact-file redirect and the row wiring, with the open items in §6.
Evidence is host tests plus real boots against the pinned retail image with the owner's installed payload
(`game/ultimate`); §5 names the log and capture behind every "measured" line. Read with `plan.md` (§3 rows,
§5.5 strategy, §6 integration boundary) and P5b's handoff (`menu.md` §6, which froze the row identity this
package binds); `../guides/ultimate-compat.md` is the payload's own side.

### 1. What this package owns

| File | What it is |
| --- | --- |
| `src/settings/ultimate_adapter.{h,cpp}` | The whole provider model, SDK-free: the payload's ark index reader, installation inventory, the shape check over the provider's **own** compiled definitions, the frozen row bindings, the provider's settings-file reader/writer, the readiness decision and the boot's provider state |
| `tests/ultimate_adapter_tests.cpp` (ctest `ultimate_adapter`, `ultimate_adapter_payload`) | 786 checks: a hermetic half that authors a provider-shaped definition with the project's DTB builder and mutates it, and a real-installation half over `game/ultimate` |
| `src/fs/overlay_merge.{h}` + `src/fs/payload_overlay.{h,cpp}` | The **third side** of the payload overlay: a writable directory of the player's own, and the root-level records redirected into it |
| `src/hooks/ultimate.{h,cpp}` | Boot wiring: inspect the payload, publish the provider state, mount the redirect |
| `src/hooks/settings_bridge_hooks.cpp` | The channel's two provider ends: seed the provider's values into the store, write a commit through to the provider's own file |
| `out/_swap/p6/run.ps1` | The boot harness (takes the payload directory explicitly, unlike P5b's) |

Not in this package: the achievements action (§6.1), the restart flow (P9), the hide list and pool (P10),
the merged-menu/record-shadowing integration and packaging (P11), and the first-run graphics policy (P4/P11).

### 2. Why the payload's *shape* is the compatibility test, not its version

`../guides/ultimate-compat.md` §3 measures that the mod's `default.xex` is **byte-identical in every
release** — 1.0, 1.01, 2.0, 2.1 and 2.11 all ship sha256 `390e0ae0…` — so a version comparison proves
nothing: the release is entirely in the ark, and which rows a release carries is a property of the ark's
contents. The adapter therefore has two levels, as plan §6.1 asks:

* **the installation inventory** (`InspectInstallation`): the payload is a `.hdr` + `.ark` pair, and the six
  records this integration needs are looked up **by name in the ark index**, each named with the reason it is
  needed (`RecordAt`), so a partial payload is reported as missing exactly what is missing rather than as
  "something changed".
* **the shape check** (`CheckShape`): the provider's **own** `ulti/gen/mod_settings.dtb`,
  `mod_settings_rw.dtb`, `ulti_macros.dtb` and `ulti_init.dtb` are parsed with the project's DTB parser and
  checked for the objects, panels, rows, row kinds, persisted variable list, choice inventories,
  settings-file path and compiled defaults this build draws. A row naming a kind this build lacks, a gone
  panel, a renamed variable, a `vars` list that lost a name, or a moved settings path are all refused **by
  name**.

`ProviderReadiness` (P3's frozen §5.7 enum) is decided from the plan's three facts and nothing else: payload
present, active target, shape ok.

### 3. The rows, bound to the provider's own vocabulary

P5b froze the row identity (`<category>.<id>` tokens); this package binds each to the provider's variable,
kind and **application strategy** (§5.5). **All 17 rows are `restart`, with no measured evidence** (so no
evidence column is needed): `gameplay.precision_mode`←`freq_ms` (boolean), `gameplay.disable_random_wiggles`
←`disable_random_wiggles` (boolean), `gameplay.camera_angle`←`camera_angle` (slider, tokens
`default`/`high`/`low`), `gameplay.game_speed`←`speedmod` (slider, percent 100–150),
`gameplay.disable_dof`←`disable_dof` (boolean), `gameplay.disable_plots`←`disable_plots` (boolean,
provider `reboot`-tagged), `gameplay.forceplot`←`forceplot` (select over the payload's plot list),
`gameplay.which_skybox`←`which_skybox` (select over the payload's skybox list), `gameplay.gems`←`gems` (select,
payload inventory), `interface.letterbox`←`letterbox`, `interface.hide_hud`←`hide_hud`,
`interface.goldprogress`←`goldprogress`, `interface.progressbar`←`progressbar`,
`interface.show_timer`←`show_timer`, `interface.show_pb`←`show_pb`, `interface.show_steak`←`show_steak`,
`interface.input_viewer`←`input_viewer` (all boolean).

`TestsBindings` fails if any row ever claims `hot` with an empty evidence string, so "make this row live" is
a measurement, not an edit. **Nothing provider-side is hot today**: a provider row's value is read out of
the DTA variables the payload's loader filled *before the first frame*, so writing the file (what this
package does) cannot change the running game — only the next boot. `Hide HUD`'s P1 live effect was the
*stub* store's, not the provider's.

Where the choices come from (plan §3.4, "the provider's inventory is authoritative"):

| Variable | Where the adapter reads it | Measured on the owner's payload | Spelling in the provider's file |
| --- | --- | --- | --- |
| `gems` | the row's inline `(array …)` in `mod_visual_settings_panel` | `default, freq, amp2003, amp2016, monkey, ulti, x, bolt` (8) | the token itself |
| `which_skybox` | `mod_force_skybox_panel`'s flat `(data …)`, the screen's own `SELECT_MSG` storing a *position* | `default_skybox, no_skybox, nfs_mw2012` (3) | the **position** (`0` = `default_skybox`) |
| `forceplot` | `mod_force_plot_panel`'s flat `(data …)` | `off` + 16 plots (17) | the **position** (`0` = `off`) |

**The last column is the 2026-10-15 fix.** `forceplot` and `which_skybox` are the payload's
*boolean-with-submenu* pair: the row in `mod_visual_settings_panel` is a `uOnOFF` toggle (`os_force_plot`),
its choices live in a panel of its own, and that panel's `SELECT_MSG` stores the selected **position**
(`{set $which_skybox {$component selected_pos}}`). This build exposes the whole choice set as one select
over the payload's own tokens - the token is what the screen, the store and a request carry - so the row's
*codec* kind is `kSelect` while the payload's own row kind stays a boolean, and `RowBinding::position_value`
says that the provider's **file** holds the position. Both directions are one place each: `ConfigureProvider`
turns a file/default position into a token on the way in (dropping, with a note, a value that names no
position) and the provider sink's `Save` turns the token back into a position on the way out - against the
payload's inventory both times.

The half-finished version this replaced spelled the pair as booleans (`RowKind::kBool` in `AllBindings()`,
with the screen supplying the payload's tokens and the file writer mapping `which_skybox` alone). A player
who pressed *Force Plot* therefore wrote a token the encoder refused, and the refusal landed in the commit
journal - where recovery could never finish it, which is the installed-copy dead end recorded in §6.2 and
in the plan's P7.

### 4. The provider's own settings file, and why the redirect is the fix

`ulti/gen/ulti_macros.dtb` names the file the provider writes: `GAME:/ulti_settings.dta`. The game root is
mounted **read-only** (`allow_game_relative_writes` defaults false; the overlay's union is no more writable
than the root it wraps), so the mod's `save_settings` has **never** been able to write it on this port:
checked before this pass, `ulti_settings.dta` did not exist anywhere on the machine, under any spelling.
Plan §6.2's rule is exact — *do not make the whole game mount writable*. The implementation is a third side
on the union device:

* `PayloadOverlayDevice::AddSettingsSide(settings_root, redirects)` mounts a **writable** `HostPathDevice`
  at `\Device\BlitzUserSettings` over `<user_data_root>/ultimate`, and records the root-level records it
  serves (the provider's file is `<game root>/ulti_settings.dta`, so the redirect list is root-level by
  construction; a nested entry is refused with a log line).
* `ReadBackingFor`/`WriteBackingFor` (SDK-free, in `src/fs/overlay_merge.h`) answer `kSettings` for such a
  record in **both** directions: whatever the payload or game root has under that name is not what the guest
  sees.
* The union's root offers the record only when the player actually has the file — reporting a file that does
  not exist would make the provider's own `file_exists` lie. The first write creates it, through
  `CreateEntryInternal`, which the union's root forwards to the settings device.
* `PayloadOverlayDevice::is_read_only()` reports writable **when there is a settings side**. That is what
  lets `Entry::CreateEntry`/`VirtualFileSystem::OpenFile` get as far as the union's root; every write that
  is *not* a redirected record is still refused one level down by the backing device's own read-only flag,
  which the overlay's rule tests and the boot's untouched record hashes both show.

Nothing pre-creates the file, so a player who never opens the provider's settings never grows an empty one;
and the adapter's writer preserves every pair the provider persists and this build does not draw
(`controller_mapping`, `server_url`, `server_ssl`, `p9dlc`), in the file's own order.

**2026-10-24 format correction [measured]:** writable storage was already working; the host
adapter was rejecting the mod's native file. The owner's session log
`out/build/win-amd64-release/logs/rb_blitz_330.log:544` reports
`refusing to rewrite ... ulti_settings.dta: there is content after the outer array`.
The actual file contains 21 top-level `(name value)` pairs, beginning `(freq_ms 0)`,
with `(goldprogress 1)`; it has no outer array or version marker. `write_file` writes the
array's children. The earlier `(0 (...))` fixture below measured only the host's invented
wrapper, and a successful guest open did not establish that the guest interpreted its values.

`ParseProviderFile` now reads the native sequence, including its first pair and single-pair
files; it also imports the old host wrapper. `WriteProviderFile` always emits native pairs,
so both a first save and an update are readable by the provider. Unknown pairs, nested
values, order and untouched scalar spelling survive; malformed files are still preserved
and reported. The exact-path writable redirect and atomic replacement are unchanged.
`TestNativeProviderFileSave` reproduces the 21-pair file in scratch storage, saves/reloads
Gold Star Progress in both directions and checks preservation; `TestRealPayload` checks
that a fresh `ConfigureProvider` restores the saved Gold Star Progress value.
Verification: the release build passed (`build-retry.log`) and all 14 related host tests
passed (`tests.log`), under `out/_swap/ultimate-settings-format/`. The documentation checker
reports 40 unrelated stale plan links/references. Game UI/restart validation of this
correction remains unproven; no game UI was driven for these checks.

### 5. Evidence

Host: `ctest --test-dir out/build/win-amd64-release` — **31/31**, of which `ultimate_adapter` (hermetic) and
`ultimate_adapter_payload` (the real install) are 786 checks and `payload_overlay` is 95 (two new cases for
the settings side).

- The five provider DTBs parse and recompile byte-for-byte with the project's own grammar —
  `out/_swap/p6/ulti/ulti/gen/*.dtb` through `rb_blitz_dtb_author_tests.exe` → 40 checks each, exit 0.
- The installed payload is a payload: 62 records, six of six required present, ark v6
  (`ultimate_adapter_payload` → `6 records present, 0 missing, ark v6`).
- Its shape is the one this build draws: 8 panels, 21 persisted names, 16 defaults, 3 choice sets
  (`ultimate_adapter_payload` → `report.ok`).
- A mutated payload is refused **by name**, eight ways (`ultimate_adapter`; each case names the missing
  record/panel/row/variable/path).
- Nothing is claimed `hot` without an observed live effect — every binding is `restart`, evidence empty.
- The provider's file survives a rewrite: unknown pairs, their order and the unexposed text survive —
  `(0 (controller_mapping "A B X Y") (freq_ms TRUE) … (legacy_multi 1 2 3))`.
- A file this build cannot parse is refused, not rewritten (bytes unchanged, no `.tmp` left).
- The value path end to end (host side): the payload's defaults, an edit through `EncodeProviderValues`, a
  durable write and a re-read → `hide_hud FALSE`, `gems ulti` survive a reload; `letterbox` keeps the
  payload's default.
- The guest's own file call reaches user storage (boot `provider.log`): `ultimate: the guest opened
  …\ulti_settings.dta for 0x…`.
- Installed records are never modified (retail + payload hashes re-checked after the boots, §5.1).

#### 5.1 Boots

All boots: `rb_blitz.exe --game_data_root=<install> --ultimate_mode=2 --ultimate_payload_root=<dir>
--user_data_root=<scratch> --launcher_profile=<empty> --log_level=debug`, through `out/_swap/p6/run.ps1`;
each has its own scratch user root, so one boot's redirect target cannot be another's file.

| Arm | Payload | What it shows | Evidence |
| --- | --- | --- | --- |
| `provider` | `game/ultimate` (the mod), provider file seeded as `(0 (controller_mapping "A B X Y") (hide_hud TRUE) (letterbox TRUE) (gems monkey) (speedmod 1.10) (unlockall TRUE))` | the adapter's real readiness, the redirect, the values seeded into the store, the payload's choice inventories, and the **guest's own open of the redirected record** | `out/_swap/p6/provider/provider.log` |
| `merged2` | `out/_swap/p5b/payload` (P5b's generated merged menu) | the gate is the adapter's answer, not the diagnostic: a *mounted* payload carrying no `ulti/gen/mod_settings.dtb` is `kUnrecognizedShape`, so nothing is seeded, `provider.active` answers false, **no settings side is mounted at all**, and the merged root draws the five rows P5b measured for a false gate | `out/_swap/p6/merged2/merged2.log`, `out/_swap/p6/merged2/shots/00-root.png` |

The provider arm's own lines, verbatim:

```
ultimate: provider compatible: 6/6 records, 8 panels, 11 values
ultimate: 1 record(s) redirected to <user_data_root>\ultimate: ulti_settings.dta
ultimate: overlay \Device\BlitzOverlay = <payload> + <game> - 4 payload-only record(s),
          39 game-root-only, 0 payload copies preferred, 1 merged director(ies), 1 hidden,
          1 redirected
settings_bridge: provider active - 10 row value(s) seeded from provider compatible: …
settings_bridge: provider choice list 'gems' = default, freq, amp2003, amp2016, monkey, ulti, x, bolt
settings_bridge: provider choice list 'which_skybox' = default_skybox, no_skybox, nfs_mw2012
settings_bridge: provider choice list 'forceplot' = off, plot_bodega_a, … plot_tokyo_c
ultimate: the guest opened \Device\BlitzOverlay\ulti_settings.dta for 0x80100081 -> 0x0
```

The last line matters: `0x0` is success, and that open can only have come from the guest's own
`file_exists`/`read_file` on `GAME:/ulti_settings.dta` — the provider's own `load_settings`, run by the
payload at boot. So the read path from inside the game to the player's own storage is **measured, not
argued**. The seeded file is 114 bytes at `<scratch>\ultimate\ulti_settings.dta`.

`merged2` says the same from the other side:

```
ultimate: provider shape: payload records unreadable: ulti/gen/mod_settings.dtb is not in the payload
settings_bridge: provider not active - 0 row value(s) seeded from payload mounted but not the
                 provider's shape: … (ok)
```

…and its overlay line has **no** `redirected` count, because a payload that does not name the settings file
gets no settings side: the union stays exactly as read-only as the game root.

**Installed records unchanged** (re-hashed after the boots, against `config/game_fingerprints.toml`):

| File | sha256 (first 16) | Manifest |
| --- | --- | --- |
| `game/default.xex` | `E2195D6241D42319` | `e2195d62…` ✓ |
| `game/gen/main_xbox.hdr` | `A12777E8DF91215D` | `a12777e8…` ✓ |
| `game/gen/main_xbox_0.ark` | `EAC4D2C2BB71D014` | `eac4d2c2…` ✓ |

The payload's own pair hashes `87B30426…` (`patch_xbox.hdr`) and `736F6F7E…` (`patch_xbox_0.ark`) —
recorded as an observed baseline, because there is no committed payload fingerprint (and, per §2, a
payload's identity is its contents, not a version).

#### 5.2 What the boots cannot show

* **A write through the redirect from the guest.** The provider's own `save_settings` runs when its settings
  screen is left with Cancel, and that screen is reached from the *retail* options list (the mod appends its
  `mod_settings` row). With P5b's payload mounted the retail list is not what the main menu opens, and with
  the mod's payload mounted the merged menu is not there — the two cannot be in one payload until P11
  generates the combined record set (§6.2). The write path is proven host-side (`SaveProviderFile`, and
  `EncodeProviderValues` in front of it) and by construction, not yet by a guest-initiated save.
* **The achievements action.** §6.1.
* **The rows' values affecting gameplay.** Every provider row is `restart`, so the honest test is a save, a
  relaunch and a gameplay observation — P12's matrix, with P9's restart flow.

### 6. Open items

#### 6.1 The achievements action is not wired

`general.unlock_online_achievements` is a one-shot **guest** action: the provider's own body is `{foreach
$acc (acc_high_cred …) {acc_mgr earn_accomplishment $acc}}`, a call on the guest thread through objects the
provider owns. This package does not make it; the bridge answers the `invoke` request without calling the
provider and **says so at boot**:

```
settings_bridge: the achievements action is not wired yet; invoking it answers without calling the provider
```

That warning is the honest half; the call needs the guest-object work P1's findings defers (the
allocator/refcount contract for a host-initiated guest call) and must run on the guest thread with the
provider's objects live. The row must not be presented as working until then.

#### 6.2 The other open items

| Item | Why open | Who owns it |
| --- | --- | --- |
| **The combined payload's shadowed records.** P5b's generator edits the *base* `ui/gen/ui.dtb` and `ui/splash/gen/splash.dtb`; the payload also carries its own copies of both, which the player's build has mounted, so a patch record built from the base ark would wipe the mod's edits to those two records. | The generator must read the record from the **mounted** payload when it has one, then apply our edits on top — plan §11's "generate the patch record that carries project definitions by extending an installed retail record". Measured: both records are in the payload's 62 (verified with `scripts/hmx_ark.py list --hdr game/ultimate/gen/patch_xbox.hdr -v`). | P11 |
| **`forceplot` and `which_skybox`: fixed 2026-10-15, and this entry is the corrected one.** It used to say the screen's placeholder tokens (`plot_a`, `plot_b` / `none`, `custom`) were not the payload's. They were replaced by the payload's inventory at boot (`src/hooks/settings_payload.cpp`) - that half was already wired - and the *value* side was wrong in a way the token half hid: the two rows are the payload's boolean-with-submenu pair, so the provider's file holds the chosen **position**, and a codec that spelled them as booleans refused the very token the menu had just written. On an installed copy that refusal was written into the commit journal, which recovery could then never finish, so **every** later edit answered `io error` and the menu blamed the settings folder (measured 2026-10-14, `out/_swap/settings-save/` + the plan's P7). Fixed by `RowBinding::position_value` (part 5 §3), by parking an uncompletable journal instead of refusing every save (part 3 §4), and by a reason token so the save-error page says which failure it was. | The tokens were never the whole contract. | done |
| **A merged install** (the mod's documented install method: files copied over the game root) has no payload directory to inspect and no overlay to redirect through, so the adapter reports it absent. | The separate-payload arrangement is the one this project mounts (`game/ultimate` + `--ultimate_payload_root`); a merged install would need the records read out of the game's own ark and a different redirect route. | P6 follow-up / P11 |
| **`unlockall`'s write path.** Its *default* is read from the payload (`ulti_init` sets it true) and the adapter exposes it, but its *value* belongs under native `[ultimate_overrides]` (plan §6.2) — i.e. in P3's store, which the bridge does not own yet. | The bridge writes only the provider's file. `EncodeProviderValues` skips the row by design and the file's `vars` list does not carry it, so nothing is silently lost — it is simply not persisted yet. | P3/P11 wiring |
| **Provider rows with no value.** `ulti_init` sets 16 variables; 10 are rows this build draws, and the other 7 (e.g. `hide_hud`, `freq_ms`) have no provider default and no file pair, so the adapter reports them **absent** and the runtime says which: `no provider value for … ; those rows keep their own default`. | Inventing a value would overrule the payload (`os_hide_hud` reads an unset `$hide_hud`, i.e. false) and would hide a payload that later ships one; the screen's own state query already degrades to "off" for an absent value. | by design; P11 decides whether the compiled default is seeded through the service |
| **The provider's second writer.** `ulti/gen/ulti_funcs.dtb` also carries `ulti_settings_writer`, which writes a *different* shape (`((settings (precision …)) (hud (hide_hud …)))`) to `$ulti_settings_path` — a variable nothing in the inspected records ever sets. | Recorded, not implemented: the file this package reads/writes is the one `mod_settings_rw` uses, which the payload's own `load_settings` reads. A payload switching to the other shape would be caught by the shape check (its `vars` list and path macro are the ones checked). | P6 follow-up, only if a payload needs it |
| **Quality of the "verbatim" claim for a nested value.** An element whose value serialized as a nested array is re-emitted with normalized whitespace. | Values the provider writes are atoms, symbols, quoted strings and floats, so a rewrite is byte-exact for every measured file; a nested array is preserved in content but not in whitespace. | documented |

### 7. Reproduce

```powershell
# The adapter and its rules (SDK-free), plus the same binary against the installed payload.
cmake --build out/build/win-amd64-release --target rb_blitz_ultimate_adapter_tests
.\out\build\win-amd64-release\rb_blitz_ultimate_adapter_tests.exe                       # hermetic
.\out\build\win-amd64-release\rb_blitz_ultimate_adapter_tests.exe game/ultimate          # real
ctest --test-dir out/build/win-amd64-release -R 'ultimate_adapter|payload_overlay'

# The boots (one at a time: the harness refuses to start while rb_blitz runs).
pwsh -File out\_swap\p6\run.ps1 -Label provider -Seed '(0 (hide_hud TRUE) (gems monkey))' -Route boot
pwsh -File out\_swap\p6\run.ps1 -Label merged2 -PayloadRoot out\_swap\p5b\payload -Route root
$env:REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE='1'   # the diagnostic that forces the gate open
pwsh -File out\_swap\p6\run.ps1 -Label forced -PayloadRoot out\_swap\p5b\payload -Route root
```

The harness waits for the game's window before driving it (with the mod's payload the window arrives well
after the process starts, and the driver otherwise throws `rb_blitz has no main window` and leaves the game
running). `-PayloadRoot` is resolved to an absolute path first, because the runtime resolves a relative one
against `game_data_root`. `out/_swap/` is ignored: the decoded payload material and boot logs are research,
not deliverables.

### 8. Handover

* **P9** owns the restart flow — every provider row is `restart` with an empty evidence string;
  `SaveProvider`'s comment and the binding table are where the classification lives.
* **P10** owns the hide list and the merge's own descriptors; the provider rows it must not hide are the
  ones whose values the adapter seeds (visible only when `provider.active` is true).
* **P11** owns the combined payload (§6.2), the `[ultimate_overrides]` write path, and seeding the compiled
  defaults for the rows the payload has no value for.
* **P12** owns the honest matrix: a provider row's effect is observable only after a save, a relaunch and a
  gameplay check, and the achievements action must not be presented as working until §6.1 lands.

---

## Part 6 — Hot apply and the restart coordinator (was `docs/plans/in-game-settings-hot-apply-restart.md`, P9)

Status: **P9's host half executed** (2026-10-11). Per-row strategy recorded; one row proven hot and applied
live in a boot; the restart coordinator and its bootstrap written and measured against real processes. Two
items are left to the coordinator / a later UI pass, named in §6 rather than implied away. Parent plan:
[plan.md](plan.md) §5.4, §5.5, §8, §10.1 (P9). Baseline `b940bd1` plus the P9 tree; nothing committed.

### 1. What this package is, and what it is not

§5.5 replaced "v1 is restart-only" with a per-row decision: a row is `hot` when its value can be applied to
the **running** process **with a measured live effect**, `restart` otherwise. §8 then defines what happens
for the rest: save first, spawn exactly one child that waits for this process to die, and only then close.

| Piece | File | What it owns |
| --- | --- | --- |
| Strategy in the frozen model | `src/settings/model.h` | `Descriptor::strategy` + `strategy_evidence`, replacing the v1 `restart_scoped = true` constant |
| The strategy table | `src/settings/schema.cpp` | One entry per `SettingId`, with the evidence string for a `hot` row |
| The application seam | `src/settings/hot_apply.{h,cpp}` | Split a commit into "apply now" and "needs a new process"; never claim an apply that did not happen |
| The runtime applier | `src/hooks/hot_apply_runtime.{h,cpp}` | The one place that knows **how** a hot row reaches the game |
| The channel's live half | `src/hooks/settings_bridge_hooks.cpp` | After a durable commit: plan, apply, log, keep what did not take effect in the restart set |
| The restart coordinator | `src/platform/game_restart.{h,cpp}` | Handoff token, argument rebuild, exactly-once request, spawn, wait, bootstrap |

It is **not** the authored restart prompt (§6.1) and **not** the boot wiring that calls the coordinator
(`src/main.cpp`'s bootstrap entry, §6.2) — both named, with the shape they need, in §6.

### 2. The strategy table, and which rows are hot

`src/settings/schema.cpp` carries a single table (`kStrategies`), materialized into `Descriptor::strategy`
by `BuildDescriptors` and read without a descriptor list by `schema::StrategyOf`. Tests assert the table is
complete, that a `hot` row always carries evidence, that a `restart` row never does, and that it agrees
**row by row** with the provider adapter's own `RowBindings()` table
(`tests/settings_hot_apply_tests.cpp`).

| Row | Strategy | Why |
| --- | --- | --- |
| `graphics.vsync` (`kVsync`) | **hot** | **Measured**: with the guest running, the host flipped this exact cvar and the discovery rate moved from 16.66 ms per item to 5.68 ms (46.7 s → 15.96 s for 2,807 items), the log recording `vsync false, was true` 7 ms after the flip — [dlc.md](../guides/dlc.md) §"Frame pacing for the discovery". The mechanism is the GPU plugin's worker re-reading `vsync` every iteration. |
| `interface.settings_labels` (`kSettingsLabels`) | **hot** | **Measured 2026-10-14**: the caption overlay re-reads `settings_help_overlay` in `HelpOverlayDialog::OnDraw` on every frame, so flipping the row in Help & Options hid the caption on the next draw — `hot_apply: settings_help_overlay = false (was true), applied to the running game` and `settings_bridge: applied 'interface.settings_labels' to the running game; the row carries no pending state`, with no `request_restart` in the log — [status.md](status.md) §"Row descriptions". |
| `general.rhythmverse_{guitar,bass,drums,vocals,keys}` (`kRhythmVerse*` leaves) | **hot** | **Measured 2026-10-20** (host): the applier replaces the store's immutable settings snapshot, which its workers read before every request, and `store_client` observes the generation bump on a real commit and its absence on an equal one. No guest state is involved, which is what makes the swap legal on the commit's own thread. |
| `general.rhythmverse` (`kRhythmVerse`, the master) | restart | The switch owns what the boot **drew** as well as what a worker reads: the main-menu *RhythmVerse* row is compiled into the record the boot generates and the store's heading is a locale entry the payload replaces, so a mid-session change would leave one of the two surfaces lying until the next start. Made `restart` by the owner pass of 2026-10-22 (RhythmVerse plan §9/P9-2), after the cost of a live row presence was weighed against a prompt the surface already has; P9-9's tab landed on the same reasoning. *(The last prediction this cell carried — that P9-10 would add a hidden flow — was retired by the owner's 2026-10-22 re-scope: a failed catalogue hides nothing, so the switch has only the two boot-drawn surfaces named above.)* |
| everything else | restart | Nothing has a measured live effect. Notably `interface.hide_hud` — the parent plan §5.5 named Hide HUD as its hot example **before** P6 measured when the provider reads its values (the payload's loader fills its DTA variables before the first frame), so the honest answer for it is `restart`. The render-path rows are structurally restart (§5.5); the remaining native rows are read at boot by their consumer. |

One candidate has a live read in the code and **no measurement yet**, staying `restart` until one exists:

* `general.dlc_menu_songs` (`enhancements_menu_dlc_songs`): `src/hooks/shell_music.cpp` reads the toggle
  when the menu is entered, so a live write should change the *next* menu entry's pool. Experiment: boot
  with a DLC library, apply the row live, leave and re-enter the main menu, and check for `shell_music: R9
  plays the loaded DLC...` where the control boot has `shell_music: R9 off`. A log-line observation of a
  live effect, which §5.5 accepts.
* `renderer`/`anti_aliasing`/`resolution` are **not** candidates: re-initializing the renderer from inside a
  menu is its own spike (§5.5 explicitly forbids trying).

### 3. The application seam (`src/settings/hot_apply.h`)

```text
PlanHotApply(changed, descriptors) / PlanHotApplyByStrategy(changed) -> HotApplyPlan{live, pending_restart}
ApplyHotPlan(plan, applier) -> one HotApplyOutcome per live row (applied | no applier | refused | failed)
RestartSetAfterApply(plan, outcomes) -> pending_restart + every live row that did not take effect
```

Two mechanical rules: **a hot row with no applier is a restart row** (`ApplyHotPlan` reports `kNoApplier`
and `RestartSetAfterApply` puts it back into the restart set — a build the coordinator has not wired yet
still behaves honestly); and **`applied` means written and read back** (`RuntimeHotApplier` re-reads the cvar
after the write and fails the outcome if the value did not take, so a lifecycle that silently ignores a
runtime write cannot be reported as applied). The *coordinator* must ask `RestartSetAfterApply` for its
prompt decision — not the descriptors — because a session can contain a hot row whose applier is missing
(plan §5.5's "the UI never claims more than it can deliver").

### 4. The runtime applier, and the thread rule

`src/hooks/hot_apply_runtime.cpp` knows two kinds of live write:

```text
graphics.vsync             -> rex::cvar::SetFlagByName("vsync", "true"|"false")
interface.settings_labels  -> rex::cvar::SetFlagByName("settings_help_overlay", "true"|"false")
general.rhythmverse_<i>    -> store::ApplyCommittedRow (replaces the store's settings snapshot)
```

The store's **master** is not among them: it is `restart` (RhythmVerse plan §9/P9-2), so the seam
never asks the applier for it and the table above deliberately omits it - a row listed here is a
claim that a commit reaches the running game.

* **By name**, because `vsync` is defined in `rexruntime.dll`'s GPU category and `settings_help_overlay` is
  defined by the game's own overlay module (`src/ui/help_overlay.cpp`); this project defines no storage
  symbol for either (the same reason R7 reads `vsync` by name).
* **On a host thread.** The commit that triggers an apply arrives on the *guest* thread — it is the guest's
  own `write_file` — and a guest thread must not touch the cvar registry
  ([song-cache-plan.md](../plans/finished/2026-10-10-song-cache-plan.md) §7). `Apply` runs the write on a short-lived thread and
  **joins** it: R7 goes async because it is on the frame path, whereas a settings commit already writes a
  file synchronously, and joining is what lets the seam answer honestly.
* A row it does not know is reported as `kNoApplier` by the seam, never guessed.

`settings_bridge` gained two things so a boot can see this: `MemorySink::last_commit_changes()` — the tokens
a commit **really changed** (a value written back unchanged reports nothing, so a no-op session cannot
re-apply a hot row); and `settings_hot_apply_diagnostic` (env `REX_SETTINGS_HOT_APPLY_DIAGNOSTIC=1`) — puts a
hot row through the real channel (a `set` and a `commit`, exactly what the menu writes) at start-up, so a
boot log proves the live path and not only the tests. It writes the row's own cvar and therefore changes
that run, which is why it is off by default.

### 5. Evidence (2026-10-11)

Harness `out\_swap\p9\run.ps1 -Label <name> [-Diagnostic] [-FullBoot] [-Seconds n]`: boots `rb_blitz.exe`
against a disposable `--user_data_root`, waits for the channel's own marker and stops the game; `-FullBoot`
runs P6's boot route to the title screen.

| Arm | Log | Observation |
| --- | --- | --- |
| `control` | `out/_swap/p9/control/control.log` | `settings_bridge: live applier: graphics.vsync -> cvar 'vsync'`, and **nothing else**: no row applied, no cvar written — the channel is silent on a boot that changes nothing. |
| `hot` | `out/_swap/p9/hot/hot.log` | `hot-apply diagnostic committed 'graphics.vsync' -> ok`; `settings_bridge: applied 'graphics.vsync' to the running game; the row carries no pending state`; `hot_apply: vsync = false (was true), applied to the running game`; `1 of 1 changed row(s) applied live; 0 row(s) still need a restart`. |
| `full` | `out/_swap/p9/full/full.log`, shot `out/_swap/p9/full/shots/01-menu.png` | The same apply, then a boot that carries on: the pad reaches the title screen with the value already changed mid-process, no fault (`violation`/`guest access`/`fatal`: zero hits). |

The two apply lines come from **different thread ids** (`t13976` guest thread vs `t26980` the applier's own
thread) — the thread rule visible in the log.

Claim boundary, plainly: the arms prove the *write, the read-back and that the process keeps running*; the
**live effect on the frame rate** is R7's measurement cited in §2, not re-measured here. A boot that drives
the merged menu's V-Sync row and measures the frame-rate change is the acceptance test §12 should run (the
row's path is now real, so it can).

Tests: `ctest` is **33/33** green in `out/build/win-amd64-release`, including two new targets:
`settings_hot_apply` (the strategy table — complete; hot ⇒ evidence; restart ⇒ none; the schema and the
provider adapter agree; the split; every apply outcome including the null-applier case) and `game_restart`
(the token — round trip, bound, eleven refusal shapes; the Windows quoting rule; the argument allowlist
rebuild — one-shot/transient arguments dropped, explicit developer arguments kept, the target's own flags;
the state machine — exactly-once, stale revision, nothing-saved, spawn failure with a retry, no-Controller Settings;
and **real process legs** — a child started and waited for, a failing spawn, a child that waits for its
parent's *verified identity* before it does anything, the reused-pid case that must not hold it hostage, and
the timeout that refuses to start beside a living parent).

### 6. What P9 did not do

#### 6.1 The authored restart prompt (UI)

> **Superseded 2026-10-14 by P3 of
> [`../plans/finished/2026-10-14-installed-game-fix-prompts.md`](../plans/finished/2026-10-14-installed-game-fix-prompts.md)
> (the successor record). The prompt exists now — generated by `src/ui/settings_screen.{h,cpp}` as
> `settings_restart_panel`/`settings_restart_screen`, asked by the merged root's own Cancel through
> `snapshot/restart.pending`, with "Restart now" as a row of kind `kRestart` — and that pass fixed the
> two defects the first version had: the confirmation could not be dismissed (it returned to the root it
> was leaving, so Help & Options could not be left at all) and a refused restart left `restart.ok` true,
> so the row was silently inert. The paragraphs below are the state on 2026-10-11 and are kept as the
> measurement of record for what P9 handed over.

The §8 visual prompt — native retail error/confirmation resources, "A Restart now / B Close" — is authored
DTA work in `src/ui/settings_screen.*` and is not in this pass. What exists is the seam it will call: the
guest writes `rbbz-settings/restart/-/-` (the op is already in the protocol), and a commit's restart set
decides whether the prompt appears at all. The Target row currently commits and leaves without offering the
soft restart — the item §3.1 of the plan describes and this pass did not close. (2026-10-14: it is the
root's Cancel that offers it, so changing Target and then leaving does prompt.)

#### 6.2 The boot wiring (coordinator)

> **Superseded 2026-10-14 — the successor is P3's record in
> [`../plans/finished/2026-10-14-installed-game-fix-prompts.md`](../plans/finished/2026-10-14-installed-game-fix-prompts.md).**
> Both insertions below exist now: `src/main.cpp` has the pre-SDK bootstrap (`RestartBootstrap`, a
> static constructed before `wWinMain`), and `rb_blitz_app.h`'s `OnPostLoadXexImage` installs the
> channel's restart handler, which drains the DLC refresh, asks `RestartCoordinator::Request` on the UI
> thread and defers the window close. Verified driven 2026-10-14:
> `out/_swap/p3/after-restart/after-restart.log` — the request, the child waiting for parent exit,
> `Title terminated; hard-exiting process.`, and a new boot 0.6 s later.

`src/main.cpp` does not yet call the bootstrap, and nothing calls `RestartCoordinator::Request`. Two small,
exact insertions:

1. **An early bootstrap mode in `rb_blitz.exe`**, before `REX_DEFINE_APP` starts the SDK: if the command
   line carries a handoff token, `platform::RunBootstrap(argc, argv, nullptr)` waits for the parent's
   verified identity and returns `kStartGame` with the one-shot arguments removed. Doing it in the existing
   executable avoids a second binary in the payload (§8 prefers this); a dedicated helper is the fallback
   and needs a packaging test.
2. **The request**, after the settings save barrier and on the UI thread:

```cpp
platform::LaunchContext context;
context.executable = /* this executable */;
context.game_data_root = /* the absolute root this boot used */;
context.working_directory = /* its folder */;
context.profile_file = /* the identity this boot was started with */;
context.target = /* the newly persisted target's token */;
context.developer_arguments = /* THIS boot's explicit dev flags, minus the retired ones */;
platform::RestartCoordinator coordinator(platform::CreateChildLauncher().release());
const platform::RestartOutcome outcome = coordinator.Request(saved_revision, current_revision, request_id, context);
if (coordinator.CloseRequested()) { /* ask the UI to close, then let OnClosing run */ }
```

`request_id` may be 0 (what a DTA script sends); the token still gets a non-zero identity.
`developer_arguments` is an explicit allowlist supplied by the caller: this module drops the retired ones
(`--restart-handoff=*`, `--restart-bootstrap`, anything naming `refresh_dlc_cache`) and keeps the rest,
because a debugging flag must survive a restart (§8) — filtering *Controller Settings-generated setting flags* is the
caller's job, since that list lives in Controller Settings' own generated table.

#### 6.3 Where the live apply runs today, and where it must move

The channel that exists is P1's `MemorySink`; P3's `Service`/`Store` are not wired into a boot yet (P11). So
today the live half runs in `settings_bridge_hooks.cpp` against the in-memory store. When the coordinator
wires the real service, the same three calls move onto `Service::last_commit_changes()` (P9 added it) and
the seam is unchanged — which is exactly why the seam is its own module.

#### 6.4 Honest limits of the coordinator

* The **handshake is "the spawn succeeded"**: the child's first instruction is to wait for this process to
  exit, so a successful spawn means it is running and has touched nothing. A pipe-based child signal would
  be stronger and is not needed while the child does nothing else first.
* `WaitForExit` in the bootstrap is **bounded** (30 s by default) and a timeout returns `kStartGame ==
  false`: two titles writing the same saves is the one outcome §8 forbids, so the child exits instead.
* The token carries the parent's **creation time** as well as its pid, and the waiter compares them before
  waiting: a reused pid is refused rather than waited on.

### 7. Reproduce

```powershell
cmake --preset win-amd64-release
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure          # 33/33

pwsh -NoProfile -File out\_swap\p9\run.ps1 -Label control                # the silent arm
pwsh -NoProfile -File out\_swap\p9\run.ps1 -Label hot  -Diagnostic       # the applied arm
pwsh -NoProfile -File out\_swap\p9\run.ps1 -Label full -Diagnostic -FullBoot   # + the title screen
```
