# RhythmVerse in-store integration — research + implementation plan

Date: 2026-10-13. Author: agent. Status: **research complete, plan only — no code yet.**
Scope: wire the RhythmVerse customs catalogue into the retail **DOWNLOAD CONTENT** store so that
custom songs can be browsed, searched, and downloaded in-game, with instrument filters in the
in-game settings.

Related reading: [dlc.md](../dlc.md) (content pipeline), [song-cache-plan.md](song-cache-plan.md)
(R7, refresh), [known-issues.md](../known-issues.md) (the store is currently offline-only — this
plan is the community-mod-style feature that known-issues.md explicitly assigns to "the community
mod's job"; we are not restoring Xbox LIVE/Rock Central, we are replacing the store's *catalogue
source* locally), [backlog.md](../backlog.md) ("what is still undefined is everything around [DLC
packages], from a store UI to custom-song content that no content API ever enumerated" — this is
that item).

Evidence files (all under `out/`, gitignored, described in §8): `store.tree.txt`,
`store_info.tree.txt`, `config_store.tree.txt`, `splash.tree.txt`, `ui.tree.txt`,
`server_connect.tree.txt`, `mega_ls.txt`, `dl_probe.bin`, `dl_headers.txt`, plus the owner's API
captures in `docs/rythmverse-responses/{list,search}.json`.

---

## 0. Headlines (what the research settled)

1. **The store is a DTA-driven UI over a file-fetch layer (netcache), not an Xbox LIVE catalogue.**
   The only Xbox LIVE imports the checkout path touches are three `XamShowMarketplace*UI` calls
   (`rb_blitz_init.cpp`). Everything else — menus, offer records, related offers — is fetched as
   files whose names follow templates we now know:
   ```text
   dlc_store                                  (0x102D00)
   /dlc_top_%s_%s.dta                         (0x102D58)   the store front menu
   /%s/top/                                   (0x102D6C)
   dlc_store/%s/%s/offers/%s/                 (0x105088)   per-offer directory
   dlc_store/%s/%s/related/%s.dta             (0x104740)
   ```
2. **The entry gate is exactly one DTA branch.** `ui/splash/gen/splash.dtb`'s `splash_dlc` case
   is `{if_else {user_mgr is_participating_user_online} <enter store> {error_popup_panel show_popup
   store_unavailable_not_online splash_panel}}`. Bypassing this one branch makes the store screen
   open; what happens next (netcache failures → `enum_error` → `store_info_unavailable_screen`) is
   already handled by retail UI.
