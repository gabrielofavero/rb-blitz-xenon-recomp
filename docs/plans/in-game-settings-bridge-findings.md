# In-game settings — the guest bridge: P1 findings and transport decision

Status: **P1 partial**. The transport is decided and the host half of the channel is implemented,
built and tested. The **native screen is not built, and G1 is not met** — that is the remaining
work, and §9–§10 say exactly what is missing. Nothing in this pass changes the running game: the
new module is not referenced by any hook yet, so a boot today behaves exactly as before.

**Update (2026-10-10, P1-b).** §9's resource-formatting gap is closed and §10's steps 1–3 are
answered by measurement: a panel name is a script object reached through `[include]` in
`ui/gen/init.dtb`, the include's name-to-file rule is measured, and an **authored** definition now
loads in a real boot through a self-made patch archive. See
`docs/plans/in-game-settings-panel-api.md` §8 (seven-boot evidence table) and §10 (what is left:
registering the screen and the row-widget binding).

**Update (2026-10-10/11, P1-c).** The two halves §10 left open are now built and measured to
different depths: the **guest bridge runs end to end in the host** and the screen is **registered
and reachable in a real boot**, but a screen of ours that focuses a *panel of ours* crashes the
guest. §12 has the eight-arm bisection and the one question it leaves. Do not read the row-kind
work below as finished: the four row kinds are authored and tested, and the screen they live on is
the thing that is blocked.

Evidence labels (parent plan §2.1): **[tree]** = this working tree, `file:line`; **[cited]** = a
build product or a byte measured from local data; **[assumed]** = reasoned, not measured.

Read with: `docs/plans/in-game-settings-plan.md` (requirements, gates, ownership),
`docs/plans/in-game-settings-contracts.md` (frozen types and rules).

---

## 1. What this pass did, and what it did not

| Done | Not done (and why) |
| --- | --- |
| Identified and **measured** the guest builtin name table on the pinned retail image (§2.1), and the **panel/list script API** the screen will use (`docs/plans/in-game-settings-panel-api.md`, read out of retail `pause.dtb` and `splash.dtb`). | The **authored screen**: no panel definition is loaded, no `Advanced Settings` row exists, no row kind navigates. The blocker is now narrower: how a *new* panel name resolves to its file, and the row-widget binding (§9). |
| **Verified the builtin calling convention** from the recompiled guest code — `r3` result cell, `r4` argument array, element stride (§2.2). | The **no-op forwarded hook** comparison the plan asks for ("Compare the original call with a no-op forwarded hook before implementing namespaced behavior") — needs the guest hook, which needs the screen (§10). |
| Decided the **transport** and wrote down why (§3). | The `read_file` **response** channel, because returning a string needs the guest DataNode/array build API, which is not verified (§5). |
| Implemented, built and tested the **host half**: `src/hooks/settings_bridge.{h,cpp}` + `tests/settings_bridge_tests.cpp` — exact namespace matching, bounded and checksummed frames, structured status, idempotent request ids (§4). | End-to-end runtime evidence: no guest call has been made and no capture taken (§11). |
| Implemented the **authoring half** of the compiled-DTA grammar: `src/ui/dtb_author.{h,cpp}` + `tests/dtb_author_tests.cpp`, whose real-file case round-trips **six shipped definitions byte for byte**. | The allocator/refcount contract for *producing* guest values (§5). |

Build/test evidence from this pass **[cited]**:

```
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --target rb_blitz            # includes the new source
cmake --build --preset win-amd64-release --target rb_blitz_settings_bridge_tests
ctest --test-dir out/build/win-amd64-release -R settings_bridge       # 1/1 passed
ctest --test-dir out/build/win-amd64-release                          # 23/23 passed
```

---

## 2. The builtin surface (measured)

### 2.1 The name table

The script builtins are interned from a NUL-separated string block in the image. Dumped from
`out/_swap/blitz_image.bin` (image base `0x82000000`, so file offset = address − base) **[cited]**:

```
0x82080DD0  file_exists\0 write_string_to_file\0 write_file\0 read_file\0 run\0 --\0 ++\0
            reverse_interp\0 eval\0 ...
0x82081040  int\0 foreach\0 last_elem\0 ... func\0 sprint\0 sprintf\0 printf\0 next_name\0
            replace_object\0 File...\0
```

