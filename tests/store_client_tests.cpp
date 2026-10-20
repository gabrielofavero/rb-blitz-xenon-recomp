// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P0 of the RhythmVerse store plan: the client's contracts, walked without a boot, a network or a
// package. The fixture-backed cases read the committed captures in `docs/rythmverse-responses/`
// (the reviewer's 2026-10-14 captures) and assert the measurements the plan records:
//
//   * §3    - one page of 25 records, 17 `rb3xbox`, pagination fields arriving as strings;
//   * §3.1  - direct-link/off-site/archive/length policy;
//   * §3.2  - the artist/title matcher's folding rules;
//   * §3.3  - the API returns no preview audio (every preview URL is a YouTube web page).
//
// The synthetic cases pin the shapes a fixture cannot cover (an error status, a withdrawn record, a
// Dropbox link, alternating number/string pagination), so schema drift fails here rather than in a
// boot.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "store/rhythmverse_client.h"

#ifdef _WIN32
#include "store/store_http.h"
#endif

namespace test = rb_blitz::test;
namespace fs = std::filesystem;

using namespace rb_blitz::store;

namespace {

constexpr const char* kDefaultProjectDir = ".";

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

std::optional<std::string> ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return text;
}

SongFile Song(std::string artist, std::string title) {
  SongFile song;
  song.file_id = "id";
  song.file_name = artist + " - " + title + "_rb3con";
  song.gameformat = "rb3xbox";
  song.artist = std::move(artist);
  song.title = std::move(title);
  song.download_url = "/download_file/user/id/" + song.file_name;
  return song;
}

void SetChart(SongFile* song, Instrument instrument, int tier) {
  song->difficulty[static_cast<std::size_t>(instrument)] = tier;
}

// --- Requests -------------------------------------------------------------------------------------

void TestListRequest() {
  test::BeginCase("request: the list body is the measured shape (instruments, sort, page)");

  Query query;
  QueryProblem problem = QueryProblem::kBadPage;
  const std::optional<RequestSpec> request = BuildListRequest(query, &problem);
  CHECK_TRUE(request.has_value());
  CHECK_ENUM(problem, QueryProblem::kNone);
  CHECK_STR(request->url, "https://rhythmverse.co/api/rb3xbox/songfiles/list");
  CHECK_STR(request->body,
            "instrument%5B%5D=guitar&instrument%5B%5D=bass&instrument%5B%5D=drums"
            "&instrument%5B%5D=vocals"
            "&sort%5B0%5D%5Bsort_by%5D=update_date&sort%5B0%5D%5Bsort_order%5D=DESC"
            "&data_type=full&page=1&records=25");
  CHECK_EQ(request->headers.size(), 3);
  CHECK_STR(request->headers[0].first, "Content-Type");
  CHECK_STR(request->headers[0].second, "application/x-www-form-urlencoded");
  CHECK_STR(request->headers[2].first, "X-Requested-With");

  // Keys is a product filter, so enabling it is a body change of exactly one field.
  query.instruments.keys = true;
  const std::string with_keys = BuildFormBody(query, false);
  CHECK_TRUE(with_keys.find("instrument%5B%5D=keys") != std::string::npos);

  // All off = no instrument constraint: no instrument field at all (§1.6/OQ10).
  query.instruments = InstrumentFilter{false, false, false, false, false};
  const std::string none = BuildFormBody(query, false);
  CHECK_TRUE(none.starts_with("sort%5B0%5D"));
  CHECK_TRUE(none.find("instrument") == std::string::npos);

  query.instruments = InstrumentFilter{};
  query.page = 2;
  CHECK_TRUE(BuildFormBody(query, false).find("page=2") != std::string::npos);
}

