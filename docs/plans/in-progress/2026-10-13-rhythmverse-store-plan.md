# RhythmVerse in-store integration — research + implementation plan

Date: 2026-10-13. Reviewed: 2026-10-14, 2026-10-20. Status: **in progress — P0 (API client, URL/byte
policy, `store_http`) and P6 (the six settings rows and the live snapshot) landed 2026-10-20; P1 is
next; P2–P5 and P7–P8 are not started.** Audio preview is decided: the API returns no preview asset
(§3.3), so v1 shows none.
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

### P3 — first light: "Recent" list with real data + art

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
