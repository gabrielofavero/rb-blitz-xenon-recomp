// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_search.h. This file is the session: the field, the request it becomes, the answer it
// accepts, and the one-bit replies the authored key pages read.

#include "store/store_search.h"

#include <chrono>
#include <iterator>
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

// --- The session ------------------------------------------------------------------------------

std::mutex g_mutex;
SearchText g_text;
SearchState g_state = SearchState::kIdle;
std::vector<SearchOffer> g_offers;
std::uint64_t g_generation = 1;
bool g_open = false;
// A terminal answer the store has not listed yet: set when one lands, cleared when the serving
// policy hands the page over (`SearchMarkShown`) or when the store is left. It is the front menu's
// gate (`kSearchShowQuery`) and half of the caption strip's visibility rule.
bool g_show = false;
// P9-11's measurement switch: a boot with `store_search_seed` filled the field without opening the
// surface. The store screen's keyboard entry asks `kSearchSeedQuery` and submits the field instead
// of showing the keyboard while this is set; typing, submitting or cancelling retires it, so a
// player who starts a real search gets the real keyboard.
bool g_seed = false;
std::string g_failure;

struct Pending {
  bool active = false;
  std::uint64_t generation = 0;
  std::uint64_t settings_generation = 0;
  std::string text;
  InstrumentFilter filter;
  // P7's chunking state: the API page the next answer should be, how many pages this submit has
  // already read, and the per-action budget it may spend.
  int next_page = 1;
  int pages_done = 0;
  int page_budget = 1;
};
Pending g_pending;

// Plan P7's per-action page budget (installed from `store_catalogue_pages` by the transport).
int g_page_budget = 1;

// Plan P7's backoff: a failed answer names a wait, and a submit inside it is refused with the wait
// rather than sent. Cleared by a new submit, a cancel and a page that lands.
std::chrono::steady_clock::time_point g_retry_at{};

SubmitSink g_submit_sink;

// The guest surface delivers one press as **two** writes in the same frame (measured 2026-10-20 in
// a boot: one A press logged `search/key/a` twice at 17:19:36.328, and one C press twice at
// 17:19:52.462 - the key pages that produced those writes are retired, but the panel behaviour they
// measured is the store's own and still applies). Applying both would post two requests for one
// submit, so an identical command inside a few milliseconds is treated as the same press. The
// window is far below a human double-tap (a player pressing A twice for "ee" takes well over
// 100 ms).
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
  // The strip belongs to the search surface - the keyboard, plus the in-flight request that is
  // still going when the player has pressed the keyboard's Cancel. The *pending answer* (`g_show`)
  // is not a strip state: it is the Search row's own gate, and the strip would otherwise follow the
  // player out of the store (measured 2026-10-22: with `show` as a strip state, the count sat over
  // the main menu after Back).
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
    case SearchState::kSearching: {
      std::string text = "Searching for " + Quote(g_text.text()) + "...";
      // P7's chunking: a multi-page answer says how far it has got, so a slow catalogue reads as
      // progress rather than a hang.
      if (g_pending.active && g_pending.pages_done > 0) {
        text += " (" + std::to_string(g_pending.pages_done) +
                (g_pending.pages_done == 1 ? " page)" : " pages)");
      }
      return text;
    }
    case SearchState::kResults:
      return CountNoun(g_offers.size()) + Quote(g_text.text()) + ". " +
             (g_offers.size() == 1 ? kSearchListHintOne : kSearchListHint);
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

void SearchText::Clear() { text_.clear(); }

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
  // Only the surface closes. The field, the answer and the pending mark all stay: the Search row
  // is the way to the answer (it lists it once and then goes back to opening the keyboard), and
  // leaving the store is not a cancel - the request can still be running.
}

void SearchCancel() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  ++g_generation;
  g_pending = Pending{};
  g_open = false;
  g_show = false;
  g_seed = false;
  g_text.Clear();
  g_offers.clear();
  g_failure.clear();
  g_state = SearchState::kIdle;
  g_retry_at = {};
  g_has_last_press = false;
}

bool SearchSetText(std::string_view utf8) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_text.Set(utf8)) {
    return false;
  }
  InvalidateInFlightLocked();
  g_offers.clear();
  g_failure.clear();
  g_show = false;
  // A typed query is the player's, not the diagnostic's: the next Search press shows the keyboard.
  g_seed = false;
  g_state = SearchState::kEditing;
  g_open = true;
  return true;
}