void TestSearchRequest() {
  test::BeginCase("request: search trims one query, encodes it, and enforces 3..64 code points");

  Query query;
  query.text = "  Boy With Luv  ";
  QueryProblem problem = QueryProblem::kNone;
  const std::optional<RequestSpec> request = BuildSearchRequest(query, &problem);
  CHECK_TRUE(request.has_value());
  CHECK_ENUM(problem, QueryProblem::kNone);
  CHECK_STR(request->url, "https://rhythmverse.co/api/rb3xbox/songfiles/search/live");
  CHECK_TRUE(request->body.find("text=Boy%20With%20Luv") != std::string::npos);
  // The API's minimum is characters, not bytes: "aee" is 3, "aéé" is also 3.
  query.text = "a\xC3\xA9\xC3\xA9";
  CHECK_TRUE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kNone);

  query.text = "ab";
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kTextTooShort);

  query.text = "   ";
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kTextTooShort);

  query.text = std::string(65, 'x');
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kTextTooLong);

  query.text = "bad\ntext";
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kTextHasControlCharacters);

  query.text = "halsey";
  query.page = 0;
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kBadPage);

  query.page = 1;
  query.records = 0;
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kBadPageSize);

  query.records = kMaxPageSize + 1;
  CHECK_FALSE(BuildSearchRequest(query, &problem).has_value());
  CHECK_ENUM(problem, QueryProblem::kBadPageSize);

  query.records = kDefaultPageSize;
  query.page = 1;
  // The list route ignores `text`; only search sends it.
  query.text = "halsey";
  const std::optional<RequestSpec> list = BuildListRequest(query, &problem);
  CHECK_TRUE(list.has_value());
  CHECK_TRUE(list->body.find("text=") == std::string::npos);
}

// --- Parsing --------------------------------------------------------------------------------------

void TestListFixture(const fs::path& project_dir) {
  test::BeginCase("parse: the committed list capture - 25 records, 17 rb3xbox, string pagination");

  const std::optional<std::string> json = ReadFile(project_dir / "docs" / "rythmverse-responses" / "list.json");
  CHECK_TRUE(json.has_value());
  if (!json) {
    return;
  }
  std::string error;
  const std::optional<ListingPage> page = ParseListing(*json, &error);
  CHECK_TRUE(page.has_value());
  if (!page) {
    std::printf("         %s\n", error.c_str());
    return;
  }

  CHECK_EQ(page->page, 1);              // delivered as the string "1"
  CHECK_EQ(page->records_per_page, 25);  // delivered as the string "25"
  CHECK_EQ(page->start, 0);
  CHECK_EQ(page->returned, 25);
  CHECK_EQ(page->songs.size(), 25);
  CHECK_TRUE(page->HasMorePages());

  int rb3xbox = 0;
  for (const SongFile& song : page->songs) {
    if (song.gameformat == "rb3xbox") {
      ++rb3xbox;
    }
  }
  CHECK_EQ(rb3xbox, 17);

  const SongFile* first = nullptr;
  for (const SongFile& song : page->songs) {
    if (song.gameformat == "rb3xbox") {
      first = &song;
      break;
    }
  }
  CHECK_TRUE(first != nullptr);
  if (first == nullptr) {
    return;
  }
  CHECK_STR(first->file_id, "38a4dcc61d324e7883c9a393a7086bb9");
  CHECK_STR(first->artist, "Youth Group");
  CHECK_STR(first->title, "Forever Young");
  CHECK_STR(first->album, "Casino Twilight Dogs");
  CHECK_STR(first->genre, "Alternative");
  CHECK_EQ(first->year.value_or(0), 2006);
  CHECK_EQ(first->song_length_seconds.value_or(0), 276);
  CHECK_EQ(first->size_bytes.value_or(0), 5591040);
  CHECK_EQ(first->difficulty[static_cast<std::size_t>(Instrument::kGuitar)].value_or(0), 4);
  CHECK_EQ(first->rank[static_cast<std::size_t>(Instrument::kGuitar)].value_or(0), 221);
  CHECK_TRUE(first->HasChart(Instrument::kGuitar));
  CHECK_TRUE(first->HasChart(Instrument::kVocals));
  // The keys field exists only in the `data` copy and 0 is its "not authored" spelling.
  CHECK_FALSE(first->HasChart(Instrument::kKeys));

  // A song with keys: Promises carries diff_keys = 6 in `data` and nothing in `file`.
  const SongFile* promises = nullptr;
  for (const SongFile& song : page->songs) {
    if (song.title == "Promises") {
      promises = &song;
    }
  }
  CHECK_TRUE(promises != nullptr);
  if (promises != nullptr) {
    CHECK_EQ(promises->difficulty[static_cast<std::size_t>(Instrument::kKeys)].value_or(0), 6);
    CHECK_TRUE(promises->HasChart(Instrument::kKeys));
  }
}

