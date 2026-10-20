// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P2's metadata *request model* and writer skeleton (RhythmVerse store plan §5/P2, exit:
// "if A is chosen, a skeleton of the metadata writer").
//
// What is factual here, measured from the decoded retail records and the image strings
// (out/_swap/store.tree.txt, plan §0 headline 1 and §2.3):
//
//   * the store front menu is fetched as `dlc_top_%s_%s.dta`;
//   * a per-offer directory is `dlc_store/%s/%s/offers/%s/` and its related offers are
//     `dlc_store/%s/%s/related/%s.dta`;
//   * the DTA drives the panel with a *string* per menu: on select it builds the source
//     `menu_<provider string>` and calls `{store_panel request <provider string> TRUE}`.
//
// What is **not** factual here: the record's own field names. The retail parser's schema has
// never been exercised (measured 2026-10-20: entering the store and accepting its first row
// issues the DTA request but the native panel performs no file open, no content call and no
// network call, so the parser never sees a file - plan §5/P2 record). The shape below is a
// skeleton with the fewest assumptions that still carries the two menus, and it is deliberately
// behind this one function so P3 changes one file when the fetch seam lands and the parser can
// be fed a hand-made record.
//
// SDK-free on purpose: it is pure bytes plus string templates, so the shape and the request
// names are pinned by tests/store_metadata_tests.cpp without a boot.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::store {

// One row of the store's front menu, in the order the player sees it.
struct FrontMenuEntry {
  // The provider string the DTA asks for (`{provider get_string ix}`): the panel derives its
  // source id from it (`menu_<id>`) and passes it to `request`.
  std::string id;
  // The token or literal the row draws.
  std::string label;
};

// The front menu the owner asked for (§1): Recent and Search, nothing else. Retail's categories
// are intentionally not reproduced.
struct FrontMenu {
  // The two substitutions of `dlc_top_%s_%s.dta`. Their meaning is unproven (see the header
  // comment); they are parameters so the caller names them rather than the writer guessing.
  std::string platform = "xbox";
  std::string locale = "en";
  std::vector<FrontMenuEntry> entries;
};

// `dlc_top_<platform>_<locale>.dta`.
std::string FrontMenuRecordPath(std::string_view platform, std::string_view locale);

// `dlc_store/<platform>/<locale>/offers/<offer_id>/`.
std::string OfferDirectory(std::string_view platform, std::string_view locale,
                           std::string_view offer_id);

// `dlc_store/<platform>/<locale>/related/<offer_id>.dta`.
std::string RelatedOfferPath(std::string_view platform, std::string_view locale,
                             std::string_view offer_id);

// The panel's source id for a menu: `menu_<entry id>`.
std::string MenuSourceId(std::string_view entry_id);

// The owner's two menus, in order.
FrontMenu RhythmVerseFrontMenu();

// The skeleton writer: a compiled DTA whose root array is
//
//   (front_menu
//     (version <kFrontMenuVersion>)
//     (platform "<platform>") (locale "<locale>")
//     (menus ( (id "<id>") (label "<label>") ) ... ))
//
// Returns an empty vector and fills `why` when the menu has no entries or an entry has an empty
// id or label - a record with a nameless row would make the panel ask for `menu_`, which is a
// worse failure than not shipping one.
std::vector<uint8_t> BuildFrontMenuRecord(const FrontMenu& menu, std::string* why);

// The `version` value the skeleton writes. P3 bumps it if the schema changes once the parser has
// been observed; it exists so a served record can be told apart from an older one in a log.
inline constexpr uint32_t kFrontMenuVersion = 1;

}  // namespace rb_blitz::store
