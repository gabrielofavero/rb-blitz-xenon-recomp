// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4 of the RhythmVerse store plan (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §1.5, §5/P4): the search's *data* half - the typed query, its transport across the guest
// bridge, the request it becomes, and the answer it accepts.
//
// Three measured facts decide the shape of this file (all recorded in the plan):
//
//   * **there is no retail keyboard to borrow.** All 131 compiled DTA records of the main ark were
//     scanned (2026-10-20, `out/_swap/scan_dtb_tokens.py`): the only `show_keyboard` in the title
//     is `rbworld_link_panel`'s native method for the Rock Central e-mail field. So the query is
//     authored - `src/ui/settings_screen.cpp` builds the key pages - and the host owns the buffer.
//   * **the DTA cannot carry a typed string.** A script can concatenate constants and format
//     integers, but it cannot build a CRC-32 (`src/hooks/settings_bridge.h`) and its text-form
//     token alphabet has no spaces. Characters therefore do not travel as text in a path at all:
//     the guest sends one *key token* per press (`rbbz-store/search/key/<token>`), and the host
//     composes the UTF-8 string. A whole-string spelling (`rbbz-store/search/text/<hex>`) exists
//     for a caller that can build bytes; it is the bounded UTF-8 transport the plan asks for -
//     hex keeps spaces, slashes, quotes and accented bytes inside a token-safe alphabet.
//   * **one query field, one value, never a concatenation.** The API matches artist OR title
//     (owner point §1.5), so this module has no API that takes two names, and the strings it
//     returns say so at the two moments a player meets the rule: on entry and after no matches.
//
// The *presentation* half is the store's own list (measured 2026-10-21 in `ui/store/gen/store.dtb`):
// the front menu's rows are provider names, and a press runs
// `{store_panel set_source …}{store_panel request <name> TRUE}{store_panel update_loading_status}`,
// whose answer the panel turns into `store_browser_screen` with `num_offers` rows. So the search's
// results reach the player through the same page Recent uses: the payload's
// `store::RouteSearchRowToKeys` makes the Search row open the authored keys while nothing is
// pending, and the serving policy answers the search page with the accepted answer
// (`SearchOffers`) otherwise. `SearchMarkShown`/`SearchAnswerPending` are the one-bit state that
// decision needs; the panel's exit writes `leave`, and the strip stops.
//
// SDK-free on purpose: every rule here is a decision over plain data, so
// tests/store_search_tests.cpp walks the whole matrix without a boot, a network or a package.
// The worker that actually performs the request lives in src/store/store_search_service.cpp and
// is reached through `SetSubmitSink`, which is how this file stays linkable from a test.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "store/rhythmverse_client.h"

namespace rb_blitz::store {

// The namespace of the store's own guest channel, beside `rbbz-settings/` and `rbbz-play/`.
// P5 (the download pipeline) adds its job ops under the same prefix; every parser here therefore
// distinguishes "not ours" from "ours and refused" instead of swallowing the namespace.
inline constexpr char kStoreNamespace[] = "rbbz-store/";

// The query's own bounds: the API's minimum is 3 code points, the field stops at 64, and the
// buffer refuses more rather than truncating a name the player is still typing.
std::size_t CodePointCount(std::string_view utf8);

// The typed query. UTF-8 throughout: a whole code point is one backspace, and an invalid sequence
// is refused rather than stored (the API's own field is UTF-8, and a broken string cannot match).
class SearchText {
 public:
  enum class Edit { kOk, kFull, kRejected };

  // One key token: a..z, 0..9, and the five spelling keys below. Unknown tokens are `kRejected`.
  Edit AppendKeyToken(std::string_view token);
  // The whole-string transport: trimmed, then validated (length, control characters, UTF-8).
  bool Set(std::string_view utf8);
  // Removes one code point from the end. False when the field was already empty.
  bool Backspace();
  void Clear();

  const std::string& text() const { return text_; }
  std::size_t length() const { return CodePointCount(text_); }
  // The API's own rule: at least `kMinSearchChars` code points.
  bool ready() const { return length() >= static_cast<std::size_t>(kMinSearchChars); }