void TestSearchFixture(const fs::path& project_dir) {
  test::BeginCase("parse: the committed search capture - 8 records, 6 rb3xbox, preview pages");

  const std::optional<std::string> json =
      ReadFile(project_dir / "docs" / "rythmverse-responses" / "search.json");
  CHECK_TRUE(json.has_value());
  if (!json) {
    return;
  }
  std::string error;
  const std::optional<ListingPage> page = ParseListing(*json, &error);
  CHECK_TRUE(page.has_value());
  if (!page) {
    return;
  }
  CHECK_EQ(page->songs.size(), 8);
  CHECK_EQ(page->returned, 8);
  CHECK_FALSE(page->HasMorePages());  // start 0 + returned 8 == total_filtered 8

  int rb3xbox = 0;
  for (const SongFile& song : page->songs) {
    if (song.gameformat == "rb3xbox") {
      ++rb3xbox;
    }
  }
  CHECK_EQ(rb3xbox, 6);

  const SongFile& first = page->songs.front();
  CHECK_STR(first.title, "Be Kind");
  CHECK_STR(first.artist, "Marshmello and Halsey");
  CHECK_STR(first.gameformat, "rv");
  CHECK_TRUE(first.file_preview.empty());
  CHECK_TRUE(first.song_preview.empty());

  // The first rb3xbox entry carries both preview spellings - the evidence behind §3.3.
  const SongFile* previews = nullptr;
  for (const SongFile& song : page->songs) {
    if (song.title == "forget me too (feat. Halsey)") {
      previews = &song;
    }
  }
  CHECK_TRUE(previews != nullptr);
  if (previews != nullptr) {
    CHECK_STR(previews->artist, "Machine Gun Kelly");
    CHECK_TRUE(previews->file_preview.starts_with("https://www.youtube.com/embed/"));
    CHECK_TRUE(previews->song_preview.starts_with("https://www.youtube.com/watch"));
  }
}

void TestParseTolerates() {
  test::BeginCase("parse: null fields, missing arrays, numeric pagination and an error status");

  {
    const char* json = R"({
      "status": "success",
      "data": {
        "records": {"total_filtered": "7", "returned": "2"},
        "pagination": {"page": 2, "records": 25, "start": 25},
        "songs": [
          {"file": {"file_id": "5b947da4929df7.93833456", "gameformat": "rb3xbox",
                    "file_artist": "Dropbox", "file_title": "Song", "size": 0,
                    "rank_guitar": null, "diff_guitar": null,
                    "download_url": "https://www.dropbox.com/s/x ?dl=0"}},
          {"data": {"title": "No file object"}},
          {"file": {"file_id": "", "file_title": ""}}
        ]
      }
    })";
    std::string error;
    const std::optional<ListingPage> page = ParseListing(json, &error);
    CHECK_TRUE(page.has_value());
    if (page) {
      CHECK_EQ(page->page, 2);
      CHECK_EQ(page->records_per_page, 25);
      CHECK_EQ(page->start, 25);
      CHECK_EQ(page->total_filtered, 7);
      CHECK_EQ(page->songs.size(), 1);  // the entry with no file and the empty one are dropped
      const SongFile& song = page->songs.front();
      CHECK_STR(song.file_id, "5b947da4929df7.93833456");  // an opaque id is preserved verbatim
      CHECK_TRUE(song.size_bytes.has_value() && *song.size_bytes == 0);
      CHECK_FALSE(song.rank[static_cast<std::size_t>(Instrument::kGuitar)].has_value());
      CHECK_FALSE(song.HasChart(Instrument::kGuitar));
      CHECK_FALSE(page->HasMorePages());  // 25 + 2 >= 7
    }
  }

  {
    const char* json = R"({"status":"success","data":{"records":{"total_filtered":3}}})";
    std::string error;
    const std::optional<ListingPage> page = ParseListing(json, &error);
    CHECK_TRUE(page.has_value());
    if (page) {
      CHECK_TRUE(page->songs.empty());
    }
  }

  {
    const char* json =
        R"({"status":"error","error":{"message":"You need to provide a text at least 3 characters long"}})";
    std::string error;
    CHECK_FALSE(ParseListing(json, &error).has_value());
    CHECK_STR(error, "You need to provide a text at least 3 characters long");
  }

  {
    std::string error;
    CHECK_FALSE(ParseListing("{not json", &error).has_value());
    CHECK_TRUE(!error.empty());
    CHECK_FALSE(ParseListing(R"({"status":"success"})", &error).has_value());
    CHECK_TRUE(!error.empty());
  }

  {
    // A withdrawn record is parsed, then refused by the offer predicate.
    const char* json = R"({"status":"success","data":{"records":{"returned":1,"total_filtered":1},
      "pagination":{"page":1,"records":25},"songs":[{"file":{"file_id":"x","gameformat":"rb3xbox",
      "file_artist":"A","file_title":"T","deleted":1,
      "download_url":"/download_file/u/x/a_rb3con"}}]}})";
    std::string error;
    const std::optional<ListingPage> page = ParseListing(json, &error);
    CHECK_TRUE(page.has_value());
    if (page && !page->songs.empty()) {
      CHECK_TRUE(page->songs.front().withdrawn);
      CHECK_ENUM(DecideOffer(page->songs.front()).reject, OfferReject::kWithdrawn);
    }
  }
}

