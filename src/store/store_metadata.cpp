// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_metadata.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "store/album_art.h"
#include "ui/dtb_author.h"

namespace rb_blitz::store {
namespace {

using ui::dtb::Node;

// See store_metadata.h: the symbol form is what the retail provider's row label compares against,
// and it is the shipped default. `store_offer_type_symbol=0` selects the string spelling.
bool g_type_symbol = true;

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

// Every part, in the retail display's order and with its own spelling (`drum`, not the API's
// `drums`), with 0 where the catalogue has no rank. 0 is emitted rather than dropped: the details
// panel loops all five parts unconditionally (`{foreach part (guitar drum vocals bass keys)}`), and a
// *missing* part hands `{song_mgr rank_tier <null> <part>}` a null to dereference (measured
// 2026-10-21: the page's parse returns, then the guest faults on `read of guest 0x00000000` as the
// browser opens).
//
// This is the retail-shaped nested map. The details panel in *this* build reads the single-component
// `rank_<part>` keys instead (see `ExtraIntKeys`), because its `(rank <part>)` path cannot resolve
// against any five-part map; the nested map stays because it is the record shape the retail schema
// and the `by_difficulty` grouping describe, and `store_payload`'s unroll only changes the panel.
std::vector<std::pair<const char*, int>> OfferRanks(const Offer& offer) {
  return {{"guitar", offer.rank_guitar},
          {"drum", offer.rank_drum},
          {"bass", offer.rank_bass},
          {"vocals", offer.rank_vocals},
          {"keys", offer.rank_keys}};
}

// The offer keys the retail UI reads through `get_data` beyond the ones the `Offer` model carries,
// with an empty value where the catalogue has none. Taken from the shipped scripts, dumped back to
// DTA text with `ui::dtb::Parse` (`out/_swap/store-dta/`, 2026-10-21; `dtb_dump.cpp` there is the
// throwaway renderer):
//
//   ui/store/gen/store.dtb, `groupings`  - the table the browser's `refresh_list` ->
//       `{provider build_list {find syscfg store groupings sort_name}}` walks.
//       `by_release_date` reads `release_date_str`, `by_artist` reads `artist`,
//       `by_song_first_letter` reads `first_char`, `by_year_released` reads `year_released`,
//       `by_pack_first_letter` reads `pack_first_letter`, `by_author`/`by_label`/`by_subgenre` read
//       `author`/`label`/`subgenre`.
//   ui/store/gen/store_info.dtb           - `preview_audio`, `album_link`, `pack_link`, `pack_name`,
//       `rating`.
//
// None of those reads is guarded by `has_data`, so an *absent* key hands the DTA a null - which is
// the null read the page faulted on once it carried offers (boots 103-109; an empty page, boot 108,
// went through untouched). An empty string is a value; a missing key is not.
//
// `preview_audio` is written as an empty string for the same reason (see `Offer::preview_audio`),
// and `preview_art` only when it has a value: both are read *behind* `has_data` by the details
// panel, and an empty string there would be "present" and send the art loader after an empty path.
std::vector<std::pair<const char*, std::string>> ExtraTextKeys(const Offer& offer) {
  std::string first_char;
  if (!offer.name.empty()) {
    first_char.assign(1, offer.name.front());
  }
  return {{"release_date_str", std::string()},
          {"first_char", first_char},
          {"pack_first_letter", std::string()},
          {"author", std::string()},
          {"label", std::string()},
          {"subgenre", std::string()},
          {"album_link", std::string()},
          {"pack_link", std::string()},
          {"pack_name", std::string()},
          // Empty, never absent: `refresh_buttons` reads it for every song before it checks
          // `has_data` (see the Offer member). No preview asset exists (§3.3), so it is "".
          {"preview_audio", offer.preview_audio}};
}

// The integer keys the same scripts read: `review` is formatted by `{"sprintf" "review_%i" ...}`
// (a grouping token, not a score). Zero where the catalogue has none.
//
// `rating` is deliberately **not** written: both details panels read it behind `has_data` and draw
// `{sprintf "rating_%i" ...}` as a locale token, so a written 0 showed the literal text `RATING_0`
// on the A press (measured 2026-10-21); an absent key hides the whole rating group instead.
//
// `rank_<part>` is the retail-shaped part of the record: a single-component spelling of the same
// value the `(rank ((guitar N) ...))` map carries. Measured 2026-10-21 by decoding the offer's
// `get_data` (`sub_82453928` -> `sub_824538A8` -> the path walk `sub_824534A8`): each path component
// is interned and then `sub_823FAE00` looks for a pair whose FIRST element equals it, so a
// two-component `(rank <part>)` asks the `rank` pair for a child whose first element is the part - a
// five-part map cannot answer that for more than one part, the lookup returns nothing, and the walk
// dereferences the null (`read of guest 0x00000000`, reproduced in boot `rb_blitz_165.log`).
//
// `diff_<part>` is the **tier** the difficulty meter draws (`Offer::diff_guitar`): the payload's
// unrolled block reads it and hands it to `set_inst_difficulty` unchanged, because that call takes a
// tier and the catalogue already publishes one. Single-component for the same reason as
// `rank_<part>`.
std::vector<std::pair<const char*, int>> ExtraIntKeys(const Offer& offer) {
  return {{"review", 0},
          {"rank_guitar", offer.rank_guitar},
          {"rank_drum", offer.rank_drum},
          {"rank_bass", offer.rank_bass},
          {"rank_vocals", offer.rank_vocals},
          {"rank_keys", offer.rank_keys},
          {"diff_guitar", offer.diff_guitar},
          {"diff_drum", offer.diff_drum},
          {"diff_bass", offer.diff_bass},
          {"diff_vocals", offer.diff_vocals},
          {"diff_keys", offer.diff_keys}};
}

}  // namespace

void SetOfferTypeSymbol(bool symbol) { g_type_symbol = symbol; }

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

Offer OfferFromSong(const SongFile& song) {
  Offer offer;
  // The site's `file_title` is the canonical title; `file_name` (the upload name) is the fallback
  // when the record carries no title at all, and it is what the catalogue shows for such rows.
  offer.name = song.title.empty() ? song.file_name : song.title;
  offer.artist = song.artist;
  offer.album = song.album;
  offer.genre = song.genre;
  offer.year_released = song.year.value_or(0);
  // Album art (plan §1.3): the API points at a cover on its own host. The record carries only the
  // path the retail loader will ask for; the texture behind it is produced by the art worker
  // (src/store/album_art_service.h) and served from the file cache. An empty field, a field that is
  // not a RhythmVerse album-art path, or the art switch turned off leaves `preview_art` empty, which
  // is what makes the retail panel draw its own placeholder instead of a broken image.
  if (AlbumArtEnabled()) {
    const std::string key = ArtKeyForSong(song.file_id, song.artist, offer.name);
    const ResolvedUrl art = ResolveAlbumArtUrl(song.album_art);
    if (!key.empty() && art.ok) {
      offer.art_url = art.url;
      offer.preview_art = ArtPathForKey(key);
    }
  }
  const auto rank = [&song](Instrument instrument) {
    const int value = song.rank[static_cast<std::size_t>(instrument)].value_or(0);
    return value > 0 ? value : 0;  // a missing or non-positive rank is "unknown", never a tier
  };
  offer.rank_guitar = rank(Instrument::kGuitar);
  offer.rank_drum = rank(Instrument::kDrums);  // the retail part token is `drum`, the API's `drums`
  offer.rank_bass = rank(Instrument::kBass);
  offer.rank_vocals = rank(Instrument::kVocals);
  offer.rank_keys = rank(Instrument::kKeys);
  // The value the difficulty meter draws, as `(diff_<part> N)` keys. The panel's
  // `{this set_inst_difficulty <id> <tier> TRUE}` is passed straight through by the payload, and
  // the meter lights `id - 1` pips for `id` 1..6 - measured on screen 2026-10-22 against three
  // offers with known catalogue tiers: `otro-dia-que-va` (5/4/3/3/0) drew 4/3/2/2/NO PART,
  // `sylvia` (4/3/1/1/3) drew 3/2/0/0/2, and `heart-full-of-soul` (4/3/3/3/0) drew 3/2/2/2/NO PART
  // (`out/_swap/store-probe/20261022-081144`, `-082057`, `-082450`). A deliberate one-step shift
  // (`out/_swap/store-probe/20261022-083045`) lit all five pips at id 6, so the meter's own scale
  // is 0 = `NO PART`, 1..6 = 0..5 pips. The catalogue's tiers are 1..6, so the mapping below keeps
  // tier 0 as `NO PART` and moves 1..6 onto 2..6: the easiest chart draws one pip instead of a row
  // that reads like a missing part, and tier 6 (and the API's 7, an unplayable chart) draws the
  // five the meter has. The devil icon is never asked for: nothing tells us which value draws it.
  const auto tier = [&song](Instrument instrument) {
    const int value = song.difficulty[static_cast<std::size_t>(instrument)].value_or(0);
    if (value <= 0) return 0;
    return std::min(value + 1, 6);
  };
  offer.diff_guitar = tier(Instrument::kGuitar);
  offer.diff_drum = tier(Instrument::kDrums);
  offer.diff_bass = tier(Instrument::kBass);
  offer.diff_vocals = tier(Instrument::kVocals);
  offer.diff_keys = tier(Instrument::kKeys);
  return offer;
}

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
  // Same shapes `BuildMenuText` writes, in the compiled form: the root is three pairs and every
  // offer is a list of pairs. The engine's map convention (`sub_823FAE00`) is that a lookup matches a
  // node that is itself an array whose first element is the key; the text parser produces exactly
  // that from `(key value)`.
  std::vector<Node> rows;
  for (const Offer& offer : offers) {
    const auto pair = [](std::string_view key, const std::string& value) {
      return Node::Array({Symbol(key), Text(value)});
    };
    std::vector<Node> fields{pair("name", offer.name)};
    // The details panel asks `{offer offer_type}` and compares it to `song`/`pack`/`album`, but the
    // *record* key the shipped scripts write is `type` - `store.dtb`'s own example is
    // `("dummy_upsell_offer" ("type" "song") ("name" {"localize" "unknown_song"}))`. The node kind
    // is the measurement switch in store_metadata.h: a symbol is what the provider compares against
    // to answer a row's label, a string is the shipped spelling.
    fields.push_back(Node::Array(
        {Symbol("type"), g_type_symbol ? Symbol("song") : Text("song")}));
    fields.push_back(Node::Array({Symbol("offer_type"), Symbol("song")}));
    // Every field is emitted even when it is empty: `process_offers` reads all of them, and a
    // missing key hands it a null to further down (boot 103).
    fields.push_back(pair("artist", offer.artist));
    fields.push_back(pair("album", offer.album));
    fields.push_back(pair("genre", offer.genre));
    fields.push_back(Node::Array({Symbol("year_released"), Node::Int(offer.year_released)}));
    // Only when it has a value: an empty `preview_art` is "present" to `has_data` and would send the
    // art loader after an empty path (see ExtraTextKeys).
    if (!offer.preview_art.empty()) {
      fields.push_back(pair("preview_art", offer.preview_art));
    }
    for (const auto& extra : ExtraTextKeys(offer)) {
      fields.push_back(pair(extra.first, extra.second));
    }
    for (const auto& extra : ExtraIntKeys(offer)) {
      fields.push_back(Node::Array({Symbol(extra.first), Node::Int(extra.second)}));
    }
    std::vector<Node> rank_pairs;
    for (const auto& part : OfferRanks(offer)) {
      rank_pairs.push_back(Node::Array({Symbol(part.first), Node::Int(part.second)}));
    }
    fields.push_back(Node::Array({Symbol("rank"), Node::Array(std::move(rank_pairs))}));
    // The Downloaded state (plan §3.2), as the key the retail scripts read it from. Written only
    // when true: an absent key is falsey, and a receipt never sets it - only the loaded library.
    if (offer.downloaded) {
      fields.push_back(Node::Array({Symbol("is_purchased"), Node::Int(offer.downloaded ? 1 : 0)}));
    }
    rows.push_back(Node::Array(std::move(fields)));
  }
  // The root `sub_8274F610` reads: three pairs, `offers_populated`, `sorted` and `offers`, and one
  // **root array**, which is the same map the text writer emits.
  //
  // `sorted` is the **sort name** the browser reads back as `{store_panel sort_name}`, not a boolean
  // and not an index list: `refresh_list` compares it with "" and, when it is empty, calls
  // `build_list_no_grouping`; otherwise it looks the value up in the shipped `groupings` table. An
  // index list there answers nothing, so `build_list` produced a list with no rows - which is what
  // boots 112-117 showed (the browser open, its row band drawn, every row blank). Recent has no
  // grouping of its own, so it writes the empty name.
  // One root array with flat key/value pairs - the convention `BuildFrontMenuRecord` writes and
  // the suite reads (`(offers_populated TRUE sorted "" offers (...))`). `ui::dtb::Compile`
  // serializes every top-level node as its own root, so the map must be a single node.
  return ui::dtb::Compile({Node::Array({Symbol("offers_populated"), Symbol("TRUE"),
                                        Symbol("sorted"), Text(""), Symbol("offers"),
                                        Node::Array(std::move(rows))})});
}

