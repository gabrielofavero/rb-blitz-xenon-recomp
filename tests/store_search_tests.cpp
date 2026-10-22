// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4 of the RhythmVerse store plan: the search session walked without a boot, a network or a
// package. The cases pin exactly the promises the plan's exit is written against:
//
//   * the field is UTF-8 and bounded - a code point is one backspace, 64 is the ceiling, and a
//     broken byte is refused rather than stored;
//   * the guest never carries the query: one key token per press, a hex spelling for a caller that
//     can build bytes, and `cancel` invalidates in-flight work;
//   * the API's 3-character rule is enforced before a request exists, and the two strings the
//     player meets (`Search by artist or song title, one at a time.`, `Try just the artist name or
//     just the song title.`) are the ones the plan asks for;
//   * a stale answer (a newer submit, a cancel, a settings commit, the master switched off) is
//     dropped, never overwriting the current answer;
//   * Search and Recent apply the *same* eligibility and instrument predicates (`DecideOffer`,
//     `PassesInstrumentFilter`), which is the plan's "identical predicates" rule.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "store/rhythmverse_client.h"
#include "store/store_search.h"
#include "store/store_settings.h"

#ifdef _WIN32
#include "store/store_http.h"
#endif

namespace test = rb_blitz::test;
using namespace rb_blitz::store;

