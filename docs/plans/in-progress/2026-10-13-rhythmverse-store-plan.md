# RhythmVerse in-store integration — research + implementation plan

Date: 2026-10-13. Reviewed: 2026-10-14, 2026-10-20. Status: **in progress — P0, P6, P1 and P2
landed 2026-10-20**, and **P3's serial half (step 0) landed the same day**: the request chain is
mapped (`sub_827501F0` → `sub_82753010` → `sub_8274FB60` → the file cache), both entries are
hooked, and the store's own DTA record is proven editable — the panel still issues no request
offline, so the data lane starts at the gate (§5/P3 step 0 record). **P5's pipeline landed the same
day, hermetically tested and not yet driven by a boot** (§5/P5 record: nothing registers offers or
starts a job until P3/P4 wire the list and the buttons). **P4 landed the same day and is held open
by exactly one line of its exit:** the field, the authored keys, the request, the answer and the
two authored sentences are boot-measured, but the **matches are only held, not yet drawn** — the
list they belong in is P3's front menu (§5/P4 record). P1's browser acceptance is met
on one boot (row drawn, store opens, B returns) and still owes the multi-boot/toggle matrix. Audio
preview is decided: the API returns no preview asset (§3.3), so v1 shows none.
Scope: wire the RhythmVerse customs catalogue into screens based on the retail store so that
custom songs can be browsed, searched, and downloaded in-game, with instrument filters in the
in-game settings.

Related reading: [dlc.md](../../guides/dlc.md) (content pipeline),
[song-cache-plan.md](../finished/2026-10-10-song-cache-plan.md) (R7, refresh),
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
   is `{if_else {user_mgr is_participating_user_online} <enter store> {error_popup_panel show_popup
   store_unavailable_not_online splash_panel}}`. In this checkout, R7's `TapRefreshCache` replaces
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