// --- Preview (§3.3) -------------------------------------------------------------------------------

void TestPreviewDecision(const fs::path& project_dir) {
  test::BeginCase("preview: the API returns YouTube web pages, never an audio asset (§3.3)");

  CHECK_FALSE(kApiSuppliesPreviewAudio);

  for (const char* name : {"list.json", "search.json"}) {
    const std::optional<std::string> json =
        ReadFile(project_dir / "docs" / "rythmverse-responses" / name);
    CHECK_TRUE(json.has_value());
    if (!json) {
      continue;
    }
    std::string error;
    const std::optional<ListingPage> page = ParseListing(*json, &error);
    CHECK_TRUE(page.has_value());
    if (!page) {
      continue;
    }
    int previews = 0;
    for (const SongFile& song : page->songs) {
      for (const std::string* field : {&song.file_preview, &song.song_preview}) {
        if (field->empty()) {
          continue;
        }
        ++previews;
        const bool youtube = field->starts_with("https://www.youtube.com/embed/") ||
                             field->starts_with("https://www.youtube.com/watch") ||
                             field->starts_with("https://youtu.be/");
        if (!youtube) {
          std::printf("         unexpected preview URL: %s\n", field->c_str());
        }
        CHECK_TRUE(youtube);
        // And the package policy could not fetch it even if the store asked it to.
        CHECK_FALSE(ResolveDownloadUrl(*field).ok);
      }
    }
    // The counts in the plan's §3.3 table.
    CHECK_EQ(previews, std::string_view(name) == "list.json" ? 14 : 8);
  }
}

// --- URL policy -----------------------------------------------------------------------------------

void TestDownloadUrls() {
  test::BeginCase("url: root-relative and same-site links resolve; everything else is refused");

  {
    const ResolvedUrl resolved =
        ResolveDownloadUrl("/download_file/anonymous-family/38a4dcc61d324e7883c9a393a7086bb9/"
                           "Youth Group - Forever Young_v1.3_rb3con");
    CHECK_TRUE(resolved.ok);
    CHECK_STR(resolved.url,
              "https://rhythmverse.co/download_file/anonymous-family/38a4dcc61d324e7883c9a393a7086bb9/"
              "Youth%20Group%20-%20Forever%20Young_v1.3_rb3con");
  }
  {
    const ResolvedUrl resolved = ResolveDownloadUrl(
        "HTTPS://RHYTHMVERSE.CO/download_file/u/id/Name_rb3con");
    CHECK_TRUE(resolved.ok);
    CHECK_STR(resolved.url, "https://rhythmverse.co/download_file/u/id/Name_rb3con");
  }
  {
    // An existing escape keeps its exact spelling: no double encoding.
    const ResolvedUrl resolved =
        ResolveDownloadUrl("/download_file/u/id/My%20Song_rb3con");
    CHECK_TRUE(resolved.ok);
    CHECK_STR(resolved.url, "https://rhythmverse.co/download_file/u/id/My%20Song_rb3con");
  }
  {
    const ResolvedUrl resolved = ResolveDownloadUrl("/download_file/u/id/x?token=1");
    CHECK_TRUE(resolved.ok);
    CHECK_STR(resolved.url, "https://rhythmverse.co/download_file/u/id/x?token=1");
  }

  struct Case {
    const char* raw;
    UrlReject reject;
  };
  const Case cases[] = {
      {"", UrlReject::kEmpty},
      {"   ", UrlReject::kEmpty},
      {"//rhythmverse.co/download_file/u/id/x", UrlReject::kMalformed},
      {"http://rhythmverse.co/download_file/u/id/x", UrlReject::kNotHttps},
      {"https://www.dropbox.com/s/abc", UrlReject::kOffSite},
      {"https://rhythmverse.co.evil.example/download_file/u/id/x", UrlReject::kOffSite},
      {"https://user:secret@rhythmverse.co/download_file/u/id/x", UrlReject::kCredentials},
      {"https://rhythmverse.co:8443/download_file/u/id/x", UrlReject::kBadPort},
      {"/download_file/u/id/a%2Fb", UrlReject::kEncodedSeparator},
      {"/download_file/u/id/a%5Cb", UrlReject::kEncodedSeparator},
      {"/download_file/../etc/passwd", UrlReject::kUpwardPath},
      {"/download_file/", UrlReject::kNoFileName},
      {"/download_file", UrlReject::kNotDirect},
      {"/songfile/38a4dcc61d324e7883c9a393a7086bb9", UrlReject::kNotDirect},
      {"/assets/album_art/x/y.png", UrlReject::kNotDirect},
      {"/download_file/u/id/bad%2", UrlReject::kMalformed},
      {"/download_file/u/id/back\\slash", UrlReject::kMalformed},
  };
  for (const Case& item : cases) {
    const ResolvedUrl resolved = ResolveDownloadUrl(item.raw);
    if (resolved.ok || resolved.reject != item.reject) {
      std::printf("         %s -> ok=%d reject=%s reason=%s\n", item.raw, resolved.ok ? 1 : 0,
                  std::string(UrlRejectText(resolved.reject)).c_str(), resolved.reason.c_str());
    }
    CHECK_FALSE(resolved.ok);
    CHECK_ENUM(resolved.reject, item.reject);
    CHECK_TRUE(!resolved.reason.empty());
  }

  CHECK_STR(UrlFileName("https://rhythmverse.co/download_file/u/id/My%20Song_rb3con"),
            "My Song_rb3con");
  CHECK_STR(UrlFileName("/download_file/u/id/x.zip?token=1#frag"), "x.zip");
}