namespace {

void CheckStr(const char* file, int line, std::string_view actual, std::string_view expected) {
  ++test::g_checks;
  if (actual == expected) {
    return;
  }
  test::Fail(file, line, "expected \"" + std::string(expected) + "\"\n         actual   \"" +
                             std::string(actual) + "\"");
}

#define CHECK_STR(actual, expected) CheckStr(__FILE__, __LINE__, (actual), (expected))

void CheckEnum(const char* file, int line, int actual, int expected, const char* what) {
  ++test::g_checks;
  if (actual == expected) {
    return;
  }
  test::Fail(file, line, std::string(what) + ": " + std::to_string(actual) + " != " +
                             std::to_string(expected));
}

#define CHECK_ENUM(actual, expected) \
  CheckEnum(__FILE__, __LINE__, static_cast<int>(actual), static_cast<int>(expected), #actual)

// The session is process-wide (the guest has one field), so every case starts and ends from a
// known state: no text, no answer, and the master switch where the case wants it.
void ResetSession(bool enabled) {
  SearchCancel();
  StoreSettings settings;
  settings.enabled = enabled;
  PublishSettings(settings);
}

SongFile Song(std::string artist, std::string title, std::string url) {
  SongFile song;
  song.file_id = "id-" + title;
  song.file_name = artist + " - " + title + "_rb3con";
  song.gameformat = "rb3xbox";
  song.artist = std::move(artist);
  song.title = std::move(title);
  song.download_url = std::move(url);
  return song;
}

void SetChart(SongFile* song, Instrument instrument, int tier) {
  song->difficulty[static_cast<std::size_t>(instrument)] = tier;
}

// The default filter requires guitar, bass, drums and vocals (On = required, plan §1.6), so a
// fixture that should pass it must carry all four charts. `Charted` is that song.
SongFile Charted(SongFile song) {
  SetChart(&song, Instrument::kGuitar, 2);
  SetChart(&song, Instrument::kBass, 2);
  SetChart(&song, Instrument::kDrums, 2);
  SetChart(&song, Instrument::kVocals, 2);
  return song;
}

ListingPage Page(std::vector<SongFile> songs) {
  ListingPage page;
  page.page = 1;
  page.records_per_page = 25;
  page.returned = static_cast<int>(songs.size());
  page.total_filtered = static_cast<int>(songs.size());
  page.songs = std::move(songs);
  return page;
}

// P7's chunking cases need pages that are **not** the last one: the server's cursor is what says
// so (`start + returned < total_filtered`), not the number of offers a caller kept.
ListingPage Paged(int number, int start, int returned, int total, std::vector<SongFile> songs) {
  ListingPage page;
  page.page = number;
  page.records_per_page = 25;
  page.start = start;
  page.returned = returned;
  page.total_filtered = total;
  page.songs = std::move(songs);
  return page;
}

// A record that charts everything (so the default filter keeps it) but whose link is an archive
// name, which `DecideOffer` refuses - the cheapest way to have a page of records with **zero**
// eligible offers.
SongFile Ineligible(std::string title) {
  return Charted(Song("Nobody", std::move(title), "/download_file/u/i/archive.zip"));
}

// --- The field -----------------------------------------------------------------------------------

void TestCodePoints() {
  test::BeginCase("the field counts code points, so one backspace is one character");
  CHECK_EQ(CodePointCount("halsey"), 6u);
  CHECK_EQ(CodePointCount(std::string_view("caf\xC3\xA9")), 4u);          // café
  CHECK_EQ(CodePointCount(std::string_view("\xE6\x9D\xB1\xE4\xBA\xAC")), 2u);  // 東京
  CHECK_EQ(CodePointCount(std::string_view("a\xFF")), 2u);  // invalid byte still bounded

  SearchText text;
  CHECK_TRUE(text.Set(std::string_view("caf\xC3\xA9")));
  CHECK_EQ(text.length(), 4u);
  CHECK_TRUE(text.Backspace());
  CHECK_TRUE(text.text() == "caf");
  CHECK_TRUE(text.Backspace());
  CHECK_TRUE(text.text() == "ca");
  CHECK_TRUE(text.Backspace());
  CHECK_TRUE(text.Backspace());
  CHECK_FALSE(text.Backspace());
  CHECK_TRUE(text.text().empty());
}

void TestKeyTokens() {
  test::BeginCase("key tokens build the query, folding case and joining words");
  SearchText text;
  CHECK_TRUE(text.AppendKeyToken("H") == SearchText::Edit::kOk);
  CHECK_TRUE(text.AppendKeyToken("a") == SearchText::Edit::kOk);
  CHECK_TRUE(text.AppendKeyToken(kKeySpace) == SearchText::Edit::kOk);
  CHECK_TRUE(text.AppendKeyToken("2") == SearchText::Edit::kOk);
  CHECK_TRUE(text.AppendKeyToken(kKeyDash) == SearchText::Edit::kOk);
  CHECK_TRUE(text.AppendKeyToken("4") == SearchText::Edit::kOk);
  CHECK_STR(text.text(), "ha 2-4");
  CHECK_TRUE(text.AppendKeyToken(kKeyApostrophe) == SearchText::Edit::kOk);
  CHECK_TRUE(text.AppendKeyToken(kKeyPeriod) == SearchText::Edit::kOk);
  CHECK_STR(text.text(), "ha 2-4'.");
  // The action keys are not characters, and an unknown spelling is refused rather than guessed.
  CHECK_TRUE(text.AppendKeyToken(kKeyBackspace) == SearchText::Edit::kRejected);
  CHECK_TRUE(text.AppendKeyToken(kKeyClear) == SearchText::Edit::kRejected);
  CHECK_TRUE(text.AppendKeyToken("nonsense") == SearchText::Edit::kRejected);
  CHECK_TRUE(text.AppendKeyToken("") == SearchText::Edit::kRejected);

  // The ceiling is the API field's own: 64 code points, refused rather than truncated.
  SearchText full;
  for (int i = 0; i < kMaxSearchChars; ++i) {
    CHECK_TRUE(full.AppendKeyToken("a") == SearchText::Edit::kOk);
  }
  CHECK_TRUE(full.AppendKeyToken("a") == SearchText::Edit::kFull);
  CHECK_EQ(full.length(), static_cast<std::size_t>(kMaxSearchChars));
}

void TestSetText() {
  test::BeginCase("the whole-string transport trims, keeps accents and slashes, refuses control bytes");
  SearchText text;
  CHECK_TRUE(text.Set("  guns n' roses "));
  CHECK_STR(text.text(), "guns n' roses");
  CHECK_TRUE(text.Set("AC/DC"));
  CHECK_STR(text.text(), "AC/DC");
  CHECK_TRUE(text.Set(std::string_view("mot\xC3\xB6rhead")));  // motörhead
  CHECK_TRUE(text.length() == 9u);
  CHECK_FALSE(text.Set("bad\nquery"));
  CHECK_FALSE(text.Set("bad\tquery"));
  CHECK_FALSE(text.Set(std::string_view("bad\xFF\xFE")));
  CHECK_FALSE(text.Set(std::string(kMaxSearchChars + 1, 'a')));
  CHECK_TRUE(text.Set(""));  // clearing is a valid edit
  CHECK_TRUE(text.text().empty());
}

// --- The grammar ---------------------------------------------------------------------------------

// Measured 2026-10-22 in the acceptance boot (`out/store-acceptance/p8-store/Store.log`): the seed
// filled the field through the typing path, which also *opens* the surface - so `Search: bush` was
// drawn over the store's own screens (the front menu and the browser) until a key page was entered
// and left, and P7's credit line was never reachable. The seed is a field edit, not an entry: the
// keys still show the seeded text when the player opens them, and the strip stays silent until
// then.
void TestSeedField() {
  test::BeginCase("the diagnostic seed fills the field without opening the search surface");
  ResetSession(true);
  CHECK_TRUE(SearchSeedField("bush"));
  CHECK_STR(SearchCurrent().text, "bush");
  // The strip belongs to the surface (`StatusTextLocked`): nothing is drawn while the keys are shut.
  CHECK_TRUE(SearchStatusText().empty());
  // The keys open it, and the seeded field is what they show.
  SearchOpen();
  CHECK_STR(SearchStatusText(), "Search: bush");
  SearchClose();
  CHECK_TRUE(SearchStatusText().empty());
  // The whole-string transport is typing, so it still opens the surface.
  CHECK_TRUE(SearchSetText("kate"));
  CHECK_STR(SearchStatusText(), "Search: kate");
  // A seed the field refuses changes nothing, and a refused seed does not open the surface either.
  SearchCancel();
  CHECK_FALSE(SearchSeedField("bad\nquery"));
  CHECK_FALSE(SearchSeedField(std::string(kMaxSearchChars + 1, 'a')));
  CHECK_TRUE(SearchCurrent().text.empty());
  CHECK_TRUE(SearchStatusText().empty());
  SearchCancel();
}

void TestParseWrites() {
  test::BeginCase("the write grammar accepts the key pages and leaves P5's commands alone");
  StoreOp op = StoreOp::kNone;
  std::string token;
  std::string text;
  std::string why;

  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/open", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_ENUM(op, StoreOp::kSearchOpen);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/done", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_ENUM(op, StoreOp::kSearchDone);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/cancel", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_ENUM(op, StoreOp::kSearchCancel);

  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/a", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_ENUM(op, StoreOp::kSearchKey);
  CHECK_STR(token, "a");
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/7", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/space", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_STR(token, "space");
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/backspace", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/clear", &op, &token, &text, &why) ==
             StoreParse::kAccepted);

