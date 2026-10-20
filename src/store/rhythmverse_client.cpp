// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See rhythmverse_client.h. Everything here is a decision over plain data: no SDK, no Win32, no
// guest, no transport. That is what lets `tests/store_client_tests.cpp` walk the plan's §3.1 matrix
// (direct links, off-site links, lookalike hosts, encoded separators, archives, length agreement)
// from the committed fixtures, and pin the §3.3 preview decision.

#include "store/rhythmverse_client.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "nlohmann/json.hpp"

namespace rb_blitz::store {
namespace {

constexpr std::string_view kHexDigits = "0123456789ABCDEF";

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string_view TrimAscii(std::string_view text) {
  while (!text.empty() && IsAsciiSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && IsAsciiSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

bool IsUnreserved(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '_' || c == '.' || c == '~';
}

bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return c - 'A' + 10;
}

// Bytes the URL itself may carry unencoded (RFC 3986 pchar plus '/' for a path).
bool IsPathSafe(unsigned char c) {
  if (IsUnreserved(c)) {
    return true;
  }
  switch (c) {
    case '!':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
    case ':':
    case '@':
    case '/':
      return true;
    default:
      return false;
  }
}

bool IsQuerySafe(unsigned char c) { return IsPathSafe(c) || c == '?'; }

bool HasControlOrBackslash(std::string_view text) {
  for (const unsigned char c : text) {
    if (c < 0x20 || c == 0x7F || c == '\\') {
      return true;
    }
  }
  return false;
}

std::string ToLowerAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

// Every '%' must start a complete two-digit escape. A raw '%' in a file name is the site's job to
// encode; a URL we cannot decode unambiguously is refused rather than guessed at (§3.1 step 1).
bool ValidPercentEscapes(std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '%') {
      continue;
    }
    if (i + 2 >= text.size() || !IsHexDigit(text[i + 1]) || !IsHexDigit(text[i + 2])) {
      return false;
    }
    i += 2;
  }
  return true;
}

std::string PercentDecode(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size() && IsHexDigit(text[i + 1]) && IsHexDigit(text[i + 2])) {
      out.push_back(static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2])));
      i += 2;
      continue;
    }
    out.push_back(text[i]);
  }
  return out;
}

// Re-encodes only the bytes that cannot appear raw, so an existing escape keeps its exact spelling
// and the transport never double-encodes (§3.1 step 1).
std::string EncodeUrlBytes(std::string_view text, bool query) {
  std::string out;
  out.reserve(text.size() + 16);
  for (std::size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '%') {
      // Validated by the caller; keep the escape verbatim.
      out.append(text.substr(i, 3));
      i += 2;
      continue;
    }
    if (query ? IsQuerySafe(c) : IsPathSafe(c)) {
      out.push_back(static_cast<char>(c));
      continue;
    }
    out.push_back('%');
    out.push_back(kHexDigits[c >> 4]);
    out.push_back(kHexDigits[c & 0x0F]);
  }
  return out;
}