bool SearchSeedField(std::string_view utf8) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_text.Set(utf8)) {
    return false;
  }
  InvalidateInFlightLocked();
  g_offers.clear();
  g_failure.clear();
  g_show = false;
  g_state = SearchState::kEditing;
  // Deliberately not `g_open = true`: the field is seeded, the surface is not entered.
  g_seed = true;
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
  // Plan P7's backoff: a failure the worker reported named a wait (Retry-After, 5xx, or the
  // transport's own default). A submit inside it is refused with the wait instead of sent - and
  // the previous answer, if any, stays on screen. The wait is cleared by a page that lands, by the
  // next accepted submit, and by a cancel (the player's own way out).
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (now < g_retry_at) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(g_retry_at - now).count() + 1;
    out.why = "the catalogue is busy; try again in " + std::to_string(remaining) + " s";
    // A refusal while an answer is on screen is not a failure of that answer: the list the Search
    // row opens is still the last good one, so the state is left alone and only the reason travels
    // (the guest hook logs it). Otherwise the busy sentence becomes the strip's own.
    if (g_state != SearchState::kResults && g_state != SearchState::kNoResults) {
      g_failure = out.why;
      g_state = SearchState::kFailed;
    }
    out.state = g_state;
    return out;
  }
  ++g_generation;
  g_pending = Pending{};
  g_pending.active = true;
  g_pending.generation = g_generation;
  g_pending.settings_generation = SettingsGeneration();
  g_pending.text = g_text.text();
  g_pending.filter = settings->instruments;
  g_pending.next_page = 1;
  g_pending.pages_done = 0;
  g_pending.page_budget = g_page_budget > 0 ? g_page_budget : 1;
  g_offers.clear();
  g_failure.clear();
  g_show = false;
  g_retry_at = {};
  g_state = SearchState::kSearching;
  g_open = true;
  // P9-11: a submit consumes the diagnostic seed - the next Search press shows the keyboard again.
  g_seed = false;
  out.started = true;
  out.generation = g_generation;
  out.request = *spec;
  out.state = g_state;
  return out;
}

void SetSearchPageBudget(int pages) { g_page_budget = pages > 0 ? pages : 1; }

int SearchPageBudget() { return g_page_budget; }

SearchPageAnswer SearchAcceptPage(std::uint64_t generation, const ListingPage& page) {
  SearchPageAnswer out;
  const std::lock_guard<std::mutex> guard(g_mutex);
  if (!g_pending.active || generation != g_pending.generation ||
      g_state != SearchState::kSearching) {
    return out;
  }
  // A settings commit since the request went out changes the answer's meaning (the filter is part
  // of the query), and the master being off means the answer must not be shown at all. Both drop
  // it the same way: the generation moves on so a duplicate cannot take hold either.
  if (SettingsGeneration() != g_pending.settings_generation) {
    const bool enabled = Settings()->enabled;
    g_pending = Pending{};
    ++g_generation;
    g_offers.clear();
    g_state = enabled ? SearchState::kEditing : SearchState::kDisabled;
    return out;
  }
  if (!Settings()->enabled) {
    g_pending = Pending{};
    ++g_generation;
    g_offers.clear();
    g_state = SearchState::kDisabled;
    return out;
  }
  out.accepted = true;
  ++g_pending.pages_done;
  // The pages accumulate in the API's order, through the same filter Recently Added uses. A page
  // the filter emptied is a valid step, not the end of the walk (P7).
  std::vector<SearchOffer> shaped = ShapeOffers(page, g_pending.filter);
  g_offers.insert(g_offers.end(), std::make_move_iterator(shaped.begin()),
                  std::make_move_iterator(shaped.end()));
  out.matches = g_offers.size();
  // The cursor is the server's, never the number of offers kept: an underfilled or filtered-empty
  // page continues.
  const int next = NextPageAfter(page, g_pending.pages_done, g_pending.page_budget);
  if (next != 0) {
    g_pending.next_page = next;
    out.next_page = next;
    g_retry_at = {};  // a page landed: an earlier failure's wait no longer applies
    return out;       // still `kSearching`: the worker asks for `next_page`
  }
  g_pending = Pending{};
  g_state = g_offers.empty() ? SearchState::kNoResults : SearchState::kResults;
  g_show = true;
  return out;
}

