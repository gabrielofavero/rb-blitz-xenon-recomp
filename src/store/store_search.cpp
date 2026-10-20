// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_search.h. This file is the session: the field, the request it becomes, the answer it
// accepts, and the one-bit replies the authored key pages read.

#include "store/store_search.h"

#include <cctype>
#include <chrono>
#include <mutex>
#include <utility>

#include "store/store_settings.h"

namespace rb_blitz::store {
namespace {

// --- UTF-8 ------------------------------------------------------------------------------------
//
// The field is UTF-8 because the API's own field is: a query is a name, and a name with an accent
// must survive being typed. Three helpers, all of which refuse instead of guessing.

// One code point at `pos`, or 0 with `length` 0 for an invalid sequence. Only the lengths UTF-8
// defines (1..4) and the shortest form for each are accepted.
std::uint32_t DecodeCodePoint(std::string_view text, std::size_t pos, std::size_t* length) {
  *length = 0;
  if (pos >= text.size()) {
    return 0;
  }
  const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
  const std::uint8_t first = byte(pos);
  if (first < 0x80) {
    *length = 1;
    return first;
  }
  std::size_t count = 0;
  std::uint32_t value = 0;
  if ((first & 0xE0) == 0xC0) {
    count = 2;
    value = first & 0x1F;
  } else if ((first & 0xF0) == 0xE0) {
    count = 3;
    value = first & 0x0F;
  } else if ((first & 0xF8) == 0xF0) {
    count = 4;
    value = first & 0x07;
  } else {
    return 0;
  }
  if (pos + count > text.size()) {
    return 0;
  }
  for (std::size_t i = 1; i < count; ++i) {
    const std::uint8_t next = byte(pos + i);
    if ((next & 0xC0) != 0x80) {
      return 0;
    }
    value = (value << 6) | (next & 0x3F);
  }
  // The shortest form, and the two ranges UTF-8 excludes (surrogates, above U+10FFFF).
  const bool overlong = (count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
                        (count == 4 && value < 0x10000);
  if (overlong || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    return 0;
  }
  *length = count;
  return value;
}

bool IsValidUtf8(std::string_view text) {
  for (std::size_t pos = 0; pos < text.size();) {
    std::size_t length = 0;
    if (DecodeCodePoint(text, pos, &length) == 0 && length == 0) {
      return false;
    }
    pos += length;
  }
  return true;
}

bool IsSpace(const std::uint32_t code_point) {
  return code_point == ' ' || code_point == '\t' || code_point == '\r' || code_point == '\n';
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && static_cast<std::uint8_t>(text.front()) < 0x80 &&
         IsSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && static_cast<std::uint8_t>(text.back()) < 0x80 &&
         IsSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

bool HasControlCharacter(std::string_view text) {
  for (std::size_t pos = 0; pos < text.size();) {
    std::size_t length = 0;
    const std::uint32_t code_point = DecodeCodePoint(text, pos, &length);
    if (length == 0) {
      return true;
    }
    if (code_point < 0x20 || code_point == 0x7F) {
      return true;
    }
    pos += length;
  }
  return false;
}

// --- Key tokens -------------------------------------------------------------------------------
//
// The spelling keys the authored pages send, and the characters they stand for. A letter or digit
// is its own token, so the pages carry one row per character and this table stays four lines.

struct SpellingKey {
  std::string_view token;
  std::string_view text;
};

constexpr SpellingKey kSpellingKeys[] = {
    {kKeySpace, " "}, {kKeyApostrophe, "'"}, {kKeyDash, "-"}, {kKeyPeriod, "."},
};

bool IsLetterOrDigitToken(std::string_view token) {
  return token.size() == 1 && ((token[0] >= 'a' && token[0] <= 'z') ||
                               (token[0] >= '0' && token[0] <= '9') ||
                               (token[0] >= 'A' && token[0] <= 'Z'));
}

bool IsKnownKeyToken(std::string_view token) {
  if (IsLetterOrDigitToken(token)) {
    return true;
  }
  for (const SpellingKey& key : kSpellingKeys) {
    if (token == key.token) {
      return true;
    }
  }
  return token == kKeyBackspace || token == kKeyClear;
}

// --- The session ------------------------------------------------------------------------------

std::mutex g_mutex;
SearchText g_text;
SearchState g_state = SearchState::kIdle;
std::vector<SearchOffer> g_offers;
std::uint64_t g_generation = 1;
bool g_open = false;
std::string g_failure;

struct Pending {
  bool active = false;
  std::uint64_t generation = 0;
  std::uint64_t settings_generation = 0;
  std::string text;
  InstrumentFilter filter;
};
Pending g_pending;

SubmitSink g_submit_sink;

// The guest surface delivers one press as **two** writes in the same frame (measured 2026-10-20 in
// a boot: one A press logged `search/key/a` twice at 17:19:36.328, and one C press twice at
// 17:19:52.462). Applying both would append the character twice, delete two characters for one
// Backspace and post two requests for one Done, so an identical command inside a few milliseconds
// is treated as the same press. The window is far below a human double-tap (a player pressing A
// twice for "ee" takes well over 100 ms).
constexpr auto kDuplicatePressWindow = std::chrono::milliseconds(50);
bool g_has_last_press = false;
StoreOp g_last_press_op = StoreOp::kNone;
std::string g_last_press_token;
std::chrono::steady_clock::time_point g_last_press_time;

bool IsDuplicatePress(const StoreOp op, const std::string_view token) {
  const auto now = std::chrono::steady_clock::now();
  const bool same = g_has_last_press && op == g_last_press_op && token == g_last_press_token &&
                    now - g_last_press_time < kDuplicatePressWindow;
  g_has_last_press = true;
  g_last_press_op = op;
  g_last_press_token = std::string(token);
  g_last_press_time = now;
  return same;
}

std::string Quote(const std::string& text) { return "\"" + text + "\""; }

std::string CountNoun(const std::size_t count) {
  return std::to_string(count) + (count == 1 ? " match for " : " matches for ");
}

// Any edit of the field makes an in-flight answer stale: it was computed for the previous query.
void InvalidateInFlightLocked() {
  if (g_state == SearchState::kSearching) {
    g_pending = Pending{};
    ++g_generation;
  }
}

std::string StatusTextLocked() {
  // The strip belongs to the search surface. It is also drawn while a request is in flight after
  // the player left the key pages (Done leaves, the answer still arrives), and not otherwise: a
  // finished answer must not sit over every other page's caption forever.
  if (!g_open && g_state != SearchState::kSearching) {
    return {};
  }
  switch (g_state) {
    case SearchState::kIdle:
      return {};
    case SearchState::kEditing:
      return g_text.text().empty() ? std::string(kSearchGuidance)
                                   : "Search: " + g_text.text();
    case SearchState::kTooShort:
      return kSearchTooShort;
    case SearchState::kSearching:
      return "Searching for " + Quote(g_text.text()) + "...";
    case SearchState::kResults:
      return CountNoun(g_offers.size()) + Quote(g_text.text()) + ". Press Done to open the store.";
    case SearchState::kNoResults:
      return "No matches for " + Quote(g_text.text()) + ". " + kSearchRetryHint;
    case SearchState::kFailed:
      return g_failure.empty() ? std::string("The search failed. Try again.")
                               : "The search failed: " + g_failure + ". Try again.";
    case SearchState::kDisabled:
      return kSearchDisabled;
  }
  return {};
}

}  // namespace

std::size_t CodePointCount(std::string_view utf8) {
  std::size_t count = 0;
  for (std::size_t pos = 0; pos < utf8.size();) {
    std::size_t length = 0;
    if (DecodeCodePoint(utf8, pos, &length) == 0 && length == 0) {
      // An invalid byte counts as one unit, so a broken field is bounded rather than free.
      ++pos;
    } else {
      pos += length;
    }
    ++count;
  }
  return count;
}

// --- SearchText -------------------------------------------------------------------------------

SearchText::Edit SearchText::AppendKeyToken(std::string_view token) {
  if (token == kKeyBackspace || token == kKeyClear || token == kKeyDone || token == kKeyCancel) {
    return Edit::kRejected;
  }
  if (length() + 1 > static_cast<std::size_t>(kMaxSearchChars)) {
    return Edit::kFull;
  }
  if (IsLetterOrDigitToken(token)) {
    // The pages send the character itself; upper case is folded, because the API's matching is
    // not case-sensitive and `A` and `a` on two pages are the same letter to the catalogue.
    text_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(token[0]))));
    return Edit::kOk;
  }
  const std::string_view piece = KeyTokenText(token);
  if (piece.empty()) {
    return Edit::kRejected;
  }
  text_.append(piece);
  return Edit::kOk;
}

