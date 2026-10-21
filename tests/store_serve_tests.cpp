// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for P3's data lane: the text record writers (src/store/store_metadata.cpp) and the
// serving policy (src/store/store_serve.cpp). SDK-free and hermetic - no boot, no guest, no
// network; the served-file cases use a scratch directory under the OS temp root.
//
// What these pin down:
//   * a `.dta` record served to the netcache is **text**: the insert parses that type with the
//     engine's text reader (`sub_82418878`, tokenizer `sub_82417BF8` compares tokens against
//     `#define`/`#merge`/`#ifdef`/`#autorun`). Measured 2026-10-20 evening - the same record served
//     as compiled DTB parsed to one stray symbol node, while the text form parsed to the
//     `submenus` map;
//   * the front menu is `(submenus "<path>" ...)` with the owner's Recent then Search, and the same
//     three refusals as the compiled writer (no entries, empty id, empty label);
//   * the menu record is `(offers_populated TRUE sorted (...) offers (...))` with one pair per offer
//     field, `sorted` as the display order, and every `get_data` key present; an empty page is
//     valid, and a nameless offer is refused (the text writer's own cases);
//   * the requests we answer are exactly the `dlc_store/` ones, and a serve directory can only be
//     read through plain relative paths (the path comes from the guest);
//   * the host file wins over the built-in record, and the built-in record answers the front menu.

#include "check.h"

#include "store/store_metadata.h"
#include "store/store_serve.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace {

using namespace rb_blitz::test;
using rb_blitz::store::BuildFrontMenuText;
using rb_blitz::store::BuildMenuText;
using rb_blitz::store::BuiltinRecord;
using rb_blitz::store::FrontMenu;
using rb_blitz::store::FrontMenuRecordPath;
using rb_blitz::store::IsMenuRecordPath;
using rb_blitz::store::IsStoreRequestPath;
using rb_blitz::store::Offer;
using rb_blitz::store::ReadServedRecord;
using rb_blitz::store::RecordForRequest;
using rb_blitz::store::RhythmVerseFrontMenu;

namespace fs = std::filesystem;

// check.h has no string comparison; the record text is exactly the value whose failure needs both
// sides printed.
void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq((actual), (expected))

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

void TestRequestPaths() {
  BeginCase("only dlc_store requests are ours");
  CHECK_TRUE(IsStoreRequestPath("dlc_store/europe/dlc_top_xbox_eng.dta"));
  CHECK_TRUE(IsStoreRequestPath("dlc_store/europe/eng/recent/"));
  CHECK_FALSE(IsStoreRequestPath("dlc_store/"));
  CHECK_FALSE(IsStoreRequestPath("dlc_store"));
  CHECK_FALSE(IsStoreRequestPath("dlc_stored/x.dta"));
  CHECK_FALSE(IsStoreRequestPath("ui/store/gen/store.dtb"));
  CHECK_FALSE(IsStoreRequestPath(""));
}

void TestFrontMenuText() {
  BeginCase("the front menu is text with the two owner entries");
  std::string why;
  const std::string text = BuildFrontMenuText(RhythmVerseFrontMenu(), &why);
  CHECK_TRUE(why.empty());
  CHECK_STR_EQ(text,
               "(submenus\n"
               "  (\"/eng/recent/\" \"Recent\" TRUE)\n"
               "  (\"/eng/search/\" \"Search\" TRUE)\n"
               ")\n"
               "(new_releases \"/eng/recent/\")\n");
  // The key is a bare symbol, each row a three-element array whose first element is the provider
  // path: the panel looks `submenus` up by symbol (`sub_8275D4E0`), `sub_8275CBD8` compares the
  // row's first string against a provider name, and `sub_8275C968` counts the row's elements.
  CHECK_TRUE(text.find("(submenus") == 0);
  CHECK_FALSE(Contains(text, "\"submenus\""));
  CHECK_TRUE(Contains(text, "(new_releases"));
}