3. **The RhythmVerse API is even better than assumed: it has gameformat-scoped routes.**
   `/api/rb3xbox/songfiles/list` and `/api/rb3xbox/songfiles/search/live` filter server-side
   (verified live 2026-10-13 — the `all` route returns mixed formats, the scoped route returns
   only `rb3xbox`; a `gameformat` *body parameter* is rejected: "Parameter 'gameformat' is not
   accepted at this path"). We still keep a client-side guard.
4. **Downloads are raw STFS `CON ` packages, no auth, verifiable size.** A byte probe of a
   same-site `download_url` returned `HTTP/1.1 200 OK`, `Content-Type: application/octet-stream`,
   `Content-Length: 4300800` (= the API's `file.size`), first bytes `43 4F 4E 20` (`CON `). The
   file drops straight into the DLC tree the existing loader already walks. (The server ignores
   `Range`; plan for full-body GETs with progress. Off-site `download_url` entries are filtered
   out in v1 — OQ9.)
5. **Two candidate data seams, one instrumented decision point (P2):** (A) serve the `dlc_store/**`
   metadata so the retail offer provider parses *our* DTA; or (B) hook the native
   `BandStorePanel`/`BandStoreOffer` surface. §4 compares them; the recommendation is A-primary
   with B-fallback, decided by P2 measurements, not by guesswork.
6. **Checkout is replaceable at the DTA level.** `store_info.dtb`'s `store_buy_now` →
   `{store_panel check_out …}` and `store_download_again` → `{store_panel re_download …}` are two
   button cases in one record; the record-replacement route (payload ark) is already proven, and
   the typed guest bridge (`rbbz-settings/` hooks in `settings_bridge_hooks.cpp`) is the proven
   way to ask the host to do work. Our downloads ride that.
7. **Instruments fit the existing settings machinery**: a new group with five boolean rows
   (Drums/Guitar/Bass/Vocals on, Keys optional) reuses the P5b/P6 merged Help & Options pipeline
   unchanged.
8. **Search has no retail UI.** `store.dtb`/`store_info.dtb` contain zero occurrences of "search";
   the owner requirement therefore needs a text-entry path (P4 lists the options).

---

## 1. Requirements (frozen from the owner, 2026-10-13)

1. The in-game store lists RhythmVerse customs: **"Recent"** (list route) and **"Search"** (search
   route). No retail categories/groupings (the official store's many filters are intentionally
   not reproduced).
2. Only **`gameformat == "rb3xbox"`** results are relevant (they are the `.con` packages Blitz can
   play). Enforced server-side by route scoping + client-side guard.
3. **Album art** comes from the API (`file.album_art`, fallback `data.album_art`) and becomes the
   store's album art.
4. **Download** = fetch `file.download_url` and save directly into the DLC path the user defined
   (the same path family the launcher/paths.toml already own), then make the game see it.
   Only **same-site targets** qualify (relative paths, or absolute `https://rhythmverse.co/...`):
   some `download_url` values point off-site (e.g. `http://dingorb.com/?id=...`); entries carrying
   those are **excluded from the store in v1** (no internal browser, no arbitrary external
   fetches — documented as OQ9).
5. Users can **search** (text ≥ 3 chars — API rule).
6. **Instrument filters** in the advanced settings: a submenu of booleans; drums/guitar/bass/
   vocals are the default set, **keys** is optional (off by default). Filters map to the API's
   `instrument[]` form fields (verified: `instrument[]=keys` works).
7. **"Already downloaded"** indicator: match by artist + title (case-insensitive) between the
   catalogue entry and what the user already has.

---

## 2. Findings — how the retail store works

### 2.1 Entry and the Xbox Live gate

- Main menu row token `splash_dlc` → label "Download Content" (locale `splash_dlc`,
  song-cache-plan.md §2.1).
- Gate, verbatim from the decoded `splash.dtb` (`out/_swap/splash.tree.txt`):

  ```
  [splash_dlc]
    {if_else {user_mgr is_participating_user_online}
      {do {data_mine_mgr add_event enter_store {{from_screen splash_menu}}}
          {ui goto_screen store_menu_screen}}
      {error_popup_panel show_popup store_unavailable_not_online splash_panel}}
  ```

  `is_participating_user_online` is a method of the native `UserMgr`/`AppUserMgr` class
  (RTTI `. ?AVUserMgr@@` / `. ?AVAppUserMgr@@` in the image; the signin surface —
  `is_pad_signed_into_live`, `is_user_signed_into_live`, `get_signin_mask`, `signin_changed` — is
  a string bucket at image offset ~0x5CA00). The runtime imports `XamUserGetSigninState`,
  `XamUserGetSigninInfo`, `XamContentGetLicenseMask`.
- `ui/gen/ui.dtb` init also preloads `store_browser_panel` and creates the netcache manager:
  `{net_cache_mgr init {find syscfg store netcache_init}}` + `{net_cache_mgr load
  kNetCacheMgrCacheSizeLarge}`.

### 2.2 Screens, panels, list resources

Decoded from `store.dtb` / `store_info.dtb` and the ui/splash registries:

| Screen | Panels | Notes |
| --- | --- | --- |
| `store_menu_screen` | background, shell_music, menu_cache, store_banner, store_menu, upsell, store_panel | `filter.lst` menu list; `header_index 2`; back = splash |
| `store_browser_screen` | background, shell_music, menu_cache, **store_browser_panel** (`store_browser.milo`, `song.lst`), store_panel, **store_details_panel** (`store_details.milo`) | the offer list, chunked (`next_chunk`/`previous_chunk`), shortcuts, preview toggles |
| `store_info_screen` | background, shell_music, menu_cache, store_banner, store_menu, **store_info_panel** (`store_info.milo`, `buttons.lst`), store_panel, store_details_panel, error_popup | single-offer detail; buy/download buttons |
| `store_info_unavailable_screen` | — | retail error target (`enum_error` routes here) |

Assets in the retail ark (`main_xbox.hdr`): `ui/store/gen/{store,store_main,store_menu,store_browser,
store_info,store_details,store_info_enum,store_shortcut,album_art}.milo_xbox` +
`store.dtb`/`store_info.dtb`, list resources `list_store_frontmenu/menu/songpage/packsongs.milo_xbox`,
and exactly ONE local metadata file: `ui/store/remote_metadata/iwannaholdyourhanddlc_prev.mogg`.
The two script records are loaded from hardcoded paths in the image: `./ui/store/gen/store.dtb`
(0x1040) and `./ui/store/gen/store_info.dtb` (0x1060) — they are patchable/payload-able like any
other record.

### 2.3 The data layer: netcache + `dlc_store`

- Config `config/gen/store.dtb` (`netcache_init`) defines servers. Xbox block: `live` (empty host,
  port 1100, root `/rbb/xbox/live/`), plus `prelive`/`test`/`dev`/`test_server_down`, plus a
  **`local` server: `{local TRUE} {root ui/store/remote_metadata/}`** — the developers' offline
  mode, and our door (§2.8).
- Metadata templates (image strings, offsets above): front menu `dlc_top_%s_%s.dta`, per-offer
  directory `dlc_store/%s/%s/offers/%s/`, related offers `dlc_store/%s/%s/related/%s.dta`.
  Companion strings: `?pid=%s`, `/%s%s`, `metadata_loaded`, `on_store_asset_ready`,
  `start_fetching_offer`, `set_target_store_offer_id`, `get_store_offer`, `index_info`,
  `next_chunk`/`previous_chunk`, `offer_provider`, `sort_name`, `request_index_if_needed`,
  `has_menus`, `get_menu_provider`, `submenus`, `cancel_enum`, `is_waiting_on_enum`,
  `offers_populated`, `on_top_menu_populated`.
- The panel also runs a **commerce session** (`has_created_commerce_session`,
  `has_aborted_commerce_session`, `is_creating_commerce_session`, `store/purchase`,
  `show_offer_ui`) whose failure paths are visible in `store.dtb`: `notify "StorePanel Commerce
  Session in an illegal state (kSessionAborted)."` / `fail "… (likely kSessionNone)."`.
- Native classes behind it (RTTI in the image): `BandStorePanel`, `BandStoreOffer`,
  `BandStoreShortcutProvider`, `RndTexBlender`, plus `NetCacheMgr` / `NetCacheMgrXbox`.

### 2.4 The offer model as the DTA consumes it

Fields read by the decoded scripts (`offer get_data [ … ]`, `offer has_data [ … ]`, direct
properties): `name`, `artist`, `album` (also `offer_album_name`), `genre`, `year_released`,
`review`, `release_date_str`, `first_char`, `pack_first_letter`, `pack_name`, `album_link`,
`pack_link`, `preview_audio`, `preview_art`, `short_name`, `rank <part>`, plus
`offer_type` (song/pack/album), `is_available`, `is_purchased`, `in_library` (`store_in_library`),
`is_completely_unavailable`, `has_available_pack`, `has_available_album`, `has_data`.

Difficulty display: for parts `guitar drum vocals bass keys` the script builds `sprintf "diff_%s"
<part>` and asks `song_mgr rank_tier rank <part>` — i.e. the record needs one numeric `rank` value
per part; keep an eye on the singular `drum` token vs RhythmVerse's `rank_drums` (open question
OQ4).

Grouping (`config/gen/store.dtb`): `by_release_date`, `by_review`, `by_song_first_letter`,
`by_artist`, `by_difficulty`, `by_year_released`, `by_pack_first_letter`, `by_author`… — all
irrelevant for us since we only list flat (owner point 1).

### 2.5 Buy / re-download / view-*, verbatim from `store_info.dtb`

```
[store_buy_now]      {if_else {cur_offer in_library} ... {store_panel check_out {cur_offer} {how_we_accessed_the_store} {cur_offer offer_name}} ...}
[store_download_again] {store_panel re_download {cur_offer} {cur_offer offer_name}}
[store_view_album]   {store_info_enum_panel request_enum_and_show_single_offer album_jump_%s FALSE {cur_offer get_data [album_link]} FALSE}
[store_view_pack]    (same with pack_link)
```

Button visibility is data-driven: `purchase_allowed = {offer is_available} && !{offer_provider
show_browser_purchased offer}`; `{offer is_purchased}` chooses "download again" vs "buy now";
preview buttons are hidden when `!{offer has_data preview_audio}` — **omitting preview data hides
the preview UI cleanly** (useful: RhythmVerse has no preview audio).

### 2.6 Art and preview

- Art: `store_details_panel` on `refresh`: sets `album.mat diffuse_tex cover_art_none.tex`, then
  `{store_panel load_art <preview_art> this}`; the completion callback does
  `{album.mat set diffuse_tex {store_panel album_tex}}`. So `load_art <path> <callback>` +
  `album_tex` are the native art pair. Also strings `cover_art_%02i.mat`, `no_art.tex`, `r%02i.mat`
  (recommendation mats), `album_art_path`, `art_url`, `preview_art`.
- Preview: `store_preview_mgr` (`allow_preview_download`, `download_preview_file`,
  `play_preview_file_if_cached`, `is_downloading_file`, `clear_current_preview`), file named by
  `{store_panel get_request_prefix} + {offer get_data [preview_audio]}`. "No preview audio file in
  store_index" is the retail message when the index lacks one.

### 2.7 Native surface inventory (for Option B, if needed)

Strings the DTA calls on `store_panel` (so any host replacement must answer these):
`add_sink`/`remove_sink`, `set offer_status_listener`/`commerce_session_status_listener`,
`update_crumb_text`, `update_loading_status`, `request`, `set_source`/`set_source_to_backup`,
`get_request_prefix`, `num_offers`, `offer_provider`, `sort_name`, `has_menus`, `has_shortcuts`,
`user_can_do_input`, `cancel_current_request`, `should_start_browser_at_bottom`, `load_art`,
`album_tex`, `cancel_art`, `get_menu_provider`, `request_index_if_needed`, `refresh_current_menu`,
`process_offers`, `check_out`, `re_download`, plus `has_created_commerce_session`,
`is_creating_commerce_session`, `has_aborted_commerce_session`, `is_store_offer`,
`get_store_offer`, `set_target_store_offer_id`. On the provider: `build_list`,
`build_list_no_grouping`, `has_shortcuts`, `get_shortcut_array`, `pos_to_shortcut`,
`shortcut_to_pos`, `is_chunk`, `get_pack_provider`, `set_pack`, `get_string`, `set_shortcut_data`,
`apply_shortcut_provider`. On offers: `get_data`, `has_data`, `is_purchased`, `is_available`, …

### 2.8 The `local` dev server — our door

`config/gen/store.dtb` ships a `local` server whose root is `ui/store/remote_metadata/` — the same
directory that holds the one shipped preview mogg. Two consequences:

1. The delivery mechanism can be a directory of files the netcache reads for the `local` server.
   Swapping `default_server` to `local` is a one-record edit of `config/gen/store.dtb` (payload),
   or a `syscfg` poke (spike).
2. The **path mapping** (does `local` resolve `dlc_store/...` under that root? prefix-replaced?)
   and whether the native parser accepts our files is the crux of Option A, and P2 measures it.

### 2.9 The download side (what a download must become)

From [dlc.md](../dlc.md): the host walks the DLC root and mounts STFS/LIVE containers; the layout
rule accepts structured `<title_id>/<content_type>/<package>` **and** a flat library of loose
containers. RB3 marketplace content lives under `45410914/00000002/…`. The verified probe shows
the download is already a complete `CON ` container whose length equals the API's `file.size` — so
"install" is: verify bytes → write as `<dlc_root>/45410914/00000002/<name>` (default structured
placement; a flat root works too because the walker is recursive) → invalidate the host DLC cache
(`src/fs/dlc_cache`) → make the game enumerate it again (the "Refresh Music Library" path, R7 /
`dlc_refresh` machinery — the auto-refresh call itself is OQ6).

Guest-side, the store's own download machinery (`songs_download` on `cntNNNNNNNN:` content
devices, seen in boot logs) is Xbox-LIVE content plumbing we are **not** reviving.

