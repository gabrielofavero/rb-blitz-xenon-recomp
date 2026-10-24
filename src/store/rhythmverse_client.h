// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P0 of the RhythmVerse store plan (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §3, §3.1, §3.2): the catalogue client and the contracts built around it.
//
// Why this is an SDK-free module of its own: every rule the plan states in words - which offers are
// eligible, what a direct download link is, what a redirect may point at, what an archive looks like,
// when artist/title matching is "the same song" - is a decision over plain data. Here they are
// functions, so `tests/store_client_tests.cpp` can walk the whole matrix without a boot, without a
// network and without a package, and the store's UI/download half only applies the answer. The
// transport (`store_http.h`) is deliberately a separate file: nothing in this one knows about
// WinHTTP, and nothing here may be called from a guest thread with guest state (project rule).
//
// Two measured facts shape the API contract (both 2026-10-20):
//
//   * the form keys are sent percent-encoded (`instrument%5B%5D=guitar`), which the site accepts:
//     `records=1` returned `status=success` with one `rb3xbox` record (`out/_swap/rv_p0_encoded.json`);
//   * `pagination.page` / `pagination.records` arrive as JSON **strings** in the captured responses
//     while the counts arrive as numbers, and the review capture has them as numbers - so every field
//     reader below accepts either spelling.
//
// Preview audio: the API returns no preview asset. `file.file_preview` is a YouTube *embed page* and
// `data.song_preview` is a YouTube *watch page* (plan §3.3, measured 2026-10-20); the retail preview
// needs a downloadable audio file, so v1 ships no preview and never opens those links. Both raw
// strings are kept on the model so the measurement stays visible and a future API revision fails a
// test instead of silently changing what the store shows.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb_blitz::store {

// --- Catalogue endpoints and the policies' fixed values -------------------------------------------
//
// The gameformat is the route token, not a body parameter (a `gameformat` body field is rejected by
// the site). §3.
inline constexpr char kApiBase[] = "https://rhythmverse.co";
inline constexpr char kListPath[] = "/api/rb3xbox/songfiles/list";
inline constexpr char kSearchPath[] = "/api/rb3xbox/songfiles/search/live";
inline constexpr char kSiteHost[] = "rhythmverse.co";
inline constexpr char kTargetFormat[] = "rb3xbox";

// Only RhythmVerse-hosted direct files and their album art are fetchable in v1 (§3.1).
inline constexpr char kDownloadPathPrefix[] = "/download_file/";
inline constexpr char kAlbumArtPathPrefix[] = "/assets/album_art/";

inline constexpr int kDefaultPageSize = 25;  // what the site itself asks for
inline constexpr int kMaxPageSize = 50;      // a bound the client enforces, not an API limit
inline constexpr int kMinSearchChars = 3;    // the API's own minimum, in code points
inline constexpr int kMaxSearchChars = 64;   // the search field's bound, in code points

// §3.1: a bounded transfer. A Rock Band custom package is tens of megabytes; this is a ceiling the
// host enforces even when the API and the response both omit a length.
inline constexpr std::uint64_t kMaxDownloadBytes = 512ull * 1024ull * 1024ull;

// §3.3: the API supplies preview *pages*, not audio. Kept as a named constant so the tests can pin
// the decision and a future API revision has one place to change.
inline constexpr bool kApiSuppliesPreviewAudio = false;

// --- Instruments ----------------------------------------------------------------------------------

enum class Instrument : std::uint8_t { kGuitar, kBass, kDrums, kVocals, kKeys, kCount };

inline constexpr std::size_t kInstrumentCount = static_cast<std::size_t>(Instrument::kCount);

// The body token (`guitar`, `bass`, `drums`, `vocals`, `keys`) and the chart token the API suffixes
// its fields with (`diff_guitar`, `rank_guitar`, ...). They are the same five words, so one function
// answers both and cannot drift.
std::string_view InstrumentToken(Instrument instrument);
std::optional<Instrument> InstrumentFromToken(std::string_view token);

// The instrument filter (plan §1.6/P6): On = that instrument's chart is required, Off = no condition.
// Keys default off - the owner's defaults - and Off does *not* exclude a song that has keys.
struct InstrumentFilter {
  bool guitar = true;
  bool bass = true;
  bool drums = true;
  bool vocals = true;
  bool keys = false;

  bool Allows(Instrument instrument) const;
  // The enabled instruments in enum order; empty means "no instrument constraint at all".
  std::vector<Instrument> Enabled() const;
};

