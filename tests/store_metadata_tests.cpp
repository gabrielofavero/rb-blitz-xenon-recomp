// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/store/store_metadata.cpp - the store's measured request names and record
// writers (RhythmVerse store plan §5/P2-P3). SDK-free: the writers produce bytes with the
// project's own DTA compiler, and the records are parsed back with the same reader.
//
// What these pin down:
//   * the request names measured in the first real fetch (`dlc_store/europe/dlc_top_xbox_eng.dta`
//     and the `/<locale>/<name>/` provider path the panel builds the next request from), plus the
//     offer directory/related templates and the `menu_<id>` source id;
//   * the owner's front menu is Recent then Search and nothing else;
//   * the front-menu record carries `submenus` - the key `sub_8275D4E0` reads - with one
//     `/<locale>/<id>/` entry per row;
//   * the menu record carries `offers_populated`, `sorted` and `offers`, an empty page is valid,
//     and a nameless offer is refused;
//   * both writers refuse before writing and their records survive compile -> parse.
//
// **Not** pinned: whether the retail parser accepts the key spellings. The fetch exists and the
// requested name is measured, but no served record has been observed being read yet (plan §5/P3).

#include "check.h"

#include "store/store_metadata.h"
#include "ui/dtb_author.h"

#include <string>
#include <vector>

#include <cstdio>