### 2.10 Project machinery this feature builds on (all proven)

- Patch ark / `PayloadOverlayDevice` record replacement, incl. "extend the mounted part in place"
  (the black-screen fix) — delivery for DTA edits (`store.dtb`, `store_info.dtb`, `splash.dtb`,
  `config/store.dtb`) and for generated `ui/store/remote_metadata/**` if we add a side.
- Typed guest bridge, namespace `rbbz-settings/` via hooked builtins `file_exists`/`write_file`
  (`src/hooks/settings_bridge_hooks.cpp`) — extend with a `rbbz-store/` namespace for download
  requests/state.
- DTA authoring (`src/ui/dtb_author.cpp`, `settings_screen` codegen + `dtb_author_tests` emit
  modes), locale overrides for new labels.
- Settings model/store/rows (`src/settings/*`, `src/ui/settings_screen.*`) — instrument toggles.
- DLC walk + cache (R7), `dlc_layout`, refresh (`src/hooks/dlc.cpp`, `dlc_refresh.cpp`,
  `menu_options` refresh row).
- Acceptance harness (`scripts/acceptance_*.ps1`, `scripts/drive_ui.ps1`, `capture_window.ps1`,
  OCR helpers).

---

## 3. RhythmVerse API (as measured 2026-10-13)