// --- Requests -------------------------------------------------------------------------------------

struct Query {
  InstrumentFilter instruments;
  int page = 1;
  int records = kDefaultPageSize;
  // Search only. One value: artist OR title, never both concatenated (plan §1.5) - this module does
  // not build a combined query and the UI must not either.
  std::string text;
};

enum class QueryProblem {
  kNone,
  kBadPage,
  kBadPageSize,
  kTextTooShort,
  kTextTooLong,
  kTextHasControlCharacters,
};

std::string_view QueryProblemText(QueryProblem problem);

// A request the transport can send as-is: no URL or body construction is left to the caller.
struct RequestSpec {
  std::string url;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

// `problem` (optional) receives the reason a request could not be built; the return is empty then.
std::optional<RequestSpec> BuildListRequest(const Query& query, QueryProblem* problem = nullptr);
std::optional<RequestSpec> BuildSearchRequest(const Query& query, QueryProblem* problem = nullptr);

// The exact `application/x-www-form-urlencoded` body, exposed for tests and diagnostics.
std::string BuildFormBody(const Query& query, bool search);

// --- The API base, and the measurement override ---------------------------------------------------
//
// `kApiBase` is the shipped value, and `ApiBase()` is what the two request builders below use. The
// override exists for the failure measurements that need a host which cannot answer (RhythmVerse
// plan §9/P9-10: the press's popup and its background retry): pointed at nothing, the walk lands in
// `kFailed` in a second instead of after a DNS or connect timeout, and a local stub can be flipped
// from refusing to serving inside one boot.
//
// It deliberately does **not** move the download/artwork resolution below: that stays same-site
// with `kSiteHost` (a policy, §3.1), so no diagnostic boot can be talked into fetching a package
// from somewhere else.
std::string_view ApiBase();

// Empty restores `kApiBase`. Otherwise `base` must be an absolute `http://` or `https://` origin
// with no path, credentials, query or fragment (a port is allowed), because the endpoints are
// appended to it. `http://` is accepted only for a **loopback** host - the measurement stub - and
// every other value is refused, leaving the current base alone.
bool SetApiBaseOverride(std::string_view base);

// True for the loopback spellings a stub can use: `127.0.0.1`, `::1` / `[::1]` and `localhost`,
// case-insensitively. The plain-http allowance above and the transport's one exception
// (`store_http.cpp`) both read this, so the rule has one definition.
bool IsLoopbackHost(std::string_view host);

// RFC 3986 percent-encoding with `%20` for a space (not `+`), and only the unreserved set left alone.
std::string FormEncode(std::string_view text);

// --- The offer model ------------------------------------------------------------------------------

struct SongFile {
  std::string file_id;       // opaque; may be dotted, never parsed for meaning
  std::string file_name;     // upload name; may be the only spelling of the title
  std::string gameformat;    // only "rb3xbox" is eligible
  std::string gamesource;

  std::string artist;
  std::string title;
  std::string album;
  std::string genre;
  std::optional<int> year;
  std::optional<int> album_track_number;
  std::optional<int> song_length_seconds;  // the API's `file_song_length` is seconds, not millis
  std::optional<std::int64_t> size_bytes;  // 0 = unknown, never "a zero-byte package"
  // The **file's** own download count (`file.downloads`), never `data.downloads` - measured
  // 2026-10-23 on the committed captures, where the same record carries 61 and 0 respectively.
  // The details page's credit block shows this one (plan §9/P9-13).
  std::optional<int> downloads;

  // P9-13: the custom's own credit facts, which are **the file's**, not the song's - one title can
  // have several customs with different authors and dates, so the details page has to say which
  // one the player is looking at (owner correction, plan §9/P9-13).
  //
  //   * `audio_type` is the catalogue's **code** (`single`, `partial`, `full`, `karaoke`), measured
  //     on the committed captures 2026-10-23: the list capture carries `single`/`partial`/`full`
  //     and neither capture carries `karaoke`. The code -> label table lives in
  //     `store_metadata.h`'s `AudioTypeLabel`; the raw token is never drawn;
  //   * `charter` is `file.charter`, else `file.author.name`, else `file.author_id`, in that order.
  //     `charter` is null on every captured record, so in practice this is the author's display
  //     name (`Virtual-Man` on the captured record);
  //   * `release_date` is `file.release_date`, else `file.upload_date`, reduced to the date part by
  //     `DatePart` (`2026-10-11 19:07:55` -> `2026-10-11`).
  std::string audio_type;
  std::string charter;
  std::string release_date;