void TestAlbumArtUrls() {
  test::BeginCase("url: album art resolves under /assets/album_art/ and must be an image");

  const ResolvedUrl resolved =
      ResolveAlbumArtUrl("/assets/album_art/anonymous-family/38a4dcc61d324e7883c9a393a7086bb9.png");
  CHECK_TRUE(resolved.ok);
  CHECK_STR(resolved.url,
            "https://rhythmverse.co/assets/album_art/anonymous-family/"
            "38a4dcc61d324e7883c9a393a7086bb9.png");

  CHECK_FALSE(ResolveAlbumArtUrl("/assets/album_art/f/x.zip").ok);
  CHECK_ENUM(ResolveAlbumArtUrl("/assets/album_art/f/x.zip").reject, UrlReject::kNotAnImage);
  CHECK_ENUM(ResolveAlbumArtUrl("/download_file/u/id/x.png").reject, UrlReject::kNotDirect);
  CHECK_ENUM(ResolveAlbumArtUrl("https://img.example/x.png").reject, UrlReject::kOffSite);
}

void TestArchiveNames() {
  test::BeginCase("archive: suffixes and multi-volume variants are refused; packages are candidates");

  const char* archives[] = {
      "a.zip", "A.ZIP", "a.zip.", "a.rar", "a.7z", "a.tar", "a.tar.gz", "a.tgz",
      "a.bz2", "a.xz",   "a.r00", "a.z01", "a.001", "foo part1.rar", "x.zip ",
  };
  for (const char* name : archives) {
    if (!IsArchiveFileName(name)) {
      std::printf("         expected an archive: %s\n", name);
    }
    CHECK_TRUE(IsArchiveFileName(name));
  }

  const char* candidates[] = {
      "", "Song", "a_rb3con", "a.con", "a.mogg", "a.png", "a", "rb3con",
  };
  for (const char* name : candidates) {
    if (IsArchiveFileName(name)) {
      std::printf("         expected a candidate: %s\n", name);
    }
    CHECK_FALSE(IsArchiveFileName(name));
  }
}

// --- Transfer validation --------------------------------------------------------------------------

