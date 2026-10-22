// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P2 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md: the data-layer
// decision gate. This is the *measurement* half — it points the retail store's netcache
// at a local (host-file) server so the paths the store asks for become file opens this
// project can see, instead of a request to a server that no longer exists.
//
// The record is `config/gen/store.dtb`, whose single definition is `netcache_init` (decoded
// verbatim in out/_swap/config_store.tree.txt): a `servers` table of named servers (each with
// `server`/`port`/`root`, and one `local` entry carrying `{local TRUE} {root ...}`) and a
// `default_server` symbol that picks one. The rewrite keeps every other record in the file —
// the store's `groupings`, `dummy_upsell_offer` and the PS3 terms — because those are still
// read by the same parser.
//
// The transform touches **every** `servers`/`default_server` the definition carries, not just
// the Xbox branch: the shipped record keeps its platform branches inside `#ifdef` nodes
// (`HX_XBOX`, `HX_PS3`, `_SHIP`, `_DEBUG`) whose evaluation is the guest's business, so
// rewriting only the branch this build "should" take would make the measurement depend on a
// guess about that evaluation. Rewriting them all means whichever branch the guest takes, its
// default server is the local one.
//
// SDK-free on purpose: no rex headers, so tests/store_payload_tests.cpp covers it without a boot.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ui/dtb_author.h"