 private:
  std::string text_;
};

// The two paths beyond the session's own grammar that other modules spell: the one-bit query the
// store's Search-row gate reads (src/store/store_payload.cpp writes the gate into `store.dtb`) and
// the dismissal the store menu writes when it is left (src/ui/settings_screen.cpp owns the keys,
// store_payload.cpp the store screen's exit).
inline constexpr char kSearchShowQuery[] = "rbbz-store/search/show";
inline constexpr char kSearchLeaveWrite[] = "rbbz-store/search/leave";
// The state a submit puts the session in. Spelled here because it is the one state a *guest* side
// distinguishes (`SearchSubmitQuery` answering `started`), and the suite pins the spelling.
inline constexpr char kSearchSearchingQuery[] = "rbbz-store/search/state/searching";

// The key tokens the DTA's pages send. Kept in one place so the authoring side
// (src/ui/settings_screen.cpp) and this parser cannot drift.
inline constexpr char kKeySpace[] = "space";
inline constexpr char kKeyApostrophe[] = "apostrophe";
inline constexpr char kKeyDash[] = "dash";
inline constexpr char kKeyPeriod[] = "period";
inline constexpr char kKeyBackspace[] = "backspace";
inline constexpr char kKeyClear[] = "clear";
inline constexpr char kKeyDone[] = "done";
inline constexpr char kKeyCancel[] = "cancel";

// The code point a spelling key stands for; empty for the four action keys.
std::string_view KeyTokenText(std::string_view token);

// --- The write channel (guest -> host) --------------------------------------------------------

enum class StoreOp {
  kNone,
  kSearchOpen,    // the key pages were entered: the session opens and shows its guidance
  kSearchKey,     // one typed key; `token` names it
  kSearchText,    // a whole query; `text` carries it (bounded UTF-8, from the hex form)
  kSearchDone,    // submit the query
  kSearchCancel,  // leave the search: invalidate in-flight work and clear the field
  // The store was left (its menu panel's `exit`): the field and the answer stay - re-entering the
  // keys shows the query, and the answer is still the list the Search row opens - but the caption
  // strip stops, because the surface it belongs to is gone.
  kSearchLeave,
};

enum class StoreParse {
  kNotOurs,   // not in `rbbz-store/` - the caller forwards the write untouched
  kAccepted,  // ours, well-formed, applied
  kRefused,   // ours, malformed - answered (never forwarded), with `why` for the log
};

// Grammar (every segment is token-safe; the query never appears in the path):
//
//   path = "rbbz-store/search/" action
//   action = "open" | "done" | "cancel" | "leave"
//          | "key/" <key>                 (a..z, 0..9, space, apostrophe, dash, period,
//                                          backspace, clear)
//          | "text/" <hex-bytes>          (UTF-8, 2 hex digits per byte, at most 4x the field)
//
// Anything else in the namespace is `kNotOurs`, not a refusal: P5's job ops share the prefix and
// must not be eaten by this parser.
StoreParse ParseStoreWrite(std::string_view path, StoreOp* op, std::string* token, std::string* text,
                           std::string* why);

// Applies one parsed write. Returns false (and fills `why`) when the op could not be applied -
// an unknown key token, or a submit the session refused (the field is too short, the master
// switch is off). A false answer is still *handled*: the write is ours and must not be forwarded.
bool ApplyStoreWrite(StoreOp op, std::string_view token, std::string_view text, std::string* why);

// The guest hook's single entry point: parse, apply, and answer "is this path ours?" so the
// `write_file` hook knows whether to forward. A refusal is ours too (it is never forwarded - a
// name in `rbbz-store/` is a name no content file can have). P5's download commands reach their
// own parser first; this one answers `false` for them because it only accepts `search/`.
// `why` receives the reason a refusal or an unapplied command gives, for the hook's log.
bool HandleStoreSearchWrite(std::string_view path, std::string* why);

// --- The one-bit read channel (host -> guest) -------------------------------------------------

// Answers the DTA's membership queries on `file_exists`; false means "not a search query", so the
// caller keeps trying the other namespaces.
//
//   rbbz-store/search/open                  the key pages are the current surface
//   rbbz-store/search/ready                 the field holds at least `kMinSearchChars` code points
//   rbbz-store/search/show                  an answer arrived and the store has not listed it yet
//                                           (the front menu's Search-row gate)
//   rbbz-store/search/state/<token>         editing | searching | results | empty | failed |
//                                           disabled
//   rbbz-store/search/count/at-least/<n>    the result count is at least n (0..99)
bool AnswerSearchQuery(std::string_view path, bool* answer);

// --- The session -------------------------------------------------------------------------------

enum class SearchState {
  kIdle,       // the pages are not open
  kEditing,    // open, nothing submitted (or the field was edited after an answer)
  kTooShort,   // a submit refused by the API's own 3-character rule
  kSearching,  // a request is in flight
  kResults,    // the last answer held at least one eligible offer
  kNoResults,  // the last answer held none - the state the retry guidance belongs to
  kFailed,     // the last request failed (network, parse, HTTP status)
  kDisabled,   // the master switch is off; no request is made and none is accepted
};

std::string_view SearchStateToken(SearchState state);

// One eligible offer: the API record plus the direct link `DecideOffer` accepted. The download
// pipeline (P5) resolves the package from the same URL; the offer list is what P3's record
// writer turns into `dlc_store/**` entries.
struct SearchOffer {
  SongFile song;
  std::string download_url;
};

// The answer to `Submit`: either a request to hand to a worker, or the reason there is none.
struct SearchSubmit {
  bool started = false;
  std::uint64_t generation = 0;
  RequestSpec request;
  std::string why;
  SearchState state = SearchState::kIdle;
};

// Opens the key pages: the session shows its guidance and accepts keys. Safe to call repeatedly
// (every page of the keyboard opens it).
void SearchOpen();
// Leaves the surface (`leave`): the strip stops, an answer that is still in flight keeps running,
// and the field and the answer are kept - re-entering shows the query, and the Search row can still
// list the answer. Cancels nothing; that is `SearchCancel`'s job.
void SearchClose();
// The whole "cancel" path the plan's exit asks about: leave, invalidate, and clear the field.
void SearchCancel();

// One key token from the DTA (`ApplyStoreWrite` routes here). Unknown tokens are refused.
bool SearchApplyKey(std::string_view token);
// The whole-string transport. Trims; refuses control characters, invalid UTF-8, or a field longer
// than `kMaxSearchChars`.
bool SearchSetText(std::string_view utf8);

// Validates the field and, when it may be sent, bumps the generation and answers the request the
  // worker should perform. Refusals are states, not exceptions: too short, the master off, and the
  // wait a failed answer named (`SearchFailAfter`: P7's backoff) still running.
  SearchSubmit SearchSubmitQuery();