The four addresses the parent plan names are therefore confirmed **exactly**: `file_exists`
`0x82080DD0`, `write_file` `0x82080DF4`, `read_file` `0x82080E00`, `run` `0x82080E0C`.

### 2.2 The calling convention (verified from the recompiled bodies)

A builtin is an ordinary recompiled guest function: `void f(PPCContext& ctx, uint8_t* base)`.

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

Frozen consequences **[cited]**:

1. **`r3` is a hidden 8-byte result cell** `{u32 value, u32 type}`, owned by the *caller* (it is the
   caller's stack). The function returns the same pointer in `r3` so a caller can chain.
2. **`r4` points at the argument array**: `*(u32*)r4` is the first argument's node pointer, and
   argument *i* is at `node + 8 + 8*i`. A string argument is read with `sub_823F9538`.
3. **Writing the result only into `r3` corrupts the ABI** — the caller reads the *cell*, exactly as
   the parent plan §2.3 warned. Any hook that answers a builtin must write the cell.
4. The engine's own existence test is **`sub_8236D830(string, 0)`**, and the file-to-array loader is
   **`sub_824193A8`** (as the plan says), reached from `run`.

### 2.3 The registrar picture — partially identified, and that is enough for the transport

The parent plan names `sub_82420658` as "the initializer". Measured: it is **a** registrar, but not
the one for the file builtins — the names it interns come from the *second* block (`0x82081080`,
`0x8208108C`, … = `func`, `sprint`, `printf`, …), and the callbacks it stores are in the `0x8241F4xx`
range **[tree]** `generated/default/rb_blitz_recomp.36.cpp:15754`.

The file builtins' names are referenced from `0x82070DD0` (a second copy of the same table — `lis
r11,-32249` + `addi 3536`) inside `sub_823C32D0`, a name-resolving helper, not a registrar
**[tree]** `generated/default/rb_blitz_recomp.26.cpp:13425`.

**Consequence.** The registrar that installs the file builtins is still unidentified. That is a gap
only for *adding our own builtin*; the transport in §3 does not need it, because it rides on
builtins the title already has. Finding the registrar is deferred to the pass that registers the
screen's own script entry point (§10 step 3).

---

## 3. Transport decision

**The guest reaches the host through the title's own `file_exists` / `write_file` builtins, by
hooking the recompiled guest functions and answering only paths in an exact project namespace.**

Why this transport, and not the alternatives the parent plan §5.3 lists:

| Option | Verdict |
| --- | --- |
| **Guest builtin hook (chosen)** | No new service, no network, no new process. The functions are ordinary recompiled guest functions, so hooking them is the pattern already proven three times in this repo — `ultimate.cpp` (`sub_821D0A18`, `sub_8236C108`), `shell_music.cpp` (`sub_82757F50`, `sub_827586D8`), `content_progress.cpp` (`sub_82759308` …). It adds **no `Nt*` hook definition**, so the read/query hooks owned by `menu_filter.cpp` and `dlc_refresh.cpp` keep their single strong definitions. |
| A second `Nt*` file hook | Rejected: it would duplicate a strong symbol (`NtCreateFile`/`NtOpenFile`/`NtQueryFullAttributesFile` are dlc_refresh's; `NtReadFile`/`NtReadFileScatter` are menu_filter's) or bypass one of them. The parent plan and the contracts doc forbid it. |
| Generalising the DLC sentinel into a command parser | Rejected outright by the parent plan §5.3. The DLC refresh sentinel is **substring**-matched on purpose; a settings command must be **exact**-namespace and structured. |
| Full `read_file` response channel in v1 | Deferred: returning a string requires building a guest DataNode/array, and the allocator/refcount contract for that is not verified (§5). The safe subset is enough for v1: `write_file` (no result to build) and `file_exists` (a boolean). |

**Safe subset used by the channel** **[cited]**:

- **Request**: `write_file("rbbz-settings/<hex frame>", <ignored>)` — the builtin's own result is
  `{0, 0}`, so there is nothing to construct and nothing to leak. (Or `file_exists` with the frame in
  the path, for a request that needs no data argument; both carry the same frame.)
- **Acknowledgement**: `file_exists("rbbz-settings/<hex response>")` — a boolean result, which the
  ABI above shows is trivially safe to write.

A guest script can build both strings itself, so no new guest-side facility is required.

---

## 4. The channel (implemented)

`src/hooks/settings_bridge.{h,cpp}` **[tree]**, SDK-free so the rules are testable without a boot.
Frozen shape:

| Element | Value |
| --- | --- |
| Namespace | `rbbz-settings/` — matched **exactly** (`IsNamespacedPath` requires the whole prefix and something after it; `songcache:/rbbz-settings/x` and `rbbz-settings-x/` are **not** ours) |
| Frame | `magic "RBZ1"` \| `u16 version` \| `u16 op` \| `u64 request_id` \| `u32 setting_len` + bytes \| `u8 kind` + value \| `u32 crc32` — little-endian, hex-encoded for the channel |
| Ops | `kSnapshot`, `kBeginEdit`, `kSet`, `kCancelField`, `kCommit`, `kCancelEdit`, `kInvoke`, `kRequestRestart` (wire-stable) |
| Statuses | `kOk`, `kUnsupported`, `kBadVersion`, `kMalformed`, `kTooLarge`, `kUnknownOp`, `kStaleRequest`, `kConflict`, `kInvalid`, `kIoError` (wire-stable) |
| Bounds | frame ≤ 8 KiB, path ≤ 32 KiB, string ≤ 4 KiB, list ≤ 256 items — all checked **before** a copy |
| Integrity | CRC-32 over every byte before it; the checksum is verified **before** any field is interpreted |
| Idempotency | `Dispatcher` remembers the last 16 `(request_id, op)` → response; a repeat returns the first answer and never reaches the sink |
| Message field | printable, ≤ 256 chars — a message can never carry data or a newline into a log |

The sink interface (`RequestSink`) is the seam P3 fills with the real `Service`; `StubSink` is the
deterministic stand-in the tests and the host-side round trip use.

`tests/settings_bridge_tests.cpp` **[tree]** pins: namespace exactness (including the substrings that
must **not** match), every op × value-kind round trip, malformed/truncated/non-hex/bad-checksum/
oversized/too-long/too-many-items rejection, response round trip with message sanitisation and
truncation, version/op/service statuses, the idempotency window and its eviction, and a whole
stub round trip. **1/1 passes; the full suite is 23/23.**

---

## 5. Ownership and allocator API — what is known, what is missing

**Known [cited]:** the result cell is 8 bytes, caller-owned, `{value, type}` with type `0` = int/bool;
`file_exists` and `write_file` are safe to produce because their results are a boolean and a
constant zero. Node payloads for string arguments start at `node + 8`, stride 8, converted with
`sub_823F9538`.

**Missing [assumed]:** producing a *string* or *array* result requires the guest's DataNode/array
construction and its allocator/refcount rules. Neither is verified: `read_file`'s own return path
(`sub_824193A8`, then wrapping in a DataNode array, then `sub_823F92E0`) was read but not traced, and
no ghost/refcount contract is written down. **Do not build the `read_file` response channel until
that is traced and tested with a disposable guest call.** This is the single largest ABI risk left
in the transport.

---

## 6. Guest lifecycle entry point

- The **transport** needs no lifecycle hook: a recompiled-function hook is installed at link time
  (the `extern "C" REX_FUNC(sub_…)` + `__imp__sub_…` forward pattern above), so it is live from the
  first guest call.
- The **screen** does need one, and it is not yet identified. What is known **[tree]**: the existing
  configuration points are host-side — `OnPostLoadXexImage` in `src/rb_blitz_app.h:232` calls
  `menu_filter::Configure`, `local_save::Configure`, `ultimate::Configure`, `dlc::Configure`,
  `content_progress::Configure`, `shell_music::Configure`; `OnPreLaunchModule` (`:266`) finalizes the
  cvar registry. The parent plan is explicit that the native menu must **not** be installed by
  blindly calling script functions from `OnPostLoadXexImage`: it needs a guest point *after* the
  script/object registration completes, and that point is **not yet traced**. It is §10 step 1.

---

## 7. Checksum implications

The chosen transport **touches no file**: it answers builtin calls in host memory, so it has no
obligation to the ark index or to the title's content database at all. This is the opposite of the
read-buffer DTB edits **[tree]** `src/hooks/menu_filter.cpp` / `src/ui/menu_options.cpp`, which must
keep a file's length and retarget the `{name, sha1}` row the title checks. Consequence: the bridge
does not need the digest correction those edits need.

If the **screen** route ends up replacing a retail DTB on its read (the fallback in parent plan §5.2),
that route inherits the existing obligation — byte-neutral edit and checksum-row retarget — which
`menu_filter.cpp` already implements and must own (single strong `Nt*` definition).

---

## 8. Original forwarding behaviour

For a recompiled guest function the original body is reached through the runtime-provided import, so
the pattern is **[tree]** `src/hooks/ultimate.cpp:303`:

```cpp
extern "C" REX_FUNC(sub_8241DFB0) {           // file_exists
  if (!TryAnswerNamespacedPath(ctx, base)) {
    __imp__sub_8241DFB0(ctx, base);           // exactly once
  }
}
```

Rules the next pass must keep: forward **exactly once** for every path that is not ours; a hook must
not silently drop a call; and the namespace test must be exact, not the DLC sentinel's substring
search. The hooks are not written yet — §10 step 4.

---

## 9. The resource-formatting gap (why the screen is not built)

What exists **[tree]/[cited]**:

- The compiled-DTA grammar is fully known and already round-tripped byte-for-byte by
  `src/ui/menu_options.cpp` (`splash.dtb` = 7,148 bytes retail, 8,808 in the payload ark) —
  `docs/engine/main-menu-flow.md` §2 and `docs/assets.md`.
- The main menu's option array and its `switch {start.lst selected_sym}` action table are documented,
  so a *row* and its *action* have a known home.

What is **missing**, and is the real gap:

- **How a panel name resolves to its definition file.** The panel/list *script* API is now known —
  see `docs/plans/in-game-settings-panel-api.md`, measured from the shipped `pause.dtb` and
  `splash.dtb`: a screen is `{new UIPanel <name> {file <milo>} {focus <list>.lst} {enter {do ...
  {<list>.lst set_data <array>}}}}` with one `switch {<list>.lst selected_sym}` case per row symbol.
  What is still unknown is the rule behind `{pause_panel load}` in `ui/gen/ui.dtb`, and that list
  cannot grow without a byte budget (the ark index fixes the file's length). The parent plan's
  byte-*neutral* machinery can only pay for bytes it frees inside the same file.
- **A row's widget type.** Which list-row widget draws a boolean, a horizontal select, a submenu and
  a compound row is a property of the list resource (`ui/resource/lists/gen/list_*.milo_xbox`), and
  binding a row symbol to a widget type has not been attempted. Ultimate's `list_mod_settings.milo_xbox`
  is **absent from the retail ark** and must not be copied. The retail candidates are
  `list_options_main.milo_xbox` and `list_splash.milo_xbox`.

So: neither the DTB *file format* nor the *panel/list script API* is the gap any more; the gap is
**registering a new panel name**, plus the row-widget binding.

---

## 10. The narrowed next experiment (what P1-b is)

In order, each with its own evidence requirement:

1. **Find the guest lifecycle point** where UI/script registration is complete and the splash panel
   builds its list (the parent plan §5.2 item 1). Evidence: a trace showing the splash list being
   built, and the call that builds it.
2. **Learn how a panel name resolves to its definition file** — the one question that blocks a new
   screen. Evidence: a boot log naming the file the engine reads for a known panel name during
   `ui my_init` (`menu_filter.cpp` already sees every read). This replaced the old step 2: the
   panel/list script API itself is now measured — see `docs/plans/in-game-settings-panel-api.md`.
3. **Author the definition.** The capability now exists: `src/ui/dtb_author.{h,cpp}` produces
   well-formed compiled DTA and its test round-trips six shipped definitions byte for byte.
4. **Write the two hooks** (`file_exists`, `write_file`) with the forwarding rule of §8 and prove the
   **no-op forwarded hook** first, then the namespaced answer. Evidence: a boot log line per handled
   request and a capture of the guest reading a response.
5. Only then the four row kinds, entry/cancel/back with keyboard and pad, and G1.

If step 2 cannot produce a resolution rule, the honest fallback is the parent plan's second route
(a narrowly scoped loader interception for project-owned virtual files), and the third route (a
byte-neutral bootstrap edit) only with an independently proven byte budget.

---

## 11. Honest status against G1

| G1 requirement (contracts doc §10) | Status |
| --- | --- |
| Base-only runtime opens the independent native screen | **Not met** — no screen exists. |
| All four row kinds navigate | **Not met**. |
| One edit round-trips without leaks/crashes | **Not met end to end**; the host half round-trips against the stub (`TestEndToEndStubRoundTrip`). |
| Installed files unchanged | **Met by construction** for the transport (no file is written); unverified for the screen because there is none. |
| Records transport / ownership / lifecycle / checksum / forwarding / formatting gap | **Met** (§3–§9), with the ownership gap called out as open (§5). |

Nothing here was captured from a running game: no boot was performed in this pass. Treat every
runtime claim above as still owed.

---

## 12. P1-c: the bridge runs, the screen registers, and a screen of ours that focuses a *panel of
ours* crashes

Measured 2026-10-10/11 with the payload device and the project's own UI driver. Everything below is
**[boot]** unless marked otherwise. Scratch: `out/_swap/p1c/` (builder `stage.py` → the emit modes of
`rb_blitz_settings_screen_tests` → `make_arm.py` per arm; driver `run.ps1 -Route <name>`); captures in
`out/_swap/p1c/shots-*/`; one log per arm.

### 12.1 What is built (host-tested, `ctest`)

| Piece | Where | Evidence |
| --- | --- | --- |
| The typed channel's frames, the DTA text grammar, the idempotency rule and an in-memory store | `src/hooks/settings_bridge.{h,cpp}` | `ctest settings_bridge`: **302 checks** |
| The two guest builtins overridden, exactly-once forwarding, the boot line | `src/hooks/settings_bridge_hooks.{h,cpp}`, wired in `src/rb_blitz_app.h` + root `CMakeLists.txt` | a boot logs `settings_bridge: typed guest channel on (namespace 'rbbz-settings/', forward-only off)` |
| The authored definition (3 panels, 3 screens, the four row kinds) and the two retail edits | `src/ui/settings_screen.{h,cpp}` | `ctest settings_screen`: **210 checks** |
| The definition compiler | `src/ui/dtb_author.*` | `ctest dtb_author`: 37 checks |

Two of the four `read/edit/ack` shapes are exercised by the definition itself: every request path the
authored screen writes is parsed by the bridge's own grammar in a test, so the seam between the two
modules cannot drift silently.

### 12.2 What a boot proves — and what it does not

| Observation | Evidence |
| --- | --- |
| The authored definition loads: `[include] rbbz/advanced_settings.dta` added to `ui/gen/init.dtb` (2045 → 2079 bytes) | armA/armB, healthy boots |
| The new row is inserted into the live main menu and routed: the retail menu draws **ADVANCED SETTINGS** between HELP & OPTIONS and DOWNLOAD CONTENT | `shots-boot2/01-menu.png`, read back by OCR |
| Its **label resolves through the locale**: the text comes from our appended entry in a shadowed `ui/locale/eng/gen/locale_keep.dtb` (76726 → 77464 bytes, 14 keys) | same capture — the row is not a raw symbol |
| A **screen of ours opens** when it focuses a retail panel: `{new UIScreen advanced_settings_screen {panels background_night_panel shell_music_panel menu_cache_panel options_panel} {focus options_panel} {back_screen splash_screen} {enter ...}}` — retail `options_screen`'s members verbatim, only the name changed | armF: **no violation**, `shots-armF/01-screen.png` taken |
| A screen focusing a **panel of ours** crashes: `Unhandled guest access violation: write of guest 0x00000200 on thread 0xF8000028` (a field write through a null pointer) | armA/armB/armG/armH/armI/armJ |
| It is **not** the row symbols: the same panel carrying retail `options_panel`'s own rows (`tutorial controls calibration …`) crashes identically | armH |
| It is **not** the list or the focus: a panel with `{file options_menu.milo}` and nothing else — no `{focus}`, no `{enter}` — crashes identically | armI |
| It is **not** scene reuse: the same empty panel over a *different* retail scene (`pause.milo`) crashes identically | armJ |
| Loading the panel explicitly does not change it: `{advanced_settings_panel load}` inserted into `ui.dtb`'s init (and the sub-panels too, chained) still crashes | armE/armG |

### 12.3 The two questions the bisection answered, and the two bugs it found

**Answer 1 — the delivery site decides whether a panel of ours is usable.** The same declarations
work when they ride **inside a retail record** and crash when they sit in **our own record reached by
`[include]`**:

| Delivery of the same panel + screen | Result |
| --- | --- |
| Appended to the retail `ui/options_menu/gen/options_menu.dtb` (2871 → 11422 bytes) | **opens**, no violation, `shots-armM/02-screen.png` shows all four rows |
| Our own `ui/rbbz/gen/advanced_settings.dtb` + `[include] rbbz/advanced_settings.dta` | `write of guest 0x00000200`, every arm |

Moving the include around inside `ui/gen/init.dtb` does not change it (`after options_menu/options_menu.dta`
still crashes), so this is not include *order*. What it means for the product is the route the parent
plan §5.2 already preferred: **generate the patch record by extending an installed retail record**,
rather than adding a record of our own. Our root `[include]` is dropped in that route.

**Answer 2 — two real bugs in the authored definition, both found by measurement.**

1. **The button table nested a second switch.** The helper that produced the retail Start/Cancel cases
   returned a whole `{switch action ...}` and it was spliced in as a *child* of the panel's switch, so
   its cases were **never evaluated**: Start and Cancel did nothing, while a dump of the file still
   looked correct. Fixed by returning *cases* and splicing them into one table; the regression test
   asserts each panel has exactly one `{switch ... action ...}` whose `kAction_Start` and
   `kAction_Cancel` are direct children.
2. **The list's selected symbol lagged the row it drew.** Measured: the first row was highlighted and
   Confirm ran the **second** row's action (`set graphics.resolution`), with the first row's
   (`set interface.hide_hud`) never running — a list keeps its selected symbol across a `set_data`.
   Fixed with retail's own idiom, read out of `song_select`, `cheat`, `recommendations` and
   `calibration` rather than guessed: `{<list>.lst set_selected 0}` paired with every `set_data`, and
   pinned by a test.

### 12.3b The round trip, verified by hand, and the Back defect

**Owner-run manual test (2026-10-11).** The owner booted the payload and changed **Hide HUD** on and
off from the controller. That is the same trip the host log records (`set` -> `commit` -> re-read
returning the new value), so the typed read/edit/ack round trip is confirmed by a human on the real
pad, not only by a scripted route.

**Back leaves towards the wrong screen** (owner-observed: from the screen the retail scene titles
"HELP & OPTIONS", Back lands on **credits**). The route our definition asks for is
`{ui goto_screen {advanced_settings_screen get back_screen}}`, and our screen declares
`{back_screen options_screen}` - so the destination is a retail screen the *shared list* also
belongs to. The likely cause is that **`options.lst` is not ours**: our panel and the retail
`options_panel` load the same scene (`options_menu.milo`) and drive the same list object, so the two
panels overwrite each other's rows and selection, and the retail options screen then dispatches on a
selection our 4-row list left behind (its own last row is `credits`). This is the parent plan's
"appearance parity still requires a spike" item arriving as a *correctness* problem: this screen needs
its **own list resource** (and probably its own scene) rather than a retail panel's. The owner has
deferred that to a scope change, so it is recorded here rather than worked around.

### 12.4 Reproducing

```powershell
python out\_swap\p1c\stage.py                       # read the retail records (never writes them)
out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe emit-screen out\_swap\p1c\def_retail.dtb retail
python out\_swap\p1c\make_arm.py armF-retailctl --defs def_retail.dtb --ui retail
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\p1c\run.ps1 -Label armF -Route screen -Payload armF-retailctl
```

`emit-screen <out> <variant>` knows `full`, `bare`, `barefile`, `otherscene`, `retailrows`,
`screenshare` and `retail`; `make_arm.py --<slot> <file|retail|->` fills any of the five record slots,
and records what it chose in its own output, which is what made the "this arm had no `ui.dtb` at all"
mistake visible instead of silent.