bool SearchText::Set(std::string_view utf8) {
  const std::string_view trimmed = Trim(utf8);
  if (!IsValidUtf8(trimmed) || HasControlCharacter(trimmed)) {
    return false;
  }
  if (CodePointCount(trimmed) > static_cast<std::size_t>(kMaxSearchChars)) {
    return false;
  }
  text_.assign(trimmed);
  return true;
}

bool SearchText::Backspace() {
  if (text_.empty()) {
    return false;
  }
  std::size_t pos = text_.size() - 1;
  while (pos > 0 && (static_cast<std::uint8_t>(text_[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  text_.erase(pos);
  return true;
}

void SearchText::Clear() { text_.clear(); }

std::string_view KeyTokenText(std::string_view token) {
  for (const SpellingKey& key : kSpellingKeys) {
    if (token == key.token) {
      return key.text;
    }
  }
  return {};
}

// --- The session's commands -------------------------------------------------------------------

void SearchOpen() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_open = true;
  const SettingsSnapshot settings = Settings();
  if (!settings->enabled) {
    g_state = SearchState::kDisabled;
    return;
  }
  // Re-entering shows the field that was being typed; an answer already on screen stays until a
  // new submit replaces it.
  if (g_state == SearchState::kIdle || g_state == SearchState::kDisabled ||
      g_state == SearchState::kFailed) {
    g_state = SearchState::kEditing;
  }
}

void SearchClose() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_open = false;
  // Leaving the pages is **not** a cancel: Done leaves while its request is still in flight, and
  // dropping that would make every search impossible to complete.
}

void SearchCancel() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  ++g_generation;
  g_pending = Pending{};
  g_open = false;
  g_text.Clear();
  g_offers.clear();
  g_failure.clear();
  g_state = SearchState::kIdle;
  g_has_last_press = false;
}

bool SearchApplyKey(std::string_view token) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!IsKnownKeyToken(token)) {
    return false;
  }
  InvalidateInFlightLocked();
  if (token == kKeyBackspace) {
    g_text.Backspace();
  } else if (token == kKeyClear) {
    g_text.Clear();
  } else {
    g_text.AppendKeyToken(token);
  }
  g_offers.clear();
  g_failure.clear();
  g_state = SearchState::kEditing;
  g_open = true;
  return true;
}