bool HasDotDotSegment(std::string_view path) {
  std::size_t begin = 0;
  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::string_view segment =
        path.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
    if (segment == "..") {
      return true;
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return false;
}

ResolvedUrl Reject(UrlReject reject, std::string reason) {
  ResolvedUrl result;
  result.reject = reject;
  result.reason = std::move(reason);
  return result;
}

// Declared before its use in ResolveSameSiteUrl; defined with the other name helpers below.
bool IsImageFileName(std::string_view name);

// Parses the raw value and applies the same-site rules shared by downloads and artwork. The prefix
// and the file-name rules are the caller's, because they differ (an image is not a package).
ResolvedUrl ResolveSameSiteUrl(std::string_view raw, std::string_view required_prefix, bool image) {
  const std::string_view text = TrimAscii(raw);
  if (text.empty()) {
    return Reject(UrlReject::kEmpty, "empty URL");
  }
  if (text.starts_with("//")) {
    return Reject(UrlReject::kMalformed, "scheme-relative URL");
  }

  std::string synthesized;
  std::string_view absolute = text;
  if (text.starts_with('/')) {
    synthesized = std::string(kApiBase) + std::string(text);
    absolute = synthesized;
  }

  const std::optional<UrlParts> parts = ParseUrl(absolute);
  if (!parts) {
    return Reject(UrlReject::kMalformed, "not an absolute http(s) URL");
  }
  if (parts->scheme != "https") {
    return Reject(UrlReject::kNotHttps, "not an https URL");
  }
  if (parts->has_credentials) {
    return Reject(UrlReject::kCredentials, "credentials in the URL");
  }
  if (parts->host != kSiteHost) {
    return Reject(UrlReject::kOffSite, "off-site host '" + parts->host + "'");
  }
  if (parts->has_port && parts->port != 443) {
    return Reject(UrlReject::kBadPort, "a non-default port");
  }
  if (!ValidPercentEscapes(parts->path) || !ValidPercentEscapes(parts->query)) {
    return Reject(UrlReject::kMalformed, "a malformed percent escape");
  }

  const std::string lowered_path = ToLowerAscii(parts->path);
  if (lowered_path.find("%2f") != std::string::npos || lowered_path.find("%5c") != std::string::npos) {
    return Reject(UrlReject::kEncodedSeparator, "an encoded path separator");
  }

  const std::string decoded_path = PercentDecode(parts->path);
  if (HasDotDotSegment(decoded_path)) {
    return Reject(UrlReject::kUpwardPath, "a path that navigates upwards");
  }
  if (!decoded_path.starts_with(required_prefix)) {
    return Reject(UrlReject::kNotDirect, std::string("not under ") + std::string(required_prefix));
  }
  const std::string file_name = UrlFileName(decoded_path);
  if (file_name.empty() || file_name == "." || file_name == "..") {
    return Reject(UrlReject::kNoFileName, "no file name");
  }
  if (image && !IsImageFileName(file_name)) {
    return Reject(UrlReject::kNotAnImage, "not an image file name");
  }

  ResolvedUrl result;
  result.ok = true;
  result.url = "https://" + parts->host + EncodeUrlBytes(parts->path, false);
  if (!parts->query.empty()) {
    result.url += '?';
    result.url += EncodeUrlBytes(parts->query, true);
  }
  return result;
}

bool IsImageFileName(std::string_view name) {
  std::string lowered = ToLowerAscii(name);
  const std::size_t dot = lowered.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  const std::string_view ext = std::string_view(lowered).substr(dot + 1);
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp";
}

// --- JSON helpers ---------------------------------------------------------------------------------
//
// `status` is the site's own; a body that is not an object with a `data` object is a schema change,
// not an empty result, and is reported as an error so the UI can say "unavailable" instead of
// silently drawing an empty store.

using Json = nlohmann::json;

const Json* Member(const Json& object, const char* key) {
  if (!object.is_object()) {
    return nullptr;
  }
  const auto it = object.find(key);
  if (it == object.end() || it->is_null()) {
    return nullptr;
  }
  return &*it;
}

const Json* ObjectMember(const Json& object, const char* key) {
  const Json* value = Member(object, key);
  return value != nullptr && value->is_object() ? value : nullptr;
}

std::string AsText(const Json* value) {
  if (value == nullptr) {
    return {};
  }
  if (value->is_string()) {
    return value->get<std::string>();
  }
  if (value->is_number_integer()) {
    return std::to_string(value->get<std::int64_t>());
  }
  if (value->is_number_unsigned()) {
    return std::to_string(value->get<std::uint64_t>());
  }
  if (value->is_number_float()) {
    return std::to_string(value->get<double>());
  }
  if (value->is_boolean()) {
    return value->get<bool>() ? "1" : "0";
  }
  return {};
}

std::optional<std::int64_t> AsInteger(const Json* value) {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (value->is_number_integer()) {
    return value->get<std::int64_t>();
  }
  if (value->is_number_unsigned()) {
    return static_cast<std::int64_t>(value->get<std::uint64_t>());
  }
  if (value->is_number_float()) {
    return static_cast<std::int64_t>(value->get<double>());
  }
  if (value->is_boolean()) {
    return value->get<bool>() ? 1 : 0;
  }
  if (value->is_string()) {
    // Pagination arrives as "1"/"25" in the captured fixtures (§3).
    const std::string& text = value->get_ref<const std::string&>();
    if (text.empty()) {
      return std::nullopt;
    }
    std::int64_t number = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, number);
    if (parsed.ec != std::errc() || parsed.ptr != end) {
      return std::nullopt;
    }
    return number;
  }
  return std::nullopt;
}

