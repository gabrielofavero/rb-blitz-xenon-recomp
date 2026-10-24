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
//   * the owner's front menu is Recently Added then Search and nothing else;
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
using rb_blitz::store::BuildMenuText;
using rb_blitz::store::FrontMenu;
using rb_blitz::store::FrontMenuEntry;
using rb_blitz::store::FrontMenuRecordPath;
using rb_blitz::store::FrontMenuRow;
using rb_blitz::store::FrontMenuRowForm;
using rb_blitz::store::FrontMenuRowFormToken;
using rb_blitz::store::Instrument;
using rb_blitz::store::MenuProviderPath;
using rb_blitz::store::MenuRecordPath;
using rb_blitz::store::MenuSourceId;
using rb_blitz::store::Offer;
using rb_blitz::store::OfferDirectory;
using rb_blitz::store::OfferFromSong;
using rb_blitz::store::RelatedOfferPath;
using rb_blitz::store::kRecentlyAddedLabel;
using rb_blitz::store::kRecentlyAddedMenuId;
using rb_blitz::store::kSearchMenuId;
using rb_blitz::store::kSearchMenuLabel;
using rb_blitz::store::kSearchMenuRow;
using rb_blitz::store::ParseFrontMenuRowForm;
using rb_blitz::store::RhythmVerseFrontMenu;
using rb_blitz::store::SetFrontMenuRowForm;
using rb_blitz::store::SongFile;
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
//
// An offer body is an array of **pair arrays** - `((name "One Week") (artist "...") ...)` - the
// convention `store.dtb`'s own `dummy_upsell_offer` demonstrates and both writers emit (the text
// form writes one `(key value)` per line).
std::string OfferField(const Node& offer, std::string_view key) {
  for (const Node& field : offer.kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.size() < 2) continue;
    if (field.kids[0].tag != rb_blitz::ui::dtb::kSymbol || field.kids[0].text != key) continue;
    if (field.kids[1].tag != rb_blitz::ui::dtb::kString) continue;
    return field.kids[1].text;
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
  BeginCase("the owner's front menu is Recently Added then Search");
  const FrontMenu menu = RhythmVerseFrontMenu();
  CHECK_EQ(menu.entries.size(), size_t{2});
  if (menu.entries.size() == 2) {
    CHECK_STR_EQ(menu.entries[0].id, "recent");
    CHECK_STR_EQ(menu.entries[0].label, "Recently Added");
    CHECK_STR_EQ(menu.entries[1].id, "search");
    CHECK_STR_EQ(menu.entries[1].label, "Search");
  }
  // The constants the writers, the tests and the payload's row gate share are the values above:
  // the id is the measured provider name, the label is what the player reads (P9-5's rename).
  CHECK_STR_EQ(std::string(kRecentlyAddedMenuId), "recent");
  CHECK_STR_EQ(std::string(kRecentlyAddedLabel), "Recently Added");
  CHECK_STR_EQ(std::string(kSearchMenuLabel), "Search");
  // The store's DTA gate presses by *index* (`{== $ix <row>}`), so the constant the payload hands
  // the transform and the row the writer emits have to stay the same fact.
  CHECK_TRUE(kSearchMenuRow >= 0 &&
             static_cast<size_t>(kSearchMenuRow) < menu.entries.size());
  if (kSearchMenuRow >= 0 && static_cast<size_t>(kSearchMenuRow) < menu.entries.size()) {
    CHECK_STR_EQ(menu.entries[static_cast<size_t>(kSearchMenuRow)].id, kSearchMenuId);
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
  nameless.entries.push_back({"", "Recently Added"});
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
  for (const Node& field : offer.kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.size() < 2) continue;
    if (field.kids[0].tag != rb_blitz::ui::dtb::kSymbol) continue;
    if (field.kids[0].text == "year_released" &&
        field.kids[1].tag == rb_blitz::ui::dtb::kInt && field.kids[1].scalar == 1998) {
      year_ok = true;
    }
    if (field.kids[0].text == "rank" && field.kids[1].tag == rb_blitz::ui::dtb::kArray) {
      for (const Node& rank : field.kids[1].kids) {
        if (rank.tag != rb_blitz::ui::dtb::kArray || rank.kids.size() < 2) continue;
        if (rank.kids[0].text == "guitar" && rank.kids[1].tag == rb_blitz::ui::dtb::kInt &&
            rank.kids[1].scalar == 3) {
          rank_ok = true;
        }
      }
    }
  }
  CHECK_TRUE(year_ok);
  CHECK_TRUE(rank_ok);
}