  // Hex carries any UTF-8 - spaces, slashes, accents - inside the token alphabet.
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/text/41432f4443", &op, &token, &text, &why) ==
             StoreParse::kAccepted);
  CHECK_ENUM(op, StoreOp::kSearchText);
  CHECK_STR(text, "AC/DC");

  // Malformed search paths are refused (answered, never forwarded).
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/", &op, &token, &text, &why) ==
             StoreParse::kRefused);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/key/mystery", &op, &token, &text, &why) ==
             StoreParse::kRefused);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/text/abc", &op, &token, &text, &why) ==
             StoreParse::kRefused);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/text/zz", &op, &token, &text, &why) ==
             StoreParse::kRefused);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/text/", &op, &token, &text, &why) ==
             StoreParse::kRefused);
  CHECK_TRUE(ParseStoreWrite("rbbz-store/search/unknown", &op, &token, &text, &why) ==
             StoreParse::kRefused);

  // Not ours: P5's download commands share the namespace and another parser owns them, and a
  // settings path is a different channel entirely.
  CHECK_TRUE(ParseStoreWrite("rbbz-store/download/start/job1", &op, &token, &text, &why) ==
             StoreParse::kNotOurs);
  CHECK_TRUE(ParseStoreWrite("rbbz-settings/set/x/b:1", &op, &token, &text, &why) ==
             StoreParse::kNotOurs);
  CHECK_TRUE(ParseStoreWrite("", &op, &token, &text, &why) == StoreParse::kNotOurs);
}

void TestWriteHandlerOwnsOnlySearch() {
  test::BeginCase("the hook entry point handles search writes and declines the rest");
  ResetSession(false);
  std::string why;
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/open", &why));
  CHECK_TRUE(why.empty());
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/h", &why));
  CHECK_TRUE(SearchCurrent().text == "h");
  // A refusal is still "ours" - it is answered, not forwarded.
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/nope", &why));
  CHECK_TRUE(!why.empty());
  CHECK_FALSE(HandleStoreSearchWrite("rbbz-store/download/start/job1", &why));
  CHECK_FALSE(HandleStoreSearchWrite("rbbz-settings/set/a/b:1", &why));
  SearchCancel();
}

void TestDuplicatePressGuard() {
  test::BeginCase("one guest press delivered twice in the same frame appends one character");
  // Measured in a boot (2026-10-20): a single A press reached the host as two `search/key/a`
  // writes in the same millisecond. The channel treats an identical command inside its window as
  // the same press; a different key, or the same one after a cancel, is a new press.
  ResetSession(true);
  std::string why;
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/open", &why));
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/a", &why));
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/a", &why));
  CHECK_STR(SearchCurrent().text, "a");
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/b", &why));
  CHECK_STR(SearchCurrent().text, "ab");
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/backspace", &why));
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/backspace", &why));
  CHECK_STR(SearchCurrent().text, "a");
  SearchCancel();
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/key/a", &why));
  CHECK_STR(SearchCurrent().text, "a");
  SearchCancel();
}

// --- The session ---------------------------------------------------------------------------------

void TestSubmitAndAccept() {
  test::BeginCase("a submit builds the search request and only its own answer is accepted");
  ResetSession(true);

  std::vector<std::pair<std::uint64_t, RequestSpec>> posted;
  SetSubmitSink([&posted](std::uint64_t generation, const RequestSpec& spec) {
    posted.emplace_back(generation, spec);
  });

  SearchOpen();
  CHECK_ENUM(SearchCurrent().state, SearchState::kEditing);
  CHECK_STR(SearchStatusText(), kSearchGuidance);

  for (const char* key : {"h", "a", "l", "s", "e", "y"}) {
    CHECK_TRUE(SearchApplyKey(key));
  }
  CHECK_STR(SearchStatusText(), "Search: halsey");
  CHECK_TRUE(SearchCurrent().generation >= 1u);

  // The DTA's Done is one write; it posts the request the worker runs.
  CHECK_TRUE(HandleStoreSearchWrite("rbbz-store/search/done", nullptr));
  CHECK_EQ(posted.size(), 1u);
  CHECK_TRUE(posted[0].second.url.find(kSearchPath) != std::string::npos);
  CHECK_TRUE(posted[0].second.body.find("text=halsey") != std::string::npos);
  CHECK_ENUM(SearchCurrent().state, SearchState::kSearching);
  CHECK_TRUE(SearchStatusText().find("halsey") != std::string::npos);

  const std::uint64_t first = posted[0].first;

  // A second submit makes the first answer stale.
  SearchApplyKey("x");
  CHECK_ENUM(SearchCurrent().state, SearchState::kEditing);
  const SearchSubmit second = SearchSubmitQuery();
  CHECK_TRUE(second.started);
  CHECK_TRUE(second.generation != first);
  CHECK_FALSE(SearchAccept(first,
                           Page({Charted(Song("Halsey", "Colors", "/download_file/u/i/C_rb3con"))})));

  // The current generation is accepted, and the offer list is the filtered page.
  CHECK_TRUE(SearchAccept(
      second.generation,
      Page({Charted(Song("Halsey", "Colors", "/download_file/u/i/C_rb3con"))})));
  const SearchSnapshot answered = SearchCurrent();
  CHECK_ENUM(answered.state, SearchState::kResults);
  CHECK_EQ(answered.offers.size(), 1u);
  CHECK_TRUE(answered.offers[0].download_url.rfind("https://rhythmverse.co/download_file/", 0) == 0);
  CHECK_TRUE(SearchStatusText().find("1 match") != std::string::npos);
  SetSubmitSink(nullptr);
  SearchCancel();
}