std::optional<int> AsInt(const Json* value) {
  const std::optional<std::int64_t> number = AsInteger(value);
  if (!number || *number < std::numeric_limits<int>::min() ||
      *number > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return static_cast<int>(*number);
}

bool AsFlag(const Json* value) {
  const std::optional<std::int64_t> number = AsInteger(value);
  if (number) {
    return *number != 0;
  }
  if (value != nullptr && value->is_string()) {
    const std::string lowered = ToLowerAscii(value->get_ref<const std::string&>());
    return lowered == "true" || lowered == "yes";
  }
  return false;
}

std::string FirstText(const Json* primary, const char* primary_key, const Json* fallback,
                      const char* fallback_key) {
  std::string text = AsText(Member(*primary, primary_key));
  if (text.empty() && fallback != nullptr) {
    text = AsText(Member(*fallback, fallback_key));
  }
  return text;
}

std::optional<int> FirstInt(const Json* primary, const char* primary_key, const Json* fallback,
                            const char* fallback_key) {
  std::optional<int> value = AsInt(Member(*primary, primary_key));
  if (!value && fallback != nullptr) {
    value = AsInt(Member(*fallback, fallback_key));
  }
  return value;
}

SongFile ParseSong(const Json& entry) {
  SongFile song;
  const Json* file = ObjectMember(entry, "file");
  if (file == nullptr) {
    return song;
  }
  const Json* data = ObjectMember(entry, "data");

  song.file_id = AsText(Member(*file, "file_id"));
  song.file_name = AsText(Member(*file, "file_name"));
  song.gameformat = AsText(Member(*file, "gameformat"));
  song.gamesource = AsText(Member(*file, "gamesource"));

  song.artist = FirstText(file, "file_artist", data, "artist");
  song.title = FirstText(file, "file_title", data, "title");
  song.album = FirstText(file, "file_album", data, "album");
  song.genre = FirstText(file, "file_genre", data, "genre");
  song.year = FirstInt(file, "file_year", data, "year");
  song.album_track_number = FirstInt(file, "file_album_track_number", data, "album_track_number");
  song.song_length_seconds = FirstInt(file, "file_song_length", data, "song_length");
  song.size_bytes = AsInteger(Member(*file, "size"));
  song.downloads = FirstInt(file, "downloads", data, "downloads");

  song.download_url = AsText(Member(*file, "download_url"));
  song.download_page_url = AsText(Member(*file, "download_page_url"));
  song.album_art = FirstText(file, "album_art", data, "album_art");
  song.file_preview = AsText(Member(*file, "file_preview"));
  song.song_preview = data != nullptr ? AsText(Member(*data, "song_preview")) : std::string();

  for (std::size_t i = 0; i < kInstrumentCount; ++i) {
    const Instrument instrument = static_cast<Instrument>(i);
    const std::string token = "diff_" + std::string(InstrumentToken(instrument));
    const std::string rank_token = "rank_" + std::string(InstrumentToken(instrument));
    song.difficulty[i] = FirstInt(file, token.c_str(), data, token.c_str());
    song.rank[i] = FirstInt(file, rank_token.c_str(), data, rank_token.c_str());
  }

  song.update_date = AsText(Member(*file, "update_date"));
  song.withdrawn = AsFlag(Member(*file, "deleted")) || AsFlag(Member(*file, "retired")) ||
                   AsFlag(Member(*file, "record_hidden")) || AsFlag(Member(*file, "hidden"));
  return song;
}

// --- Text folding ---------------------------------------------------------------------------------

struct Decoded {
  char32_t code_point = 0;
  std::size_t length = 1;
};

Decoded DecodeUtf8(std::string_view text, std::size_t at) {
  const unsigned char lead = static_cast<unsigned char>(text[at]);
  if (lead < 0x80) {
    return {lead, 1};
  }
  std::size_t extra = 0;
  char32_t value = 0;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    value = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    value = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    value = lead & 0x07;
  } else {
    // An invalid lead byte is folded as Latin-1, so two copies of the same broken name agree.
    return {lead, 1};
  }
  if (at + extra >= text.size()) {
    return {lead, 1};
  }
  for (std::size_t i = 1; i <= extra; ++i) {
    const unsigned char next = static_cast<unsigned char>(text[at + i]);
    if ((next & 0xC0) != 0x80) {
      return {lead, 1};
    }
    value = (value << 6) | (next & 0x3F);
  }
  return {value, extra + 1};
}

void AppendUtf8(std::string& out, char32_t code_point) {
  if (code_point <= 0x7F) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

bool IsFoldSpace(char32_t code_point) {
  if (code_point == ' ' || code_point == '\t' || code_point == '\n' || code_point == '\r' ||
      code_point == '\f' || code_point == '\v') {
    return true;
  }
  return code_point == 0x00A0 || (code_point >= 0x2000 && code_point <= 0x200A) ||
         code_point == 0x202F || code_point == 0x205F || code_point == 0x3000;
}

// Simple case folding for the ranges a catalogue uses. Nothing here is locale-dependent, which is
// the point: the same name must fold identically in a worker on any machine (§3.2).
char32_t FoldCodePoint(char32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    return cp + 32;
  }
  if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) {  // Latin-1, except the multiplication sign
    return cp + 32;
  }
  if (cp >= 0x0100 && cp <= 0x0137) {  // even = uppercase, with the dotted I outside the pair
    if (cp == 0x0130) {
      return 'i';
    }
    return (cp % 2 == 0) ? cp + 1 : cp;
  }
  if (cp >= 0x0139 && cp <= 0x0148) {  // odd = uppercase
    return (cp % 2 == 1) ? cp + 1 : cp;
  }
  if (cp >= 0x014A && cp <= 0x0177) {  // even = uppercase
    return (cp % 2 == 0) ? cp + 1 : cp;
  }
  switch (cp) {
    case 0x0178:  // Y with diaeresis
      return 0x00FF;
    case 0x0179:
    case 0x017B:
    case 0x017D:
      return cp + 1;
    case 0x017F:  // long s
      return 's';
    case 0x03C2:  // final sigma
      return 0x03C3;
    default:
      break;
  }
  if (cp >= 0x0391 && cp <= 0x03AB && cp != 0x03A2) {  // Greek capital letters
    return cp + 32;
  }
  if (cp >= 0x0410 && cp <= 0x042F) {  // Cyrillic capitals
    return cp + 32;
  }
  if (cp >= 0x0400 && cp <= 0x040F) {
    return cp + 80;
  }
  return cp;
}

}  // namespace