// P5's identity on the offer (store_metadata.h, `Offer::job`): the record carries a token, never the
// file id or the URL, and it carries the key on **every** offer - the details page's Download row
// reads it unconditionally, so a missing key is a null walk rather than a hidden row.
void TestDownloadIdentity() {
  BeginCase("an offer's job token is the record's only download spelling");
  SongFile song;
  song.file_id = "abc/../def";
  song.artist = "Bush";
  song.title = "The People That We Love";
  song.size_bytes = 12345;
  const std::string url = "https://example.invalid/download/abc";
  const Offer offer = OfferFromSong(song, url);
  // Host-only: the identity the worker needs is on the offer, and the file id is never a record key.
  CHECK_STR_EQ(offer.file_id, "abc/../def");
  CHECK_STR_EQ(offer.download_url, url);
  CHECK_TRUE(offer.size_bytes.has_value() && *offer.size_bytes == 12345);
  // A caller with no resolved URL (a test, or a song the catalogue did not offer) leaves the offer
  // without one, which is what makes the serving path skip the job.
  CHECK_TRUE(OfferFromSong(song).download_url.empty());

  // The token the serving path would fill in; both writers carry it under `job`, and neither writes
  // the file id or the URL anywhere.
  Offer listed = offer;
  listed.job = "j0123456789abcdef";
  std::string why;
  const std::string text = BuildMenuText({listed}, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(text.find("(job \"j0123456789abcdef\")") != std::string::npos);
  CHECK_TRUE(text.find("abc/../def") == std::string::npos);
  CHECK_TRUE(text.find("example.invalid") == std::string::npos);
  const std::vector<uint8_t> bytes = BuildMenuRecord({listed}, &why);
  CHECK_TRUE(why.empty());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
  const Node* offers = RootArray(roots, "offers");
  CHECK_TRUE(offers != nullptr && offers->kids.size() == size_t{1});
  if (offers == nullptr || offers->kids.empty()) return;
  CHECK_STR_EQ(OfferField(offers->kids[0], "job"), "j0123456789abcdef");

  // An offer with no job still carries the key (the empty string): a page served before the
  // download service is configured is still a page whose rows can be read.
  const std::string bare_text = BuildMenuText({offer}, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(bare_text.find("(job \"\")") != std::string::npos);
  const std::vector<uint8_t> bare_bytes = BuildMenuRecord({offer}, &why);
  CHECK_TRUE(why.empty());
  std::vector<Node> bare_roots;
  rb_blitz::ui::dtb::RootHeader bare_header;
  CHECK_TRUE(
      rb_blitz::ui::dtb::Parse(bare_bytes.data(), bare_bytes.size(), &bare_roots, &bare_header, &why));
  const Node* bare_offers = RootArray(bare_roots, "offers");
  CHECK_TRUE(bare_offers != nullptr && !bare_offers->kids.empty());
  if (bare_offers == nullptr || bare_offers->kids.empty()) return;
  CHECK_STR_EQ(OfferField(bare_offers->kids[0], "job"), "");
}

void TestSongMapping() {
  BeginCase("a catalogue record becomes an offer with its own difficulty tier per part");
  SongFile song;
  song.file_id = "abc";
  song.artist = "Bush";
  song.title = "The People That We Love";
  song.album = "Golden State";
  song.genre = "Pop-Rock";
  song.year = 2001;
  const auto slot = [](rb_blitz::store::Instrument instrument) {
    return static_cast<std::size_t>(instrument);
  };
  song.difficulty[slot(rb_blitz::store::Instrument::kGuitar)] = 4;
  song.difficulty[slot(rb_blitz::store::Instrument::kDrums)] = 3;
  song.difficulty[slot(rb_blitz::store::Instrument::kBass)] = 3;
  song.difficulty[slot(rb_blitz::store::Instrument::kVocals)] = 3;
  song.difficulty[slot(rb_blitz::store::Instrument::kKeys)] = 0;
  song.rank[slot(rb_blitz::store::Instrument::kGuitar)] = 221;

  const Offer offer = OfferFromSong(song);
  CHECK_STR_EQ(offer.name, "The People That We Love");
  CHECK_STR_EQ(offer.artist, "Bush");
  CHECK_EQ(offer.year_released, 2001);
  // The value the difficulty meter draws, which is the catalogue's tier moved onto the meter's own
  // scale: measured on screen 2026-10-22, the meter lights `id - 1` pips for id 1..6, so tier N is
  // sent as N + 1 and tier 0 (a part the song does not chart) stays 0, which draws `no_part`.
  CHECK_EQ(offer.diff_guitar, 5);
  CHECK_EQ(offer.diff_drum, 4);
  CHECK_EQ(offer.diff_bass, 4);
  CHECK_EQ(offer.diff_vocals, 4);
  CHECK_EQ(offer.diff_keys, 0);
  // The rank is the retail schema's own value, a different number from the tier.
  CHECK_EQ(offer.rank_guitar, 221);
  CHECK_EQ(offer.rank_drum, 0);

  // The ends of the scale: the easiest chart draws one pip (it is not a missing part), an
  // unplayable one (the API publishes 7) draws the five the meter has, and a negative is "no part".
  SongFile edges = song;
  edges.difficulty[slot(rb_blitz::store::Instrument::kGuitar)] = 1;
  edges.difficulty[slot(rb_blitz::store::Instrument::kDrums)] = 6;
  edges.difficulty[slot(rb_blitz::store::Instrument::kBass)] = 9;
  edges.difficulty[slot(rb_blitz::store::Instrument::kVocals)] = -2;
  const Offer shifted = OfferFromSong(edges);
  CHECK_EQ(shifted.diff_guitar, 2);
  CHECK_EQ(shifted.diff_drum, 6);
  CHECK_EQ(shifted.diff_bass, 6);
  CHECK_EQ(shifted.diff_vocals, 0);

  // Both writers carry the keys the payload reads (`(diff_<part>)`), and the compiled one as
  // integers.
  std::string why;
  const std::string text = BuildMenuText({offer}, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(text.find("(diff_guitar 5)") != std::string::npos);
  CHECK_TRUE(text.find("(diff_keys 0)") != std::string::npos);
  CHECK_TRUE(text.find("(rank_guitar 221)") != std::string::npos);
  const std::vector<uint8_t> bytes = BuildMenuRecord({offer}, &why);
  CHECK_TRUE(why.empty());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
  const Node* offers = RootArray(roots, "offers");
  CHECK_TRUE(offers != nullptr && offers->kids.size() == size_t{1});
  if (offers == nullptr || offers->kids.empty()) return;
  bool diff_ok = false;
  for (const Node& field : offers->kids[0].kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.size() < 2) continue;
    if (field.kids[0].tag != rb_blitz::ui::dtb::kSymbol) continue;
    if (field.kids[0].text == "diff_guitar" && field.kids[1].tag == rb_blitz::ui::dtb::kInt &&
        field.kids[1].scalar == 5) {
      diff_ok = true;
    }
  }
  CHECK_TRUE(diff_ok);
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

// P9-13: the details page's credit block reads four record keys (`audio_type`, `charter`,
// `rbbz_release_date`, `downloads`), so both writers carry all four on **every** offer - an empty value
// included, because `get_data` on a missing key walks a null (the trap P3 measured on
// `(rank <part>)`).
void TestOfferCredits() {
  BeginCase("an offer carries the custom's credit facts, empty values included");

  // The site's own dropdown labels (plan §9/P9-13). `karaoke` is the one code neither committed
  // capture carries; an unknown code is empty, so the raw token can never reach a record.
  CHECK_STR_EQ(std::string(rb_blitz::store::AudioTypeLabel("single")), "Single Track");
  CHECK_STR_EQ(std::string(rb_blitz::store::AudioTypeLabel("full")), "Multitrack");
  CHECK_STR_EQ(std::string(rb_blitz::store::AudioTypeLabel("partial")), "Partial Multitracks");
  CHECK_STR_EQ(std::string(rb_blitz::store::AudioTypeLabel("karaoke")), "Karaoke");
  CHECK_TRUE(rb_blitz::store::AudioTypeLabel("stems").empty());
  CHECK_TRUE(rb_blitz::store::AudioTypeLabel("").empty());
  CHECK_TRUE(rb_blitz::store::AudioTypeLabel("Single").empty());  // codes are lower case

  SongFile song;
  song.file_id = "abc";
  song.artist = "Youth Group";
  song.title = "Forever Young";
  song.audio_type = "single";
  song.charter = "Virtual-Man";
  song.release_date = "2026-10-11";
  song.downloads = 61;
  const Offer offer = OfferFromSong(song);
  CHECK_STR_EQ(offer.audio_type, "single");  // the code stays the model's storage
  CHECK_STR_EQ(offer.charter, "Virtual-Man");
  CHECK_STR_EQ(offer.release_date, "2026-10-11");
  CHECK_EQ(offer.downloads, 61);

  std::string why;
  const std::string text = BuildMenuText({offer}, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(text.find("(audio_type \"Single Track\")") != std::string::npos);
  CHECK_TRUE(text.find("(charter \"Virtual-Man\")") != std::string::npos);
  CHECK_TRUE(text.find("(rbbz_release_date \"2026-10-11\")") != std::string::npos);
  CHECK_TRUE(text.find("(downloads 61)") != std::string::npos);
  // Retail consumes release_date as (key year month day) during offer
  // construction, before a UI script runs. Display metadata must not claim it.
  CHECK_TRUE(text.find("(release_date ") == std::string::npos);
  // The code itself is storage, never a record value.
  CHECK_TRUE(text.find("(audio_type \"single\")") == std::string::npos);

  const std::vector<uint8_t> bytes = BuildMenuRecord({offer}, &why);
  CHECK_TRUE(why.empty());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
  const Node* offers = RootArray(roots, "offers");
  CHECK_TRUE(offers != nullptr && offers->kids.size() == size_t{1});
  if (offers == nullptr || offers->kids.empty()) return;
  CHECK_STR_EQ(OfferField(offers->kids[0], "audio_type"), "Single Track");
  CHECK_STR_EQ(OfferField(offers->kids[0], "charter"), "Virtual-Man");
  CHECK_STR_EQ(OfferField(offers->kids[0], "rbbz_release_date"), "2026-10-11");
  for (const Node& field : offers->kids[0].kids) {
    CHECK_FALSE(field.tag == rb_blitz::ui::dtb::kArray && !field.kids.empty() &&
                field.kids[0].text == "release_date");
  }
  bool downloads_ok = false;
  for (const Node& field : offers->kids[0].kids) {
    if (field.tag != rb_blitz::ui::dtb::kArray || field.kids.size() < 2) continue;
    if (field.kids[0].tag == rb_blitz::ui::dtb::kSymbol && field.kids[0].text == "downloads" &&
        field.kids[1].tag == rb_blitz::ui::dtb::kInt && field.kids[1].scalar == 61) {
      downloads_ok = true;
    }
  }
  CHECK_TRUE(downloads_ok);

  // A record the catalogue could not credit still carries all four keys, empty: the payload's
  // block reads them unconditionally, so a missing key would be a null walk, not a hidden line.
  SongFile bare;
  bare.file_id = "abc";
  bare.artist = "Nobody";
  bare.title = "Untitled";
  const Offer blank = OfferFromSong(bare);
  const std::string bare_text = BuildMenuText({blank}, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(bare_text.find("(audio_type \"\")") != std::string::npos);
  CHECK_TRUE(bare_text.find("(charter \"\")") != std::string::npos);
  CHECK_TRUE(bare_text.find("(rbbz_release_date \"\")") != std::string::npos);
  CHECK_TRUE(bare_text.find("(downloads 0)") != std::string::npos);
}

void TestFrontMenuRowForm() {
  BeginCase("a front-menu row's count form defaults to the no-count marker");
  // P9-5: the front-menu list's `count` label formats the row's third element, and only the
  // fourth-element `random_song` marker makes `sub_8275C9D8` skip it (store_metadata.h). The
  // shipped default is therefore the marker, and the other three spellings exist for the boot that
  // compares them.
  CHECK_TRUE(FrontMenuRow() == FrontMenuRowForm::kNoCount);
  CHECK_STR_EQ(std::string(FrontMenuRowFormToken(FrontMenuRowForm::kCount)), "count");
  CHECK_STR_EQ(std::string(FrontMenuRowFormToken(FrontMenuRowForm::kFalse)), "false");
  CHECK_STR_EQ(std::string(FrontMenuRowFormToken(FrontMenuRowForm::kZero)), "zero");
  CHECK_STR_EQ(std::string(FrontMenuRowFormToken(FrontMenuRowForm::kNoCount)), "no-count");

  // Every spelling the diagnostic documents parses, and a typo changes nothing - a boot whose cvar
  // is mistyped must serve the default rather than a form nobody measured.
  FrontMenuRowForm parsed = FrontMenuRowForm::kCount;
  CHECK_TRUE(ParseFrontMenuRowForm("false", &parsed));
  CHECK_TRUE(parsed == FrontMenuRowForm::kFalse);
  CHECK_TRUE(ParseFrontMenuRowForm("zero", &parsed));
  CHECK_TRUE(parsed == FrontMenuRowForm::kZero);
  CHECK_TRUE(ParseFrontMenuRowForm("no-count", &parsed));
  CHECK_TRUE(parsed == FrontMenuRowForm::kNoCount);
  CHECK_TRUE(ParseFrontMenuRowForm("count", &parsed));
  CHECK_TRUE(parsed == FrontMenuRowForm::kCount);

  parsed = FrontMenuRowForm::kNoCount;
  CHECK_FALSE(ParseFrontMenuRowForm("", &parsed));
  CHECK_FALSE(ParseFrontMenuRowForm("NoCount", &parsed));
  CHECK_FALSE(ParseFrontMenuRowForm("no count", &parsed));
  CHECK_FALSE(ParseFrontMenuRowForm("1", &parsed));
  CHECK_TRUE(parsed == FrontMenuRowForm::kNoCount);  // left alone

  SetFrontMenuRowForm(FrontMenuRowForm::kZero);
  CHECK_TRUE(FrontMenuRow() == FrontMenuRowForm::kZero);
  SetFrontMenuRowForm(FrontMenuRowForm::kNoCount);
  CHECK_TRUE(FrontMenuRow() == FrontMenuRowForm::kNoCount);
}

}  // namespace

int main() {
  TestRequestNames();
  TestOwnerFrontMenu();
  TestFrontMenuRowForm();
  TestWriterRefusals();
  TestMenuWriterRefusals();
  TestRecordRoundTrip();
  TestDownloadIdentity();
  TestSongMapping();
  TestMenuRecordRoundTrip();
  TestEmptyMenuRecord();
  TestOfferCredits();
  return rb_blitz::test::Finish();
}