namespace {

using namespace rb_blitz::test;
using rb_blitz::store::BuildFrontMenuRecord;
using rb_blitz::store::BuildMenuRecord;
using rb_blitz::store::FrontMenu;
using rb_blitz::store::FrontMenuEntry;
using rb_blitz::store::FrontMenuRecordPath;
using rb_blitz::store::MenuProviderPath;
using rb_blitz::store::MenuRecordPath;
using rb_blitz::store::MenuSourceId;
using rb_blitz::store::Offer;
using rb_blitz::store::OfferDirectory;
using rb_blitz::store::RelatedOfferPath;
using rb_blitz::store::RhythmVerseFrontMenu;
using rb_blitz::ui::dtb::Node;

// check.h has no string comparison; a request name is exactly the value whose failure needs both
// sides printed.
void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq((actual), (expected))

// The strings of the root map's `submenus` value - the front menu's rows, in order. A DTA map is
// symbol/value alternation, so the value is the node after the `submenus` symbol.
std::vector<std::string> Submenus(const std::vector<Node>& roots) {
  std::vector<std::string> rows;
  if (roots.size() != 1) return rows;
  const std::vector<Node>& kids = roots[0].kids;
  for (size_t i = 0; i + 1 < kids.size(); ++i) {
    if (kids[i].tag != rb_blitz::ui::dtb::kSymbol || kids[i].text != "submenus") continue;
    if (kids[i + 1].tag != rb_blitz::ui::dtb::kArray) continue;
    for (const Node& entry : kids[i + 1].kids) {
      if (entry.tag == rb_blitz::ui::dtb::kString) rows.push_back(entry.text);
    }
  }
  return rows;
}

// The value of a root map key whose value is an array, or null.
const Node* RootArray(const std::vector<Node>& roots, std::string_view name) {
  if (roots.size() != 1) return nullptr;
  const std::vector<Node>& kids = roots[0].kids;
  for (size_t i = 0; i + 1 < kids.size(); ++i) {
    if (kids[i].tag != rb_blitz::ui::dtb::kSymbol || kids[i].text != name) continue;
    if (kids[i + 1].tag != rb_blitz::ui::dtb::kArray) continue;
    return &kids[i + 1];
  }
  return nullptr;
}

// The string value of an offer map's key, or empty.
std::string OfferField(const Node& offer, std::string_view key) {
  for (size_t i = 0; i + 1 < offer.kids.size(); ++i) {
    if (offer.kids[i].tag != rb_blitz::ui::dtb::kSymbol || offer.kids[i].text != key) continue;
    if (offer.kids[i + 1].tag != rb_blitz::ui::dtb::kString) continue;
    return offer.kids[i + 1].text;
  }
  return {};
}

void TestRequestNames() {
  BeginCase("request names match the measured templates");
  // Measured 2026-10-20 in the first real fetch (plan §5/P3): the panel asked the cache for
  // exactly this name after the corrected gate.
  CHECK_STR_EQ(FrontMenuRecordPath("europe", "xbox", "eng"),
               "dlc_store/europe/dlc_top_xbox_eng.dta");
  CHECK_STR_EQ(MenuProviderPath("eng", "recent"), "/eng/recent/");
  CHECK_STR_EQ(MenuRecordPath("europe", "eng", "recent"),
               "dlc_store/europe/eng/recent/");
  CHECK_STR_EQ(OfferDirectory("xbox", "eng", "42"), "dlc_store/xbox/eng/offers/42/");
  CHECK_STR_EQ(RelatedOfferPath("xbox", "eng", "42"), "dlc_store/xbox/eng/related/42.dta");
  CHECK_STR_EQ(MenuSourceId("recent"), "menu_recent");
  // The defaults are the measured substitutions, not an earlier guess.
  CHECK_STR_EQ(FrontMenuRecordPath(), "dlc_store/europe/dlc_top_xbox_eng.dta");
}

void TestOwnerFrontMenu() {
  BeginCase("the owner's front menu is Recent then Search");
  const FrontMenu menu = RhythmVerseFrontMenu();
  CHECK_EQ(menu.entries.size(), size_t{2});
  if (menu.entries.size() == 2) {
    CHECK_STR_EQ(menu.entries[0].id, "recent");
    CHECK_STR_EQ(menu.entries[0].label, "Recent");
    CHECK_STR_EQ(menu.entries[1].id, "search");
    CHECK_STR_EQ(menu.entries[1].label, "Search");
  }
  CHECK_STR_EQ(menu.region, "europe");
  CHECK_STR_EQ(menu.platform, "xbox");
  CHECK_STR_EQ(menu.locale, "eng");
}

void TestWriterRefusals() {
  BeginCase("the writer refuses a menu it cannot name");
  std::string why;
  CHECK_TRUE(BuildFrontMenuRecord(FrontMenu{}, &why).empty());
  CHECK_FALSE(why.empty());

  FrontMenu nameless;
  nameless.entries.push_back({"", "Recent"});
  why.clear();
  CHECK_TRUE(BuildFrontMenuRecord(nameless, &why).empty());
  CHECK_FALSE(why.empty());

  FrontMenu unlabelled;
  unlabelled.entries.push_back({"recent", ""});
  why.clear();
  CHECK_TRUE(BuildFrontMenuRecord(unlabelled, &why).empty());
  CHECK_FALSE(why.empty());
}

void TestMenuWriterRefusals() {
  BeginCase("the menu writer refuses an offer it cannot name");
  std::string why;
  Offer nameless;
  nameless.artist = "Someone";
  CHECK_TRUE(BuildMenuRecord({nameless}, &why).empty());
  CHECK_FALSE(why.empty());
}

void TestRecordRoundTrip() {
  BeginCase("the front-menu record survives compile and parse");
  const FrontMenu menu = RhythmVerseFrontMenu();
  std::string why;
  const std::vector<uint8_t> bytes = BuildFrontMenuRecord(menu, &why);
  CHECK_TRUE(why.empty());
  CHECK_FALSE(bytes.empty());

  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
  CHECK_EQ(roots.size(), size_t{1});
  if (roots.size() != 1) return;
  // The root is the map `(submenus (...))` - symbol/value, the shape `sub_8275D4E0` reads.
  CHECK_EQ(roots[0].kids.size(), size_t{2});
  if (roots[0].kids.size() != 2) return;
  CHECK_STR_EQ(roots[0].kids[0].text, "submenus");

  const std::vector<std::string> rows = Submenus(roots);
  CHECK_EQ(rows.size(), size_t{2});
  if (rows.size() == 2) {
    CHECK_STR_EQ(rows[0], "/eng/recent/");
    CHECK_STR_EQ(rows[1], "/eng/search/");
  }
}

void TestMenuRecordRoundTrip() {
  BeginCase("the menu record survives compile and parse");
  Offer song;
  song.name = "One Week";
  song.artist = "Barenaked Ladies";
  song.album = "Stunt";
  song.genre = "Rock";
  song.year_released = 1998;
  song.preview_art = "dlc_store/xbox/eng/offers/42/art";
  song.rank_guitar = 3;

  std::string why;
  const std::vector<uint8_t> bytes = BuildMenuRecord({song}, &why);
  CHECK_TRUE(why.empty());
  CHECK_FALSE(bytes.empty());

  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
  CHECK_EQ(roots.size(), size_t{1});
  const Node* offers = RootArray(roots, "offers");
  CHECK_TRUE(offers != nullptr);
  if (offers == nullptr) return;
  CHECK_EQ(offers->kids.size(), size_t{1});
  if (offers->kids.empty()) return;
  const Node& offer = offers->kids[0];
  CHECK_STR_EQ(OfferField(offer, "name"), "One Week");
  CHECK_STR_EQ(OfferField(offer, "artist"), "Barenaked Ladies");
  CHECK_STR_EQ(OfferField(offer, "album"), "Stunt");
  CHECK_STR_EQ(OfferField(offer, "genre"), "Rock");
  CHECK_STR_EQ(OfferField(offer, "preview_art"), "dlc_store/xbox/eng/offers/42/art");
  bool year_ok = false;
  bool rank_ok = false;
  for (size_t i = 0; i + 1 < offer.kids.size(); ++i) {
    if (offer.kids[i].tag != rb_blitz::ui::dtb::kSymbol) continue;
    if (offer.kids[i].text == "year_released" &&
        offer.kids[i + 1].tag == rb_blitz::ui::dtb::kInt && offer.kids[i + 1].scalar == 1998) {
      year_ok = true;
    }
    if (offer.kids[i].text == "rank" && offer.kids[i + 1].tag == rb_blitz::ui::dtb::kArray) {
      const std::vector<Node>& ranks = offer.kids[i + 1].kids;
      for (size_t j = 0; j + 1 < ranks.size(); ++j) {
        if (ranks[j].text == "guitar" && ranks[j + 1].tag == rb_blitz::ui::dtb::kInt &&
            ranks[j + 1].scalar == 3) {
          rank_ok = true;
        }
      }
    }
  }
  CHECK_TRUE(year_ok);
  CHECK_TRUE(rank_ok);
}

void TestEmptyMenuRecord() {
  BeginCase("an empty page is a valid menu record");
  std::string why;
  const std::vector<uint8_t> bytes = BuildMenuRecord({}, &why);
  CHECK_TRUE(why.empty());
  CHECK_FALSE(bytes.empty());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
  const Node* offers = RootArray(roots, "offers");
  CHECK_TRUE(offers != nullptr);
  if (offers != nullptr) CHECK_TRUE(offers->kids.empty());
}

}  // namespace

int main() {
  TestRequestNames();
  TestOwnerFrontMenu();
  TestWriterRefusals();
  TestMenuWriterRefusals();
  TestRecordRoundTrip();
  TestMenuRecordRoundTrip();
  TestEmptyMenuRecord();
  return rb_blitz::test::Finish();
}