void TestPrefixes() {
  test::BeginCase("prefix: only STFS may proceed; archive and web-page signatures are refused");

  auto classify = [](std::string_view bytes) {
    return ClassifyPrefix(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
  };

  CHECK_ENUM(classify(""), PrefixKind::kEmpty);
  CHECK_ENUM(classify("CON "), PrefixKind::kStfsCon);
  CHECK_ENUM(classify("LIVE"), PrefixKind::kStfsLive);
  CHECK_ENUM(classify("PIRS"), PrefixKind::kStfsPirs);
  CHECK_ENUM(classify(std::string_view("PK\x03\x04", 4)), PrefixKind::kZip);
  CHECK_ENUM(classify(std::string_view("Rar!\x1A\x07", 6)), PrefixKind::kRar);
  CHECK_ENUM(classify(std::string_view("\x37\x7A\xBC\xAF\x27\x1C", 6)), PrefixKind::kSevenZip);
  CHECK_ENUM(classify(std::string_view("\x1F\x8B", 2)), PrefixKind::kGzip);
  CHECK_ENUM(classify("BZh9"), PrefixKind::kBzip2);
  CHECK_ENUM(classify(std::string_view("\xFD" "7zXZ\x00", 6)), PrefixKind::kXz);
  CHECK_ENUM(classify("<!DOCTYPE html>"), PrefixKind::kHtml);
  CHECK_ENUM(classify("  <html>"), PrefixKind::kHtml);
  CHECK_ENUM(classify(std::string_view("\xEF\xBB\xBF" "<html>", 9)), PrefixKind::kHtml);
  CHECK_ENUM(classify(R"({"status":"error"})"), PrefixKind::kJson);
  CHECK_ENUM(classify("garbage"), PrefixKind::kUnknown);

  CHECK_TRUE(IsStfsPrefix(PrefixKind::kStfsCon));
  CHECK_TRUE(IsStfsPrefix(PrefixKind::kStfsLive));
  CHECK_TRUE(IsStfsPrefix(PrefixKind::kStfsPirs));
  CHECK_FALSE(IsStfsPrefix(PrefixKind::kZip));
  CHECK_TRUE(IsArchivePrefix(PrefixKind::kZip));
  CHECK_TRUE(IsArchivePrefix(PrefixKind::kSevenZip));
  CHECK_FALSE(IsArchivePrefix(PrefixKind::kHtml));
  CHECK_FALSE(IsArchivePrefix(PrefixKind::kStfsCon));
}

void TestTransferLength() {
  test::BeginCase("length: agreement is required when known; zero/missing is unknown, not zero bytes");

  CHECK_TRUE(CheckTransferLength(100, 100, 100).ok);
  CHECK_TRUE(CheckTransferLength(std::nullopt, 100, 100).ok);
  CHECK_TRUE(CheckTransferLength(100, std::nullopt, 100).ok);
  CHECK_TRUE(CheckTransferLength(std::nullopt, std::nullopt, 12345).ok);
  CHECK_TRUE(CheckTransferLength(0, 0, 12345).ok);  // zero means unknown
  CHECK_TRUE(!CheckTransferLength(100, 101, 100).ok);
  CHECK_TRUE(!CheckTransferLength(100, 100, 99).ok);
  CHECK_TRUE(!CheckTransferLength(-1, std::nullopt, 10).ok);
  CHECK_TRUE(!CheckTransferLength(std::nullopt, -1, 10).ok);
  CHECK_TRUE(!CheckTransferLength(std::nullopt, std::nullopt, kMaxDownloadBytes + 1).ok);
  CHECK_TRUE(!CheckTransferLength(kMaxDownloadBytes + 1, std::nullopt, kMaxDownloadBytes + 1).ok);
}

// --- Eligibility ----------------------------------------------------------------------------------

void TestFixtureEligibility(const fs::path& project_dir) {
  test::BeginCase("offer: the fixtures split into eligible rb3xbox links and refused everything else");

  struct Expectation {
    const char* name;
    int eligible;
    int wrong_format;
    int other;
  };
  const Expectation expectations[] = {
      {"list.json", 17, 8, 0},
      // search.json has six rb3xbox records, one of which is an off-site http://dingorb.com link.
      {"search.json", 5, 2, 1},
  };

  for (const Expectation& expectation : expectations) {
    const std::optional<std::string> json =
        ReadFile(project_dir / "docs" / "rythmverse-responses" / expectation.name);
    CHECK_TRUE(json.has_value());
    if (!json) {
      continue;
    }
    std::string error;
    const std::optional<ListingPage> page = ParseListing(*json, &error);
    CHECK_TRUE(page.has_value());
    if (!page) {
      continue;
    }
    int eligible = 0;
    int wrong_format = 0;
    int other = 0;
    for (const SongFile& song : page->songs) {
      const OfferDecision decision = DecideOffer(song);
      if (decision.eligible) {
        ++eligible;
        CHECK_TRUE(decision.download_url.starts_with("https://rhythmverse.co/download_file/"));
      } else if (decision.reject == OfferReject::kWrongFormat) {
        ++wrong_format;
      } else {
        ++other;
        CHECK_TRUE(!decision.reason.empty());
      }
    }
    CHECK_EQ(eligible, expectation.eligible);
    CHECK_EQ(wrong_format, expectation.wrong_format);
    CHECK_EQ(other, expectation.other);
  }
}

void TestOfferDecisions() {
  test::BeginCase("offer: withdrawal, missing identity, no link, off-site and archive names are refused");

  {
    SongFile song = Song("Artist", "Title");
    CHECK_TRUE(DecideOffer(song).eligible);
  }
  {
    SongFile song = Song("Artist", "Title");
    song.gameformat = "rv";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kWrongFormat);
  }
  {
    SongFile song = Song("", "Title");
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kMissingIdentity);
  }
  {
    SongFile song = Song("Artist", "Title");
    song.download_url.clear();
    song.download_page_url = "https://rhythmverse.co/songfile/x";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kNoDirectLink);
  }
  {
    SongFile song = Song("Artist", "Title");
    song.download_url = "https://www.dropbox.com/s/ovs6fm88pw593hq/DRIThemBones?dl=0";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kOffSiteLink);
  }
  {
    SongFile song = Song("Artist", "Title");
    song.download_url = "http://dingorb.com/?id=67f6369551a4ac4832c9a346";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kNotDirectFile);
  }
  {
    SongFile song = Song("Artist", "Title");
    song.file_name = "Song.zip";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kArchiveName);
  }
  {
    // The URL's own last segment is checked too, not just the upload's file name.
    SongFile song = Song("Artist", "Title");
    song.file_name = "Song_rb3con";
    song.download_url = "/download_file/u/id/Song.zip";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kArchiveName);
  }
  {
    // An encoded archive suffix is decoded before the check.
    SongFile song = Song("Artist", "Title");
    song.file_name = "Song_rb3con";
    song.download_url = "/download_file/u/id/Song%2Ezip";
    CHECK_ENUM(DecideOffer(song).reject, OfferReject::kArchiveName);
  }
}