| Item | Value |
| --- | --- |
| List | `POST https://rhythmverse.co/api/rb3xbox/songfiles/list` |
| Search | `POST https://rhythmverse.co/api/rb3xbox/songfiles/search/live` |
| Body | `x-www-form-urlencoded` |
| Fields | `instrument[]` (repeatable; `drums, guitar, bass, vocals, keys`), `sort[0][sort_by]=update_date`, `sort[0][sort_order]=DESC`, `data_type=full`, `text` (search only, ≥3 chars), `page`, `records` (site uses 25) |
| Scoping | gameformat is the route token (`rb3xbox`); a `gameformat` body param is rejected. The generic `all` route returns mixed formats. |
| Auth | none for list/search/download |
| Response | `{status:"success", data:{records:{total_filtered, returned}, pagination:{page, records}, songs:[{data, file}]}}` |
| Fields we consume | `file.file_id, file_name, gameformat, file_artist, file_title, file_album, file_year, file_album_track_number, file_song_length, file_genre, size, downloads, album_art, download_url, diff_*, rank_*` (+ `data.album_art` fallback) |
| Download | `GET https://rhythmverse.co` + `file.download_url` when it is same-site → raw STFS `CON ` / `LIVE`, `Content-Length == file.size`. **Off-site** values (e.g. `http://dingorb.com/?id=...`, seen on the `all` route) are out of scope in v1 and filtered out (OQ9). |
| Search rule | a too-short query returns `{"status":"error","error":{"message":"You need to provide a text at least 3 characters long"}}` |