void TestStaleAnswers() {
  test::BeginCase("a filter commit, a cancel and the master switch each drop a late answer");

  {
    ResetSession(true);
    const SearchSubmit submit = SearchSubmitQuery();
    CHECK_FALSE(submit.started);  // empty field: the API's own minimum refuses it
    CHECK_ENUM(SearchCurrent().state, SearchState::kTooShort);
  }
  {
    ResetSession(true);
    for (const char* key : {"a", "c", "d", "c"}) CHECK_TRUE(SearchApplyKey(key));
    const SearchSubmit submit = SearchSubmitQuery();
    CHECK_TRUE(submit.started);
    // A settings commit changes the query's meaning (the filter is part of it).
    StoreSettings changed = *Settings();
    changed.instruments.keys = true;
    PublishSettings(changed);
    CHECK_FALSE(SearchAccept(submit.generation,
                             Page({Charted(Song("AC/DC", "Thunderstruck", "/download_file/u/i/t_rb3con"))})));
    CHECK_ENUM(SearchCurrent().state, SearchState::kEditing);
    // ...and the generation moved on, so a duplicate is stale too.
    CHECK_FALSE(SearchAccept(submit.generation, Page({})));
  }
  {
    // Cancel invalidates in-flight work: the answer must not appear after the player left.
    ResetSession(true);
    for (const char* key : {"a", "c", "d", "c"}) CHECK_TRUE(SearchApplyKey(key));
    const SearchSubmit submit = SearchSubmitQuery();
    CHECK_TRUE(submit.started);
    SearchCancel();
    CHECK_ENUM(SearchCurrent().state, SearchState::kIdle);
    CHECK_FALSE(SearchAccept(submit.generation, Page({})));
    CHECK_TRUE(SearchStatusText().empty());
  }
  {
    // Master off mid-flight: the answer is dropped and the state says why.
    ResetSession(true);
    for (const char* key : {"a", "c", "d", "c"}) CHECK_TRUE(SearchApplyKey(key));
    const SearchSubmit submit = SearchSubmitQuery();
    StoreSettings off;
    off.enabled = false;
    PublishSettings(off);
    CHECK_FALSE(SearchAccept(submit.generation, Page({})));
    CHECK_ENUM(SearchCurrent().state, SearchState::kDisabled);
    CHECK_STR(SearchStatusText(), kSearchDisabled);
    // ...and no new request is built while it is off.
    const SearchSubmit refused = SearchSubmitQuery();
    CHECK_FALSE(refused.started);
    CHECK_STR(refused.why, kSearchDisabled);
  }
  SearchCancel();
}

void TestMessages() {
  test::BeginCase("the two authored sentences, the no-match hint and a failure reason");
  ResetSession(true);
  SearchOpen();
  CHECK_STR(SearchStatusText(), kSearchGuidance);
  CHECK_TRUE(std::string(kSearchGuidance).find("one at a time") != std::string::npos);

  for (const char* key : {"l", "o", "r", "n"}) CHECK_TRUE(SearchApplyKey(key));
  const SearchSubmit submit = SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  CHECK_TRUE(SearchAccept(submit.generation, Page({})));
  CHECK_ENUM(SearchCurrent().state, SearchState::kNoResults);
  CHECK_TRUE(SearchStatusText().find("No matches for \"lorn\"") != std::string::npos);
  CHECK_TRUE(SearchStatusText().find(kSearchRetryHint) != std::string::npos);

  // A failure names its own reason, so "no matches" can never be mistaken for a network fault.
  const SearchSubmit again = SearchSubmitQuery();
  CHECK_TRUE(again.started);
  CHECK_TRUE(SearchFail(again.generation, "the catalogue answered HTTP 503"));
  CHECK_TRUE(SearchStatusText().find("HTTP 503") != std::string::npos);

  // Typing after an answer returns to the field; the answer is no longer current.
  CHECK_TRUE(SearchApplyKey("s"));
  CHECK_ENUM(SearchCurrent().state, SearchState::kEditing);
  CHECK_TRUE(SearchCurrent().offers.empty());
  SearchCancel();
}

