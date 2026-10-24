# Engine diagnostics — the probe and the UI-state observer

Two runtime instruments. **The probe** reports *in what order the guest reached the points I care about, and with what arguments* — no debugger, no instruction trace, no reading guest memory. **The UI-state observer** reports *what changed* when the game is driven to a named screen — from a capture, and it refuses the answers that have already lied to this project. Both sit behind [plans/customization-plan.md](../plans/finished/2026-10-03-customization-plan.md): the probe is P2 (verification: "the instrument works on a real path"); the observer is P3, contract 3, and the thing E1's baseline set is made with. The observer joins scripts that already existed; the probe is the other half, for the guest's internals.

## At a glance

- **Probe.** A bounded ring of `probe_trace_capacity` events (256 default), newest-window only, written by one-line points inside hooks.
- **Probe — run.** `--probe_trace=1 --probe_trace_path=<file>`; a new point is one `REX_HOOK_RAW` containing `RBBLITZ_PROBE(0xADDR, "subsystem.thing")`.
- **Probe — output.** The trace file (rewritten wholesale ~twice a second, oldest first) and the live ImGui overlay (`probe_overlay`).
- **Probe — limits.** A point exists only where a hook does; the presenter's paint rectangle is not exposed by the SDK; not a profiler; never touches guest memory.
- **Observer.** `scripts/observe_ui.ps1` over five existing scripts, with **one table** of state names, routes and needles in `scripts/ui_states.ps1`; run `-ListStates`, or `-State <name> [-Ocr] [-BaselineDir <dir>] [-Compare <dir>]` (here with `powershell -NoProfile -ExecutionPolicy Bypass -File ...`).
- **Observer — output.** One JSON report on stdout and a PNG capture; a set goes to `out/observations/baseline/<build>/` (PNG + JSON per state, plus `summary.json`).
- **Observer — limits.** A pixel diff is evidence only against a same-build baseline; animated screens have no settled frame (OCR controls, the diff corroborates); `-MaxDiffPercent` (default **1.0**) covers settled states only.
- **Store probe.** `--store_trace=1` logs the store paths the guest asks the game root for (`src/store/store_probe.cpp`), and `--store_local_server_root=<dir>` points the retail store's netcache at a local server for one boot; `scripts/probe_store.ps1` drives and captures the whole measurement (§ Part 1, "The store probe").
- **Store failure probe.** `--store_api_base=http://127.0.0.1:<port>` moves the list/search requests to a loopback stub (`scripts/store_stub_api.py`), which refuses every request while a flag file exists and serves a captured list page once it is gone. `scripts/probe_store_failure.ps1` runs the whole measurement in one boot: press → the retail connection popup and a press-asked walk, press again → the same, lift the flag → the next press opens the store (RhythmVerse plan P9-10). Plain http is accepted **only** for a loopback host; the transport's rule and `SetApiBaseOverride` share `IsLoopbackHost`.
- **Cost.** A capture run measured **46 s**; a compare run **154 s**, mostly `frame_diff.ps1`'s per-pixel PowerShell loop — a research tool, not a `ctest` target. A `probe_store` boot measured **~2 min** end to end (title, menu, store, return, log census) on this machine.

## Sources

| Part | Original document | Status |
| --- | --- | --- |
| Part 1 — The probe | `docs/engine/diagnostics.md (part 1, the probe)` | Merged into this document (Part 1) |
| Part 2 — Observing a UI state | `docs/engine/diagnostics.md (part 2, the observer)` | Merged into this document (Part 2) |

## Part 1 — The probe (was `docs/engine/probe.md`)

Code: [src/diag/probe.h](../../src/diag/probe.h) (interface), [probe.cpp](../../src/diag/probe.cpp) (cvars, the ring, recording), [probe_overlay.cpp](../../src/diag/probe_overlay.cpp) (the panel), [probe_trace.h](../../src/diag/probe_trace.h) (the ring, SDK-free, unit-tested by `tests/probe_trace_tests.cpp`), [probe_points.cpp](../../src/diag/probe_points.cpp) (the points that exist today).

### The three things it is

1. **A ring of events.** `probe_trace_capacity` events (256 by default) in a bounded, thread-safe buffer; the oldest are overwritten and counted as dropped, so it always holds the *newest* window — what a screen transition needs.
2. **A point.** One line inside a hook:

   ```cpp
   REX_HOOK_RAW(VorbisReader_CheckHmxHeader) {
     RBBLITZ_PROBE(0x82768C88, "mogg.header");
     __imp__VorbisReader_CheckHmxHeader(ctx, base);
   }
   ```

   `RBBLITZ_PROBE(address, tag)` records the address, a short tag, the calling thread, the guest return address (`lr`, i.e. who called it) and `r3..r10`; `RBBLITZ_PROBE_REGISTERS(tag)` does the same for a point with no address of its own, and `RBBLITZ_PROBE_MARK(tag)` is a breadcrumb from host code (a boot milestone) that lands in the same ordered trace.
3. **A view.** An ImGui overlay (below), and a trace file a script can read. It is not a profiler, it cannot see a call that has no point on it (a hook is a compile-time override of one function, so the trace is exactly the points you placed), and it does not touch guest memory.

### Using it

| cvar | Default | What it does |
| --- | --- | --- |
| `probe_trace` | `false` | Records points into the ring. Off, a point costs a predictable-not-taken branch. |
| `probe_trace_capacity` | `256` | Ring size (min 16, max 1048576). Raise it to hold a whole transition; watch `dropped` in the file header or the panel. |
| `probe_trace_path` | `""` | When set, the newest events are rewritten to this file about twice a second, oldest first. This is the scriptable output. |
| `probe_overlay` | `false` | Draws the panel. **Live**: it is re-read every frame, so it can be switched on mid-run with no restart. |

All four go in the same `<exe name>.toml` as the other settings (see [build-and-run.md](../guides/build-and-run.md)), or on the command line, which is what a script uses:

```powershell
.\rb_blitz.exe --game_data_root=..\..\..\game --fullscreen=0 `
  --probe_trace=1 --probe_trace_capacity=8192 --probe_trace_path=probe-trace.log
```

The file is rewritten wholesale, so **the header line is the state of the run**, not of the file: `# probe trace: 1810 event(s) held, 0 dropped, 1810 recorded since the last clear` says the ring never wrapped; a non-zero `dropped` means you are reading only the tail — raise the capacity, or narrow the trace. A line is `<seq> <seconds> <thread> <address> <entry|mark> "<tag>" lr=<caller> r3..r10`:

```text
#000002 +    0.000s tF8000028 0x823DE0C0 entry "mogg.hv_decrypt" lr=0x82768E08 r3=... r10=...
```

The overlay adds the guest's **requested** video mode (`resolution` preset, refresh rate), the present path (`present_letterbox`, `present_safe_area_x/y`, `present_effect`, dither, overscan), window physical/logical size, fullscreen and DPI scale, and the ring's state (held / dropped / total).

#### Two standing limits the panel states rather than hides

- **The presenter's own paint rectangle is not exposed by the SDK** — `rex::ui::Presenter` has no public accessor for it, so the overlay prints `(not exposed by the SDK)` instead of a plausible number: a probe that reports a guess is worse than one that reports nothing. A later change can add an SDK patch that exposes it, or read the value the mouse driver already measures.
- **A point only exists where a hook does.** Nothing is auto-discovered: `probe_points.cpp` places the points, and a new one is one line.