// --- Instruments ----------------------------------------------------------------------------------

std::string_view InstrumentToken(Instrument instrument) {
  switch (instrument) {
    case Instrument::kGuitar:
      return "guitar";
    case Instrument::kBass:
      return "bass";
    case Instrument::kDrums:
      return "drums";
    case Instrument::kVocals:
      return "vocals";
    case Instrument::kKeys:
      return "keys";
    case Instrument::kCount:
      break;
  }
  return {};
}

std::optional<Instrument> InstrumentFromToken(std::string_view token) {
  for (std::size_t i = 0; i < kInstrumentCount; ++i) {
    const Instrument instrument = static_cast<Instrument>(i);
    if (token == InstrumentToken(instrument)) {
      return instrument;
    }
  }
  return std::nullopt;
}

bool InstrumentFilter::Allows(Instrument instrument) const {
  switch (instrument) {
    case Instrument::kGuitar:
      return guitar;
    case Instrument::kBass:
      return bass;
    case Instrument::kDrums:
      return drums;
    case Instrument::kVocals:
      return vocals;
    case Instrument::kKeys:
      return keys;
    case Instrument::kCount:
      break;
  }
  return false;
}

std::vector<Instrument> InstrumentFilter::Enabled() const {
  std::vector<Instrument> enabled;
  for (std::size_t i = 0; i < kInstrumentCount; ++i) {
    const Instrument instrument = static_cast<Instrument>(i);
    if (Allows(instrument)) {
      enabled.push_back(instrument);
    }
  }
  return enabled;
}

// --- Requests -------------------------------------------------------------------------------------

std::string_view QueryProblemText(QueryProblem problem) {
  switch (problem) {
    case QueryProblem::kNone:
      return "ok";
    case QueryProblem::kBadPage:
      return "the page number is out of range";
    case QueryProblem::kBadPageSize:
      return "the page size is out of range";
    case QueryProblem::kTextTooShort:
      return "the search text needs at least 3 characters";
    case QueryProblem::kTextTooLong:
      return "the search text is too long";
    case QueryProblem::kTextHasControlCharacters:
      return "the search text contains control characters";
  }
  return "unknown problem";
}

std::string FormEncode(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const unsigned char c : text) {
    if (IsUnreserved(c)) {
      out.push_back(static_cast<char>(c));
      continue;
    }
    // A space is %20 rather than '+' so the body reads the same as the encoded spelling the site's
    // own forms use; both decode identically on the server.
    out.push_back('%');
    out.push_back(kHexDigits[c >> 4]);
    out.push_back(kHexDigits[c & 0x0F]);
  }
  return out;
}

namespace {

int CountCodePoints(std::string_view text) {
  int count = 0;
  for (std::size_t i = 0; i < text.size();) {
    const Decoded decoded = DecodeUtf8(text, i);
    i += decoded.length;
    ++count;
  }
  return count;
}

bool HasControlCharacters(std::string_view text) {
  for (const unsigned char c : text) {
    if (c < 0x20 || c == 0x7F) {
      return true;
    }
  }
  return false;
}

QueryProblem ValidateQuery(const Query& query, bool search) {
  if (query.page < 1 || query.page > 1000) {
    return QueryProblem::kBadPage;
  }
  if (query.records < 1 || query.records > kMaxPageSize) {
    return QueryProblem::kBadPageSize;
  }
  if (!search) {
    return QueryProblem::kNone;
  }
  const std::string_view text = TrimAscii(query.text);
  if (HasControlCharacters(text)) {
    return QueryProblem::kTextHasControlCharacters;
  }
  const int points = CountCodePoints(text);
  if (points < kMinSearchChars) {
    return QueryProblem::kTextTooShort;
  }
  if (points > kMaxSearchChars) {
    return QueryProblem::kTextTooLong;
  }
  return QueryProblem::kNone;
}

std::optional<RequestSpec> BuildRequest(const Query& query, bool search, QueryProblem* problem) {
  const QueryProblem status = ValidateQuery(query, search);
  if (problem != nullptr) {
    *problem = status;
  }
  if (status != QueryProblem::kNone) {
    return std::nullopt;
  }
  RequestSpec spec;
  spec.url = std::string(kApiBase) + (search ? kSearchPath : kListPath);
  spec.body = BuildFormBody(query, search);
  spec.headers = {
      {"Content-Type", "application/x-www-form-urlencoded"},
      {"Accept", "application/json"},
      {"X-Requested-With", "XMLHttpRequest"},
  };
  return spec;
}

}  // namespace