Matching rule for "already downloaded": compare `file_artist` + `file_title` (case-insensitive,
trimmed) against the local library/download manifest.

---

## 4. Architecture options and the recommendation

### 4.1 Option A — serve `dlc_store/**` metadata through netcache (retail UI, retail parser)

The native offer provider parses files fetched from the configured server. If we (i) route it to a
source we control (`local` server root, or a fetch hook) and (ii) generate those files in the
format it expects, everything above the file layer is retail: menus, list, details, difficulty
tiers, grouping, buttons, art pipeline (`load_art`), popups.

- Pros: smallest UI surface; retail behaviour for free; the owner's "two menus" model maps 1:1 to
  the retail menu model.
- Cons: the file **schema is unknown** until instrumented (fields partially known from DTA usage
  and image strings); commerce-session state may still need a stub.
- Unknowns: local path mapping, parser acceptance, art/preview file formats, chunk protocol.

### 4.2 Option B — replace the native panel/offer provider at the method level

Hook `BandStorePanel` + `BandStoreOffer` methods (inventory in §2.7) and answer with RhythmVerse
data; the retail DTA drives them unchanged.

- Pros: no metadata schema; full control of entitlement (`is_purchased` → our matcher) and
  checkout (`check_out` → our download).
- Cons: a large native ABI surface; art still needs a real texture (`album_tex`); locating and
  hooking every method is RE-heavy; the DTA's `filter.lst`/provider coupling is intricate.

### 4.3 Option C — own screens reusing the store scenes

Author our own `UIScreen`s (house style from P5b) that reuse `store_browser.milo`/list resources.

- Pros: total control; machinery proven.
- Cons: the list widgets are provider-bound (song rows are not simple symbol rows); most UI work;
  loses the "retail store" look the owner started from.

### 4.4 Recommendation

**A-primary, B as fallback for whatever A cannot express, C later if both disappoint.**
The two are not exclusive: the natural landing zone is A for data + a **thin runtime stub set**
for commerce-state methods that A cannot satisfy (`has_created_commerce_session` → true,
`is_purchased`/`in_library` from our matcher, `check_out`/`re_download` → our download flow).

P2 (instrumentation) is the decision gate; it is cheap and answers A-vs-B with measurements
(what the store requests, what it parses, what fails on empty/local data).

Seam table (the plan of record):

| Component | First choice | Fallback |
| --- | --- | --- |
| Store opens | DTA patch of `splash_dlc` branch, runtime-gated by `rbbz-store/enabled` through the existing bridge builtin hook | hook `UserMgr is_participating_user_online` for the store context only |
| Menus ("Recent", "Search") | A: generated top-menu metadata | B: hook `get_menu_provider`/`request` |
| Offer list + fields | A: generated offer records | B: hook provider `build_list` + offer `get_data` |
| Album art | A: serve art files the same way (`load_art` path) | pre-convert + feed `album_tex`; last resort placeholder `no_art.tex` |
| Entitlement / "already downloaded" | manifest matcher exposed via `is_purchased`/`in_library` (A: fields in the record; B: method hooks) | full method hook |
| Checkout/download | DTA patch of the two button cases + `rbbz-store/` bridge ops | hook `check_out`/`re_download` |
| Refresh after download | invalidate host cache + guest refresh (OQ6) | tell the user to press "Refresh Music Library" |
| Instrument filters | settings store, read by the host client | — |

---

## 5. Phases

House style: each phase is a self-contained prompt with a measurable exit.

### P0 — API client + contracts (SDK-free, no game changes)

- Build `src/store/rhythmverse_client.{h,cpp}` (SDK-free): request builder (`instrument[]`,
  `sort[0]…`, `data_type=full`, `text`, `page=1..`, `records=25`), response model (song/file pair,
  the fields in §3), paging cursor, the artist+title matcher, and the `IsDownloadable` predicate
  (same-site only — OQ9). JSON parsing: vendored minimal
  parser (new dependency — check `thirdparty/` first) or a hand-rolled parser limited to the shapes
  we consume; keep it testable and hermetic.