### What is wired today, and why only that

[probe_points.cpp](../../src/diag/probe_points.cpp) carries the four named functions of the MOGG path — the only guest path this project has proved end to end. A real trace, vanilla (`--ultimate_mode=0`; the Ultimate run gives the same order and census, and per-event times vary by tens of ms run to run — the order and the counts are the evidence):

```text
#000001 +    0.000s tF8000028 0x82768C88 entry "mogg.header"
#000002 +    0.015s tF8000028 0x823DE0C0 entry "mogg.hv_decrypt"    lr=0x82768E08
#000003 +    0.015s tF8000028 0x823DE070 entry "mogg.enc_method"    lr=0x823DE0E4
#000004 +    0.056s tF8000028 0x82768AD0 entry "mogg.setup_cypher"  lr=0x82768E48
```

Three facts fall straight out of it, none guessed: `lr=0x82768E08` on `hv_decrypt` is inside `CheckHmxHeader`'s body (`0x82768C88`) — [symbols.md](../reference/symbols.md)'s "only caller" claim measured rather than read; `lr=0x823DE0E4` on the first `enc_method` is inside `HvDecrypt` (`0x823DE0C0` + 0x24) — the key is selected by the function that installs it; and the census (1,806 `header` entries against 1 `hv_decrypt` and 1 `setup_cypher`) corrected the rate [symbols.md](../reference/symbols.md) recorded for `CheckHmxHeader` (see its MOGG row).

**The menu path is deliberately not wired yet** — a point needs an already-identified function, and the main menu's have no names; finding them is S2's work in [plans/customization-plan.md](../plans/finished/2026-10-03-customization-plan.md), and the probe is what it does it with. So P2's verification is "the instrument works on a real path", not "the title → main menu trace exists" — that trace is S2's output.

### The gameplay-seam probe (P0, 2026-10-18)

[gameplay_probe.cpp](../../src/diag/gameplay_probe.cpp) is a separate, narrow instrument for the
controller-to-action seam of [plans/finished/2026-10-17-gameplay-input-mapping-plan.md](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md) §10. It hooks the guest controller
factory, `ButtonDown`/`ButtonUp`, the button→action lookup, the action dispatch and release, the
per-frame `JoypadController` update, and `Game::update_controls`; it logs each in the release
`core` log instead of the trace ring. The cvars are registered here because they are not
enhancement toggles, and each also accepts the `REX_<UPPERCASE>` environment override:

| Cvar | Default | What it does |
| --- | --- | --- |
| `probe_gameplay` | off | Enables the logs: controller creation/destruction, the lookup result per button, down/up, and every entry into the action dispatch/release with its action and arg. |
| `probe_gameplay_bypass` | off | Makes the button→action lookup return 0, so the native pad produces no dispatch. The direct actions still run. |
| `probe_gameplay_inject` | off | Runs a timed direct-action sequence from the per-frame controller update: Lane0, TrackUp and TrackDown (gated), Lane1, UsePowerUp, with lane/power-up releases. **It dispatches real actions into the live song**, so it is a P0/P3 arm, never a menu-time diagnostic. |
| `probe_gameplay_inject_track` | off | Adds the TrackUp/TrackDown pair to the sequence. Kept separate because a direct track action starts the controller's own held-action repeat, which runs until the same action is released (`sub_824367E0` clears the slot). |
| `probe_gameplay_block_dispatch` | off | Suppresses **every** native entry into the action boundary while the song controller exists — press, release, the symbol message producer, the keyboard controller and the held-action repeat — so one run can show all four producers blocked. Direct probe actions still dispatch. |
| `probe_gameplay_challenge` | off | Drives the two dormant producers (`sub_82436A28`'s symbol message and `sub_82442B30`'s keyboard controller) from guest-stack fixtures the probe builds, rather than from a fabricated return address. Pair with `block_dispatch` for the before/after pair. |
| `probe_gameplay_sources` | off | Logs raw SDL keyboard/mouse state beside the pad (G/H = notes, N/M = tracks, mouse buttons = notes) and dispatches on its edges. The arm that measures source provenance; it has **not** been run yet (plan §10.5, O5). |
| `probe_gameplay_inject_spam` | off | Replaces the fixed sequence with an alternating Lane0/Lane1 press-release loop starting 8 s into the song, for the judgement proof (the HUD score rises) rather than a handler-only check. |

The matching harness is `scripts/probe_gameplay.ps1`: it boots the release build with `--mnk_mode=1`,
waits for the guest's own title by OCR before sending any key, drives the menu to a song, presses
native pad controls and/or runs an injection arm, and keeps the combined log and shots. Every menu
tap carries its own `wait:1` — the guest drops a run of taps with no wait between them — and the
window is never started hidden, because a hidden window cannot be captured or driven. The scratch
driver `out/_swap/p0/run.ps1` carries the later switches that have not been folded back.

### Adding a point

1. Pick a function that is already named, or an address proved by hand ([symbols.md](../reference/symbols.md) is the table).
2. Add the override to [probe_points.cpp](../../src/diag/probe_points.cpp), with the guest address in the macro and a `subsystem.thing` tag:

   ```cpp
   REX_EXTERN(__imp__sub_XXXXXXXX);
   REX_HOOK_RAW(sub_XXXXXXXX) {
     RBBLITZ_PROBE(0xXXXXXXXX, "menu.screen");
     __imp__sub_XXXXXXXX(ctx, base);
   }
   ```

   The override is a pass-through: the original body is `__imp__<name>`, exactly as [ultimate.cpp](../../src/hooks/ultimate.cpp) reasons about its own overrides.
3. Run with `--probe_trace=1 --probe_trace_path=<file>`, and read the file.

**A caution about hot points.** `CheckHmxHeader` is entered ~65 times a second while the title music plays, so a point on a per-frame or per-packet function fills a small ring in seconds. That is what `probe_trace_capacity` is for; it is also why the tag census, not the raw order alone, is the first thing to read.

### The store probe (P2, 2026-10-20)

[store_probe.cpp](../../src/store/store_probe.cpp) is the RhythmVerse store plan's data-layer
instrument (§5/P2). It answers a narrower question than the trace ring: **which store paths does the
guest ask the game root for, and does the read succeed** — the offer/metadata names are otherwise
the guest's own strings, and no log line names them.