std::string BuildFormBody(const Query& query, bool search) {
  std::string body;
  auto append = [&body](std::string_view key, std::string_view value) {
    if (!body.empty()) {
      body.push_back('&');
    }
    body.append(key);
    body.push_back('=');
    body += FormEncode(value);
  };

  for (const Instrument instrument : query.instruments.Enabled()) {
    // Only an enabled instrument is sent: "On = required, Off = no condition, all Off = no
    // instrument constraint" (§1.6/OQ10). Sending nothing is the last case.
    append("instrument%5B%5D", InstrumentToken(instrument));
  }
  append("sort%5B0%5D%5Bsort_by%5D", "update_date");
  append("sort%5B0%5D%5Bsort_order%5D", "DESC");
  append("data_type", "full");
  if (search) {
    append("text", TrimAscii(query.text));
  }
  append("page", std::to_string(query.page));
  append("records", std::to_string(query.records));
  return body;
}

std::optional<RequestSpec> BuildListRequest(const Query& query, QueryProblem* problem) {
  return BuildRequest(query, false, problem);
}

std::optional<RequestSpec> BuildSearchRequest(const Query& query, QueryProblem* problem) {
  return BuildRequest(query, true, problem);
}

// --- Listing -------------------------------------------------------------------------------------

bool SongFile::HasChart(Instrument instrument) const {
  const std::size_t index = static_cast<std::size_t>(instrument);
  if (index >= kInstrumentCount) {
    return false;
  }
  const std::optional<int>& tier = difficulty[index];
  return tier && *tier >= 1;
}

bool ListingPage::HasMorePages() const {
  if (returned <= 0) {
    return false;
  }
  const int total = total_filtered > 0 ? total_filtered : total_available;
  if (total <= 0) {
    return false;
  }
  return start + returned < total;
}

std::optional<ListingPage> ParseListing(std::string_view json, std::string* error) {
  const Json root = Json::parse(json, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    if (error != nullptr) {
      *error = "the response is not a JSON object";
    }
    return std::nullopt;
  }

  const std::string status = AsText(Member(root, "status"));
  if (status != "success") {
    if (error != nullptr) {
      const Json* details = ObjectMember(root, "error");
      std::string message = details != nullptr ? AsText(Member(*details, "message")) : std::string();
      if (message.empty()) {
        message = status.empty() ? "the response has no status" : "the API answered status '" + status + "'";
      }
      *error = message;
    }
    return std::nullopt;
  }

  const Json* data = ObjectMember(root, "data");
  if (data == nullptr) {
    if (error != nullptr) {
      *error = "the response has no data object";
    }
    return std::nullopt;
  }

  ListingPage page;
  if (const Json* records = ObjectMember(*data, "records")) {
    page.total_available = AsInt(Member(*records, "total_available")).value_or(0);
    page.total_filtered = AsInt(Member(*records, "total_filtered")).value_or(0);
    page.returned = AsInt(Member(*records, "returned")).value_or(0);
  }
  if (const Json* pagination = ObjectMember(*data, "pagination")) {
    page.page = AsInt(Member(*pagination, "page")).value_or(0);
    page.records_per_page = AsInt(Member(*pagination, "records")).value_or(0);
    page.start = AsInt(Member(*pagination, "start")).value_or(0);
  }

  const Json* songs = Member(*data, "songs");
  if (songs != nullptr) {
    if (!songs->is_array()) {
      if (error != nullptr) {
        *error = "data.songs is not an array";
      }
      return std::nullopt;
    }
    page.songs.reserve(songs->size());
    for (const Json& entry : *songs) {
      if (!entry.is_object()) {
        continue;
      }
      SongFile song = ParseSong(entry);
      if (song.file_id.empty() && song.title.empty()) {
        // An entry with neither an identity nor a title cannot be shown or matched; the plan's rule
        // is that a missing optional field must not crash the client (§3 "Parse these documented
        // variations deliberately"). It is dropped, not fatal.
        continue;
      }
      page.songs.push_back(std::move(song));
    }
    if (page.returned == 0) {
      page.returned = static_cast<int>(page.songs.size());
    }
  }
  return page;
}