  std::string download_url;       // as returned: root-relative or absolute, possibly containing spaces
  std::string download_page_url;  // a web page, never used as a download target
  std::string album_art;          // as returned (usually root-relative under /assets/album_art/)

  // §3.3: web pages, kept for evidence. Never fetched and never turned into a preview.
  std::string file_preview;
  std::string song_preview;

  // `diff_*` is a difficulty tier the API publishes; `rank_*` is the numeric rank the retail tier
  // display reads. They are different numbers: a tier must never be passed as a rank (plan OQ4).
  // `file` carries g/b/d/v + the pro parts, and the keys field only exists in the `data` copy.
  std::array<std::optional<int>, kInstrumentCount> difficulty;
  std::array<std::optional<int>, kInstrumentCount> rank;

  std::string update_date;  // the site's own string, kept for sorting/diagnostics
  bool withdrawn = false;   // deleted / retired / hidden by the catalogue

  // A chart exists when its tier is present and at least 1. Measured 2026-10-20 on the committed
  // captures: every authored part is a tier 1..6, absence is `-1` (the pro parts), `0` (the keys
  // field, which only the `data` copy carries) or a missing field. The retail display wants the
  // *rank*, a different number - `difficulty` must never be passed as a rank (plan OQ4).
  bool HasChart(Instrument instrument) const;
};

struct ListingPage {
  int page = 0;
  int records_per_page = 0;
  int start = 0;
  int total_available = 0;
  int total_filtered = 0;  // a server count of *records*, not of downloadable offers
  int returned = 0;
  std::vector<SongFile> songs;