| Cvar | Default | What it does |
| --- | --- | --- |
| `store_trace` | off | Logs one line per distinct store path the game root resolves or opens, with the open's access and result. The filter also matches any path ending in `store.dtb`, so the netcache definition itself is covered. **It no longer decides whether the store works** (2026-10-22): P3's hook used to force the panel's readiness word (`0x82853D4C`) only while this switch was on, so every store boot that drew a catalogue was a trace boot; the write is now tied to the RhythmVerse master switch, and this one only logs - plus forces it for a diagnostic boot whose profile has the switch off. |
| `store_local_server_root` | "" | Rewrites `config/gen/store.dtb`'s `netcache_init` at boot so the store's default server is a *local* (host-file) one rooted at this guest-relative directory. Empty leaves the record as shipped. Requires a restart (it is built into the generated payload). |
| `store_search_seed` | "" | Seeds the search field with this text at boot, so one **Search** press exercises the real request and answer (`scripts/probe_search.ps1 -Query <text>`). Empty leaves the field empty. It fills the field **without opening the search surface** (2026-10-22): the seed used the typing path, which set the surface open, so a seeded boot drew `Search: <query>` over the store's own screens. Since P9-11 the seeded boot also **skips the keyboard**: the store screen's `open_store_search` asks `rbbz-store/search/seed` (`src/store/store_search.h`) and writes the submit instead of drawing the title's keyboard, which a script cannot type on (`scripts/acceptance_store.ps1`, `scripts/probe_search.ps1`). A player's boot never has a seed, so the keyboard is what they always see. Requires a restart. |
| `store_serve_dir` | "" | RhythmVerse plan P3's data lane: a host directory whose files answer the store's own `dlc_store/**` netcache requests (a file named exactly like the requested path, e.g. `dlc_store/europe/dlc_top_xbox_eng.dta`). Empty uses the built-in records. A `.dta` record is **text** - the insert parses that type with the engine's own text reader - and is inserted into the manager's loaded-file container. Requires a restart. |
| `store_fetch_deliver` | **on** | P3's delivery half: leaving the file cache's readiness word (`manager + 44`) set after a served record makes retail's **own** request body take its "already loaded" branch and send the `file_req_complete` message (`sub_8235A0F8`) itself - one delivery per request, every layout below it the engine's. Off, the word stays as the boot found it and the body takes its fetch branch, exactly as retail does offline. On by default since 2026-10-21: the store has nothing to show without it, and only records this module serves are affected (serving itself is gated on the RhythmVerse master switch), so a stock boot is untouched. Measured on: the store draws the served front menu's rows (RECENT, SEARCH), a row press asks for `dlc_store/<region>/<locale>/<name>/`, the served page opens the retail browser with all 24 titles drawn and the details panel reading the offer's own artist and genre, and A opens the details screen (`out/_swap/store-probe/20261021-190304`). Requires a restart. |
| `store_catalogue_off` | off | Turns the store catalogue fetch off: a menu page is then served **empty** (the shape the retail DTA handles by refreshing the menu), which is the control leg for "is this page the catalogue's or the writer's?". Off means the catalogue runs. Requires a restart. |
| `store_api_base` | "" | The **failure measurement's** instrument (RhythmVerse plan P9-10): an absolute `http(s)://host[:port]` origin with **no path** that the list and search requests are built against, so a boot can be pointed at an origin that cannot answer without touching the machine's network. Empty keeps `https://rhythmverse.co`. It moves **only** list/search - the download and artwork resolution stay same-site with `rhythmverse.co` (the §3.1 policy), so a diagnostic boot cannot be talked into fetching a package from elsewhere. A value that is not a bare origin is refused at install and reported (`store_api_base '…' is not an absolute http(s) origin`); `http://` is accepted **only for a loopback host** (`127.0.0.1`, `::1`, `localhost` - `IsLoopbackHost`), which is the one plain-http exception the transport makes (`store_http.cpp`; the package transfer in `store_transfer_winhttp.cpp` stays HTTPS-only). `scripts/store_stub_api.py` is the stand-in it is meant to point at: it answers `503` while a flag file exists and a captured list page otherwise, so one boot can measure a failed press, its popup, and the later success (`scripts/probe_store_failure.ps1` drives all three). Requires a restart. |
| `store_catalogue_ttl_seconds` | 300 | How long the fetched catalogue stays fresh before a new store visit refetches it (Recently Added's own freshness, separate from the page cache below). The list is **warmed once per boot**, before the guest thread starts (P9-10, `store_catalogue: fetching … (the boot asked)`), so a store visit normally finds it fresh and the TTL is what decides when a later visit re-fetches; a press inside a **cold** fetch's first ~3 s is answered with the empty page (the DTA refreshes the menu), which is why the prewarm exists. A failed page backs off for a wait derived from the answer (P7): `Retry-After` honoured within 5..600 s, a 5xx 60 s, a transport failure 10 s, a `200` whose body could not be read 60 s and any other status 120 s. The fetch runs on its own thread, so a store visit never blocks the guest. Requires a restart. |
| `store_catalogue_pages` | 4 | How many API pages one store action accumulates into the browser's single list (clamped 1..20). Retail has no paging - `song_select`'s PageUp/PageDown are screen transitions - so an offer past the first API page can only be reached by asking for those pages. The walk is **chunked on the server's own cursor** (P7): the offers are published after each page, a page the instrument/eligibility filter emptied is **not** treated as the last one, and the budget is the only bound on a catalogue that never says "done". A later page that fails keeps the pages already read and holds the next visit off for its backoff. Measured 2026-10-22: `page 1..4 -> 24,25,25,25 eligible offers (99 total)`, and row 70 of the served list was an API **page 4** offer. Requires a restart. |
| `store_page_cache_ttl_seconds` | 300 | RhythmVerse plan **P7's in-memory page cache**: how long one fetched API page (25 records) is reused before the site is asked again; **0 switches the cache off**, so every visit asks the site. The key is the endpoint **and the form body**, so the query text, the page number, the page size and the instrument filter each identity a page - a settings commit cannot read a page fetched for the filter it replaced. Single flight spans Recently Added and Search: an identical page that is already being fetched is never requested twice. The cache holds at most 64 pages and evicts the oldest. There is no disk cache (the plan marks `<user_data_root>/store/cache/` optional; it stays unbuilt). Requires a restart. |
| `store_search_pages` | 4 | How many API pages **one search** may read (clamped 1..20; the site's page size is 25 records) - P7's per-action budget for the search half of `store_catalogue_pages`. The walk continues past a page the eligibility/instrument filter emptied and stops on the server's cursor, the budget, or a newer submit/edit/cancel/settings commit (the cancellation the worker checks between pages). 1 is the pre-P7 behaviour. Requires a restart. |
| `store_search_wait_ms` | 4000 | How long the **search page's own request** waits for the RhythmVerse answer before the page is written (`src/store/store_fetch.cpp`). Confirming the title's keyboard submits the query **and** asks the store for the page in the same breath (`src/store/store_payload.h`), and the retail browser only opens when the page carries offers (`store_menu_panel.process_offers`), so the request holds until the first API page of the answer lands. The wait is bounded and only paid on a query the page cache does not already hold; measured 2026-10-23 on `bush`: `the search page waited 2730 ms for the answer`, then `15 offer(s)` served and `store_browser_screen` was the current screen 0.2 s later (`out/_swap/search-probe/20261023-175633`). 0 writes the page immediately - an empty list, which the store answers by staying on its menu. |
| `store_offer_type_symbol` | **on** | Serves each offer's `type` as a DTA **symbol** (`(type song)`) instead of a string. The symbol is what the retail provider compares `get_data("type")` against (`sub_8275A230` -> `sub_822D1CE0`) to answer a browser row's label, and with it on the log shows every title being read (`store-probe: offer name exit '<title>'`). It is the shipped default since 2026-10-21: the null read it used to trigger was the details panel's `(rank <part>)` path, which the payload now unrolls onto the record's `rank_<part>` keys (`src/store/store_payload.h`). Off is the pre-P3 blank-row behaviour. Requires a restart. |
| `store_front_menu_row` | `no-count` | How a served front-menu row spells its count (`src/store/store_metadata.h`'s `FrontMenuRowForm`). The front-menu list's provider (`sub_8275CD58`) formats the row's **third** element through the `count` label, and `sub_8275C9D8` skips that branch - handing the label an empty string - only when the row's **fourth** element is the symbol `random_song` (retail's own "no count" marker). `no-count` serves `(path label TRUE random_song)`, which is why the front menu draws **no digit** beside Recently Added and Search (P9-5); `count` is the pre-P9-5 `(path label TRUE)`, which drew `1`, and `false` / `zero` spell the same count as `FALSE` and `0`. Requires a restart. |
| `store_art_off` | off | RhythmVerse plan P3's album art: with it on, the offer records carry no `preview_art`, the retail panel keeps its own `cover_art_none.tex` placeholder, and the cover worker does not fetch anything. Off (the shipped default) serves a `.png_xbox` texture per offer from `src/store/album_art_service.cpp` - the API's `album_art` cover, decoded and encoded by `src/assets/texture_codec.cpp` - through the netcache entry `BandStorePanel::load_art` reaches (`sub_82469C28`). Requires a restart. |
| `store_art_dump_dir` | empty | The art worker's scratch directory, for the "the cover is wrong on screen - is it the file or the engine?" question. Every encoded cover is written there as `<key>.png_xbox`, and **a file already present for a key is served instead of being fetched**, so a texture encoded in an earlier boot, a hand-made one, or a shipped `.png_xbox` can be pushed through the exact serving path. Decode a dump with `python scripts/hmx_tex.py export <key>.png_xbox cover.png`; empty (the default) writes nothing and reads nothing. Requires a restart. |

The observation point is [fs::PayloadOverlayDevice](../../src/fs/payload_overlay.cpp), not the SDK:
every `game:`/`d:` path resolves through that device, so the hook is a single function pointer that
costs a null check when off, and the paths arrive overlay-relative (`ui/store/...`).

**The store's own sentences (P7).** What a player reads about the store is not a cvar: every store
state change is published to [store_catalogue_status.cpp](../../src/store/store_catalogue_status.cpp),
which the caption overlay draws below the search strip. The credit line
**`Customs from RhythmVerse`** follows the front menu's own serve (the heading itself is
`RHYTHMVERSE`, because `CUSTOMS FROM RHYTHMVERSE` clips in the retail header - measured 2026-10-21),
the empty state says where the instrument filter lives, the loading line counts the pages read so
far, and a failed fetch carries the server's own reason. Since the boot warms the list (P9-10,
2026-10-22) the loading line is normally not drawn on entry: no fetch is running when the store
opens, and the press gets the offers (measured: the front menu served at 19:53:34, the Recent page
75,229 bytes at 19:53:55, `out/_swap/p910-recent2/20261022-195200`). It reappears while a fetch
runs — after a failure whose backoff expired, or inside a cold fetch's first seconds. Measured on
screen 2026-10-22
(`out/_swap/store-probe/20261022-140259`: the credit line is drawn under the store menu's RECENT /
SEARCH rows). All of it is drawn only within
`kCatalogueSurfaceWindow` (20 s) of the store serving a record, so the strip cannot follow a player
out of the store, and it is drawn with Settings Labels off - an empty or unreachable catalogue is a
state a player cannot explain from the screen.

**The store's fault logger and its map.** `store_trace` also installs the first-chance handler that
turns a guest null read into a line
(`store-probe: fault read of 0x… at rip 0x…, frames: …`, then the SDK's own report). The frames are
**host** addresses, so they are only names when the link map comes from the same link as the binary
that booted - a map one build old named nothing real and cost this plan a diagnosis
(measured 2026-10-22). A measurement build asks for the map explicitly:

```powershell
cmake --preset win-amd64-release -DRBBLITZ_LINK_MAP=ON
cmake --build --preset win-amd64-release --target rb_blitz
python out/_swap/fault_lookup.py --base <the module base the boot logged> <frames…>
```

The option adds `-Xlinker /MAP` to the game target only (`CMakeLists.txt`), so the tool targets are
untouched; it is off by default and a normal build carries no map. The store's own art path has two
extra trace lines for the same class of question: `store-probe: art request manager=… client=…
path='…'` (every request for a store texture, served or not) and the completion's `wrapper=…[…]`
words (the file object whose bytes the decode reads).

**What a silent probe does *not* prove (measured 2026-10-20).** Records read from an `.ark` are
never seen: the guest mounts the ark part and reads records out of it **by offset**, so no path is
resolved for them at all. A trace-level boot of this title is 30 `NtCreateFile` lines in total — the
two ark headers + parts and a handful of special files — and no `ui/store/**` or `dlc_store/**` path
ever appears; the store's own UI records are in that invisible class. The probe is therefore
right about *loose files a served store needs* and must not be read as "the guest asked for nothing":
for that question the noisy export log is the instrument below.

`scripts/probe_store.ps1` drives the whole measurement in one boot: title → main menu → a chosen row
→ the store → optionally its first menu row, keeping a screenshot and OCR of the menu, the *selected*
row, the store and the return, plus the store-probe census and the run's log
(`out/_swap/store-probe/<stamp>/`). `-MenuOnly` stops at the menu, `-LocalRoot ""` is the control
leg (retail server), and `-StoreRow` is a parameter because the menu's rows depend on three settings
— run `-MenuOnly` first and read `menu.txt`. `-PageRow`/`-OfferRow` walk one level deeper each (a
menu page, then a details page), and **`-InfoRow` presses a button on that details page** and waits
`-DownloadWaitSec` (default 40) before the last capture — that is the P5 route: `-StoreRow 3 -PageRow
0 -OfferRow 0 -InfoRow 0` is Recent's first offer's Download row. The report's
`--- download / refresh lines ---` section carries `store_download:`/`store_install:`/`dlc:`
lines, which is where a started, installed and refreshed transfer shows up. Since 2026-10-22 the
install line names the job it finished — `store_download: job <job> installed '<Artist - Title>'
<path> (<N> bytes, title <id>)` — which is what lets a later boot say *which* song it must find
on the title's list (the receipt stays a transfer history with no name in it, by design).

**The search route, end to end (P9-11, 2026-10-23).** `scripts/probe_search.ps1 -Query bush
-SearchMenuRow 1 -StoreRow 3` walks the shipped path: main menu → **RhythmVerse** → the store's
front menu → its **Search** row (RECENT is 0, SEARCH 1) → the **title's own keyboard**
(`virtual_keyboard show_keyboard`, the object the Ultimate payload's song search drives; retail's
does not). On a seeded boot (`-Query` fills the field) the same press submits instead of drawing
the keyboard, because the screen asks the `rbbz-store/search/seed` bit, so one press exercises the
request; the caption strip then reads `15 MATCHES FOR "BUSH". PRESS SEARCH TO LIST THEM.`, and the
same row asks for the page the answer fills. `-RowOnly` is the control leg that presses the row and
captures what it opened without submitting (`-SearchMenuRow 0` is Recently Added).

**The keyboard's record (P9-11, measured 2026-10-23).** The class is native and in the
**retail** image: `.?AVVirtualKeyboard@@`'s vtable slot 5 is `sub_8237AD10`, whose literals are
`show_keyboard` and `clear_callback`; `.?AVVirtualKeyboardResultMsg@@`'s symbol table carries `ok`
and `text`, which are the two values the DTA handler binds. The boot init `sub_823631C0` names the
class's single instance `virtual_keyboard` in the object directory, so the call is
`{virtual_keyboard show_keyboard $user 100 (localize <title>) (localize <desc>) "" $this}` — with a
**User object** in the first slot, because the native show (`sub_8237B008`) reads the player's name
from it (the bare symbol `user` was a null read, measured in game: `read of guest 0x00000000` right
after the press). The result arrives as `(virtual_keyboard_result_msg ($ok $text) …)` on whatever
object `$this` was — the lowercase spelling `sub_8237A878` interns, which `macros.dtb`'s
`VIRTUAL_KEYBOARD_RESULT_MSG` expands to. The
payload writes both methods onto `store_menu_screen` (`store::AddSearchKeyboard`), so the message's
receiver is the screen that opened it: `{sprintf "rbbz-store/search/text/%s" $text}` carries the
query to the host and `{write_file "rbbz-store/search/done" 0}` submits it. Confirming the keyboard
then asks the store for the search page in the same body (the shipped row's own `set_source` +
`request "/eng/search/"` + `update_loading_status`), so the results open in the retail browser with
no second press; the host holds that request until the first API page lands (`store_search_wait_ms`
below). The bridge reads
printable UTF-8 in a path, so a typed accent survives; a control character is refused by the
session, not by the reader. `rb_blitz_store_payload_tests store-edit <store.dtb> search` prints
both methods, the routed `SELECT_MSG` and the panel `exit` that dismisses the strip, against the
shipped record - which is how the edit was checked before any boot.

**The details page's credit block (P9-13, 2026-10-23).** The box that carries the Download rows
draws the **custom's** own facts, written by `store::ShowOfferCredits` into labels the shipped
`store_info_panel.refresh_panel` already owns: `drm.lbl` the artist, `rating.lbl` the audio type
(hidden when the catalogue publishes none), `rating_description.lbl` `Charted by <name>`, and
`cost.lbl` `Released at <date>, <N> Downloads` (the scene's last free row; the audio type and the
charter hide rather than draw an empty line, and the cost row hides when there is neither a date nor
a count). The values are record keys: `audio_type` is already the **label** (`AudioTypeLabel`'s
table; the raw code never reaches a record), and `charter`/`release_date`/`downloads` are the
file's own facts. The block is one `{do ...}` appended to the method, after `refresh_panel`'s hides
and after `HideStoreInfoPriceChrome`'s, which is why the payload wires it after that transform.
`rb_blitz_store_payload_tests store-edit <store_info.dtb> info` runs it against the shipped record
and dumps the whole transformed `refresh_panel`, which is how the edit was checked before any boot;
the owner read the rendered block in game (2026-10-24), which is what closed the prompt.

**The download popup (P9-6a, 2026-10-24).** P5 drew a transfer's progress and outcome as a host-side
caption line; the owner's 2026-10-23 pass replaced it with one **modal** panel, in the shape the
search loading panel already proves. `store::AddDownloadPopup` defines `rbbz_download_panel` in
`store_info.dtb` - the retail error popup's resource (`../error/error_general.milo`), `error.lbl`
for the sentence and `continue.lbl` for the prompt - and lists it last on `store_info_screen`, so
its `all.grp` draws over the details page. `show_popup` sets the loading sentence
(**Downloading from RhythmVerse...**) and the Cancel prompt and takes the screen's focus; `poll`
reads the job the press was made on (`{store_info_panel get cur_offer}` -> `(job)`) and asks the
bridge for `rbbz-store/state/<job>/installed|failed|cancelled`; the first terminal answer switches
the same panel's labels (`Downloaded '<Artist - Title>'.` + Close, or the generic failure sentence)
once, guarded by a `terminal` property. **B** writes `rbbz-store/download/cancel/<job>` while the job
is running and closes, so a cancelled transfer never outlives the press, and leaving the screen with
the popup up is the same cancel. No string property and no method argument carries the token:
neither shape is measured on this engine's script objects, while the offer already holds it.
P9-6b fills the loading line with the progress bucket and names a failure's reason; P9-6c turns the
installed state into the toast whose A runs the main menu's Refresh Songs. The popup calls no host
drawing at all - it is the guest's own labels. `rb_blitz_store_payload_tests store-edit
<store_info.dtb> download` prints the definition and both routed presses against the shipped record.

**The master switch's restart rule (P9-2, measured 2026-10-22).** `scripts/probe_master_restart.ps1`
flips a filter leaf and the store's master switch through Help & Options and reads what leaving does.
A leaf is applied live (`hot_apply: general.rhythmverse_guitar = false`) and asks for nothing; the
master draws the authored restart confirmation exactly once (`settings_restart_screen`, offering
`Restart now` / `Close`) and is **never** applied live; `Close` finishes the leave on the session's
pre-change value (the main menu still draws the store row); the boot makes no extra catalogue
request from the commit; and the next boot, on the file the game itself committed, has no store row
and no store request. Two boots write `<OutDir>/<stamp>/session.log` and `next-boot.log`, a
screenshot per step, and a copy of the committed `settings-after-session.toml`. `-SessionOnly` stops
after the first boot, and `-NextBootOnly` runs only the second one on the `settings.toml` the session
leg already committed (it does not rewrite the fixture, which is the point of that leg).

**The acceptance harness (P8, 2026-10-22).** `scripts/acceptance_store.ps1` runs the whole feature
in four boots and is the only script here that asserts instead of describing: `-Legs Enable` (the
master switch turned on through Help & Options > General > Downloadable Content, then the committed
`settings.toml` value), `Store` (the row opens the retail store, Recent, the search answer, its
results page, the details screen's Download - P9-6a's popup and its outcome sentence - and the
install asserted on disk, in the receipt and in
the log), `Install` (the next boot's song list carries the downloaded title, read out of the install
line above) and `Control` (RhythmVerse Off: no store request in the log at all and the main menu's
refresh row still dispatches its sentinel). It uses its own profile directories and an **empty
scratch DLC root**, so it never reads or writes the player's library, and it exits non-zero unless
every step behaved; `-Reuse` runs the legs in separate invocations on the fixtures the first one
wrote. Rows are clicked at the bounds the host logs (`native_mouse: row=<n> bounds=…`) because which
index carries a row depends on the boot's settings, and the screen a click lands on
(`native_mouse: screen=` / `help_observer: screen '…'`) is the oracle, never an OCR guess.

**Its boot route is press, hover, look (`Enter-MainMenu`).** One OCR finds the title, and after that
the only instruments are host lines: a hover at the list makes the native capture log the row table,
the menu is the only screen with one, and `content_pipeline: usable menu` names the same moment from
the pipeline's side. A press goes out only after a look that found no rows, so the retried accept
cannot land on a row; a press sent during the title's discovery pass is swallowed, which is why the
route presses again rather than reading the screen. Nothing captures while the pass runs - a boot
that is OCR'd every ~18 s sat on `server_connect_screen` for minutes with the guest idle and 585
ringbuffer-drop lines in its log, while the same boot left the pass 100-450 ms after the enumeration
with the machine to itself. That is also why the harness logs `--log_level=info`: the debug stream is
APU/GPU chatter (4.7 MB in four minutes), and every line it asserts on is an info line.

**Measured shape, 2026-10-22** (`out/store-acceptance/p8-store/`): `Enable` passes every step
(`RHYTHMVERSE: OFF` → press → `rhythmverse = true` in the profile → `RHYTHMVERSE: ON`), `Control`
passes 13 of 13 (no store row on the menu, six absence checks at 0 matches, the refresh row's
sentinel present), the store row opens the retail store (`native_mouse: screen=store_menu_screen`) -
and the `Store` leg then finds the guest no longer logging on this machine. *(Superseded the same
day: the store's silence there was a defect, not the machine - the panel's readiness word was
written only under `store_trace` - and the close-out run below is all four legs green.)* Two other
traps the harness encodes: `drive_ui.ps1` joins `-OutDir` against the repository root (pass it
relative), and OCR hands back one line per drawn line (`EXIT GAME` arrives as two), which is why
every label check folds the whitespace out.

**The closed-out run, 2026-10-22** (`out/store-acceptance/p8-final/`): **69 steps, 0 failures** over
all four legs on one build. `Enable` reads the switch off, presses it, finds `rhythmverse = true` in
the profile and reads it on (the master was off by default then; the owner pass of plan §9/P9-1
flipped that default and made the Enable leg assert it instead of pressing the row); `Store` draws
`RHYTHMVERSE` + `RECENT SEARCH` + the credit line, lists
`99 offer(s)`, opens the browser on them, answers a search (`15 MATCHES FOR "BUSH"`) and lists the
result, opens the details page's `DOWNLOAD`, installs `rv_436f1b68360f1ad9be533437be169410.con`
(4,927,488 bytes, one container in the flat root, empty staging, receipt `pending`, refresh logged)
with no points/Buy Now chrome anywhere; *(the name is the 2026-10-20/22 rule; the store names an
install `Artist - Song` since 2026-10-23 - `docs/guides/dlc.md` §2.2)* `Install` finds the title on
the next boot's own song list
(`YOUR SONGS … BUSH THE PEOPLE THAT WE LOVE` after `dlc: 1 saved-game library package(s) presented
as content type 00000002`); `Control` makes no store request at all and still dispatches the refresh
row. The assertions that changed to get there are listed in the plan's §5/P8 close-out record - the
row-label and heading needles, the credit line read after the first page lands, the shipping
`store_fetch: served '<path>'` line instead of the trace-only per-page count, and the retried Play
press in the `Install` leg.

**Naming the exports a screen calls.** `-LogLevel trace -Noisy` (that is `--log_noisy=1`) turns on
the SDK's kernel-import logging, which prints one line per export call (`[NtCreateFile] path=…`,
`[XamInputGetState] …`, `[Xe…]`) with the guest path or arguments. It is how P2 found that the
store's request is followed by no file, content or socket call at all (plan §5/P2 record) — a
question the path probe could not answer. Two cautions: the log grows to ~50 MB per boot, and the
per-frame input/swap lines dominate, so census them by export name before reading order.

**Naming a guest fault (2026-10-21).** The SDK reports an unhandled guest access violation as one
line - `read of guest 0x00000000 (host 0x100000000) on thread 0x…` - and nothing else, which is not
enough to find the code. While `store_trace` is on, [store_probe.cpp](../../src/store/store_probe.cpp)
installs a **first-chance vectored exception handler** that logs the faulting `rip`, the translated
guest address and the first 24 host frames, together with the module base:

```
store-probe: fault read of 0x00000000 at rip 0x7ff605bf70dd, frames: 0x7ff8c3aa7faa …
store-probe: fault logger installed; module base 0x7ff6058f0000; guest window [0x100000000, 0x200000000)
```

The handler only observes: it returns `EXCEPTION_CONTINUE_SEARCH`, so the SDK's own decision is
unchanged. Resolve the frames with `python scripts/map_lookup.py --base <module base> <frames…>`, which
rebases them onto `rb_blitz.map` (the build writes it beside the executable; the binary itself has no
symbols) and prints the generated function that contains each one — `--keep all` widens it to the C++
symbols, and the unwind funclets next to every generated function are always skipped. Two things it
taught, worth knowing before reading a stack: the addresses are **host** addresses (a guest zero read
is `virtual_membase + 0`), and a new thread's stack probe writes its guard page, which is an access
violation the filter has to drop — the near-null addresses are the guest's.

The same script also takes a bare map address (no `--base`), which is what an address read straight
out of `rb_blitz.map` needs.

**What the store's label fault resolved to (2026-10-21).** A fresh map (the checkout's
`rb_blitz.map` was from 2026-10-12, so the names in older stacks were stale) put
`read of guest 0x00000000` at `sub_824534A8 +0x31d`: the offer's `get_data` path walk, reached from
the offer's message handler (`sub_82453928`). Two rules came out of decoding it, and both matter when
authoring an offer record (`src/store/store_metadata.cpp`): the walk compares every path component
against the **first element of a pair**, so a multi-component path like `(rank <part>)` cannot index
a five-part map (the missing answer is dereferenced), and an **absent** key is as fatal as a wrong
one because the same tail reads the answer unconditionally. The fix - single-component
`rank_<part>` keys plus a payload unroll of the panel's loop - is in the plan's §5/P3 exit record.

**The store's native chain (P3 step 0, 2026-10-20).** [store_probe.cpp](../../src/store/store_probe.cpp)
now also hooks two guest functions, both pass-throughs that only report (and, for the gate, force a
value) while `store_trace` is on:

| Function | What it proves |
| --- | --- |
| `sub_8274FB60` | the panel's request builder — formats the metadata path and hands it to the file cache. A `store-probe: request '<path>' (input '<provider>', flag …)` line is the request list P2 could not observe from the file side. |
| `sub_82753010` | `request_index_if_needed`'s body: reports `this+52`, `this+56` and the state word at `0x82853D20+44`, then forces that word to 2 through the heap's protect API (the image's data section is read-only; a plain store does nothing). |
| `sub_8235A240` | the file-cache entry the builder hands the path to: `store-probe: cache request '<path>' (manager <ptr>)`. It separates "the panel asked" from "the cache looked". |

**Correction (2026-10-20, supersedes the first P3 step-0 run):** that run reported the state word
at `0x828A3D20+44` and forced it there, and no request followed. The address was decoded wrong —
`lis r11,-32123` is `0x8285` (65536 - 32123 = 33413 = `0x8285`), so the word is at `0x82853D4C`.
With the corrected address a boot (`rb_blitz_055.log`, 17:10:11-17:11:04; the store entry was
driven by the P4 search diagnostic's `enter` statements, but the request itself is the retail
`request_index_if_needed` path) reaches the builder and the cache:

```text
store-probe: index request reached; this+52=0x0 this+56=0x0 state=0 (forced to 2: yes)
store-probe: cache request 'dlc_store/europe/dlc_top_xbox_eng.dta' (manager 0x400019a0)
store-probe: request 'dlc_store/europe/dlc_top_xbox_eng.dta' (input '/dlc_top_xbox_eng.dta', flag 1)
```

Two facts follow: the measured substitutions are **region `europe`, platform `xbox`, locale
`eng`** (not the plan's earlier `xbox`/`en` guess), and the request reaches `sub_8235A240` — the
single fetch entry P2's decision A narrowed to. What is still missing is the file open: the same
boot carries no `store-probe: open`/`resolve` line for that path, so the cache did not read a
local file in that run.

**Proving a generated DTA record is the one the guest runs** — a path probe cannot, because records
come out of the ark by offset. The method that worked: make the edit do something visible. The
payload's `store_menu_screen.enter` gained `{store_panel on_init_commerce_success}`; swapping it for
`{ui goto_screen splash_screen}` for one boot made entering the store jump to splash, which proved
the record executes. `{notify "…"}` is *not* a usable marker (no log line, probably a ship-build
no-op).

The metadata writer's half of the measurement is `rb_blitz_store_payload_tests dump-menu <dtb>` and
`dump-strings <dtb>`: extract a boot's generated payload (`scripts/hmx_ark.py extract --hdr
<profile>/cache/settings-menu/<pid>/gen/patch_xbox.hdr`) and these print the option array the guest
will load and the locale keys it will look up. That is how "the row was authored" was told apart from
"the row is in the record the guest reads" on 2026-10-20.

## Part 2 — Observing a UI state (was `docs/engine/observing.md`)

```powershell
.\scripts\observe_ui.ps1 -ListStates
.\scripts\observe_ui.ps1 -State "main menu" -Ocr
.\scripts\observe_ui.ps1 -State title -BaselineDir out\observations\baseline
.\scripts\observe_ui.ps1 -State title -Compare out\observations\baseline
```

The execution policy on this machine is Restricted, so a bare `.\scripts\...ps1` fails: run it as `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\observe_ui.ps1 ...`, or from a shell that already has the policy set.

### What it is made of

It joins scripts that already existed (Part 1 is the *other* half of the instrumentation, for the guest's internals): [drive_ui.ps1](../../scripts/drive_ui.ps1) sends the keys (one tap is one menu row, through the MnK pad); [capture_window.ps1](../../scripts/capture_window.ps1) saves the window and **refuses to save the wrong one** if another window owns the foreground; [ocr_image.ps1](../../scripts/ocr_image.ps1) reads the static text back (Windows OCR); [frame_diff.ps1](../../scripts/frame_diff.ps1) measures the difference against a baseline; [ui_states.ps1](../../scripts/ui_states.ps1) is **the one table** of state names, routes and needles.

### The states

`-ListStates` prints the table as JSON. The names are dashes; `-State "main menu"` and `-State main-menu` are the same state.

| State | Needle (the text that proves it) | Route |
| --- | --- | --- |
| `boot` | — | the window, before the splash finishes |
| `title` | `TO START` | wait for the title screen |
| `offline-prompt` | `ROCK CENTRAL` | title → the accept that starts a game → the failed-connect dialog |
| `main-menu` | `DOWNLOAD CONTENT` | title → the accept that starts a game → menu |
| `song-list` | `YOUR SONGS` | menu, A on row 0 (PLAY) |
| `in-song` | — | …and A on the list's first row (Random Song) |
| `pause` | — | …and START |
| `help-options` | `HOW TO PLAY` | menu row 3 |
| `controls` | `CONTROLLER` | HELP & OPTIONS, one row down |
| `calibration` | `CALIBRATION WIZARD` | HELP & OPTIONS, two rows down |
| `audio-video` | `OVERSCAN` | HELP & OPTIONS, three rows down |
| `credits` | `PROGRAMMER` | HELP & OPTIONS, four rows down |
| `leaderboards` | `CAREER LEADERBOARD` | menu row 1 |
| `download` | `ESTORE` | menu row 4 |
| `exit-confirm` | `WANT TO EXIT` | menu row 5 |

The needles are the strings those screens actually draw (OCR, this build); the routes are the ones [acceptance_screens.ps1](../../scripts/acceptance_screens.ps1) already walks — safe because **a list clamps at both ends** (eight taps up is the first row, whatever the highlight was) and **a list resets its highlight to the first row whenever it is entered**.

**How many accepts reach the menu is a setting, not a fact.** R3 ([main-menu.md](main-menu.md) §7) makes the title's failed connect become offline mode on its own: one accept reaches the menu by default, three when the dialogs are driven, and one too many accepts whatever row the menu's own accept opens. `-SkipOfflinePrompts 0` drives the dialogs (passes `--enhancements_skip_offline_dialog=false`; the routes are built for it), the only way to reach `offline-prompt`. [scripts/ui_states.ps1](../../scripts/ui_states.ps1) reads that from `RBBLITZ_SKIP_OFFLINE_PROMPTS` because it builds the routes when dot-sourced; the three-accept acceptance scripts pin the flag for the same reason.

A state may carry a **`Crop`**, because some screens only give up their text in a band: the stylised How to Play title is lost by full-frame OCR of 3840×2160 (past what Windows OCR upscales — `-GrayscaleScale 3` refused, scale 2 misses the heading), so `help-options` OCRs `150,100,2700,500`. **A crop is in the captured frame's pixels, so it follows the capture size**; the report carries `capture_size` and `ocr.crop`, so a misaligned crop is visible rather than silently wrong.

`in-song` and `pause` have no needle yet — nobody has pinned down what they draw — so they report an unknown expectation rather than a string that could pass for the wrong reason. **`in-song` starts a song**, which the title may write to the save when it ends; the harness closes the window mid-song.

### The report

One JSON object on stdout; exit code 0 when the state was reached and its expectation held. It carries `state`/`note`; `binary_sha256` (the build — the identity a baseline has to match); `ultimate_mode` (`0` unless `-UltimateMode 1`; an installed payload must not change a screen); `launched` (started vs `-ReuseRunning`); the `route` it drove, so a run can be repeated by hand; `capture`/`capture_size` (a capture at one size cannot be diffed against another); `settle_seconds`; the `ocr.*` block (`ran`, `needle`, `crop`, `matched`, `text` — `ocr.text` is the *crop's* text when the state carries one); the `diff` block (`diff.scale` the downscale factor); and `passed` (`ocr.matched` **and** the diff where the diff may decide).

`diff.status` is **`not-requested`** (no `-Compare`), **`no-baseline`** (`-Compare` named a directory with no `<state>.png` — **not a failure and does not fail the run**: a measurement with nothing to measure against is not a pass), or **`compared`** (`percent`, `scale`, `baseline_scale`, `same_scale`, `max_percent`, `threshold_from`, `same_build`, `settled`, `gates_pass`, `pass`, and the raw `frame_diff` line). `gates_pass` decides the run: false on a state with no settled frame and on a baseline whose floor was measured at another scale; `percent` is then corroboration only.

#### The floor is scale-dependent

`-CompareScale N` downscales both frames by `N` before diffing, and the factor is recorded in the report (`diff.scale`) and the baseline's metadata — **a floor is never compared across scales.** The older floor numbers (**3.5–5.9 %** just-entered, **~0.14 %** settled) were measured on ~1 Mpx captures, while a capture here is **3840×2160 = 8.3 Mpx**, so the same pixel movement is a different percentage; and [frame_diff.ps1](../../scripts/frame_diff.ps1) walks every pixel in PowerShell, so cost is the frame's area — tens of seconds at `-CompareScale 4`.

**A capture is 288 dpi, and `frame_diff.ps1` used to ignore that**: `DrawImageUnscaled` into a 96-dpi scratch bitmap draws at *physical* size, so a third-scale copy landed top-left and the rest compared empty — 3840×2160 diffed as 1280×720, **17.768 %** reported as **1.894 %** (bbox `0,0 - 1279,718`). Fixed 2026-10-04 by matching the scratch resolution; **the floor table is unaffected** (measured at `-CompareScale 4`, where both frames are redrawn into 96-dpi copies). Any *earlier* game-capture percentage was under-reported the same way.

[observe_baseline.ps1](../../scripts/observe_baseline.ps1) produces a whole baseline set at one scale: two passes per state (capture, then compare), the second pass's `percent` **being** that state's same-build floor. It writes `out/observations/baseline/<build>/` (`<build>` = the executable's SHA-256, first 12 digits) with a PNG + JSON per state and `summary.json`; `-Resume` reuses an existing capture instead of booting again (the run is a long sequence of boots and does get interrupted); and it records the scale and warns on a mismatch, because a floor does not transfer between scales.

#### What the floor actually is, measured

`observe_baseline.ps1 -Resume -CompareScale 4` on build `caa1e18c3c6c` (2026-10-04), one same-build pair per state. `noise` is the whole-frame percentage; `screen` is what OCR read — the column that says whether the number means anything:

| State | noise | OCR of the capture | Verdict |
| --- | --- | --- | --- |
| `help-options` | **0 %** | `HOW TO PLAY` | reproduced exactly |
| `exit-confirm` | **0 %** | `WANT TO EXIT` | reproduced exactly |
| `song-list` | 0.473 % | the entry tutorial, not the list | reproduced — but not the state it names |
| `controls` | 5 % | `CONTROLLER` | same screen; its top three tenths differ, the rest is pixel-identical |
| `calibration` | 5 % | `CALIBRATION WIZARD` | same screen, same shape |
| `leaderboards` | 10.535 % | `CAREER LEADERBOARD` | same screen, its aurora band |
| `download` | 14.62 % | the eStore notice | same screen, its aurora band |
| `title` | 18.28 % | `TO START` | same screen, aurora phase; four other same-build pairs read 0.046 %, 1.978 %, 10.74 % and 1.894 % |
| `main-menu` | 17.71 % | the menu's own rows | same screen, aurora phase |
| `pause` | 56.031 % | `GAME PAUSED` | same menu over a live scene |
| `audio-video` | 13.1 % | the help index | **route miss** — a row short |
| `credits` | 75.384 % | the Audio/Video page | **route miss** — a row short, and the page scrolls |
| `in-song` | 72.777 % | unreadable | a different song: the route accepts *Random Song* |
| `boot` | 84.972 % | — | **not a screen** — one capture was black (mean luminance 4.4 against 75.2) |

Only two of the fourteen reproduce exactly and three more sit under the 1 % default; the rest do not, each for a different reason. `audio-video` and `credits` say the important thing: **a same-build `percent` cannot tell a change of screen from a change of pixels**, so gating on it reports a route miss as a visual regression and vice versa.

### The two rules, and why

Both come from things this project has already been wrong about ([backlog.md](../reference/backlog.md), "Measure the noise floor before believing any screenshot diff").

**A pixel diff is only evidence against a same-build baseline.** Two runs of the *same* build differ by **3.5–5.9 %** just after a screen is entered and **~0.14 %** settled, so a 3 % diff is a normal unsettled screen, not a changed one — which is how a round of "proven" texture swaps turned out to be nothing. So the harness needs `-BaselineDir` to compare against, reports `same_build` from the baseline's metadata, and compares the **settled** frame (hence `settle_seconds`), not the one the last key produced.

**And on an animated screen there is no settled frame at all**, measured five times on the title: **0.046 %**, **1.978 %**, **18.28 %**, **10.74 %**, **1.894 %**, the last three with captures OCR'd as `TO START` — the same screen. The first two suggested a 2.5 % ceiling; the third and fourth are what that ceiling cost — a same-build run reported as a failure. The aurora is an animation, what differs is the phase distance between runs, and no settle time removes it. So the state gets no ceiling: it carries **`Settled = $false`** in [ui_states.ps1](../../scripts/ui_states.ps1), its diff is reported, and **OCR is the control while the diff is corroboration**.

**A run gates on the diff only where it can be believed** — a settled frame against a baseline at the same `-CompareScale`; anything else is `Settled = $false` and gates on its needle (`diff.settled`, `diff.gates_pass`). `-MaxDiffPercent` (default **1.0**) and a per-state `MaxDiff` cover settled states only; raise either deliberately, not to make a test pass.

### Adding a state

1. Add an entry to [scripts/ui_states.ps1](../../scripts/ui_states.ps1) with `Route` (an action list in [drive_ui.ps1](../../scripts/drive_ui.ps1)'s grammar), `Needle`, `Settle`, `Note` and — only if the text needs it — `Crop`. Add the name to `$script:UiStateOrder` too.
2. Run it once with `-Ocr` and no baseline: the report's `ocr.text` is what the screen says. Put the shortest unambiguous fragment of that text in `Needle` — not a whole sentence, which OCR noise will break (measured: the title reads `BLITZ PRESS@TO START`, so the needle is `TO START`). If the needle does not appear, the text is not there to be read: try a `Crop` of the band it is drawn in before weakening the needle to something the full frame does read.
3. If the state animates, raise `Settle` and then **measure two same-build pairs**. If they disagree — and on this build they do, by 10–18 % on the aurora screens — the state has no settled frame: mark it `Settled = $false` and drop any `MaxDiff`. Do not widen `MaxDiff` to cover the spread; at 15 % it is no longer a threshold.
4. Record the baseline with `-BaselineDir` only once the state reads the same way twice, and check that both readings are of the right screen: a `percent` is not evidence that the route arrived where the state's name says it did.

### Open, and left open on purpose

- **The How to Play screen's first row is not what its description implies.** Entering HELP & OPTIONS and accepting nothing captures a screen titled `How to PLAY` with rows `BASIC TUTORIAL` and `ADVANCED TUTORIAL`; one row down is `CONTROLLER`, which is how [acceptance_screens.ps1](../../scripts/acceptance_screens.ps1)'s table reaches Controls. So row 0 is a tutorial entry, and that document's "a submenu titled How to Play over four pages" describes rows 1–4 and misses row 0. Not resolved here — it belongs to S2's flow map; the harness only reports what it saw.
- **`in-song` and `pause` have no needle.** Nothing has pinned down what they draw, so they report an unknown expectation rather than a string that could pass for the wrong reason. `pause` read `GAME PAUSED` in the baseline run (a candidate); `in-song` cannot have a stable one until the route stops accepting *Random Song*.
- **The menu chain's routes are not deterministic, and nothing waits for a screen to appear.** The route language is taps and `wait:N` ([drive_ui.ps1](../../scripts/drive_ui.ps1)); a dialog slower than its `wait` leaves the next tap on the dialog, and the run captures whatever that produced. Measured: `main-menu` captured the "Proceed in Offline Mode?" dialog, `audio-video` the help index, `credits` the Audio/Video page — two of the three had a needle that did not read (the harness noticing), but `song-list` is the counter-example: both readings were the one-time entry tutorial, so it agreed with itself about the wrong screen. Until a route can wait on a needle, a menu-chain `percent` is not a measurement of the screen it names. **R3 removed two of the three dialogs** (see "The states"), so a route is two `wait`s shorter and the baseline above is now the `-SkipOfflinePrompts 0` case on purpose; the remaining wait — the failed connect, seconds long and of no fixed length — is what the route still cannot see.
- **The harness can change what the next run sees.** `in-song` starts a song and the guest writes its save; the entry tutorial `song-list` captured is the kind of thing a save decides. A baseline set is only comparable to another from the same save state, and nothing records that yet.
- **`-Resume` compares across sessions.** It reuses a capture because the run is a long sequence of boots; the run behind the floor measurement reused a morning capture and compared it against an evening one 11 h later. The build matched, so the comparison is legitimate — but `same_build` is all it checks; nothing about the desktop, the save or the machine's load is.
- **The cost is in `frame_diff`.** A capture run measured **46 s** end to end; a compare run **154 s**, most of it that script's per-pixel PowerShell loop over a 3840×2160 frame. The harness is a research tool, not a `ctest` target, and this is why.
