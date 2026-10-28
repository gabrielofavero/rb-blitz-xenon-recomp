# RhythmVerse in-store integration — research + implementation plan

the Date: 2026-10-13. Reviewed: 2026-10-14, 2026-10-20, 2026-10-21, 2026-10-22, 2026-10-23, 2026-10-24. Status:
**reopened 2026-10-22** — the owner drove the shipped store and asked for eleven adjustments, then
two more the same day (P9-12 and P9-13), and **P9-6 was redesigned from scratch on 2026-10-23** (one
modal download popup; **delivered 2026-10-24**), with **P9-14** (the toast's A Refresh, **landed
2026-10-24**) following; **§9** carries one prompt per adjustment and is the
work now, and the record below is what shipped before that pass, kept as it was written. **Finished
2026-10-22 — P0, P6, P1, P2, P3, P4, P5, P7 and P8 all landed, and the acceptance harness is green
in all four legs on one build (`out/store-acceptance/p8-final/`, **69 steps, 0 failures**): the
switch is turned on through the UI, the store lists the catalogue, Search answers and lists, a
download installs, the next boot's song list carries the song, and a boot with the feature off makes
no store request at all. That last step cost two real defects, both found by the harness and fixed
(§5/P8's close-out record): the panel's readiness word was written only under `store_trace`, and the
search seed opened the search surface**; §5 keeps one record per phase, and P8's record carries the
harness, the matrix and the legs' own verdicts. The route there: **P0, P6, P1 and P2
landed 2026-10-20**, and **P3's serial half (step 0) landed the same day**: the request chain is
mapped (`sub_827501F0` → `sub_82753010` → `sub_8274FB60` → the file cache), both entries are
hooked, and the store's own DTA record is proven editable — the panel still issues no request
offline, so the data lane starts at the gate (§5/P3 step 0 record). **P3's data lane then reached
the file cache**: a served record is inserted and parsed by the retail DTA reader, and **the retail
notification now delivers it** — the store draws the served front menu's rows and a row press asks
for `dlc_store/europe/eng/recent/`, the request name P2 predicted (§5/P3 landing record,
2026-10-21). **The catalogue landed 2026-10-21**: the page is filled from the live list (24 offers
in boot 090), retail's own request body delivers it, and the **retail browser opens on those
offers** with its details panel showing the offer's artist and genre (boots 112-120). **P3's
remaining items were then worked in code and on screen the same day** (§5/P3 landing record): the
store's heading now names RhythmVerse and the retail **price footer is gone** (both boot-measured),
the **Downloaded matcher** is built (its index reads every loaded package's `songs.dta` and marks
the offers the library already holds), the details page's cost labels and its
`is_completely_unavailable` guard are edited in the payload, and the browser's blank rows were
traced to the offer's **string-typed `type` key** (the retail provider compares it against the
symbol `song`; with the symbol served it answers all 24 titles - but the list then faulted while
drawing, so that form shipped behind a default-off switch and the rows stayed blank). **P3's
blocking items are exited 2026-10-21**: the draw-time null is the details panel's two-component
`(rank <part>)` read, which no five-part rank map can satisfy (the walk matches each component
against a pair's *first* element); the record now carries `rank_<part>` keys and the payload unrolls
the panel's loop onto them, the symbol `type` and the delivery are on by default, the browser draws
all 24 titles, and A opens `store_info_screen` with the offer's own data (§5/P3 exit record, boot
`out/_swap/store-probe/20261021-190304`). **P3's `Exit:` line is met 2026-10-22**: album art draws
from a runtime PNG→texture encoder (§5/P3 art record), the Downloaded matcher reads a real package
and a real **multi-song pack** (25 songs from one package, `declares 25 song(s) across 21
package(s)`), one visit carries four API pages (99 offers), the difficulty meter draws the
catalogue's tier per part with `NO PART` only where it publishes 0, and a marked offer's details
page draws retail's own **DOWNLOAD AGAIN** row (§5/P3 close-out record, boots
`out/_swap/store-probe/20261022-084741`, `-090627`, `-091435`, `-100814`, `-101027`). The meter's two
honest limits - no devil, no sixth pip - are in
[known-issues.md](../../reference/known-issues.md).
**P5's pipeline landed the same
day, hermetically tested**; **its guest half then landed 2026-10-22 and one boot drove it end to
end** - the details page's **DOWNLOAD** row registers, starts, installs, refreshes and receipts a
real catalogue package, and the caption strip reports each step (§5/P5 landing record, boot
`out/_swap/store-probe/20261022-124753`). **P4's last line landed 2026-10-22**: the
store's own **Search** row now opens the authored keys, Done submits, and pressing the row again
fills the retail browser with the answer - `15 MATCHES FOR "BUSH"` and the fifteen offers drawn,
with Recent unchanged on the control leg (§5/P4 landing record, boots
`out/_swap/search-probe/20261022-105010`, `-105401`). The field, the authored keys, the request, the
answer and the two authored sentences were boot-measured on 2026-10-20 and stand. **P7's polish
landed 2026-10-22** (§5/P7 record): the catalogue and the search walk API pages on the **server's
own cursor** with a bounded per-action budget and a real cancellation, both share an in-memory page
cache (~5 min, single flight) and P7's answer-derived backoff, and the store now says its own
sentences in the caption strip - the credit line `Customs from RhythmVerse`, the loading line, the
empty-state sentence and the offline reason. That review is
**hermetic for the sentences and boot-measured for the credit line**: a store boot on the shipped
release binary draws `Customs from RhythmVerse` under the store menu's rows
(`out/_swap/store-probe/20261022-140259`) and its log carries the four-page walk
(`page 1..4 -> 24,25,25,25 eligible offer(s) of 25 record(s)`, `99 offer(s) for the Recent list`).
The loading, empty and offline lines are module-tested only. P1's browser
acceptance is met
on one boot (row drawn, store opens, B returns) and the multi-boot/toggle matrix it still owed is
P8's: the same route now runs as four scripted legs, including the control leg that boots with the
feature off. Audio
preview is decided: the API returns no preview asset (§3.3), so v1 shows none. **P8, the last
phase, landed 2026-10-22** (§5/P8 record): the acceptance harness, the phase's matrix and its docs -
and, the same day, the run that closed the walk: **all four legs green on one build**, after the
two defects that run found were fixed (§5/P8 close-out record).
Scope: wire the RhythmVerse customs catalogue into screens based on the retail store so that
custom songs can be browsed, searched, and downloaded in-game, with instrument filters in the
in-game settings.

Related reading: [dlc.md](../../guides/dlc.md) (content pipeline),
[song-cache-plan.md](../cancelled/2026-10-10-song-cache-plan.md) (R7, refresh),
[settings/menu.md](../../settings/menu.md), [settings/architecture.md](../../settings/architecture.md),
[known-issues.md](../../reference/known-issues.md) (the store is currently offline-only — this
plan is the community-mod-style feature that known-issues.md explicitly assigns to "the community
mod's job"; we are not restoring Xbox LIVE/Rock Central, we are replacing the store's *catalogue
source* locally), [backlog.md](../../reference/backlog.md) ("what is still undefined is everything around [DLC
packages], from a store UI to custom-song content that no content API ever enumerated" — this is
that item).

Evidence files (all under `out/`, gitignored, described in §8): `store.tree.txt`,
`store_info.tree.txt`, `config_store.tree.txt`, `splash.tree.txt`, `ui.tree.txt`,
`server_connect.tree.txt`, `mega_ls.txt`, `dl_probe.bin`, `dl_headers.txt`, plus the owner's API
captures in `docs/rythmverse-responses/{list,search}.json`.

---

## 0. Headlines and review corrections

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
2. **Retail's entry gate is one DTA branch, but that row already has a new job.**
   `ui/splash/gen/splash.dtb`'s original `splash_dlc` case
   is `{if_else {user_mgr is_participating_user_online} <enter store> {error_popup_panel show_popup store_unavailable_not_online splash_panel}}`. In this checkout, R7's `TapRefreshCache` replaces
   that case with **Refresh Music Library**. Add a separate **RhythmVerse Customs** row/token;
   do not overwrite or relabel the refresh entry (§2.11). Whether the store fully enters without
   further commerce-state work remains a P1/P2 runtime check.
3. **The RhythmVerse API is even better than assumed: it has gameformat-scoped routes.**
   `/api/rb3xbox/songfiles/list` and `/api/rb3xbox/songfiles/search/live` filter server-side
   (verified live 2026-10-13 — the `all` route returns mixed formats, the scoped route returns
   only `rb3xbox`; a `gameformat` *body parameter* is rejected: "Parameter 'gameformat' is not
   accepted at this path"). We still keep a client-side guard.
4. **One download was verified as a raw STFS `CON ` package; that is not a format guarantee.** A byte probe of a
   same-site `download_url` returned `HTTP/1.1 200 OK`, `Content-Type: application/octet-stream`,
   `Content-Length: 4300800` (= the API's `file.size`), first bytes `43 4F 4E 20` (`CON `). The
   file can use the existing loader. Other files may be archives or external links even when
   `gameformat` is `rb3xbox`. V1 **skips archives**, rejects non-direct/off-site links, checks
   redirects and response headers, and validates bytes before installation (§3.1). This probe
   ignored `Range`; do not assume a range request limits the transfer on other responses.
5. **Two candidate data seams, one instrumented decision point (P2):** (A) serve the `dlc_store/**`
   metadata so the retail offer provider parses *our* DTA; or (B) hook the native
   `BandStorePanel`/`BandStoreOffer` surface. §4 compares them; the recommendation is A-primary
   with B-fallback, decided by P2 measurements, not by guesswork.
6. **Checkout is replaceable at the DTA level.** `store_info.dtb`'s `store_buy_now` →
   `{store_panel check_out …}` and `store_download_again` → `{store_panel re_download …}` are two
   button cases in one record; the record-replacement route (payload ark) is already proven, and
   the typed guest bridge (`rbbz-settings/` hooks in `settings_bridge_hooks.cpp`) is the proven
   way to ask the host to do work. Our downloads ride that.
7. **Use the existing Settings → General → Downloadable Content page**: add **RhythmVerse:
   On | Off** and **RhythmVerse Song Filter**, with Guitar/Bass/Drums/Vocals On and Keys Off.
   This needs six schema leaves, a child panel, persistence, and a live settings applier (§1/P6).
8. **Search has no retail UI.** `store.dtb`/`store_info.dtb` contain zero occurrences of "search";
   the owner requirement therefore needs a text-entry path (P4 lists the options).
9. **Free customs need explicit UI edits**: keep both cost labels hidden on every refresh,
   change purchase wording to download wording, and check browser row resources for native price
   rendering (§2.12). Supplying a zero price alone is insufficient.
10. **Loaded DLC metadata is required for matching**. A download manifest cannot identify songs
    installed manually and must not keep deleted songs marked downloaded. Build a lazy artist/title
    index from the loaded DLC catalogue; a manifest is only a transfer receipt (§3.2).

---

## 1. Requirements (owner update, 2026-10-14)

1. The in-game store lists RhythmVerse customs: **"Recent"** (list route) and **"Search"** (search
   route). No retail categories/groupings (the official store's many filters are intentionally
   not reproduced).
2. Only **`file.gameformat == "rb3xbox"`** results are relevant. Enforce route scoping plus a
   client-side guard. This identifies the target format, not the downloaded body's container type.
3. **Album art** comes from the API (`file.album_art`, fallback `data.album_art`) and becomes the
   store's album art.
4. **Download** = fetch an official RhythmVerse-hosted **direct file link** from
   `file.download_url`, validate a raw package, and install into the user's resolved DLC path.
   Only HTTPS `rhythmverse.co` `/download_file/...` targets qualify, including root-relative
   equivalents. A same-site HTML/download landing page does not qualify. Recheck every redirect.
   No embedded browser, external host, login/cookie flow, or automatic ZIP/RAR/7z extraction in v1.
   Known archives are filtered before download; disguised archives are rejected before installation.
5. Users can **search by artist OR song title**, one at a time (text ≥ 3 chars — API rule).
   Owner clarification, 2026-10-14: combining the artist and song title in one query fails to find
   the intended result. The search UI must explain this limitation; do not construct combined
   artist/title queries. Multi-word artist names and song titles are still valid inputs.
6. **Settings placement and labels**, in this order under **Settings → General → Downloadable
   Content** (the existing Cache DLC Library and DLC Scan Threads rows remain):

   ```text
   RhythmVerse: On | Off
   RhythmVerse Song Filter →
       Guitar: On | Off     (default On)
       Bass: On | Off       (default On)
       Drums: On | Off      (default On)
       Vocals: On | Off     (default On)
       Keys: On | Off       (default Off)
   ```

   Master default remains **Off** as proposed in the original plan; the owner specified the
   instrument defaults, not a different master default. Off cancels outstanding store work and
   prevents new catalogue/art/download requests; saved filters and installed songs remain.
   Filters remain editable while Off. Proposed filter meaning: On requires that instrument's
   chart; Off imposes no condition (Keys Off does **not** exclude songs with keys). All Off means
   no instrument constraint. Verify the API's multi-instrument semantics in P0; apply the predicate
   locally if needed. Both Recent and Search use the same committed selection.
7. **"Downloaded"** indicator: lazily compare artist + song title against **loaded DLC songs**,
   including manually installed content and every song in a pack. Do not use purchase ownership,
   filename, RhythmVerse ID, or a download manifest as the authority (§3.2).
8. **Attribution is required in the first usable UI**, not deferred polish. Chosen design:
   a separate main-menu entry **RhythmVerse Customs**, with the same store heading. If scene
   constraints force a generic heading, show **Customs from RhythmVerse** persistently in the store.
   Use the spelling [RhythmVerse](https://rhythmverse.co/). Preserve Refresh Music Library.
9. **All customs are free**: hide monetary values, points/currency icons, balances, and purchase
   prompts. Use **Download**, **Downloaded**, and explicit **Download Again** actions. No zero-price
   placeholder and no `XamShowMarketplace*UI` call in the RhythmVerse path.
10. **Audio preview (owner update, 2026-10-20)**: the retail store previews a song on hover. Wire the
    preview only if the API returns a preview asset — it does not: both preview fields are YouTube
    web pages (§3.3). V1 therefore ships **no audio preview**, and no YouTube/browser path exists to
    fake one.

> **Superseded 2026-10-22 by the owner pass (§9), revised 2026-10-23 and 2026-10-24.** The settings pair moves
> into `General ▸ Downloadable Content ▸ RhythmVerse` as that page's first row, and the filter is
> **Filters** (`RHYTHMVERSE FILTERS` page title; P9-3, renamed by P9-11); the master now defaults
> **On** (P9-1) and stays hot only if the main-menu row and the store's heading follow a commit
> live - otherwise a change to it asks for a restart (P9-2); the **Customs from RhythmVerse** line
> of item 8 **stays** (the credit-line removal was withdrawn on 2026-10-24), while a download press
> opens a single modal popup that carries the transfer's state (P9-6, delivered 2026-10-24), whose
> **A Refresh** (P9-14, landed 2026-10-24) runs the main menu's own refresh from the installed toast
> so the song is listed without a restart; and
> "Recent" reads **Recently Added** (P9-5). Items 1-10 above otherwise stand as the 2026-10-14
> requirement.

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
  `{net_cache_mgr init {find syscfg store netcache_init}}` + `{net_cache_mgr load kNetCacheMgrCacheSizeLarge}`.

### 2.2 Screens, panels, list resources

Decoded from `store.dtb` / `store_info.dtb` and the ui/splash registries:

| Screen                            | Panels                                                                                                                                                                          | Notes                                                                                   |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `store_menu_screen`             | background, shell_music, menu_cache, store_banner, store_menu, upsell, store_panel                                                                                              | `filter.lst` menu list; `header_index 2`; back = splash                             |
| `store_browser_screen`          | background, shell_music, menu_cache, **store_browser_panel** (`store_browser.milo`, `song.lst`), store_panel, **store_details_panel** (`store_details.milo`)  | the offer list, chunked (`next_chunk`/`previous_chunk`), shortcuts, preview toggles |
| `store_info_screen`             | background, shell_music, menu_cache, store_banner, store_menu, **store_info_panel** (`store_info.milo`, `buttons.lst`), store_panel, store_details_panel, error_popup | single-offer detail; buy/download buttons                                               |
| `store_info_unavailable_screen` | —                                                                                                                                                                              | retail error target (`enum_error` routes here)                                        |

Assets in the retail ark (`main_xbox.hdr`): `ui/store/gen/{store,store_main,store_menu,store_browser, store_info,store_details,store_info_enum,store_shortcut,album_art}.milo_xbox` +
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
  `show_offer_ui`) whose failure paths are visible in `store.dtb`: `notify "StorePanel Commerce Session in an illegal state (kSessionAborted)."` / `fail "… (likely kSessionNone)."`.
- Native classes behind it (RTTI in the image): `BandStorePanel`, `BandStoreOffer`,
  `BandStoreShortcutProvider`, `RndTexBlender`, plus `NetCacheMgr` / `NetCacheMgrXbox`.

### 2.4 The offer model as the DTA consumes it

Fields read by the decoded scripts (`offer get_data [ … ]`, `offer has_data [ … ]`, direct
properties): `name`, `artist`, `album` (also `offer_album_name`), `genre`, `year_released`,
`review`, `release_date_str`, `first_char`, `pack_first_letter`, `pack_name`, `album_link`,
`pack_link`, `preview_audio`, `preview_art`, `short_name`, `rank <part>`, plus
`offer_type` (song/pack/album), `is_available`, `is_purchased`, `in_library` (`store_in_library`),
`is_completely_unavailable`, `has_available_pack`, `has_available_album`, `has_data`.

Difficulty display: for parts `guitar drum vocals bass keys` the script builds `sprintf "diff_%s" <part>` and asks `song_mgr rank_tier rank <part>` — i.e. the record needs one numeric `rank` value
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

Button visibility is data-driven: `purchase_allowed = {offer is_available} && !{offer_provider show_browser_purchased offer}`; `{offer is_purchased}` chooses "download again" vs "buy now";
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

From [dlc.md](../../guides/dlc.md): the host walks the DLC root and mounts STFS containers; the layout
rule accepts structured `<title_id>/<content_type>/<package>` **and** a flat library of loose
containers. RB3 marketplace content lives under `45410914/00000002/…`. The verified probe shows
one download was a complete `CON ` container whose length equalled the API's `file.size`. Install
only after §3.1 validation. **Preserve the resolved root's layout**: use a short filename under
`45410914/00000002/` for a structured root; add a short filename directly to an existing flat root.
Adding a title-ID directory to a flat root would make `HasStructuredLayout()` classify it differently
on the next boot, potentially excluding the existing loose packages.

Stage incomplete files outside enumeration (§5/P5). Host cache refresh and **guest song discovery**
are separate operations: `RefreshConfigured()` refreshes configured flat libraries, and
`PublishPendingRefresh()` publishes them to the content manager; neither explicitly starts the
guest's song enumeration. Structured package paths are captured at boot and reused during that
refresh. P5 must close both gaps; a restart is the honest fallback until live discovery is proven.

Guest-side, the store's own download machinery (`songs_download` on `cntNNNNNNNN:` content
devices, seen in boot logs) is Xbox-LIVE content plumbing we are **not** reviving.

### 2.10 Project machinery this feature builds on (all proven)

- Patch ark / `PayloadOverlayDevice` record replacement, incl. "extend the mounted part in place"
  (the black-screen fix) — delivery for DTA edits (`store.dtb`, `store_info.dtb`, `splash.dtb`,
  `config/gen/store.dtb`) and for generated `ui/store/remote_metadata/**` if we add a side.
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

### 2.11 Additional code references checked in this review

These are existing code entry points, not claims that the store feature is already implemented.
Repository links are relative to this plan; decoded retail evidence remains local under `out/`.

| Work                       | Reference                                                                                                                                                                                                                                                                                                                                                                                                  | What execution must account for                                                                                                                                                                                                                                                                                                                                                                                                               |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Settings rows              | [settings_screen.cpp](../../../src/ui/settings_screen.cpp), `AllPanels()`/`InSongPanels()`, `settings_dlc_panel`, `BoolRow`/`SubmenuRow`                                                                                                                                                                                                                                                          | Extend the existing DLC page and add the filter child, including its pause-context twin and Back target. A schema group alone does not generate these authored rows.                                                                                                                                                                                                                                                                          |
| Settings contract          | [model.h](../../../src/settings/model.h), [model.cpp](../../../src/settings/model.cpp), [schema.cpp](../../../src/settings/schema.cpp), [hot_apply.h](../../../src/settings/hot_apply.h)                                                                                                                                                                                                                       | Allocate six unused stable IDs without renumbering; add tokens/defaults/storage mappings and a real live applier. Merely marking rows `kHot` falls back to restart when no applier handles them.                                                                                                                                                                                                                                            |
| Store/refresh collision    | [menu_options.cpp](../../../src/ui/menu_options.cpp), `TapRefreshCache`; [menu_filter.cpp](../../../src/hooks/menu_filter.cpp), `PatchFile`/`Configure`; [menu_compat.h](../../../src/settings/menu_compat.h)                                                                                                                                                                                           | `splash_dlc` is the R7 refresh token and protected from the default offline hide set while it owns refresh. Use a distinct proposed `rbbz_rhythmverse` token; do not change that ownership.                                                                                                                                                                                                                                               |
| Authored menu/payload      | [settings_screen.h](../../../src/ui/settings_screen.h), `RerouteRow`/`RemoveRow`/`AddLocaleEntry`; [menu_payload.cpp](../../../src/settings/menu_payload.cpp), `BuildMenuRecords`/`ExtendMenuArchive`; [settings_payload.cpp](../../../src/hooks/settings_payload.cpp)                                                                                                                              | `RerouteRow` only replaces an existing dispatch case. Add an idempotent main-menu insertion helper for both option and case, and compose edits into the existing mounted payload. On/Off needs runtime list repopulation, since records are built at boot.                                                                                                                                                                                  |
| Guest bridge               | [settings_bridge_hooks.cpp](../../../src/hooks/settings_bridge_hooks.cpp), `sub_8241DFB0` / `sub_8241DE48`; [settings_bridge.h](../../../src/hooks/settings_bridge.h)                                                                                                                                                                                                                                    | Builtins `file_exists` and `write_file` are hooked already. Current readback is **one bit**, not arbitrary strings; state/progress need predicates or an explicitly implemented typed return extension. Search text also exceeds the current ASCII token grammar.                                                                                                                                                                   |
| DLC inventory              | [dlc.h](../../../src/hooks/dlc.h), `LibraryPackagePaths`; [dlc.cpp](../../../src/hooks/dlc.cpp), `RebuildPackagePaths`/`ScanLibraryForRefresh`                                                                                                                                                                                                                                                         | Package paths cover configured structured + flat sources, but contain no song title/artist and are not a guest loaded-song list. Copy on the owning thread before worker use; add source coverage for normal content-manager installs if using this fallback.                                                                                                                                                                                 |
| Package metadata fallback  | [shell_music.cpp](../../../src/hooks/shell_music.cpp), `MountPackage`; [stfs_container_device.h](../../../rexglue-sdk/include/rex/filesystem/devices/stfs_container_device.h), `Initialize`/`ResolvePath`; [file.h](../../../rexglue-sdk/include/rex/filesystem/file.h), `ReadSync`                                                                                                                   | The project already mounts STFS and extracted packages read-only. Reuse that reader for `songs/songs.dta` rather than implementing an STFS extractor from scratch. A textual DTA parser and loaded-content association still need work. The menu-music pool itself is not the metadata index.                                                                                                                                               |
| Header validation/layout   | [dlc_library.h](../../../src/fs/dlc_library.h), `ReadPackageIdentity`, `kMaxContentFileName`, `LibraryContentType`; [dlc_layout.h](../../../src/fs/dlc_layout.h), `HasStructuredLayout`                                                                                                                                                                                                              | Scanner accepts `CON `, `LIVE`, `PIRS`; 4-byte magic alone is insufficient. Guest filenames have a **42-byte** limit. Flat libraries get aliases for long names; structured paths keep the actual filename. Use short generated install names, not artist/title filenames.                                                                                                                                                        |
| Paths/refresh              | [paths.cpp](../../../src/settings/paths.cpp), `ResolvePathValue`; [dlc.cpp](../../../src/hooks/dlc.cpp), `Configure`/`RefreshConfigured`/`PublishPendingRefresh`; [refresh_state.h](../../../src/fs/refresh_state.h)                                                                                                                                                                                  | Reuse the boot's resolved DLC destination. `paths.toml` relative paths and legacy cvar relative paths have different bases. Refresh is cache-toggle-gated, skips an empty flat-root list, and does not rebuild structured paths. Test cache On and Off.                                                                                                                                                                                     |
| HTTP                       | [download.cpp](../../../installer/src/download.cpp), `InternetHandle`/`DownloadToFile`/`SplitUrl`                                                                                                                                                                                                                                                                                                     | RAII handles, explicit timeouts and a proxy-aware access type are useful. Note the file's own reason for following GitHub's redirect: `WinHttpSetOption(..., WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS, ...)` for release assets. **Do not copy that policy** for RhythmVerse; validate each redirect manually. (This row used to cite `controller-settings/src/updater.cpp`, which was deleted on 2026-10-15 with D19's update check.) |
| JSON dependency            | [vendored json.hpp](../../../rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp)                                                                                                                                                                                                                                                                                                             | A JSON implementation already exists in the SDK tree. Verify the build target/include/license arrangement before adding another dependency; do not write a partial JSON parser.                                                                                                                                                                                                                                                               |
| Existing regression suites | [settings_screen_tests.cpp](../../../tests/settings_screen_tests.cpp), [settings_hot_apply_tests.cpp](../../../tests/settings_hot_apply_tests.cpp), [menu_compat_tests.cpp](../../../tests/menu_compat_tests.cpp), [dlc_library_tests.cpp](../../../tests/dlc_library_tests.cpp), [dlc_layout_tests.cpp](../../../tests/dlc_layout_tests.cpp), [refresh_state_tests.cpp](../../../tests/refresh_state_tests.cpp) | Extend the relevant fixtures during implementation: exact menu defaults/navigation, live commit/cancel, refresh-row ownership, staging exclusion, layout preservation and publish-once refresh behavior.                                                                                                                                                                                                                                      |

### 2.12 Free-download UI: exact price seam

`out/_swap/store_info.tree.txt`, `refresh_panel` (lines 326–377), first hides `cost_ps3.lbl` and
`cost.lbl`, then calls `set_offer_cost` and shows one again when `purchase_allowed` is true.
For RhythmVerse offers, remove/guard that re-enable block and keep both labels hidden on entry,
selection change, refresh and return from a download. Preserve useful Download buttons separately:
forcing `purchase_allowed` false can suppress them along with the price.

`refresh_buttons` (same dump, around lines 160–180) switches on `is_purchased`; its
`show_browser_purchased` dependency means simply setting an ownership flag is not enough to promise
a visible Download Again action. Author explicit free-download states and suppress commerce prompts
in the `store_buy_now` case as well as bypassing the actual checkout call.

Browser row prices/icons can be supplied by native list providers or `.milo` resources rather than
the two DTB scripts. P2 must inspect `song.lst`, `list_store_*` resources and their providers; P3
must prove the browser, details and any retained recommendation area show no monetary UI.

---

## 3. RhythmVerse API (2026-10-13 research; list route rechecked 2026-10-14)

| Item                                 | Value                                                                                                                                                                                                                                                                                                           |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| List                                 | `POST https://rhythmverse.co/api/rb3xbox/songfiles/list`                                                                                                                                                                                                                                                      |
| Search                               | `POST https://rhythmverse.co/api/rb3xbox/songfiles/search/live`                                                                                                                                                                                                                                               |
| Body                                 | `x-www-form-urlencoded`                                                                                                                                                                                                                                                                                       |
| Fields                               | `instrument[]` (repeatable; `drums, guitar, bass, vocals, keys`), `sort[0][sort_by]=update_date`, `sort[0][sort_order]=DESC`, `data_type=full`, `text` (search only, ≥3 chars), `page`, `records` (site uses 25)                                                                               |
| Scoping                              | gameformat is the route token (`rb3xbox`); a `gameformat` body param is rejected. The generic `all` route returns mixed formats.                                                                                                                                                                          |
| Auth                                 | none for list/search/download                                                                                                                                                                                                                                                                                   |
| Response                             | `{status:"success", data:{records:{total_filtered, returned}, pagination:{page, records}, songs:[{data, file}]}}`                                                                                                                                                                                             |
| Fields we consume                    | `file.file_id, file_name, gameformat, file_artist, file_title, file_album, file_year, file_album_track_number, file_song_length, file_genre, size, downloads, album_art, download_url, diff_*, rank_*` (+ `data.album_art` fallback)                                                                        |
| Preview fields (kept, never fetched) | `file.file_preview` and `data.song_preview` are YouTube web pages; there is no audio asset. Parsed onto the offer model for evidence and classification only — §3.3.                                                                                                                                      |
| Form keys                            | Sent percent-encoded (`instrument%5B%5D=guitar`, `sort%5B0%5D%5Bsort_by%5D=update_date`); measured 2026-10-20 with `records=1` — HTTP 200, `status=success`, one `rb3xbox` record (`out/_swap/rv_p0_encoded.json`). Literal brackets also work, but the encoded spelling is what the client emits. |
| Download                             | `file.download_url` is a candidate, not proof of a raw package. Apply §3.1. The scoped `rb3xbox` route also returns external links (Dropbox observed on 2026-10-14).                                                                                                                                       |
| Search rule                          | a too-short query returns `{"status":"error","error":{"message":"You need to provide a text at least 3 characters long"}}`                                                                                                                                                                                    |
| Search matching                      | Artist alone OR song title alone; combining both in `text` fails to find the intended result (owner-reported, 2026-10-14; not independently re-probed). This is not a one-word restriction.                                                                                                                   |

Review evidence: the owner's `list.json` contains 25 entries (17 `rb3xbox`, 8 `rv`); `search.json`
contains 8 (6 `rb3xbox`, 2 `rv`). The ZIP entries in those captures are `rv`; they are useful
negative fixtures but do not prove all `rb3xbox` downloads are uncompressed. A fresh scoped Recent
request with the four default instruments returned 25 `rb3xbox` entries, of which 23 had
root-relative `/download_file/...` links. The store must tolerate fewer eligible entries than the
API page size. Its `total_filtered` is a server count, not a count of downloadable store entries.

Keep IDs opaque: one scoped result had `file_id = "5b947da4929df7.93833456"`, `size = 0`, a
Dropbox link, and null ranks. Pagination `page`/`records` were JSON strings, while counts were
numbers. Parse these documented variations deliberately; missing/null optional fields must not
crash the client. Zero size means unknown, not a valid zero-byte package.

The existing `out/_swap/rvp_list_rb3xbox.json` is actually an HTML 404 response, so it is **not** a
scoped-route JSON fixture. The new successful captures are indexed in §8. API authentication,
search behavior and the raw-package probe retain their original measurement dates; this review
did not download a song body or re-test search.

### 3.1 Direct-link and archive policy (v1: skip archives)

Apply the same checks to Recent and Search, then repeat them when starting a download:

1. **Catalogue eligibility:** require exact `file.gameformat == "rb3xbox"` and a nonempty
   `file.download_url`. Resolve root-relative `/download_file/...` against `https://rhythmverse.co`.
   Parse absolute URLs structurally: HTTPS, case-insensitive exact host `rhythmverse.co`, default
   port 443, no credentials, and the approved direct-file path. Reject scheme-relative URLs,
   lookalike hosts, backslashes/control characters, malformed escapes, and paths that normalize
   outside `/download_file/`. Encode spaces/Unicode correctly without double-encoding existing
   escapes. Do not substitute `file_url` or `download_page_url` (web pages).
2. **Reject known archives before a transfer:** inspect both `file.file_name` and the safely
   percent-decoded URL path basename, case-insensitively, excluding query/fragment. Reject `.zip`,
   `.rar`, `.7z`, `.tar`, `.gz`, `.tgz`, `.bz2`, `.xz` and multipart variants. A `.con`, `_rb3con`
   or extensionless name is only a candidate. Do not require a `.con` suffix: the captures have
   valid-looking extensionless direct links. Negative decisions may be cached by file ID/URL.
3. **Response policy:** disable automatic redirects; resolve and validate each `Location` before
   following, with a small hop limit. No off-site/CDN exception in v1 without a separately reviewed
   policy change. Inspect `Content-Disposition` filenames (including `filename*`) and
   `Content-Type`; reject archives and HTML/JSON/error/login pages before consuming the body.
   An optional HEAD can reject early, but missing/unsupported HEAD is not proof either way: use
   the streaming GET's headers and prefix. Do not HEAD every catalogue item while scrolling.
4. **Byte validation:** read a bounded prefix and abort on ZIP/RAR/7z signatures or any non-STFS
   magic, including HTML with an octet-stream header. Only `CON ` / `LIVE` / `PIRS` proceed to
   bounded streaming and structural package validation. A server that ignores `Range` may return 200;
   close the stream on rejection instead of reading the full body. Use `ReadPackageIdentity`
   plus the SDK package reader; validate RB3 title/content compatibility and readable song
   metadata, not just the first four bytes. A `rb3xbox` API tag never overrides invalid bytes.
5. **Length and completion:** enforce a maximum transfer size even if length is unknown. Compare
   final bytes with positive API size and HTTP Content-Length when available; reject disagreement
   or truncation. Missing/zero API size or chunked HTTP requires bounded streaming plus package
   validation, not an equality check against zero. Disable transparent content decoding or account
   for it explicitly so transfer-size checks use the same representation.
6. **Failure behavior:** known archives/off-site links are omitted from both lists. If an opaque
   link reveals an archive at download time, show **Archive downloads are not supported**, clean
   the partial file and mark the offer unavailable for that URL revision. Never install an archive,
   launch an extractor/browser, or show Downloaded for a failed transfer. Support internal extraction
   only as a later feature with its own bounded extraction and package-validation design.

Split this into a cheap metadata predicate (`IsEligibleOffer`) and transfer-time validation
(`ValidateDownloadResponse`/`ValidatePackage`); a URL-only `IsDownloadable` cannot certify bytes.
Apply the same HTTPS/redirect discipline to artwork, with its own approved image paths and decoder
size limits; download-file path rules are not image path rules. Use a placeholder for disallowed art.

### 3.2 Lazy downloaded matching against the loaded DLC library

Primary source: snapshot DLC entries known to the guest song manager after content discovery,
copying song title, artist and DLC/source identity on the guest-owning thread. Locating the Blitz
accessors/notification is P2 work, not an existing exported host API. If this cannot be reached,
read `songs/songs.dta` through the existing read-only package reader for content the game has
accepted/mounted; handle multi-song packs and extracted package directories. That fallback must
cover all loaded DLC sources, not only paths recorded by the store or the menu-music pool.

Build the normalized `(artist, title)` set only when the store first needs it. Compare displayed
offers against that set and cache answers by library generation. Use trimmed/collapsed whitespace
and consistent Unicode-aware case folding on both sources; require both fields to be nonempty.
Do not remove punctuation, version suffixes or featured-artist text, and do not use fuzzy matching.
This is the owner's intentionally simple approximation: alternate charts with the same artist/title
can all show Downloaded, while differently named versions can miss. Keep per-file offers visible
and allow an explicit Download Again action; do not collapse them by title.

Until the index is ready, the state is **unknown/checking**, not a negative match. Invalidate after
the loaded-library generation changes, a refresh/discovery completes, or a successful installation
is handed to discovery. While discovery is pending, show **Downloaded — refresh/restart required**
from the verified install result; do not claim the song is already playable. A receipt may track
this pending state and transfer history, but stale receipts never establish loaded ownership after
deletion or a root change. No per-row filesystem scan and no guest-memory reads from HTTP workers.

### 3.3 Preview audio — measured 2026-10-20: the API returns no audio asset (decision)

The retail store previews a song on hover (§2.6). Owner rule (2026-10-20): implement the preview only
if the API returns something usable; otherwise ship without it.

| Field                 | Content                                                                  | Count                                          |
| --------------------- | ------------------------------------------------------------------------ | ---------------------------------------------- |
| `file.file_preview` | `https://www.youtube.com/embed/<id>`                                   | 14/25 in `list.json`, 4/8 in `search.json` |
| `data.song_preview` | `https://www.youtube.com/watch?v=<id>` / `https://youtu.be/<id>`     | 0/25 in `list.json`, 4/8 in `search.json`  |
| `file.audio_type`   | `single` / `partial` / `full` — a chart description, not an asset | every song                                     |

Both preview fields are **web pages**, never an audio stream or file. A probe of one song page
(`https://rhythmverse.co/songfile/38a4dcc61d324e7883c9a393a7086bb9`, HTTP 200, 171,707 bytes,
`out/_swap/rv_songfile.html`) contains exactly one preview element — the YouTube
`<iframe id="ytplayer">` — and no `<audio>`, `.mogg`, `.mp3` or `.ogg` reference. The site previews by
embedding YouTube, so there is no RhythmVerse-hosted preview asset to fetch.

Decision:

- V1 ships **no audio preview**. Offers carry no `preview_audio`, which is exactly what hides the retail
  preview UI (§2.5: preview buttons are hidden when `!{offer has_data preview_audio}`). No YouTube link
  is opened, embedded or launched — §1.4 and §7 already forbid a browser and off-site fetches.
- The raw `file_preview` / `song_preview` strings are still parsed and kept on the offer model, so the
  measurement stays visible and a future API revision fails a test: `tests/store_client_tests.cpp`
  classifies every preview URL in the committed fixtures as a web page and pins
  `kApiSuppliesPreviewAudio == false`.
- If the API ever returns a direct audio file, the retail seam is `store_preview_mgr` +
  `preview_audio` + `{offer get_data [preview_audio]}` (§2.6). That is a separate feature with its own
  URL and format/size validation — an audio preview is not a package — not a silent fallback.

---

## 4. Architecture options and the recommendation

### 4.1 Option A — serve `dlc_store/**` metadata through netcache (retail UI, retail parser)

The native offer provider parses files fetched from the configured server. If we (i) route it to a
source we control (`local` server root, or a fetch hook) and (ii) generate those files in the
format it expects, the retail provider supplies menus, lists, details, difficulty tiers and the art
pipeline (`load_art`). The attribution, free-download controls and price hiding still need the
explicit UI edits in §1/§2.12.

- Pros: smallest UI surface; retail behaviour for free; the owner's "two menus" model maps 1:1 to
  the retail menu model.
- Cons: the file **schema is unknown** until instrumented (fields partially known from DTA usage
  and image strings); commerce-session state may still need a stub.
- Unknowns: local path mapping, parser acceptance, art/preview file formats, chunk protocol.

### 4.2 Option B — replace the native panel/offer provider at the method level

Hook `BandStorePanel` + `BandStoreOffer` methods (inventory in §2.7) and answer with RhythmVerse
data; retain retail navigation and apply the same free-download/attribution edits as Option A.

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

| Component                   | First choice                                                                                                                                       | Fallback                                                                                          |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Store opens                 | New `rbbz_rhythmverse` row/case, labelled RhythmVerse Customs and runtime-gated by the persisted enable setting; preserve `splash_dlc` refresh | store-context entry stub if commerce state prevents entry; never change global signin             |
| Menus ("Recent", "Search")  | A: generated top-menu metadata                                                                                                                     | B: hook `get_menu_provider`/`request`                                                         |
| Offer list + fields         | A: generated offer records                                                                                                                         | B: hook provider `build_list` + offer `get_data`                                              |
| Album art                   | A: serve art files the same way (`load_art` path)                                                                                                | pre-convert + feed `album_tex`; last resort placeholder `no_art.tex`                          |
| "Downloaded"                | lazy loaded-DLC artist/title index (§3.2), adapted to `is_purchased`/`in_library` only as UI signals                                          | package-metadata index over accepted DLC sources; still required in v1                            |
| Checkout/download           | DTA patch of the two button cases + `rbbz-store/` bridge ops                                                                                     | hook `check_out`/`re_download`                                                                |
| Free UI / attribution       | guard `refresh_panel` cost block, author download states, RhythmVerse heading                                                                    | provider/resource edits for prices rendered outside DTA                                           |
| Refresh after download      | rescan host sources + publish + guest discovery (OQ6)                                                                                              | verified install plus restart-required message; manual refresh alone is not yet proven sufficient |
| Enable + instrument filters | six native settings leaves under General → Downloadable Content; copied effective values for workers                                              | —                                                                                                |

---

## 5. Phases

Each phase has a measurable exit. Phase IDs are retained for earlier references, but implement
**P0 → P6 → P1 → P2 → P3 → P4 → P5 → P7 → P8**: the enable setting must exist before the
menu gate and client are connected. P2 includes the loaded-DLC metadata probe needed by P3/P5.

### P0 — API client + contracts (SDK-free, no game changes)

- Build `src/store/rhythmverse_client.{h,cpp}` (SDK-free): request builder (`instrument[]`,
  `sort[0]…`, `data_type=full`, `text`, `page=1..`, `records=25`), response model (song/file pair,
  the fields in §3), paging cursor, normalized artist/title matcher, `IsEligibleOffer`, and the
  response/package validation contracts (§3.1). Reuse an established JSON parser (§2.11).
  Preserve unknown optional fields; handle null ranks, opaque IDs and numeric pagination strings.
  Verify multiple `instrument[]` fields enforce the required instruments; tier 0 must not be
  confused with absence. Pin all-Off behavior and reject stale responses after filter changes.
- Host transport: port the installer's WinHTTP pattern (`installer/src/download.cpp` — RAII handles
  and timeouts, **not unrestricted redirects**) into `store_http` for the game exe. All
  network calls on host worker threads; never touch the cvar registry or guest state from them
  (project rule).
- Fixtures: `docs/rythmverse-responses/{list,search}.json` plus a checked successful scoped response
  (the old `rvp_list_rb3xbox.json` is HTML). Add small synthetic malformed/archive/redirect cases;
  never require song downloads for unit tests.
- Tests: `tests/store_client_tests.cpp` (ctest `store_client`) — URL/body building, parse,
  pagination after filtering, matcher, `IsEligibleOffer` (relative/absolute direct links, same-site
  web pages, off-site, credentials/lookalike hosts, scheme-relative, uppercase host, encoded archive
  suffix), redirect policy, absent/zero/mismatched lengths and error mapping.
- Exit: ctest green; no game-code changes.

**P0 record (2026-10-20).** Landed as `src/store/rhythmverse_client.{h,cpp}` (SDK-free: request
builder, listing model/parser, URL policy, offer predicate, transfer validators, matcher),
`src/store/store_http.{h,cpp}` (the single WinHTTP file: `WINHTTP_OPTION_REDIRECT_POLICY_NEVER`, RAII
handles, timeouts, automatic proxy with a default-proxy fallback, a 4 MiB body ceiling) and
`tests/store_client_tests.cpp` (ctest `store_client`, **330 checks**). The JSON dependency is the
nlohmann 3.11.3 single header the SDK tree already vendors
(`rexglue-sdk/thirdparty/inja/third_party/include`), so no third-party code was added; the game
target links `winhttp`. Nothing calls the transport yet — P0's exit is the green suite, not a live
request.

Measurements this phase added (all 2026-10-20):

- **Encoded form keys are accepted**: `instrument%5B%5D=guitar`, `sort%5B0%5D%5Bsort_by%5D=…` with
  `records=1` answered HTTP 200, `status=success`, one `rb3xbox` record
  (`out/_swap/rv_p0_encoded.json`); the client emits that spelling.
- **The chart predicate is `>= 1`, not `>= 0`**: every authored part in the captures is a tier 1..6,
  absence is `-1` (the pro parts) or a missing field, and **keys exist only in the `data` copy**,
  where `0` is the "not authored" spelling (21 of 25 entries). `SongFile::HasChart` treats `-1`, `0`
  and a missing field as absence — the "tier 0" the prompt warned about never appears as an authored
  tier in these captures.
- **`file_song_length` is seconds** (276 for a 4:36 song), so the model names it
  `song_length_seconds`; reading it as milliseconds would make every duration 1000x wrong.
- **`zippata`/`pacchetto` are not an archive signal**: both are 0 on every entry, including the `rv`
  records whose files are ZIPs. Archive rejection stays the name + response-bytes policy of §3.1.
- Fixture facts the suite pins: `list.json` = 25 records / 17 `rb3xbox` / 14 YouTube preview pages;
  `search.json` = 8 records / 6 `rb3xbox` / 8 preview pages; 17 of 17 list entries and 5 of 6 search
  entries are eligible (the sixth is `http://dingorb.com/?id=…` — not HTTPS, not the site —
  `kNotDirectFile`).

Not in P0, deliberately: no live request from the game, no UI, no settings, no download; the
transport has no caller until P3/P5.

**P0 verification (2026-10-20, same day).** The debug tree builds and links the game with the new
sources (`src/store/store_http.cpp` and `rhythmverse_client.cpp` compile into `rb_blitz.exe` and
`winhttp` links), full `ctest` is **38/38**, and the manual live probe
(`rb_blitz_store_client_tests --live-list`, evidence `out/_swap/rv_p0_live.txt`) answers **HTTP 200,
8,546 bytes, 7 headers**, one eligible `rb3xbox` record ("Bush - The People That We Love"). The probe
found one real defect before any caller existed: `WinHttpConnect` needs a null-terminated host name,
and the component `WinHttpCrackUrl` returns points into the URL buffer — so the call failed with
`12005 ERROR_WINHTTP_INVALID_URL` until the host was copied into its own string.

### P1 — the store opens without Xbox LIVE

- Add the `rbbz_rhythmverse` option and dispatch case through the existing payload composition,
  labelled **RhythmVerse Customs**. Gate population and entry on the persisted enable value via
  a one-bit `rbbz-store/enabled` query. Rebuild the visible menu when the setting changes.
  Preserve R7's `splash_dlc` refresh action/label and its explicit Hide Items behavior. When cache
  is Off, suppress any remaining obsolete retail store row while RhythmVerse is On so there are
  not two store entries. RhythmVerse Off preserves the existing retail/R7 menu policy.
- Verify the new row participates in `MenuInventory` and explicit Hide Items choices; default
  offline filtering must not remove the new usable store. A plain `RerouteRow(splash_dlc, …)`
  is not this implementation. If entry needs additional state, use the local store context only.
- Acceptance of this phase is modest on purpose: `store_menu_screen` opens, draws, B returns; the
  offer path is allowed to land on `store_info_unavailable_screen` (retail handles empty data).
- Evidence: screenshots via `drive_ui.ps1`, log lines for the bridge gate hit.
- Exit: store opens in ≥5 consecutive boots; the master toggles availability without restart;
  Refresh Music Library still dispatches its sentinel. Exercise cache On/Off, store On/Off and
  Ultimate present/absent. Control records outside the intended edits remain unchanged.

**P1 record (2026-10-20).** The main menu now carries **RhythmVerse Customs** (`rbbz_rhythmverse`),
built by `ui::screen::AddRhythmVerseRow` and composed by `src/settings/menu_payload.cpp` after the
restored Download Content row (after `splash_dlc` when that row is absent). Its case contains no
online check and no Marketplace call: it enters the retail `store_menu_screen` only while
`rbbz-store/enabled` answers true, a one-bit query the host answers from the store's settings
snapshot (`store::AnswerStoreQuery`, hooked in `settings_bridge_hooks.cpp` beside the play probes, so
it works even with the settings channel off). The row and its label exist only on a boot whose
master switch was on.

Measured: `settings_screen` pins the insertion (right after `splash_dlc`, refused when off, label
present, case asks the gate and enters `store_menu_screen`, a second pass refused, the record
reparses) and `store_client` pins the query answers (`enabled`, `instrument/<token>`, unknown paths
refused); full `ctest` is **38/38** and the game target links.

Honest limits — P1's exit is partly met:

- **The row's presence is baked at boot.** The exit asks for "the master toggles availability
  without restart"; the *press* is live (the case re-reads the gate), the *row* is not: records are
  generated once per boot, so turning the store off leaves the row drawn until the next start, and
  turning it on needs a restart to appear. A live presence needs a menu-rebuild path (the settings
  screen repopulates its own lists after a commit; the main menu's records are compiled at boot).
- **The browser acceptance was run on one boot on 2026-10-20 (see the P2 record below)**: the row
  is drawn, the press opens `store_menu_screen`, and B returns to the main menu. Two defects had to
  be fixed for that to be true, and both are recorded there: the row's placement (a row appended
  after `splash_dlc` is selectable but off the visible page) and the R5 hide decision (its in-flight
  edit is not delivered for this record, which is what pushed the row off the page).
- Not yet exercised: ≥5 boots, cache On/Off, store On/Off after a restart, Ultimate absent, and
  Refresh Music Library's sentinel in the same run.
- **P1 defect (placement) closed 2026-10-23**: the store entry was drawn *last*, after EXIT GAME,
  because the only selectable position measured was the retail store row's own (`splash_dlc` follows
  `splash_exit` in the shipped array) and inserting before `splash_exit` produced a row whose label
  drew and whose navigation was skipped. The row's *placement* is now a separate step:
  `ui::screen::OrderMainMenuRows` (src/ui/settings_screen.cpp) permutes the option array after every
  insertion, so the menu draws **PLAY / PLAY CURRENT SONG / RHYTHMVERSE / REFRESH SONGS / HELP &
  OPTIONS / EXIT GAME** with EXIT GAME always last, while the symbol is still inserted at the one
  position measured to navigate ([main-menu.md](../../engine/main-menu.md) §7.4). Pinned by
  `rb_blitz_settings_screen_tests` in both shipped array shapes; the drawn order and the highlight
  are **unproven on screen** until a boot is measured.

### P2 — instrument the data layer (DECISION GATE: A vs B)

- Log every guest file open / netcache fetch whose path contains `dlc_store`, `dlc_top`, `offers`,
  `related`; log the commerce-session methods as they are called; try `default_server local`
  (payload edit of `config/gen/store.dtb`) with an empty `ui/store/remote_metadata/` and capture the
  retail error strings (they name the missing/parse-failed file).
- Deliverables: (1) the exact request list for opening the store to a list of N offers; (2) the
  path-mapping rule for `local` (or proof that a fetch hook is the only route); (3) first schema
  guesses validated by serving one hand-made file and observing the retail reaction; (4) the
  loaded-DLC title/artist source and library-generation notification, or a bounded package-reader
  fallback with complete source coverage; (5) every price/icon/provider surface and a tested hide
  path. Record guest addresses/signatures only after verifying them against Blitz.
- Exit: a written A/B decision with evidence; if A is chosen, a skeleton of the metadata writer;
  if B, the hook inventory (which methods, which addresses, which signatures).

**P2 record (2026-10-20) — the instruments, the first two boots, and a decision that is *not* yet
evidence-backed for A.**

Landed, and tested without a boot (`store_payload`, **32 checks**; full `ctest` **39/39**):

- `src/store/store_payload.{h,cpp}`: rewrites `config/gen/store.dtb`'s `netcache_init` so every
  `servers` table gains (or replaces) a `local` entry rooted at a caller-chosen guest path and every
  `default_server` selects `local`. It rewrites *all* the platform branches rather than guessing
  which `#ifdef` the guest evaluates, keeps the rest of the record (`groupings`, `dummy_upsell_offer`,
  PS3 terms) untouched, and refuses (leaving the tree byte-identical — pinned by a test) when the
  record has no `netcache_init`, no `default_server`, or the root is empty.
- `src/store/store_probe.{h,cpp}`: the two measurement switches, `store_trace` and
  `store_local_server_root`; the trace is a single function-pointer observer installed on
  `fs::PayloadOverlayDevice`, the one device every `game:`/`d:` path resolves through, so an open
  costs one null check when off.
- `scripts/probe_store.ps1`: one boot, title → main menu → N rows → the store → (optionally) its
  first menu row, with a screenshot and OCR of the menu, the *selected* row, the store and the
  return, a store-probe census, and the log copied beside it (`out/_swap/store-probe/<stamp>/`).
- `tests/store_payload_tests.cpp` also carries `dump-menu <dtb>` and `dump-strings <dtb>`, which is
  how a boot's generated record was checked against the guest's: the harness extracted
  `cache/settings-menu/<pid>/gen/patch_xbox.hdr` with `scripts/hmx_ark.py` and dumped it.

**Measured, boot 1–2 (`out/_swap/store-probe/20261020-132416`, `…-133319`):**

- P1's row was authored into the generated record — `dump-menu` shows
  `… splash_exit splash_dlc rbbz_rhythmverse` — and the locale carries `rbbz_rhythmverse = "RhythmVerse Customs"`, yet the menu did not draw it. The row *was* selectable: six taps past the
  first row, A opened `store_menu_screen`. So: authored, functional, and seventh of six visible
  rows. Two separate defects, both now fixed:
  1. **R5's hide decision is not delivered by the in-flight edit.** The boot warns that the edit at
     ark offset 16041952 (that is `ui/splash/gen/splash.dtb`) "reaches into the first 14368 byte(s),
     which this read does not carry and the guest has already copied; it was not delivered" — so
     LEADERBOARDS and ACHIEVEMENTS were still drawn even though the log said `hid 3 row(s)`. The
     decision is now **baked into the generated record** (`ui::screen::HideMenuRows`, carried
     through `BuildMenuRecords` from `menu_filter::HiddenRows()`); the in-flight edit stays as the
     fallback for a boot with no payload.
  2. **A row inserted before `splash_exit` is drawn and skipped by navigation.** With the row placed
     above EXIT the menu read PLAY / PLAY CURRENT SONG / HELP & OPTIONS / RHYTHMVERSE CUSTOMS / EXIT
     GAME, but three and four taps both landed the highlight on EXIT GAME. The only *selectable*
     position measured is the retail store row's own: appended after `splash_dlc`, i.e. last. (The
     drawn position was a separate problem, fixed 2026-10-23 by `OrderMainMenuRows`, main-menu.md
     §7.4; the insert index is still this one.)
- `settings_menu`'s P1 boot line now says *requested*, and every optional authoring refusal is
  reported (`BuildMenuRecords`'s new `notes`, logged at warn) — before that, a refused row and a
  built row printed the same sentence.

**Measured, boot 3–4 (`…-134915`, `…-135143`):** with the fixes and the store's master switch on,
the menu reads **PLAY / PLAY CURRENT SONG / HELP & OPTIONS / EXIT GAME / RHYTHMVERSE CUSTOMS**, the
fifth row opens the retail store, and B returns to the same menu (screenshots + OCR in the
evidence directory). That is P1's browser half on one boot. The store draws **MUSIC STORE /
RECOMMENDED / YOUR SONGS / BACK / HELP & OPTIONS / ALL PRICES ARE IN MICROSOFT® POINTS** over a
central **LOADING**, and stays on LOADING: no `store-probe` line follows — the store never resolves
or opens a single `ui/store` path, in the front screen *or* after its first menu row was accepted.
It is waiting on data it has not asked the file system for.

**What that meant for the gate at the first pass (kept; its conclusion is superseded below).** A was
neither proven nor disproven by that pass:

- The **door** is real: an offline entry reaches the retail store screens, so the UI half of A needs
  no Xbox LIVE state (measured, above).
- The **data half** was unmeasured: nothing requested a metadata file, so the `local` path mapping,
  the schema, and the parser's reaction were all unknowns. The guess was that the netcache's request
  is issued through the panel's commerce/request state machine before any file fetch, and that state
  never advances offline — the plan's OQ3. The next measurement was therefore *not* another schema
  guess: it was to instrument the request itself and to confirm whether the generated
  `config/gen/store.dtb` is the record the panel read at all.
  *(Superseded the same day by the second pass: the DTA **does** issue the request, and the native
  side does nothing observable after it. The block is after the request, not before it.)*
- Deliverables 4 (loaded-DLC metadata source) and 5 (price/icon hide path) are untouched. The
  price surface is visible in the measured screen (`ALL PRICES ARE IN MICROSOFT® POINTS` is drawn
  even with no offers), so it is a real target for P3, not a hypothetical.

**Second pass, same day — the request is issued and the native fetch is inert. P2's decision.**

Two boots under `scripts/probe_store.ps1`, both with `--store_trace=1 --store_local_server_root=ui/store/remote_metadata/`: `out/_swap/store-probe/20261020-142448` (the
control leg, on the profile as it stands — master off) and `…-142749` (the measured leg, with
`general.rhythmverse = true` written into `settings.toml` for that boot and restored afterwards, and
with `--log_level=trace --log_noisy=1`; the script grew `-Noisy` for it).

- **The route is stable and P1's baked presence behaves as documented.** With the master off the
  fifth row is EXIT GAME (its accept raised the exit confirmation and B returned to the title);
  with it on the fifth row is RHYTHMVERSE CUSTOMS and it opens the store. The store drew MUSIC
  STORE / MESSAGES / RECOMMENDED / YOUR SONGS / BACK / HELP & OPTIONS / ALL PRICES ARE IN
  MICROSOFT® POINTS. The capture taken right after entry had **no** LOADING; the one taken after
  accepting the first row did — so in this boot the loading state follows the accept, i.e. the
  request is what turns it on.
- **The DTA asks, and the loading state is the panel's own.** `out/_swap/store.tree.txt`'s
  SELECT_MSG branch is `{if {store_panel user_can_do_input} && {this has_menus}}` →
  `{store_panel set_source {sprintf menu_%s {provider get_string ix}} TRUE}` →
  `{store_panel request {provider get_string ix} TRUE}` → `{store_panel update_loading_status …}`.
  A LOADING on screen is therefore an *in-progress request*, not an idle menu.
- **Nothing follows the request.** Trace census while `store_menu_screen` was up (90 s, log lines
  251400–327070): `XamInputGetState` 28,742, `[XE_SWAP]` 2,031, `NtReadFile` 272,
  `xeRtlNtStatusToDosError` 136 — **and no other export at all**. `XamContent*` does not appear, and
  `NetDll_*`/`XNet*` do not appear anywhere in the 48 MB boot. The only XAM call at entry is
  `__imp__XamBackgroundDownloadSetMode` — a **STUB** — twice: 14:29:24.966 (the screen line is
  14:29:24.975) and 14:29:58.763 (leaving).
- **No store file is opened, and a path probe cannot see this class of record.** The whole boot
  carries 30 `NtCreateFile` lines, all of them the two ark headers + parts (`d:\gen\main_xbox.hdr`,
  `d:\gen\main_xbox_0.ark`, `d:\gen\patch_xbox.hdr`, `d:\gen\patch_xbox_0.ark`) plus a handful of
  special files (`game:\scores`, `game:\screenshots\_keepme`, `game:\ulti_settings.dta`,
  `globaloptions:`, `menumusic4:\songs\…mogg`, `songcache:`). `config/gen/store.dtb` is opened
  **nowhere** — not at boot, not at the store — and neither is any `ui/store/**` or `dlc_store/**`
  path. The UI's own records arrive from the mounted arks **by offset**, which is exactly why R5's
  hide decision needs the in-flight read edit (first pass, above) and why the path observer stays
  silent for them. The probe's filter now also catches `*store.dtb`, so this absence is measured
  rather than filtered away.
- **Content enumeration is a boot-time operation here:** `XamContentAggregateCreateEnumerator` added
  1,773 items at 14:28:16 (the DLC library), 68 s before the store existed. The store itself makes
  no content call.

**Decision: A, narrowed to the fetch seam. Not B.**

- A's UI half is measured working offline: navigation, the panel, the list widgets, the art
  pipeline and the DTA's own request all run. What is missing is the native step *behind* `request`
  — measured inert, above — so A now means "retail UI + retail DTA + one replaced fetch entry",
  not "serve files to the retail netcache and let it work".
- B (replace the panel/provider methods) is rejected: it would rebuild a surface that is measured
  working, and its own first step — locating the native store entry points — is identical to A's.
- The rejected reading of A (rewrite `netcache_init`, serve `dlc_store/**` files) is disproven as a
  *sufficient* route, because the panel never reaches a file fetch to be served. It is not disproven
  as a *data format*: once the fetch entry is ours, the netcache's request names and the DTA reader
  are still the retail ones this plan already mapped, and the writer skeleton below targets exactly
  them.

Landed with the decision (`store_metadata`, **29 checks**; full `ctest` **40/40**):

- `src/store/store_metadata.{h,cpp}` — the request model the plan measured (`dlc_top_%s_%s.dta`,
  `dlc_store/%s/%s/offers/%s/`, `dlc_store/%s/%s/related/%s.dta`, source id `menu_<id>`) and the
  **writer skeleton** P2's exit asks for: `BuildFrontMenuRecord` emits a compiled DTA carrying the
  owner's two menus (Recent, Search), refuses an empty menu or a nameless/unlabelled entry, and
  keeps the record's field names in one place because they are still a guess. `tests/ store_metadata_tests.cpp` pins the names, the refusals and the compile→parse round trip — never
  the retail parser, which no boot has reached.
- The writer is **not** wired into the payload: with the fetch seam missing there is no caller, and
  a served record nobody reads is worse than none (it would look like progress in a boot log).

**P3 step 0 (the next measurement, named so it is not rediscovered):** locate the native fetch
entry behind `store_panel request`. *(Landed later the same day — see the P3 step 0 record below:
the entry is `sub_8274FB60`, reached from the panel's `Handle` and from `sub_82753010`, and both are
hooked.)*

Status: **P2 is exited (2026-10-20)** with the decision above. Remaining P2 deliverables 4
(loaded-DLC metadata source) and 5 (price/icon hide path) move into P3, whose exit needs them.

### P3 — first light: "Recent" list with real data + art

**P3 step 0 record (2026-10-20) — the request chain is mapped, and the store's own DTA is editable.**
This is the serial half of P3; the data lane (schema, offers, art) starts here.

The native chain, read out of the decrypted image (`out/_swap/blitz_image.bin`, base 0x82000000 —
confirmed by the one pointer to `./ui/store/gen/store.dtb` at 0x82001060) and the recompiled
sources:

| Step                              | Where                                                                                 | What                                                                                                                                                                 |
| --------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| method dispatch                   | `sub_827501F0`                                                                      | `BandStorePanel::Handle(Symbol, DataArray*)`; the `request` branch reads the menu symbol from argument 0 and the bool from argument 1                            |
| request builder                   | `sub_8274FB60`                                                                      | formats the metadata path (`"/%s%s"` at 0x82103138, `"?pid=%s"` at 0x82103130), stores it at `this+148`, calls the file cache `sub_8235A240(manager, &path)` |
| index request                     | `sub_82753010` (reached from `sub_82753B20`'s `request_index_if_needed` branch) | returns early when `*(this+52) != *(this+56)`; then returns unless `*(0x828A3D20+44) == 2`; then calls `sub_8274FB60`                                          |
| the callback that should start it | DTA `on_init_commerce_success` in `ui/store/gen/store.dtb`                        | `{this request_index_if_needed}` then a `has_menus` check that sets the loading visual state                                                                     |

Measured in boots (`out/_swap/store-probe/20261020-15{1718,2008,2327,2622,2933}`):

1. **The retail panel never requests.** With the store open and its first row accepted, no
   `store-probe: request` line appears: `sub_8274FB60` is never reached. The screen sits on its
   loading state because the DTA's select branch is gated by `{store_panel has_menus}`, and the
   menus only ever arrive from the callback above.
2. **The callback only fires on a commerce session, and offline there is none.** Entering the store
   calls `__imp__XamBackgroundDownloadSetMode` (a STUB) and nothing else.
3. **The store's DTA is ours to edit, and it runs.** The payload now appends
   `{store_panel on_init_commerce_success}` to `store_menu_screen`'s `enter` method
   (`store::AddStoreEntryBypass`). Proven by a one-boot swap of that command for
   `{ui goto_screen splash_screen}`: entering the store jumped to splash and B returned to the
   title — so the generated `ui/store/gen/store.dtb` really is the record the guest executes. The
   bypass itself then reaches `request_index_if_needed` (the gate hook logs it), and the index
   path still declines: `this+52 == this+56 == 0` (so its "already loaded" test passes), the state
   word reads **0**, and forcing it to 2 through the read-only page protection (the same
   unprotect-write `ultimate.cpp` uses) does not produce a request either.
4. `ui/gen/ui.dtb`'s manager init is now also rewritable: the `{find syscfg store netcache_init}`
   argument is replaced with the record's own rewritten definition (`store::PointUiNetcacheAtLocalServer`).
   Delivery of that record is **unproven** (a `{notify …}` marker produced no log line; `notify` may
   be a ship-build no-op), unlike the store DTA above.

**Next measurement (the top of P3's data lane):** why `sub_82753010` still does not call the request
builder after the state word is forced. Either the compared word is refreshed from another cell
before the compare, or the `sub_8274ED98`/`sub_8274E710` pair it calls first declines. The cheapest
next step is a breakpoint-style dump of the three values *inside* the original body (a hook on
`sub_8274ED98` reporting its return), not another schema guess.

**P3 gate successor (2026-10-20, same day; supersedes the "next measurement" above).** The answer
was the address, not a missing hook: `lis r11,-32123` is `0x8285` (65536 - 32123 = 33413), so the
state word is `0x82853D4C`; the first run forced `0x828A3D4C`, an unrelated cell. With the
corrected address forced to 2, boot `rb_blitz_055.log` (17:10:11-17:11:04; the entry was driven by
the P4 search diagnostic's `enter` statements, but the request is the retail
`request_index_if_needed` path) reaches the builder and the cache:

```text
store-probe: index request reached; this+52=0x0 this+56=0x0 state=0 (forced to 2: yes)
store-probe: cache request 'dlc_store/europe/dlc_top_xbox_eng.dta' (manager 0x400019a0)
store-probe: request 'dlc_store/europe/dlc_top_xbox_eng.dta' (input '/dlc_top_xbox_eng.dta', flag 1)
```

The measured substitutions are **region `europe`, platform `xbox`, locale `eng`** (not the earlier
`xbox`/`en` guess). The file cache `sub_8235A240` is reached and opens no store path in that boot,
so **record delivery is P3's remaining step**. Two practical notes: `scripts/probe_store.ps1` must
have its `-StoreRow` re-read with `-MenuOnly` after a main-menu change (the row moved from 4 to 3
when Play Current Song was hidden), and a probe report can name another boot's log when two sessions
run at once — match the log by its launch time, not by "newest".

`src/store/store_metadata.*` now carries the measured constants and writes the shapes read out of
the consumers: the front menu is `(submenus ("/<locale>/<id>/" ...))` (`sub_8275D4E0` +
`sub_8275CBD8`), a menu is `(offers_populated TRUE sorted TRUE offers (...))` (`sub_8274F610`), each
offer keys the plan §2.4 fields, and an empty page is valid. `tests/store_metadata_tests.cpp` is
**50 checks**; `store_payload` stays **84**. The keys are not yet proven against the retail parser,
which needs the delivery step.

**P3 delivery record (2026-10-20, evening) — the record reaches the cache and parses; the panel's
notification is the remaining step.** Boots `out/_swap/store-probe/20261020-2037…-2120`, read out of
the decrypted image (`out/_swap/blitz_image.bin`, RTTI resolved with the new
`out/_swap/rtti_vtables.py`) and the recompiled sources:

- **The fetch entry is two functions, not one.** `sub_8235A240(cache, &path, client)` asks
  `sub_82468518(manager, path)` whether the path is *loaded*: that is a container lookup
  (`sub_82474E68` -> `sub_82474E08`, an exact `strcmp` against each entry's path String at `+44`)
  guarded by three words of the manager object, `+44 == 1`, `+48 == 0`, `+104 == 1`. Measured
  offline (`store_fetch: … guard(44=0,48=0,104=1)`): `+44` is **0**, so the "already loaded" branch
  never runs and the request always falls through to the submit path.
- **`sub_824685E0(manager, path, data, size)` is the retail insert** (its other caller is the
  download completion, which hands it the downloaded buffer and its length). It classifies a record
  by the path's extension (`strncmp` against `dxt`/`png_xbox`/`dtz`/`dta`) and builds a 72-byte file
  object (`sub_82468248`: vtable, path String at `+44`, type `+52`, data `+56`, size `+60`, the
  **parsed DataArray at `+68`**). `.dta` is type 3, parsed eagerly by the text DTA reader
  (`BufStream` `sub_824628A8` + `sub_82418940`) - so a served store record is DTA **text**.
- **Measured working, without a fault**: the 46-byte front-menu record
  `(submenus "/eng/recent/" "/eng/search/")` is inserted and parsed
  (`store_fetch: served 'dlc_store/europe/dlc_top_xbox_eng.dta' … (46 byte(s), insert -> 0x…)`), and
  a control boot with delivery off is stable: the record is served, the request returns to the
  panel, and the store still waits on its loading state.
- **The notification is what still blocks the data lane.** Forcing the manager's `+44` to 1 for the
  request makes retail take its loaded branch, which calls `sub_8235A0F8` -> the client's vtable
  slot 5. The client is the panel's secondary subobject (vtable `0x82102AC4`, slot 5 a thunk into
  the message dispatcher `sub_827501F0`), and the dispatcher faults (`read of guest 0x656E742F`, a
  piece of the record text): the object the cache passes is not the message shape the dispatcher
  interns its symbol from. A retail message's first word points at a node array whose `+8` is a
  symbol; the cache's result object carries a vtable in its second word (`0x8200BA88`).
- **The direct route was measured too** (`store_fetch_deliver`): the panel's top-menu handler is
  `sub_827538B8(out, panel, message)` -> `sub_827537C0(panel, DataArray, source)` -> `sub_8275D5B8`
  (72-byte menu object) -> `sub_8275D4E0` (`submenus` parse). Called directly with the parsed array
  and the dispatcher's own `this` (`client - *(client - 4)`), the constructor **and** its parse both
  complete (`menu ctor enter/exit`, `menu parse enter/exit`), and the call then faults in the
  menu-vector insert `sub_821F73B8(panel + 52, &menu)`: with the −204 base the vector is empty and
  its growth path faults on `read of guest 0x00000004`, while with the unadjusted base `+52` is not
  that vector and the insert writes the read-only image address `0x820009D4`. The panel's menu list
  is therefore not reachable from that `this` alone; the missing piece is state the retail
  notification sets up first.
- **Landed and tested**: `src/store/store_serve.{h,cpp}` (which path is ours, the host-directory
  override with a no-escape rule, the built-in record), the text writers in
  `src/store/store_metadata.{h,cpp}` (`BuildFrontMenuText`/`BuildMenuText`), the guest half in
  `src/store/store_fetch.{h,cpp}` (insert only by default), `scripts/probe_store.ps1 -Extra`, the
  cvars `store_serve_dir` and `store_fetch_deliver` (both documented in
  `docs/engine/diagnostics.md`), and ctest **`store_serve` = 50 checks** (full suite **43/43**; the
  count is 61 after the landing record below).
- **Next measurement, named so it is not rediscovered**: find the code that turns a loaded file into
  the message `sub_8235A0F8` delivers - the download completion in `rb_blitz_recomp.23.cpp` builds
  the same shape - and reproduce it verbatim instead of calling the client callback with the file
  object; or locate the state the retail notification establishes before it calls the builder.

**P3 landing record (2026-10-21) — the front menu is delivered: the retail store draws RECENT and
SEARCH, and pressing one asks for the request name P2 predicted.** Boots
`out/_swap/store-probe/20261020-2139…2202` (`rb_blitz_079…088.log`), same instruments as above.

- **A `.dta` record is TEXT.** The insert's type-3 branch (`sub_82468138`) parses it with the
  engine's own text reader: `sub_82418878` builds the root array and `sub_82417BF8` is its
  tokenizer (`sub_82423FD8` is `GetToken`; the tokens it special-cases are 20/22/23/24 =
  `#define`/`#merge`/`#ifdef`/`#autorun`). Measured: the same record served as compiled DTB parsed
  to a single stray symbol node (`{0x8200BA88, type 5}`), the text form to the `submenus` map. The
  earlier "compile it" reading is superseded, and so is the writer choice that came from it.
- **The delivery that works is the retail notification, not a builder call.** After the request has
  registered its client, the hook calls `sub_8235A0F8(cache, &request_path, 1)` - the exact call
  `sub_8235A240`'s "already loaded" branch makes - with the manager's readiness word
  (`manager + 44`) forced to 1 for the call and restored after it (`WriteGuestU32Protected`, now
  public in `src/store/store_probe.h`). Every layer below that call is the engine's own, so the
  message layout (`file_req_complete`, vt `0x82102DC4`) never has to be reproduced by hand.
- **The message, measured** (`rb_blitz_085.log`): `{vt, array}` with
  `[int, {type 5}, {type 16 = the record's DataArray}, int, {type 18 = the source path}, int]`; the
  panel's helper `sub_827538B8` reads nodes 2..5 and refuses a record whose
  `sub_823FAE00(record, Sym("submenus"))` finds nothing.
- **Front-menu record shape (measured, after two superseded attempts).** Each row is a
  **three-element array** whose first element is the provider path -
  `(submenus ("/eng/recent/" "Recent" TRUE) ...)` - because `sub_8275C968` reads a row as an array
  and asks for `count > 2` with the third element non-null, and `sub_8275CBD8` compares the row's
  first string against a provider name (its static default is `pro`). The record also carries
  `(new_releases "<path>")`: `sub_8275D4E0` stores that lookup at `menu + 68`, and without it the
  builder's last step faults `read of 0x44`. Bare quoted paths as rows, and a compiled DTB, both
  fail - those are the two superseded attempts in the record above.
- **Measured result**: the store's own screen OCRs as `MUSIC STORE … RECENT SEARCH …`
  (`out/_swap/store-probe/20261020-2201xx`, boot `rb_blitz_088.log`; the panel's menu vector reads
  `{0x4163B538, 0x4163B53C, 0x4163B53C}`), and pressing the row requests
  **`dlc_store/europe/eng/recent/`** - the `MenuRecordPath(region, locale, name)` template P2's
  writer predicted, now measured.
- **Panel pointers, for whoever picks this up**: `sub_8274FB60`'s `this` is the primary panel
  (vt `0x82102B24`); the client the cache registers is primary **+ 204** (vt `0x82102AC4`); the
  menu vector the builder pushes into belongs to the object the retail handler passes as
  `arg0 - 72`. That is why `Panel()`/`Panel() + 52` disagree with the builder's receiver, and why
  the retail route - not a direct builder call - is the correct seam.
- **The offers page** answers `dlc_store/<region>/<locale>/<name>/` with `BuildMenuText({})`: an
  **empty** page, the shape P3's exit already requires to work, because the catalogue that fills it
  is not wired yet. `IsMenuRecordPath` is the rule that separates a menu request from an offer or
  related-record one.
- Landed: `store::IsMenuRecordPath`, the offers page, the corrected text writer, the retail
  notification behind `store_fetch_deliver` (still **off** by default), the two new hooks and the
  public `WriteGuestU32Protected`, and ctest `store_serve` with the menu-path and offers-page cases
  (**61 checks**; full suite **43/43** in `out/build/win-amd64-debug`).
- **P3 exit still open**: Recent shows no offers (the catalogue is not wired), so title/artist/art,
  the details screen, and the Downloaded matcher (§3.2) wait for the list to be filled. The next
  step is one function - `BuiltinRecord`'s menu branch - fed by the P5 pipeline's snapshot, plus the
  field mapping §5/P3 already names.
- **Control leg**: with `store_fetch_deliver` off the record is still inserted and the request still
  runs, and the store shows its own list with LOADING (`rb_blitz_089.log`) - no fault, no change to
  the shipped behaviour. The insert's 72-byte file object reads
  `{vt, …, path String@44, type 3@52, size 108@60, parsed array@68}`, which is the layout the
  delivery depends on, now measured rather than inferred.

Landed with this record: `store::AddStoreEntryBypass` (+ tests), the `sub_8274FB60` request hook and
the `sub_82753010` gate hook in `src/store/store_probe.cpp` (pass-through; they log and force only
while `store_trace` is on). `store_payload` is **84 checks**; full `ctest` **40/40**.

**P3 catalogue record (2026-10-21) - the page is filled from the live RhythmVerse catalogue, and the
retail offers parser reads it; the browser built from those offers still faults.** Boots
`rb_blitz_097...110.log` (release tree, `out/build/win-amd64-release/logs/`), driven by
`scripts/probe_store.ps1 -StoreRow 3 -PageRow 0` with `--store_fetch_deliver=1 --store_trace=true`.

- **The delivery seam, corrected.** A menu page does **not** need a hand-built parser call: the file
  cache's own `file_req_complete` reaches the panel's dispatcher `sub_827501F0`, whose branch at
  `loc_8275088C` calls `sub_8274F610(out, client - 204, {vt = 0x82102DC4, array})` with **the
  message's own array**, and the parser reads the record out of that array's node 2. Measured: the
  front menu takes that branch at boot (its record has no `offers_populated`, so the parse is a
  no-op), and with the page served through the same notification the parser reads
  `offers_populated`/`sorted`/`offers` (`rb_blitz_110.log`).
- **Two calls that look right and are not.** (1) Passing the record itself as the message payload
  makes the parser read node 2 *of the record* - boots 097/098 showed it parsing the front menu's
  keys. (2) Calling `sub_8235A0F8` for a page whose request the cache never registered makes
  `sub_82468560` answer 0 and the notify reads through the null (boot 099). The fix that sticks is
  smaller than either: `ServeStoreRequest` now leaves the manager's readiness word (`manager + 44`)
  at 1 after the insert, so retail's own request body takes its "already loaded -> notify" branch
  and the message, its array and every node below them are the engine's (boots 101 and later).
- **The catalogue feeds the writer.** `src/store/store_catalogue.{h,cpp}` fetches page 1 of
  `/api/rb3xbox/songfiles/list` on its own thread (TTL 300 s, 30 s failure backoff, single flight,
  drops an answer whose settings generation changed), shapes it with the same `DecideOffer` pair
  P4's search uses, and hands `Offer` records to `SetOfferSource`; `BuildMenuText` writes them. First
  measured page: `page 1 -> 24 eligible offer(s) of 25 record(s)` (boot 090).
- **Zero offers is a real, working page** (boot 108, `store_catalogue_off`): the parser returns, the
  DTA's `process_offers` takes its `num_offers < 1` arm and refreshes the menu, and the store comes
  back to `store_menu_screen` with no fault. That is the control leg for every offers question.
- **The retail scripts are readable offline now.** `ui::dtb::Parse` (already in the tree for P1)
  dumps a shipped `.dtb` back to DTA text; `ui/store/gen/store.dtb` holds the whole store UI and
  answers questions that used to cost boots: `on_offers_populated` -> `process_offers` ->
  `{if_else {>= {store_panel num_offers} 1} {ui goto_screen store_browser_screen}
  {this refresh_current_menu}}`; the browser's `refresh_list` groups through
  `{find syscfg store groupings sort_name}`, whose entries read `release_date_str`, `artist`,
  `first_char`, `year_released`, `pack_first_letter`, `author`, `label`, `subgenre` and
  `rank band` through `get_data`; and **Start** in the store menu is retail's own
  `goto_screen options_screen`, not a harness artifact (boots 088-108 read that as one).
- **The offer schema, still incomplete.** Every `get_data` key those groupings read is now emitted
  (empty strings included, because those reads are not guarded by `has_data`), each offer carries
  `type` as well as `offer_type` (the shipped `dummy_upsell_offer` writes `("type" "song")`), and
  `sorted` is the display order (`(sorted (0 1 2 ...))`) instead of a boolean. With all of that the
  guest still faults right after the parse returns (boot 110), so what remains is something the
  native offer accessors read and the dump does not spell out - the shortest candidate list is the
  rank tiering (`rank_tier` + `rank_tier_token` on `rank band`) and the browser provider's own
  `build_list` contract. **Unproven:** the `type` key and the extra `get_data` keys landed after that
  boot and are covered by `store_serve`'s writer cases only - the next boot has to confirm or refute
  them. (The owner's profile is restored, so a store boot needs `[general] rhythmverse = true` in
  `%APPDATA%\rb_blitz\settings.toml` for the duration again; boot 111 pressed the hidden store row's
  old position and hit EXIT GAME instead.)
- Landed and tested: `store_catalogue`, the corrected writers, `RememberMessageArray` (the message
  array a page's parser call can reuse), the offers-page delivery, the parser's entry/exit/panel
  diagnostics, `scripts/probe_store.ps1 -PageRow/-PageKey`, the catalogue cvars
  (`store_catalogue_off`, `store_catalogue_ttl_seconds`) and the updated `store_serve` cases
  (**61 checks**; full `ctest` **43/43** in `out/build/win-amd64-debug`).

**P3 browser record (2026-10-21, later) - the retail browser opens on catalogue offers and its
details panel reads them; the list rows still draw no text.** Boots `rb_blitz_112...120.log` plus
`out/_swap/store-probe/20261021-105946` and `-111707` (release tree, `probe_store.ps1 -StoreRow 3
-PageRow 0 -LocalRoot "" --store_fetch_deliver=1 --store_trace=true`).

- **What unblocked the browser was the rank block, not the root shape.** Every offer must carry all
  **five** rank parts - `(rank ((guitar 3)(drum 1)(bass 0)(vocals 0)(keys 0)))`. The details panel
  loops `{foreach part (guitar drum vocals bass keys)}` with no `has_data` guard, so a part the
  catalogue does not rank handed `{song_mgr rank_tier <null> <part>}` a null; boots 103-110 faulted
  on `read of guest 0x00000000` there. With all five written, `process_offers` takes its
  `num_offers >= 1` arm and `store_browser_screen` is entered (`help_observer: screen
  'store_browser_screen'`, boots 112/117/120), **no fault**.
- **Measured on screen** (`20261021-111707/page-late.png`): the browser's details panel shows the
  offer's own data - `FOUR YEAR STRONG` (our `artist`) and `PUNK` (our `genre`) - over the retail
  placeholder art (`cover_art_none.tex`), because `preview_art` is still empty (the art gap below).
  So the record reaches the retail panels with real values.
- **The list rows are blank** (crop of the same frame: the selected band has no text, and the rows
  below it are empty). Two hypotheses were measured and are both dead ends: writing `name` as the
  shipped `dummy_upsell_offer`'s localize command (`{"localize" "..."}`) faults the parser on a null
  (boot 119), and making `sorted` empty (the browser's `refresh_list` treats an empty
  `{store_panel sort_name}` as "no grouping", `build_list_no_grouping`) changes nothing (boot 120).
  The list *does* hold the offers - `store_details_panel refresh` resolves `find_offer
  {song.lst selected_sym}` to our offer - so what is missing is only the row's own label.
- **Delivery happens exactly once, from the cache's own body.** `ServeStoreRequest` leaves
  `manager + 44 = 1`, so retail's `sub_8235A240` takes its "already loaded -> notify" branch and
  sends the `file_req_complete` message itself; `DeliverStoreRecord` no longer calls
  `sub_8235A0F8` (the cvar now gates only the readiness write). Calling it as well delivered every
  record twice, which re-entered the panel's screen transition.
- **Hooking `sub_823FAE00` (the map lookup) is not a safe instrument**: resolving the key needs a
  guest call (`sub_823F9538`) from inside the DTA engine's own evaluation, which is re-entrant and
  left boots faulting on a string read as a pointer (`read of guest 0x75706461`, the four characters
  of a command name). Removed; the note sits in `store_probe.cpp` so it is not retried.
- **Still open for P3's exit**: the row labels; the retail price chrome (`ALL PRICES ARE IN
  MICROSOFT(R) POINTS` in the browser's footer, §2.12's hide path); A opening the details screen
  (the details *panel* is measured, the `store_info_screen` transition is not); the Downloaded
  matcher (§3.2); and the art gap below. The next measurement for the labels is what the row widget
  asks the offer for - the browser's provider is native, and neither `name`'s node type nor the sort
  name changed the outcome, so the label is a third field (a `song_id`/`sym`-style row key is the
  cheapest candidate to write next).


**P3 landing record (2026-10-21, evening) - the row labels' cause is found, the price chrome and the
heading are edited into the payload, and the Downloaded matcher is built.** Read out of the
decrypted image (`out/_swap/blitz_image.bin`), the extracted records
(`assets/game/360/ui/store/gen/{store,store_info}.dtb`, dumped with
`rb_blitz_settings_screen_tests.exe dump-record`) and the list resources
(`ui/resource/lists/gen/list_store_*.milo_xbox`, dumped with `scripts/hmx_milo.py`).

- **Why every browser row drew blank: `type` was a string.** The row text is the provider's
  `Text(data, label, out)` (`StoreOfferProvider` vtable slot 2 + the `UIListProvider` secondary
  vtable; `sub_8275A230`): it compares `get_data("type")` - read by `sub_822D1CE0`, which interns
  the node's value as a Symbol - against the interned symbols `song` (`0x82044670`) and `pack`
  (`0x82041CE8`). Our record wrote `(type "song")`, a **string node**, which never equals that
  symbol, so the provider fell through to its empty answer. `offer_type` (a symbol) is a different
  key, which is why the details panel worked while the list did not. Both writers now emit
  `(type song)`; the row's own key is the map's first element (read by provider slot 8,
  `sub_8275A820`), which is our `name`.
- **The list rows' label names, measured statically**: `list_store_songpage`'s `UIListLabel` is
  named `list` and its mesh `slot`; `list_store_packsongs`' is `song`. The provider answers `album`,
  `song`, `rbn_icon`, `cost`, `new` and `purchased`, and the `cost` branch asks
  `is_purchased`/`is_available` (`sub_82453408`/`sub_82453170`) - so a free custom never grows a
  price column.
- **The price chrome is retail text, so it is edited in the payload (§2.12).** Two surfaces:
  (a) the footer `microsoft.lbl`, which `BandStorePanel.enter` shows on the Xbox branch of its
  `#ifdef HX_PS3` block - `HideStorePriceChrome` appends `{microsoft.lbl set_showing FALSE}` after
  the block (a scan that ignores the `#endif` would find the PS3 branch's own hide and never add
  ours); (b) `cost.lbl`/`cost_ps3.lbl`, which `store_info_panel.refresh_panel` re-shows under
  `purchase_allowed` - `HideStoreInfoPriceChrome` removes every show inside the method and hides
  both once more at the end. The purchase gate itself needed no edit: every offer now writes
  `(is_available FALSE)`, so `purchase_allowed` is false and neither a price nor a Buy Now row is
  built (the details screen still opens - `is_completely_unavailable` is a different key).
- **The store's heading names the source.** Retail draws it from the locale entry `store`
  ("MUSIC STORE"); `SetStoreHeading` replaces that entry's value in every installed locale with
  `CUSTOMS FROM RHYTHMVERSE` while the master switch is on (`kStoreHeadingText`).
- **The store's DTA edits are now gated on the master switch, not on P2's spike.** With RhythmVerse
  on, the payload rewrites `ui/store/gen/store.dtb` (commerce bypass + price footer) and
  `ui/store/gen/store_info.dtb` (cost labels); the P2 local-server rewrite keeps its own cvar.
  A refusal is a note, never a failed payload.
- **The Downloaded matcher (§3.2) is built and wired to the record.**
  `src/store/store_library.{h,cpp}` + `store_library_index.cpp`: the normalization (trim, collapse,
  ASCII case fold) and the `(artist, title)` set are SDK-free and tested; the builder thread mounts
  every package `dlc::LibraryPackagePaths()` names and reads its `songs/songs.dta` through the
  reader the R9 menu pool already used (now shared as `src/fs/dlc_song_metadata.{h,cpp}`), which
  gained the record's `title` and `artist` fields. The serving policy asks the installed predicate
  once per offer (`store_serve.h`'s `SetDownloadedPredicate`) and writes `(is_purchased TRUE)` for
  a song the loaded library declares - the key `refresh_buttons` reads as Downloaded. The index is
  built when the store opens (off the guest thread), dropped by `InvalidateLibrary()` after a DLC
  refresh or a store install, and absent until it lands, which is "unknown", never a negative
  claim. A receipt never sets the mark.
- **Measured on screen 2026-10-21** (`out/_swap/p3-boot/`, release tree, `store_fetch_deliver=1`,
  `--dlc_root` at an empty folder, store row 4): the store's heading reads **RHYTHMVERSE** (the
  locale entry `store` replaced) and the browser's footer **no longer shows "ALL PRICES ARE IN
  MICROSOFT(R) POINTS"**; the details panel still reads `MADBALL / PUNK` from the offer. Both are
  the payload edits below, on screen.
- **The row labels: the provider answers our titles, and the list then faults.** With the symbol
  form on, `store-probe: offer name exit '<title>'` prints every offer's title (three passes of the
  24-offer page), so `StoreOfferProvider::Text` is reading `get_data("name")` - and then the guest
  dies on `read of guest 0x00000000` while the list draws (`rb_blitz_124/126/128.log`). The symbol
  form therefore ships **off** (`store_offer_type_symbol`, diagnostics.md) and the rows stay blank,
  which is the pre-P3 behaviour: a faulting store is worse than a blank row.
- **A still lands on `store_info_unavailable_screen`.** With the retail guard neutralised
  (`AlwaysShowOfferDetails`, pinned by the suite) the popup remains, so it is the **native** offer
  panel reporting `enum_error` for a data-only offer, not the DTA's `is_completely_unavailable`
  branch. That is the next P3 measurement: what `store_info_enum_panel.set_offer` (or the panel's
  offer fetch) refuses about an offer that has no commerce state.
- **Tests**: `store_library` (new, **26 checks**), `store_serve` **79** (the `type` switch in both
  forms, the preview keys absent when empty and present when set, the Downloaded mark from the
  installed predicate), `store_payload` **130** (the footer hide after the `#endif`, the details
  cost strip and its round trip, the heading, and the details guard), `store_metadata` **50**.
- **Still open for P3's exit**: (a) the row labels (above); (b) A -> `store_info_screen` (above);
  (c) album art: `preview_art` stays empty because the retail art loader wants a platform texture
  (`png_xbox`/`dxt`), and producing one needs a runtime PNG decode + DXT encode + Xbox swizzle
  (`scripts/hmx_tex.py import` documents the format; no C++ encoder exists yet), so the browser
  keeps retail's placeholder art; (d) the Downloaded index has not been measured on a real library
  (its cost is one mount + one `songs.dta` read per package, on its own thread).

**P3 landing record (2026-10-21, late) - the label fault has a name: the offer's `(rank <part>)`
lookup.** The fault logger below turned `read of guest 0x00000000` into a stack, and the stack names
the store's own offer handler. Nothing about the shipping state changed: the symbol form of `type`
stays **off**, because the fault is still open.

- **The instrument.** `store_trace` now installs a first-chance vectored exception handler
  (`src/store/store_probe.cpp`, `LogFault`) that logs the faulting rip, the guest/host address and 24
  host frames, plus the module base. `out/_swap/fault_lookup.py` rebases those frames onto
  `rb_blitz.map` and prints the nearest recompiled symbol. This is the missing piece for every
  "the guest reads address 0" report: the SDK's own line names the address and nothing else.
- **The chain, innermost first** (`rb_blitz_153.log`, `rb_blitz_161.log`): `sub_824534A8` (a data
  path walk) <- `sub_824538A8` <- `sub_82453928` (**the offer's own message handler**: its literals
  are `get_data`, `has_data`, `offer_type`, `offer_name`, `year_released`, `genre`, `cost_str`,
  `in_library`, `is_available`, `is_purchased`, `has_available_pack`, `has_available_album`,
  `is_completely_unavailable`, `first_char`, `pack_first_letter`, `release_date_str`, `short_name`)
  <- `sub_8275C6C0` (the store offer's `has_available_demo`/`demo_purchased`/`upgrade` branch) <- the
  DTA evaluation in `sub_823FF900`. So the null read happens **inside the offer's `get_data`**, not
  in the list or the label.
- **The lookup that answers nothing** (temporary path hook, `rb_blitz_156.log`): the last walk before
  the fault is `(rank <part>)` - node 0 is the symbol `rank`, node 1 is a **string**, which is what
  `store_info`'s `refresh_panel` passes (`{foreach $part (guitar drum vocals bass keys)}` -> `rank
  $part`). `sub_824534A8` resolves each component through `owner + 192` and then reads word 0 of the
  answer; a component that resolves to nothing leaves a zero there and **that** read is the fault.
  The open question is therefore narrow and written down: why does the offer record's own `rank`
  sub-array not answer the panel's *string* key, when the shipped grouping asks for the *symbol*
  form instead (`p0/defs/store.txt`: `{song_mgr rank_tier {$offer get_data (rank band)} band}`)?
  Both spellings have to answer for a retail offer.
- **The two hooks that measured it are removed again.** A shared data-walk helper must not stay
  hooked: one boot with them in place faulted in unrelated panel lookups on
  `read of guest 0x70616E65` (the bytes `pane`, a string read as a pointer). The fault logger stays -
  it only observes.
- **Control**: with `store_offer_type_symbol=0` the same route runs the same
  `{genre.lbl set text_token ...}` / `{album.mat set diffuse_tex cover_art_none.tex}` sequence and
  completes (`rb_blitz_145.log`), so the fault needs the provider to answer a row's text - the label
  path and the offer's `get_data` share the offer object.

**P3 exit record (2026-10-21, evening) - the labels draw and A opens the details screen; P3's
blocking items are exited.** The old `rb_blitz.map` was from 2026-10-12, so every earlier stack was resolved against a
stale map and the offsets (not the names) were noise. Re-linking with `LINKER:/MAP:` (a temporary
cache option, removed afterwards) and re-running the same boot put the fault at
**`sub_824534A8 +0x31d`** - the offer's data-path walk, reached from `sub_82453928` (the offer's
message handler) via `sub_8275C6C0` and the DTA evaluator (`rb_blitz_165.log`).

- **Why no five-part rank map can work.** `sub_824534A8` interns each path component and calls
  `sub_823FAE00`, which walks the current map's elements and compares each element's **first child's
  value** with that component (`lwz r10,0(element); lwz r8,0(r10); lwz r8,0(r8); cmpw r8,key`).
  The first component (`rank`) matches the record's `(rank <value>)` pair and the walk continues
  **inside that pair**; a second component can then only match a value array whose first element is
  that part - so `(rank guitar)` would work for one part and every other part returns nothing. The
  walk's tail (`loc_82453540`) then dereferences the missing answer when the caller passed no
  default: exactly the `read of guest 0x00000000`. This also explains why the details page drew
  `artist`/`genre` (single-component reads, the same path as `get_data("name")`) and died at the
  rank loop, and why an **absent** key is just as fatal: `preview_audio` had to become a key.
- **What landed** (`src/store/store_metadata.*`, `src/store/store_payload.*`,
  `src/settings/menu_payload.cpp`, `src/store/store_fetch.cpp`):
  - the offer record carries single-component `(rank_guitar N)` ... `(rank_keys N)` keys next to the
    retail-shaped `(rank ((guitar N) ...))` map;
  - `store::UnrollOfferRanks` rewrites `store.dtb`'s `store_details_panel.refresh`: the
    `{foreach part (guitar drum vocals bass keys) ... (rank part) ...}` block becomes five explicit
    blocks reading `(rank_<part>)`, spelled exactly as the shipped record spells its statements
    (`kVar` for `$offer`/`$rank`/`$tier`/`$idd`/`$this`, symbols for keys and tokens). A scan for
    the loop first; a refusal leaves the record byte-identical;
  - `store::ShowOfferDetailsDirectly` replaces `store_info_enum_panel.enum_and_show_offer` with the
    body retail itself runs on success (`clear_data`, `set cur_offer offer`, `store_details_panel
    refresh`, `goto store_info_screen`), so the A press no longer goes through the native
    enumeration that answered `enum_error`. `store::AlwaysShowOfferDetails` now rewrites **every**
    `is_completely_unavailable` guard - the first cut edited only the first match in document
    order, which is `store_offer_fetch_panel`'s, not the browser's;
  - the record writes `preview_audio` as an empty string (its reader in `refresh_buttons` is
    unconditional) and no longer writes `rating` (both details panels read it behind `has_data`
    and draw `rating_0` as a literal token - boot `out/_swap/store-probe/20261021-185501` read
    `RATING_0`). `store::HidePreviewButton` removes the now-dead `store_preview_audio` row insert;
  - `store_offer_type_symbol` and `store_fetch_deliver` now default **on**: the symbol is what the
    provider matches, and without the delivery the store has nothing to draw. Serving itself is
    gated on the master switch, so a stock boot is untouched.
- **Measured on screen with the shipped defaults only** (`--store_trace=1 --dlc_root=<empty>`, no
  diagnostic switches; `out/_swap/store-probe/20261021-190304`): the main menu carries RHYTHMVERSE
  CUSTOMS, the store heading reads RHYTHMVERSE, Recent draws **all 24 titles**
  (`BUILDING A MYSTERY`, `BEEN THERE. DONE THAT`, ...), the details band reads
  `SARAH MCLACHLAN / POP-ROCK`, and A opens the details screen
  (`RHYTHMVERSE BUILDING A MYSTERY SARAH MCLACHLAN POP-ROCK ... SELECT EXIT BACK`) with **no**
  rating token, **no** preview row, **no** price/Buy Now chrome and **zero** fault lines in the
  boot log. B returns to the browser, then to the store menu.
- **Tests**: `store_serve` **85** checks (the `rank_<part>` keys, `preview_audio` present-empty,
  no `rating`, both `type` spellings), `store_payload` **204** (every-guard rewrite, the direct
  details route, the preview-row removal, the loop unroll with its idempotency and both refusals),
  `store_metadata` 50, `store_library` 26; full `ctest` **44/44** in `out/build/win-amd64-debug`.
  `rb_blitz_store_payload_tests store-edit <dtb> store|info` is the diagnostic half: it runs the
  transforms against a shipped record and prints the refusal reason and the method's node tree,
  which is how the `kVar` spelling was found.
- **P3's blocking exit items**: Recent shows the newest eligible `rb3xbox` offers with **title,
  artist and genre**, empty pages work, A opens the details screen, RhythmVerse is visible, and no
  price/currency/Buy Now UI appears. **The phase's own `Exit:` line is not fully met, so P3 stays
  open on two lines**: the `art` it asks for (album art needs a runtime PNG→texture encoder - the
  `preview_art` key and the `store_panel load_art` seam are ready, the texture is not), and
  "manually installed DLC and multi-song packs match without a store receipt" (the matcher is built
  and wired, but no boot has read it against a real library). Also not done: the difficulty display
  reads `NO PART` where the catalogue publishes no rank, and page 2+ is not requested (one page of
  24 offers is shown). **Superseded 2026-10-22 by the P3 close-out record below**: the matcher reads
  a real package, the list carries four API pages, and the meter draws the catalogue's difficulty.

**P3 art record (2026-10-21, late evening) - album art draws, and the phase's last content exit is
met.** A cover now goes catalogue → worker → platform texture → the retail `load_art` seam → the
panel's material, on both the browser and the details screen (`out/_swap/store-probe/20261021-204913`:
Sarah McLachlan's *Surfacing* cover over `BUILDING A MYSTERY / SARAH MCLACHLAN / POP-ROCK`).

- **The chain, measured out of the image.** `store_details_panel.refresh` sets
  `album.mat` to `cover_art_none.tex`, calls `{store_panel load_art <path> this}` and expects the
  completion to send `art_loaded`, whose handler assigns `{store_panel album_tex}`. The request
  entry is `sub_82469C28(manager, const char* path, client, 1)` - it performs the loaded-file lookup
  itself (`sub_82474E08(manager + 76)`), so unlike the metadata request there is **no readiness word
  and no delivery step**: an insert is all a served texture needs. The completion `sub_82445CD8`
  decodes the bytes (`sub_821FE800` → `sub_821FD400`) and copies the bitmap into the panel's texture
  through `sub_8220CB98(*(panel + 100), &bitmap, 0, 0)`.
- **Two guest facts worth keeping.** (a) The request's path argument is a **C string**, not a
  `String`: the details panel composes `global prefix (0x8285487C) + preview_art`, and all four call
  sites pass the chars word. Reading it as a `String` dereferences its own characters and faults
  (`read of guest 0x73746F72`, "stor" - boot `rb_blitz_172.log`). (b) The panel's prefix is
  `dlc_store` **with no trailing slash**, so `preview_art` must begin with `/` or the request reads
  `dlc_storeoffers/...` (`rb_blitz_173.log`).
- **`art_loaded` never fires for us, so the material is linked in the record.** The completion
  messages the DTA object only when its wrapper's state word is 1; measured with a served texture it
  reads **2** (`store-probe: art completion enter ... callback[28]=2`), because our delivery is
  synchronous inside `load_art` and retail's asynchronous completion is what would have set 1. The
  texture was decoded and uploaded (256x256, `has_bitmap=1`) while the screen kept
  `cover_art_none.tex`. `store::LinkAlbumArt` therefore appends
  `{album.mat set diffuse_tex {store_panel album_tex}}` immediately **after** the `load_art` command in
  `store_details_panel.refresh` - the same assignment the callback would have made, and a sibling
  after it because a DTA command's arguments run before the next sibling. The serving policy keeps
  the append safe: `KeepOnlyHeldArt` (store_serve.cpp) drops `preview_art` for any offer whose texture
  is not encoded yet, so the DTA's `{offer has_data preview_art}` test fails and retail's placeholder
  stays instead of a link to a texture the panel never loaded.
- **The codec is ours and reusable** (`src/assets/texture_codec.{h,cpp}`, SDK-free): envelope
  parse/write, DXT1 **and** DXT5 decode and encode, the 16-bit word swap, `ResizeRgba`, and
  `BuildPlatformTexture`/`DecodePlatformTexture`. `src/store/album_art.{h,cpp}` holds the key and path
  rules plus the two caches (`PublishArtTexture`/`ArtTextureForKey`, `PublishArtUrl`/`ArtUrlForKey`);
  `src/store/album_art_service.cpp` is the Windows worker (WIC decode, box filter to 256x256, encode,
  publish - 23 of 24 covers in ~13 s, ~300 ms each, one cover refused for exceeding the 2 MiB body
  ceiling). `store_art` is a new suite of **112 checks** over the shipped envelopes, the codec round
  trip, the resizer and the path rules.
- **Covers ship as DXT1, not DXT5.** The first cut encoded DXT5 (format `0x1808`, 8 bpp, 87328
  bytes) and the panel drew blocky noise with the right shapes and wrong colours. The file was not the
  problem: dumped through the new `store_art_dump_dir`, unswapped and decoded as BC3 it matches the
  downloaded cover to a mean absolute error of **0.8** per channel, and `scripts/hmx_tex.py export`
  renders the exact artwork; a 1-mip variant and a mip-reversed variant drew just as wrong. A
  **shipped** retail texture served through the same seam draws correctly in the same test
  (`default_album_art_keep.png_xbox`, 512x512 DXT1 → the silver ROCK BAND BLITZ cover,
  `out/_swap/store-probe/20261021-203850`). So the worker encodes DXT1 (43680 bytes at 256x256 with
  mips to 16) - half the bytes for the same picture, and the format retail's own album art uses.
  The DXT5 loss is recorded as an open known issue with its evidence, not guessed at.
- **`scripts/hmx_tex.py` had a real bug, found by the same measurement**: `unswap` was
  `out[0::2], out[1::2] = data[0::2], data[1::2]`, a **copy onto itself** that swaps nothing, so
  every export showed correct shapes with wrong colours (a 16-bit word swap only moves the DXT
  endpoints; the index bits stay put). Fixed, and `export` of `default_album_art_keep.png_xbox` now
  renders the artwork as it is. Any earlier conclusion drawn from a `hmx_tex.py` export's colours -
  including a texture-swap check - is suspect; the geometry was right, the palette was not.
- **Tests and machinery**: `store_art` **112**, `store_serve` **94** (the art-advertised rule
  through `BuiltinRecord` with `PublishArtTexture`), `store_payload` **227** (the append's sibling
  position, the placeholder it leaves in place, idempotency, the two refusals, and a
  compile/parse round trip); `store_metadata` 50, `store_library` 26; full `ctest` **45/45** in
  `out/build/win-amd64-debug`.
- **P3 state after this record**: art is done on screen. Left open in the phase: the Downloaded
  matcher measured on a real library, page 2+ (the catalogue serves page 1), and the difficulty
  display's `NO PART` where the catalogue publishes no rank. **All three are closed by the
  close-out record below**, which also records the two halves the exit line still owed (a real
  multi-song pack, and the Downloaded affordance on the details page).

**P3 close-out record (2026-10-22) — the matcher reads a real package, the list carries four API
pages, and the meter draws the catalogue's difficulty.** All three remaining items were finished
against measurements, not assumptions.

- **The Downloaded matcher: a real package was downloaded and read.** The test library is one
  RhythmVerse package (`out/_swap/dlclib/45410914/00000002/775D9F4AD487454E82BE29DCF31CBCF9`,
  5,419,008 bytes, "Sarah McLachlan - Building a Mystery v2.0"), and three boots read it as
  **0 songs** while reporting no failure at all. Two causes, both fixed:
  - **The file's shape.** Dumped out of the container (the STFS directory names `songs/songs.dta`,
    1261 bytes; the text sits at offset 5,414,960), a CON's metadata is
    `('o454039871_buildingamyster' ('name' "Building a Mystery") ('artist' "Sarah McLachlan")
    ('song' ('name' "songs/o454.../o454...") ...))` — **no `(songs ...)` wrapper**, and the
    display `name`/`artist` sit on the **record**, while the `song` node names only the stream.
    `ReadSongMetadata` read `title`/`artist` from inside `song`, so the record parsed with two
    empty fields, and `LoadedSongIndex::Add` refuses an empty artist or title *silently*. It now
    falls back to the record's `title`, then the record's `name`, and to the record's `artist`;
    the song node's own fields still win. `ReadDeviceSongMetadata` also warns when a parsed entry
    is missing a field, because that silent refusal is what made three boots unreadable.
  - **The lookup.** `Entry::ResolvePath("\\songs\\songs.dta")` answered null for a device that
    plainly carries the file, so the reader now walks `Entry::children()` itself (case-insensitive)
    and names the level it could not find.
  - **Measured after the fix** (`out/_swap/store-probe/20261022-081144`):
    `store_library: the loaded DLC library declares 1 song(s) across 1 package(s) (0 could not be
    mounted) in 0 ms` and `store_fetch: the served page carries 99 offer(s), 1 of them marked
    Downloaded` — the marked offer is row 1, "Building a Mystery", the song the package carries.
    `menu_music_pool` grew a case built from the real CON text (**4504** checks).
- **Page 2+: retail has no paging, so the catalogue accumulates pages into the one list.**
  `song_select`'s PageUp/PageDown are `transition_to inbox_screen`/`song_select_screen`, not page
  turns, so there is nothing to reimplement — the browser is one list and the store's job is to
  fill it. `store_catalogue_pages` (default **4**, clamped 1..20) fetches that many API pages on
  one visit, publishing the offers after each page so the list fills progressively, and stopping
  early when the API reports no more pages. Measured: `page 1..4 -> 24,25,25,25 eligible offers
  (99 total)` and `4 page(s) -> 99 offer(s) for the Recent list`; opening row **70** reached
  **"Heart Full of Soul" / The Yardbirds**, which is API **page 4, index 16** — proof the list
  carries page 2+ and not just the first page (`out/_swap/store-probe/20261022-082450`).
- **The difficulty meter now draws the catalogue's tier.** Retail's details panel reads
  `{offer get_data (rank <part>)}` and converts the *online rank* with `song_mgr rank_tier`, whose
  thresholds this build does not have — which is why every part read `NO PART`. The catalogue
  publishes the tier itself (`diff_drums`/`diff_guitar`/... 0..6, where 0 means the song does not
  chart that part), so `UnrollOfferDifficulty` rewrites the panel's loop into five blocks that read
  `(diff_<part>)` and hand the value to `set_inst_difficulty` unchanged; `Offer` carries the five
  values and both record writers emit them.
  - **The meter's own scale was measured, not assumed.** Its asset
    (`ui/difficulty_meter/gen/diff_meter_all_inst.milo`) has five pips (`diff_1.mesh` ..
    `diff_5.mesh`) plus `icon_devil.tex`, so the guess was "tier N draws N pips". Three offers with
    known catalogue tiers say otherwise: `otro-dia-que-va` (5/4/3/3/0) drew **4/3/2/2/NO PART**,
    `sylvia` (4/3/1/1/3) drew **3/2/0/0/2**, `heart-full-of-soul` (4/3/3/3/0) drew
    **3/2/2/2/NO PART** — always `tier - 1`. A deliberate one-step shift
    (`out/_swap/store-probe/20261022-083045`) lit **all five pips at id 6**, which fixes the meter's
    scale at `0 = NO PART`, `1..6 = 0..5 pips`. `OfferFromSong` therefore sends
    `tier == 0 ? 0 : min(tier + 1, 6)`: the easiest chart draws one pip instead of a row that reads
    like a missing part, tier 6 and the API's 7 (an unplayable 2x-bass chart) draw the five the
    meter has, and 0 still draws `NO PART`.
  - **Cost, stated plainly**: the devil icon is never asked for — nothing tells us which value
    draws it — and the meter cannot show a sixth pip. Both are written down in
    `docs/reference/known-issues.md`.
  - **Confirmed on the shipped binary** (`out/_swap/store-probe/20261022-084741`, release,
    `--dlc_root=<test library> --store_trace=1`, no other switch): *Otro Día Que Va*
    (catalogue 5/4/3/3/0) draws **5/4/3/3 pips and `NO PART` on the keys row**, and that boot also
    reported `declares 1 song(s)`, `page 4 -> 25 ... (99 total)` and `1 of them marked Downloaded`.
- **The last two halves of the `Exit:` line, closed the same day.**
  - **Multi-song packs.** The retail library in `game/dlc` carries the RB Blitz soundtrack pack in
    **one** package (803,606,528 bytes, `songs/songs.dta` plus 25 `.mogg` files), and a boot with it
    reports `declares 25 song(s) across 21 package(s) (0 could not be mounted) in 29 ms`
    (`out/_swap/store-probe/20261022-090627`) - one package, 25 (artist, title) pairs in the index.
    The other 20 packages declare nothing (their own metadata layout is a separate question, not the
    matcher's). A second RhythmVerse download (*Otro Día Que Va*, 16,080,896 bytes) then made the
    plural case measurable too: `declares 2 song(s) across 2 package(s)` and `99 offer(s), 2 of them
    marked Downloaded` (`-091435`).
  - **The Downloaded affordance is on the page, and its cause was not what the plan assumed.** The
    retail method was printed out of the shipped record rather than guessed at (a new read-only
    mode, `rb_blitz_store_payload_tests store-edit <dtb> buttons`), and its row is inserted by
    `{if_else {offer is_purchased} {insert_elem buttons 0 store_download_again}
    {if {this purchase_allowed offer} {insert_elem buttons 0 store_buy_now}}}` - so it does **not**
    depend on `purchase_allowed`, the gate this build keeps false. The problem is that
    `is_purchased` is answered **natively** by the offer object's own message handler
    (`sub_82453928`, whose literals are `get_data`, `has_data`, `offer_type`, `year_released`,
    `genre`, `is_purchased`, ...) from a commerce state a data-only custom never has, so the
    record's key was never read and the marked offer's page drew Exit alone (`-091435`).
    `store::UseDataForDownloadedRow` redirects that one gate to the record's key
    (`{offer get_data (is_purchased)}`, the same one-component data read retail itself uses for
    `preview_audio`), and the key is now written on **every** offer as `(is_purchased 1)` /
    `(is_purchased 0)`: `get_data` on a missing key walks a **null** - measured, `read of
    0x00000000` on an unmarked offer's page (`-094505`) - which is the same trap the two-component
    `(rank <part>)` read set. Measured after the fix: marked offer `OTRO DIA QUE VA / DOWNLOAD AGAIN
    / EXIT` with **0** fault reads (`-101027`), unmarked offer `EXIT` only and no fault (`-100814`).
- **Tests**: `store_metadata` **72** (the tier mapping's ends), `store_payload` **254** (the
  Downloaded-row gate: native read gone, data read present, both rows kept, idempotency, and both
  refusals), `store_serve` (the key on every offer, `1` for the marked one), `menu_music_pool`
  **4504**, `menu_payload` 109; full `ctest` **45/45** in `out/build/win-amd64-debug`.
- **P3 state after this record**: the phase's `Exit:` line is met and every item on it is measured.
  Recent lists the catalogue with art, a part with no chart reads `NO PART` while the others show
  the catalogue's difficulty, the list reaches past the first API page, an installed package is
  marked **and shows Download Again**, and a multi-song package contributes every song it declares.
  `known-issues.md` keeps the meter's two limits (no devil icon, no sixth pip), the native-property
  trap and the DXT5 texture loss.

P3 inherits three things from P2 (§5/P2 record): **step 0**, locating and hooking the native fetch
entry behind `store_panel request` (nothing can be served or observed before it) — **done, see the
record above**; **deliverable 4**, the loaded-DLC title/artist source the Downloaded state needs;
and **deliverable 5**, the price/icon hide path. The writer skeleton
(`src/store/store_metadata.*`) is where the record's shape changes once the parser can be fed one.

- Generate the store front menu with **Recent** (and a placeholder **Search** entry) and offer
  records for page 1 of `/api/rb3xbox/songfiles/list` (25 API records; unsupported links/formats/
  known archives are filtered by `IsEligibleOffer`), then
  serve them through the chosen seam. Album art: first use `album_art_path`-style references to files we serve; if the
  engine demands platform textures, add host-side PNG→texture conversion (tooling exists:
  `scripts/hmx_tex.py`, `tools/milotex`).
- Field mapping (first cut): `file_title`→`name`, `file_artist`→`artist`,
  `file_album`→`album`, `file_genre`→`genre`, `file_year`→`year_released`,
  `rank_*`→the `rank <part>` values the tier display reads when populated (spike scale/token,
  OQ4); `diff_*` is already a tier and must not be passed as a raw rank,
  `album_art`→`preview_art`.
- Apply §2.12's free UI edits now, along with required attribution and the lazy Downloaded state.
  Wire a missing-rank fallback only after establishing the tier mapping; otherwise show unknown.
- Exit: Recent displays eligible newest `rb3xbox` offers with title/artist/art (possibly fewer than
  25); empty/filtered-only pages work; A opens details. RhythmVerse is visible; no price, currency
  or Buy Now UI appears. Manually installed DLC and multi-song packs match without a store receipt.

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
- Keep a single query field with guidance **Search by artist or song title, one at a time**.
  Include **Try just the artist name or just the song title** in the no-results guidance.
  Send the entered query as one `text` value; do not concatenate artist/title metadata or guess
  a split at spaces or hyphens, which can belong to either name. A combined query's actual empty
  result or API error must use the corresponding UI state, not be reported as a network failure.
- The current settings bridge's token grammar cannot carry arbitrary search strings. Specify a
  bounded UTF-8 transport/escaping scheme (including spaces, slashes, accents and cancellation)
  rather than concatenating raw text into `rbbz-settings/` paths. Apply the identical eligibility
  and instrument predicates to Search; a new query/filter generation invalidates old responses.
- Exit: on-screen query entry → results for artist-only and title-only queries against known
  catalogue entries; multi-word names survive intact; a combined artist/title query exercises
  the documented limitation and retry guidance. Verify cancel paths and no regressions in P3.

**P4 record (2026-10-20) — the field, the keys and the request are landed; the results still wait
for P3's browser.**

*Investigation (measured).* All **131** compiled DTA records of `main_xbox_0.ark` were decoded and
scanned for `keyboard`/`search` (`out/_swap/scan_dtb_tokens.py`): the title has exactly one
keyboard entry point — `rbworld_link.dtb`'s `{rbworld_link_panel show_keyboard}`, a *native* method
of the Rock Central e-mail panel that answers to that panel's own `VirtualKeyboardResultMsg`
handler. There is no generic keyboard screen record to route into, and no store search UI anywhere
(the plan's earlier finding stands), so the plan's option 2 applies: the keys are **authored** in
the house row style.

*Landed, tested without a boot (`store_search`, **283 checks**):*

- `src/store/store_search.{h,cpp}` — the field, the transport grammar, the request and the answer.
  The query is UTF-8 and bounded (3..64 code points, the API's own minimum/maximum; one code point
  is one backspace; invalid UTF-8 and control characters are refused). **The query never travels in
  a path**: a DTA script cannot build a CRC-32 or concatenate a runtime string, so the guest sends
  one key token per press (`rbbz-store/search/key/<token>`) and the host composes the text; a
  whole-string spelling (`rbbz-store/search/text/<hex>`) carries spaces, slashes, quotes and
  accented bytes inside a token-safe alphabet for a caller that can build bytes. `open`, `done`
  and `cancel` complete the grammar, and **`rbbz-store/download/...` is `kNotOurs`** so P5's job
  commands keep their own parser.
- The **session**: `Submit` validates through P0's builder (a too-short field is the API's own
  rule, not a second one), bumps a generation and answers the `RequestSpec`; `Accept`/`Fail` drop a
  stale answer — a newer submit, an edit, a cancel, **a settings commit (the filter is part of the
  query)** and the master switched off all move the generation on, so a late response cannot
  overwrite the current one. The five statuses (`editing`, `searching`, `results`, `empty`,
  `failed`) carry the plan's two sentences verbatim: **"Search by artist or song title, one at a
  time."** on entry and **"Try just the artist name or just the song title."** after no matches.
- `ShapeOffers` applies the **same** `DecideOffer` + `PassesInstrumentFilter` pair Recent will use,
  keeps the API's order and the per-file offers, and resolves each direct link once.
- `src/store/store_search_service.{h,cpp}` — the transport: one worker thread, `store_http`'s
  `PostForm` (redirects never followed), `ParseListing`, and one log line per answer
  (`store_search: N record(s) answered, M eligible offer(s), state ...`). Installed at boot beside
  `InstallPathProbe` (`rb_blitz_app.h`).
- `src/hooks/settings_bridge_hooks.cpp` — the two call sites (`file_exists` predicates,
  `write_file` commands), before the settings gate so they work with the channel off, and **ahead
  of P5's download handler**, which claims the whole `rbbz-store/` prefix for any path it cannot
  parse ("refusing a malformed write") and would otherwise swallow every key press.
- `src/ui/settings_screen.{h,cpp}` — the authored key pages: ten pages per context, each inside the
  six-row budget. The **root** is `Done / Letters / Numbers 0-5 / Numbers 6-9 / Space / More Keys`
  (Done first: it is the one row a player who read the seeded or last-used field needs);
  `Letters` is an index of `Letters A-F / G-L / M-R / S-X / Y-Z and Symbols`; the five letter
  pages, the two number pages and `More Keys` (`Backspace / Clear / Cancel`) complete the set.
  Every key is a `kStoreKey` row writing its own token; **Done** submits *and* leaves
  (`kStoreKeyLeave`); Cancel writes `cancel` and leaves. Every character page's back target is the
  **root**, not the index it came from, so a query can mix letters, digits and a space without
  walking back down; every page's `{enter}` writes `open`, so the host re-shows its field.
- `src/ui/help_overlay.cpp` — the **search strip**: the caption bar draws `Search: <field>` /
  `Searching for "<field>"...` / `N matches...` / the retry guidance while the keys are open (or a
  request is in flight), outranking row captions and drawn even with Settings Labels off — a field
  a player cannot read is an input problem, not optional help, the same rule the binding capture
  follows.
- `src/ui/settings_screen.cpp` + `src/settings/menu_payload.cpp` — the measurement route:
  `AddRhythmVerseRow`'s destination becomes the keys screen while `store_search_diagnostic` is on
  (i.e. the retail **RhythmVerse Customs** main-menu row jumps straight to the keys), so the search
  is drivable before P3's front menu owns a Search row. The first cut appended the jump to the
  retail store screen's `{enter}`, which needed `AppendPanelEnterStatements` to accept a `UIScreen`
  definition as well as a `UIPanel` one (the first boot refused by name: `no {new UIPanel <name> ...} definition for that panel`). That edit was then **retired** — it only fired once the store
  screen was already open, which is the one place P3 will change — and the `UIScreen` widening kept.
- `scripts/probe_search.ps1` — one boot that types a query on the keys and OCRs the strip.
- `src/store/rhythmverse_client.cpp` — one widening the live API forced (§"Measured" below): a
  query with **no matches** answers HTTP 200 with `{"records":{"total_filtered":0,"returned":0}, "songs":false}` — a JSON **boolean** where every matched answer carries an array. `ParseListing`
  now reads a falsey `songs` with a zero record count as *zero songs*, not a parse failure, so the
  plan's own rule ("an actual empty result ... must use the corresponding UI state, not be reported
  as a network failure") holds. Any other non-array shape — and a falsey `songs` with a non-zero
  count — is still refused, and `tests/store_search_tests.cpp` pins both sides.

*Measured in boots (`out/_swap/search-probe/…`, `scripts/probe_search.ps1`).* Everything below is an
OCR of a capture or a log line, not an expectation:

- **The keys render and the field is readable.** `out/_swap/search-probe/20261020-174624`: the
  captured page OCRs as `DONE LETTERS NUMBERS 0-5 NUMBERS 6-9 SPACE MORE KEYS` with
  `SEARCH: BUSH` on the caption bar and `SELECT BACK` in the footer — the authored rows, the
  retail frame and the strip in one shot (`keys.png`, `typed.png`).
- **Key presses reach the field.** The earlier unseeded boot typed on the letters page and the
  strip read `Search: 025`, then `Search: 02555` — one write per press, one character per write.
- **The API's own minimum is enforced in-game.** With the field short, Done was answered and
  refused: `store_search: 'rbbz-store/search/done' answered but not applied: Type at least 3 characters. Search by artist or song title, one at a time.` (`rb_blitz_059.log`, 17:39:30 and
  17:39:32 — the second line is the same press delivered twice, see the limits).
- **Done → request → answer, in the game.** The seeded boot (field `bush`, one Done) logs
  `store_search: 25 record(s) answered, 15 eligible offer(s), state results (current)` and the
  next capture's strip reads `15 MATCHES FOR "BUSH". PRESS DONE TO OPEN THE STORE.`
  (`answer.png`). The offers are the API's order, resolved to direct links.
- **A newer query invalidates an older answer, in the wild.** That same boot pressed Done twice
  (two generations). The first answer was logged `state searching (stale, dropped)` and the
  second `state results (current)`: the drop is the generation rule, not a second code path.
- **The request is what the plan asked for.** `store_search_tests --live-search` sends one `text`
  value with no split or concatenation: `kate bush` → `2 record(s) returned, 2 eligible offer(s)`;
  `dreamers` → 6 returned, **5** eligible (a title-only query); `bush` → 25 returned, **15**
  eligible. The instrument predicates are the settings snapshot's, so the counts already reflect
  the filter.
- **The no-match answer is a UI state, not an error.** The live API answers zero matches with
  HTTP 200 and `"songs":false` (a JSON boolean, 239 bytes) — the first arm through this path
  logged `the answer could not be read: data.songs is not an array` and would have shown a
  *failure*. After the parse widening a boot with the field seeded to `zzzzqqq`
  (`out/_swap/search-probe/20261020-175302`) logs `0 record(s) answered, 0 eligible offer(s), state empty (current)` and the strip OCRs as **`NO MATCHES FOR "ZZZZQQQ". TRY JUST THE ARTIST NAME OR JUST THE SONG TITLE.`** — the plan's second sentence, verbatim, in the game.
- **The field is drawn while the keys are open** in that same capture (`SEARCH: ZZZZQQQ` beside
  `DONE LETTERS NUMBERS 0-5 NUMBERS 6-9 SPACE MORE KEYS`), so the strip and the rows coexist on
  the root page.
- **The combined query does what the plan says it does.** `kate bush` finds 2 offers
  (including *Army Dreamers*); `kate bush army` — the same entry, artist plus title in one field —
  returns **0 records**, so the limitation is real, reachable from the field, and lands on the
  no-match sentence with the retry guidance rather than on an error.
- **The suite is green:** `store_search` **283 checks**, `store_client` 378, `settings_screen`
  137,057, `settings_hot_apply` 310, full `ctest` **42/42**. Two gate repairs were needed on the way
  and are recorded because they were **not** introduced here: `rb_blitz_menu_payload_tests` never
  listed `src/store/store_payload.cpp`, so it linked only while nothing in `menu_payload.cpp`
  reached the store section — it does now, and the source is listed; and
  `settings_hot_apply`'s "exactly two hot rows" pin was stale against `src/settings/schema.cpp`,
  which carries eight (the six RhythmVerse rows joined V-Sync and Settings Labels in P6), so the
  pin and its named list moved with the table.

*Honest limits.*

- **The matches are not drawn anywhere yet.** P4 ends with the offers *answered and held*
  (`SearchSnapshot::offers`, already filtered and URL-resolved); replacing the browser list is
  P3's front menu, which is why the strip summarises the count instead of pretending to list it.
  Until P3 lands, "results replace the browser list" is a data-level claim only.
- **Typing is rows, not a keyboard.** A physical keyboard is not wired into the field: the guest
  has no text input the host can hand characters to on these pages, so a 6-letter query is six
  presses through the index. The whole-string `text/<hex>` spelling exists for a caller that can
  build bytes, and `store_search_seed` exists so a boot can exercise Done without the index.
- **One press can arrive twice.** Measured: a single A press produced two identical
  `search/key/<t>` writes **in the same millisecond**, `lstick_down` moved two rows, and `key:b`
  arrived as Confirm. The host treats an identical command inside 50 ms as the same press (a
  different key, or the same one after a cancel, is a new press), which is why the field does not
  double up — but the *navigation* quirk is not fixable from this lane, so the harness either
  over-shoots or the probe picks its row by index. The live proof therefore seeds the field and
  presses Done once.
- **Cancel and Clear were verified on the host, not on the sheet.** `SearchCancel()` is pinned by
  tests (it clears the field, moves the generation on, empties the strip and stops in-flight
  answers being applied), and the `Cancel` row writes `rbbz-store/search/cancel`, but the harness
  could not be driven to that row reliably enough to OCR the result. The write paths themselves
  are pinned by the settings test, which reads every key's path out of the built pages.
- **Back is asymmetric on purpose.** A character page's back returns to the **root**, so one query
  can mix letters, digits and a space without walking back down the index; the root's back goes to
  the store screen (`store_menu_screen`), which a diagnostic boot never opens — so in that boot the
  root's back lands on the screen P3 is building.
- **Combined queries are a documented limitation, not a bug.** The field is one `text`: a player
  who types `kate bush army` sends exactly that, the API matches it as a single string and answers
  **zero records** (measured above). The no-match sentence carries the plan's own retry guidance,
  which is the intended outcome, not a failure state.

**P4 landing record (2026-10-22) — the results are drawn: the store's Search row opens the keys,
Done submits, and pressing the row again fills the retail browser.** The 2026-10-20 record above
left one item open ("the matches are answered and held, not drawn - they belong in the same list P3
now feeds"); P3 has since landed, and this closes it.

- **The entry that was missing.** P3's front menu has carried a `search` row since 2026-10-21; what
  no boot had was a destination for it. The payload now rewrites `store.dtb`'s
  `store_menu_panel.SELECT_MSG` (`store::RouteSearchRowToKeys`): the measured body's
  `{store_panel request <name> TRUE}` becomes

  ```text
  {if_else {== $ix <row>}
     {if_else {file_exists "rbbz-store/search/show"} <the retail request> {ui goto_screen settings_rhythmverse_search_screen}}
     <the retail request>}
  ```

  The row is identified by **index** (`{component selected_pos}` is already in `$ix`): a comparison
  against `{provider get_string $ix}` would depend on whether that answer is a symbol or a string,
  while `{== $ix 1}` is the integer the DTA already holds and the menu is ours (`kSearchMenuRow`,
  pinned against `RhythmVerseFrontMenu()` by `store_metadata`). Every other row keeps retail's own
  statement verbatim, which the control leg below measures. `rbbz-store/search/show` is the pending
  answer: set when a terminal answer lands (results, no results, a failure), cleared when the
  serving policy hands the page over and by any edit, submit or cancel.
- **The page *is* the answer.** `BuiltinRecord` answers `dlc_store/europe/eng/search/` with the
  `SearchPageSource` installed by `store_search_service.cpp`: the session's accepted offers through
  the same `OfferFromSong` mapping Recent uses, plus the Downloaded predicate, the art prefetch and
  `KeepOnlyHeldArt`. So the browser, the details screen and the Downloaded mark behave exactly as
  they do for Recent - the page goes through retail's own parser and `process_offers`, and an
  **empty** answer is P3's measured zero-offer page (boot 108: the DTA refreshes the menu).
- **`leave` ends the surface, not the search.** Every search page's `exit` writes
  `rbbz-store/search/leave`, which clears only "the keys are up", so the strip stops. The field,
  the answer and the pending mark stay - the Search row is the way to the answer. Two earlier cuts
  are recorded because both were measured on this route: keying the strip on the pending flag made
  the count sit over the main menu after leaving the store, and letting `leave` clear the pending
  flag made the second Search press open the keys again instead of the page.
- **Done deliberately does not leave.** These panels receive one press as two (the duplicate-press
  note in `store_search.h`), so a leaving Done would leave twice and the second delivery would press
  whatever the destination screen lists. The strip therefore says the truth - `N matches for "x".
  Press Back, then Search, to list them.` - and a refused submit (the API's 3-character rule) keeps
  the player on the page with the reason.
- **Measured on the shipped defaults** (`--store_search_seed=bush`, no diagnostic switch;
  `scripts/probe_search.ps1 -SearchMenuRow 1 -StoreRow 3 -Seed bush`):
  - `out/_swap/search-probe/20261022-105010`: the store menu OCRs as `RHYTHMVERSE … RECENT SEARCH`;
    the Search row opens the keys (strip `SEARCH: BUSH`); Done submits and the strip reads
    `15 MATCHES FOR "BUSH". PRESS BACK, THEN SEARCH, TO LIST THEM.` while the log records
    `store_search: 25 record(s) answered, 15 eligible offer(s), state results (current)`; Back
    returns to the store menu; the same row then asks for the page and the **retail browser lists
    the fifteen offers** - `THE PEOPLE THAT WE LOVE / MOUTH / ARMY DREAMERS / SHAKE THAT / BUSH
    AGAIN / COLD CONTAGIOUS / RAINBOW BRITE AND ME / FAVORITE SON …` with the details band reading
    the selected offer's own `BUSH / GOLDEN STATE / BUSH …`.
  - `out/_swap/search-probe/20261022-105401` (the control leg, `-SearchMenuRow 0 -RowOnly`):
    pressing **Recent** still gets the catalogue - `OTRO DIA QUE VA / BUILDING A MYSTERY / BEEN
    THERE. DONE THAT / …` - and an album art is served (`art
    'dlc_store/offers/8a384938c30da5bc/cover_art.png_xbox' returned to the panel`). P3 does not
    regress.
- **Tests**: `store_search` (the presentation cases - pending set/cleared/kept across `leave`, the
  two hint sentences, offers kept when the surface closes), `store_serve` (the search page answers
  the search source, Recent the catalogue's; an empty page without a source), `store_payload` (the
  gate on the shipped shape: both branches, idempotency, the refusals and the compile/parse round
  trip), `store_metadata` (the row index against the menu), `settings_screen` (Done writes and never
  leaves; every search page's `exit` carries the dismissal, and only those pages do); full `ctest`
  **45/45**. `rb_blitz_store_payload_tests store-edit <store.dtb> search` prints the routed
  `SELECT_MSG` out of the shipped record - the gate sits inside the same `{do}` that defines
  `$provider`/`$ix`, which is what makes the index comparison legal DTA.
- **P4's `Exit:` line** is met for the query the boot used (a one-word artist/title text). The
  2026-10-20 measurements still stand for multi-word names (`kate bush` → 2 offers), the
  combined-query limitation (`kate bush army` → 0 records, the retry sentence) and Cancel
  (host-pinned). Not measured: the A press on a *search* offer's details page (the same record and
  the same details route P3 closed; the list and the details band are what this boot reads).
- **Honest limits**: a search is one API page (25 records) - Recent's `store_catalogue_pages`
  accumulation is not applied to the search, so P7's paging rule is still owing there; typing is
  still the authored key rows (unchanged from 2026-10-20); and the answer is a snapshot, so
  re-pressing Search after browsing results opens the keys again (the pending mark was consumed by
  the listing).

*(Superseded 2026-10-23 by §9/P9-11's record: the authored key pages are **deleted** and the
Search row opens the title's own keyboard, so "typing is still the authored key rows", the two
"Press Back, then Search" sentences and the key-page test cases this record lists no longer
describe the build; the measurements it took - the request, the answer and the served page - stand.)*

### P5 — download pipeline

- Host `store_download` implements **all of §3.1**, using the offer's validated URL held by the
  host; the guest sends an offer/job token, never an arbitrary URL or destination. Start/cancel
  are idempotent; serialize conflicting installs. Offer lookup remains stable across page changes.
- Stage under a reserved `<resolved_dlc_root>/.rbbz-store-staging/` directory on the destination
  volume. **First add and test scanner exclusion for that directory**, including recursive flat
  scans and fingerprints, and ensure no content-manager registration exposes it. If that cannot
  be guaranteed for a configured source, use a same-volume staging directory outside every source
  or refuse the transfer. A dot prefix or `.part` suffix is not enough: scanners inspect magic,
  so a half-downloaded `CON ` can otherwise be enumerated. No final title-ID directory is created
  inside a flat library (§2.9).
- Validate, flush/close, then atomically rename to a safe name of ≤42 UTF-8 bytes, with collision
  detection and no silent overwrite of user packages. **Revised 2026-10-23 (owner):** the name is
  the catalogue's `Artist - Song` after sanitizing - every character Windows refuses and every `.`
  removed, whitespace collapsed, both ends trimmed, cut at a UTF-8 code-point boundary - and it
  carries **no extension**; a device name (`CON`, `NUL`, `LPT1`, ...) gets a trailing `_`, a
  collision adds `~<n>`, and an offer with no usable name falls back to `rv_<32 hex>` of its
  opaque id. The staged part file keeps that hash as `rv_<32 hex>.part`. The P5 build below
  shipped `rv_<32 hex>.con`; that spelling is historical. Existing loaded packages must not be
  replaced in place; an explicit Download Again either installs a distinct package or reports
  that replacement requires restart. Everything that reaches the file system passes the
  sanitizer: no `Content-Disposition` name and no raw opaque id is ever joined into a path
  unsanitized.
- Cancel and clean up on user cancel, master Off or shutdown; drain workers before restart.
  Retry transient network failures at most twice with backoff (honor Retry-After for throttling);
  do not retry archive/policy/validation errors. Give specific disk-full/read-only/HTTP/size errors.
- Bridge: use exact `rbbz-store/` namespace dispatch alongside the existing hooks, with a dedicated
  parser/sink. A feasible current read protocol is `file_exists` predicates such as
  `state/<job>/downloading`, `state/<job>/installed`, `state/<job>/error`, and
  `progress/<job>/at-least/<bucket>`. Specify error predicates and bounded tokens too. A proposed
  `state/<id>` returning `"download:42"` does **not** work through the present one-bit bridge.
  Alternatively implement and test a typed read channel before depending on one.
- UI: replace the buy/re-download actions and their purchase confirmations; use Download,
  Downloaded and Download Again with retail progress/error components. A verified file is an
  install result, not a purchase entitlement. No Marketplace import is reached.
- Matching: §3.2 is required, including manual DLC, packs, deletion/root changes and unknown state.
  Optional `store/downloads.toml` records file ID/path/size/date for receipts and pending discovery;
  it never replaces the loaded-song index.
- Refresh: extend the host refresh to cover the selected destination/layout even with cache Off,
  publish on the owning guest thread, then invoke a proven guest discovery path at a safe menu
  boundary. Do not start discovery while a store provider holds invalidated objects. Until that
  path is proven, show restart required and verify the song after restart; do not call the existing
  refresh sentinel sufficient. Update the loaded-song generation after discovery completes.
- Exit: a verified direct package is playable after the proven refresh or explicitly required
  restart; downloaded/manual songs match on a later visit. Archive, redirect, cancellation,
  truncation and simultaneous library refresh cases leave no visible partial package. Failed
  transfers leave no receipt/Downloaded state. Verify flat and structured roots and cache On/Off.

**P5 record (2026-10-20) — the pipeline is built and tested hermetically; no boot has driven it
yet.** Landed as four SDK-free modules plus one Windows transport, wired to the boot and to the
guest bridge:

- `src/store/store_download.{h,cpp}` — the policy core: the job token (`j` + 16 hex of the file
  id's SHA-256, never the opaque id), the bridge grammar, the error vocabulary and retry decision,
  and `TransferPackage`, which applies §3.1 end to end: hop-limited redirects resolved through
  `ResolveDownloadUrl` (off-site, scheme-relative and credentialed targets refused), response-head
  refusal (archive MIME/`Content-Disposition`, text/HTML/JSON pages, non-identity
  `Content-Encoding`, an oversize `Content-Length`), streaming prefix classification (only `CON `/
  `LIVE`/`PIRS` proceeds; ZIP/RAR/7z/HTML are named refusals), the 512 MiB ceiling, the
  API/`Content-Length`/received cross-check, and the completed file's own STFS identity (RB3's
  title id; the content type is recorded, not policed, because a dumped library presents saved-game
  packages).
- `src/store/store_install.{h,cpp}` — staging under `<resolved_dlc_root>/.rbbz-store-staging/`,
  `rv_<32 hex>.con` (39 bytes; a collision adds `~<n>` and still fits the guest's 42-byte field),
  the layout rule (structured root → `45410914/00000002/`; flat root → a loose container; an absent
  root is created flat, never with a title-ID level), and no silent overwrite of a user package.
  *(Superseded 2026-10-23 by the revised naming bullet above: the package is `Artist - Song` with
  no extension, the 42-byte cut and the device-name guard are part of the rule, and only the part
  file keeps the `rv_<32 hex>` spelling.)*
- `src/store/store_download_service.{h,cpp}` — the offer registry (`RegisterOffer`, the P3
  contract: the host holds the URL, the guest only ever sends a token), one worker with a bounded
  queue (8), idempotent start/again/cancel, master Off refusing a start and cancelling a running
  transfer at the next chunk (the snapshot is read per chunk, so the commit is live), specific
  disk-full/read-only/storage errors, receipts at `<user_data_root>/store/downloads.tsv`
  (transfer history only, never ownership), and the post-install refresh callback.
- `src/store/store_transfer_winhttp.cpp` — the streaming transport: redirects never followed,
  transparent content decoding disabled, 64 KiB chunks. `store_http.cpp` stays the small-body
  catalogue transport.
- Scanner exclusion, as P5's first deliverable asked: `fs::kStoreStagingDirName` is skipped by the
  flat library walk, the cache fingerprint and the structured scan (silently — a staging directory
  is not a misplaced title level), so a partial `CON ` can never be enumerated and a transfer in
  flight cannot invalidate the cache on every byte.
- Wiring: `dlc::Configure` publishes this boot's resolved DLC root and measured layout, the
  receipt path and `WinHttpTransfer()`; `dlc::RefreshAfterStoreInstall()` re-walks the flat roots
  **without** the R7 cache gate (an install is not a cache feature) and hands the result to the
  existing guest-thread publish; `DrainRefreshForRestart` stops downloads first. The bridge hook
  answers `rbbz-store/state|progress|error/...` reads and `rbbz-store/download/start|cancel|again/...`
  writes before the settings gate, like P1's gate query.

Tests: ctest **`store_download`**, 206 checks — token and grammar, retry/backoff, staging and
commit (including the collision name), the full transfer matrix (happy path, 302 → same-site, 302
→ off-site/scheme-relative, ZIP/HTML/wrong-title/short-header, MIME and disposition refusals,
length mismatch, truncation, ceiling, unknown length, 404/429/network, master-Off and cancel
mid-body), the service (install, idempotent start, Download Again, queued cancel, master Off
refusal, unknown token), and the scanner/fingerprint exclusion. Full `ctest` is **42/42** on the
debug tree (the count includes P4's `store_search`, in the working tree at the time), and
`rb_blitz.exe` links with the new sources. One real defect fell out of the
suite: a cancel of a **queued** job logged while holding the service mutex — `LogLine` takes the
same mutex — which deadlocked the worker; the log is now outside the lock.

**P5 landing record (2026-10-22) — the guest half is wired and a boot drove the whole pipeline.**
The 2026-10-20 record above ends with "nothing registers offers or starts a job"; that was true then.
What the guest now does, in the order a press walks it:

- **Registration at serve time** (`src/store/store_serve.cpp`, `AttachDownloadJob`). Every offer a
  page carries is handed to the worker as the page is written, from the offers' own fields: the
  resolved URL `DecideOffer` accepted, the catalogue's byte count and the "Artist - Title" the
  strip shows. One seam (`store_serve.h`'s `OfferRegistrar`, installed beside the destination in
  `src/hooks/dlc.cpp`) means the suite installs a fake and the boot installs the service, and it is
  the same call for Recent and for the search page - `Offer::file_id`/`download_url`/`size_bytes`
  are host-only, exactly like `art_url`, and never reach a record.
- **The row** (`src/store/store_payload.h`, `AddDownloadButton`). A free custom is never
  purchasable, so the details page a served offer opens carried Exit alone. The transform inserts
  `store_download_now` into the **not-downloaded** branch of `refresh_buttons`'
  `{if_else {offer is_purchased} …}` gate, keeping retail's own else branch (the buy path) by
  value. Retail's `store_download_again` row is untouched, so a downloaded offer draws
  `DOWNLOAD AGAIN / EXIT` and a fresh one draws `DOWNLOAD / EXIT`. The label is the locale key the
  symbol names, added with `screen::AddLocaleEntry` in the same rhythmverse-gated block.
- **The press** (`RouteDownloadButtons`). Retail's `store_download_again` case calls the native
  `{store_panel re_download …}`, which needs a commerce object this build never creates; the case is
  retargeted and a `store_download_now` case appended, both writing the request path
  `rbbz-store/download/start|again/<job>` through `write_file`. The token is the record's own
  `(job "…")` key - written on **every** offer by both writers (`store_metadata.cpp`), because the
  row reads it unconditionally and a missing key is the null walk this build has measured twice.
- **The strip** (`src/ui/help_overlay.cpp`, `store_download_service.h`'s `DownloadStatusText`):
  `Downloading 'Artist - Title': N%`, `Waiting to download …`, `Installed '…'. Restart the game to
  play it.`, `Download failed for '…': <error>.`, and the cancelled pair - drawn where the search
  strip draws, below it and above the row caption, for 30 s after a terminal state.
- Accepted start/again writes are logged (`store_download: start accepted for <job>`), so a boot's
  log answers "did the press arrive" separately from the transfer's own lines.

Measured on `out/_swap/store-probe/20261022-124753` (boot, flat scratch root
`out/_swap/p5-dlc`, profile `out/_swap/p5-profile`): store -> RECENT -> the first offer
('Sweet Surrender') -> **DOWNLOAD**; the strip drew `INSTALLED 'SARAH MCLACHLAN - SWEET
SURRENDER'…` on the +3 s capture and was gone on the +40 s one; the log shows
`start accepted for j4a4d1603aab322a6`, then `installed …\rv_4a4d1603aab322a6ab257e03977614d1.con
(5238784 bytes, title 1161890068)`, then `the store's install asked for a refresh; scanning 1 library
root(s)` and `the refresh finished (1 package(s))`. On disk: the 5,238,784-byte container, an empty
`.rbbz-store-staging/`, and the receipt row
`<sha> j4a4d1603aab322a6 rv_….con 5238784 pending <stamp>` in `<user_data_root>/store/downloads.tsv`.

Two guest-side defects fell out of the boots, both with the same signature (`read of guest
0x00000000` the frame the row was pressed) and both in this payload's DTA, not in the worker:

1. `{cur_offer get_data (job)}` - `get_data` sent at a **property receiver**. Retail binds the
   property into a local first (`{var $offer {property cur_offer}}` in `refresh_buttons`) and reads
   the offer from that.
2. The binding written as a `{var $offer …}` **command** inside a `do`. In a compiled record a
   binding statement is an **array** (`(Var value...)` - retail's own tree), and a `var` command
   there is not one. A literal probe write in front of the binding proved it: the probe answered
   `store_download: refusing a malformed write: rbbz-store/download/start/--` and the crash
   followed in the same millisecond.

The `{offer has_data job}` guard (retail's own `has_data preview_audio` shape) is what turns a
record without the key into a message instead of a crash.

Remaining honest limits (P5's exit is met; these are the parts it does not claim):

- The post-install **refresh** runs and reports `1 package(s)`, and the **next boot's library scan sees the new
  package** - `out/_swap/store-probe/20261022-130126`'s log has
  `dlc: 1 saved-game library package(s) presented as content type 00000002` and
  `store_library: the Downloaded mark is built from the loaded DLC packages' songs.dta`. That boot then sat on the
  title's "Loading Cached Content List" until the harness gave up, so the *page* half - the served-page line
  `N of them marked Downloaded` and a details page drawing `DOWNLOAD AGAIN` - is **not measured yet**; the line to
  read on the next successful store visit is `store_fetch: the served page carries N offer(s), M of them marked
  Downloaded`. The title's own discovery of a newly installed song inside the same session is still OQ6's open half,
  which is why the strip says **restart**.
- A boot that sees the DLC library change spends minutes on that loading screen in this environment (two of five
  P5 boots never left it in 240-480 s), so a P5 evidence run wants a long `-ScreenTimeoutSec` and a log check
  before blaming the pipeline.
- §3.1 step 4's "readable song metadata" is only the STFS header identity; mounting the package
  and reading `songs/songs.dta` before commit is still owed.
- The §3.2 matching index stays P3's loaded-library work; a receipt never makes a song
  "Downloaded", and a failed transfer leaves none.
- Structured roots are not rescanned after an install (their package list is captured at boot);
  the next boot discovers them.
- The service's static destructor joins the worker, so a process that exits mid-transfer can wait
  up to the transport's receive timeout.

### P6 — RhythmVerse settings in General → Downloadable Content

- Implement the exact hierarchy/order/defaults in §1. Six new native boolean settings, suggested
  stable tokens: `general.rhythmverse`, `general.rhythmverse_guitar`, `general.rhythmverse_bass`,
  `general.rhythmverse_drums`, `general.rhythmverse_vocals`, `general.rhythmverse_keys`. Group the
  master under `dlc`; the filter submenu has no SettingId and its five leaves use their own group.
  Extend the stable model/token table/schema/storage and the authored panel/locale/help tables.
- Keep IDs stable and defaults for absent keys explicit; verify existing settings files retain
  unknown fields and all unrelated values. Use the standard transaction/commit path and implement
  a store settings applier so these values become effective live, exposing the enable signal P1's
  main-menu gate will consume.
  Workers receive immutable snapshots; no worker reads the settings/cvar registry or guest memory.
- A filter commit resets paging and invalidates cached requests by query/filter generation.
  Master Off cancels in-flight store work and ignores late results; turning it back On keeps the
  saved filters. The filter page remains accessible while Off. Include pause-context navigation.
- Exit: exact text/order/defaults, Back behavior, request bodies and persistence pass; master Off
  makes zero store requests, On works without restart, all-Off filter behavior is covered, and
  the existing settings/menu tests still pass.
  **P6 record (2026-10-20).** Six native rows, ids **80–85** and tokens `general.rhythmverse`,
  `general.rhythmverse_{guitar,bass,drums,vocals,keys}` (appended; nothing renumbered). Defaults are the
  owner's: master **off**, G/B/D/V **on**, Keys **off**. The master sits in the `dlc` group, the five
  leaves in a `rhythmverse` group; the filter is a plain submenu with no `SettingId`.
- **The page was reorganised to keep its six-row budget** (in-game-settings skill §2): the
  Downloadable Content page is now `Cache DLC Library`, `Refresh Music Library`, `DLC Scan Threads`,
  `RhythmVerse`, `RhythmVerse Song Filter`, `DLC Menu Music` — the last being a new submenu that
  holds `DLC Menu Songs` and `Play Current Song`, which were two rows of the page before (a seventh
  row would have scrolled invisibly, and no scroll element is drawn). Both new pages get the
  in-song twin automatically (`InSongPanels`), so a paused song reaches them too.
- **Live apply**: `src/store/store_settings.{h,cpp}` holds the store's settings as an **immutable
  snapshot** (generation 1 = compiled defaults; an equal publish keeps the generation), and
  `src/hooks/hot_apply_runtime.cpp` applies a committed row to it — no cvar, no guest state — so the
  six rows are `kHot` with that observed behaviour as their evidence. Workers will read the snapshot
  and never the settings store or a cvar registry (project rule). **Superseded 2026-10-22 by
  §9/P9-2, for the master only**: `general.rhythmverse` is `kRestart`, because the switch also owns
  what the boot drew (the main-menu row and the store's heading), while the five filter leaves keep
  the `kHot` this record measured.
- **Tests**: `settings_screen` grew `TestRhythmVerseRows` (page order for all three pages, both
  contexts, exact labels, the 28-character line budget, and the two new panels' in-song twins) and
  its pinned page counts moved 57 → **59** per context (114 → **118**, non-root cancels 112 →
  **116**); `store_client` grew the row → snapshot cases (live generation bump, no-op on an equal
  commit, wrong-kind and foreign-row refusals). Full `ctest` is **38/38**.
- **Not done in P6**: nothing consumes the snapshot yet — P1 builds the menu gate and P3 the
  catalogue worker — so "master Off makes zero store requests" is structural (the snapshot is the
  only input a worker will have) rather than yet measured; no boot was driven to read the new page.
  The plan's P6 wording also owed `store_http`'s request bodies, which P0 already built and tested.

### P7 — polish

- Chunking: preserve API-page position separately from visible eligible entries. Do not stop at
  a page emptied by filtering; advance with a bounded per-action fetch budget and cancellation.
  Underfilled pages are valid. Avoid presenting server `total_filtered` as the eligible count.
- Caching: in-memory page cache with TTL (~5 min) + single-flight; disk cache optional
  (`<user_data_root>/store/cache/`); polite `User-Agent`; backoff on 5xx.
- Refine the required RhythmVerse heading/credit from P1/P3; attribution is already present.
- Empty-state and offline text; the store's own `store_info_unavailable_screen` copy reused.
- Optional: recommendation/marquee areas hidden or fed from the same list (do not fetch extra).

**P7 record (2026-10-22) — the polish landed in code, hermetically tested, and the credit line is
measured on screen.** P7 has no `Exit:` line of its own, so this is the per-bullet account, each
with the module that owns it.

- **Chunking.** The cursor is the **server's own page number**, kept separately from the offers a
  caller has kept: `rhythmverse_client.h`'s `NextPageAfter(page, pages_fetched, page_budget)` reads
  `ListingPage::page` + `HasMorePages()` and never the visible count, so a page the
  eligibility/instrument filter **emptied** continues the walk, an underfilled page is valid, and
  `total_filtered` is never read as a count of downloadable offers (the strips count eligible
  offers; the log lines print `record(s) answered` beside the eligible count). Both workers use the
  one helper: the catalogue's `store_catalogue_pages` (default 4) and the search's new
  `store_search_pages` (default 4) are the per-action budgets. **Cancellation** is a check, not a
  comment: the catalogue tests `StillWanted(generation)` (same settings generation, master still
  on, worker not quitting) before every page *and* inside a sliced `PauseBetweenPages`, so a
  filter commit or the master switch stops the walk **before** the next request; the search's
  equivalent is `SearchPageWanted` + `SearchBuildPageRequest` returning nothing once the job is
  stale. A cancelled catalogue drops the pages it had (the filter that produced them is gone); a
  cancelled search keeps the field and the previous answer.
- **Caching.** `src/store/store_page_cache.{h,cpp}` — the in-memory page cache with the plan's
  ~5 min TTL (`store_page_cache_ttl_seconds`, 0 = off), a 64-entry bound with oldest-first
  eviction, and **single flight** per key. The key is the endpoint **and the form body**, so the
  query text, the page number, the page size and the instrument filter each identity a page - a
  settings commit cannot read a page fetched for the filter it replaced. One shared instance, so
  Recent and Search reuse each other's pages. **The disk cache (`<user_data_root>/store/cache/`) is
  not built**: the plan marks it optional, and it would need the user-data root and a wire format
  inside a module whose point is being SDK-free; the in-memory TTL covers a session. The
  transport's `User-Agent` is already the plan's product string
  (`rb-blitz-store/0.1 (+https://rhythmverse.co/)`).
- **Politeness / 5xx.** `FailureBackoffSeconds(status, retry_after)` is the one policy: the
  server's `Retry-After` wins within 5..600 s (`RetryAfterSeconds` parses the delta-seconds form
  only), a 5xx or 429 without one is 60 s, a transport failure 10 s, a `200` that could not be read
  60 s, any other status 120 s. The catalogue holds its next fetch off for exactly that
  (`g_retry_at`; a page that lands clears it, a partial list keeps it, so the pages that failed are
  retried after the wait rather than on the next keypress). The search session gains the same wait
  as state: `SearchFailAfter` stores it, `SearchSubmitQuery` **refuses inside it** with
  `the catalogue is busy; try again in N s` instead of sending, and a page that lands, a cancel or
  the next accepted submit clears it. A failure after some pages landed ends the walk but keeps the
  offers already read - a partial list is a working store, not an error page.
- **Heading and credit.** The heading stays `kStoreHeadingText = "RHYTHMVERSE"` because
  `CUSTOMS FROM RHYTHMVERSE` clips in the retail header (measured 2026-10-21). The full attribution
  is now drawn where a sentence fits: the caption strip's credit line
  **`Customs from RhythmVerse`**, shown when the store's front menu is served.
- **Empty state and offline text.** `src/store/store_catalogue_status.{h,cpp}` holds the sentences
  and the precedence - **failure > empty > loading > credit** - with the honest reasons: a failed
  fetch says what failed (`RhythmVerse could not be reached: the catalogue answered HTTP 503. Press
  Back and try Recent again.`), an empty one says where the filter lives, a fetch in flight counts
  the pages read so far, and the `store_catalogue_off` diagnostic explains its own silence. Two windows keep it
  honest: nothing is said before a store surface served a record, and nothing after
  `kCatalogueSurfaceWindow` (20 s) - so the strip cannot follow a player out of the store, the
  mistake the search strip made once. Retail's own `store_info_unavailable_screen` is **not**
  fabricated for an empty page: that screen is the enum-error route and keeps retail's copy; an
  empty page is a normal answer the DTA already handles (it refreshes the menu), and the strip is
  what turns that silent return into a sentence. The overlay draws it below the search and download
  strips, above a row caption, and with Settings Labels off.
- **Recommendation / marquee (optional): not done, on purpose.** `MESSAGES`, `RECOMMENDED` and
  `YOUR SONGS` are **scene labels** in the store menu's art, not rows the DTA lists - measured
  2026-10-22, the list carries only the served submenus (RECENT = 0, SEARCH = 1). Feeding them means
  extra fetches, which the bullet forbids; hiding them means a `.milo` edit, which is not this
  phase's seam. The list a player can act on is complete without them.

Verification: **47/47** ctest in `out/build/win-amd64-debug` with two new suites - `store_page_cache`
(**39 checks**: the TTL, the bound, the eviction order, single-flight, the shared instance) and
`store_catalogue_status` (**28 checks**: every sentence and the precedence) - and the extended ones:
`store_client` **402** (the chunking cursor and the backoff table, plus `Retry-After` parsing),
`store_search` **384** (a filtered-empty page continuing the walk, the budget, cancel/commit
cancellation, a partial failure keeping its offers, the busy refusal). The release preset builds and
links `rb_blitz.exe`. **On screen** (`out/_swap/store-probe/20261022-140259`, release binary, a
scratch profile with `[general] rhythmverse = true`, `-StoreRow 5 -LocalRoot ""`): the store menu
draws `RHYTHMVERSE … RECENT SEARCH` and the strip draws **`Customs from RhythmVerse`** across the
bottom of the screen (OCR of `store.png`; the harness's own store OCR also fragments it as
`R USTOMS`), and the log carries the walk this phase added:

```text
store_search: transport ready (one worker thread, /api/rb3xbox/songfiles/search/live, up to 4 page(s) per search)
store_catalogue: fetching up to 4 page(s) (the store opened)
store_catalogue: page 1 -> 24 eligible offer(s) of 25 record(s) (24 total)
store_catalogue: page 2 -> 25 eligible offer(s) of 25 record(s) (49 total)
store_catalogue: page 3 -> 25 eligible offer(s) of 25 record(s) (74 total)
store_catalogue: page 4 -> 25 eligible offer(s) of 25 record(s) (99 total)
store_catalogue: 4 page(s) -> 99 offer(s) for the Recent list
```

The three sentences the boot did **not** exercise are the loading line (the store's screenshot was
taken after the four pages had landed), the empty state (the live catalogue answered 99 offers) and
the offline reason (it needs an unreachable site, which the build cannot be pointed at) - all three
are pinned by `store_catalogue_status`'s suite instead.


### P8 — acceptance + docs

- `scripts/acceptance_store.ps1`: enable under General → Downloadable Content → return to main
  menu → RhythmVerse Customs → assert Recent list OCR ("Recent",
  first entries) → open Search → assert results → select an entry → download → wait for file →
  refresh/restart as indicated → assert the song appears in the song list; plus a control leg
  with the feature Off (no store requests and the existing Refresh Music Library action intact).
- Acceptance matrix: stock/Ultimate; cache On/Off; flat/structured roots; master/filter defaults
  and persisted changes; artist-only/title-only/multi-word search and combined-query guidance;
  manual DLC/pack matches without receipts; deletion/root-change invalidation;
  per-file duplicate offers; no price/currency/purchase UI in browser/details/return states;
  direct raw package, visible/encoded/disguised archive, same-site HTML, off-site redirect, unknown
  length, interrupted transfer and refresh during transfer. Downloads must never launch a browser.
- ctest additions from P0/P5/P6 (all hermetic where possible).
- Docs: `docs/guides/dlc.md` cross-reference, this plan's phase status updated,
  `docs/reference/known-issues.md` gets the
  scoped statement ("the store's catalogue can be supplied locally by the mod; online services
  remain out of scope").

**P8 record (2026-10-22) - the acceptance harness, the matrix, and the phase's docs.**

`scripts/acceptance_store.ps1` is the tracked harness the bullet above asks for. It drives the
feature in the order a player takes it, split into four legs so each can be run and read on its own
(`-Legs`, with `-Reuse` to run the rest later on the fixtures the first one wrote):

| Leg | What one boot proves |
| --- | --- |
| `Enable` | `Help & Options > General > Downloadable Content > RhythmVerse` switched **On through the UI**, and the committed value read back out of the profile the next boot uses. A separate boot because P1 measured that the row's *presence* is baked at boot while the press is live. |
| `Store` | The row opens the retail store; the front menu draws `RECENT`, `SEARCH`, the `RHYTHMVERSE` heading and the `Customs from RhythmVerse` credit; the catalogue's eligible count and the served page are read from the log; the retail browser opens on the offers; Search submits the seeded query, the strip reports the matches, and the same row opens the results page in the browser; A opens the details screen, DOWNLOAD is pressed, and the install is asserted on disk (one container, flat root, empty staging), in the receipt (`pending`) and in the log (start, install, refresh); no price, currency or purchase word appears on the browser, the details page or the browser it returns to. |
| `Install` | The next boot, same profile and DLC root: the title's own song list carries the downloaded title, whose name is parsed from the Store leg's install line. This is the plan's "appears in the song list" and the reason the strip says *Restart the game to play it*. |
| `Control` | RhythmVerse **Off**: the main menu has no store row, the log carries **no** store request at all (`store_catalogue: fetching`, `store_fetch: served '`, `store_fetch: the served page carries`, `store_library: indexing`, `store_download: start`, `store_search: page` - all absent, while the modules' own boot lines are present), and the main menu's **Refresh Music Library** row still dispatches `dlc: refresh asked for from the main menu`. |

Two scratch fixtures, never the owner's files: a profile directory per leg family (the launcher
profile and its `settings.toml` sidecar, where a committed setting lives) and an **empty** DLC root,
so the install is the only custom package in it. Both profiles also ask for the retail store's
replacement row (`general.refresh_music_library = true`) and hide LEADERBOARDS and ACHIEVEMENTS -
with those two drawn the six-row menu is full and the store row, appended after the refresh row, is
*selectable but off the visible page* (the P2 defect this plan already records). Rows are clicked at
the bounds the host logs (`native_mouse: row=<n> bounds=…`); a click's **screen** is the oracle
(`native_mouse: screen=` / `help_observer: screen '…'`), and long labels are matched with the
whitespace folded out because OCR drops letters from them (measured: `REFRESH MUSIC LIBRARY` came back
as `REF ESH MUSIC LIBR RY`).

**The acceptance matrix**, row by row, with where each row's evidence actually is:

| Matrix row | Verdict |
| --- | --- |
| stock / Ultimate | **Booted (Ultimate)**: every leg runs `--ultimate_mode=1` (auto - the payload is applied when it is on disk, which it is here) with the profile's `[launch] target = "ultimate"`. The stock half is the same code path with no payload to apply; not re-booted in this phase. |
| cache On / Off | **On measured** (the acceptance boots run the default). Off is the same walk with R7's gate skipped: `dlc::RefreshAfterStoreInstall` names it (an install is not a cache feature) and the refresh row's press is the one thing that waits on the cache (`WaitForRefreshToLand`). Not booted with the cache off. |
| flat / structured roots | **Flat measured** (the scratch root is a flat library: one loose `*.con`, no title-id level created). Structured is pinned by `store_download`'s `TestInstallPlanning`/`TestStagingAndCommit`; not booted. |
| master/filter defaults and persisted changes | **Measured**: the Enable leg reads `RHYTHMVERSE: OFF` on the page, presses the row, reads `RHYTHMVERSE: ON` and finds `rhythmverse = true` in `settings.toml`; defaults, page order and the five filter rows are pinned by `settings_screen`, and the filter's request body by `store_client`/`store_search`. |
| search: artist-only / title-only / multi-word / combined | **Measured 2026-10-20** live (`bush` 25/15, `dreamers` 6/5, `kate bush` 2/2, `kate bush army` 0 records - the documented combined-query limitation) with the P4 probe; the acceptance leg seeds `bush` and reads the strip once it can pass the store menu, which it has not reached yet. The sentences are pinned by `store_search`. |
| manual DLC / pack matches without a receipt | **Measured 2026-10-22**: a real RhythmVerse package and a real 25-song pack (`declares 25 song(s) across 21 package(s)`, `declares 2 song(s) across 2 package(s)`, `99 offer(s), 2 of them marked Downloaded`); pinned by `store_library` and `menu_music_pool`'s real-`CON` case. |
| deletion / root-change invalidation | **Host half pinned**: `store_library`'s `TestClear` is what an invalidated library looks like, `InvalidateLibrary()` runs after a refresh and a store install, and the predicate answers "unknown" until the rebuilt index lands. Deleting a package and re-visiting the store was **not** booted. |
| per-file duplicate offers | **Pinned by P8**: `store_search`'s new `TestPerFileDuplicates` - two files for one artist + title stay two offers in the API's order, each with its own id and resolved link. |
| no price/currency/purchase UI (browser / details / return) | **Measured in the P3/P5 probe boots** (`out/_swap/store-probe/20261021-190304` and the P5 download boot, OCR of the browser, the details page and the return screen) and pinned by `store_payload`'s edits that remove the chrome. The Store leg asserts the same three screens; it has not reached them yet. |
| direct raw package | **Measured by P5's download boot** (`rv_4a4d1603aab322a6ab257e03977614d1.con`, 5,238,784 bytes, `CON ` verified by its STFS header before the commit); the acceptance harness installs the same way once it gets past the store menu. |
| visible/encoded/disguised archive, same-site HTML, off-site redirect, unknown length, interrupted transfer, refresh during transfer | **Pinned** by `store_download`'s transfer matrix (206 checks: the redirect chain, MIME/disposition refusals, prefix classification, length cross-checks and truncation, the ceiling, cancel mid-body, master-Off mid-body). None of these is booted; the archive/off-site cases never reach the store's list at all. |
| Downloads never launch a browser | **Structural**: `src/store/` contains no `ShellExecute`/`ShellExecuteEx`/`CreateProcess`/`WinExec` (the only `CreateProcessW` in the project is `src/platform/game_restart.cpp`), and the download is WinHTTP plus a staged file rename. |

The one code change the phase made is a log line: the install line now names the job it finished
(`store_download: job <job> installed '<Artist - Title>' <path> (<N> bytes, title <id>)`), which is
what lets a later boot assert *which* song it must find on the title's list. The receipt keeps no
name by design (`store/downloads.tsv` is transfer history, not ownership).

The harness itself cost eight boots before it drove the store end to end, and every defect it hit is
now one of the harness's own rules - which is the only reason the list is worth keeping:

- `drive_ui.ps1` joins its `-OutDir` against the repository root, so an absolute value becomes
  `<root>\D:\...` and every capture dies.
- a blind repeated accept at the title lands on whatever row the menu has selected (measured
  entering the refresh row's discovery pass), so the route looks at the screen and presses only
  while it still reads `TO START`;
- the store row is *selectable but off the visible page* when the six-row menu is full
  (`LEADERBOARDS`/`ACHIEVEMENTS` drawn), so the fixture hides those two rows;
- OCR drops letters from long labels (`REFRESH MUSIC LIBRARY` read as `REF ESH MUSIC LIBR RY`, and
  a selected row's highlight eats the first letter of `RHYTHMVERSE CUSTOMS`), which is why the label
  checks fold the whitespace out and match tolerant words instead of a spelling;
- the discovery caption is drawn in **mixed case** (`Discovering Downloadable Content`) while every
  menu row is upper case, so the needles are compared upper-cased;
- `--dlc_root` is resolved against `--game_data_root` when it is relative - a relative scratch root
  silently becomes `<game>\out\...` and the boot logs `no content directory`, DLC is then served
  from the content root alone;
- `--log_level=debug` spends 4.7 MB on four minutes of APU/GPU chatter and makes a stalled boot
  unreadable (a boot that hangs before presenting writes *only* that), so the harness logs `info` -
  every line it asserts on is an info line;
- **the boot route is the log's, not the screen's.** The menu is proven by a host line (`content_pipeline: usable menu`, or a hovered row table), and the route is *press, hover, look, press again*: a hover costs two seconds and no OCR, and the menu is the only screen in this build that has a row table. A press is sent only after a look that found no rows, so the accept that opened the menu in every run cannot land on a row. This is the shape that finally walked the Enable and Control legs to the end (`BLITZ / HELP & OPTIONS / EXIT GAME / REFRESH MUSIC LIBRARY [/ RHYTHMVERSE CUSTOMS]`, the rows at `native_mouse: row=` bounds);
- and the same rule applies to the operator: a terminal command raised over the game window makes
  `capture_window.ps1` read the covering window, so the run owns the foreground for its whole length.

The abandoned runs are kept beside the evidence (`out/store-acceptance/p8-store-attempt*/`) rather
than deleted: each one is a measured reason for a rule above.

**Verification.** Two of the four legs are green in `out/store-acceptance/p8-store/`, on the
release binary this record describes:

| Leg | Verdict | What its own log/OCR says |
| --- | --- | --- |
| `Enable` | **PASS, every step** | the DLC page reads `RHYTHMVERSE: OFF`, the row is pressed, `rhythmverse = true` is in the profile, and the page then reads `RHYTHMVERSE: ON` with the caption *"Browse the RhythmVerse customs catalogue in the store and download songs."* |
| `Control` | **PASS, 13 step(s) / 0 failure(s)** | the menu is `BLITZ ULTIMATE / HELP & OPTIONS / EXIT GAME / REFRESH MUSIC LIBRARY` - **no store row**; the Refresh Music Library row dispatches `dlc: refresh asked for from the main menu`; `store_search: transport ready` is present while `store_catalogue: fetching`, `store_fetch: served '`, `store_fetch: the served page carries`, `store_library: indexing`, `store_download: start` and `store_search: page` are all **absent** (0 matches each), and the boot carries 6 `store_*` lines, all of them the modules' own announcements |
| `Store` | **reached the retail store, then the guest stopped logging** | the main menu drew the store row and the refresh row beside it, `HYTH VERSE CUSTOM` was pressed at its logged bounds, and the log turned to `native_mouse: screen=store_menu_screen` (15:52:45, `Store.log`) - then nothing for the rest of the run: no catalogue fetch, no served record, no OCR-able screen. The same 4-core machine wedges on this emulator under load (the ringbuffer drop this record already lists, and the title's own "Loading Cached Content List" minutes); the probe boots that reached the same store menu did so with `--no-mouse_ui_nav` and no capture activity at all. **Not asserted:** the Recent list, Search, the details page, the download, the install and the return state |
| `Install` | **not run** | it asserts the *song* the Store leg installed, and that leg never got as far as a download |

*(Superseded for the `Store` and `Install` legs the same day: see the close-out record at the end of
this section. The rows above are what this record measured, and they stay here as that record.)*


The readiness the matrix above claims is therefore: the switch (Enable), the silence (Control), the
fact that the retail store opens from the mod's row (Store, up to `store_menu_screen`), and every
other row pinned hermetically by the suites named in it. The end-to-end walk - Recent, Search,
download, install, the song on the title's own list - is **what remains to be measured**, and it is
the one row of the P8 bullet this record does not call done.

`ctest` in `out/build/win-amd64-debug` is **47/47**, `store_search` grown by `TestPerFileDuplicates`
(the per-file policy in the matrix above), and the release preset builds and links `rb_blitz.exe`
with the one-line change above.

**Docs written with the phase.** [`docs/guides/dlc.md`](../../guides/dlc.md) §2.2 - what the store
adds to the install rules (layout preservation per root, the generated name (`rv_<32 hex>.con` then,
`Artist - Song` since 2026-10-23), the
staging exclusion, "the Downloaded mark reads the packages, not a receipt", and the restart the strip
asks for). [`docs/reference/known-issues.md`](../../reference/known-issues.md)'s *Accepted (by
design)* section gains the scoped statement the P8 bullet asks for: the catalogue is supplied
locally, the online services stay out of scope, and the control leg is what measures the switch's
silence. [`docs/engine/diagnostics.md`](../../engine/diagnostics.md) gains the harness and the new
install-line spelling. [`docs/settings/status.md`](../../settings/status.md) had two statements this
plan made stale - "Off makes no request is structural until a worker exists" (the workers exist, and
the control leg measures the silence) and "the matches are not drawn in the store's own list yet"
(P4 drew them) - and both now say what measures them. The plan stays in `plans/in-progress/` with
this record: P8's harness is in place and two legs are green, while the end-to-end walk the phase
owns is still unmeasured. *(Superseded the same day: the walk was measured - see the close-out record
below - and the plan then moved to `plans/finished/`.)*

**Honest limits of this record.** The matrix above says which rows are measured and which are only
pinned hermetically; on top of those, three things the harness does not do: it seeds the search field
(`store_search_seed`) instead of typing 25 key presses, so the **typing** route rests on P4's
2026-10-20 measurements; it runs the two fixture profiles with `LEADERBOARDS`/`ACHIEVEMENTS` hidden,
which is a normal player layout but not the shipped default; and the `Install` leg's boot can spend
minutes on the title's "Loading Cached Content List" after the library changed, which is why its boot
budget is 720 s (a timeout there is the environment, not the store - the log's `dlc:` lines say which).

**P8 close-out record (2026-10-22) — all four legs green on one build, and the two defects the walk
found.** `scripts/acceptance_store.ps1`, release binary, its own profile directories and an empty
scratch DLC root, on `out/store-acceptance/p8-final/`: **69 steps, 0 failures**, legs
`Enable, Store, Install, Control`.

| Leg | Verdict | Its own evidence |
| --- | --- | --- |
| `Enable` | **PASS, every step** | the DLC page reads `RHYTHMVERSE: OFF`, the row is pressed, `rhythmverse = true` is in `settings.toml`, and the page then reads `RHYTHMVERSE: ON` |
| `Store` | **PASS, every step** | heading `RHYTHMVERSE`, front menu `RECENT SEARCH`, the credit line (OCR `R USTOMS` … `M YTHMVERSE`), `99 offer(s)` from the catalogue; the served page opens the retail browser (`CUT THE CORD`, `COMING FOR YOU`, `SWEET SURRENDER`, …); Search → the authored keys → `15 MATCHES FOR "BUSH"` → the same row lists the fifteen offers; A → the details page `DOWNLOAD / EXIT` with no Buy Now, points or currency; the press installs `rv_436f1b68360f1ad9be533437be169410.con` (*the naming rule of that day - installs are `Artist - Song` since 2026-10-23, see §5/P5's revised bullet*) (4,927,488 bytes) into the flat root, one container, empty staging, the receipt row `pending`, and the host's refresh runs and reports `1 package(s)`; the browser it returns to carries no chrome |
| `Install` | **PASS** | the next boot: `dlc: 1 saved-game library package(s) presented as content type 00000002`, the song list draws `YOUR SONGS … BUSH THE PEOPLE THAT WE LOVE`, and the installed title is found on it (read out of the Store leg's install line) |
| `Control` | **PASS, every step** | no store row, no `RHYTHMVERSE` label, no `CUSTOMS` row on the main menu, the Refresh Music Library row dispatches its sentinel, and `store_catalogue: fetching`, `store_fetch: served '`, `the served page carries`, `store_library: indexing`, `store_download: start` and `store_search: page` are all at **0 matches** |

**Two real defects, both found by this run and fixed in `src/store/`:**

1. **The store only worked with the diagnostic switch on.** The panel's readiness word
   (`0x82853D4C`) was written **only while `store_trace` was on** — P3's `sub_82753010` hook forced
   it as a diagnostic. Every boot that had ever drawn a catalogue was a probe boot with
   `--store_trace=1`; the acceptance boot carries no switch, so `request_index_if_needed` returned
   before the request builder, nothing was asked for, and the store sat on LOADING under its own
   heading (`out/store-acceptance/p8-store/Store.log`, 16:37: `native_mouse:
   screen=store_menu_screen` and then no store line for the rest of the run). The write is now tied
   to **the store's own master switch** (`store::Settings()->enabled`): RhythmVerse on takes the
   retail request path, RhythmVerse off is a pure pass-through, and `store_trace` still forces it
   for a diagnostic boot on a profile whose switch is off.
2. **The search seed opened the search surface.** `store_search_seed` filled the field through
   `SearchSetText`, which is the *typing* path and sets `g_open`, so a seeded boot drew
   `Search: bush` over the store's front menu and the browser and P7's credit line never appeared.
   `SearchSeedField` fills the field without opening the surface; `store_search`'s `TestSeedField`
   pins both sides (typing still opens it, the seed does not).

**Harness fixes this walk forced** — each one a measured false negative, not a preference:

- the store row is OCR'd at least three ways (`HYTH VERSE CUSTOM`, `RHYTHMVERSE CUSTOM`, `RHY
  HMVERSECUST M`), so the row check asks for `CUST`; the **header** draws the word in full
  (`RHYTHMVERSE`), which is what the heading check asks for;
- the credit line is outranked by the loading sentence while the catalogue holds no offer, so it is
  read once the first page has landed, not on the entry capture;
- `store_fetch: the served page carries N offer(s)` is **trace-only**; the shipping line is
  `store_fetch: served '<path>'`, which is what the leg asserts — waiting on the trace line cost a
  300 s timeout;
- the Install leg's Play press is retried, with a keyboard accept after it: the generated payload
  patches the menu in the same millisecond as the first click (`menu_filter: patched a file …`, the
  panel reporting `available=false`), and a click into that window is dropped — measured as six
  minutes on the main menu;
- the Install leg reads its title out of `Store.log` itself (the fallback lived in the Store leg, so
  `-Legs Install -Reuse` could never find it);
- `Enter-MainMenu` uses the leg's own boot budget (40 accepts) instead of a fixed 300 s, because the
  boot that follows a library change runs the discovery pass first;
- the Install leg asserts `dlc: N saved-game library package(s) presented as content type`; the
  Downloaded matcher's line belongs to the Store leg, since the index is built only when the store
  asks for a page;
- the control leg asserts three row spellings (`HYTH VERSE`, `RHYTHMVERSE`, `CUSTOMS`), because a
  single negative check on a fragment can miss a drawn row.

**Environment, stated plainly.** This 4-core machine drops the GPU ringbuffer
(`PRIMARY RINGBUFFER … still failing after 8 resync(s); dropping the rest of the ring`) on roughly
one boot in three; four runs of this record died that way (the preserved attempts `-12`, `-13` and
two `Store`/`Install` retries) and each was retried, never counted as a store result. The abandoned
runs stay beside the evidence.

**Tests**: `ctest` in `out/build/win-amd64-debug` is **47/47** (`store_search` grown by
`TestSeedField`, **402 checks**), and the release preset builds and links `rb_blitz.exe`.

**P8's `Exit:` line is met**: the harness enables the switch through the UI, walks Recent, Search,
the details page and a download, asserts the install on disk, in the receipt and in the log, and
finds the song on the title's own list on the next boot — with the control leg measuring the
switch's silence in the same run.

---

## 6. Open questions & risks

- **OQ1 — metadata schema** (Option A's crux): exact file set + field names the native parser
  expects. Mitigation: P2 instrumentation; DTA usage inventory; fallback Option B.
- **OQ2 — `local` path mapping** and whether `default_server local` is even needed (vs a direct
  fetch hook).
- **OQ3 — commerce session**: can it be satisfied by file data alone, or does it need a stub? If a
  stub, which methods (see §2.7)?
- **OQ4 — part-token spelling** in offer records (`drum` vs `drums`) and the numeric meaning of
  `rank` per part (`song_mgr rank_tier` input domain). Use API `rank_*` when valid, not `diff_*`
  tiers as raw ranks; establish a fallback for null ranks.
- **OQ5 — loaded-library metadata seam**: locate guest song title/artist/DLC accessors and the
  discovery-generation boundary, or finish the package-reader fallback with complete accepted
  source coverage. Required in v1; manifest-only matching is ruled out. No guest ABI/address is
  established by this plan for that accessor.
- **OQ6 — host refresh plus guest rediscovery**: extend flat-only/cache-gated refresh as needed,
  update structured source snapshots, and identify a safe guest discovery transition. Returning
  to the main menu may be necessary. Existing Refresh Music Library does not prove the second
  half; explicit restart is the fallback until both are measured.
- **OQ7 — duplicates (decision)**: keep per-file offers. Artist/title matching deliberately marks
  alternate charts of the same song together; Download Again remains explicit, and install names
  stay per-file. No fuzzy matching or automatic deduplication by title.
- **OQ8 — art format**: what `load_art` accepts and how it caches; PNG vs platform texture.
- **OQ9 — link/archive policy (decision)**: only RhythmVerse-hosted HTTPS direct links for
  `rb3xbox`; validate every redirect. Skip ZIP/RAR/7z and other archives, including those detected
  only in headers/body. Same-site URLs and format tags alone are insufficient. Extraction and
  off-site/browser support are later features, not fallbacks during this implementation.
- **OQ10 — instrument semantics**: verify combined `instrument[]` behavior and the API's absent
  instrument representation. Proposed product rule is On = required, Off = unconstrained;
  all-Off = no instrument constraint. Apply locally if the server uses different semantics.
- **OQ11 — runtime menu population**: prove adding/removing `rbbz_rhythmverse` after a settings
  commit without reloading the whole payload, and inventory/hide-list integration. Preserve the
  existing `splash_dlc` refresh action across transform order and cache On/Off.
- **R1 — faking "online" side effects**: contained by patching DTA branches and namespaced bridge
  reads only; never flip global signin state (would affect Rock Central/leaderboard paths).
- **R2 — RhythmVerse load/rate**: cache + single-flight + backoff + UA; no bulk prefetch;
  `records=25` like the site.
- **R3 — DLC path safety**: respect `paths.toml`/launcher profile as the single source of truth;
  use the boot's resolved path and preserve flat/structured classification. Initialize a missing
  configured root deliberately; refuse unwritable roots with a clear popup. Exclude staging from
  every enumerator and use short, collision-safe install names.
- **R4 — Ultimate payload interplay**: the store records and the `ui/store/remote_metadata/**`
  delivery must merge like everything else (one part, extended in place — the black-screen
  lesson); test with Ultimate installed.
- **R5 — API drift**: the site is PHP/Slim and changed before (`gameformat` param rejected,
  filters route 403s to us); isolate parsing in P0 tests so drift is a one-file fix.
- **R6 — naming/attribution**: show RhythmVerse as the source in the store UI; do not ship retail
  bytes; the proposed master default is Off. Attribution and free-download UI are P1/P3 acceptance
  requirements, not optional P7 polish.
- **R7 — transport reuse**: launcher redirect-following would violate the direct-link policy;
  the existing settings bridge cannot return strings or carry unrestricted UTF-8 search tokens.
  Implement the narrower HTTP policy and an explicit store protocol, with hermetic tests.

## 7. Non-goals (v1)

No Xbox LIVE/Rock Central restoration, no purchases/entitlement checks, no preview audio
(measured 2026-10-20: both preview fields are YouTube web pages, not assets — §3.3; the retail preview
UI is hidden by omitting `preview_audio`), no retail categories/grouping,
no PS3 paths, no account features, no writes into the retail game data, no songcache write-back,
no browser integration, **no archive extraction**, and **no off-site download links** (including
redirect targets). Recognizing manually installed loaded DLC is a v1 requirement, not a non-goal.

## 8. Evidence index

| File                                                                                    | What it is                                                                                                                                                                                                                                                                                                                                                                                                        |
| --------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `out/_swap/store.tree.txt`                                                            | decoded `ui/store/gen/store.dtb` (BandStorePanel, browser, previews, menus)                                                                                                                                                                                                                                                                                                                                     |
| `out/_swap/store_info.tree.txt`                                                       | decoded `ui/store/gen/store_info.dtb` (buttons, buy/download cases)                                                                                                                                                                                                                                                                                                                                             |
| `out/_swap/config_store.tree.txt`                                                     | decoded `config/gen/store.dtb` (servers incl. `local`, groupings)                                                                                                                                                                                                                                                                                                                                             |
| `out/_swap/splash.tree.txt`                                                           | decoded `ui/splash/gen/splash.dtb` (the gate, verbatim)                                                                                                                                                                                                                                                                                                                                                         |
| `out/_swap/ui.tree.txt`                                                               | decoded `ui/gen/ui.dtb` (store panels preload, netcache init)                                                                                                                                                                                                                                                                                                                                                   |
| `out/_swap/server_connect.tree.txt`                                                   | decoded `ui/net/gen/server_connect.dtb` (discovery/offline states)                                                                                                                                                                                                                                                                                                                                              |
| `out/_swap/mega_ls.txt`                                                               | STFS listing of the 803 MB RB3 DLC container (song layout reference)                                                                                                                                                                                                                                                                                                                                              |
| `out/_swap/dl_headers.txt`, `dl_probe.bin`                                          | download probe: 200 OK, octet-stream, `CON ` magic, size match                                                                                                                                                                                                                                                                                                                                                  |
| `docs/rythmverse-responses/list.json`, `search.json`                                | owner captures (fixtures for P0)                                                                                                                                                                                                                                                                                                                                                                                  |
| `out/_swap/rv_review_20261014.body`, `rv_review_20261014.headers.txt`               | successful 2026-10-14 scoped list request (`records=1`, no sort/instrument fields); `rb3xbox` result with external Dropbox URL, opaque dotted ID, zero size and null ranks                                                                                                                                                                                                                                    |
| `out/_swap/rv_review_20261014_recent.json`, `rv_review_20261014_recent.headers.txt` | successful scoped Recent request, default four instruments, update-date descending, 25 records; all `rb3xbox`, 23 root-relative direct-file links                                                                                                                                                                                                                                                               |
| `out/_swap/rvp_list_rb3xbox.json`                                                     | failed earlier probe: HTML 404, **not** a JSON success fixture                                                                                                                                                                                                                                                                                                                                              |
| `out/_swap/rv_songfile.html`                                                          | 2026-10-20 song-page probe (HTTP 200, 171,707 bytes): the only preview is a YouTube `<iframe>`; no audio asset (§3.3)                                                                                                                                                                                                                                                                                          |
| `out/_swap/rv_p0_encoded.json`                                                        | 2026-10-20 list probe with percent-encoded form keys: HTTP 200, `status=success`, 1 `rb3xbox` record                                                                                                                                                                                                                                                                                                          |
| `out/_swap/song_select.tree.txt`                                                      | decoded `ui/song_select/gen/song_select.dtb` (2026-10-14): the song list's tab cases (`kAction_PageUp`/`PageDown` at `:1659`/`:1627`, gated on `rock_central`/`user_mgr`) and its own keyboard search (`kAction_Start` at `:1753` → `virtual_keyboard show_keyboard … localize search_for_song`) - cited by §9/P9-9 and §9/P9-11 |
| `out/_swap/locale.tree.txt`                                                           | decoded locale table (2026-10-13): `inline_help_options` = `HELP & OPTIONS` (`:732`), `store_offer` = `VIEW ITEM` (`:2169`) - cited by §9/P9-7 and §9/P9-8                                                                                                                                                                                                                                                    |
| §2.11 code-reference table                                                             | current menu ownership, settings schema/panels, bridge limitations, SDK package reader, HTTP redirects, filename limits and refresh gaps checked on 2026-10-14                                                                                                                                                                                                                                                    |
| image string offsets                                                                    | `BandStorePanel` 0x3A078; `is_store_offer` 0x3D308; `has_created_commerce_session` 0x87B00; `load_art` 0x87BC0; `netcache_init` 0x8E6EC; `get_store_offer` 0x105248; `set_target_store_offer_id` 0x105270; `dlc_store` 0x102D00; `/dlc_top_%s_%s.dta` 0x102D58; `dlc_store/%s/%s/offers/%s/` 0x105088; `dlc_store/%s/%s/related/%s.dta` 0x104740; `Xis_participating_user_online` 0x13FBB |
| imports (checkout)                                                                      | `__imp__XamShowMarketplaceUI` 0x827F6544, `…NuiMarketplaceUI` 0x827F65B4, `…MarketplaceDownloadItemsUI` 0x827F6774                                                                                                                                                                                                                                                                                        |

## 9. Owner pass — reopened 2026-10-22 (P9 prompts)

The store shipped, the owner drove it, and asked for eleven adjustments. **One prompt each**, grouped
by the surface it touches, each names the seam it edits and the measurement that closes it.
**P9-6** was split into three the same day, redesigned from scratch on 2026-10-23 as **one modal
download popup**, and **delivered as one prompt on 2026-10-24** - the three follow-up states it was
briefly split into (a live progress line, the toast's A-refresh, a credit-line removal) were
**withdrawn by the owner that day**, the A-refresh returning as **P9-14**.
**A twelfth adjustment (P9-12) followed on 2026-10-22** — two row renames and the refresh entry's
default — and is recorded below with its prompt. **A thirteenth (P9-13) followed the same day**: the
details page's box that carries the Download / Cancel buttons must carry the **custom's own** credit
block — the charter, the custom's release date, its audio type and its download count — and it is
recorded beside P9-12 with its prompt. The owner corrected the first reading the same day: the
values are the **file's**, not the song's, because one title can have several customs with different
authors and dates. **A fourteenth (P9-14) followed on 2026-10-24**: the download toast must offer an
**A Refresh** beside **B Back**, and that refresh must list the new song **without a restart**.
Where a prompt overrides a P0-P8 decision it says so — the records in §5 are not rewritten, they are
the evidence of what shipped. Prompts are ordered so a later one does not have to undo an earlier
one.

| # | Status | Surface | The adjustment | Overrides |
| --- | --- | --- | --- | --- |
| P9-1 | Done | Settings | The master switch defaults **On** | the P6 record's "master off" |
| P9-2 | Done | Settings | A master change stays **hot** only if the menu row **and** the header follow it live; otherwise a restart is asked for | P6 (all six rows `kHot`) |
| P9-3 | Done | Settings | `Downloadable Content ▸ RhythmVerse` submenu first; the filter is renamed **Search Song Filter** (renamed again to **Filters**, page `RHYTHMVERSE FILTERS`, by P9-11) | P6's flat page |
| P9-4 | Done | Settings | Independent `Show Messages` and `Show Recommendations` under Interface ▸ Music Library & Store | — |
| P9-5 | Done | Store menu | **Recently Added**, and no count beside the two rows | P3/P7's labels |
| P9-6 | Done | Store surface | A download press opens one modal *Downloading from RhythmVerse* popup (the search panel's shape); **B** cancels, and the same panel switches to its outcome sentence — *Downloaded '<song>'.* / **Press B to Close**. The 6b/6c/6d follow-ups were withdrawn 2026-10-24 (the toast's refresh is P9-14) | P5's overlay strip, the 2026-10-22 P9-6 draft |
| P9-7 | Done | Store footer | **Start = Search Filters**, and Back returns to the store | P1/P3's Start route |
| P9-8 | Done | Store footer | Hide **Y View Item** where it does nothing | — |
| P9-9 | Done | Your Songs | The LT/RT tabs navigate from the song list; the RhythmVerse tab follows the master | — |
| P9-10 | Done | Catalogue | Warm Recently Added at boot; a failed press draws the retail connection-failed popup and retries in the background — nothing is hidden | P7's fetch-on-open |
| P9-11 | Done | Search | Use the title's own keyboard search; retire the authored key pages | P4 |
| P9-12 | Done | Main menu + Settings | The store row is renamed **RhythmVerse** (was *RhythmVerse Customs*), the refresh row is renamed **Refresh Songs** (was *Refresh Music Library*), and the refresh entry defaults **On** again | the 2026-10-20 default-off and the P1 label |
| P9-13 | Done | Store details page | The box that carries the **Download / Cancel** buttons carries the **custom's** credit block: who charted it, the custom's release date, its audio type and its download count (not the song's year) | — |
| P9-14 | In Progress | Store download toast | The downloaded toast carries **A Refresh** beside **B Back**: A opens the refresh/discovery page and runs the refresh, and the new song is listed **without a restart** | P9-6's outcome state (one sentence, **B** only) |

Status checked 2026-10-23 against this section's records and the tree: **Done** = the prompt's
Done-when is met by a record; **In Progress** = part is landed and the closing measurement is owed;
**Todo** = nothing is in the tree. Evidence for the non-Done rows: P9-5
landed 2026-10-23 (the label, the four row forms and the `store_front_menu_row` diagnostic) and the
owner's boot reads `RECENTLY ADDED / SEARCH` with no digit beside either row; **P9-9 is Done
(owner's boot, 2026-10-23)** — the split-controls record's ordered tab ring works on the rebuilt
binary, closing the walk the prompt's Done-when names; **P9-6 was redesigned 2026-10-23** and none
of it is in the tree yet (`src/ui/help_overlay.cpp:383` still draws `DownloadStatusText`,
`src/store/store_catalogue_status.cpp:118` still returns `kStoreCreditText`, and the only modal
popup is P9-11's search panel — there is no download popup and no `refresh-songs` command);
**P9-7 and P9-8 are
Done (owner's boot, 2026-10-23)**: `store::RouteStoreStartToFilter`, `SetStoreFooterStart`,
`ClearStoreFooterOption` and `ClearDetailsFooterOption` (`src/store/store_payload.{h,cpp}`, wired
in `src/settings/menu_payload.cpp` with the `inline_help_search_filters` locale entry;
`store_payload` **376 checks**) re-point both store Start cases at the Search Song Filter page and
clear the Y caption on all three store surfaces, and the owner's retest on the release build read
the store root's footer correct after the panel-enter correction; the acceptance harness's
Store-leg steps (footer `FILTERS`, Start → the filter page, Back → the store menu, no
`VIEW ITEM` on any store surface) stay as the standing check; **P9-10 is Done (measured
2026-10-23)** — the boot prewarm (2026-10-22), then items 2–3 (popup, press-asked retry, recovery)
measured in one boot against a loopback stub via the new `store_api_base` diagnostic: two refusing
presses each drew the retail popup and logged `(a press asked)`, the third after the stub was
flipped served four pages and the Recent list opened with its offers
(`out/_swap/p910-fail/run3/20261023-151158`); **P9-11 is Done (owner's boot, 2026-10-23)**: the
title's keyboard route, the modal search popup, the answer listing, a second search replacing the
first and the per-search art were confirmed in game, closing the prompt's Done-when;
labels and the refresh entry's compiled-On default were confirmed on the rebuilt binary; **P9-13's
model, writers and payload edit landed 2026-10-23** (the record under the prompt below) and the boot
that OCRs the block is the one measurement it still owed.

Status checked 2026-10-24, against this section's records and the tree: **P9-4, P9-6 and P9-13 are
Done** (each closed by the owner's own in-game check on 2026-10-24 - see the close-outs under their
prompts; P9-4's reopened Search-overlap item included), and **P9-14 is In Progress**: the toast's
**A Refresh** is in the tree (`finish_installed` sets an `installed` property beside `terminal`,
`BUTTON_DOWN_MSG`'s `kAction_Confirm` case runs the menu row's own three statements, and the prompt
reads **A Refresh   B Close**), its suites are green and the acceptance harness asserts the
same-boot walk; the boot that reads the song on Your Songs after **A** is the measurement it still
owes - see its record under the prompt.

### P9-12 — two row renames, and the refresh entry on by default (2026-10-22)

- **Do**: two owner-visible renames and one default flip. The main-menu store row's label becomes
  `RhythmVerse` (`src/ui/settings_screen.h`'s `kRhythmVerseLabel`) and the refresh row's label
  becomes `Refresh Songs` (`src/ui/menu_options.h`'s `kDlcRowRefreshLabel`, which the main-menu
  locale rewrite, the payload's own records and the Interface → Main Menu mirror all read). The
  Downloadable Content page's own row is renamed with it (`src/ui/settings_screen.cpp`), and
  `general.refresh_music_library` compiles `BoolValue(true)` in `src/settings/schema.cpp` with its
  cvar carrier (`src/enhancements.cpp`) flipped to match.
- **Do not** rename the setting id or the cvar: `general.refresh_music_library` and
  `enhancements_refresh_music_library` are what a profile and a command line already carry, so a
  rename would silently drop a saved Off.
- **Move with it**: the store acceptance harness's OCR assertions (`scripts/acceptance_store.ps1`
  read `CUST` and `MUSIC LIBR` on the main menu, and `REFRESH MUSIC` on the DLC page) and the
  observation harness's `main-menu` needle (`scripts/ui_states.ps1`), which now reads
  `REFRESH SONGS` on a default boot.
- **Done when**: the suites are green, the DLC page draws `Refresh Songs: On` on a profile that never
  named the key, and a boot's main menu reads `RHYTHMVERSE` / `REFRESH SONGS`. The label halves are
  the harness's Enable/Store legs (`scripts/acceptance_store.ps1`), **not yet measured** on a build
  that carries the rename. *(Superseded by the 2026-10-23 record below: the harness now reads the
  renamed rows and proves the default from the profile's silence; only the boot OCR is open.)*

**P9-12 record (2026-10-23).** The labels and the default were already in the tree; this pass
finished the halves the prompt still owed and closed the owner-visible gap that came with them.

- **The harness proves the default instead of naming it.** `scripts/acceptance_store.ps1`'s
  `Write-StoreSettings` no longer writes `refresh_music_library = true`, so both fixture profiles
  start from the compiled default (On) exactly as the store switch does (P9-1). The Enable leg now
  asserts `HYTH` (the fragment every measured OCR reading of the shortened `RhythmVerse` label
  keeps) and `REFRESH` on the main menu, `SONGS` on the Downloadable Content page, and that the
  boot did **not** commit `refresh_music_library` into the profile - the same read-not-write check
  the store master already had. The old `CUST` needle is gone (the label has not contained it since
  the 2026-10-22 rename). `Store` and `Control` keep their own `REFRESH` checks; `scripts/ui_states.ps1`
  no longer claims the refresh-entry-off boot reads `DOWNLOAD CONTENT` (that boot has no such row).
- **The Interface → Main Menu mirror now agrees with the menu** (owner report, 2026-10-23: the page
  said `Refresh Songs: Visible` while the button was absent). With `general.refresh_music_library`
  off the menu does not carry `splash_dlc`, so `MenuConfiguration::refresh_row_on` (frozen at boot
  from the cvar in `src/hooks/settings_payload.cpp`) makes `src/ui/settings_screen.cpp` drop that
  row from the live list *and* from the static fallback. The switch stays where it lives, on
  Downloadable Content. Pinned by `settings_screen` (`TestDownloadContentVisibilityAndNoDlc`: two
  mirror rows with the entry on, one with it off, and the restored row's own switch still offered
  while the entry is off).
- **`Download content`, not `DOWNLOAD CONTENT`** (owner request, 2026-10-23, the same pass): the
  restored row's own locale entry (`kDownloadContentLabel`), its two mirror labels and the
  `interface.hide_download_content` schema label are sentence case, matching the mirror's
  locale-derived rows (`Leaderboards`, `Refresh Songs`) that the boot OCRs. The shipped
  `Download Content` text stays only in `menu_options::kDlcRowLabel`, which is the string the
  refresh locale rewrite searches for - changing it would silently stop the relabel.
- **Verification**: `ctest` in `out/build/win-amd64-debug` is **47/47**; the release preset builds and
  links `rb_blitz.exe` (2026-10-23 11:57). The owner's own profile carried
  `general.refresh_music_library = false` with `manual` provenance, which is why their menu had no
  refresh row at all; with their approval that stored value and its provenance row were removed on
  2026-10-23, so the compiled default applies on the next boot.
- **Not measured at this record's time**: the boot OCR itself. The owner chose to check it
themselves on the rebuilt binary (the recipe: main menu `RHYTHMVERSE` + `REFRESH SONGS`; Help &
Options → General → Downloadable Content reads `Refresh Songs: On`; Interface → Main Menu reads
`Download content: Hidden` and `Refresh Songs: Visible`). *(Superseded by the close-out below.)*
- **Close-out (owner's boot, 2026-10-23) — P9-12 is met.** The owner confirmed the two renamed rows
and the refresh entry's compiled-On default on the rebuilt binary; read by hand, no capture stored.
**P9-12 → Done.**

### P9-13 — the box that carries Download / Cancel carries the custom's own credit block (2026-10-22)

- **Do**: draw the **custom's** facts, not the song's year, in the details page's own box —
  `store_info_panel` of `ui/store/gen/store_info.dtb`, the left panel whose `buttons.lst` carries
  Download / Cancel / Download Again and Exit. The box today carries the name alone (`song.lbl` via
  `set_offer_name`, `out/_swap/store_info.tree.txt:338`; the P3 details capture
  `out/_swap/store-probe/20261021-190304/offer-late.png` OCRs `Building a Mystery` and nothing else
  above the buttons), with the *song's* artist in the right-hand `store_details_panel` band. The
  owner's correction (2026-10-22) is the point of the prompt: a title can have several customs with
  different authors and release dates, so the page must say which one this is. The layout asked
  for, in this order (bold/italic/spacing only if the label supports it; two facts on one line is
  fine when space demands it):

  ```text
  Song Title          (the name, bold if the label permits)
  Song Artist
  Audio type          (nothing at all when the catalogue has none)
  Charted by <name>
  Released at <date>
  <N> Downloads
  ```

**The fields, measured on the committed captures** (`docs/rythmverse-responses/list.json`,
`search.json`; each record's `file` object, all of them present on all 33 captured records unless
noted):

| Line | Field | Measured, 2026-10-22 |
| --- | --- | --- |
| Audio type | `file.audio_type` | a **code**: `single` (19 of the list capture's 25), `full` (4), `partial` (2). The site's own dropdown labels four options **Multitrack / Karaoke / Partial Multitracks / Single Track** (owner's capture); the `karaoke` code is **not** in either capture, so the code→label table is a reading to confirm against the site, an unknown code draws **nothing** and logs once, and the raw token is never drawn. |
| Charted by | `file.charter`, else `file.author.name`, else `file.author_id` | `charter` is **null on every captured record**, `author.name` is the display name (`Virtual-Man`) and `author_id` the shortname (`anonymous-family`). `file.credits.instrument.<part>[]` names per-instrument charters - a richer list than one line, left out of v1 with this note. |
| Released at | `file.release_date`, else `upload_date` | `2026-10-11 19:07:55` on the capture (equal to `upload_date`/`record_created`/`giorno` there); the line shows the date part only (`2026-10-11`). |
| Downloads | `file.downloads` | `61` on the list capture and `84` on the search capture. **`data.downloads` is a different number** (`0` on the same record - the song's, not this file's) and `file.scaricamenti` is a legacy counter that stays `0`; the line reads the file's own count, a snapshot from the catalogue fetch (5-minute TTL since P7), never a live figure. |
| Song Artist | `file.file_artist` | already served as the offer's `artist`. |
| Song Title | `file.file_title`, else `file_name` | already served as the offer's `name`. |

- **The model grows four fields**: `SongFile` today parses `downloads` (`file.downloads` preferred
  over `data.downloads`, `src/store/rhythmverse_client.cpp:406`) and `update_date`, but **not**
  `audio_type`, `author`/`charter` or `release_date`; `Offer`
  (`src/store/store_metadata.h`) carries none of the four. Both are extended, and **both writers
  write all four keys on every offer** (`store_metadata.cpp`'s compiled writer at `:303-306` and
  its text writer at `:395-398`): a `get_data` on a missing key walks a null (the trap P3 measured
  on `(rank <part>)`), so an empty value is written as an empty string, never omitted. The audio
  type is stored as its **code** and rendered through one `AudioTypeLabel(code)` table in the
  SDK-free module, so the mapping is tested once rather than spelled at the write site.
- **Where the lines go (one boot measures the scene).** The box's milo has no spare label
  (`store_info.milo_xbox`: `song.lbl`, `drm.lbl`, `cost.lbl`/`cost_ps3.lbl`,
  `rating.lbl`/`rating_description.lbl`, `recommended*.lbl`, `wait.lbl` — `python
  scripts/hmx_milo.py list assets/game/360/ui/store/gen/store_info.milo_xbox`), and two rows are
  already ours: `song.lbl` (the name) and `drm.lbl` (the description `refresh_panel` writes,
  `store_info.tree.txt:418-419`). The labels that
  exist but are hidden today — `rating.lbl`/`rating_description.lbl` (inside `rating.grp`, hidden by
  `refresh_panel` since P3 writes no `rating`) and `cost.lbl`/`cost_ps3.lbl` (hidden by the
  payload's price-chrome removal) — are the candidates for the remaining rows, and one boot that
  writes sentinel text into each says which is usable and where it sits. In this order:
  1. **each fact on its own row** (the owner's layout), by showing the hidden label(s) **after**
     `refresh_panel`'s own hides and writing the text — guest-drawn retail furniture, no new milo;
  2. **two facts per row** when the scene has fewer usable rows — `Artist · Multitrack` and
     `Charted by X · Released 2026-10-11 · 61 Downloads` — keeping the owner's order and dropping
     only the audio type when it is empty;
  3. **one composed block in `drm.lbl`** only if that label renders line breaks (unproven; a boot
     decides).
  Bold/italic are the label's own (a text tag or its material): if the boot shows neither, the
  title stays plain, which the owner permitted.
- **Do not**: draw a host-side line (owner rule), add a milo (no writer), write the raw
  `audio_type` code, or invent a line for an empty audio type. The song's `year_released` stays on
  the record (the details band reads it) but is **not** what this box shows.
- **Order**: independent of P9-6 since its 2026-10-23 redesign (the download status lives in its
  own popup and leaves `drm.lbl` alone); the writes go into `refresh_panel` beside
  `set_offer_description` (the seam P3's transforms own).
- **Move with it**: `src/store/rhythmverse_client.{h,cpp}` (the three new fields and their captured
  values), `src/store/store_metadata.{h,cpp}` (the four `Offer` fields, `OfferFromSong`, both
  writers, the `AudioTypeLabel` table), `src/store/store_payload.{h,cpp}` (the block's writes;
  idempotent, refusing by name before an edit), `tests/store_client_tests.cpp` (the parse against
  the captured records: `audio_type`, `author.name`, `release_date`, `downloads`, and that
  `data.downloads` never wins over `file.downloads`), `tests/store_metadata_tests.cpp` (both
  writers carry the four keys on every offer; the label table's four codes and its unknown-code
  fallback), `tests/store_payload_tests.cpp` (the writes and the empty-field fallbacks, including
  the empty audio type drawing nothing), the `store-edit <store_info.dtb> info` diagnostic dump,
  and `scripts/acceptance_store.ps1`'s details-page step, which today asserts `DOWNLOAD` / `EXIT`
  only (`out/store-acceptance/p8-final/`).
- **Done when**: a boot opens a served offer's details page and its box OCRs the block on the
  captured fixture — `Forever Young`, `Youth Group`, the audio type's label (`Single Track` for
  that record's `single`), `Charted by Virtual-Man`, `Released at 2026-10-11` and `61 Downloads` —
  while pressing the download row still opens P9-6's popup, an offer with no audio type draws no
  line for it, and no caption-strip line carries any of it.

**P9-13 record (2026-10-23) — the model, both writers and the payload edit landed; the boot that
OCRs the block is owed.** Everything the prompt moves with is in the tree and pinned hermetically;
the prompt's own Done-when is a boot, and on this machine that is the owner's to take.

- **The catalogue model** (`src/store/rhythmverse_client.{h,cpp}`). `SongFile` gained `audio_type`
  (the raw **code**), `charter` and `release_date`, and the parse follows the prompt's order:
  `charter` first, then `author.name`, then `author_id`; `release_date`, then `upload_date`. A new
  `DatePart` reduces a timestamp to its day (`2026-10-11 19:07:55` → `2026-10-11`), passes a
  non-timestamp through unchanged rather than blanking it, and turns the site's zero date
  (`0000-00-00 00:00:00`) into empty. `downloads` stays the **file's** count: the committed record
  carries `file.downloads = 61` beside `data.downloads = 0`, and `store_client` pins 61.
- **Measured on the committed captures** (`docs/rythmverse-responses/{list,search}.json`, both
  re-read 2026-10-23): `file.audio_type` is `single` on 25 records, `partial` on 3 and `full` on 5;
  `karaoke` appears in neither; `file.charter` is **null on every record**, so the resolved charter
  is `author.name` (`Virtual-Man` on the record the Done-when names); `release_date` equals
  `upload_date`/`record_created` and the line shows `2026-10-11`. `data.audio_type` does not exist,
  which is why the field is read from `file` only.
- **The model and the table** (`src/store/store_metadata.{h,cpp}`). `Offer` carries the four facts,
  and `AudioTypeLabel` is the one code → label map (Multitrack / Karaoke / Partial Multitracks /
  Single Track; an unknown code is **empty**, so the raw token can never be drawn). Both writers
  emit `audio_type` (already the label), `charter`, `rbbz_release_date` and `downloads` on **every**
  offer, empty values included - a `get_data` on a missing key walks a null, the trap P3 measured on
  `(rank <part>)`. An unknown code is named once per boot by the catalogue worker and once by the
  search's page source (`store_catalogue` / `store_search` warn), because the writer is SDK-free and
  cannot log.
- **The payload edit** (`src/store/store_payload.{h,cpp}`, `store::ShowOfferCredits`). The details
  box has six `UILabel`s and no spare one (`store_info.milo_xbox`), so the block reuses the labels
  retail's own `refresh_panel` drives and hides for a data-only offer, in the owner's order:
  `song.lbl` stays the title (retail's `set_offer_name`); `drm.lbl` draws **Song Artist**;
  `rating.lbl` draws the **audio type** (the group is shown again, the label hidden when the
  catalogue has none); `rating_description.lbl` draws `Charted by <charter>` (hidden when nobody is
  named); `cost.lbl` - the scene's last free row - draws `Released at <date>, <N> Downloads`, the
  count alone without a date, and nothing when neither exists. One `{do ...}` block is appended to
  the method (retail's own body shape, with the compiled record's array binding for `$offer`), so
  it runs **after** `refresh_panel`'s hides and after `HideStoreInfoPriceChrome`'s - which is why it
  is wired after that transform in `src/settings/menu_payload.cpp`. The DTA tests are the DTA's own
  string truthiness (`{if <get_data value> ...}`, the shape `refresh_list` uses for its sort name),
  and every value is composed from the record's keys - nothing is invented.
- **No host drawing, no new milo**: the lines are retail's own labels and the guest's text setter
  (`set_display_text_string`, the message the label's name table carries beside `set_offer_name`).
- **Checked against the shipped record before any boot**:
  `rb_blitz_store_payload_tests store-edit assets\game\360\ui\store\gen\store_info.dtb info` answers
  `credit block -> edited` and dumps the transformed `refresh_panel` - the block sits after the
  shipped `do`, with the binding, the three writes and their guards, exactly as the suite pins
  (evidence kept as `out/_swap/p9-13-info2.txt`).
- **Suites**: `store_client` **590** (the fixture's four facts, `DatePart`, the precedence of
  `charter`/author and `file.downloads`/`data.downloads`), `store_metadata` **144** (`OfferFromSong`
  carries the four, both writers emit them empty-included, the label table's four codes and its
  unknown fallback), `store_payload` **460** (`TestOfferCredits`: the block's statements, the
  binding, idempotency, the compile/parse round trip and both refusals), full `ctest` **47/47** in
  `out/build/win-amd64-debug`; the release preset builds and links `rb_blitz.exe`.
- **The harness**: `scripts/acceptance_store.ps1`'s Store leg now asserts `CHARTED` and
  `RELEASED`/`DOWNLOADS` on the details capture (the artist and figures vary with the live
  catalogue, so the steps assert the block's own words). Not re-run on this build.
- **Owed (the prompt's Done-when)**: the boot that opens a served offer's details page and OCRs the
  block. On this machine that is the owner's to take (`AGENTS.md`), and it is the one measurement
  that decides two things this record cannot: whether the re-used labels read in the owner's order,
  and whether `karaoke` really is the site's fourth code. The Done-when's "pressing the download row
  still opens P9-6's popup" clause is P9-6's, which landed on 2026-10-24 - until then the press
  started the transfer as P5 shipped it. **P9-13 stayed In Progress** until its own boot, which the
  close-out below records.

**P9-13 layout correction (2026-10-24).** The owner's capture shows the retail label positions
are unsuitable: both rating labels overlap above the box, the release/count label overlaps the
title, and the artist sits far below it. `ShowOfferCredits` now resets `rating.grp`'s translation
and explicitly places the six existing labels in a left-aligned stack inside the box. The title
keeps its bold font at size 24; the other rows use size 18, a 28-unit pitch, bounded width and
ellipsis. Release date and downloads have separate rows (`cost.lbl` and `cost_ps3.lbl`). Missing
optional values hide their labels without advancing the row cursor; the action list follows the
last visible fact and aligns with the text. No scene asset or host text overlay is added.

Validation: `cmake --build --preset win-amd64-release` succeeds; `store_payload` and `menu_payload`
pass in the debug build. The real `store_info.dtb` passes the `store-edit ... info` diagnostic
(`out/_swap/p9-13-layout-info.txt`), including the new layout commands. This validates payload
construction, not rendering. The owner chose to perform the visual check themselves; P9-13
remains In Progress pending that check.

**P9-13 crash correction (2026-10-24).** The owner's first layout test crashed opening details;
`logs/rb_blitz_301.log` ends with a read of guest `0x00000008`. The new property writes used
RndText's `align` and `(styles 0 size)` on UILabel objects. The shipped UILabel schema instead
exposes `alignment` and `(styles 0 text_size)`; retail's `song_select.dtb` uses the latter path.
The native UILabel handlers are `sub_824BD570` / `sub_824BBC28`, rather than the lower-level text
handlers the initial layout change referenced. The payload now writes the UILabel names, and its
regression checks require them and reject the RndText spellings. The original structural tests
checked the wrong names and did not execute guest property handlers. Runtime confirmation remains
with the owner, who chose manual testing.

**P9-13 visual refinement (2026-10-24).** The owner confirmed the corrected property names work
and supplied a rendered capture, then requested better visuals with both buttons on the right.
The layout now adds padding inside the frame, retains the bold size-24 title and orange size-18
artist, and groups the remaining facts below them at size 14 with a 22-unit pitch. The title and
artist widths are 480 units; metadata is 440, with ellipsis retained. Download / Exit return to
the scene's original lower-right action area, fixed independently of missing metadata. This
refinement uses the same confirmed UILabel properties; its visual check remains with the owner.

**P9-13 close-out (2026-10-24; owner's own boots) — P9-13 is Done.** The owner read the refined
block in game on the rebuilt binary and reports the box correct: the custom's own facts in the
owner's order (title, artist, audio type, `Charted by`, release date, downloads) with both actions
in the scene's lower-right area, which is the prompt's Done-when. Read by hand, no capture stored -
the same standing as P9-5's, P9-7's and P9-9's owner boots. What stays as the standing check:
`store_client` (the four facts on the captured records and `DatePart`), `store_metadata` (both
writers emit the four keys on every offer, the audio-type label table and its unknown fallback),
`store_payload`'s `TestOfferCredits` and the `store-edit <store_info.dtb> info` dump, and
`scripts/acceptance_store.ps1`'s Store leg (`CHARTED`, `RELEASED`, `DOWNLOADS`). P9-13 leaves one
downstream task, not its own: P9-6's popup opens on the same box's Download press, and the row
pressed there is unchanged by this block.

### P9-14 — the download toast offers **A Refresh**, and the new song appears without a restart (2026-10-24)

- **Do**: the popup P9-6 delivered already has its outcome state - `error.lbl` reads
  **Downloaded '<song>'.** and `continue.lbl` reads **Press B to Close**, and `BUTTON_DOWN_MSG`
  consumes every action but `kAction_Cancel`. This prompt gives that state a second action:
  **A Refresh**. The prompt line becomes **A Refresh   B Close** (a new locale entry beside
  `rbbz_download_close`), and **A** (`kAction_Confirm`) - offered **only while the state is
  `installed`**, so a cancelled or failed job keeps B alone - does what the main menu's **Refresh
  Songs** row does, in that row's own words rather than a copy of it:
  1. closes the popup (the details page is left behind, so there is nothing to cancel - `terminal`
     is already set and the popup's `exit` therefore writes no cancel),
  2. `{ui goto_screen server_connect_screen}` - the title's own discovery pass, the page the row
     enters (`menu_options::TapRefreshCache`, `src/ui/menu_options.cpp:867`),
  3. `{file_exists "songcache:/rbbz_dlc_refresh"}` - the sentinel `src/hooks/dlc_refresh.cpp`
     answers on a **guest thread** by re-scanning the configured libraries and publishing the new
     list (`dlc::RefreshConfigured`, with `shell_music::StandDownForRefresh` first). The thread rule
     is why the sentinel, not a host-initiated publish, is the seam: `ContentManager`'s library is
     written from a guest thread only.

  So "the refresh cache page" is the discovery screen the row already uses, and the button reuses
  the row's own two statements - no second refresh path, no host-drawn progress.
- **The hard half — the guest's own discovery.** The install's host-side refresh already runs at
  commit (`dlc::RefreshAfterStoreInstall`, P5's landing record: `1 package(s)`), and the title's
  **next boot** lists the song. What has never been proven is the running title picking it up, which
  is OQ6's open half and the reason P5's strip said *restart* and P9-6's sentence stops at
  *Downloaded*. This prompt makes that measurable and requires it: after **A**, the discovery pass
  runs, and the song list - **same session, no restart** - carries the title. Where the pass alone
  is not enough the prompt's measurement decides the next step (for example a publish at a proven
  menu boundary, or re-entering Your Songs after the pass), but *restart* is not an accepted answer,
  and nothing may hide the failure.
- **Do not**: add a host-side progress or confirmation line (the popup is the surface, and the
  owner's rule stands); draw A in a state where there is nothing to refresh; or change what **B**
  does (close, no refresh - the song then arrives on the next boot, which the harness keeps as belt
  and braces).
- **Move with it**: `finish_installed` and the popup's `BUTTON_DOWN_MSG` (`src/store/store_payload.cpp`),
  the A/B locale pair, `store_payload_tests` (A exists only in the installed state, its case runs
  the navigate + sentinel pair, B still closes, idempotency), `scripts/acceptance_store.ps1` (the
  Store leg becomes a **same-boot** assertion - download, A, the song list carries the title - with
  the Install leg kept as the next-boot check), `docs/guides/dlc.md` §2.2 (its "waits for the next
  start" bullet becomes conditional on **B**), `docs/reference/known-issues.md` (OQ6's restart
  limit narrows to "until P9-14 is measured", then to whatever the measurement leaves) and
  `docs/engine/diagnostics.md`.
- **Done when**: a boot downloads a real package; the toast reads **A Refresh   B Close**; **A**
  shows the discovery pass, the refresh runs (the host's own re-scan line), and returning to the
  menu Your Songs lists the downloaded title **in that same session**; the offer's next visit reads
  Downloaded; **B** closes without refreshing, and that route's song arrives on the next boot's list
  instead (the harness keeps the Install leg as that check); a cancelled or failed job draws no
  **A**.

**P9-14 record (2026-10-24) — the payload, its tests and the harness's same-boot steps landed; the
boot that reads the song on Your Songs after A is the measurement the prompt still owes.**

- **The state.** `AddDownloadPopup` (`src/store/store_payload.cpp`) grew a third boolean beside
  `active`/`terminal`: `installed`, declared `FALSE` in the panel, set `TRUE` by `finish_installed`
  and `FALSE` by `show_popup` and `finish_failed` - so the caption and the action both hang off the
  one state that has a song to refresh for, and a cancelled or failed job carries neither. The
  installed state's prompt is the new locale entry `rbbz_download_refresh` =
  `<alt1>A</alt1> Refresh   <alt1>B</alt1> Close` (added by `menu_payload.cpp` beside the other four
  popup entries). Nothing else about P9-6's panel changed: B still cancels while the job runs and
  closes after it, `exit` still cancels with the popup up, and no host line was added.
- **A's case, measured against the shipped record.** `BUTTON_DOWN_MSG`'s `kAction_Confirm` case is
  the menu row's own three statements, in the row's own order, as **siblings** - the shape
  `TapRefreshCache` measured a screen change to survive, where a `do` loses everything after the
  navigation (2026-10-15):

  ```text
  (kAction_Confirm
    {if [installed] {rbbz_download_panel hide_popup}}
    {if [installed] {ui goto_screen server_connect_screen}}
    {if [installed] {file_exists "songcache:/rbbz_dlc_refresh"}})
  ```

  The two spellings are `menu_options::kRefreshRowDiscoverScreen` / `kRefreshSentinelPath` - the
  row's own constants, not copies - and the guard is repeated per statement so no partial sequence
  can run. `rb_blitz_store_payload_tests store-edit assets\game\360\ui\store\gen\store_info.dtb
  download` answers `download row -> edited`, `download popup -> edited`, `download press ->
  edited` and dumps exactly that tree (`out/_swap/p9-14-download.txt`).
- **The refresh's three layers, mechanically.** The sentinel reaches `dlc_refresh.cpp`'s file-call
  hook, which logs `dlc: refresh asked for ("songcache:/rbbz_dlc_refresh"), re-scanning the DLC
  libraries` (the line names the refresh now, not one entry - the toast is its second caller) and
  calls `dlc::RefreshConfigured`, which schedules the scan (`dlc: the song refresh entry asked for a
  refresh; …`) and then `WaitForRefreshToLand` publishes the fresh list to the content manager on
  this guest thread, bounded at 15 s. Two host changes make that the same guarantee the row gets:
  `RefreshConfigured` no longer returns before the wait when the cache is off (the store's
  post-install scan ignores that switch, so a pending list is published either way), and
  `WaitForRefreshToLand` is gated on the runtime alone instead of the cache - with nothing pending
  it exits on its first test.
- **Tests.** `store_payload` is **575 checks** (was 552): `TestDownloadRefreshAction` pins the
  `installed` declaration, the three writers, the refresh prompt token, the A case's three guarded
  siblings in order with the menu constants, and that B's case never mentions the refresh;
  `TestDownloadPopup` now also pins the refresh token in the tree. Full `ctest` in
  `out/build/win-amd64-debug` is **47/47** (`texture_conversion`, a test another workstream added to
  the tree, is unbuilt in that directory and excluded - its "Not Run" predates this change).
- **Harness.** `scripts/acceptance_store.ps1`'s Store leg now reads the installed toast's prompt
  (`DOWNLOADED`, `REFRESH`, `CLOSE`), presses **A**, and asserts the three host oracles the press
  produces - the sentinel's line, a further `content_pipeline: attempt N started` (the title
  re-enters its discovery pass), and `splash_screen` coming back on its own - then Play → Your Songs
  and the installed title's words on the list, **in the same boot**. The leg's browser-return chrome
  check moved before the download, because A ends the visit at the main menu. The `Install` leg
  keeps the next-boot check, which is also the route **B** leaves. The Control leg's sentinel
  assertion follows the log's new wording (`dlc: refresh asked for`).
- **Docs**: `docs/engine/diagnostics.md` (the popup paragraph, the A case and the harness's steps),
  `docs/guides/dlc.md` §2.2 (the "waits for the next start" bullet is now the A/B split) and
  `docs/reference/known-issues.md`'s store row (OQ6's restart limit becomes "the same-session boot is
  the measurement owed").
- **Owed (the prompt's Done-when)**: the boot. It must read the toast's **A Refresh   B Close**, the
  discovery pass after A, and the downloaded title on Your Songs without a restart; a cancelled or
  failed job must draw no **A**. The same-session list is the part this environment has never
  measured for any package - the install's list has only been read after a **boot** (the `Install`
  leg) - so the first boot either closes OQ6's open half or says what the pass alone still misses.
  On this machine the boot is the owner's to take (`AGENTS.md`), and the release binary must be
  relinked first (the owner's running copy holds it).
- **Not asserted by the new leg**: **B**'s close-after-install and the browser it returns to. P9-6
  measured that press in game (2026-10-24) and the A case's guard pins that B carries no refresh;
  the Store leg's one transfer is spent on the same-session route instead. The offer's next visit
  reading *Downloaded* is likewise not on this boot's route (the Downloaded matcher's in-session
  invalidation after a refresh is host-pinned: `InvalidateLibrary` runs in `PublishPendingRefresh`).

### P9-1 — the master switch defaults On

- **Do**: flip the default in three places that must agree — the schema row
  (`src/settings/schema.cpp:159-163`, `BoolValue(false)` plus the comment above it), the store's
  compiled snapshot (`src/store/store_settings.h:29-33`, `enabled = false`), and the prose that
  promises "off by default" (`docs/settings/status.md`, `docs/guides/dlc.md`, the index rows).
- **Move with it**: the acceptance harness. `scripts/acceptance_store.ps1` currently starts the
  Enable leg from a fresh profile with the switch **off** (`:6-7`, `-RhythmVerseRow 3` at `:66`,
  the `rhythmverse = false` write at `:152`, the "reads Off before the press" step at `:557`). With
  On as the default the Enable leg proves the default instead of flipping it, and the **Control leg
  becomes the only leg that writes the value off**. Pin the default in
  `tests/settings_screen_tests.cpp` and `tests/store_client_tests.cpp`.
- **Done when**: a profile with no `settings.toml` boots with the row On, the main menu draws the
  store row and the store serves offers; the control leg (master written off) still makes no store
  request; the suites are green.

**P9-1 record (2026-10-22).** The default is On in both tables that must agree, and the harness now
proves it instead of flipping it:

- **Code**: `src/settings/schema.cpp`'s row compiles `BoolValue(true)` and `StoreSettings::enabled`
  defaults `true` - one is what the page shows before a commit, the other what a worker reads before
  one, so they can never disagree. `src/ui/settings_screen.cpp`'s page comment no longer promises
  "off by default"; `Off` is still the switch that silences the store, because the workers ask
  `StoreSettings::enabled` before every request (P8's control leg measures that silence).
- **Pinned**: `settings_screen` reads the master's `compiled_default` through
  `schema::FindRow(kRhythmVerse)` (its storage key is `[general] rhythmverse`; the target now links
  `src/settings/model.cpp` + `schema.cpp` for that one assertion), and `store_client` asserts the
  snapshot's own default plus a live **Off** commit and its idempotent repeat (the case used to
  commit On, which the default now makes a no-op).
- **The harness**: `Write-StoreSettings` takes a nullable value, and only the **Control** leg passes
  `false`. The Enable leg's profile carries the fixture's other rows but never names `rhythmverse`;
  the leg asserts the page draws `RhythmVerse: On`, that the boot added no `rhythmverse` line to the
  profile (so the value really was the compiled default), and that the same boot's main menu already
  carries the store row. The row press was removed - this prompt makes Control the only leg that
  writes the value off - and `-RhythmVerseRow` went with it.
- **Docs**: `settings/status.md`, `settings/plan.md`, `reference/backlog.md` and
  `engine/diagnostics.md` now say On; diagnostics marks its 2026-10-22 measured Enable run as
  predating the flip.
- **Measured** (release binary). `scripts/acceptance_store.ps1 -Legs Enable -Label p9-1-enable`
  (`out/store-acceptance/p9-1-enable/`) passes **8 of 8**: the page drew
  `RHYTHMVERSE: ON` on a profile whose settings file never named the key, the file still had no
  `rhythmverse` line afterwards (only `target = 'ultimate'`, written by migration), and the main menu
  carried `HYTHMVERSE CUSTOM` - the Done-when's page and menu halves. `-Legs Control -Label
  p9-1-control` passes **16 of 16**: no store row, six absence checks at 0 matches, the refresh row's
  sentinel present. The same default profile then walked the **Store** leg (`-Legs Store -Label
  p9-1-enable -Reuse`): **38 steps, 1 failure** - the store served `99 offer(s)`, the browser opened
  on them, Search answered `15 eligible of 25`, a download installed
  `rv_436f1b68360f1ad9be533437be169410.con` (4,927,488 bytes) and the refresh ran, so "the store
  serves offers" with the switch never written is measured; the one failure is the credit strip's
  step, and its cause is a timing window, not the switch: the front menu was noted at 19:30:11.5,
  page 1 landed at 19:30:21.9 (10.4 s), and the credit is only drawn within
  `kCatalogueCreditHold` (12 s) of the surface note, so the capture at ~19:30:25 found a blank strip
  (`store-credit.png` OCRs the front menu and no credit). **P9-6d deletes that line and its step**, so
  the fix belongs there rather than here. *(Superseded 2026-10-24: 6d was withdrawn with the other
  follow-ups, so the credit line and that harness step stay; the timing window the step hit is the
  harness's own to widen.)* `ctest` is **47/47** with the two suites' new pins.

### P9-2 — a master change: hot only if the menu row and the header follow it

- **The scope (owner pass, 2026-10-22)**: the master staying hot is the *preferred* answer, but it is
  only honest if **everything the switch owns changes live too**. A hot master therefore owes the
  main-menu **RhythmVerse Customs** row's presence **and** the store's heading, not just the
  workers' snapshot. The heading's own live form is a later prompt's subject; this prompt owes the
  rule that it cannot lag behind the switch. If both cannot be proven live, the master is **cold**
  (`kRestart`).
- **What already follows a commit**: the store's settings snapshot (`src/store/store_settings.cpp`,
  the applier at `:61-74`), so the next catalogue/art/download/search request obeys the new value,
  and the row's **press** (the case re-reads `rbbz-store/enabled` at press time, §5/P1). What does
  **not** follow: what is *drawn* - the main-menu record is compiled once per boot (§5/P1's baked
  presence) and the store's heading is a locale entry `SetStoreHeading` replaces in the payload
  (`src/store/store_payload.cpp`).
- **Route A — stay `kHot`** (only if it lands, measured): give the main-menu row a live presence the
  way the settings screen repopulates its own list after a commit (`set_data` + `set_selected` on the
  splash list from a host-side call, or a payload-authored gate the row's own `set_data` consults),
  and make the heading follow the committed value in the same boot. Prove both on one boot: flip
  RhythmVerse Off, leave Settings, the row is gone *and* the heading no longer claims RhythmVerse;
  flip it On, both return - no restart. Because the heading's live change belongs to a later prompt,
  Route A is **not** met until that prompt exists or this one proves the heading can be re-replaced
  live; a stale heading over a hidden row is exactly the mismatch this scope forbids.
- **Route B — `kRestart`** (the landing unless Route A is proven): make the **master** `kRestart`;
  the committed value reaches the store at the next boot, and the surface's existing prompt - the
  authored `settings_restart_now` / `settings_close` page
  (`src/ui/settings_screen.cpp:1500-1512`), opened when the merged root is left
  (`docs/settings/architecture.md` part 6 §6.1) - offers `Restart now` / `Close`. Keep the five
  instrument leaves `kHot`: their live effect is measured (P7's cancellable walk, the page cache
  keyed by the request body).
- **Also bound to the master later**: P9-9's RhythmVerse tab adds a surface the switch owns. (P9-10
  first carried a hidden flow beside it; its 2026-10-22 scope change retired that — a failed press
  says so and retries, nothing is hidden.) A hot master would owe that tab in the same boot as
  well, which is the second reason Route B is the honest default today; say so in the record if
  Route B lands.
- **Move with it**: the pinned hot-row list in `tests/settings_hot_apply_tests.cpp` (six store rows
  today), P6's "the six rows are `kHot`" sentence (supersede with a line naming this prompt), and
  `docs/settings/menu.md`'s hot/restart column. Route A also owes a live-presence case (the built
  menu plus the gate) beside the boot that shows the row appearing and disappearing.
- **Done when** (Route A): flipping the master in Help & Options adds/removes the main-menu row in
  the same boot, the store's heading agrees with it, no restart is asked for, and the suites are
  green with the row-presence case pinned.
  **Done when** (Route B): flipping the master and leaving Help & Options draws the prompt once;
  `Close` leaves the store on its pre-change value and makes no request; `Restart now` relaunches
  and the new value acts (row present/absent, prewarm done/not); flipping a filter leaf asks for
  nothing. The record names which route landed and why.

**P9-2 record (2026-10-22) — Route B landed: the master is `restart`, the five filter leaves stay
`hot`.** The owner's rule is a hot master only while the main-menu row **and** the store's heading
follow a commit live. Neither does: the row is compiled into the record the boot generates (§5/P1)
and the heading is a locale entry `SetStoreHeading` replaces in that same payload, so a mid-session
change would leave one of the two lying — and a hot master would also owe P9-9's RhythmVerse tab and
P9-10's hidden flow in the same boot. Route A therefore stays unproven (a live row presence plus a
live heading replacement is its own spike), and the prompt the surface already has is the honest
answer.

- **Code**: `kStrategies` carries `kRhythmVerse` as `kRestart` with an empty evidence string, and the
  five `kRhythmVerse*` leaves keep `kHot`; the runtime applier's store table drops the master (the
  seam is never asked for it, and listing it would be a second, quieter claim), and
  `store_settings.h` says where the master's value does come from — `SettingsBoot::SeedStoreSettings`
  at boot, which is also what lets *Close* leave the running store on its pre-change value.
- **No new prompt was written**: leaving the merged root already asks `snapshot/restart.pending`,
  which `SettingsBoot::RestartPending()` answers by comparing every row's effective value with the
  boot's (`PendingRestartRows`), so a master change opens the authored `settings_restart_screen` and
  *Close* finishes the leave.
- **Measured** (`scripts/probe_master_restart.ps1`, release binary, scratch profile, two boots; the
  session leg is `out/_swap/master-restart/20261022-214004/session.log`, every step green, and the
  next boot is `out/_swap/master-restart/20261022-214555/next-boot.log`, which that leg's own run
  reports as **9 steps, 0 failures**):
  - the page reads `RHYTHMVERSE: ON` on a profile that never named it; the filter page's `Guitar`
    flips Off and the log carries `hot_apply: general.rhythmverse_guitar = false`;
  - leaving after the leaf returns to `splash_screen` with **no** `settings_restart_screen` line;
  - re-entering and flipping the master reads `RHYTHMVERSE: OFF`, the log carries **no**
    `hot_apply: general.rhythmverse = …` line, and leaving enters `settings_restart_screen` **once**
    (OCR `RESTART … CLOSE`; the entry count does not move again after *Close*);
  - the commit makes **no** catalogue request (the boot's own prewarm count is unchanged, 1 before
    and 1 after) and the store is never opened (`store_fetch: served '` = 0);
  - *Close* returns to the main menu, which **still draws the store row** (`CUST`) — the session
    kept the pre-change value;
  - `settings.toml` carries `rhythmverse = false` and `rhythmverse_guitar = false`, written by the
    game itself (copied beside the log as `settings-after-session.toml`);
  - the next boot on that file has no store row (`BLITZ … HELP & OPTIONS EXIT GAME REFRESH MUSIC
    LIBRARY`, and the host says `settings_menu: the RhythmVerse Customs entry is off (its setting is
    off)`), reads `RHYTHMVERSE: OFF` on the page, and `store_catalogue: fetching`,
    `store_fetch: served '`, `store_library: indexing`, `store_download: start` and
    `store_search: page` are all at 0 matches. Its live-applier line lists seven rows and the
    master is not among them (`graphics.vsync`, `interface.settings_labels` and the five
    `general.rhythmverse_*` leaves).
  *Not measured*: the `Restart now` row's own relaunch. Its mechanics are P3's installed-game review
  (status item 32) and `game_restart`'s host tests, and the value's action is what the next boot
  above reads; pressing it here would only re-prove the coordinator.
- **Environment**: this 4-core machine dropped the GPU ringbuffer (`PRIMARY RINGBUFFER: packet …
  incomplete … retrying`) on three of the seven boots taken for this prompt, each time on the
  connect screen with no log line afterwards. Those attempts are kept beside the evidence
  (`20261022-213310`, `20261022-214349`, `20261022-214452`) and none of them is counted as a result;
  the two logs above are the green ones. The session leg's run also carried two probe-only failures
  in the next-boot half — absence needles that matched the modules' own boot announcements
  (`store_library: `, `store_download: `) instead of their work lines (`… indexing`, `… start`) —
  fixed before the leg was re-run on its own (`-NextBootOnly`, the run cited above).
- **Tests**: `settings_hot_apply` pins **7** hot rows and `kRhythmVerse` in the must-stay-restart
  list (re-promoting it now needs a measurement), and `store_client`'s case title no longer calls
  the master a live commit; full `ctest` **47/47** in `out/build/win-amd64-debug`.
- **Docs**: `settings/plan.md` (the row's `hot` → `restart`), `settings/status.md` (at a glance plus
  the measurement), `settings/architecture.md` part 6 (§2's table, §4's row set), `engine/
  diagnostics.md` (the new probe), and §5/P6's record carries a superseding line.
  `settings/menu.md` carries no per-row strategy column for these rows, so it needed nothing.

### P9-3 — `Downloadable Content ▸ RhythmVerse` first, and a renamed filter

- **Do**: `src/ui/settings_screen.cpp:1056-1098` becomes five rows, in this order — **RhythmVerse**
  (a submenu, first), Cache DLC Library, Refresh Music Library, DLC Scan Threads, DLC Menu Music. The
  new `settings_rhythmverse_panel` (Back → `settings_dlc_screen`) holds the master `RhythmVerse` row
  and the filter submenu; the filter's page title (`:659`) and its row label (`:1088-1091`) read
  **Search Song Filter**, help text included. Five rows stay inside the six-row budget
  (in-game-settings skill §2), and the filters stay reachable while the master is off (P6's rule).
- **Move with it**: `settings_screen`'s page-order and page-count pins (P6 moved them to 59 per
  context, 118 both; they move again — the tests hold the exact numbers), the title/locale/help
  tables, `scripts/acceptance_store.ps1:66` (`-RhythmVerseRow 3`) and its Enable-leg OCR steps
  (`:556-574`: the master is now reached through the submenu), `docs/settings/menu.md` (the
  Downloadable Content page) and `docs/settings/status.md`.
- **Done when**: Help & Options ▸ General ▸ Downloadable Content reads those five rows in that order,
  RhythmVerse opens onto the master + Search Song Filter, Back returns to the page, and the main-menu
  and pause-context twins agree.

**P9-3 record (2026-10-22) — landed.** Downloadable Content now has the five rows
above, with RhythmVerse first. The new two-row RhythmVerse page contains the master and
**Search Song Filter**; the filter's title and help text use the new name, and it remains
reachable while the master is Off. Back from the filter returns to RhythmVerse, and Back
from RhythmVerse returns to Downloadable Content in both contexts. The shared page set is
now **70 per context / 140 total** (the starting point was 69 / 138 after P4's key pages).
The acceptance script enters DLC row **0**, checks the submenu and renamed filter, and checks
Back to DLC. `docs/settings/menu.md` and `status.md` describe the new tree.

Validation: the owner manually tested the change and reported it working. The
`settings_screen` host suite passed **137,801 checks**, including both contexts' Back targets
and the page counts. The updated acceptance script has not been run end to end for this change.

### P9-4 — `Interface ▸ Music Library & Store ▸ Show Messages and Recommendations`

- **Do**: add a fourth row to the Interface page (`src/ui/settings_screen.cpp:1431-1447` — Main Menu,
  Offline Prompts, Help & Options Labels, in that order, unchanged), a page of its own, and one
  boolean `interface.show_messages_recommendations` (**On** by default) with a fresh `SettingId`
  appended to `src/settings/model.h` (80-85 are taken by P6) and its row/token/help entries. It
  gates the retail tab bar's MESSAGES and RECOMMENDED tabs on **both** surfaces (P9-9); the
  RhythmVerse tab is gated by the store master instead. Strategy: **restart** unless a boot measures
  the tab bar changing live — the tab captions are scene art (§5/P7), so do not mark it hot on
  faith.
- **Move with it**: `settings_screen`'s page/row pins and defaults, the `profile` round-trip tests,
  `docs/settings/menu.md` (the Interface page) and `docs/settings/status.md`.
- **Done when**: a fresh profile reads On; MESSAGES and RECOMMENDED are drawn while On and neither
  while Off, in the store and in the song list; the row's help sentence names both tabs.

**P9-4 implementation record (2026-10-22).** The owner chose **Show Headers**
when the proposed label would exceed the in-game-settings skill's 28-character
line budget; the full Off line is 17 characters. Interface keeps its first three
rows and appends **Music Library & Store**, whose one boolean has the help caption
**Show the MESSAGES and RECOMMENDED headers in Your Songs and the store. Requires a restart.**
The page exists in both contexts and backs to Interface (71 pages per context).
`SettingId::kShowMessagesRecommendations = 86` maps to
`interface.show_messages_recommendations`, defaults On, and is restart-scoped.
The boot passes its effective value into the payload configuration.

The shared `ui/background/gen/background.dtb` owns the labels: its
`set_offline_mode_headers` handler runs on store and song-list entry. With Off,
the payload appends hiding `main_Home.lbl` (RECOMMENDED), `main_inbox.lbl`
(MESSAGES), and `main_new_msg.lbl`; `update_inbox_header` also hides the badge
after its retail callback. On leaves this record untouched. The change is
independent of the RhythmVerse master. **Superseded 2026-10-23:** the correction
below also implements the song list's offline trigger navigation; P9-9 still owes
the store tab's own visibility.

**P9-4 correction (2026-10-23; manual verification pending).** The owner tested the
first implementation: **On** draws the labels but Your Songs does not navigate,
and **Off** crashes. The earlier unit checks proved the generated tree's shape,
not its behavior in the game. The initial visual probe measured the new settings
page and caption only; its extra Back press dismissed the restart prompt before
the assertion and it never completed the On/Off surface matrix.

- **Off:** replace `{{$this find "<label>"} set_showing FALSE}` with
  `{<label> set_showing FALSE}`, the direct scene-object command retail already
  uses for `main_new_msg.lbl` in this same panel. This removes the nested lookup
  added after the header Flow runs. The reported crash is **not yet reproduced
  or confirmed fixed**; the owner elected to retest manually.
- **On:** `ui/song_select/gen/song_select.dtb`'s `song_select_panel` wraps both
  trigger cases in an online condition (P9-9's measured starting point). Replace
  that condition with TRUE, preserving the store destination, analytics and arrow
  flow. This is the reason the song list previously stayed still while the store
  could navigate. With the store master Off, RT skips the disabled store for Inbox.
- **Off navigation:** both song-list triggers open the enabled store; the store
  menu and browser replace their RT destination Inbox with Your Songs. Thus the
  hidden MESSAGES and RECOMMENDED pages are skipped. With both switches Off,
  the song-list triggers only pulse their arrows.
- **Host verification:** the four relevant suites (`menu_payload`, `settings_screen`,
  `settings_service`, `settings_hot_apply`) pass. The payload suite also provides
  `check-header-records <extracted-assets> <output-dir>`: the real extracted retail
  records were built for all four switch combinations, **zero optional-edit
  refusals**, every generated DTB parsed and recompiled byte-identically. The output
  is `out/_swap/headers-correction/records/{on,off}-store-{on,off}/`. This check
  does not boot or navigate the game.
  The release executable was rebuilt successfully; the documentation checker
  reports zero errors. Manual retest: restart with Show Headers On, use LT/RT
  from Your Songs and the store, then restart with Off and repeat.

**P9-4 header presentation correction (2026-10-23; owner screenshots).** The
owner's Your Songs capture still shows dim neighboring tabs and no LT/RT arrows;
the RhythmVerse capture shows bright tabs and both arrows. The previous correction
changed the button handlers, but retained the screen's separate presentation gate:
`{background_panel set_offline_mode_headers {! {user_mgr is_participating_user_online}}}`.
The store's own screen sends `FALSE`. In `ui/background/gen/background.dtb`, this
argument selects `offline_headers.flow` versus `online_headers.flow`.

The song-list payload now sends **FALSE whenever Show Headers or the RhythmVerse
master is On**, matching the store's navigation appearance. With both Off it sends
TRUE, since neither trigger has a destination. The shared background definition
remains unchanged while Show Headers is On. Regression tests pin both the trigger
cases and this screen-entry call, including the actual generated retail records
for all four switch combinations. **Visual retest remains with the owner**; these
  checks do not navigate the game. *(Superseded 2026-10-23: the owner confirmed the
  corrected swipes and compact layout on the rebuilt binary.)*
**P9-4 split-controls record (2026-10-23; owner update).** The owner reported
that the preceding combined header/navigation correction **worked perfectly**,
then requested independent **Show Messages** and **Show Recommendations** rows
in the same Music Library & Store page. Both default On, require a restart,
and fit the row budget (`Show Recommendations: Off` is 25 characters).
The page has two rows in both contexts; no new page is needed.

- **Storage:** fresh stable ids 87 and 88, tokens `show_messages` and
  `show_recommendations`, under `[interface]`. Id 86 is retired and never reused.
  Each absent new key inherits the valid old `show_messages_recommendations`
  boolean; an explicit new key wins independently. The old bytes remain, and
  loading and conflict detection use the same fallback so an inherited Off
  does not cause a false save conflict.
- **Visibility:** MESSAGES and the new-message badge follow Show Messages;
  RECOMMENDED follows Show Recommendations; RHYTHMVERSE follows the store master.
  The shared panel keeps the direct scene-object commands the previous correction
  used. Its full-header Flow is selected whenever **any** optional tab is On,
  regardless of sign-in. All three Off selects offline styling and hides LT/RT.
- **Navigation:** all five panels (song list, store menu/browser, inbox,
  recommendations) use the ordered ring **Your Songs → RhythmVerse → Messages →
  Recommended**, RT forwards and LT backwards, skipping every disabled entry.
  This replaces the earlier paired shortcuts. Original preview cleanup,
  store-highlight saving and the directional arrow Flows are retained.
  Recommendations' RT helper is expanded only in that trigger case, preserving
  other callers that must still open Your Songs. No disabled store is re-entered
  from Inbox. The record therefore also implements P9-9's tab visibility rule.
- **Host verification:** the five relevant suites pass (`settings_screen`,
  `settings_service`, `settings_store`, `settings_hot_apply`, `menu_payload`),
  including independent persistence, legacy fallback and conflict checks.
  The release executable was rebuilt successfully. `check-header-records` builds
  **all eight states** from extracted retail records, with zero refusals and
  byte-identical DTB round trips; its **617 assertions** inspect the destinations
  in both directions, arrow state, labels, badge and preserved preview cleanup.
  Evidence: `out/_swap/split-headers/records/state-0..7/`, where the state bits
  are store=1, Messages=2, Recommendations=4. **In-game retest of the new split
  remains with the owner.** *(Superseded 2026-10-23: the owner confirmed the tabs
  and the split switches on the rebuilt binary - see the P9-9 close-out.)*

**Final defaults (owner update, 2026-10-23): Show Messages = true; Show
Recommendations = false.** These replace the split record's initial On/On
defaults in the schema and both payload configuration defaults. Saved split
values and the legacy combined preference keep their existing precedence.
The existing default assertions now pin Messages On and Recommendations Off.

**Final placement (owner update, 2026-10-23):** Interface now reads **Main Menu →
Music Library & Store → Offline Prompts → Help & Options Labels**, in both menu
and pause contexts. This supersedes the initial placement as the fourth row;
the two switches and their defaults are unchanged. The existing row-order test
pins the new order.

**Animation and layout correction (owner reports, 2026-10-23).** The split was
manually exercised: with RhythmVerse Off and both other tabs On, RT correctly
opened Messages but swiped left. With Recommendations Off and RhythmVerse/Messages
On, the hidden label left a hole between visible labels, rotating through the bar.
This supersedes the earlier assumption that retaining the retail positions and
transition inference was sufficient.

- **Direction:** `ui/gen/transition_screens.dtb` infers a wrap whenever the index
  difference is not one. Songs is index 1 and Messages is 3, so skipping the store
  reverses that inference. Header trigger cases now call an authored wrapper with
  +1 for RT / -1 for LT. The transition object receives that override before
  `ui goto_screen`; ordinary transitions explicitly reset it to zero, retaining
  the original selector for vertical changes and unrelated callers. Cached
  transitions therefore cannot retain an earlier trigger override. Two-tab rings
  can animate the same source/destination pair in either direction.
- **Compaction:** the background panel's scene owns `TextShift.anim`, with
  `local_xfm/x` tracks on `main_home.trans`, `main_mySongs.trans`, `main_store.trans`,
  `main_inbox.trans`, and `main_newMessage.trans`. Its endpoints are frames
  0/10/20/30/40 (Recommended/Songs/Store/Messages/Recommended). On background entry,
  native `set_key_val` commands rewrite the enabled labels' endpoint X values in
  cyclic order into consecutive retail slots (-345, -43, 91, 226; Recommended's
  original fourth-slot offset is 235). The badge shares Messages' position. Hidden
  slots consequently trail the enabled labels at every selected header. Retail
  scale/colour tracks, offscreen wrap keys, and unrelated scene tracks survive;
  all four tabs enabled emits no position edits. This changes the guest's animation
  data, with no host drawing or per-frame position correction. The native handler
  is `sub_82286570`, its `set_key_val` branch at 0x822867F4, calling `sub_82286178`.
- **Host verification:** `menu_payload` passes, and the real-record checker runs
  **1,550 assertions** over all eight combinations: both trigger routes and swipe
  directions, ordinary-transition fallback/reset, consecutive positions for every
  enabled current header, the frame-0/40 wrap, and the badge. All generated DTBs
  parse/recompile byte-identically, with zero refused edits. Evidence is
  `out/_swap/header-animation/records/state-0..7/`. The full
  `cmake --build --preset win-amd64-release` succeeds. **Visual motion and layout
  still require the owner's manual retest; no game UI was driven for this fix.**

**P9-4 closed (2026-10-23; owner confirmation):** the owner manually tested
and confirmed the independent switches, skipped-tab swipe direction, and compact
header layout work. P9-4 is Done. The pending-test notes above preserve the state
before this confirmation. The release build and all eight host configurations
were verified; the agent did not navigate the game.

**P9-4 reopened: intermittent Search overlap (2026-10-23).** After the earlier
manual acceptance, the owner reported Recommended overlapping Your Songs around
Search, with RhythmVerse and Messages On and Recommendations Off. The screenshot
shows the disabled caption at the same position as the compacted Your Songs label.
Static inspection finds `store_browser_screen.enter` does not call
`set_offline_mode_headers`; only the store front menu does. A restored caption
therefore has no visibility correction on that entry path. The exact intermittent
restoration timing has not been reproduced in-game.

`ConfigureLibraryHeaderBar` now applies its disabled-label mask in the shared
background panel's `enter` and `poll`, as well as the existing mode and badge
callbacks. Existing poll statements survive. These callbacks hide only disabled
captions; poll never restarts a Flow or rewrites position keys, so it cannot
interrupt a swipe. All tabs On adds no poll method. Host tests simulate captions
being restored visible and verify entry/poll hide every disabled caption while
leaving enabled captions alone. `menu_payload` passes, and the extracted-record
matrix passes **1,982 checks** across all eight states, with zero refusals and
byte-identical DTB round trips. Evidence:
`out/_swap/header-search-visibility/records/state-0..7/`.
**Manual Search retest remains with the owner; the intermittent visual glitch is
not claimed reproduced or confirmed fixed from host checks.**

**P9-4 close-out (2026-10-24; owner's own check) — P9-4 is Done.** The owner drove the rebuilt binary
and reports the two independent switches (**Show Messages** On / **Show Recommendations** Off, both
restart-scoped), the ordered tab ring with its skipped-tab direction, and the compact header bar as
working, the intermittent Search overlap included. Read by hand, no capture stored - the same standing
as P9-5's, P9-7's and P9-9's owner boots. What stays as the standing check: the eight-state
extracted-record matrix (`menu_payload`'s `check-header-records`, **1,982 assertions**, evidence
`out/_swap/header-search-visibility/records/state-0..7/`) and the suite pins the two rows, their
defaults and the legacy fallback.

### P9-5 — **Recently Added**, and no count beside the two rows

- **Do two things in `src/store/store_metadata.cpp`**:
  1. the front-menu label (`:259`, `{"recent", "Recent"}`) becomes **Recently Added**. The id stays
     `recent` — the provider path `dlc_store/europe/eng/recent/` the panel asks for is measured and
     unchanged — and the user-facing sentences follow: `store_catalogue_status.cpp:43` and `:46`
     ("try Recent again"), the empty-state sentence, and §1.6's prose.
  2. the trailing **1**. The row is a three-element array `(path label TRUE)` — the shape P3 measured
     (§5/P3: `sub_8275C968` reads a row as an array and wants `count > 2` with the third element
     non-null). Serve variants and keep the one with no number drawn: `FALSE`, an integer, and a
     four-field row (never a two-field row — P3 measured that faulting). If the non-null third
     element is load-bearing, find the field the retail menu draws the number from (the list
     resource or the panel's own count) before inventing state.
- **Move with it**: `store_metadata`'s tests, the front-menu OCR in
  `scripts/acceptance_store.ps1:600-601` and the `Assert-Label` calls, and P7's strip copy. The log
  token (`offer(s) for the Recent list`) is a log string and may stay.
- **Done when**: the front menu OCRs `RHYTHMVERSE / RECENTLY ADDED / SEARCH` with no digit beside
  either row, both rows still request their measured paths, and the browser opens on their offers.

**P9-5 record (2026-10-23) — landed, and the owner's boot reads the row with no count.**

- **Where the number came from, measured statically out of the image** (`out/_swap/blitz_image.bin`,
  base `0x82000000`, read with `out/_swap/fn_strings.py` and `find_string_refs.py`). The front menu's
  list is `store_menu.milo`'s `filter.lst`, whose resource
  (`ui/resource/lists/gen/list_store_menu.milo_xbox`, dumped with `scripts/hmx_milo.py`) carries
  exactly two labels — `count.lbl` and `filter.lbl` — and the native provider that fills them is
  `sub_8275CD58(data, index, label, out)`:
  - `label == "filter"` (literal `0x82044668`) copies the row's **second** element, the text the
    player reads;
  - `label == "count"` (literal `0x82059C28`) resolves the row's **third** element and formats it
    through the formatter at `provider + 68`. That is why `(path label TRUE)` drew `1`: `TRUE` is the
    number 1 (`out/_swap/store-probe/20261022-140259/store.png` OCRs `Recent 1 / Search 1`).
  - `sub_8275C9D8(row)` is the only predicate that **skips** that branch, handing the label an empty
    string: it answers true when the row is an array whose **fourth** element resolves to the symbol
    `random_song` (its sole string literal, `0x82106738`) — retail's own "this row has no count"
    marker, used on its random-song row. So no spelling of the *third* element draws nothing, and the
    prompt's "four-field row" is the only variant that can.
- **Code**: `src/store/store_metadata.h/.cpp` gained `FrontMenuRowForm` (`kCount` =
  `(path label TRUE)`, `kFalse`, `kZero`, `kNoCount` = `(path label TRUE random_song)`) with
  `FrontMenuRow`/`SetFrontMenuRowForm`/`FrontMenuRowFormToken`/`ParseFrontMenuRowForm`, and
  `RhythmVerseFrontMenu()` now serves `Recently Added` (`kRecentlyAddedLabel`; the id stays `recent`,
  so the measured request `dlc_store/europe/eng/recent/` and the source id `menu_recent` are
  unchanged) and `Search` (`kSearchMenuLabel`). `BuildFrontMenuText` writes the row's tail per form,
  with the marker as a **bare symbol** (the predicate compares the resolved value, so a quoted string
  would not match). `src/store/store_fetch.cpp` reads the new `store_front_menu_row` diagnostic
  (**default `no-count`**) at installation and logs the form it serves; an unknown token warns and
  keeps the default rather than serving something nobody measured.
- **Copy that followed the rename**: the two offline sentences now read "Press Back and open
  Recently Added again" (`src/store/store_catalogue_status.cpp`); the empty-state sentence never
  named the row, so it stands; the comments in `store_catalogue`, `store_serve`, `store_search`,
  `store_search_service` and `store_page_cache` name the row `Recently Added`. The log token
  (`offer(s) for the Recent list`) stays, as the prompt allows.
- **Pinned hermetically**: `store_metadata` (new `TestFrontMenuRowForm`: the default is `no-count`,
  every token round-trips, and a typo leaves the form alone) and `store_serve` (the exact front-menu
  text with both labels and the marker, plus `TestFrontMenuRowForms` covering all four tails and that
  the marker is never quoted); full `ctest` **47/47** in `out/build/win-amd64-debug` and the debug
  game target links.
- **Harness**: `scripts/acceptance_store.ps1`'s Store leg waits for `RECENTLY`, asserts the two
  labels, and now also asserts the **absence** of the digit the old row drew (`RECENTLYADDED1`,
  `SEARCH1` through `Assert-Lacks`'s whitespace fold).
- **Measured (owner, 2026-10-23) — the boot this prompt's Done-when asks for, and it closes the
  prompt.** On a release build carrying this change the owner opened **RHYTHMVERSE** from the main
  menu and the front menu draws **Recently Added / Search with no digit beside either row**, then
  listed the offers and reported it working. That is the Done-when's OCR half, read by hand rather
  than stored as a capture: no screenshot path accompanies this record, so the `store_front_menu_row`
  diagnostic stays documented as the way to re-read the row (`count` restores the pre-P9-5 record
  that drew `1`). The harness's Store leg asserts the same two labels and the absence of the digit
  (`RECENTLYADDED1`, `SEARCH1`), but that leg has **not** been re-run on a build carrying the
  change - the owner's boot is the measurement, and the leg is the standing check.

### P9-6 — the download is one modal popup (delivered 2026-10-24)

*(One prompt, as delivered. The owner's 2026-10-23 design replaced P5's caption line with one modal
panel; the follow-up states it was briefly split into — a live progress line (**6b**), the toast's
A-refresh (**6c**) and a credit-line removal (**6d**) — were **withdrawn by the owner on 2026-10-24**.
The toast's A-refresh returns as its own prompt, **P9-14**; the credit line stays as P7 wrote it.
What shipped, and all this prompt owns, is the panel with its loading sentence, its outcome sentence,
the cancel and the close.)*

Measurements the design rides on (each dated at its source):

- **The modal popup exists and works (P9-11, 2026-10-23).** `rbbz_search_loading_panel`
  (`src/store/store_payload.h:136`) is defined by `AddSearchKeyboard` (`src/store/store_payload.cpp`)
  as `{new UIPanel … (file "../error/error_general.milo")}`: focus-owning, every button action
  consumed, B/Back cancels, and its native `poll` closes it from the bridge's one-bit predicates.
  Its two labels are `error.lbl` (the message, `set_token_fmt`) and `continue.lbl` (the footer
  prompt). The download popup is this panel's shape, defined in the record that draws it.
- **`store_info.dtb` already calls `show_popup`** (its offer panels use it), so the details record —
  where the download press happens — can host a modal popup; `store.dtb` does not (scanned over all
  131 records, 2026-10-22).
- **The job is pollable through P5's bridge**: `rbbz-store/state/<job>/<state>` (`queued`,
  `downloading`, `installed`, `failed`, `cancelled`), `progress/<job>/at-least/<bucket>` with buckets
  `{10,25,50,75,100}` and `error/<job>/<token>`; writes `download/start|cancel|again/<job>`
  (`src/store/store_download.h:90`, `src/store/store_download.cpp:255-388`). Every offer record
  carries its `(job "...")` key (P5).
- **No DTA record references a progress bar (scanned 2026-10-22).** The only bar in the extracted
  UI is `server_connect.milo`'s `progress_bar.grp` (`content_percent.lbl`, the boot's enumeration
  bar); the main menu's own loading state is the text caption `Loading Cached Content List` (P5's
  boots), not a bar. The engine's UI class table has no progress-bar class and the tree has no milo
  writer (`scripts/hmx_milo.py` reads scenes; nothing writes one) — which is why the delivered popup
  is text-only, and why the withdrawn 6b would have been a spike with a textual fallback.
- **The refresh the main menu runs** is `menu_options::TapRefreshCache`
  (`src/ui/menu_options.cpp:867`, logged as `dlc: refresh asked for from the main menu`), whose own
  action is `{ui goto_screen server_connect_screen}` followed by
  `{file_exists "songcache:/rbbz_dlc_refresh"}` — the discovery pass, then the sentinel
  `src/hooks/dlc_refresh.cpp` answers by re-scanning. `dlc::RefreshAfterStoreInstall` re-walks the
  flat roots (no cache gate) and publishes to the content manager, but the **running title's own
  discovery** of the newly installed package is OQ6's open half — which is exactly what **P9-14**
  now owns, and why the delivered popup stops at *Downloaded '<song>'.* instead of promising the
  song is playable.
- **The line this prompt removes**: the overlay's download status (`DownloadStatusText`,
  `src/ui/help_overlay.cpp:383` and its drawing branch, `src/store/store_download_service.cpp:441`).
  The caption strip's credit sentence stays — 6d was withdrawn, so P7's credit line and its
  `store_catalogue_status` tests are untouched.

**The prompt as delivered** (the owner's 2026-10-23 words, with the withdrawn states removed):

- **Do**: the two download cases P5 wrote (`store_download_now` / `store_download_again` in
  `store_info.dtb`) open a modal popup before they write `rbbz-store/download/start|again/<job>` —
  a panel on `store_info_screen` (where the press happens), built exactly like
  `rbbz_search_loading_panel`: the same `../error/error_general.milo`, `error.lbl` reading
  **Downloading from RhythmVerse...**, `continue.lbl` reading **B to Cancel**, focus-owning, every
  button action consumed. **B/Back** writes `rbbz-store/download/cancel/<job>` and closes, so the
  transfer never outlives the press. The popup **switches state on a terminal one, in the same panel
  object** (no second popup): `installed` reads **Downloaded '<song>'.**, `failed`/`cancelled` the
  generic failure sentence, both with **Press B to Close**.
- **Do not** keep the overlay line — `DownloadStatusText()` leaves `help_overlay.cpp` with its
  drawing branch, and the service's status string and hold window go once nothing reads them. Do
  not remove the caption strip's credit line (6d is withdrawn), and do not draw a host-side line for
  any of it.
- **Move with it**: the panel definition and its placement on `store_info_screen` (idempotent,
  refusing by name), the two button cases' route into it, the cancel write, `store_payload_tests`
  (the popup opens on the press, the cancel write, the terminal switch), `scripts/acceptance_store.ps1`
  (the Store leg's download step OCRs the popup instead of the strip), `docs/engine/diagnostics.md`,
  `docs/guides/dlc.md` §2.2 and `known-issues.md` where the restart wording moves.
- **Done when**: one boot presses Download on a real offer — the popup draws over the details page,
  the transfer runs, and B at any point cancels (no package, no receipt, the popup closes); letting
  it finish switches the same panel to its outcome sentence.

**P9-6 record (2026-10-24) — the modal is in the tree, the caption strip's download line is gone,
and the owner's boot reads both states on screen.**

- **The panel** (`store::AddDownloadPopup`, `src/store/store_payload.{h,cpp}`). `rbbz_download_panel`
  is defined in `store_info.dtb` — the record that carries the press — with the search modal's own
  resource (`../error/error_general.milo`), and its symbol is appended **last** to
  `store_info_screen`'s `panels` list (the position the search modal was measured at, and the reason
  its `all.grp` draws over the screen's panels). It carries two booleans and no string property:
  `active` (the popup is up) and `terminal` (the job already answered), the pair the search panel
  proved.
- **The press** stays P5's two cases, one statement longer: `DownloadRequest` now binds the offer's
  `job` key, builds the request path from that local, calls `{rbbz_download_panel show_popup}`, and
  only then writes `rbbz-store/download/start|again/<job>` — so the modal is up before the transfer
  starts, as the prompt asks. The `{offer has_data job}` guard and the compiled record's array
  binding are untouched.
- **The states**, all in the one panel object:
  - `show_popup` sets `active`, clears `terminal`, writes `error.lbl` = **Downloading from
    RhythmVerse...** and `continue.lbl` = **Press B to Cancel** (two new locale entries, added by
    `menu_payload.cpp` beside the search modal's), shows `all.grp` and takes the screen's focus;
  - `poll` waits on `rbbz-store/state/<job>/installed|failed|cancelled` (`store_download.h`'s
    grammar, built with `sprintf` — the shape the art poll uses) and switches the same labels
    **once**: `installed` → `Downloaded '<song title>'.` + **Press B to Close**,
    `failed`/`cancelled` → **The download could not finish.** + the Close prompt;
  - `hide_popup` releases `active`, hides, and gives the focus back to `store_info_panel`'s
    `buttons.lst`;
  - `BUTTON_DOWN_MSG`'s B writes `rbbz-store/download/cancel/<job>` while the job is not terminal and
    closes — so a cancelled transfer never outlives the press — and every other action is consumed;
    `exit` cancels the same way when the screen is left with the popup up.
- **Where the job token comes from, and why not a property.** The popup is a different object from
  the details panel, so the token is read from the panel's own `cur_offer` when it is needed:
  `{store_info_panel get cur_offer}` — the cross-object read retail's `go_back` already makes on
  every B press of this screen (`{store_info_screen get back_screen}`) — behind `{if $offer …}`, the
  object-truthiness test `AddSearchKeyboard` relies on for the keyboard's User (measured 2026-10-23).
  A string property was rejected because no shipped record sets one from DTA, and a method argument
  was rejected for the same class of reason (the token would still have to be stored between calls);
  the offer already carries it. The song title in the outcome sentence is read the same way and
  written with `set_display_text_string` — the writer `ShowOfferCredits` already draws with.
- **The overlay line is gone**: `DownloadStatusText`/`DownloadStatusJob`, the service's last-job
  bookkeeping (`last_job`/`last_finished`, `kStatusHold`, `NoteTerminal`) and `help_overlay.cpp`'s
  download branch were removed with it, so no host-side line describes a transfer any more. P7's
  catalogue sentences and the search strip are untouched, and the credit line stays (6d withdrawn).
- **Scope of this prompt, stated plainly**: the loading sentence is static - the popup reports that
  a transfer is running, not how far it has come (the live-status state was withdrawn, so no bucket
  text is owed) - and the installed state stops at the sentence, because the in-session refresh is
  **P9-14**'s. What this prompt owns is the panel, its switch between states, the cancel and the
  close, and it does not draw over the game: every string is one of the panel's own labels.
- **Verification**, hermetic: `cmake --build --preset win-amd64-debug` links the game and every
  target; `ctest` in `out/build/win-amd64-debug` is **47/47**; `store_payload` is **552 checks**
  (`TestDownloadPopup`: the definition, its resource, the eight methods, the three state predicates,
  the cancel command, the four tokens, the composed sentence, the panels-list position, idempotency,
  compile→parse and both refusals; `TestDownloadButtonsRouted` now also pins that each press shows
  the popup **before** it writes the request). Against the shipped record,
  `rb_blitz_store_payload_tests store-edit assets\game\360\ui\store\gen\store_info.dtb download`
  answers `download row -> edited`, `download popup -> edited`, `download press -> edited` and dumps
  the whole panel definition (evidence: `out/_swap/p9-6a-download.txt`).
- **Measured — the prompt's Done-when, by the owner's own boot (2026-10-24).** The owner pressed
  **Download** on a served offer on the release binary, the popup drew over the details page, the
  transfer ran, and its switch to the outcome sentence drew as recorded here and in
  `docs/engine/diagnostics.md`; the owner reports it working. Read by hand, no capture stored - the
  same standing as P9-5's, P9-7's and P9-9's owner boots. What stays as the standing check: the
  recipe below, and the harness's Store leg, which asserts both states (`popup.png` reading
  *Downloading* or *Downloaded*, `popup-done.png` reading *Downloaded … Close*, `popup-closed.png`
  showing it gone) but has not been re-run on this build. Recipe: main menu → **RhythmVerse** →
  **Recently Added** → A on an offer → **Download**; **B** at once cancels (log:
  `store_download: job … cancelled`, no package in the root, no receipt row, the popup closes);
  letting the transfer finish leaves the same panel reading *Downloaded '<song>'.* / *Press B to
  Close*, and **B** closes it with the details page focused again (its Exit row still returns to
  the browser).

**P9-6 close-out (2026-10-24; owner's boot) — P9-6 is Done.** The modal is the delivered answer to
the owner's 2026-10-23 design, and the owner confirmed it in game. The three follow-up states
withdrawn the same day are recorded once, in the preamble above; the one the owner still wants - a
refresh from the toast - is **P9-14**, and the credit line stays P7's.

### P9-7 — the store's Start button opens **Search Filters**, and Back returns to the store

- **Do**: the store's Start case jumps to retail's `options_screen` and sets its `back_screen` to
  `store_menu_screen` — two copies, `out/_swap/store.tree.txt:450-461` (front menu) and `:1061-1075`
  (browser). Re-point both at the **Search Song Filter** page and make **this flow's** Back the store
  front menu; the Help & Options route keeps `settings_dlc_screen` (`src/ui/settings_screen.cpp:1118-1120`),
  so the back target is per flow — author a second entry page whose Back is `store_menu_screen` or
  make the target runtime state, whichever the payload measures as possible. The footer label is the
  scene's inline help: set Start's token to a new locale string **Search Filters** with
  `store.ihp`'s `set_action_token kAction_Start <token>` (the retail idiom,
  `out/_swap/song_select.tree.txt:1821-1823`; the default token is `inline_help_options` =
  `HELP & OPTIONS`, `out/_swap/locale.tree.txt:732`). Do not re-point `options_screen` globally:
  Help & Options elsewhere must still reach the merged surface.
- **Move with it**: the payload edit (two DTA cases + one locale entry), a boot capture of the footer,
  `docs/settings/menu.md` if it claims the store's Start target, and the acceptance harness if it
  presses Start in the store.
- **Done when**: the store's footer reads `(Start) SEARCH FILTERS`, Start opens that page, Back
  returns to the store front menu **and only from this flow** (the Help & Options route still backs
  to Downloadable Content), and the master being off does not block the page (P6's rule).

**P9-7 record (2026-10-23) — landed in code and pinned hermetically; the boot capture is the
closing step.**

- **Where the button is, measured out of the shipped record** (dumped with
  `rb_blitz_settings_screen_tests dump-record assets/game/360/ui/store/gen/store.dtb`): the front
  menu's case is `({== $action kAction_Start} {options_screen set back_screen store_menu_screen}
  {ui goto_screen options_screen})` in `store_menu_panel.BUTTON_DOWN_MSG`, and the browser's is the
  same two statements inside `{do {$this turn_off_preview} …}` in
  `store_browser_panel.BUTTON_DOWN_MSG`. `store::RouteStoreStartToFilter` rewrites both:
  `settings_rhythmverse_filter_screen set back_screen store_menu_screen` then
  `{ui goto_screen settings_rhythmverse_filter_screen}`. Nothing else in either case changes.
- **No second entry page was needed — the back target is runtime state.** A settings screen's Back
  is `{ui goto_screen {{ui current_screen} get back_screen}}`
  (`src/ui/settings_screen.cpp`'s `Leave`), and Help & Options' own navigation re-points the
  destination at whoever sent it (`EnterScreen`). So the store's case sets the filter page's
  `back_screen` to `store_menu_screen` on its way in, and the Help & Options route sets it to
  `settings_rhythmverse_screen` on its own way in: one page, two return paths. The RhythmVerse
  page's own back stays `settings_dlc_screen`.
- **The footer caption is a scene property, not a DTA token — that is why the plan's own line was
  the only one.** Every compiled record in `assets/game/360` was dumped and searched for the bare
  locale token `store_offer`, and only the locale records carry it; the captions are baked into the
  scenes' `InlineHelp` panels (`store_menu.milo` carries `inline_help_options` and `store_offer`;
  `store_info.milo` carries `store_offer`; `store_browser.milo` carries neither, which is why its
  Option caption is the record's own `shortcut_header` or nothing). The objects are named
  `InlineHelp.ihp` (`store_menu.milo`), `store.ihp` (`store_browser.milo`) and `store_info.ihp` /
  `view_offer.ihp` (`store_info.milo`) — measured 2026-10-23 with
  `python scripts/hmx_milo.py list`. `store::SetStoreFooterStart` therefore writes
  `{InlineHelp.ihp set_action_token kAction_Start inline_help_search_filters}` and the same command
  on `store.ihp` for the browser — the retail idiom `song_select.dtb` already spells
  (`{InlineHelp.ihp set_action_token kAction_Start search}`). The token is a bare symbol and the
  locale entry `inline_help_search_filters` = `SEARCH FILTERS` is added to every locale beside the
  Download label; `SEARCH FILTERS` matches the retail caption table's own upper case.
- **The command belongs in the panel, not the screen — corrected after the owner's boot
  (2026-10-23).** The first cut appended both front-menu commands to `store_menu_screen.enter`;
  the owner's boot then showed the **browser** footer correct (`(X) SEARCH FILTERS`, no View Item)
  while the **store front menu** still drew `(X) HELP & OPTIONS` and `(Y) VIEW ITEM` — the
  browser's commands sit in `store_browser_panel.refresh_list`, a panel method, and the screen's
  `enter` runs before the panels' scenes are the objects a command resolves against (the same rule
  `settings_screen.cpp` records for retail scenes: "a statement in our own panel runs before
  `{ui goto_screen …}`, when the retail scene is not loaded yet"). `store_panel.enter`'s own
  `{microsoft.lbl set_showing FALSE}` is the measured precedent that a panel `enter` does reach its
  scene's objects. Both front-menu commands are now written into `store_menu_panel.enter` and
  inserted **first** in it, because that method ends in the shipped `{cond …}` whose default arm is
  `{fail …}`: a statement after that arm is one this build would be relying on `fail` not aborting
  to run. Measured against the shipped record (`store-edit <store.dtb> start`): the two commands
  are the method's first two statements, before `{store_panel add_sink …}`.
- **Refusals, not errors**: both transforms run in the payload's rhythmverse-gated store block and
  report a named refusal without touching the record (no `BUTTON_DOWN_MSG`, no `kAction_Start`
  case, a Start case that jumps somewhere else, or a missing `enter`/`refresh_list`). A second pass
  is idempotent: the destination is the marker (and the caption token is rewritten in place rather
  than appended twice).
- **Tests**: `store_payload` grew `TestStoreStartRoutesToFilter`, `TestStoreFooterStart`,
  `TestStoreFooterOptionCleared` and `TestDetailsFooterOptionCleared` (**376 checks** in the
  suite):
  both cases re-pointed with the browser's `turn_off_preview` kept, no `options_screen` jump left,
  the caption command on the scene's own objects with a symbol token, the clear sitting after the
  browser's shortcut gate and **first** in the front menu's `enter` (with the shipped `{fail …}`
  arm still present after it), idempotency, compile→parse round trips, and every refusal leaving
  the tree byte-identical. The `store-edit <store.dtb> start` diagnostic prints each verdict and
  dumps both `BUTTON_DOWN_MSG` methods, `store_menu_panel.enter` and the browser's `refresh_list`:
  measured against the shipped record, all three transforms answer **edited**, and the dump shows
  exactly the statements above.
- **Harness**: `scripts/acceptance_store.ps1`'s Store leg now asserts the footer reads
  `SEARCH FILTERS`, presses **Start**, waits for `settings_rhythmverse_filter_screen`, checks the
  five instrument rows, presses Back and asserts `store_menu_screen` — and asserts the same
  caption and the absence of `VIEW ITEM` on the browser and the details page (P9-8).
- **Docs**: `docs/settings/menu.md` claims no store Start target (checked 2026-10-23), so it needed
  nothing; the `store-edit ... start` mode is documented here and in the test's own header.
- **Owner retest recipe** (release build relinked 2026-10-23,
  `out/build/win-amd64-release/rb_blitz.exe`): main menu → **RhythmVerse** → the store front menu's
  footer reads `SEARCH FILTERS` beside the Start glyph (the owner's 2026-10-23 capture shows the
  glyph as `(X)`) and draws no `Y VIEW ITEM`; Start opens the page titled `SEARCH SONG FILTER`
  (Guitar / Bass / Drums / Vocals / Keys); Back returns to the store front menu; open Recently
  Added and the browser's footer reads the same caption with no View Item; A on an offer shows a
  details page with no View Item; and Help & Options → General → Downloadable Content →
  RhythmVerse → Search Song Filter still returns to the RhythmVerse page (the other flow). The
  **browser** half is already measured (owner boot, 2026-10-23); the front menu's is the half the
  first placement missed.
- **Not measured at this record's time**: the boot itself. The Done-when's OCR ("the footer reads
  `(Start) SEARCH FILTERS`, Start opens that page, Back returns to the store") is
  `scripts/acceptance_store.ps1 -Legs Store`'s new steps, which had not been run on a binary
  carrying this change. *(Superseded by the close-out below: the owner's boot is the measurement,
  and the harness's steps stay as the standing check.)*
- **Close-out (owner's boot, 2026-10-23) — P9-7 is met.** After the panel-enter correction the
  owner retested `out/build/win-amd64-release/rb_blitz.exe` (relinked 13:00) against the recipe
  above and confirmed it works: the store root's footer no longer reads `HELP & OPTIONS`/`VIEW
  ITEM`, Start reaches the `SEARCH SONG FILTER` page, and Back returns to the store front menu.
  Read by hand rather than stored as a capture (the same standing as P9-5's owner boot), on top of
  the browser half measured on the earlier boot of the same day. **P9-7 → Done.**

### P9-8 — hide **Y View Item** where it does nothing

- **Do**: the caption is the locale token `store_offer` = `VIEW ITEM`
  (`out/_swap/locale.tree.txt:2168-2169`). Measure what Y dispatches on the RhythmVerse screens with
  `store_trace`. The front menu's `kAction_Option` (`out/_swap/store.tree.txt:513-560`) is a
  trial-mode upsell or the banner marquee's offer (`store_banner_panel marquee_path` →
  `store_info_enum_panel request_enum_and_show_single_offer`), and the browser's (`:1080-1093`) only
  opens `show_shortcuts` when the focus is `song.lst`. We serve no marquee path and the browser's
  list is not `song.lst`, so on our surfaces the button is inert — which is what the owner sees.
  Remove the button **from our surfaces only**: the `kAction_Option` inline-help token on
  `store_menu_screen`, `store_browser_screen` and `store_info_screen` (`store.ihp`, `:850-856`), and
  the `shortcuts.lst` entry if it names `store_offer`. Nothing is removed globally.
- **Done when**: no `VIEW ITEM` is drawn on any store surface and Y has no effect there; a capture
  of the footer proves it, and the shortcut headers elsewhere are untouched.

**P9-8 record (2026-10-23) — landed in code and pinned hermetically; the boot capture is the
closing step.**

- **The caption's source, measured** (2026-10-23): no DTA record sets `kAction_Option` to
  `store_offer`. All records of `assets/game/360` were dumped and searched; the only store-record
  Option token is `refresh_list`'s `shortcut_header` for a provider that reports shortcuts
  (`out/_swap/store.tree.txt:849-856`), and the bare token `store_offer` exists only in the locale
  records (the store, inbox and recommendations records carry longer tokens such as
  `store_offer_fetch_panel`, `store_offer_unavailable` and `store_offer_new`, not the caption's).
  The caption is baked into the scenes: `store_menu.milo` and `store_info.milo` carry `store_offer`,
  `store_browser.milo` does not. That also says the three Y handlers the plan lists are inert for
  us (no trial mode, no marquee path, no shortcut provider, no recommendation band), which is what
  the owner saw.
- **Three clears, on the scenes' own objects**: `store::ClearStoreFooterOption` writes
  `{InlineHelp.ihp clear_action_token kAction_Option}` into `store_menu_panel.enter` (first
  statement, for the panel-versus-screen reason the P9-7 record measures) and
  `{store.ihp clear_action_token kAction_Option}` at the end of `store_browser_panel.refresh_list`
  — after that method's own shortcut-header gate, so a provider that ever reported shortcuts still
  cannot bring the caption back. `store::ClearDetailsFooterOption` appends the same clear for
  `store_info.ihp` and `view_offer.ihp` to `store_info_panel.enter`, because both inline-help
  objects belong to the details scene and which one draws the caption is not distinguishable from
  the outside. A clear of an action that is not set is safe: retail's own
  `store_offer_fetch_panel.enter` calls `{cancel.ihp clear_action_token kAction_Cancel}`
  unconditionally before anything sets that token.
- **Y's handlers are left in place, not removed.** They already do nothing on our surfaces (above);
  the plan's own instruction was to remove the caption, and removing dispatch cases would be an
  edit no measurement asks for. The browser's `shortcuts.lst` needs no edit either: it is filled
  from the provider's shortcut array, which this build never supplies.
- **Refusals and idempotency**: each transform refuses by name before any edit when its method is
  missing; the counter for an existing clear counts **direct children only**, so the browser's own
  nested clear inside the `has_shortcuts` else branch is not mistaken for this edit's work (that
  nested clear is exactly what a true branch could undo).
- **Tests**: the three cases in `store_payload` (above, **376 checks**) pin the commands on the
  right objects, the front menu's clear first in the method, the browser's clear after the gate,
  idempotency and the refusals; the bootstrap `store-edit <store.dtb> start` and
  `store-edit <store_info.dtb> info` diagnostics answer **edited** against both shipped records.
- **Not measured at this record's time**: the boot capture. `scripts/acceptance_store.ps1`'s Store
  leg asserts `VIEW ITEM` is absent from the front menu, the browser and the details page; it had
  not been run on a binary carrying this change. *(Superseded by the close-out below.)*
- **Close-out (owner's boot, 2026-10-23) — P9-8 is met.** The same retest the P9-7 close-out
  records confirmed the captions are gone from the store's own footer (the store root's `Y VIEW
  ITEM` is no longer drawn; the browser had already lost it on the earlier boot). Read by hand,
  no capture stored; the harness's absence checks stay as the standing check. **P9-8 → Done.**

### P9-9 — the LT/RT tabs work from **Your Songs**, and the RhythmVerse tab follows the master

**Partial implementation (2026-10-23):** P9-4's correction above enables the
song-list trigger cases offline and skips MESSAGES/RECOMMENDED while hidden.
The subsequent **P9-4 split-controls record** implements the master-dependent
tab visibility and the full ring for all eight states; that split's in-game
acceptance remains pending with the owner. *(Superseded the same day by the close-out
below: the owner confirmed the tabs on the rebuilt binary.)*

- **Measured starting point** (both dumps are in §8): the song list already carries the two cases —
  `out/_swap/song_select.tree.txt:1627` (`kAction_PageDown` → the store via `which_store_screen`) and
  `:1659` (`kAction_PageUp` → `recommendation_screen`) — but both arms sit under
  `{|| {rock_central is_login_blocked} {user_mgr is_participating_user_online}}`, and offline they
  take the `arrow_*.flow activate` arm: the arrow flashes, nothing moves. The store's own cases are
  the mirror image and already navigate (`out/_swap/store.tree.txt:464-497`: PageDown →
  `inbox_screen`, PageUp → `song_select_screen`; `:1023-1050` the same in the browser).
- **Do**: force the navigation arm of `song_select_screen`'s two cases, the way P1 forced the store's
  entry gate; leave the store's cases alone. The tab captions are scene art (`background_panel`,
  §5/P7), so the visibility rule is a resource/payload edit, not a list row: **MESSAGES and
  RECOMMENDED follow P9-4**, and **the RhythmVerse tab is hidden while the store master is off**
  (P9-1 makes it On by default).
- **Done when**: one boot walks Your Songs → RT → the store front menu → LT → Your Songs, with the
  highlight following the tab; with the master off the RhythmVerse tab is not drawn; with P9-4 off
  neither Messages nor Recommended is drawn; P9-7's Start/Back still behave from the store.

**P9-9 close-out (owner's boot, 2026-10-23) — P9-9 is met.** The owner confirmed on the rebuilt
binary that the song list's LT/RT tabs navigate as the split-controls record describes (Your Songs →
RhythmVerse → Messages → Recommended, each disabled tab skipped), which is the Done-when's walk and
the visibility rule in one reading. Read by hand, no capture stored — the same standing as P9-5's
and P9-7's owner boots. **P9-9 → Done.**

### P9-10 — warm **Recently Added** at boot; a failed press says so and retries

- **Why**: the owner sees the row "sometimes work, sometimes do nothing, sometimes crash". Today the
  first fetch starts when the store opens (`src/store/store_fetch.cpp:194-197`), so the row can be
  pressed into a request still in flight; the in-memory page cache already answers a second visit
  for ~5 minutes (`store_page_cache`), which is the "feels automatic" asked for.
- **Scope change (owner, 2026-10-22)** — this supersedes the **hidden flow** the prompt first
  carried (the main-menu row removed, the RhythmVerse tab hidden): **nothing is hidden on
  failure.** A warm that failed makes the press say so in the same popup P9-6b uses for its
  install notice — connection failed, try again later — and starts a fresh fetch in the
  background; if that fetch lands the next press is served the list, and if it does not the next
  press draws the same popup and asks for another fetch, and so on. No measurement depends on
  hiding, so that half was retired rather than softened.
- **Do**:
  1. **prewarm** — with the master on, once per boot after the guest is up, call
     `RequestCatalogueRefresh("the boot asked")` (`src/store/store_catalogue.h:62-68`; installed at
     `src/rb_blitz_app.h:361`). Same worker, same budget (`store_catalogue_pages`), and silent: no
     strip, no screen, no guest state. A failure lands in the catalogue's existing `kFailed` state
     with its backoff.
  2. **failure surface** — a press of the **RhythmVerse Customs** row while the catalogue holds no
     offers and its state is `kFailed` (`CatalogueStateToken()` = `failed`) does **not** enter the
     store: the row's case (P1's `AddRhythmVerseRow`, `src/settings/menu_payload.cpp`) shows
     `{error_popup_panel show_popup <token> splash_panel}` — the `error_popup_panel` `splash_screen`
     already carries, whose Ack closes it and returns the focus, and the very call retail's own
     `splash_dlc` case makes with `store_unavailable_not_online` (§2.1) — with a **new locale
     sentence** through `screen::AddLocaleEntry`: **RhythmVerse could not be reached. Try again
     later.** One panel, two sentences (P9-6b's install notice and this one), the same visual. It is
     drawn on **every** such press, not once per boot. Inside the store, P7's failure sentence still
     describes a walk that failed after it published offers (P9-6c keeps it); the popup is the
     press's answer when there is nothing to open.
  3. **the press retries** — in the same case, beside the popup, a
     `{write_file "rbbz-store/catalogue/retry" 0}` asks for a fresh background walk
     (`RequestCatalogueRefresh("a press asked")`: same worker, same `store_catalogue_pages` budget,
     silent). The read and the write use the bridge's established shapes — a one-bit `file_exists`
     predicate answered by the catalogue beside P1's `store::AnswerStoreQuery` probes in
     `src/hooks/settings_bridge_hooks.cpp` (`rbbz-store/catalogue/failed`), and a `write_file`
     command beside P4/P5's (`rbbz-store/catalogue/retry`). The case, in one shape:
     `{if_else {file_exists "rbbz-store/catalogue/failed"} {do {write_file
     "rbbz-store/catalogue/retry" 0} {error_popup_panel show_popup <token> splash_panel}} <P1's
     enabled gate and the store entry>}` — the retry first, the popup last, so nothing re-focuses
     after the panel takes the focus. **Single flight** already folds a burst of presses into the
     one running walk, and this **user-initiated** attempt must bypass P7's remaining backoff wait
     (`RequestCatalogueRefresh` refuses inside `g_retry_at`, `store_catalogue.cpp:454-465`), so the
     press path asks with that wait ignored — the automatic refetch keeps it and a failed retry
     restores it. When the walk lands the catalogue holds offers, so the next press is served the
     page: the owner's "unblocked" half. A press while a fetch is merely in flight (the prewarm, or
     a press-asked retry still running) is unchanged: the state is not `failed`, and the retail
     empty page is what the DTA already handles (the record below measured it).
  4. **the three outcomes** — reproduce each with `store_trace` before changing timing: "works";
     "nothing" (does the log show `store_catalogue: fetching` and then no page?); and the crash
     (capture its log and fault). This machine drops the GPU ring on roughly one boot in three
     (§5/P8's environment note), so a crash claim needs its own evidence.
- **Done when**: a boot pressed immediately opens the browser on offers, and its log carries no
  `store_catalogue: fetching` at press time; a boot pointed at an unreachable host draws the popup
  on the first press, **keeps** the main-menu row and the RhythmVerse tab, and its log carries the
  press-asked fetch; a second press with the site still down draws the same popup and starts
  another walk; a later press once the site answers (same boot, same row) opens the list; each of
  the three symptoms has a recorded cause or a recorded measurement that it is the environment.

**P9-10 record (2026-10-22) — item 1 landed: the catalogue warms at boot; items 2–3 are open.**

- **The symptom, measured — this is the owner's "sometimes do nothing".** Two consecutive boots
  (`rb_blitz_233.log`, `rb_blitz_234.log`) pressed **Recent** `1.7 s` and `1.1 s` after the store's
  front menu was served, and the page was answered **empty**: `store_fetch: served
  'dlc_store/europe/eng/recent/' from the built-in front menu (46 byte(s) …)` — `BuildMenuText({})`,
  no offers. Catalogue page 1 landed `1.0 s` / `1.9 s` **after** that press (`store_catalogue: page
  1 -> 24 eligible offer(s) of 25 record(s)`), so the press was simply early. An empty page is the
  shape the retail DTA already handles (`process_offers` with zero offers calls
  `refresh_current_menu`), which from the player's seat is "Recent does nothing"; the caption
  strip's loading sentence is the only sign, and the player in both logs left the store rather
  than pressing again.
- **Why it was late**: the first fetch started on the **front menu's serve**
  (`src/store/store_fetch.cpp`, `RequestCatalogueRefresh("the store opened")`) — the same frame the
  player starts pressing rows — and the site answers page 1 about 3 s later.
- **Landed**: `RbBlitzApp::OnPostLoadXexImage` (`src/rb_blitz_app.h`) calls
  `RequestCatalogueRefresh("the boot asked")` after the four store installations, so the walk starts
  before the guest thread exists. Same worker and the same `store_catalogue_pages` budget, and
  nothing is drawn: the strip only speaks inside `kCatalogueSurfaceWindow`, which starts when the
  store is served a record, never at boot. Master off stays silent (`RequestCatalogueRefresh`'s own
  gate), which is the Control leg's assertion.
- **Measured** (`scripts/probe_store.ps1 -StoreRow 4 -PageRow 0 -OutDir out/_swap/p910-recent2`,
  release tree, the owner's profile): `store_catalogue: fetching up to 4 page(s) (the boot asked)`
  at 19:52:06.6, all four pages by 19:52:15.9 (`99 offer(s) for the Recent list`) while the title
  was still on `server_connect_screen`; the store opened at 19:53:34.7 and Recent was served
  **75,229 bytes** (the 99 offers, not the 46-byte empty page) with its art served beside it, and
  `help_observer: screen 'store_browser_screen'` follows at 19:53:55.7 — the press opens the
  browser. The press-time log carries **no** `store_catalogue: fetching`, which is this prompt's own
  Done-when. A `-MenuOnly` boot (`out/_swap/p910-menu/20261022-194644`) shows the same warm-up with
  the store never opened.
- **Still open (items 2–3)**: no failure popup and no hidden flow when the catalogue cannot be
  reached, and the three outcomes are not each reproduced with `store_trace`. Until item 2 lands an
  unreachable site still answers the press with the empty page (the strip's failure sentence is what
  the player sees), and a press inside the ~3 s a **cold** fetch needs — a boot whose prewarm failed
  and whose backoff has expired, or a master turned on after boot — can still meet that empty page.
  *(Item 2 was re-scoped the same day by the owner — no hidden flow; a failed press draws the retail
  popup and retries in the background, see the prompt above — and nothing has landed for it yet, so
  the measurements in this bullet stand.)*
  *(The crash symptom is closed by the record below: it was the second visit's cover decode, not a
  store-visit race.)*

**P9-10 crash record (2026-10-22) — the owner's "sometimes crash" was the second visit's cover
decode, and it is fixed.** The owner's own boot (`rb_blitz_238.log`, 20:09:03, **no** diagnostic
switch) ended in `read of guest 0x00000004` the moment **Recent** was pressed on a **second** store
visit: the first visit (20:06:21) drew the catalogue, downloaded and installed a song, and the
re-entry (20:09:01) faulted before the serve hook logged anything.

- **Reproduced, and the art path is the trigger.** `scripts/probe_store.ps1` grew
  `-Reenter`/`-ReenterWaitSec` (leave the store, wait, walk back in, press the page row again). With
  covers served, the second press faults at the same address (`out/_swap/repro3` →
  `rb_blitz_241.log`); with `--store_art_off=1` the same two visits list the offers and no fault
  (`out/_swap/repro4/20261022-203302`).
- **Named, once the map matched the binary.** The old `rb_blitz.map` was a day older than the exe,
  so its frames named nothing real. `-DRBBLITZ_LINK_MAP=ON` (now a documented diagnostic) relinks
  the game target with `/MAP`; with it, the completion `sub_82445CD8` enters and never exits, and
  the decode `sub_824455A8` receives the **same arguments on both visits** (`a=0x45f18638
  data=0xaaa0`), which rules the page/parse out.
- **Cause.** The netcache insert `sub_824685E0` **refuses a path the container already holds** (it
  answers 0), and the file object stores the **caller's pointer** — `sub_82468248` writes `data` at
  `file + 56` and parses it in place. The second visit therefore decoded the **first visit's file
  object**, whose bytes were the boot's one shared guest buffer — overwritten since by a menu page
  and the other covers (`src/store/store_fetch.cpp`'s old `EnsureBuffer`). The decode followed the
  overwritten texture header into guest 0x4.
- **Fix.** One guest buffer **per served path per boot** (`RecordBuffer`, keyed by path): a path's
  bytes never change within a boot, a repeat serve reuses its own allocation, and no live file
  object ever sees another record's bytes. `src/store/store_fetch.h`'s chain note now carries the
  two measured facts (refused duplicate insert, pointer stored).
- **Measured after the fix** (`out/_swap/repro9/20261022-210411`, release binary, the owner's
  profile and a two-package library): both visits open Recent with the offers listed, both art
  completions exit `has_bitmap=1 width=256 height=256`, and the boot carries **zero** fault lines.
  `ctest` is **47/47**.
- **Also landed**: the probe's `-Reenter` route (its header names the crash), the
  `RBBLITZ_LINK_MAP` build option, and two trace lines in `docs/engine/diagnostics.md`
  (`art request …`, the completion's `wrapper=…`). The temporary step hooks used to bisect the
  completion (match/decode/upload) were removed again.

**P9-10 status (2026-10-23) — the boot half is done, the failure half is not.** *(Superseded the
same day by the two records below: items 2–3 landed, and the failure clauses were then measured in
the game against a loopback stub — the plan entry is **Done**.)* Since the two records above, the
phase stood at:

| Item | State |
| --- | --- |
| 1. warm Recently Added once per boot (`RequestCatalogueRefresh("the boot asked")`, `src/rb_blitz_app.h`) | **Landed and measured** (2026-10-22, release build: all four pages by the title screen, the press serves 75,229 bytes and opens the browser) |
| the "sometimes crash" symptom | **Closed** (second-visit cover decode fixed with one guest buffer per served path per boot) |
| 2. a failed press draws the connection-failed popup instead of the empty page | **Landed 2026-10-23 and measured** (two refusing presses, one boot: the retail popup over the main menu, the row still listed) |
| 3. the same press starts a fresh background walk (and a press inside P7's backoff bypasses the wait) | **Landed 2026-10-23 and measured** (`'rbbz-store/catalogue/retry' asked for a fresh walk` + `(a press asked)` on every press while refusing; the first press after the stub answered served 4 pages and opened the Recent list) |

So an unreachable catalogue still answered the press with the retail empty page plus the caption
strip's failure sentence (the P7 line), and a press inside a cold fetch's ~3 s still met that
empty page. P9-10 stayed **In Progress** on items 2–3, whose work was exactly the prompt's `Do`
bullets 2 and 3 above — landed, with the owed boots, in the record that follows.

**P9-10 record (2026-10-23) — items 2–3 landed: a failed press says so and asks for a fresh walk;
the closing boot is owed.** Nothing is hidden (the owner's re-scope above): with an unreachable
catalogue the main-menu row and the RhythmVerse tab stay, and the press answers with the retail
popup while a new walk starts in the background.

- **The row's case**, one shape in `AddRhythmVerseRow` (`src/ui/settings_screen.cpp`):
  `{if_else {file_exists "rbbz-store/catalogue/failed"} {do {write_file
  "rbbz-store/catalogue/retry" 0} {error_popup_panel show_popup rbbz_store_unreachable
  splash_panel}} {if {file_exists "rbbz-store/enabled"} {ui goto_screen store_menu_screen}}}` — the
  retry write first and the popup last, so nothing re-focuses after the panel takes the focus; the
  popup is the `error_popup_panel` / `splash_panel` pair retail's own `splash_dlc` case uses on this
  screen (§2.1), and the press after a success is P1's unchanged enabled gate.
- **The predicate** — `store::AnswerCatalogueQuery` (`src/store/store_catalogue.cpp`) answers
  `rbbz-store/catalogue/failed` true **iff** the catalogue is `kFailed` **and** holds no offers. A
  walk that failed *after* publishing offers therefore leaves the press alone: the strip's P7
  sentence still describes that visit, and P9-6c owns its wording.
- **The retry** — `store::HandleCatalogueWrite` claims `rbbz-store/catalogue/retry` and calls
  `RequestCatalogueRefreshNow("a press asked")`, the one worker with the same
  `store_catalogue_pages` budget and the same silence, reached through a `ignore_backoff` flag that
  skips `g_retry_at`. P7's automatic path (`RequestCatalogueRefresh`, boot prewarm and the store's
  own open) keeps the wait; a failed retry restores it. **Single flight** is untouched: `kFetching`
  returns before the backoff branch, so a burst of presses folds into the walk already running, and
  the master-off gate still refuses at the top of `RequestRefresh`.
- **The bridge order** (`src/hooks/settings_bridge_hooks.cpp`): the predicate is answered in
  `TakeExists` beside P1's `AnswerStoreQuery`, and the write is offered to `HandleCatalogueWrite`
  **before** `HandleDownloadWrite`, which would otherwise claim the whole `rbbz-store/` prefix.
- **The sentence** — `rbbz_store_unreachable` → *RhythmVerse could not be reached. Try again
  later.*, added beside `inline_help_search_filters` through `screen::AddLocaleEntry`, so a build
  with the store off carries neither token.
- **Pinned by tests**: `store_client` (**413 checks**) pins both path predicates exact (trailing
  slash, prefix, foreign namespace all refused); `settings_screen` (**141602**) pins the row's
  shape (the predicate path, the retry as `{write_file … 0}` before the popup, the popup's three
  arguments and `splash_panel` last, and the else branch as the enabled gate); `menu_payload`
  (**1692**) pins both locale entries to `state.store`. `ctest` **47/47**.
- **Still owed (the prompt's Done-when)**: a boot **pointed at an unreachable host** — no
  diagnostic switch covers this, `store_catalogue_off` leaves the state `kIdle` (so the popup is
  correctly *not* drawn) and the API base is a compile-time constant
  (`src/store/rhythmverse_client.h:47`), so the measurement needs the name blocked outside the game
  (a hosts-file mapping of `rhythmverse.co` to `127.0.0.1`, or the network down). What that boot
  must show: the first press draws the popup and logs `store_catalogue: fetching … (a press asked)`
  while the row and the tab stay; a second press draws the same popup and starts another walk; once
  the block is lifted the next press on the same row opens the store with offers. Until then the
  failure surface is **built and pinned, unmeasured in the game**. *(Superseded the same day: the
  instrument named in the next record — `store_api_base` and a loopback stub — measured all three
  clauses in one boot; the tab is P9-9's and was not part of that boot.)*

**P9-10 record (2026-10-23, second) — measured: the popup, the press-asked retry, and the recovery,
all in one boot.** The measurement needed a host that could fail *and then answer* inside one boot,
which the hosts file cannot give; so the boot got an instrument of its own.

- **The instrument — `store_api_base`** (Diagnostics cvar, requires a restart): an absolute
  `http(s)://host[:port]` origin with no path that the **list and search** requests are built against
  (`store::ApiBase` / `SetApiBaseOverride`, `src/store/rhythmverse_client.{h,cpp}`). Empty keeps
  `https://rhythmverse.co`; a value that is not a bare origin is refused at install and reported
  (`store_catalogue: store_api_base '…' is not an absolute http(s) origin; the requests keep '…'`).
  It moves **only** list/search: the download and artwork resolution stays same-site with
  `rhythmverse.co` (§3.1), so no diagnostic boot can be talked into fetching a package elsewhere.
  `tests/store_client_tests.cpp` pins both (the builders follow it, same-site resolution does not,
  every malformed value refused, the loopback spellings).
- **Plain http is loopback-only.** The transport refused `http://` outright on the first attempt
  (`run1`: "the transport only speaks HTTPS", before any socket), so `store_http.cpp` now allows
  plain http for a **loopback** host and nothing else, and `SetApiBaseOverride` refuses a
  non-loopback `http://` base for the same reason; both read one predicate (`IsLoopbackHost`:
  `127.0.0.1`, `::1`, `[::1]`, `localhost`). The package transfer
  (`store_transfer_winhttp.cpp`) stays strict and says why in a comment. `store_http.cpp`'s
  `WINHTTP_FLAG_SECURE` is now set from the scheme instead of always.
- **The stub and the probe**: `scripts/store_stub_api.py` answers every request `503` while a flag
  file exists and a captured real list page once it is gone (no `Retry-After`, so the wait in play
  is the catalogue's own); `scripts/probe_store_failure.ps1` starts it, boots
  `--store_api_base=http://127.0.0.1:<port>`, and drives the three presses, lifting the flag before
  the third. The payload is one real body captured with the client's own form body.
- **Measured (`out/_swap/p910-fail/run2/20261023-150750`, release build, the owner's profile)**:
  15:08:06 `the API base is 'http://127.0.0.1:8765' (store_api_base)` then
  `fetching … (the boot asked)` → `HTTP 503; the next attempt waits 60 s`; **15:09:31 press →
  `fetching … (a press asked)` + `'rbbz-store/catalogue/retry' asked for a fresh walk`** while the
  stub logs the request, and `popup.png` shows the retail popup over the main menu; **15:09:45 the
  second press → the same pair** (`popup2.png`), i.e. drawn on every press and not once per boot;
  the flag lifted, **15:10:09 the third press → four pages served, `4 page(s) -> 76 offer(s)`** and
  `serving.png` reads the retail front menu (`RECENTLY ADDED`, `YOUR SONGS`, `SEARCH`, the
  `SEARCH FILTERS` footer, `BACK`). Every menu capture after a popup still lists `RHYTHMVERSE`, so
  nothing is hidden. **Fault lines: 0.**
- **Measured again with the Recent row (`out/_swap/p910-fail/run3/20261023-151158`)**: the same
  three clauses (walks at 15:13:37, 15:13:51 and 15:14:01 while refusing; 15:14:14 serving), and
  `page.png` / `page-late.png` read the **served offers**: `HUWAG NA HUWAG MONG SASABIHIN`,
  `EVERYBODY'S GOT TO LEARN SOMETIME`, `MIGRAINE`, `SWEET TALK 101`, `NEWPORT LIVING`, `RENT TO
  OWN`, `FOR EVERY LIFE`, `AO TO NATSU`, `CUT E CORD`, with the details panel on `KITCHIE NADAL` /
  `POP-ROCK` / `NO PART` — the later press opens the list.
- **The sentence, from the owner's own boot** (same build, screenshot kept with this record): the
  message box reads **RhythmVerse could not be reached. Try again later.** with *Press A to
  Continue*, drawn by the retail panel over the main menu — the panel, the ack and the focus are
  retail's own, which is the point (`error_popup_panel` / `splash_panel`).
- **Note on the counts**: 76 offers is the stub's doing — it serves the same page for pages 1..4
  because it has no cursor, so the walk lists the same 19 eligible offers four times. The site's
  own walk measured 99 distinct offers on 2026-10-22; nothing about either number is a claim about
  the other.
- **Not measured here**: the RhythmVerse **tab**. It is P9-9's surface and follows the master
  switch (measured 2026-10-23); nothing in the failure path touches a record that draws it. The
  main-menu row, which the failure path *does* own, was read on every capture above.

### P9-11 — Search uses the title's own keyboard; the authored keys retire

- **Measured starting point**: the song list's Start opens the title's keyboard —
  `out/_swap/song_select.tree.txt:1753` `kAction_Start` → `virtual_keyboard show_keyboard user 100
  (localize search_for_song) (localize search_for_song_desc) this`, with `search_mode` and
  `song_search.lst` beside it (`:1769-1823`). **P4's record contradicts this** — its scan of
  `main_xbox_0.ark`'s 131 records found only `rbworld_link_panel`'s `show_keyboard`. Resolve that
  first: name the ark the dump came from (the owner sees the search in Ultimate; the default target
  is `common`) and confirm the route under both builds.
- **Do**: the store's Search row opens that keyboard instead of the authored key pages
  (`settings_rhythmverse_search_*`, P4), and the answer reaches `store_search`'s existing submit
  (the field rules stay: >= 3 characters, one value, artist **or** title, never combined; the
  caption strip's query/count sentences stay). The authored pages leave the flow — delete them or
  leave them unreachable, and say which; the surface does not grow a second keyboard. If a target
  without the facility matters, keep the authored keys behind the same row and state the fallback on
  the row's help line.
- **Move with it**: `store_search`'s key-token grammar keeps only what the typed field needs;
  `settings_screen`'s key-page pins drop; the seed diagnostic (`scripts/probe_search.ps1`,
  `store_search_diagnostic`) moves to the new entry.
- **Done when**: store → Search draws the title's own popup, a typed query answers in the strip and
  fills the browser, Cancel returns to the front menu, and the authored key pages are gone or
  provably unreachable.

**P9-11 record (2026-10-23) — the authored key pages are gone and the store's Search row opens the
title's own keyboard; the in-game typing is the one half this environment cannot measure.**

- **The contradiction, resolved — the dump was from the Ultimate build.** `out/_swap/song_select.tree.txt`
  is the record in `assets/game/360/…`, which `scripts/extract_assets.ps1` writes as *base plus the
  Ultimate overlay*; P4's scan read `game/gen/main_xbox_0.ark` alone. Re-running
  `out/_swap/scan_dtb_tokens.py keyboard show_keyboard` (with UTF-8 output — the 2026-10-20 run died
  on the French locale's first hit, which is why its report named only the first records it printed)
  gives **131 records, 20 with a hit**, and the only `show_keyboard` in the **retail** ark is
  `ui/rbworld_link/gen/rbworld_link.dtb`. The Ultimate patch's own entry list
  (`python scripts/hmx_ark.py list --hdr game/ultimate/gen/patch_xbox.hdr -v`) carries
  `ui/song_select/gen/song_select.dtb` at 32,554 bytes against retail's 19,934 — the overlays are
  exactly the difference. So P4's measurement was right about the *base* target and P9-11's starting
  point is right about the *installed* one.
- **The keyboard is native and in the retail image, so both builds can show it.** The RTTI table
  (`python out/_swap/rtti_vtables.py VirtualKeyboard`) puts `.?AVVirtualKeyboard@@`'s vtable slot 5
  at `sub_8237AD10`, whose only literals are `show_keyboard` and `clear_callback` — the class's
  `Handle`. `.?AVVirtualKeyboardResultMsg@@` exists beside it, and `sub_822CE5F8` interns the message
  name while `sub_8237A878` builds it from `(ok, text)`, whose two symbols sit in the engine's name
  block (`0x37A97`). The object name `virtual_keyboard` is a symbol at `0x5F8F8` with no code
  reference: it is registered by the UI manager, which is why the Ultimate record can call it
  without creating it and why retail's `song_select` simply never does.
- **The route, in the record the store already owns.** `store::AddSearchKeyboard`
  (`src/store/store_payload.{h,cpp}`) writes two script methods onto `store_menu_screen`:
  `(open_store_search {do {write_file "rbbz-store/search/open" 0} {if_else {file_exists
  "rbbz-store/search/seed"} {write_file "rbbz-store/search/done" 0} {virtual_keyboard show_keyboard
  user 100 (localize search_for_song) (localize search_for_song_desc) "" $this}}})` and
  `(VIRTUAL_KEYBOARD_RESULT_MSG {if $ok {do {write_file {sprintf "rbbz-store/search/text/%s" $text} 0}
  {write_file "rbbz-store/search/done" 0}}})`; the same call appends
  `{write_file "rbbz-store/search/leave" 0}` to `store_menu_panel.exit`, so leaving the store stops
  the strip. `store::RouteSearchRowToKeyboard` replaces P4's `RouteSearchRowToKeys`: the Search
  row's gate keeps `{file_exists "rbbz-store/search/show"}` → retail's own request, and its
  no-answer arm is now `{{ui current_screen} open_store_search}`. `$this` in the screen's method is
  the screen object, which is why the keyboard's result lands on the surface that opened it.
- **Measured against the shipped record, before any boot** (`rb_blitz_store_payload_tests store-edit
  assets\game\360\ui\store\gen\store.dtb search`): `search keyboard -> edited`, `search row ->
  edited`, and the printed tree carries `{{ui current_screen} open_store_search}`, the
  `open_store_search` method with `rbbz-store/search/open`, the `rbbz-store/search/seed` gate,
  `virtual_keyboard show_keyboard`, and `VIRTUAL_KEYBOARD_RESULT_MSG` with
  `rbbz-store/search/text/%s` + `rbbz-store/search/done`, plus `rbbz-store/search/leave` in the
  panel's exit.
- **The transport keeps only what the typed field needs.** `ParseStoreWrite` (`store_search.cpp`)
  now reads `open | done | cancel | leave | text/<raw UTF-8>`; `key/<token>` and the hex spelling are
  **refused** (`unknown search action`) and `SearchText` lost `AppendKeyToken`/`Backspace` and the
  five key constants. The bridge's `ArgumentString` admits bytes ≥ 0x80 (control characters and DEL
  are still refused), so a typed accent survives; every other namespace matches ASCII tokens only.
  `AnswerSearchQuery` answers `show` and `seed` and nothing else — the key pages' `open`, `ready`,
  `state/<token>` and `count/at-least/<n>` probes retired with them.
- **The authored pages are deleted, not hidden** — ten pages per context, so the page count moves
  71 → 61 (142 → 122 panels, `settings_screen` pins updated), and `store_search_diagnostic`, its
  locale work and the main-menu destination branch are gone with them. The seed stays and **moves to
  the new entry**: the screen asks `rbbz-store/search/seed`, which is true only while a boot's
  `store_search_seed` is in the field (typing, submitting or cancelling retires it), so a scripted
  boot's Search press submits instead of drawing a keyboard it cannot type on. `probe_search.ps1`
  and `acceptance_store.ps1` drive that route now; `-SearchMenuRow` defaults to 1 and a negative
  value throws rather than pretending the key route exists.
- **The sentences followed the route.** The answer hint is `Press Search to list them.` (there is no
  Back step any more: the keyboard closes onto the same menu), and the keyboard's own title and
  description are new locale entries `search_for_song` = `SEARCH` and `search_for_song_desc` =
  `Search by artist or song title, one at a time.` — retail's locale carries neither, so the Ultimate
  payload's tokens would have drawn bare names.
- **Tests**: `ctest` **47/47** in `out/build/win-amd64-debug`; `store_search` **287** (the raw-text
  grammar, the retired paths refused, the seed bit, the one-bit queries, the new hint),
  `store_payload` (the keyboard methods, the routed gate, the dismissal, idempotency, refusals, a
  compile/parse round trip, and the printed diagnostics), `settings_screen` **107,644** with the
  new page counts and no key-label exception left.
- **Not measured (the prompt's Done-when, in the game)**: store → Search drawing the title's own
  keyboard, typing on it, and Cancel returning to the front menu. A script cannot type on the
  console keyboard and this machine cannot drive it, so the owner retests on the release build
  (`out/build/win-amd64-release/rb_blitz.exe`, relinked 2026-10-23): main menu → **RhythmVerse** →
  the store → **Search** → the keyboard draws over the store menu → type a query → OK → the strip
  reads the matches → the same row lists them → Cancel (OK with an empty field, or the keyboard's
  back) returns to the front menu. `store_search_seed` must be empty for that boot, or the keyboard
  is skipped by design.

**P9-11 follow-up (2026-10-23) — the crash the owner hit, the one-press flow, and the Filters rename.**

- **The crash, named and fixed: the keyboard's first argument was a symbol, not a User object.**
  The owner's boot (`out/build/win-amd64-release/logs/rb_blitz_271.log`) ended in `Unhandled guest
  access violation: read of guest 0x00000000` 150 ms after `store_search: 'rbbz-store/search/open'`.
  `sub_8237B008` (the show the keyboard's `show_keyboard` handler reaches) reads the **player's name**
  out of its first argument before it calls the platform keyboard — `r19 = (*(void**)(*arg0))(arg0)`
  — so the record's bare `user` symbol resolved to no object and the call read address 0. The fix is
  the shape this build's own records use: bind it first, `{$user {user_mgr get_participating_user}}`,
  with `{user_mgr get_user_from_pad_num 0}` as the fallback and a `leave` write when neither answers
  (the store is reachable in both the front-menu gate's and the offline case). Measured on the next
  boot: the keyboard draws (`SEARCH SONG` + the Ultimate locale's description + OK/Cancel over the
  store page) and no fault line appears.
- **The result handler's name and shape were also wrong.** `sub_8237A878` interns
  `virtual_keyboard_result_msg` (lowercase; `macros.dtb`'s `VIRTUAL_KEYBOARD_RESULT_MSG` is the macro
  that expands to it) and sends `(ok, text)` **positionally**, so the record's method must carry a
  parameter list: `(virtual_keyboard_result_msg ($ok $text) …)`, the shape `store_info.dtb`'s
  `(preview_download_complete_msg ($bSuccess) …)` uses. The traced boot
  (`out/_swap/search-probe/20261023-171029`, instrumented with one `rbbz-store/search/trace/<n>`
  write per step, since removed) showed the body taking the participating-user arm and reaching the
  keyboard.
- **One press now opens the results.** Confirming the keyboard submits (`text/<query>` + `done`)
  **and** runs the shipped Search row's own three statements — `{store_panel set_source menu_search
  TRUE}`, `{store_panel request "/eng/search/" TRUE}`, `{store_panel update_loading_status}` — written
  out because a screen method has none of the row's locals; `"/eng/search/"` is exactly
  `MenuProviderPath(default locale, "search")`, the string the row itself would pass. The retail
  browser only opens when the page carries offers (`store_menu_panel.process_offers`), so the host
  holds that request until the first API page of the answer lands (`store_search_wait_ms`, default
  4000; the wait is bounded and never paid by a query the page cache holds). Measured
  (`out/_swap/search-probe/20261023-175633`): `cache request 'dlc_store/europe/eng/search/'` at
  17:58:29.008 → `the search page waited 2730 ms for the answer` → `the served page carries 15
  offer(s)` → `screen 'store_browser_screen'` at 17:58:31.927, with the OCR reading the Bush list
  (`THE PEOPLE THAT WE LOVE`, `ARMY DREAMERS`, band `BUSH / GOLDEN STATE`). The `leave` line that
  follows `open` is the store menu panel's own `exit`, which the engine runs while a system dialog
  (the keyboard) is up — it only dismisses the strip.
- **Renames (owner request, same pass).** The store's Start caption is `FILTERS` (was `SEARCH
  FILTERS`; the OCR of the same boot reads it in the footer), the page it opens is titled
  `RHYTHMVERSE FILTERS` (was `SEARCH SONG FILTER`), and the RhythmVerse page's row is `Filters`.
  Pins updated in `store_payload`, `settings_screen` and `acceptance_store.ps1`.
- **Tests**: `store_payload` **421**, `settings_screen` **107,627**; the payload suite pins the User
  binding, the lowercase handler with its parameter list, and the page request in both the keyboard's
  and the seed's arm.
- **Still the owner's to measure**: typing on the keyboard in a player's boot (this environment can
  drive the platform dialog only when a script sends host keystrokes into it) and whether the
  ~2.7 s hold before the results page reads acceptably; the store page stays on screen for it, and a
  deferral that lets retail's own LOADING draw instead is the next step if it does not.

**P9-11 input/loading follow-up (2026-10-23).** The owner's typing pass found keyboard input
also navigating the store and rejected the nonmodal loading feedback. The input filter now
neutralizes controller, keyboard and mouse input while XAM UI is active, skips gameplay/capture
dispatch, and consumes the closing press through release. The separate native mouse queue is
also disabled during system UI.

Submitting Search now opens `rbbz_search_loading_panel`, using the retail error popup's
`../error/error_general.milo` visual with **Searching RhythmVerse...** and **B to Cancel**.
It owns focus and consumes all button actions. B/Back invalidates the search generation and
restores the store list; late responses cannot open a page. The panel's native `poll` waits for
the entire API page walk, then hides the popup and requests the completed results directly.
Empty, short and failed searches show a terminal notice. The blocking `store_search_wait_ms`
path is removed, and the loading sentence no longer appears in the caption overlay. Confirming
empty text replaces the previous query instead of resubmitting it; the keyboard's length limit
now matches the search session's 64 characters.

Validation: release build and the `input_gates`, `store_search`, `store_payload` and
`settings_screen` suites. Regression coverage includes input release gating, partial pages staying
busy, cancellation rejecting late results, terminal notices and the modal's focus/button contract.
**Live UI verification remains pending the owner's testing choice** under `AGENTS.md`.

**P9-11 repeat-search/art follow-up (2026-10-23).** The owner verified the keyboard/loading
changes in-game, then reported that only the first search song showed art and a second search
still displayed the first query's list. `rb_blitz_280.log` confirms that the API returned 98
eligible Linkin Park offers and later 7 Walk the Moon offers, while the shared search page path
was reused. The native file-cache insert refuses duplicates: rewriting the source buffer leaves
the first parsed DataArray in place. Served text records now use fresh parser-owned buffers and
reparse the existing cached file before its normal single delivery. The replaced array's reference
and the cache's byte count are updated using the native ownership rules; art files keep their
separate persistent raw buffers.

The art log also shows covers completing after the results page was served. `KeepOnlyHeldArt`
had removed every cold cover path from that page, so those images could never be selected even
after encoding. Pages now retain every valid path. Selection requests its cover with priority
over background prefetches; `store_details_panel.poll_store_art` checks readiness and links only
the current path after its bytes are available. There is no blocking wait and no stale cover
assignment. Missing/failed art keeps the placeholder.

Validation: release build; `menu_payload`, `store_payload`, `store_serve`, `store_art` and
`store_search` suites. Regression checks cover repeated parsed-page replacements, growing and
shrinking cache accounting, array ownership, cold art paths, and independent readiness for
different selected covers. The new in-game behavior still needs the owner's retest.

**P9-11 close-out (owner's boot, 2026-10-23) — P9-11 is met.** The owner drove the keyboard route
end to end in game and reported everything good: the store's **Search** row draws the title's own
keyboard, a typed query submits once, the modal **Searching RhythmVerse...** popup owns the screen
(B cancels), the answer lists in the retail browser with its covers, and a second search lists its
own answer instead of the first query's. Read by hand, no capture stored — the same standing as
the P9-5, P9-7 and P9-9 owner boots. **P9-11 → Done.**

## Appendix A — request cheat sheet

```powershell
# Recent (rb3xbox only, newest first)
curl.exe -s -X POST "https://rhythmverse.co/api/rb3xbox/songfiles/list" `
  -d "instrument[]=drums" -d "instrument[]=guitar" -d "instrument[]=bass" -d "instrument[]=vocals" `
  -d "sort[0][sort_by]=update_date" -d "sort[0][sort_order]=DESC" `
  -d "data_type=full" -d "page=1" -d "records=25" -H "X-Requested-With: XMLHttpRequest"

# Search (>=3 chars): artist OR song title alone; do not combine both.
curl.exe -s -X POST "https://rhythmverse.co/api/rb3xbox/songfiles/search/live" `
  -d "text=halsey" -d "instrument[]=guitar" -d "instrument[]=vocals" `
  -d "data_type=full" -d "page=1" -d "records=25" -H "X-Requested-With: XMLHttpRequest"

# Metadata/header probe for an already validated direct URL (URL-encode its filename).
# HEAD may be unsupported; the runtime must still check GET headers and bytes (§3.1).
# Never add -L: each redirect target needs validation before following it.
curl.exe --head --max-time 20 --max-redirs 0 `
  "https://rhythmverse.co/download_file/<user_folder>/<file_id>/<encoded_file_name>"
```


**2026-10-24 — Recently Added crash correction.** The last run (`rb_blitz_299.log`)
served 98 offers and faulted before delivery returned. The same fault occurred with
24 offers in `rb_blitz_298.log`. The native StoreOffer constructor (`sub_824544E0`,
`0x82454710..0x82454780`) consumes the reserved `release_date` field as three
integers (year, month, day). P9-13 wrote a date string under that name, which made
that constructor read beyond the field and dereference null. Both metadata writers
now use `rbbz_release_date` for the credit string, and the credit-panel transform
reads that key. Regression checks reject the reserved key in text and compiled
records. The release build and focused host tests pass; the owner chose to perform
the in-game validation.


**2026-10-25 � Installed custom management.** The details page now queries installed
ownership when opened and while the index changes, matching normalized artist and
song title across all scanned DLC packages, including manual installs in the shared writable content root. Matching
folds Unicode case and tolerates Latin-1 metadata. The metadata reader also descends
into anonymous outer lists; previously those packs declared no songs. The index
starts at boot, and a selected offer shows Checking installed songs while it builds.
Package-list snapshots and invalidation generations prevent a concurrent refresh
from publishing an obsolete ownership index.

An owned offer shows **Delete**. A guest dialog confirms removal of every matching
package and warns that other songs in those packages are included. Successful
removal immediately returns the action to **Download**. Deletion errors retain the
remaining ownership and show an error; partial removals still require a refresh.
Leaving the details page after deletion without a replacement download offers
**Refresh** or **Ignore**, warning that playing without refreshing could crash.
Refresh starts the host rescan and the existing guest discovery route. Ignore
finishes the leave and dismisses that warning until another deletion. Receipts
remain download history and never determine ownership.

Validation: release build, host service regressions with temporary packages, metadata
and index regressions, and authored dialog compile/parse tests. In-game interaction
still requires an owner test; no game navigation was performed for this change.