bool SearchSetText(std::string_view utf8) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_text.Set(utf8)) {
    return false;
  }
  InvalidateInFlightLocked();
  g_offers.clear();
  g_failure.clear();
  g_state = SearchState::kEditing;
  g_open = true;
  return true;
}

SearchSubmit SearchSubmitQuery() {
  const std::lock_guard<std::mutex> guard(g_mutex);
  SearchSubmit out;
  const SettingsSnapshot settings = Settings();
  if (!settings->enabled) {
    g_state = SearchState::kDisabled;
    out.state = g_state;
    out.why = kSearchDisabled;
    return out;
  }
  Query query;
  query.instruments = settings->instruments;
  query.page = 1;
  query.records = kDefaultPageSize;
  query.text = g_text.text();
  QueryProblem problem = QueryProblem::kNone;
  const std::optional<RequestSpec> spec = BuildSearchRequest(query, &problem);
  if (!spec) {
    if (problem == QueryProblem::kTextTooShort) {
      g_state = SearchState::kTooShort;
    } else {
      g_state = SearchState::kFailed;
      g_failure = std::string(QueryProblemText(problem));
    }
    out.state = g_state;
    out.why = g_state == SearchState::kTooShort ? std::string(kSearchTooShort)
                                                : std::string(QueryProblemText(problem));
    return out;
  }
  ++g_generation;
  g_pending = Pending{};
  g_pending.active = true;
  g_pending.generation = g_generation;
  g_pending.settings_generation = SettingsGeneration();
  g_pending.text = g_text.text();
  g_pending.filter = settings->instruments;
  g_offers.clear();
  g_failure.clear();
  g_state = SearchState::kSearching;
  g_open = true;
  out.started = true;
  out.generation = g_generation;
  out.request = *spec;
  out.state = g_state;
  return out;
}

