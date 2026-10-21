// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_metadata.h"

#include "ui/dtb_author.h"

namespace rb_blitz::store {
namespace {

using ui::dtb::Node;

Node Symbol(std::string_view name) { return Node::Symbol(name); }
Node Text(std::string_view text) { return Node::String(text); }

// A DTA text value: double-quoted, with the two characters that would end or escape the string
// backslash-escaped. A song title with a quote in it is unusual but not impossible, and a record
// the parser rejects is worse than one with an escaped quote in it.
std::string Quote(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

// Refuses what both writers refuse: the shape checks live here so the text and the compiled form
// cannot drift apart.
bool ValidateFrontMenu(const FrontMenu& menu, std::string* why) {
  if (menu.entries.empty()) {
    if (why) *why = "the front menu has no entries";
    return false;
  }
  for (const FrontMenuEntry& entry : menu.entries) {
    if (entry.id.empty()) {
      if (why) *why = "a front-menu entry has an empty id";
      return false;
    }
    if (entry.label.empty()) {
      if (why) *why = "front-menu entry '" + entry.id + "' has an empty label";
      return false;
    }
  }
  return true;
}

bool ValidateOffers(const std::vector<Offer>& offers, std::string* why) {
  for (const Offer& offer : offers) {
    if (offer.name.empty()) {
      if (why) *why = "an offer has an empty name";
      return false;
    }
  }
  return true;
}

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
  if (!ValidateFrontMenu(menu, why)) {
    return {};
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
  if (!ValidateOffers(offers, why)) {
    return {};
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

std::string BuildFrontMenuText(const FrontMenu& menu, std::string* why) {
  if (!ValidateFrontMenu(menu, why)) {
    return {};
  }
  // A DTA text file is one map: `(key value key value ...)`. The panel looks `submenus` up by
  // symbol (`sub_823FAE00(array, Sym("submenus"), 0)`), so the key carries no quotes and every
  // entry is a String node - a quoted path, not a symbol.
  std::string out = "(submenus\n";
  for (const FrontMenuEntry& entry : menu.entries) {
    out += "  ";
    out += Quote(MenuProviderPath(menu.locale, entry.id));
    out += "\n";
  }
  out += ")\n";
  return out;
}

std::string BuildMenuText(const std::vector<Offer>& offers, std::string* why) {
  if (!ValidateOffers(offers, why)) {
    return {};
  }
  std::string out = "(offers_populated TRUE\n sorted TRUE\n offers (\n";
  for (const Offer& offer : offers) {
    out += "  (name ";
    out += Quote(offer.name);
    auto add_text = [&out](std::string_view key, const std::string& value) {
      if (value.empty()) return;
      out += " ";
      out += key;
      out += " ";
      out += Quote(value);
    };
    add_text("artist", offer.artist);
    add_text("album", offer.album);
    add_text("genre", offer.genre);
    if (offer.year_released != 0) {
      out += " year_released " + std::to_string(offer.year_released);
    }
    add_text("preview_art", offer.preview_art);
    const struct {
      const char* part;
      int value;
    } ranks[] = {{"guitar", offer.rank_guitar}, {"drum", offer.rank_drum},
                 {"bass", offer.rank_bass},     {"vocals", offer.rank_vocals},
                 {"keys", offer.rank_keys}};
    bool opened_ranks = false;
    for (const auto& rank : ranks) {
      if (rank.value == 0) continue;
      out += opened_ranks ? " " : " (rank ";
      opened_ranks = true;
      out += rank.part;
      out += " ";
      out += std::to_string(rank.value);
    }
    if (opened_ranks) {
      out += ")";
    }
    out += ")\n";
  }
  out += " )\n)\n";
  return out;
}

}  // namespace rb_blitz::store