  // The per-action page budget (plan P7: "a bounded per-action fetch budget"): how many API pages
  // one submit may read. Installed once per boot by the transport from `store_catalogue_pages`; 1
  // is the pre-P7 behaviour and the default, so a caller that never sets it asks for one page.
  void SetSearchPageBudget(int pages);
  int SearchPageBudget();

  // One page of the current job, as the worker reports it (plan P7's chunking rule). Pages
  // accumulate in the API's order: a page whose records the eligibility/instrument filter rejected
  // is not the end of the walk (only the server's cursor and the budget decide that), and the
  // match count is the *eligible* one - the server's `total_filtered` is never presented as it.
  struct SearchPageAnswer {
    bool accepted = false;  // the page belonged to the current job (a stale one is dropped)
    int next_page = 0;      // the API page to fetch next, or 0 when the job is over
    std::size_t matches = 0;
  };

  // The worker's two answers to one page: `SearchAcceptPage` folds it into the answer in progress
  // (state stays `kSearching` while `next_page` is non-zero), and `SearchFailAfter` ends the walk -
  // with the offers already read kept as the answer when some pages landed, because a partial list
  // is a working store, not a failure to show. `retry_after` is P7's backoff: the session refuses
  // a new submit until it elapses and says how long (the worker derives it from the answer with
  // `rhythmverse_client.h`'s `FailureBackoffSeconds`). `SearchAccept` and `SearchFail` are the
  // one-page spellings: a page with no more rows, and a failure with the transport's default wait.
  //
  // `SearchBuildPageRequest` builds page `page` of the *same* query for the worker's loop, and
  // answers nothing when the job went stale (a newer submit, an edit, a cancel, a settings commit
  // or the master switched off) - which is the cancellation the loop checks between pages, with
  // `SearchPageWanted` as the cheap test while it waits.
  SearchPageAnswer SearchAcceptPage(std::uint64_t generation, const ListingPage& page);
  bool SearchAccept(std::uint64_t generation, const ListingPage& page);
  bool SearchFailAfter(std::uint64_t generation, std::string reason,
                       std::chrono::seconds retry_after);
  bool SearchFail(std::uint64_t generation, std::string reason);
  std::optional<RequestSpec> SearchBuildPageRequest(std::uint64_t generation, int page,
                                                    std::string* why = nullptr);
  bool SearchPageWanted(std::uint64_t generation);
// The eligible offers of the last accepted answer, in the API's order. `store_search_service.cpp`
// turns them into the store's search page (`OfferFromSong`, installed as `store_serve.h`'s
// `SearchPageSource`); Recent's equivalent is the catalogue's own list.
std::vector<SearchOffer> SearchOffers();

// The serving policy handed the store the search page: the answer is on screen, so the front
// menu's Search row opens the keys again instead of re-listing it. Called by `BuiltinRecord`.
void SearchMarkShown();
// True while a terminal answer (results, no results, a failure) has not been presented yet - the
// one bit the front menu's gate reads (`kSearchShowQuery`).
bool SearchAnswerPending();

// A copy of everything a caller needs: the state, the field, and the eligible offers in the API's
// order (newest first). `status` is the caption line the overlay draws.
struct SearchSnapshot {
  SearchState state = SearchState::kIdle;
  std::string text;
  std::vector<SearchOffer> offers;
  std::uint64_t generation = 0;
  std::string status;
};
SearchSnapshot SearchCurrent();
std::string SearchStatusText();

// The worker seam: `store_search_service.cpp` installs the transport at boot
// (`InstallSearchTransport`), and a test simply leaves it unset - a submit then reports
// "no transport" instead of pretending a request happened.
using SubmitSink = std::function<void(std::uint64_t, const RequestSpec&)>;
void SetSubmitSink(SubmitSink sink);

// The one filter rule, shared with Recent: the cheap eligibility predicate (`DecideOffer`) plus
// the instrument filter. Exposed so P3's record writer and the tests apply the same functions.
std::vector<SearchOffer> ShapeOffers(const ListingPage& page, const InstrumentFilter& filter);

// --- The strings ------------------------------------------------------------------------------

// The plan's wording, shared with the key pages' captions and pinned by the tests.
inline constexpr char kSearchGuidance[] =
    "Search by artist or song title, one at a time.";
inline constexpr char kSearchTooShort[] =
    "Type at least 3 characters. Search by artist or song title, one at a time.";
inline constexpr char kSearchRetryHint[] =
    "Try just the artist name or just the song title.";
inline constexpr char kSearchDisabled[] =
    "RhythmVerse is switched off. Turn it on in Help & Options, Downloadable Content.";
// The route from an answer to the browser, in the two presses it takes: Back leaves the keys (the
// strip lives with them), and the front menu's Search row then asks for the page the answer fills.
inline constexpr char kSearchListHint[] = "Press Back, then Search, to list them.";
// The single-match spelling of the same sentence (`CountNoun` picks the branch).
inline constexpr char kSearchListHintOne[] = "Press Back, then Search, to list it.";

}  // namespace rb_blitz::store