namespace rb_blitz::store {

// The `netcache_init` definition's own name in `config/gen/store.dtb`.
inline constexpr char kNetcacheInitName[] = "netcache_init";

// Rewrites `roots` (the parsed `config/gen/store.dtb` record) in place so that:
//
//   * every `servers` table carries a `local` entry rooted at `server_root`
//     (an existing `local` entry is rewritten rather than duplicated), and
//   * every `default_server` picks `local`.
//
// `server_root` is the guest-relative directory the netcache resolves against, spelled the way
// the shipped `local` entry spells its own (`ui/store/remote_metadata/`, trailing slash
// included). An empty root is refused: a local server with no root would measure nothing.
//
// Returns false and fills `why` when the record does not carry `netcache_init`, when it carries
// no `default_server` at all, or when `server_root` is empty — never a partial rewrite that
// looks successful.
bool PointNetcacheAtLocalServer(std::vector<ui::dtb::Node>* roots, std::string_view server_root,
                                std::string* why);

// The `netcache_init` definition inside a parsed `config/gen/store.dtb`, or null. Used to hand the
// same definition to the manager through `ui/gen/ui.dtb` (below), so the manager's argument has the
// shipped shape by construction.
const ui::dtb::Node* FindNetcacheInit(const std::vector<ui::dtb::Node>& roots);

// `ui/gen/ui.dtb` creates the netcache manager once, at boot, with a definition it looks up in the
// engine's syscfg:
//
//   {net_cache_mgr init {find syscfg store netcache_init}}
//
// The lookup is unmeasured (measured 2026-10-20: the store's `request` is issued and the manager
// then performs no file, content or socket call at all — so whether `syscfg.store` is populated in
// this build is not known), so this rewrite replaces the argument with `definition` — normally the
// record's own rewritten `netcache_init`, never a hand-built one, because a literal that differs
// from the shipped shape is a guess about what the native manager reads.
//
// Returns false and fills `why` when the record carries no `net_cache_mgr init` command or when
// `definition` is not a `netcache_init` array — again before any edit, so a refusal leaves the tree
// as it was.
bool PointUiNetcacheAtLocalServer(std::vector<ui::dtb::Node>* ui_roots,
                                  const ui::dtb::Node& definition, std::string* why);

// P3 step 0's second half. The panel starts its first request only from its own DTA callback
// `on_init_commerce_success`, which fires when a native commerce session is created — a session
// an offline boot never gets (measured 2026-10-20: no `store-probe: request` line is ever printed,
// and the screen sits on its loading visual state). The callback's own body is harmless to run
// (`{this request_index_if_needed}` then a `has_menus` check that sets the visual state), so this
// rewrite appends `{store_panel on_init_commerce_success}` to `store_menu_screen`'s `enter`
// method and lets the retail DTA do the rest.
//
// Returns false and fills `why` when the record carries no `store_menu_screen` with an `enter`
// method — before any edit.
bool AddStoreEntryBypass(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);
// P3's free-download seam (plan §2.12). Retail draws its price chrome in two places, both of them
// text the scripts show and no data we could supply hides:
//
//   * the store's footer, "ALL PRICES ARE IN MICROSOFT(R) POINTS" (`microsoft.lbl`, shown on the
//     Xbox branch of `BandStorePanel.enter`);
//   * the details page's `cost.lbl`/`cost_ps3.lbl`, which `store_info_panel.refresh_panel` re-shows
//     whenever `purchase_allowed` is true.
//
// RhythmVerse customs are free, so this appends `{microsoft.lbl set_showing FALSE}` to the panel's
// `enter` (which every store screen runs, because the panel is a member of all three) and returns
// false when that method is missing - before any edit.
bool HideStorePriceChrome(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// The same seam on the details record (`ui/store/gen/store_info.dtb`): every command that shows a
// cost label is removed from `store_info_panel.refresh_panel`, and both labels are hidden once more
// at the end of the method. The purchase button needs no edit: `purchase_allowed` needs
// `{offer is_available}`, which a free custom never sets - see `Offer::downloaded`.
bool HideStoreInfoPriceChrome(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// A free custom is never "completely unavailable": that state only exists in the retail purchase
// model. `store_info_enum_panel.on_enum_finished` opens `store_info_screen` only when
// `{offer is_completely_unavailable}` is false, and measured 2026-10-21 the A press on a served
// offer landed on `store_info_unavailable_screen` instead. This replaces that guard's test with
// `FALSE` (the else branch - the details screen - always runs) in **every** panel that carries it
// (`store_info_enum_panel` and `store_offer_fetch_panel` both do; the first cut edited only the
// first match, which in document order is the fetch panel, so the browser's own guard survived).
// Refused, before any edit, when no such guard exists in the record at all.
bool AlwaysShowOfferDetails(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The browser's A press hands the offer to `store_info_enum_panel.enum_and_show_offer`, which
// switches to `store_info_enum_screen` and lets the native `set_offer` enumerate it. For a served
// offer that enumeration reports an error and the press ends on `store_info_unavailable_screen`
// (measured 2026-10-21: the popup survives `AlwaysShowOfferDetails`). This rewrite replaces the
// method's body with the body the retail panel itself runs when its enumeration succeeds - the
// `on_enum_finished` else branch, verbatim - so the details screen is populated directly:
//
//   {do {store_info_panel clear_data}
//       {store_info_panel set cur_offer offer}
//       {store_details_panel refresh offer}
//       {ui goto_screen store_info_screen}}
//
// `set_return_target` still runs before it (the browser's own case), so Back keeps its target.
// Idempotent; refused, before any edit, when the panel or the method is missing.
bool ShowOfferDetailsDirectly(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The details page's Downloaded row. `store_info_panel.refresh_buttons` (printed out of the shipped
// record 2026-10-22) inserts `store_download_again` under `{if_else {offer is_purchased}
// {insert_elem buttons 0 store_download_again} {if {this purchase_allowed offer}
// {insert_elem buttons 0 store_buy_now}}}` - so the row does **not** depend on `purchase_allowed`,
// which is what this build keeps false. The gate itself is the problem: `is_purchased` is answered
// **natively** by the offer object's own message handler (`sub_82453928`, whose literals include
// `get_data`, `has_data`, `offer_type`, `year_released`, `genre`, `is_purchased`), from a commerce
// state a data-only custom never has, so the record's `(is_purchased TRUE)` was ignored and a
// marked offer's page drew Exit alone (measured 2026-10-22, `out/_swap/store-probe/20261022-091435`;
// the same record counted as marked in the trace). This replaces that gate with the **data** read
// retail itself uses for `preview_audio` - `{offer get_data (is_purchased)}` - which the offer walk
// resolves like any other one-component key. An offer without the key reads a null, so its page is
// unchanged (Exit only). Idempotent; refused, before any edit, when `refresh_buttons` is missing or
// carries no such gate.
bool UseDataForDownloadedRow(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The details page's Preview Audio row is built from `refresh_buttons`' own preview block, and its
// gate is `{&& is_song {offer has_data preview_audio} {is_allowed_to_preview_offer offer}}`. The
// record must carry `preview_audio` (its previous statement reads it unconditionally, and an absent
// key is a null read - see `Offer::preview_audio`), but an empty string satisfies `has_data`, so the
// row appeared on a served offer with nothing behind it (measured 2026-10-21). This removes the
// `store_preview_audio` insertion from `store_info_panel.refresh_buttons`; the rest of the method
// (which hands the preview manager an empty file name, a no-op) is left as retail wrote it.
// Idempotent; refused, before any edit, when the method is missing.
bool HidePreviewButton(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// Album art, the last link in the chain. `store_details_panel.refresh` sets
// `album.mat`'s texture to `cover_art_none.tex`, calls `{store_panel load_art <path> this}` and
// expects the completion to message `art_loaded`, whose handler sets the material to
// `{store_panel album_tex}`. Measured 2026-10-21 with a served texture: the file is inserted, the
// panel decodes it and the texture object is filled (256x256 DXT5, `has_bitmap=1`), but the
// completion skips the message - its guard reads the request object's state word and finds 2, not
// the 1 an asynchronous download would have. Our delivery is synchronous (the insert happens inside
// the request), so that guard can never be satisfied: this append runs the same assignment the
// callback would have, right after `load_art` returns (boot `rb_blitz_176.log`).
//
// The serving policy only advertises `preview_art` for a texture it already holds, so the append
// cannot link a stale cover: with no art the DTA never calls `load_art` (store_serve.cpp).
// Idempotent; refused, before any edit, when `store_details_panel.refresh` has no `load_art` call.
bool LinkAlbumArt(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// The details panel's difficulty read is `{offer get_data (rank <part>)}` -
// `store.dtb`'s `store_details_panel.refresh`, `{foreach part (guitar drum vocals bass keys)}`,
// which then converts each rank with `{song_mgr rank_tier rank part}` and hands the result to
// `{this set_inst_difficulty diff_<part> <tier> TRUE}`. That read is a **two-component** path, and
// the offer's `get_data` walk cannot resolve it against any five-part rank map: measured 2026-10-21
// by decoding the walk (`sub_82453928` -> `sub_824538A8` -> `sub_824534A8`), each component is
// interned and `sub_823FAE00` matches it against the FIRST element of a pair, so the second
// component asks the `rank` pair for a child whose first element is the part. A map can answer that
// for one part; for the rest the lookup returns nothing and the walk dereferences the null (boot
// `rb_blitz_165.log`: `read of guest 0x00000000` at the walk, stack `sub_824534A8` <-
// `sub_82453928` <- `sub_8275C6C0` <- the DTA evaluator).
//
// This rewrite replaces the loop with five explicit blocks that read the offer's own
// `(diff_<part>)` - the **tier** the catalogue publishes - and pass it to `set_inst_difficulty`
// unchanged, dropping the `rank_tier` conversion. Two measurements say that is the right seam:
// the call takes a tier (its argument is retail's own `$tier`), and the meter asset is built for
// the catalogue's 0..6 scale - five pips plus a devil icon
// (`ui/difficulty_meter/gen/diff_meter_all_inst.milo`: `diff_1.mesh` ... `diff_5.mesh`,
// `icon_devil.tex`), with 0 drawing the locale's `no_part`. The rank path would need
// `rank_tier`'s per-instrument thresholds, which this build does not carry; the catalogue already
// publishes the tier, so the display shows the catalogue's difficulty instead of a guess.
//
// Cost of that choice, stated plainly: an offer whose `diff_*` the API does not publish reads
// `NO PART` for that part, and `NO PART` is also the correct state for a part the song has no chart
// for (`diff_* = 0`). Idempotent; refused, before any edit, when `store_details_panel.refresh` has
// no `(rank <part>)` loop and no unrolled block.
bool UnrollOfferDifficulty(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// The store heading is retail's own locale entry `store` ("MUSIC STORE"); the owner's attribution
// requirement (§1.8) is met by a store that names its source. Replaces the entry's value whatever
// the locale currently says, because a translated heading is still retail's, not ours. Returns
// false and fills `why` when the locale does not carry the key.
inline constexpr char kStoreHeadingText[] = "RHYTHMVERSE";
bool SetStoreHeading(std::vector<ui::dtb::Node>* locale_roots, std::string_view heading,
                     std::string* why);
}  // namespace rb_blitz::store
