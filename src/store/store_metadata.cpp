// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_metadata.h"

#include "ui/dtb_author.h"

namespace rb_blitz::store {
namespace {

using ui::dtb::Node;

Node Symbol(std::string_view name) { return Node::Symbol(name); }
Node Text(std::string_view text) { return Node::String(text); }

}  // namespace

std::string FrontMenuRecordPath(std::string_view platform, std::string_view locale) {
  return "dlc_top_" + std::string(platform) + "_" + std::string(locale) + ".dta";
}

std::string OfferDirectory(std::string_view platform, std::string_view locale,
                           std::string_view offer_id) {
  return "dlc_store/" + std::string(platform) + "/" + std::string(locale) + "/offers/" +
         std::string(offer_id) + "/";
}

std::string RelatedOfferPath(std::string_view platform, std::string_view locale,
                             std::string_view offer_id) {
  return "dlc_store/" + std::string(platform) + "/" + std::string(locale) + "/related/" +
         std::string(offer_id) + ".dta";
}

std::string MenuSourceId(std::string_view entry_id) { return "menu_" + std::string(entry_id); }

FrontMenu RhythmVerseFrontMenu() {
  FrontMenu menu;
  menu.entries.push_back({"recent", "Recent"});
  menu.entries.push_back({"search", "Search"});
  return menu;
}

std::vector<uint8_t> BuildFrontMenuRecord(const FrontMenu& menu, std::string* why) {
  if (menu.entries.empty()) {
    if (why) *why = "the front menu has no entries";
    return {};
  }
  for (const FrontMenuEntry& entry : menu.entries) {
    if (entry.id.empty()) {
      if (why) *why = "a front-menu entry has an empty id";
      return {};
    }
    if (entry.label.empty()) {
      if (why) *why = "front-menu entry '" + entry.id + "' has an empty label";
      return {};
    }
  }
  std::vector<Node> menus;
  menus.push_back(Symbol("menus"));
  std::vector<Node> rows;
  for (const FrontMenuEntry& entry : menu.entries) {
    rows.push_back(Node::Array({
        Node::Array({Symbol("id"), Text(entry.id)}),
        Node::Array({Symbol("label"), Text(entry.label)}),
    }));
  }
  menus.push_back(Node::Array(std::move(rows)));
  // The `menus` table is moved in after the fixed fields rather than written inline: a braced list
  // cannot hold a vector where a Node is expected, and a copy of the table is not needed.
  std::vector<Node> root_kids{
      Symbol("front_menu"),
      Node::Array({Symbol("version"), Node::Int(kFrontMenuVersion)}),
      Node::Array({Symbol("platform"), Text(menu.platform)}),
      Node::Array({Symbol("locale"), Text(menu.locale)}),
  };
  root_kids.push_back(Node::Array(std::move(menus)));
  std::vector<Node> root{Node::Array(std::move(root_kids))};
  return ui::dtb::Compile(root);
}

}  // namespace rb_blitz::store