void TestFrontMenuRefusals() {
  BeginCase("the text writer refuses what the compiled writer refuses");
  std::string why;
  CHECK_TRUE(BuildFrontMenuText(FrontMenu{}, &why).empty());
  CHECK_TRUE(Contains(why, "no entries"));

  FrontMenu nameless;
  nameless.entries.push_back({"", "Recent"});
  why.clear();
  CHECK_TRUE(BuildFrontMenuText(nameless, &why).empty());
  CHECK_TRUE(Contains(why, "empty id"));

  FrontMenu unlabelled;
  unlabelled.entries.push_back({"recent", ""});
  why.clear();
  CHECK_TRUE(BuildFrontMenuText(unlabelled, &why).empty());
  CHECK_TRUE(Contains(why, "empty label"));
}

void TestMenuText() {
  BeginCase("the menu record is text and carries the keys the panel reads");
  std::vector<Offer> offers(1);
  offers[0].name = "The People That We Love";
  offers[0].artist = "Bush";
  offers[0].album = "Golden State";
  offers[0].genre = "Rock";
  offers[0].year_released = 2001;
  offers[0].rank_guitar = 3;
  offers[0].rank_drum = 1;
  std::string why;
  const std::string text = BuildMenuText(offers, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(Contains(text, "(offers_populated TRUE"));
  // `sorted` is the name `refresh_list` reads back as `{store_panel sort_name}`; empty means "no
  // grouping", which is the plain list Recent asks for.
  CHECK_TRUE(Contains(text, "(sorted \"\")"));
  // Each offer is one list of pairs on its own line, under the `offers` root.
  CHECK_TRUE(Contains(text, "(offers\n"));
  CHECK_TRUE(Contains(text, "(name \"The People That We Love\")"));
  // `type` defaults to a **symbol** - what the retail provider compares to answer a row's label
  // (store_metadata.h). The string form is the shipped `dummy_upsell_offer`'s spelling and is
  // pinned below through the measurement switch.
  CHECK_TRUE(Contains(text, "(type song)"));
  CHECK_FALSE(Contains(text, "(type \"song\")"));
  CHECK_TRUE(Contains(text, "(offer_type song)"));
  CHECK_TRUE(Contains(text, "(artist \"Bush\")"));
  CHECK_TRUE(Contains(text, "(year_released 2001)"));
  // One pair per field, and `rank` two levels deep: `get_data (rank guitar)` walks into it. Every
  // part is present even at 0 - the details panel loops all five and a missing one is a null that
  // `rank_tier` dereferences (boot 112 had the browser open only once all five were written).
  CHECK_TRUE(Contains(text, "(rank ((guitar 3)(drum 1)(bass 0)(vocals 0)(keys 0)))"));
  // The same five values as **single-component** keys. The details panel's `(rank <part>)` read is
  // a two-component path the offer walk cannot resolve (measured 2026-10-21; store_payload.h's
  // `UnrollOfferRanks`), so the payload unrolls the loop onto these, which use the same lookup the
  // offer's `name` already answers.
  CHECK_TRUE(Contains(text, "(rank_guitar 3)"));
  CHECK_TRUE(Contains(text, "(rank_drum 1)"));
  CHECK_TRUE(Contains(text, "(rank_bass 0)"));
  CHECK_TRUE(Contains(text, "(rank_vocals 0)"));
  CHECK_TRUE(Contains(text, "(rank_keys 0)"));
  // Every other key the retail scripts read through `get_data` is present even when it is empty -
  // the browser's groupings do not guard with `has_data`, and an absent key is a null there.
  CHECK_TRUE(Contains(text, "(release_date_str \"\")"));
  CHECK_TRUE(Contains(text, "(first_char \"T\")"));
  // `preview_audio` is present but empty: RhythmVerse has no preview asset (§3.3), yet
  // `refresh_buttons` reads the key unconditionally for a song and a missing one makes the offer
  // walk dereference null (measured 2026-10-21, the A press on a served offer). `preview_art`
  // stays absent when empty, because its reader is guarded by `has_data` and an empty path would
  // be handed to the art loader.
  CHECK_TRUE(Contains(text, "(preview_audio \"\")"));
  CHECK_FALSE(Contains(text, "(preview_art"));
  // `is_available` is deliberately absent: `purchase_allowed` needs it, so no price block and no
  // Buy Now row is ever built, and writing it FALSE made `{offer is_completely_unavailable}` answer
  // true (measured 2026-10-21: the A press landed on the unavailable screen).
  CHECK_FALSE(Contains(text, "is_available"));
  // Not downloaded: no `is_purchased` key at all (absent is falsey).
  CHECK_FALSE(Contains(text, "is_purchased"));

  // A page with no offers is valid (P3's exit requires empty/filtered-only pages to work).
  why.clear();
  const std::string empty = BuildMenuText({}, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(Contains(empty, "(offers\n"));
  CHECK_FALSE(Contains(empty, "(name"));

  // A nameless offer is refused before anything is written.
  std::vector<Offer> nameless(1);
  nameless[0].artist = "Bush";
  why.clear();
  CHECK_TRUE(BuildMenuText(nameless, &why).empty());
  CHECK_TRUE(Contains(why, "empty name"));

  // A preview path that exists is written, which is what would show the retail preview UI.
  std::vector<Offer> with_preview(1);
  with_preview[0].name = "Army Dreamers";
  with_preview[0].preview_art = "dlc_store/xbox/eng/offers/7/art";
  why.clear();
  const std::string preview = BuildMenuText(with_preview, &why);
  CHECK_TRUE(Contains(preview, "(preview_art \"dlc_store/xbox/eng/offers/7/art\")"));

  // The measurement switch: off is the shipped `dummy_upsell_offer`'s string spelling, which the
  // provider never matches (every row draws blank).
  rb_blitz::store::SetOfferTypeSymbol(false);
  why.clear();
  const std::string string_form = BuildMenuText(offers, &why);
  CHECK_TRUE(Contains(string_form, "(type \"song\")"));
  CHECK_FALSE(Contains(string_form, "(type song)"));
  rb_blitz::store::SetOfferTypeSymbol(true);
}

void TestTextQuoting() {
  BeginCase("a quote in a title cannot end the record");
  std::vector<Offer> offers(1);
  offers[0].name = "Say \"Hello\"";
  offers[0].artist = "Back\\Slash";
  std::string why;
  const std::string text = BuildMenuText(offers, &why);
  CHECK_TRUE(Contains(text, "name \"Say \\\"Hello\\\"\""));
  CHECK_TRUE(Contains(text, "artist \"Back\\\\Slash\""));
}

// The Downloaded mark (plan §3.2): the serving policy asks the installed predicate once per offer
// and writes the record key the retail scripts read as `{offer is_purchased}`. A page never claims
// a song is downloaded without the predicate saying so - with none installed every offer is
// unmarked, which is what a boot before the index lands looks like.
void TestDownloadedMark() {
  BeginCase("the Downloaded mark comes from the installed predicate, never from the page");
  std::vector<Offer> offers(2);
  offers[0].name = "The People That We Love";
  offers[0].artist = "Bush";
  offers[1].name = "Army Dreamers";
  offers[1].artist = "Kate Bush";
  rb_blitz::store::SetOfferSource([offers] { return offers; });

  // No predicate: both offers are unmarked and the record carries no `is_purchased` at all.
  rb_blitz::store::SetDownloadedPredicate({});
  CHECK_FALSE(rb_blitz::store::IsDownloadedOffer(offers[0]));
  std::string why;
  const std::string unmarked = BuildMenuText(rb_blitz::store::OffersForPage(), &why);
  CHECK_FALSE(Contains(unmarked, "is_purchased"));

  // The predicate marks exactly one of the two.
  rb_blitz::store::SetDownloadedPredicate(
      [](const Offer& offer) { return offer.name == "Army Dreamers"; });
  CHECK_FALSE(rb_blitz::store::IsDownloadedOffer(offers[0]));
  CHECK_TRUE(rb_blitz::store::IsDownloadedOffer(offers[1]));
  const std::string marked = BuildMenuText(rb_blitz::store::OffersForPage(), &why);
  CHECK_TRUE(Contains(marked, "(is_purchased TRUE)"));
  // One occurrence only: the marked offer's. The other offer's body must stay unmarked (a page
  // that marks everything would make every song look installed).
  const size_t first = marked.find("(is_purchased TRUE)");
  CHECK_TRUE(first != std::string::npos);
  CHECK_TRUE(marked.find("(is_purchased TRUE)", first + 1) == std::string::npos);
  // Every offer still carries the free-path signal: `is_available` is absent, so `purchase_allowed`
  // stays false and neither a price nor a Buy Now button is built.
  CHECK_FALSE(Contains(marked, "is_available"));

  // The source and predicate are per boot; leave nothing behind for a later case.
  rb_blitz::store::SetDownloadedPredicate({});
  rb_blitz::store::SetOfferSource({});
}

void TestServedFile() {
  BeginCase("a serve directory answers by exact path and refuses escapes");
  const fs::path dir = fs::temp_directory_path() / "rbbz-store-serve-tests";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "dlc_store" / "europe", ec);
  {
    std::ofstream out(dir / "dlc_store" / "europe" / "dlc_top_xbox_eng.dta", std::ios::binary);
    out << "(submenus \"/eng/recent/\")\n";
  }
  std::string why;
  const std::string text =
      ReadServedRecord(dir.string(), "dlc_store/europe/dlc_top_xbox_eng.dta", &why);
  CHECK_STR_EQ(text, "(submenus \"/eng/recent/\")\n");

  why.clear();
  CHECK_TRUE(ReadServedRecord(dir.string(), "dlc_store/europe/missing.dta", &why).empty());
  CHECK_TRUE(Contains(why, "no such file"));

  why.clear();
  CHECK_TRUE(ReadServedRecord(dir.string(), "../escape.dta", &why).empty());
  CHECK_TRUE(Contains(why, "plain relative"));

  why.clear();
  CHECK_TRUE(ReadServedRecord(dir.string(), "dlc_store/../../escape.dta", &why).empty());
  CHECK_TRUE(Contains(why, "plain relative"));

  why.clear();
  CHECK_TRUE(ReadServedRecord("", "dlc_store/europe/dlc_top_xbox_eng.dta", &why).empty());
  CHECK_TRUE(Contains(why, "no serve directory"));

  fs::remove_all(dir, ec);
}

void TestRecordChoice() {
  BeginCase("the served file wins over the built-in record");
  const std::string front = FrontMenuRecordPath();
  std::string why;
  const std::string builtin = BuiltinRecord(front, &why);
  CHECK_TRUE(why.empty());
  CHECK_TRUE(Contains(builtin, "\"/eng/recent/\""));

  // A path the build has no record for is reported, not guessed: the retail fetch then runs
  // unchanged.
  why.clear();
  CHECK_TRUE(BuiltinRecord("dlc_store/europe/offers/abc/", &why).empty());
  CHECK_TRUE(Contains(why, "no built-in record"));

  const rb_blitz::store::ServedRecord from_builtin = RecordForRequest(front, "");
  CHECK_TRUE(Contains(from_builtin.bytes, "\"/eng/search/\""));
  CHECK_STR_EQ(from_builtin.source, "the built-in front menu");

  // A menu row's own request answers an offers page. It is empty until the catalogue is wired,
  // which is a shape the retail DTA handles (zero offers -> refresh, not the browser).
  const rb_blitz::store::ServedRecord page =
      RecordForRequest("dlc_store/europe/eng/recent/", "");
  CHECK_TRUE(Contains(page.bytes, "offers_populated TRUE"));
  CHECK_FALSE(Contains(page.bytes, "(name"));
  CHECK_STR_EQ(page.source, "the built-in front menu");

  const rb_blitz::store::ServedRecord nothing =
      RecordForRequest("dlc_store/europe/offers/abc/", "");
  CHECK_TRUE(nothing.bytes.empty());
  CHECK_TRUE(Contains(nothing.source, "nothing"));
}

void TestMenuPaths() {
  BeginCase("a front-menu row's request is the only menu shape");
  CHECK_TRUE(IsMenuRecordPath("dlc_store/europe/eng/recent/"));
  CHECK_TRUE(IsMenuRecordPath("dlc_store/europe/eng/search/"));
  CHECK_FALSE(IsMenuRecordPath("dlc_store/europe/dlc_top_xbox_eng.dta"));
  CHECK_FALSE(IsMenuRecordPath("dlc_store/europe/offers/abc/"));
  CHECK_FALSE(IsMenuRecordPath("dlc_store/europe/eng/related/abc.dta"));
  CHECK_FALSE(IsMenuRecordPath("dlc_store/europe/eng/recent"));
  CHECK_FALSE(IsMenuRecordPath(""));
}

}  // namespace

int main() {
  TestRequestPaths();
  TestFrontMenuText();
  TestFrontMenuRefusals();
  TestMenuText();
  TestTextQuoting();
  TestDownloadedMark();
  TestServedFile();
  TestRecordChoice();
  TestMenuPaths();
  return rb_blitz::test::Finish();
}