bool SearchAccept(std::uint64_t generation, const ListingPage& page) {
  const std::lock_guard<std::mutex> guard(g_mutex);
  if (!g_pending.active || generation != g_pending.generation || g_state != SearchState::kSearching) {
    return false;
  }
  // A settings commit since the request went out changes the answer's meaning (the filter is part
  // of the query), and the master being off means the answer must not be shown at all. Both drop
  // it the same way: the generation moves on so a duplicate cannot take hold either.
  if (SettingsGeneration() != g_pending.settings_generation) {
    const bool enabled = Settings()->enabled;
    g_pending = Pending{};
    ++g_generation;
    g_state = enabled ? SearchState::kEditing : SearchState::kDisabled;
    return false;
  }
  if (!Settings()->enabled) {
    g_pending = Pending{};
    ++g_generation;
    g_state = SearchState::kDisabled;
    return false;
  }
  std::vector<SearchOffer> offers = ShapeOffers(page, g_pending.filter);
  g_pending = Pending{};
  g_offers = std::move(offers);
  g_state = g_offers.empty() ? SearchState::kNoResults : SearchState::kResults;
  return true;
}

bool SearchFail(std::uint64_t generation, std::string reason) {
  const std::lock_guard<std::mutex> guard(g_mutex);
  if (!g_pending.active || generation != g_pending.generation || g_state != SearchState::kSearching) {
    return false;
  }
  g_pending = Pending{};
  g_failure = std::move(reason);
  g_state = SearchState::kFailed;
  return true;
}

// --- Queries and status -----------------------------------------------------------------------

std::string_view SearchStateToken(SearchState state) {
  switch (state) {
    case SearchState::kIdle: return "idle";
    case SearchState::kEditing: return "editing";
    case SearchState::kTooShort: return "too-short";
    case SearchState::kSearching: return "searching";
    case SearchState::kResults: return "results";
    case SearchState::kNoResults: return "empty";
    case SearchState::kFailed: return "failed";
    case SearchState::kDisabled: return "disabled";
  }
  return "idle";
}

bool AnswerSearchQuery(std::string_view path, bool* answer) {
  constexpr std::string_view kPrefix = "rbbz-store/search/";
  if (answer == nullptr || !path.starts_with(kPrefix)) {
    return false;
  }
  const std::string_view rest = path.substr(kPrefix.size());
  const std::lock_guard<std::mutex> guard(g_mutex);
  if (rest == "open") {
    *answer = g_open;
    return true;
  }
  if (rest == "ready") {
    *answer = g_text.ready();
    return true;
  }
  constexpr std::string_view kState = "state/";
  if (rest.starts_with(kState)) {
    *answer = SearchStateToken(g_state) == rest.substr(kState.size());
    return true;
  }
  constexpr std::string_view kCount = "count/at-least/";
  if (rest.starts_with(kCount)) {
    const std::string_view digits = rest.substr(kCount.size());
    if (digits.empty() || digits.size() > 2) {
      return false;
    }
    int wanted = 0;
    for (const char c : digits) {
      if (c < '0' || c > '9') {
        return false;
      }
      wanted = wanted * 10 + (c - '0');
    }
    *answer = static_cast<int>(g_offers.size()) >= wanted;
    return true;
  }
  return false;
}

SearchSnapshot SearchCurrent() {
  const std::lock_guard<std::mutex> guard(g_mutex);
  SearchSnapshot out;
  out.state = g_state;
  out.text = g_text.text();
  out.offers = g_offers;
  out.generation = g_generation;
  out.status = StatusTextLocked();
  return out;
}

std::string SearchStatusText() {
  // The overlay draws this every frame, and the worker writes the state, so the caption is read
  // under the same lock every command takes.
  const std::lock_guard<std::mutex> guard(g_mutex);
  return StatusTextLocked();
}

void SetSubmitSink(SubmitSink sink) {
  const std::lock_guard<std::mutex> guard(g_mutex);
  g_submit_sink = std::move(sink);
}

// --- The write channel ------------------------------------------------------------------------