// --- URL policy -----------------------------------------------------------------------------------

std::string_view UrlRejectText(UrlReject reject) {
  switch (reject) {
    case UrlReject::kNone:
      return "ok";
    case UrlReject::kEmpty:
      return "empty URL";
    case UrlReject::kMalformed:
      return "malformed URL";
    case UrlReject::kNotHttps:
      return "not HTTPS";
    case UrlReject::kOffSite:
      return "off-site host";
    case UrlReject::kCredentials:
      return "credentials in the URL";
    case UrlReject::kBadPort:
      return "a non-default port";
    case UrlReject::kEncodedSeparator:
      return "an encoded path separator";
    case UrlReject::kUpwardPath:
      return "a path that navigates upwards";
    case UrlReject::kNotDirect:
      return "not a direct file path";
    case UrlReject::kNoFileName:
      return "no file name";
    case UrlReject::kNotAnImage:
      return "not an image file name";
  }
  return "unknown reason";
}

std::optional<UrlParts> ParseUrl(std::string_view text) {
  if (text.empty() || HasControlOrBackslash(text)) {
    return std::nullopt;
  }
  const std::size_t scheme_end = text.find("://");
  if (scheme_end == std::string_view::npos || scheme_end == 0) {
    return std::nullopt;
  }
  UrlParts parts;
  parts.scheme = ToLowerAscii(text.substr(0, scheme_end));
  if (parts.scheme != "http" && parts.scheme != "https") {
    return std::nullopt;
  }

  std::string_view rest = text.substr(scheme_end + 3);
  const std::size_t authority_end = rest.find_first_of("/?#");
  const std::string_view authority =
      authority_end == std::string_view::npos ? rest : rest.substr(0, authority_end);
  if (authority.empty()) {
    return std::nullopt;
  }

  std::string_view host_port = authority;
  const std::size_t at = host_port.rfind('@');
  if (at != std::string_view::npos) {
    parts.has_credentials = true;
    host_port = host_port.substr(at + 1);
  }
  if (host_port.empty()) {
    return std::nullopt;
  }

  std::string_view host = host_port;
  std::string_view port_text;
  if (host_port.starts_with('[')) {
    const std::size_t close = host_port.find(']');
    if (close == std::string_view::npos) {
      return std::nullopt;
    }
    host = host_port.substr(0, close + 1);
    const std::string_view after = host_port.substr(close + 1);
    if (!after.empty()) {
      if (after.front() != ':') {
        return std::nullopt;
      }
      port_text = after.substr(1);
      parts.has_port = true;
    }
  } else {
    const std::size_t colon = host_port.rfind(':');
    if (colon != std::string_view::npos) {
      host = host_port.substr(0, colon);
      port_text = host_port.substr(colon + 1);
      parts.has_port = true;
    }
  }
  if (host.empty()) {
    return std::nullopt;
  }
  parts.host = ToLowerAscii(host);

  if (parts.has_port) {
    if (port_text.empty()) {
      return std::nullopt;
    }
    unsigned port = 0;
    for (const char c : port_text) {
      if (!IsAsciiDigit(c)) {
        return std::nullopt;
      }
      port = port * 10 + static_cast<unsigned>(c - '0');
      if (port > 65535) {
        return std::nullopt;
      }
    }
    if (port == 0) {
      return std::nullopt;
    }
    parts.port = port;
  }

  std::string_view tail = authority_end == std::string_view::npos ? std::string_view() : rest.substr(authority_end);
  const std::size_t fragment = tail.find('#');
  if (fragment != std::string_view::npos) {
    tail = tail.substr(0, fragment);
  }
  const std::size_t question = tail.find('?');
  if (question != std::string_view::npos) {
    parts.query = std::string(tail.substr(question + 1));
    tail = tail.substr(0, question);
  }
  if (!tail.empty() && tail.front() == '/') {
    parts.path = std::string(tail);
  }
  return parts;
}

std::string UrlFileName(std::string_view url_or_path) {
  std::string_view text = TrimAscii(url_or_path);
  const std::size_t fragment = text.find('#');
  if (fragment != std::string_view::npos) {
    text = text.substr(0, fragment);
  }
  const std::size_t question = text.find('?');
  if (question != std::string_view::npos) {
    text = text.substr(0, question);
  }
  const std::size_t slash = text.rfind('/');
  if (slash != std::string_view::npos) {
    text = text.substr(slash + 1);
  }
  return PercentDecode(text);
}