- Host transport: port the launcher's WinHTTP pattern (`launcher/src/update_launcher.cpp` —
  RAII handles, timeouts, URL rules) into a small `store_http` module for the game exe. All
  network calls on host worker threads; never touch the cvar registry or guest state from them
  (project rule).
- Fixtures: `docs/rythmverse-responses/{list,search}.json` + the scoped-route captures.
- Tests: `tests/store_client_tests.cpp` (ctest `store_client`) — URL/body building, parse,
  pagination, matcher, `IsDownloadable` (relative, absolute same-site, off-site, scheme-relative,
  uppercase host), error mapping.
- Exit: ctest green; no game-code changes.

### P1 — the store opens without Xbox LIVE

- Gate bypass (seam table row 1). Two arms measured:
  - arm 1: runtime-gated splash patch (`{if_else {file_exists "rbbz-store/enabled"} …}`) with the
    existing builtin hook extended to the `rbbz-store/` namespace — default boot untouched;
  - arm 2 (control): patch that always enters.
- Acceptance of this phase is modest on purpose: `store_menu_screen` opens, draws, B returns; the
  offer path is allowed to land on `store_info_unavailable_screen` (retail handles empty data).
- Evidence: screenshots via `drive_ui.ps1`, log lines for the bridge gate hit.
- Exit: store opens in ≥5 consecutive boots, stock boot behaviour unchanged (byte-identical
  records still hashed, see P5b's `records-before.sha256` pattern).

### P2 — instrument the data layer (DECISION GATE: A vs B)

- Log every guest file open / netcache fetch whose path contains `dlc_store`, `dlc_top`, `offers`,
  `related`; log the commerce-session methods as they are called; try `default_server local`
  (payload edit of `config/store.dtb`) with an empty `ui/store/remote_metadata/` and capture the
  retail error strings (they name the missing/parse-failed file).
- Deliverables: (1) the exact request list for opening the store to a list of N offers; (2) the
  path-mapping rule for `local` (or proof that a fetch hook is the only route); (3) first schema
  guesses validated by serving one hand-made file and observing the retail reaction.
- Exit: a written A/B decision with evidence; if A is chosen, a skeleton of the metadata writer;
  if B, the hook inventory (which methods, which addresses, which signatures).

### P3 — first light: "Recent" list with real data + art

- Generate the store front menu with **Recent** (and a placeholder **Search** entry) and offer
  records for page 1 of `/api/rb3xbox/songfiles/list` (25 records; off-site entries are filtered
  out by `IsDownloadable`, so the list only holds things the store can actually fetch), then
  serve them through the chosen seam. Album art: first use `album_art_path`-style references to files we serve; if the
  engine demands platform textures, add host-side PNG→texture conversion (tooling exists:
  `scripts/hmz_tex.py`, `tools/milotex`).
- Field mapping (first cut): `file_title`→`name`, `file_artist`→`artist`,
  `file_album`→`album`, `file_genre`→`genre`, `file_year`→`year_released`,
  `diff_*`→the `rank <part>` values the tier display reads (spike the exact key spelling, OQ4),
  `album_art`→`preview_art`.
- Exit: entering the store shows the two menus; selecting Recent lists the 25 newest rb3xbox
  customs with title/artist/art; scrolling selects, A opens details; no crash on empty lib.

### P4 — Search

- The retail store has no search UI (verified). Investigate, in order:
  1. the game's own keyboard facility (strings `virtual_keyboard_result_msg`, `keyboard_email_*`;
     not in the records decoded so far — dump the remaining DTB records and look; RB3 had a
     generic on-screen keyboard);
  2. if unusable, an authored minimal keyboard screen in house style (A–Z/space/backspace as
     rows; the settings machinery proves authored rows + dispatch work);
  3. interim fallback: search seeded from the launcher (a query passed at launch) — demoable but
     not what the owner asked for; keep only as a spike-time crutch.
- Wire the chosen keyboard to `search/live` (≥3 chars enforced with a retail-style popup), results
  replace the browser list; empty results use the retail empty state.
- Exit: on-screen query entry → results; cancel paths; no regressions in P3.

### P5 — download pipeline

- Host `store_download` module: GET `download_url` (relative → prefixed with
  `https://rhythmverse.co`; absolute same-site accepted; anything else refused — defense in depth
  behind `IsDownloadable`), stream to
  `<dlc>/45410914/00000002/.<file_id>.part` with progress, verify `bytes == file.size`, atomic
  rename to a safe name (`<Artist> - <Title> (<file_id>)`, matching the flat-library convention in
  dlc.md §2.1), delete on failure; cancel support; 2 retries with backoff; clear errors for
  disk-full / HTTP failure / mismatched size / read-only path.