| Work | Reference | What execution must account for |
| --- | --- | --- |
| Settings rows | [settings_screen.cpp](../../../src/ui/settings_screen.cpp), `AllPanels()`/`InSongPanels()`, `settings_dlc_panel`, `BoolRow`/`SubmenuRow` | Extend the existing DLC page and add the filter child, including its pause-context twin and Back target. A schema group alone does not generate these authored rows. |
| Settings contract | [model.h](../../../src/settings/model.h), [model.cpp](../../../src/settings/model.cpp), [schema.cpp](../../../src/settings/schema.cpp), [hot_apply.h](../../../src/settings/hot_apply.h) | Allocate six unused stable IDs without renumbering; add tokens/defaults/storage mappings and a real live applier. Merely marking rows `kHot` falls back to restart when no applier handles them. |
| Store/refresh collision | [menu_options.cpp](../../../src/ui/menu_options.cpp), `TapRefreshCache`; [menu_filter.cpp](../../../src/hooks/menu_filter.cpp), `PatchFile`/`Configure`; [menu_compat.h](../../../src/settings/menu_compat.h) | `splash_dlc` is the R7 refresh token and protected from the default offline hide set while it owns refresh. Use a distinct proposed `rbbz_rhythmverse` token; do not change that ownership. |
| Authored menu/payload | [settings_screen.h](../../../src/ui/settings_screen.h), `RerouteRow`/`RemoveRow`/`AddLocaleEntry`; [menu_payload.cpp](../../../src/settings/menu_payload.cpp), `BuildMenuRecords`/`ExtendMenuArchive`; [settings_payload.cpp](../../../src/hooks/settings_payload.cpp) | `RerouteRow` only replaces an existing dispatch case. Add an idempotent main-menu insertion helper for both option and case, and compose edits into the existing mounted payload. On/Off needs runtime list repopulation, since records are built at boot. |
| Guest bridge | [settings_bridge_hooks.cpp](../../../src/hooks/settings_bridge_hooks.cpp), `sub_8241DFB0` / `sub_8241DE48`; [settings_bridge.h](../../../src/hooks/settings_bridge.h) | Builtins `file_exists` and `write_file` are hooked already. Current readback is **one bit**, not arbitrary strings; state/progress need predicates or an explicitly implemented typed return extension. Search text also exceeds the current ASCII token grammar. |
| DLC inventory | [dlc.h](../../../src/hooks/dlc.h), `LibraryPackagePaths`; [dlc.cpp](../../../src/hooks/dlc.cpp), `RebuildPackagePaths`/`ScanLibraryForRefresh` | Package paths cover configured structured + flat sources, but contain no song title/artist and are not a guest loaded-song list. Copy on the owning thread before worker use; add source coverage for normal content-manager installs if using this fallback. |
| Package metadata fallback | [shell_music.cpp](../../../src/hooks/shell_music.cpp), `MountPackage`; [stfs_container_device.h](../../../rexglue-sdk/include/rex/filesystem/devices/stfs_container_device.h), `Initialize`/`ResolvePath`; [file.h](../../../rexglue-sdk/include/rex/filesystem/file.h), `ReadSync` | The project already mounts STFS and extracted packages read-only. Reuse that reader for `songs/songs.dta` rather than implementing an STFS extractor from scratch. A textual DTA parser and loaded-content association still need work. The menu-music pool itself is not the metadata index. |
| Header validation/layout | [dlc_library.h](../../../src/fs/dlc_library.h), `ReadPackageIdentity`, `kMaxContentFileName`, `LibraryContentType`; [dlc_layout.h](../../../src/fs/dlc_layout.h), `HasStructuredLayout` | Scanner accepts `CON `, `LIVE`, `PIRS`; 4-byte magic alone is insufficient. Guest filenames have a **42-byte** limit. Flat libraries get aliases for long names; structured paths keep the actual filename. Use short generated install names, not artist/title filenames. |
| Paths/refresh | [paths.cpp](../../../src/settings/paths.cpp), `ResolvePathValue`; [dlc.cpp](../../../src/hooks/dlc.cpp), `Configure`/`RefreshConfigured`/`PublishPendingRefresh`; [refresh_state.h](../../../src/fs/refresh_state.h) | Reuse the boot's resolved DLC destination. `paths.toml` relative paths and legacy cvar relative paths have different bases. Refresh is cache-toggle-gated, skips an empty flat-root list, and does not rebuild structured paths. Test cache On and Off. |
| HTTP | [download.cpp](../../../installer/src/download.cpp), `InternetHandle`/`DownloadToFile`/`SplitUrl` | RAII handles, explicit timeouts and a proxy-aware access type are useful. Note the file's own reason for following GitHub's redirect: `WinHttpSetOption(..., WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS, ...)` for release assets. **Do not copy that policy** for RhythmVerse; validate each redirect manually. (This row used to cite `controller-settings/src/updater.cpp`, which was deleted on 2026-10-15 with D19's update check.) |
| JSON dependency | [vendored json.hpp](../../../rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp) | A JSON implementation already exists in the SDK tree. Verify the build target/include/license arrangement before adding another dependency; do not write a partial JSON parser. |
| Existing regression suites | [settings_screen_tests.cpp](../../../tests/settings_screen_tests.cpp), [settings_hot_apply_tests.cpp](../../../tests/settings_hot_apply_tests.cpp), [menu_compat_tests.cpp](../../../tests/menu_compat_tests.cpp), [dlc_library_tests.cpp](../../../tests/dlc_library_tests.cpp), [dlc_layout_tests.cpp](../../../tests/dlc_layout_tests.cpp), [refresh_state_tests.cpp](../../../tests/refresh_state_tests.cpp) | Extend the relevant fixtures during implementation: exact menu defaults/navigation, live commit/cancel, refresh-row ownership, staging exclusion, layout preservation and publish-once refresh behavior. |

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
| Preview fields (kept, never fetched) | `file.file_preview` and `data.song_preview` are YouTube web pages; there is no audio asset. Parsed onto the offer model for evidence and classification only — §3.3. |
| Form keys | Sent percent-encoded (`instrument%5B%5D=guitar`, `sort%5B0%5D%5Bsort_by%5D=update_date`); measured 2026-10-20 with `records=1` — HTTP 200, `status=success`, one `rb3xbox` record (`out/_swap/rv_p0_encoded.json`). Literal brackets also work, but the encoded spelling is what the client emits. |
| Download | `file.download_url` is a candidate, not proof of a raw package. Apply §3.1. The scoped `rb3xbox` route also returns external links (Dropbox observed on 2026-10-14). |
| Search rule | a too-short query returns `{"status":"error","error":{"message":"You need to provide a text at least 3 characters long"}}` |
| Search matching | Artist alone OR song title alone; combining both in `text` fails to find the intended result (owner-reported, 2026-10-14; not independently re-probed). This is not a one-word restriction. |

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

| Field | Content | Count |
| --- | --- | --- |
| `file.file_preview` | `https://www.youtube.com/embed/<id>` | 14/25 in `list.json`, 4/8 in `search.json` |
| `data.song_preview` | `https://www.youtube.com/watch?v=<id>` / `https://youtu.be/<id>` | 0/25 in `list.json`, 4/8 in `search.json` |
| `file.audio_type` | `single` / `partial` / `full` — a chart description, not an asset | every song |

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

| Component | First choice | Fallback |
| --- | --- | --- |
| Store opens | New `rbbz_rhythmverse` row/case, labelled RhythmVerse Customs and runtime-gated by the persisted enable setting; preserve `splash_dlc` refresh | store-context entry stub if commerce state prevents entry; never change global signin |
| Menus ("Recent", "Search") | A: generated top-menu metadata | B: hook `get_menu_provider`/`request` |
| Offer list + fields | A: generated offer records | B: hook provider `build_list` + offer `get_data` |
| Album art | A: serve art files the same way (`load_art` path) | pre-convert + feed `album_tex`; last resort placeholder `no_art.tex` |
| "Downloaded" | lazy loaded-DLC artist/title index (§3.2), adapted to `is_purchased`/`in_library` only as UI signals | package-metadata index over accepted DLC sources; still required in v1 |
| Checkout/download | DTA patch of the two button cases + `rbbz-store/` bridge ops | hook `check_out`/`re_download` |
| Free UI / attribution | guard `refresh_panel` cost block, author download states, RhythmVerse heading | provider/resource edits for prices rendered outside DTA |
| Refresh after download | rescan host sources + publish + guest discovery (OQ6) | verified install plus restart-required message; manual refresh alone is not yet proven sufficient |
| Enable + instrument filters | six native settings leaves under General → Downloadable Content; copied effective values for workers | — |

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
- **Open P1 defect found by the measurement**: the store entry is drawn *last*, after EXIT GAME,
  because the only selectable position is the retail store row's own (`splash_dlc` follows
  `splash_exit` in the shipped array) and inserting before `splash_exit` produced a row whose label
  drew and whose navigation was skipped. Placing it above EXIT needs a runtime reorder, not a
  different insert index; see P7.

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
  `… splash_exit splash_dlc rbbz_rhythmverse` — and the locale carries `rbbz_rhythmverse =
  "RhythmVerse Customs"`, yet the menu did not draw it. The row *was* selectable: six taps past the
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
     position measured is the retail store row's own: appended after `splash_dlc`, i.e. last.
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