bool IsArchiveFileName(std::string_view name) {
  std::string lowered = ToLowerAscii(TrimAscii(name));
  // Windows strips trailing dots and spaces when it creates a file, so "x.zip." is the same file as
  // "x.zip"; normalise before the suffix test.
  while (!lowered.empty() && (lowered.back() == '.' || lowered.back() == ' ')) {
    lowered.pop_back();
  }
  const std::size_t dot = lowered.rfind('.');
  if (dot == std::string::npos || dot + 1 >= lowered.size()) {
    return false;
  }
  const std::string_view ext = std::string_view(lowered).substr(dot + 1);

  static constexpr std::string_view kArchiveExtensions[] = {
      "zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "z", "lz", "lzma", "cab",
  };
  for (const std::string_view archive : kArchiveExtensions) {
    if (ext == archive) {
      return true;
    }
  }
  // Multi-volume suffixes: .r00-.r99 (RAR), .z01-.z99 (ZIP), .001-.099 (generic split archives).
  if (ext.size() == 3 && IsAsciiDigit(ext[1]) && IsAsciiDigit(ext[2])) {
    if (ext[0] == 'r' || ext[0] == 'z' || ext[0] == '0') {
      return true;
    }
  }
  return false;
}

ResolvedUrl ResolveDownloadUrl(std::string_view raw) {
  return ResolveSameSiteUrl(raw, kDownloadPathPrefix, false);
}

ResolvedUrl ResolveAlbumArtUrl(std::string_view raw) {
  return ResolveSameSiteUrl(raw, kAlbumArtPathPrefix, true);
}

// --- Eligibility ----------------------------------------------------------------------------------

std::string_view OfferRejectText(OfferReject reject) {
  switch (reject) {
    case OfferReject::kNone:
      return "ok";
    case OfferReject::kWrongFormat:
      return "not an rb3xbox file";
    case OfferReject::kWithdrawn:
      return "withdrawn from the catalogue";
    case OfferReject::kMissingIdentity:
      return "no artist or title";
    case OfferReject::kNoDirectLink:
      return "no download link";
    case OfferReject::kOffSiteLink:
      return "off-site download link";
    case OfferReject::kNotDirectFile:
      return "not a direct file link";
    case OfferReject::kArchiveName:
      return "archive downloads are not supported";
  }
  return "unknown reason";
}

OfferDecision DecideOffer(const SongFile& song) {
  OfferDecision decision;
  if (song.gameformat != kTargetFormat) {
    decision.reject = OfferReject::kWrongFormat;
    decision.reason = std::string(OfferRejectText(decision.reject)) + " ('" + song.gameformat + "')";
    return decision;
  }
  if (song.withdrawn) {
    decision.reject = OfferReject::kWithdrawn;
    decision.reason = std::string(OfferRejectText(decision.reject));
    return decision;
  }
  if (TrimAscii(song.artist).empty() || TrimAscii(song.title).empty()) {
    decision.reject = OfferReject::kMissingIdentity;
    decision.reason = std::string(OfferRejectText(decision.reject));
    return decision;
  }
  if (TrimAscii(song.download_url).empty()) {
    decision.reject = OfferReject::kNoDirectLink;
    decision.reason = std::string(OfferRejectText(decision.reject));
    return decision;
  }

  const ResolvedUrl resolved = ResolveDownloadUrl(song.download_url);
  if (!resolved.ok) {
    decision.reject = resolved.reject == UrlReject::kOffSite ? OfferReject::kOffSiteLink
                                                            : OfferReject::kNotDirectFile;
    decision.reason = resolved.reason;
    return decision;
  }

  // Both names are checked: the upload's own file name and the URL's last segment. A server that
  // renamed one but not the other still cannot smuggle an archive past the metadata predicate.
  if (IsArchiveFileName(song.file_name) || IsArchiveFileName(UrlFileName(resolved.url))) {
    decision.reject = OfferReject::kArchiveName;
    decision.reason = std::string(OfferRejectText(decision.reject));
    return decision;
  }

  decision.eligible = true;
  decision.reject = OfferReject::kNone;
  decision.download_url = resolved.url;
  return decision;
}

bool PassesInstrumentFilter(const SongFile& song, const InstrumentFilter& filter) {
  for (const Instrument instrument : filter.Enabled()) {
    if (!song.HasChart(instrument)) {
      return false;
    }
  }
  return true;
}

// --- Transfer validation --------------------------------------------------------------------------