- Bridge: extend `settings_bridge_hooks.cpp` with the `rbbz-store/` namespace:
  `rbbz-store/download/<file_id>` (start), `rbbz-store/state/<file_id>` (`idle`/`download:<pct>`/
  `done`/`error:<code>`), `rbbz-store/cancel/<file_id>`.
- UI: patch `store_info.dtb`'s `store_buy_now`/`store_download_again` cases to call the bridge and
  poll state (retail loading thinger + `error_popup_panel` for errors). The buy button label
  already reads "download again vs buy" from `is_purchased` — our matcher supplies it.
- Entitlement/"already downloaded": `store/downloads.toml` under the user data root
  (`{artist, title, file_id, size, date}`), keyed by normalized artist+title; matcher exposes it
  as `is_purchased`/`in_library` to the store data layer (per seam). Stretch: also read the
  library's own song metadata (songcache or a host-side STFS `songs.dta` extractor) so cons the
  user dropped in manually are recognized — flagged as stretch, OQ5.
- Refresh: invalidate `fs/dlc_cache`; trigger the guest refresh (OQ6) or surface the existing
  "Refresh Music Library" row.
- Exit: downloaded song appears in the game's song list after refresh and is playable; the store
  shows the entry as downloaded on the next visit; failures leave no `.part` residue.

### P6 — instrument filters in Help & Options

- New settings group (submenu) "Music Store" with five boolean rows: Drums, Guitar, Bass, Vocals
  (default On), Keys (default Off) — reusing `SchemaRow::group`, the P5b child-screen pattern, and
  the existing persistence/migration (tokens `store.instruments_drums`…). Rows go through the
  standard commit path; the store client reads the effective set per request and emits
  `instrument[]` (keys only when On).
- Exit: toggling a row changes the request body (log-visible) and the resulting list; settings
  survive restarts; no regression in the merged menu tests.

### P7 — polish

- Chunking: map API `page` to the store's `next_chunk`; show `total_filtered` in the crumb/banner
  (retail shows counts? verify) or rely on retail.
- Caching: in-memory page cache with TTL (~5 min) + single-flight; disk cache optional
  (`<user_data_root>/store/cache/`); polite `User-Agent`; backoff on 5xx.
- "Powered by RhythmVerse" credit line (locale override — the P1-c/P5b locale mechanism).
- Empty-state and offline text; the store's own `store_info_unavailable_screen` copy reused.
- Optional: recommendation/marquee areas hidden or fed from the same list (do not fetch extra).

### P8 — acceptance + docs

- `scripts/acceptance_store.ps1`: boot → Download Content → assert Recent list OCR ("Recent",
  first entries) → open Search → assert results → select an entry → download → wait for file →
  refresh → assert the song appears in the song list; plus a control leg with the feature off
  (store still gated).