void TestNoMatchesBody() {
  test::BeginCase("the API's no-match answer (`songs: false`) is an empty result, not a failure");
  // Measured 2026-10-20 against the live API: a query with no matches answers HTTP 200 with
  // `{"records":{"total_filtered":0,"returned":0},"songs":false}` - a JSON boolean, not `[]`. The
  // plan forbids reporting that as a network failure, so the parse reads it as zero songs and the
  // session reaches the no-match state with the retry guidance.
  const char* body = R"({"status":"success","data":{"records":{"total_available":51196,
    "total_filtered":0,"returned":0},"pagination":{"start":0,"records":"25","page":"1"},
    "songs":false,"_debug_timing_ms":{"song_list":306.42,"total":0.2,"total_filtered":268.76}}})";
  std::string error;
  const std::optional<ListingPage> page = ParseListing(body, &error);
  CHECK_TRUE(page.has_value());
  if (page) {
    CHECK_TRUE(page->songs.empty());
    CHECK_EQ(page->returned, 0);
    CHECK_EQ(page->total_filtered, 0);
    CHECK_FALSE(page->HasMorePages());
  }
  // A non-array that is *not* a falsey marker is still refused - this is a widening, not a shrug.
  CHECK_FALSE(ParseListing(R"({"status":"success","data":{"songs":"nope"}})", &error).has_value());
  CHECK_FALSE(
      ParseListing(R"({"status":"success","data":{"records":{"returned":4},"songs":false}})", &error)
          .has_value());

  ResetSession(true);
  for (const char* key : {"z", "z", "z", "q"}) CHECK_TRUE(SearchApplyKey(key));
  const SearchSubmit submit = SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  CHECK_TRUE(SearchAccept(submit.generation, *ParseListing(body, &error)));
  CHECK_ENUM(SearchCurrent().state, SearchState::kNoResults);
  CHECK_TRUE(SearchStatusText().find("No matches for \"zzzq\"") != std::string::npos);
  CHECK_TRUE(SearchStatusText().find(kSearchRetryHint) != std::string::npos);
  SearchCancel();
}

void TestQueries() {
  test::BeginCase("the one-bit queries answer the key pages and nothing else");
  ResetSession(true);
  SearchOpen();
  bool answer = false;
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/open", &answer));
  CHECK_TRUE(answer);
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/ready", &answer));
  CHECK_FALSE(answer);
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/state/editing", &answer));
  CHECK_TRUE(answer);
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/state/searching", &answer));
  CHECK_FALSE(answer);

  for (const char* key : {"z", "z", "t", "o", "p"}) CHECK_TRUE(SearchApplyKey(key));
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/ready", &answer));
  CHECK_TRUE(answer);
  const SearchSubmit submit = SearchSubmitQuery();
  CHECK_TRUE(SearchAccept(submit.generation,
                          Page({Charted(Song("ZZ Top", "Sharp Dressed Man", "/download_file/u/i/s_rb3con")),
                                Charted(Song("ZZ Top", "Gimme All Your Lovin", "/download_file/u/i/g_rb3con"))})));
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/state/results", &answer));
  CHECK_TRUE(answer);
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/count/at-least/2", &answer));
  CHECK_TRUE(answer);
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/count/at-least/3", &answer));
  CHECK_FALSE(answer);
  CHECK_FALSE(AnswerSearchQuery("rbbz-store/search/count/at-least/x", &answer));
  CHECK_FALSE(AnswerSearchQuery("rbbz-store/search/nonsense", &answer));
  CHECK_FALSE(AnswerSearchQuery("rbbz-store/enabled", &answer));
  CHECK_FALSE(AnswerSearchQuery("rbbz-store/download/state/x/installed", &answer));

  SearchClose();
  CHECK_TRUE(AnswerSearchQuery("rbbz-store/search/open", &answer));
  CHECK_FALSE(answer);
  // Leaving is not a cancel: the answer is still there when the pages are re-entered.
  CHECK_ENUM(SearchCurrent().state, SearchState::kResults);
  SearchCancel();
}

// --- Predicates ----------------------------------------------------------------------------------

void TestShapeOffers() {
  test::BeginCase("Search and Recent apply the same eligibility and instrument predicates");
  SongFile a = Charted(Song("Alpha", "One", "/download_file/u/i/one_rb3con"));
  SetChart(&a, Instrument::kGuitar, 3);
  SongFile b = Charted(Song("Beta", "Two", "/download_file/u/i/two_rb3con"));
  SetChart(&b, Instrument::kKeys, 4);
  SongFile archive = Charted(Song("Gamma", "Three", "/download_file/u/i/three.zip"));
  SongFile offsite = Charted(Song("Delta", "Four", "http://example.com/four_rb3con"));
  SongFile format = Charted(Song("Epsilon", "Five", "/download_file/u/i/five_rb3con"));
  format.gameformat = "rv";

  const ListingPage page = Page({a, b, archive, offsite, format});

  // The default filter (guitar..vocals on, keys off) keeps the two guitar charts that are also
  // eligible downloads, in the API's order.
  const std::vector<SearchOffer> default_filtered = ShapeOffers(page, InstrumentFilter{});
  CHECK_EQ(default_filtered.size(), 2u);
  CHECK_STR(default_filtered[0].song.title, "One");
  CHECK_STR(default_filtered[1].song.title, "Two");

  // Keys on is a *requirement*: only the song that carries a keys chart survives.
  InstrumentFilter with_keys;
  with_keys.keys = true;
  const std::vector<SearchOffer> keys_only = ShapeOffers(page, with_keys);
  CHECK_EQ(keys_only.size(), 1u);
  CHECK_STR(keys_only[0].song.title, "Two");

  // All off means no instrument constraint at all - but eligibility still applies, so the
  // archive, the off-site link and the wrong format stay out.
  InstrumentFilter none;
  none.guitar = none.bass = none.drums = none.vocals = none.keys = false;
  CHECK_EQ(ShapeOffers(page, none).size(), 2u);
}