void TestInstrumentFilter() {
  test::BeginCase("filter: On requires a chart, Off imposes nothing, all-off is no constraint");

  SongFile full = Song("A", "T");
  SetChart(&full, Instrument::kGuitar, 4);
  SetChart(&full, Instrument::kBass, 4);
  SetChart(&full, Instrument::kDrums, 4);
  SetChart(&full, Instrument::kVocals, 3);
  SetChart(&full, Instrument::kKeys, 0);  // the keys field's "not authored"

  const InstrumentFilter defaults;
  CHECK_TRUE(PassesInstrumentFilter(full, defaults));
  CHECK_EQ(defaults.Enabled().size(), 4);

  InstrumentFilter with_keys;
  with_keys.keys = true;
  CHECK_FALSE(PassesInstrumentFilter(full, with_keys));

  SongFile instrumental = Song("A", "Instrumental");
  SetChart(&instrumental, Instrument::kGuitar, 2);
  SetChart(&instrumental, Instrument::kBass, 2);
  SetChart(&instrumental, Instrument::kDrums, 2);
  SetChart(&instrumental, Instrument::kVocals, -1);  // "not authored"
  CHECK_FALSE(PassesInstrumentFilter(instrumental, defaults));
  InstrumentFilter no_vocals;
  no_vocals.vocals = false;
  CHECK_TRUE(PassesInstrumentFilter(instrumental, no_vocals));

  SongFile keys_only = Song("A", "Keys Only");
  SetChart(&keys_only, Instrument::kKeys, 6);
  const InstrumentFilter keys{false, false, false, false, true};
  CHECK_TRUE(PassesInstrumentFilter(keys_only, keys));
  CHECK_FALSE(PassesInstrumentFilter(keys_only, defaults));

  const InstrumentFilter all_off{false, false, false, false, false};
  SongFile chartless = Song("A", "Chartless");
  CHECK_TRUE(PassesInstrumentFilter(chartless, all_off));
  CHECK_FALSE(PassesInstrumentFilter(chartless, defaults));
}

// --- Matching -------------------------------------------------------------------------------------

