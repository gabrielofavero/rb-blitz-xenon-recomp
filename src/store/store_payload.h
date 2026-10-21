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
// `FALSE` (the else branch - the details screen - always runs). Refused, before any edit, when the
// panel's `on_enum_finished` does not carry the `if_else` whose test reads the key.
bool AlwaysShowOfferDetails(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The store heading is retail's own locale entry `store` ("MUSIC STORE"); the owner's attribution
// requirement (§1.8) is met by a store that names its source. Replaces the entry's value whatever
// the locale currently says, because a translated heading is still retail's, not ours. Returns
// false and fills `why` when the locale does not carry the key.
inline constexpr char kStoreHeadingText[] = "RHYTHMVERSE";
bool SetStoreHeading(std::vector<ui::dtb::Node>* locale_roots, std::string_view heading,
                     std::string* why);
}  // namespace rb_blitz::store
