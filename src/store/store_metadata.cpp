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

std::string FrontMenuRecordPath(std::string_view region, std::string_view platform,
                                std::string_view locale) {
  return "dlc_store/" + std::string(region) + "/dlc_top_" + std::string(platform) + "_" +
         std::string(locale) + ".dta";
}

std::string MenuProviderPath(std::string_view locale, std::string_view name) {
  return "/" + std::string(locale) + "/" + std::string(name) + "/";
}

std::string MenuRecordPath(std::string_view region, std::string_view locale,
                           std::string_view name) {
  return "dlc_store/" + std::string(region) + MenuProviderPath(locale, name);
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
  // `(submenus ("/<locale>/<id>/" ...))`: the array `sub_8275D4E0` keeps on the panel and
  // `sub_8275CBD8` compares each entry against `/<locale>/<name>/`.
  std::vector<Node> entries;
  for (const FrontMenuEntry& entry : menu.entries) {
    entries.push_back(Text(MenuProviderPath(menu.locale, entry.id)));
  }
  std::vector<Node> root{
      Node::Array({Symbol("submenus"), Node::Array(std::move(entries))})};
  return ui::dtb::Compile(root);
}

std::vector<uint8_t> BuildMenuRecord(const std::vector<Offer>& offers, std::string* why) {
  for (const Offer& offer : offers) {
    if (offer.name.empty()) {
      if (why) *why = "an offer has an empty name";
      return {};
    }
  }
  // A DTA map is symbol/value alternation (`(name "X" artist "Y" ...)`), which is the shape the
  // native `get_data`/`has_data` lookups read. A key with an unknown/empty value is omitted (the
  // scripts treat a missing key as "no data", which is how the preview and rank UI hide
  // themselves).
  std::vector<Node> rows;
  for (const Offer& offer : offers) {
    std::vector<Node> fields;
    auto add_text = [&fields](std::string_view key, const std::string& value) {
      if (value.empty()) return;
      fields.push_back(Symbol(key));
      fields.push_back(Text(value));
    };
    fields.push_back(Symbol("name"));
    fields.push_back(Text(offer.name));
    add_text("artist", offer.artist);
    add_text("album", offer.album);
    add_text("genre", offer.genre);
    if (offer.year_released != 0) {
      fields.push_back(Symbol("year_released"));
      fields.push_back(Node::Int(offer.year_released));
    }
    add_text("preview_art", offer.preview_art);
    // The per-part ranks (`rank <part>` in the decoded scripts; OQ4 has not settled whether the
    // retail key is `rank` with a sub-map or `rank_<part>`). One `rank` key with a part/value map
    // keeps every part in one place, which is the shape the plan's field mapping describes.
    const struct {
      const char* part;
      int value;
    } ranks[] = {{"guitar", offer.rank_guitar}, {"drum", offer.rank_drum},
                 {"bass", offer.rank_bass},     {"vocals", offer.rank_vocals},
                 {"keys", offer.rank_keys}};
    std::vector<Node> rank_map;
    for (const auto& rank : ranks) {
      if (rank.value == 0) continue;
      rank_map.push_back(Symbol(rank.part));
      rank_map.push_back(Node::Int(rank.value));
    }
    if (!rank_map.empty()) {
      fields.push_back(Symbol("rank"));
      fields.push_back(Node::Array(std::move(rank_map)));
    }
    rows.push_back(Node::Array(std::move(fields)));
  }
  // The root map `sub_8274F610` reads: `offers_populated`, `sorted`, `offers`.
  std::vector<Node> root_kids{
      Symbol("offers_populated"), Symbol("TRUE"),
      Symbol("sorted"),           Symbol("TRUE"),
      Symbol("offers"),           Node::Array(std::move(rows)),
  };
  std::vector<Node> root{Node::Array(std::move(root_kids))};
  return ui::dtb::Compile(root);
}

}  // namespace rb_blitz::store