bool SearchAccept(std::uint64_t generation, const ListingPage& page) {
  return SearchAcceptPage(generation, page).accepted;
}

bool SearchFailAfter(std::uint64_t generation, std::string reason,
                     std::chrono::seconds retry_after) {
  const std::lock_guard<std::mutex> guard(g_mutex);
  if (!g_pending.active || generation != g_pending.generation ||
      g_state != SearchState::kSearching) {
    return false;
  }
  g_pending = Pending{};
  g_retry_at = std::chrono::steady_clock::now() + retry_after;
  if (!g_offers.empty()) {
    // Pages already read are a usable answer: the failure ends the walk, not the answer. The
    // reason still reaches the log (the worker logs it); the strip shows the count, which is what
    // the player can act on (Back, then Search).
    g_failure.clear();
    g_state = SearchState::kResults;
    g_show = true;
    return true;
  }
  g_failure = std::move(reason);
  g_state = SearchState::kFailed;
  // A failure is presented the same way: the Search row opens the (empty) page, which is what
  // returns the store to its menu, and the strip carries the reason until the store is left.
  g_show = true;
  return true;
}

bool SearchFail(std::uint64_t generation, std::string reason) {
  return SearchFailAfter(generation, std::move(reason),
                         std::chrono::seconds(kBackoffNetworkSeconds));
}

std::optional<RequestSpec> SearchBuildPageRequest(std::uint64_t generation, int page,
                                                  std::string* why) {
  const std::lock_guard<std::mutex> guard(g_mutex);
  auto refuse = [why](std::string_view reason) {
    if (why != nullptr) *why = std::string(reason);
    return std::optional<RequestSpec>{};
  };
  if (!g_pending.active || generation != g_pending.generation ||
      g_state != SearchState::kSearching) {
    return refuse("the search job is no longer current");
  }
  if (!Settings()->enabled) {
    return refuse("RhythmVerse is switched off");
  }
  if (SettingsGeneration() != g_pending.settings_generation) {
    return refuse("the settings changed during the search");
  }
  if (page == 0) {
    return refuse("the page number is not a page");
  }
  Query query;
  query.instruments = g_pending.filter;
  query.page = page;
  query.records = kDefaultPageSize;
  query.text = g_pending.text;
  QueryProblem problem = QueryProblem::kNone;
  std::optional<RequestSpec> spec = BuildSearchRequest(query, &problem);
  if (!spec) {
    return refuse(QueryProblemText(problem));
  }
  return spec;
}

bool SearchPageWanted(std::uint64_t generation) {
  const std::lock_guard<std::mutex> guard(g_mutex);
  return g_pending.active && generation == g_pending.generation &&
         g_state == SearchState::kSearching && Settings()->enabled &&
         SettingsGeneration() == g_pending.settings_generation;
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
  if (rest == "show") {
    *answer = g_show;
    return true;
  }
  if (rest == "seed") {
    *answer = g_seed;
    return true;
  }
  return false;
}

std::vector<SearchOffer> SearchOffers() {
  const std::lock_guard<std::mutex> guard(g_mutex);
  return g_offers;
}

void SearchMarkShown() {
  const std::lock_guard<std::mutex> guard(g_mutex);
  g_show = false;
}

bool SearchAnswerPending() {
  const std::lock_guard<std::mutex> guard(g_mutex);
  return g_show;
}

bool SearchInFlight() {
  const std::lock_guard<std::mutex> guard(g_mutex);
  return g_state == SearchState::kSearching && g_offers.empty();
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
  if (action == "leave") {
    *op = StoreOp::kSearchLeave;
    return StoreParse::kAccepted;
  }
  constexpr std::string_view kText = "text/";
  if (action.starts_with(kText)) {
    // P9-11: the title's keyboard hands over one raw UTF-8 string, and `SearchText::Set` is the
    // only gate it needs (trim, control characters, UTF-8, length). The path is not a token list
    // any more, so spaces, apostrophes and accented bytes are the query itself.
    const std::string_view query = action.substr(kText.size());
    if (query.empty()) {
      return refuse("the text body is empty");
    }
    if (query.size() > 4u * kMaxSearchChars) {
      return refuse("the text body is too long");
    }
    *op = StoreOp::kSearchText;
    *text = std::string(query);
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
    case StoreOp::kSearchLeave:
      // Deliberately not de-duplicated: leaving twice is the same as leaving once.
      SearchClose();
      return true;
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