  // True when the server says there are rows past this page. Filtering can empty a page without
  // making it the last one, which is why this reads the server's own cursor (§P7).
  bool HasMorePages() const;
};

// Parses one listing/search response. `error` receives a human-readable reason on failure; a
// `status:"error"` answer is a failure and carries the site's own message.
std::optional<ListingPage> ParseListing(std::string_view json, std::string* error);

// --- Chunking and politeness (§P7) ----------------------------------------------------------------

// The date part of a catalogue timestamp (`2026-10-11 19:07:55` -> `2026-10-11`), or empty for the
// site's zero date (`0000-00-00 00:00:00`, which appears in its own date fields) and for a string
// that does not start with a `YYYY-MM-DD` shape. The details page's credit line shows the day only
// (plan §9/P9-13); `0 Downloads`-style composed lines are the caller's business, not this one's.
std::string DatePart(std::string_view text);

// The API page a chunked fetch should ask for next, or 0 to stop. Plan §P7's rule in one function:
// the cursor is the **server's own page number**, kept separately from the offers a caller has
// kept - so a page the eligibility/instrument filter emptied is not mistaken for the last one (an
// underfilled page is a valid page, and only `HasMorePages()` decides there is nothing after it),
// and the server's `total_filtered` is never read as a count of downloadable offers. `page_budget`
// is the per-action bound the caller was configured with (`store_catalogue_pages` /
// `store_search_pages`); `pages_fetched` counts the pages this action has already read.
int NextPageAfter(const ListingPage& page, int pages_fetched, int page_budget);

// Plan §P7's backoff: how long a failed answer holds the next request off. A server that sent
// `Retry-After` names its own wait (clamped to 5..600 s); 429/503 without one gets a minute; any
// other 5xx a minute; a transport failure (`status` 0) 10 s; a 200 whose body could not be read a
// minute (the same drift); any other status 120 s, because a moved route cannot be fixed by
// retrying it soon. The value is a wait, not an error string - callers log it and refuse work
// until it elapses.
inline constexpr int kBackoffNetworkSeconds = 10;
inline constexpr int kBackoffServerErrorSeconds = 60;
inline constexpr int kBackoffAnswerSeconds = 60;
inline constexpr int kBackoffStatusSeconds = 120;
inline constexpr int kBackoffMinimumSeconds = 5;
inline constexpr int kBackoffMaximumSeconds = 600;
int FailureBackoffSeconds(int http_status, std::optional<int> retry_after_seconds);

// --- URL policy (§3.1) ----------------------------------------------------------------------------

struct UrlParts {
  std::string scheme;  // lowercased
  std::string host;    // lowercased
  unsigned port = 0;
  bool has_port = false;
  std::string path;   // raw (encoded) bytes, leading '/'
  std::string query;  // without '?'
  bool has_credentials = false;
};

// Strict parse of an absolute http(s) URL. Returns nullopt on anything that is not one. It does not
// apply the same-site policy; `ResolveSameSiteUrl` does.
std::optional<UrlParts> ParseUrl(std::string_view text);

// Why a candidate link was refused. The caller maps these onto the offer's own reason
// (`OfferReject`); tests pin them so a policy relaxation is visible.
enum class UrlReject {
  kNone,
  kEmpty,
  kMalformed,
  kNotHttps,
  kOffSite,
  kCredentials,
  kBadPort,
  kEncodedSeparator,
  kUpwardPath,
  kNotDirect,
  kNoFileName,
  kNotAnImage,
};

std::string_view UrlRejectText(UrlReject reject);

struct ResolvedUrl {
  bool ok = false;
  UrlReject reject = UrlReject::kNone;
  std::string url;     // safe to hand to the transport (spaces and non-ASCII encoded, %HH preserved)
  std::string reason;  // why it was refused, for the log and the tests
};

// Root-relative `/download_file/...` and absolute `https://rhythmverse.co/download_file/...` only.
ResolvedUrl ResolveDownloadUrl(std::string_view raw);
// The same policy for album art, under `/assets/album_art/`, and image extensions only.
ResolvedUrl ResolveAlbumArtUrl(std::string_view raw);

// The file name an archive check uses: percent-decoded, query/fragment removed, last path segment.
std::string UrlFileName(std::string_view url_or_path);

// Archive detection by name (case-insensitive, including multi-volume suffixes). `_rb3con`,
// `.con` and extensionless names are *candidates*, never archives.
bool IsArchiveFileName(std::string_view name);

// --- Eligibility (§3.1 step 1, §3.2) --------------------------------------------------------------

enum class OfferReject {
  kNone,
  kWrongFormat,
  kWithdrawn,
  kMissingIdentity,
  kNoDirectLink,
  kOffSiteLink,
  kNotDirectFile,
  kArchiveName,
};

std::string_view OfferRejectText(OfferReject reject);

struct OfferDecision {
  bool eligible = false;
  OfferReject reject = OfferReject::kWrongFormat;
  std::string download_url;  // resolved and encoded when eligible
  std::string reason;
};

// The cheap metadata predicate: format, identity, withdrawal, direct link, archive name. Instrument
// filtering is a product decision on top of it (PassesInstrumentFilter), and the *bytes* are only
// ever certified by the transfer (§3.1: ValidatePackage).
OfferDecision DecideOffer(const SongFile& song);

// True when every enabled instrument has a chart on this song; all-off means no constraint.
bool PassesInstrumentFilter(const SongFile& song, const InstrumentFilter& filter);

// --- Transfer validation (§3.1 steps 4-5) ---------------------------------------------------------

enum class PrefixKind {
  kEmpty,
  kStfsCon,
  kStfsLive,
  kStfsPirs,
  kZip,
  kRar,
  kSevenZip,
  kGzip,
  kBzip2,
  kXz,
  kHtml,
  kJson,
  kUnknown,
};

// Classifies the first bytes of a response body. Only an STFS prefix may proceed.
PrefixKind ClassifyPrefix(std::span<const std::uint8_t> bytes);
bool IsStfsPrefix(PrefixKind kind);
bool IsArchivePrefix(PrefixKind kind);

struct LengthCheck {
  bool ok = false;
  std::string reason;
};

// §3.1 step 5: a positive API size and a positive Content-Length must agree with each other and with
// the received byte count; a missing or zero length is "unknown", not a failure.
LengthCheck CheckTransferLength(std::optional<std::int64_t> api_size,
                                std::optional<std::int64_t> content_length, std::uint64_t received);

// --- Loaded-library matching (§3.2) ---------------------------------------------------------------

// Trimmed, whitespace-collapsed, case-folded UTF-8. Folding covers ASCII, Latin-1, Latin Extended-A,
// Greek and Cyrillic - the ranges a RhythmVerse catalogue actually uses - and nothing else. Invalid
// UTF-8 is folded as Latin-1 bytes rather than dropped, so two copies of the same broken name agree.
std::string FoldForMatch(std::string_view utf8);

struct MatchKey {
  std::string artist;
  std::string title;
  // Both halves must be non-empty; a key that is not usable never matches anything.
  bool usable() const { return !artist.empty() && !title.empty(); }
};

MatchKey MakeMatchKey(std::string_view artist, std::string_view title);
bool SameSong(const MatchKey& lhs, const MatchKey& rhs);

}  // namespace rb_blitz::store