- ctest additions from P0/P5/P6 (all hermetic where possible).
- Docs: `docs/dlc.md` cross-reference, this plan's status table updated, `known-issues.md` gets the
  scoped statement ("the store's catalogue can be supplied locally by the mod; online services
  remain out of scope").

---

## 6. Open questions & risks

- **OQ1 — metadata schema** (Option A's crux): exact file set + field names the native parser
  expects. Mitigation: P2 instrumentation; DTA usage inventory; fallback Option B.
- **OQ2 — `local` path mapping** and whether `default_server local` is even needed (vs a direct
  fetch hook).
- **OQ3 — commerce session**: can it be satisfied by file data alone, or does it need a stub? If a
  stub, which methods (see §2.7)?
- **OQ4 — part-token spelling** in offer records (`drum` vs `drums`) and the numeric meaning of
  `rank` per part (`song_mgr rank_tier` input domain).
- **OQ5 — library metadata for the matcher**: manifest-only vs reading existing cons. A host-side
  STFS file extractor for `songs.dta` is a known moderate task (Python reference:
  `scripts/stfs_ls.py`).
- **OQ6 — auto-refresh** after a download: which guest call makes the fresh con enumerate (the
  same path as "Refresh Music Library"), and whether it is safe mid-store (needs a return to the
  menu first?). Manual refresh is the guaranteed fallback.
- **OQ7 — duplicates**: the same song/title can exist as multiple files (different uploaders);
  list entries are per-file today; decide whether to show all or collapse.
- **OQ8 — art format**: what `load_art` accepts and how it caches; PNG vs platform texture.
- **OQ9 — off-site `download_url` values** (e.g. `http://dingorb.com/?id=...`): excluded in v1 —
  the store must neither open a browser nor fetch arbitrary hosts. Documented options for later:
  list them as "not downloadable in-store", or hand the URL to the launcher/OS on an explicit user
  action. The client keeps the policy behind one predicate (`IsDownloadable`) so revisiting it is
  a small change.
- **R1 — faking "online" side effects**: contained by patching DTA branches and namespaced bridge
  reads only; never flip global signin state (would affect Rock Central/leaderboard paths).
- **R2 — RhythmVerse load/rate**: cache + single-flight + backoff + UA; no bulk prefetch;
  `records=25` like the site.
- **R3 — DLC path safety**: respect `paths.toml`/launcher profile as the single source of truth;
  refuse downloads into a read-only or missing root with a clear popup.
- **R4 — Ultimate payload interplay**: the store records and the `ui/store/remote_metadata/**`
  delivery must merge like everything else (one part, extended in place — the black-screen
  lesson); test with Ultimate installed.
- **R5 — API drift**: the site is PHP/Slim and changed before (`gameformat` param rejected,
  filters route 403s to us); isolate parsing in P0 tests so drift is a one-file fix.
- **R6 — naming/attribution**: show RhythmVerse as the source in the store UI; do not ship retail
  bytes; the feature ships default-off until P3–P5 pass.

## 7. Non-goals (v1)

No Xbox LIVE/Rock Central restoration, no purchases/entitlement checks, no preview audio
(RhythmVerse has none; the UI hides itself when the data is absent), no retail categories/grouping,
no PS3 paths, no account features, no writes into the retail game data, no songcache write-back,
and **no off-site download links** (external `download_url` entries are filtered out in v1 — OQ9).

## 8. Evidence index

| File | What it is |
| --- | --- |
| `out/_swap/store.tree.txt` | decoded `ui/store/gen/store.dtb` (BandStorePanel, browser, previews, menus) |
| `out/_swap/store_info.tree.txt` | decoded `ui/store/gen/store_info.dtb` (buttons, buy/download cases) |
| `out/_swap/config_store.tree.txt` | decoded `config/gen/store.dtb` (servers incl. `local`, groupings) |
| `out/_swap/splash.tree.txt` | decoded `ui/splash/gen/splash.dtb` (the gate, verbatim) |
| `out/_swap/ui.tree.txt` | decoded `ui/gen/ui.dtb` (store panels preload, netcache init) |
| `out/_swap/server_connect.tree.txt` | decoded `ui/net/gen/server_connect.dtb` (discovery/offline states) |
| `out/_swap/mega_ls.txt` | STFS listing of the 803 MB RB3 DLC container (song layout reference) |
| `out/_swap/dl_headers.txt`, `dl_probe.bin` | download probe: 200 OK, octet-stream, `CON ` magic, size match |
| `docs/rythmverse-responses/list.json`, `search.json` | owner captures (fixtures for P0) |
| image string offsets | `BandStorePanel` 0x3A078; `is_store_offer` 0x3D308; `has_created_commerce_session` 0x87B00; `load_art` 0x87BC0; `netcache_init` 0x8E6EC; `get_store_offer` 0x105248; `set_target_store_offer_id` 0x105270; `dlc_store` 0x102D00; `/dlc_top_%s_%s.dta` 0x102D58; `dlc_store/%s/%s/offers/%s/` 0x105088; `dlc_store/%s/%s/related/%s.dta` 0x104740; `Xis_participating_user_online` 0x13FBB |
| imports (checkout) | `__imp__XamShowMarketplaceUI` 0x827F6544, `…NuiMarketplaceUI` 0x827F65B4, `…MarketplaceDownloadItemsUI` 0x827F6774 |

## Appendix A — request cheat sheet

```powershell
# Recent (rb3xbox only, newest first)
curl.exe -s -X POST "https://rhythmverse.co/api/rb3xbox/songfiles/list" `
  -d "instrument[]=drums" -d "instrument[]=guitar" -d "instrument[]=bass" -d "instrument[]=vocals" `
  -d "sort[0][sort_by]=update_date" -d "sort[0][sort_order]=DESC" `
  -d "data_type=full" -d "page=1" -d "records=25" -H "X-Requested-With: XMLHttpRequest"

# Search (>=3 chars)
curl.exe -s -X POST "https://rhythmverse.co/api/rb3xbox/songfiles/search/live" `
  -d "text=halsey" -d "instrument[]=guitar" -d "instrument[]=vocals" `
  -d "data_type=full" -d "page=1" -d "records=25" -H "X-Requested-With: XMLHttpRequest"

# Download (no auth; Content-Length == file.size; raw STFS CON/LIVE)
curl.exe -s -o out.bin "https://rhythmverse.co/download_file/<user_folder>/<file_id>/<file_name>"
```