void TestMatching() {
  test::BeginCase("match: trim/collapse/case-fold, punctuation kept, both fields required");

  CHECK_STR(FoldForMatch("  THE   1975 "), "the 1975");
  CHECK_STR(FoldForMatch("Caf\xC3\xA9"), "caf\xC3\xA9");
  CHECK_STR(FoldForMatch("CAF\xC3\x89"), "caf\xC3\xA9");
  CHECK_STR(FoldForMatch("Sigur R\xC3\xB3s"), "sigur r\xC3\xB3s");
  CHECK_STR(FoldForMatch("a\xC2\xA0" "\xC2\xA0" "b"), "a b");
  CHECK_STR(FoldForMatch("\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2"),
            "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
  // Greek capital sigma folds to sigma, and the final form folds with it (the folded text is all
  // sigma, so the expected string's last letter is \xCF\x83, not the final form).
  CHECK_STR(FoldForMatch("\xCE\xA3\xCE\x91\xCE\x9A\xCE\x97\xCE\xA3"),
            "\xCF\x83\xCE\xB1\xCE\xBA\xCE\xB7\xCF\x83");

  const MatchKey acdc = MakeMatchKey("AC/DC", "T.N.T");
  CHECK_TRUE(acdc.usable());
  CHECK_FALSE(SameSong(acdc, MakeMatchKey("ACDC", "TNT")));  // punctuation is not folded
  CHECK_TRUE(SameSong(acdc, MakeMatchKey("ac/dc", "t.n.t")));

  const MatchKey empty = MakeMatchKey("", "Title");
  CHECK_FALSE(empty.usable());
  CHECK_FALSE(SameSong(empty, empty));
}

}  // namespace

#ifdef _WIN32
// Manual only: `rb_blitz_store_client_tests --live-list` sends ONE list request with records=1
// through the real transport and parses the answer. ctest never passes the flag, so the suite stays
// hermetic; the probe exists because the transport is the one part of P0 no fixture can cover.
int LiveListProbe() {
  test::BeginCase("live: one list request through store_http (manual, not run by ctest)");

  Query query;
  query.records = 1;
  QueryProblem problem = QueryProblem::kNone;
  const std::optional<RequestSpec> spec = BuildListRequest(query, &problem);
  CHECK_TRUE(spec.has_value());
  if (!spec) {
    return 1;
  }

  const HttpResponse response = PostForm(spec->url, spec->body, spec->headers);
  if (!response.ok) {
    std::printf("         transport failed: %s\n", response.error.c_str());
  }
  CHECK_TRUE(response.ok);
  if (!response.ok) {
    return 1;
  }
  std::printf("         HTTP %d, %zu body byte(s), %zu header(s)\n", response.status,
              response.body.size(), response.headers.size());
  CHECK_EQ(response.status, 200);

  std::string error;
  const std::optional<ListingPage> page = ParseListing(response.body, &error);
  if (!page) {
    std::printf("         parse failed: %s\n", error.c_str());
  }
  CHECK_TRUE(page.has_value());
  if (!page) {
    return 1;
  }
  CHECK_EQ(page->returned, 1);
  CHECK_TRUE(!page->songs.empty());
  if (!page->songs.empty()) {
    const SongFile& song = page->songs.front();
    std::printf("         %s - %s [%s], eligible=%d\n", song.artist.c_str(), song.title.c_str(),
                song.gameformat.c_str(), DecideOffer(song).eligible ? 1 : 0);
    CHECK_STR(song.gameformat, kTargetFormat);
  }
  return test::Finish();
}
#endif

int main(int argc, char** argv) {
  fs::path project_dir = kDefaultProjectDir;
  bool live_list = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--project-dir" && i + 1 < argc) {
      project_dir = argv[++i];
    } else if (arg == "--live-list") {
      live_list = true;
    }
  }
  std::printf("project dir: %s\n", project_dir.string().c_str());

#ifdef _WIN32
  if (live_list) {
    return LiveListProbe();
  }
#else
  (void)live_list;
#endif
  TestListRequest();
  TestSearchRequest();
  TestListFixture(project_dir);
  TestSearchFixture(project_dir);
  TestParseTolerates();
  TestPreviewDecision(project_dir);
  TestDownloadUrls();
  TestAlbumArtUrls();
  TestArchiveNames();
  TestPrefixes();
  TestTransferLength();
  TestFixtureEligibility(project_dir);
  TestOfferDecisions();
  TestInstrumentFilter();
  TestMatching();

  return test::Finish();
}