void TestPerFileDuplicates() {
  test::BeginCase("two files for the same song stay two offers, in the API's order");
  // OQ7 of the plan: an offer is a *file*, not an artist + title. Two charts of one song are two
  // catalogue entries with their own ids and links, and the store lists both - the Downloaded
  // matcher (artist + title) deliberately marks both, and Download Again stays an explicit press
  // per row. Nothing here is deduplicated, and the API's order is preserved.
  SongFile first = Charted(Song("Same Artist", "Same Title", "/download_file/u/i/aaa_rb3con"));
  first.file_id = "aaa";
  SongFile second = Charted(Song("Same Artist", "Same Title", "/download_file/u/i/bbb_rb3con"));
  second.file_id = "bbb";
  SongFile other = Charted(Song("Another", "Song", "/download_file/u/i/ccc_rb3con"));
  other.file_id = "ccc";

  const std::vector<SearchOffer> offers = ShapeOffers(Page({first, second, other}), InstrumentFilter{});
  CHECK_EQ(offers.size(), 3u);
  CHECK_STR(offers[0].song.file_id, "aaa");
  CHECK_STR(offers[1].song.file_id, "bbb");
  CHECK_STR(offers[2].song.file_id, "ccc");
  CHECK_STR(offers[0].song.title, "Same Title");
  CHECK_STR(offers[1].song.title, "Same Title");
  // Both links survived resolution, so a Download Again row's press has its own target.
  CHECK_TRUE(offers[0].download_url != offers[1].download_url);
}

void TestAnswerPresentation() {
  test::BeginCase("an answer waits for the Search row, and only the strip belongs to the keys");
  ResetSession(true);
  SearchOpen();
  bool answer = false;
  CHECK_TRUE(AnswerSearchQuery(kSearchShowQuery, &answer));
  CHECK_FALSE(answer);  // nothing to list yet: the row opens the keys

  for (const char* key : {"b", "u", "s", "h"}) CHECK_TRUE(SearchApplyKey(key));
  const SearchSubmit submit = SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  CHECK_TRUE(AnswerSearchQuery(kSearchSearchingQuery, &answer));
  CHECK_TRUE(answer);  // the submit is in flight
  CHECK_FALSE(SearchAnswerPending());

  CHECK_TRUE(SearchAccept(
      submit.generation,
      Page({Charted(Song("Bush", "The People That We Love", "/download_file/u/i/p_rb3con"))})));
  CHECK_TRUE(SearchAnswerPending());
  CHECK_TRUE(AnswerSearchQuery(kSearchShowQuery, &answer));
  CHECK_TRUE(answer);
  CHECK_EQ(SearchOffers().size(), 1u);
  CHECK_TRUE(SearchStatusText().find("1 match") != std::string::npos);
  CHECK_TRUE(SearchStatusText().find(kSearchListHintOne) != std::string::npos);

  // `leave` (every search page's `exit`, so Back ends it) closes the surface: the strip goes - it
  // must not follow the player out of the keys - while the field, the answer **and** the pending
  // mark stay. The mark is the Search row's gate: pressing it lists the answer.
  std::string why;
  CHECK_TRUE(HandleStoreSearchWrite(kSearchLeaveWrite, &why));
  CHECK_TRUE(why.empty());
  CHECK_TRUE(SearchStatusText().empty());
  CHECK_STR(SearchCurrent().text, "bush");
  CHECK_EQ(SearchOffers().size(), 1u);
  CHECK_TRUE(SearchAnswerPending());
  CHECK_TRUE(AnswerSearchQuery(kSearchShowQuery, &answer));
  CHECK_TRUE(answer);

  // Re-entering the keys shows the answer's sentence again; the serving policy handing the store
  // the page is what retires the mark, and the answer and the field still stay.
  SearchOpen();
  CHECK_TRUE(SearchStatusText().find("1 match") != std::string::npos);
  SearchMarkShown();
  CHECK_FALSE(SearchAnswerPending());
  CHECK_EQ(SearchOffers().size(), 1u);
  CHECK_TRUE(SearchStatusText().find("1 match") != std::string::npos);

  // An edit retires both the answer and its pending mark.
  SearchOpen();
  CHECK_TRUE(SearchApplyKey("s"));
  CHECK_FALSE(SearchAnswerPending());
  CHECK_TRUE(SearchOffers().empty());
  SearchCancel();

  // A no-match answer is presented the same way, and so is a failure: both are pages the row must
  // be able to open (an empty one returns the store to its menu), not silent states.
  ResetSession(true);
  SearchOpen();
  for (const char* key : {"z", "z", "z", "q"}) CHECK_TRUE(SearchApplyKey(key));
  const SearchSubmit empty = SearchSubmitQuery();
  CHECK_TRUE(empty.started);
  CHECK_TRUE(SearchAccept(empty.generation, Page({})));
  CHECK_TRUE(SearchAnswerPending());
  CHECK_TRUE(SearchStatusText().find(kSearchRetryHint) != std::string::npos);
  SearchMarkShown();
  const SearchSubmit failed = SearchSubmitQuery();
  CHECK_TRUE(failed.started);
  CHECK_TRUE(SearchFail(failed.generation, "the catalogue answered HTTP 503"));
  CHECK_TRUE(SearchAnswerPending());
  SearchCancel();
  CHECK_FALSE(SearchAnswerPending());
  CHECK_TRUE(SearchOffers().empty());
}

void TestCancelSemantics() {
  test::BeginCase("cancel clears the field, close keeps it, and open re-shows it");
  ResetSession(true);
  for (const char* key : {"q", "u", "e", "e", "n"}) CHECK_TRUE(SearchApplyKey(key));
  SearchClose();
  CHECK_ENUM(SearchCurrent().state, SearchState::kEditing);
  CHECK_STR(SearchCurrent().text, "queen");
  CHECK_TRUE(SearchStatusText().empty());  // closed: no strip over a page that is not the search
  SearchOpen();
  CHECK_STR(SearchStatusText(), "Search: queen");
  SearchCancel();
  CHECK_ENUM(SearchCurrent().state, SearchState::kIdle);
  CHECK_TRUE(SearchCurrent().text.empty());
  CHECK_TRUE(SearchStatusText().empty());
}

