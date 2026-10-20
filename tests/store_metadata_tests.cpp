// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/store/store_metadata.cpp — P2's request model and front-menu writer
// skeleton (RhythmVerse store plan §5/P2). SDK-free: the writer produces bytes with the project's
// own DTA compiler, and the record is parsed back with the same reader.
//
// What these pin down:
//   * the three request names the retail panel uses (`dlc_top_%s_%s.dta`, the per-offer directory
//     and the related-offer path) and the `menu_<id>` source id, exactly as the decoded records
//     spell them;
//   * the owner's front menu is Recent then Search and nothing else;
//   * the writer refuses a menu with no entries and an entry with an empty id or label, leaving no
//     bytes behind;
//   * the record it does write survives compile -> parse and carries the version, platform, locale
//     and both rows. **Not** pinned: whether the retail parser accepts the field names - that is
//     unmeasurable until the store's fetch seam exists (plan §5/P2 record).
//
// The record the guest is served is the deliverable; this suite is what stops it regressing while
// the schema is still a guess.

#include "check.h"

#include "store/store_metadata.h"
#include "ui/dtb_author.h"

#include <string>
#include <vector>

#include <cstdio>

namespace {

using namespace rb_blitz::test;
using rb_blitz::store::BuildFrontMenuRecord;
using rb_blitz::store::FrontMenu;
using rb_blitz::store::FrontMenuEntry;
using rb_blitz::store::FrontMenuRecordPath;
using rb_blitz::store::MenuSourceId;
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

// The `(id "<x>")`/`(label "<y>")` pairs of the parsed `menus` array, flattened to `id label`.
std::vector<std::string> MenuRows(const std::vector<Node>& roots) {
  std::vector<std::string> rows;
  if (roots.size() != 1) return rows;
  for (const Node& field : roots[0].kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.empty()) continue;
    if (field.kids[0].text != "menus") continue;
    for (const Node& row : field.kids[1].kids) {
      std::string id;
      std::string label;
      for (const Node& leaf : row.kids) {
        if (leaf.tag != rb_blitz::ui::dtb::kArray || leaf.kids.size() != 2) continue;
        if (leaf.kids[0].text == "id") id = leaf.kids[1].text;
        if (leaf.kids[0].text == "label") label = leaf.kids[1].text;
      }
      rows.push_back(id + " " + label);
    }
  }
  return rows;
}

// The `(name <value>)` field of the root, as a string; numbers come back empty.
std::string RootField(const std::vector<Node>& roots, std::string_view name) {
  if (roots.size() != 1) return {};
  for (const Node& field : roots[0].kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.size() != 2) continue;
    if (field.kids[0].text == name && field.kids[1].tag == rb_blitz::ui::dtb::kString) {
      return field.kids[1].text;
    }
  }
  return {};
}

void TestRequestNames() {
  BeginCase("request names match the decoded retail templates");
  CHECK_STR_EQ(FrontMenuRecordPath("xbox", "en"), "dlc_top_xbox_en.dta");
  CHECK_STR_EQ(OfferDirectory("xbox", "en", "42"), "dlc_store/xbox/en/offers/42/");
  CHECK_STR_EQ(RelatedOfferPath("xbox", "en", "42"), "dlc_store/xbox/en/related/42.dta");
  CHECK_STR_EQ(MenuSourceId("recent"), "menu_recent");
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
  // The default substitutions are the caller's, not the writer's: they only have to be present.
  CHECK_FALSE(menu.platform.empty());
  CHECK_FALSE(menu.locale.empty());
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
  CHECK_EQ(roots[0].kids.size(), size_t{5});
  if (roots[0].kids.empty()) return;
  CHECK_STR_EQ(roots[0].kids[0].text, "front_menu");

  CHECK_STR_EQ(RootField(roots, "platform"), menu.platform);
  CHECK_STR_EQ(RootField(roots, "locale"), menu.locale);

  const std::vector<std::string> rows = MenuRows(roots);
  CHECK_EQ(rows.size(), size_t{2});
  if (rows.size() == 2) {
    CHECK_STR_EQ(rows[0], "recent Recent");
    CHECK_STR_EQ(rows[1], "search Search");
  }

  // The version is what a boot log can name; it must be the one the header declares.
  bool version_matches = false;
  for (const Node& field : roots[0].kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.size() != 2) continue;
    if (field.kids[0].text == "version" &&
        field.kids[1].tag == rb_blitz::ui::dtb::kInt &&
        field.kids[1].scalar == rb_blitz::store::kFrontMenuVersion) {
      version_matches = true;
    }
  }
  CHECK_TRUE(version_matches);
}

}  // namespace

int main() {
  TestRequestNames();
  TestOwnerFrontMenu();
  TestWriterRefusals();
  TestRecordRoundTrip();
  return rb_blitz::test::Finish();
}