StoreParse ParseStoreWrite(std::string_view path, StoreOp* op, std::string* token, std::string* text,
                           std::string* why) {
  auto refuse = [why](std::string_view reason) {
    if (why != nullptr) *why = std::string(reason);
    return StoreParse::kRefused;
  };
  if (!path.starts_with(kStoreNamespace)) {
    return StoreParse::kNotOurs;
  }
  constexpr std::string_view kSearch = "search/";
  const std::string_view rest = path.substr(sizeof(kStoreNamespace) - 1);
  if (!rest.starts_with(kSearch)) {
    // P5's job ops live under the same prefix; they are not this parser's to judge.
    return StoreParse::kNotOurs;
  }
  const std::string_view action = rest.substr(kSearch.size());
  if (action == "open") {
    *op = StoreOp::kSearchOpen;
    return StoreParse::kAccepted;
  }
  if (action == "done") {
    *op = StoreOp::kSearchDone;
    return StoreParse::kAccepted;
  }
  if (action == "cancel") {
    *op = StoreOp::kSearchCancel;
    return StoreParse::kAccepted;
  }
  constexpr std::string_view kKey = "key/";
  if (action.starts_with(kKey)) {
    const std::string_view name = action.substr(kKey.size());
    if (!IsKnownKeyToken(name)) {
      return refuse("unknown key token");
    }
    *op = StoreOp::kSearchKey;
    *token = std::string(name);
    return StoreParse::kAccepted;
  }
  constexpr std::string_view kText = "text/";
  if (action.starts_with(kText)) {
    const std::string_view hex = action.substr(kText.size());
    if (hex.empty() || hex.size() % 2 != 0 || hex.size() > 4u * kMaxSearchChars) {
      return refuse("the text body is not whole-byte hex");
    }
    std::string bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
      const auto nibble = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = nibble(hex[i]);
      const int lo = nibble(hex[i + 1]);
      if (hi < 0 || lo < 0) {
        return refuse("the text body is not whole-byte hex");
      }
      bytes.push_back(static_cast<char>((hi << 4) | lo));
    }
    *op = StoreOp::kSearchText;
    *text = std::move(bytes);
    return StoreParse::kAccepted;
  }
  return refuse("unknown search action");
}

bool ApplyStoreWrite(StoreOp op, std::string_view token, std::string_view text, std::string* why) {
  auto refuse = [why](std::string_view reason) {
    if (why != nullptr) *why = std::string(reason);
    return false;
  };
  switch (op) {
    case StoreOp::kSearchOpen:
      SearchOpen();
      return true;
    case StoreOp::kSearchCancel:
      if (IsDuplicatePress(op, {})) {
        return true;
      }
      SearchCancel();
      return true;
    case StoreOp::kSearchKey:
      if (IsDuplicatePress(op, token)) {
        return true;
      }
      return SearchApplyKey(token) ? true : refuse("unknown key token");
    case StoreOp::kSearchText:
      return SearchSetText(text) ? true : refuse("the query is not valid UTF-8 or is too long");
    case StoreOp::kSearchDone: {
      if (IsDuplicatePress(op, {})) {
        return true;
      }
      const SearchSubmit submit = SearchSubmitQuery();
      if (!submit.started) {
        return refuse(submit.why);
      }
      SubmitSink sink;
      {
        const std::lock_guard<std::mutex> guard(g_mutex);
        sink = g_submit_sink;
      }
      if (!sink) {
        SearchFail(submit.generation, "no search transport is installed");
        return refuse("no search transport is installed");
      }
      sink(submit.generation, submit.request);
      return true;
    }
    case StoreOp::kNone:
      break;
  }
  return refuse("no operation");
}

bool HandleStoreSearchWrite(std::string_view path, std::string* why) {
  StoreOp op = StoreOp::kNone;
  std::string token;
  std::string text;
  std::string note;
  const StoreParse parsed = ParseStoreWrite(path, &op, &token, &text, &note);
  if (parsed == StoreParse::kNotOurs) {
    return false;
  }
  // A name in our namespace can never be a content file, so a malformed one is answered (the
  // guest gets its "write refused") rather than forwarded - the settings channel's rule.
  if (parsed == StoreParse::kRefused) {
    if (why != nullptr) *why = "refused: " + note;
    return true;
  }
  if (!ApplyStoreWrite(op, token, text, &note) && why != nullptr) {
    *why = "answered but not applied: " + note;
  }
  return true;
}

// --- Offer shaping ----------------------------------------------------------------------------

std::vector<SearchOffer> ShapeOffers(const ListingPage& page, const InstrumentFilter& filter) {
  std::vector<SearchOffer> out;
  out.reserve(page.songs.size());
  for (const SongFile& song : page.songs) {
    if (!PassesInstrumentFilter(song, filter)) {
      continue;
    }
    const OfferDecision decision = DecideOffer(song);
    if (!decision.eligible) {
      continue;
    }
    SearchOffer offer;
    offer.song = song;
    offer.download_url = decision.download_url;
    out.push_back(std::move(offer));
  }
  return out;
}

}  // namespace rb_blitz::store