// --- P7: chunking, cancellation and backoff ----------------------------------------------------

void TestMultiPageAnswer() {
  test::BeginCase(
      "a search walks pages, keeps every page's eligible offers, and does not stop at an emptied "
      "one");
  ResetSession(true);
  SetSearchPageBudget(3);
  CHECK_TRUE(SearchSetText("bush"));
  const SearchSubmit submit = SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  CHECK_TRUE(submit.request.body.find("page=1") != std::string::npos);

  // Page 1: 25 records, none eligible (every link is an archive name), and the server says there
  // are more rows. The walk continues - which is the point: the cursor, not the visible count.
  std::vector<SongFile> junk;
  for (int i = 0; i < 25; ++i) {
    junk.push_back(Ineligible("junk" + std::to_string(i)));
  }
  const SearchPageAnswer first =
      SearchAcceptPage(submit.generation, Paged(1, 0, 25, 30, std::move(junk)));
  CHECK_TRUE(first.accepted);
  CHECK_EQ(first.next_page, 2);
  CHECK_EQ(first.matches, 0u);
  CHECK_ENUM(SearchCurrent().state, SearchState::kSearching);  // in flight: the strip counts pages
  CHECK_TRUE(SearchCurrent().offers.empty());
  CHECK_TRUE(SearchStatusText().find("1 page") != std::string::npos);

  // The worker's next request is the same query, one page on.
  std::string why;
  const std::optional<RequestSpec> second =
      SearchBuildPageRequest(submit.generation, first.next_page, &why);
  CHECK_TRUE(second.has_value());
  if (second) {
    CHECK_TRUE(second->url.find(kSearchPath) != std::string::npos);
    CHECK_TRUE(second->body.find("page=2") != std::string::npos);
    CHECK_TRUE(second->body.find("text=bush") != std::string::npos);
  }

  // Page 2 is the last one and carries the only eligible offer.
  const SearchPageAnswer last = SearchAcceptPage(
      submit.generation,
      Paged(2, 25, 5, 30,
            {Charted(Song("Bush", "The People That We Love", "/download_file/u/i/p_rb3con"))}));
  CHECK_TRUE(last.accepted);
  CHECK_EQ(last.next_page, 0);
  CHECK_EQ(last.matches, 1u);
  CHECK_ENUM(SearchCurrent().state, SearchState::kResults);
  CHECK_EQ(SearchOffers().size(), 1u);
  CHECK_TRUE(SearchAnswerPending());
  // The count is the eligible one: the server's `total_filtered` (30) never reaches the strip.
  CHECK_TRUE(SearchStatusText().find("1 match") != std::string::npos);
  CHECK_TRUE(SearchStatusText().find("30") == std::string::npos);
  SetSearchPageBudget(1);
  SearchCancel();
}

void TestPageBudgetAndCancellation() {
  test::BeginCase("the per-action budget bounds the walk, and a cancel or a commit ends it");
  {
    // A budget of 1 is the pre-P7 shape: page 1 is the whole answer.
    ResetSession(true);
    SetSearchPageBudget(1);
    CHECK_TRUE(SearchSetText("bush"));
    const SearchSubmit submit = SearchSubmitQuery();
    const SearchPageAnswer answer =
        SearchAcceptPage(submit.generation, Paged(1, 0, 25, 60, {Ineligible("x")}));
    CHECK_TRUE(answer.accepted);
    CHECK_EQ(answer.next_page, 0);
    CHECK_ENUM(SearchCurrent().state, SearchState::kNoResults);
  }
  {
    // A cancel ends the walk even with budget left.
    ResetSession(true);
    SetSearchPageBudget(4);
    CHECK_TRUE(SearchSetText("bush"));
    const SearchSubmit submit = SearchSubmitQuery();
    const SearchPageAnswer answer =
        SearchAcceptPage(submit.generation, Paged(1, 0, 25, 60, {Ineligible("x")}));
    CHECK_EQ(answer.next_page, 2);
    CHECK_TRUE(SearchPageWanted(submit.generation));
    SearchCancel();
    CHECK_FALSE(SearchPageWanted(submit.generation));
    std::string why;
    CHECK_FALSE(SearchBuildPageRequest(submit.generation, 2, &why).has_value());
  }
  {
    // ...and so does a settings commit, because the filter is part of the query.
    ResetSession(true);
    SetSearchPageBudget(4);
    CHECK_TRUE(SearchSetText("bush"));
    const SearchSubmit submit = SearchSubmitQuery();
    CHECK_TRUE(
        SearchAcceptPage(submit.generation, Paged(1, 0, 25, 60, {Ineligible("x")})).accepted);
    StoreSettings changed = *Settings();
    changed.instruments.keys = true;
    PublishSettings(changed);
    CHECK_FALSE(SearchPageWanted(submit.generation));
    std::string why;
    CHECK_FALSE(SearchBuildPageRequest(submit.generation, 2, &why).has_value());
    CHECK_TRUE(why.find("settings changed") != std::string::npos);
  }
  SetSearchPageBudget(1);
  SearchCancel();
}