PrefixKind ClassifyPrefix(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return PrefixKind::kEmpty;
  }
  auto starts_with = [bytes](std::string_view magic) {
    if (bytes.size() < magic.size()) {
      return false;
    }
    return std::equal(magic.begin(), magic.end(), bytes.begin());
  };
  auto byte = [bytes](std::size_t index) { return bytes[index]; };

  if (starts_with("CON ")) {
    return PrefixKind::kStfsCon;
  }
  if (starts_with("LIVE")) {
    return PrefixKind::kStfsLive;
  }
  if (starts_with("PIRS")) {
    return PrefixKind::kStfsPirs;
  }
  if (bytes.size() >= 4 && byte(0) == 'P' && byte(1) == 'K' &&
      (byte(2) == 3 || byte(2) == 5 || byte(2) == 7) && byte(3) == 4) {
    return PrefixKind::kZip;
  }
  if (bytes.size() >= 6 && byte(0) == 'R' && byte(1) == 'a' && byte(2) == 'r' && byte(3) == '!' &&
      byte(4) == 0x1A && byte(5) == 0x07) {
    return PrefixKind::kRar;
  }
  if (bytes.size() >= 6 && byte(0) == 0x37 && byte(1) == 0x7A && byte(2) == 0xBC &&
      byte(3) == 0xAF && byte(4) == 0x27 && byte(5) == 0x1C) {
    return PrefixKind::kSevenZip;
  }
  if (bytes.size() >= 2 && byte(0) == 0x1F && byte(1) == 0x8B) {
    return PrefixKind::kGzip;
  }
  if (starts_with("BZh")) {
    return PrefixKind::kBzip2;
  }
  if (bytes.size() >= 6 && byte(0) == 0xFD && byte(1) == '7' && byte(2) == 'z' && byte(3) == 'X' &&
      byte(4) == 'Z' && byte(5) == 0x00) {
    return PrefixKind::kXz;
  }

  // HTML and JSON are the shapes an error/login page takes; both are refused before an install.
  std::size_t at = 0;
  if (bytes.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
    at = 3;
  }
  while (at < bytes.size() && (byte(at) == ' ' || byte(at) == '\t' || byte(at) == '\r' ||
                               byte(at) == '\n' || byte(at) == '\f' || byte(at) == '\v')) {
    ++at;
  }
  if (at < bytes.size() && byte(at) == '<') {
    return PrefixKind::kHtml;
  }
  if (at < bytes.size() && (byte(at) == '{' || byte(at) == '[')) {
    return PrefixKind::kJson;
  }
  return PrefixKind::kUnknown;
}

bool IsStfsPrefix(PrefixKind kind) {
  return kind == PrefixKind::kStfsCon || kind == PrefixKind::kStfsLive || kind == PrefixKind::kStfsPirs;
}

bool IsArchivePrefix(PrefixKind kind) {
  switch (kind) {
    case PrefixKind::kZip:
    case PrefixKind::kRar:
    case PrefixKind::kSevenZip:
    case PrefixKind::kGzip:
    case PrefixKind::kBzip2:
    case PrefixKind::kXz:
      return true;
    default:
      return false;
  }
}

LengthCheck CheckTransferLength(std::optional<std::int64_t> api_size,
                                std::optional<std::int64_t> content_length, std::uint64_t received) {
  LengthCheck check;
  if (api_size && *api_size < 0) {
    check.reason = "the API size is negative";
    return check;
  }
  if (content_length && *content_length < 0) {
    check.reason = "Content-Length is negative";
    return check;
  }
  if (received > kMaxDownloadBytes) {
    check.reason = "the transfer exceeds the " + std::to_string(kMaxDownloadBytes) + "-byte ceiling";
    return check;
  }
  const std::optional<std::int64_t> api_known = api_size && *api_size > 0 ? api_size : std::nullopt;
  const std::optional<std::int64_t> http_known =
      content_length && *content_length > 0 ? content_length : std::nullopt;
  if (api_known && http_known && *api_known != *http_known) {
    check.reason = "the API size (" + std::to_string(*api_known) + ") and Content-Length (" +
                   std::to_string(*http_known) + ") disagree";
    return check;
  }
  const std::optional<std::int64_t> expected = api_known ? api_known : http_known;
  if (expected && received != static_cast<std::uint64_t>(*expected)) {
    check.reason = "received " + std::to_string(received) + " bytes, expected " +
                   std::to_string(*expected);
    return check;
  }
  check.ok = true;
  check.reason.clear();
  return check;
}

// --- Matching -------------------------------------------------------------------------------------

std::string FoldForMatch(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  bool pending_space = false;
  bool wrote_any = false;
  for (std::size_t i = 0; i < utf8.size();) {
    const Decoded decoded = DecodeUtf8(utf8, i);
    i += decoded.length;
    if (IsFoldSpace(decoded.code_point)) {
      pending_space = wrote_any;
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    AppendUtf8(out, FoldCodePoint(decoded.code_point));
    wrote_any = true;
  }
  return out;
}

MatchKey MakeMatchKey(std::string_view artist, std::string_view title) {
  MatchKey key;
  key.artist = FoldForMatch(artist);
  key.title = FoldForMatch(title);
  return key;
}

bool SameSong(const MatchKey& lhs, const MatchKey& rhs) {
  return lhs.usable() && rhs.usable() && lhs.artist == rhs.artist && lhs.title == rhs.title;
}

}  // namespace rb_blitz::store