Two boots under `scripts/probe_store.ps1`, both with `--store_trace=1
--store_local_server_root=ui/store/remote_metadata/`: `out/_swap/store-probe/20261020-142448` (the
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
  keeps the record's field names in one place because they are still a guess. `tests/
  store_metadata_tests.cpp` pins the names, the refusals and the compile→parse round trip — never
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

| Step | Where | What |
| --- | --- | --- |
| method dispatch | `sub_827501F0` | `BandStorePanel::Handle(Symbol, DataArray*)`; the `request` branch reads the menu symbol from argument 0 and the bool from argument 1 |
| request builder | `sub_8274FB60` | formats the metadata path (`"/%s%s"` at 0x82103138, `"?pid=%s"` at 0x82103130), stores it at `this+148`, calls the file cache `sub_8235A240(manager, &path)` |
| index request | `sub_82753010` (reached from `sub_82753B20`'s `request_index_if_needed` branch) | returns early when `*(this+52) != *(this+56)`; then returns unless `*(0x828A3D20+44) == 2`; then calls `sub_8274FB60` |
| the callback that should start it | DTA `on_init_commerce_success` in `ui/store/gen/store.dtb` | `{this request_index_if_needed}` then a `has_menus` check that sets the loading visual state |

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

Landed with this record: `store::AddStoreEntryBypass` (+ tests), the `sub_8274FB60` request hook and
the `sub_82753010` gate hook in `src/store/store_probe.cpp` (pass-through; they log and force only
while `store_trace` is on). `store_payload` is **84 checks**; full `ctest` **40/40**.

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
  definition as well as a `UIPanel` one (the first boot refused by name: `no {new UIPanel <name>
  ...} definition for that panel`). That edit was then **retired** — it only fired once the store
  screen was already open, which is the one place P3 will change — and the `UIScreen` widening kept.
- `scripts/probe_search.ps1` — one boot that types a query on the keys and OCRs the strip.
- `src/store/rhythmverse_client.cpp` — one widening the live API forced (§"Measured" below): a
  query with **no matches** answers HTTP 200 with `{"records":{"total_filtered":0,"returned":0},
  "songs":false}` — a JSON **boolean** where every matched answer carries an array. `ParseListing`
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
  refused: `store_search: 'rbbz-store/search/done' answered but not applied: Type at least 3
  characters. Search by artist or song title, one at a time.` (`rb_blitz_059.log`, 17:39:30 and
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
  (`out/_swap/search-probe/20261020-175302`) logs `0 record(s) answered, 0 eligible offer(s), state
  empty (current)` and the strip OCRs as **`NO MATCHES FOR "ZZZZQQQ". TRY JUST THE ARTIST NAME OR
  JUST THE SONG TITLE.`** — the plan's second sentence, verbatim, in the game.
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
- Validate, flush/close, then atomically rename to a safe generated name of ≤42 UTF-8 bytes
  (for example `rv_<32-hex-stable-id-hash>.con`), with collision detection and no silent overwrite
  of user packages. Existing loaded packages must not be replaced in place; an explicit Download
  Again either installs a distinct package or reports that replacement requires restart. Never use
  untrusted title/artist, Content-Disposition or raw opaque IDs as a filesystem path.
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

Honest limits — P5's exit is **not** met yet:

- No boot has downloaded anything. Nothing registers offers yet (the catalogue worker is P3's
  post-step-0 lane) and no DTA button case starts a job (P3/P4 own the store screens), so "a
  verified direct package is playable after the proven refresh or explicitly required restart" is
  unmeasured. The service reports a verified install as installed and pending discovery; the list
  must show **restart required** until OQ6's guest-discovery half is proven.
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
  and never the settings store or a cvar registry (project rule).
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
| `out/_swap/rv_review_20261014.body`, `rv_review_20261014.headers.txt` | successful 2026-10-14 scoped list request (`records=1`, no sort/instrument fields); `rb3xbox` result with external Dropbox URL, opaque dotted ID, zero size and null ranks |
| `out/_swap/rv_review_20261014_recent.json`, `rv_review_20261014_recent.headers.txt` | successful scoped Recent request, default four instruments, update-date descending, 25 records; all `rb3xbox`, 23 root-relative direct-file links |
| `out/_swap/rvp_list_rb3xbox.json` | failed earlier probe: HTML 404, **not** a JSON success fixture |
| `out/_swap/rv_songfile.html` | 2026-10-20 song-page probe (HTTP 200, 171,707 bytes): the only preview is a YouTube `<iframe>`; no audio asset (§3.3) |
| `out/_swap/rv_p0_encoded.json` | 2026-10-20 list probe with percent-encoded form keys: HTTP 200, `status=success`, 1 `rb3xbox` record |
| §2.11 code-reference table | current menu ownership, settings schema/panels, bridge limitations, SDK package reader, HTTP redirects, filename limits and refresh gaps checked on 2026-10-14 |
| image string offsets | `BandStorePanel` 0x3A078; `is_store_offer` 0x3D308; `has_created_commerce_session` 0x87B00; `load_art` 0x87BC0; `netcache_init` 0x8E6EC; `get_store_offer` 0x105248; `set_target_store_offer_id` 0x105270; `dlc_store` 0x102D00; `/dlc_top_%s_%s.dta` 0x102D58; `dlc_store/%s/%s/offers/%s/` 0x105088; `dlc_store/%s/%s/related/%s.dta` 0x104740; `Xis_participating_user_online` 0x13FBB |
| imports (checkout) | `__imp__XamShowMarketplaceUI` 0x827F6544, `…NuiMarketplaceUI` 0x827F65B4, `…MarketplaceDownloadItemsUI` 0x827F6774 |

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