void TestPartialFailure() {
  test::BeginCase("a page that fails after another landed keeps the answer and still backs off");
  ResetSession(true);
  SetSearchPageBudget(4);
  CHECK_TRUE(SearchSetText("bush"));
  const SearchSubmit submit = SearchSubmitQuery();
  CHECK_TRUE(SearchAcceptPage(
                 submit.generation,
                 Paged(1, 0, 25, 60,
                       {Charted(Song("Bush", "Mouth", "/download_file/u/u/m_rb3con"))}))
                 .accepted);
  CHECK_TRUE(SearchFailAfter(submit.generation, "the catalogue answered HTTP 502",
                             std::chrono::seconds(20)));
  CHECK_ENUM(SearchCurrent().state, SearchState::kResults);
  CHECK_EQ(SearchOffers().size(), 1u);
  CHECK_TRUE(SearchAnswerPending());
  // The wait still applies: pressing Done again straight away is refused with it, and the answer
  // that is already on screen is not replaced by a failure sentence.
  const SearchSubmit refused = SearchSubmitQuery();
  CHECK_FALSE(refused.started);
  CHECK_TRUE(refused.why.find("try again in") != std::string::npos);
  CHECK_ENUM(SearchCurrent().state, SearchState::kResults);
  SearchCancel();
  SetSearchPageBudget(1);
}

void TestBackoffWindow() {
  test::BeginCase("a failed answer's wait refuses the next submit, and a cancel clears it");
  ResetSession(true);
  CHECK_TRUE(SearchSetText("bush"));
  const SearchSubmit first = SearchSubmitQuery();
  CHECK_TRUE(first.started);
  CHECK_TRUE(SearchFailAfter(first.generation, "the catalogue answered HTTP 503",
                             std::chrono::seconds(30)));
  CHECK_ENUM(SearchCurrent().state, SearchState::kFailed);
  CHECK_TRUE(SearchStatusText().find("HTTP 503") != std::string::npos);
  const SearchSubmit refused = SearchSubmitQuery();
  CHECK_FALSE(refused.started);
  CHECK_TRUE(refused.why.find("try again in") != std::string::npos);
  CHECK_TRUE(SearchStatusText().find("try again in") != std::string::npos);

  // Cancel is the player's own way out of the wait (it clears the field too, so it is a fresh
  // start rather than a retry).
  SearchCancel();
  CHECK_TRUE(SearchSetText("bush"));
  const SearchSubmit again = SearchSubmitQuery();
  CHECK_TRUE(again.started);
  // A wait of zero - a failure that named none - lets the next submit through at once.
  CHECK_TRUE(
      SearchFailAfter(again.generation, "the answer could not be read", std::chrono::seconds(0)));
  CHECK_TRUE(SearchSubmitQuery().started);
  SearchCancel();
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Manual only: `rb_blitz_store_search_tests --live-search bush` sends ONE real search through
  // store_http and prints its answer. ctest never passes it - the suite above is hermetic, and
  // this arm exists so the request body and the parse/filter can be checked against the live site
  // without a boot (the same shape as store_client's --live-list).
  if (argc >= 3 && std::strcmp(argv[1], "--live-search") == 0) {
    ResetSession(true);
    if (!SearchSetText(argv[2])) {
      std::printf("live-search: the text was refused\n");
      return 2;
    }
    const SearchSubmit submit = SearchSubmitQuery();
    if (!submit.started) {
      std::printf("live-search: no request (%s)\n", submit.why.c_str());
      return 2;
    }
    std::printf("live-search: POST %s\n  body: %s\n", submit.request.url.c_str(),
                submit.request.body.c_str());
    const HttpResponse response =
        PostForm(submit.request.url, submit.request.body, submit.request.headers);
    std::printf("  HTTP %d, %zu byte(s), ok=%d%s%s\n", response.status, response.body.size(),
                response.ok ? 1 : 0, response.error.empty() ? "" : ", error: ",
                response.error.c_str());
    if (!response.ok || response.status != 200) {
      return 1;
    }
    std::string error;
    const std::optional<ListingPage> page = ParseListing(response.body, &error);
    if (!page) {
      std::printf("  parse failed: %s\n", error.c_str());
      return 1;
    }
    const std::vector<SearchOffer> offers = ShapeOffers(*page, Settings()->instruments);
    std::printf("  %d record(s) returned, %zu eligible offer(s)\n", page->returned,
                offers.size());
    for (const SearchOffer& offer : offers) {
      std::printf("    %s - %s  %s\n", offer.song.artist.c_str(), offer.song.title.c_str(),
                  offer.download_url.c_str());
    }
    return offers.empty() ? 1 : 0;
  }
#endif
  TestCodePoints();
  TestKeyTokens();
  TestSetText();
  TestSeedField();
  TestParseWrites();
  TestWriteHandlerOwnsOnlySearch();
  TestDuplicatePressGuard();
  TestSubmitAndAccept();
  TestStaleAnswers();
  TestMessages();
  TestNoMatchesBody();
  TestQueries();
  TestShapeOffers();
  TestPerFileDuplicates();
  TestAnswerPresentation();
  TestCancelSemantics();
  TestMultiPageAnswer();
  TestPageBudgetAndCancellation();
  TestPartialFailure();
  TestBackoffWindow();

  std::printf("\n%d check(s), %d failure(s)\n", test::g_checks, test::g_failures);
  return test::g_failures == 0 ? 0 : 1;
}