std::string BuildFrontMenuText(const FrontMenu& menu, std::string* why) {
  if (!ValidateFrontMenu(menu, why)) {
    return {};
  }
  // A DTA text file is one map: `(key value key value ...)`. The panel looks `submenus` up by
  // symbol (`sub_823FAE00(array, Sym("submenus"), 0)`), and each row is itself an **array whose
  // first element is the provider path**: `sub_8275C968` reads an entry as an array and counts its
  // elements (`> 2`, the third one tested non-null), and `sub_8275CBD8` compares the entry's first
  // string against a provider name. A bare quoted path (the first shape) is a *string* node, which
  // those two readers walk as if it were an array - measured 2026-10-20 evening, the builder's
  // first call faulted on `read of 0x44` right after the entry walk.
  std::string out = "(submenus\n";
  for (const FrontMenuEntry& entry : menu.entries) {
    out += "  (";
    out += Quote(MenuProviderPath(menu.locale, entry.id));
    out += " ";
    out += Quote(entry.label);
    out += " TRUE)\n";
  }
  out += ")\n(new_releases ";
  out += Quote(MenuProviderPath(menu.locale, menu.entries.front().id));
  out += ")\n";
  return out;
}

std::string BuildMenuText(const std::vector<Offer>& offers, std::string* why) {
  if (!ValidateOffers(offers, why)) {
    return {};
  }
  // The pair convention (see BuildMenuRecord): a lookup matches a sub-array whose first node is the
  // key, so the root is three pairs and every offer is a list of pairs. `rank` is two levels -
  // `(rank ((guitar 3) (drum 1)))` - because the details panel asks for `get_data (rank guitar)`.
  // Measured 2026-10-21: the root pairs are read correctly (boot 101 logged `offers_populated`,
  // `sorted`, `offers`), and a *flat* offer body made the parser fault on a null read while walking
  // the offers, so each offer goes back to one pair per field.
  std::string out = "(offers_populated TRUE)\n(sorted \"\")\n(offers\n";
  const auto pair_text = [](std::string_view key, const std::string& value) {
    return "    (" + std::string(key) + " " + Quote(value) + ")\n";
  };
  for (const Offer& offer : offers) {
    out += "  (\n";
    out += pair_text("name", offer.name);
    // See store_metadata.h: a symbol makes the provider answer the row's label, a string is the
    // shipped spelling. `store_offer_type_symbol` decides which one is served.
    out += g_type_symbol ? "    (type song)\n" : "    (type \"song\")\n";
    out += "    (offer_type song)\n";
    out += pair_text("artist", offer.artist);
    out += pair_text("album", offer.album);
    out += pair_text("genre", offer.genre);
    out += "    (year_released " + std::to_string(offer.year_released) + ")\n";
    // Only when it has a value: `has_data preview_art` is true for an empty string too, and the art
    // loader would then be handed an empty path (`store_details_panel.refresh`).
    if (!offer.preview_art.empty()) {
      out += pair_text("preview_art", offer.preview_art);
    }
    for (const auto& extra : ExtraTextKeys(offer)) {
      out += pair_text(extra.first, extra.second);
    }
    for (const auto& extra : ExtraIntKeys(offer)) {
      out += "    (" + std::string(extra.first) + " " + std::to_string(extra.second) + ")\n";
    }
    // The Downloaded state (plan §3.2). Retail's `store_info_panel.refresh_buttons` inserts
    // `store_download_again` for it, but its own gate reads the offer's **native** `is_purchased`,
    // which the payload redirects to this key (`store_payload.h`, `UseDataForDownloadedRow`).
    //
    // Written on **every** offer, as an int: a missing key makes that data read walk a null
    // (measured 2026-10-22, `read of 0x00000000` on an unmarked offer's details page,
    // `out/_swap/store-probe/20261022-094505`), and an int is the one value whose truth is not a
    // spelling question - 1 shows the row, 0 leaves the page as retail draws it.
    out += offer.downloaded ? "    (is_purchased 1)\n" : "    (is_purchased 0)\n";
    // `is_available` is deliberately not written: `purchase_allowed` needs it (so an absent key
    // keeps the price block and the Buy Now row from being built), and writing it FALSE made the
    // details path answer `{offer is_completely_unavailable}` true - measured 2026-10-21, the A
    // press landed on `store_info_unavailable_screen` with it and on retail's offer enumeration
    // without it.
    const std::vector<std::pair<const char*, int>> parts = OfferRanks(offer);
    out += "    (rank (";
    for (const auto& part : parts) {
      out += "(";
      out += part.first;
      out += " ";
      out += std::to_string(part.second);
      out += ")";
    }
    out += "))\n";
    out += "  )\n";
  }
  out += ")\n";
  return out;
}

}  // namespace rb_blitz::store
