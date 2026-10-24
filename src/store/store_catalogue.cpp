// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_catalogue.h.

#include "store/store_catalogue.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "store/album_art_service.h"
#include "store/rhythmverse_client.h"
#include "store/store_catalogue_status.h"
#include "store/store_http.h"
#include "store/store_metadata.h"
#include "store/store_page_cache.h"
#include "store/store_search.h"
#include "store/store_serve.h"
#include "store/store_settings.h"

REXCVAR_DEFINE_INT32(store_catalogue_ttl_seconds, 300, "Diagnostics",
                     "How long the fetched RhythmVerse catalogue stays fresh (RhythmVerse plan "
                     "P7's cache, the Recent list's own freshness): a store visit inside this "
                     "window re-uses the list instead of fetching it again. Pages are cached "
                     "separately, see store_page_cache_ttl_seconds")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(store_catalogue_pages, 4, "Diagnostics",
                     "How many API pages one store action accumulates (25 records each, the site's "
                     "own page size; clamped 1..20). Recent fills the retail browser's single list "
                     "from this many pages - retail has no paging of its own - and the value is "
                     "also the per-action budget the search is given (store_search_pages). The cap "
                     "keeps a boot's traffic bounded; the walk continues past a page the "
                     "instrument filter emptied, because the server's cursor, not the visible "
                     "offer count, decides where the list ends")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(store_page_cache_ttl_seconds, 300, "Diagnostics",
                     "How long one fetched RhythmVerse API page stays in the in-memory page cache "
                     "(RhythmVerse plan P7's ~5 min; 0 turns the cache off, so every visit asks the "
                     "site again). The key is the request body, so the query text, the page number "
                     "and the instrument filter each identity a page; single flight spans Recent "
                     "and Search, so the same page is never requested twice at once")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_catalogue_off, false, "Diagnostics",
                    "Never fetch the RhythmVerse catalogue (the store then answers every menu with "
                    "an empty page); off is the ordinary boot")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(store_api_base, "", "Diagnostics",
                      "Point the list and search requests at another API origin - an absolute "
                      "http(s)://host[:port] with no path (RhythmVerse plan P9-10's failure "
                      "measurement: an origin that refuses makes the press draw the popup and ask "
                      "for a fresh walk, and a local stub can be flipped to serving inside the "
                      "same boot); empty keeps https://rhythmverse.co. The download and artwork "
                      "resolution stay same-site and do not follow it")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

using Clock = std::chrono::steady_clock;

// Plan P7's politeness rules, as constants: one request at a time, a fresh page reused for the
// TTL (the page cache holds that), a failed page left alone for a wait derived from its answer
// (`FailureBackoffSeconds`), and a pause between two pages of one visit. The site is a community
// server; four requests in a row is still polite, one after another with a pause is politer.
constexpr auto kPagePause = std::chrono::milliseconds(150);
// The pause is walked in slices so a cancel is noticed while waiting, not only after it.
constexpr auto kPauseSlice = std::chrono::milliseconds(25);

enum class State {
  kIdle,      // nothing has been asked for yet
  kFetching,  // one request is in flight
  kReady,     // a page is held and was fresh when it landed
  kFailed,    // the last attempt failed; the backoff decides when the next one may start
};

// Set once by InstallStoreCatalogue() on the host thread, before the guest exists.
bool g_on = false;
bool g_fetch_enabled = true;
std::chrono::seconds g_ttl{300};
int g_pages = 4;  // `store_catalogue_pages`: how many API pages one visit accumulates

std::mutex g_mutex;
std::condition_variable g_wake;
State g_state = State::kIdle;
bool g_pending = false;
bool g_quit = false;
Clock::time_point g_changed_at{};
// The wait a failed answer named (plan P7's backoff). `RequestCatalogueRefresh` refuses work
// inside it, so pressing Recent again cannot hammer a catalogue that just said no.
Clock::time_point g_retry_at{};
std::vector<Offer> g_offers;
std::string g_reason;  // why the last attempt failed, or why the last fetch started
std::uint64_t g_fetched_generation = 0;

std::string_view TokenOf(State state) {
  switch (state) {
    case State::kIdle:
      return "off";
    case State::kFetching:
      return "fetching";
    case State::kReady:
      return "fresh";
    case State::kFailed:
      return "failed";
  }
  return "off";
}

CatalogueFetchState StatusOf(State state) {
  switch (state) {
    case State::kIdle:
      return CatalogueFetchState::kIdle;
    case State::kFetching:
      return CatalogueFetchState::kFetching;
    case State::kReady:
      return CatalogueFetchState::kReady;
    case State::kFailed:
      return CatalogueFetchState::kFailed;
  }
  return CatalogueFetchState::kIdle;
}

// The caption strip's copy of the worker's state (`store_catalogue_status.h`). Called after the
// worker's own lock is released: the two modules have their own mutexes and nothing here needs
// them held together.
void PublishStatus(State state, int pages, std::size_t offers, const std::string& reason) {
  PublishCatalogueFetchState(StatusOf(state), pages, offers, reason);
}

// One page's request, end to end, on the worker thread. The settings snapshot is captured by the
// caller so a commit during the fetch cannot change what the answer means.
//
// P7's two rules the callers depend on: a page the shared cache holds is answered from memory (no
// request at all), and a failure names its own wait in `backoff` - derived from the answer, with
// `Retry-After` honoured - so the caller holds the next visit off for exactly that long. Returns
// false when the page could not be fetched or read, with `failure` naming the step.
bool FetchPage(const SettingsSnapshot& settings, int page_number, ListingPage* page,
               std::chrono::seconds* backoff, std::string* failure) {
  Query query;
  query.instruments = settings->instruments;
  query.page = page_number;
  query.records = kDefaultPageSize;

  QueryProblem problem = QueryProblem::kNone;
  const std::optional<RequestSpec> spec = BuildListRequest(query, &problem);
  if (!spec) {
    *failure = std::string("the request could not be built: ") +
               std::string(QueryProblemText(problem));
    *backoff = std::chrono::seconds(kBackoffStatusSeconds);
    return false;
  }

  // Plan P7's page cache. The key is the request body, so a hit can only be the same question:
  // the same page number, page size, sort and instrument filter.
  const std::string key = PageCacheKey(*spec);
  if (std::optional<ListingPage> cached = SharedPageCache().Get(key)) {
    *page = std::move(*cached);
    *backoff = std::chrono::seconds(0);
    REXLOG_INFO("store_catalogue: page {} answered from the page cache", page_number);
    return true;
  }
  if (!SharedPageCache().Begin(key)) {
    // Somebody is already asking this exact question (single flight spans the catalogue and the
    // search). Waiting for their answer instead of sending a second request is the point; the
    // caller backs off a little and the answer will be cached by then.
    *failure = "an identical request is already in flight";
    *backoff = std::chrono::seconds(kBackoffNetworkSeconds);
    return false;
  }
  struct Flight {
    std::string key;
    ~Flight() { SharedPageCache().End(key); }
  } flight{key};

  HttpOptions options;
  options.receive_timeout_ms = 20000;
  const HttpResponse response = PostForm(spec->url, spec->body, spec->headers, options);
  if (!response.ok) {
    *failure = response.error.empty() ? "the network is unavailable" : response.error;
    *backoff = std::chrono::seconds(FailureBackoffSeconds(0, std::nullopt));
    return false;
  }
  if (response.status != 200) {
    *failure = "the catalogue answered HTTP " + std::to_string(response.status);
    *backoff = std::chrono::seconds(
        FailureBackoffSeconds(response.status, RetryAfterSeconds(response)));
    return false;
  }
  std::string error;
  std::optional<ListingPage> parsed = ParseListing(response.body, &error);
  if (!parsed) {
    *failure = error.empty() ? "the answer could not be read" : error;
    *backoff = std::chrono::seconds(FailureBackoffSeconds(200, std::nullopt));
    return false;
  }
  SharedPageCache().Put(key, *parsed);
  *page = std::move(*parsed);
  *backoff = std::chrono::seconds(0);
  return true;
}

// True while this fetch is still the one the store wants: the same settings generation (a commit is
// a different query), the master switch still on, the worker not shutting down. Checked before
// every page and inside the pause, which is what makes P7's "cancellation" a stop rather than a
// stop-after-the-next-answer.
bool StillWanted(std::uint64_t generation) {
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_quit) {
      return false;
    }
  }
  return SettingsGeneration() == generation && Settings()->enabled;
}

// The pause between two pages, interruptible: a cancel is noticed inside a slice, not only after
// the whole wait.
bool PauseBetweenPages(std::uint64_t generation) {
  const Clock::time_point deadline = Clock::now() + kPagePause;
  while (Clock::now() < deadline) {
    if (!StillWanted(generation)) {
      return false;
    }
    std::this_thread::sleep_for(kPauseSlice);
  }
  return StillWanted(generation);
}

// The whole Recent list: page 1, then as many more pages as `store_catalogue_pages` allows (or as
// the site has). Each page is published as it lands, so a player who presses Recent while the
// fetch is still running already sees the offers fetched so far - the retail browser reads the
// snapshot when it is asked for the page, and asking for the page is what starts this.
void RunOne() {
  const SettingsSnapshot settings = Settings();
  const std::uint64_t generation = SettingsGeneration();
  if (!settings->enabled) {
    // The master went off between the request and the fetch: publish nothing and stay idle, so a
    // later boot/commit with the switch on fetches fresh. A list the old filter produced is not
    // this boot's answer any more (plan §1.6: off cancels outstanding work).
    {
      const std::lock_guard<std::mutex> lock(g_mutex);
      g_state = State::kIdle;
      g_pending = false;
      g_offers.clear();
    }
    PublishStatus(State::kIdle, 0, 0, {});
    return;
  }

  std::vector<Offer> offers;
  int fetched = 0;
  int page_number = 1;
  for (;;) {
    ListingPage page;
    std::chrono::seconds backoff{0};
    std::string failure;
    if (!FetchPage(settings, page_number, &page, &backoff, &failure)) {
      State state = State::kFailed;
      {
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_pending = false;
        g_retry_at = Clock::now() + backoff;
        g_changed_at = Clock::now();
        g_reason = failure;
        if (fetched > 0) {
          // Some pages landed: keep them (a partial list is a working store) and back off before
          // trying the rest, instead of throwing away a usable answer.
          state = State::kReady;
          g_fetched_generation = generation;
        }
        g_state = state;
      }
      REXLOG_WARN("store_catalogue: page {} failed after {} page(s): {}; the next attempt waits "
                  "{} s",
                  page_number, fetched, failure, backoff.count());
      PublishStatus(state, fetched, offers.size(), failure);
      return;
    }
    ++fetched;

    // The same eligibility and instrument rules the search uses (`ShapeOffers`), then the record
    // mapping. A page that filtering empties is still a fine answer - the next one continues.
    const std::vector<SearchOffer> shaped = ShapeOffers(page, settings->instruments);
    for (const SearchOffer& offer : shaped) {
      // The resolved URL travels with the offer (P5): the record keeps it host-only, and the
      // serving path hands it to the download worker when the page is written.
      offers.push_back(OfferFromSong(offer.song, offer.download_url));
    }
    // P9-13: an audio type this build has no label for draws no line on the details page. Name it
    // once per boot, so a catalogue that starts publishing a new code is visible without a screen.
    {
      static bool noted_unknown_audio_type = false;
      if (!noted_unknown_audio_type) {
        for (const SearchOffer& offer : shaped) {
          if (!offer.song.audio_type.empty() &&
              AudioTypeLabel(offer.song.audio_type).empty()) {
            REXLOG_WARN("store_catalogue: the catalogue publishes audio type '{}' which this "
                        "build has no label for; the details page draws no audio-type line",
                        offer.song.audio_type);
            noted_unknown_audio_type = true;
            break;
          }
        }
      }
    }

    bool dropped = false;
    {
      const std::lock_guard<std::mutex> lock(g_mutex);
      if (SettingsGeneration() != generation) {
        // A commit landed while the answer was in flight: drop everything and let the next request
        // re-fetch with the new filter. Clearing the published list is part of that - a page
        // written from the old filter must not survive the filter that replaced it.
        g_offers.clear();
        g_state = State::kIdle;
        g_pending = false;
        dropped = true;
      } else {
        g_offers = offers;  // the snapshot a page is built from, grown page by page
        g_fetched_generation = generation;
        g_changed_at = Clock::now();
        g_retry_at = {};  // a page landed: an earlier failure's wait no longer applies
      }
    }
    if (dropped) {
      REXLOG_INFO("store_catalogue: the settings changed during the fetch; the answer was "
                  "dropped");
      PublishStatus(State::kIdle, 0, 0, {});
      return;
    }
    REXLOG_INFO("store_catalogue: page {} -> {} eligible offer(s) of {} record(s) ({} total)",
                page_number, shaped.size(), page.returned, offers.size());
    PublishStatus(State::kFetching, fetched, offers.size(), {});
    // The covers for the offers that just landed. The player is still on the front menu - Recent is
    // pressed seconds later - so most textures are encoded before a row is selected, which matters
    // because the retail art loader asks once per selection and does not retry a path it already
    // set (`album_art.h` has the DTA). The worker's queue is bounded, so a long list simply fills
    // it and the rest is asked for again when the page is served (`StartArtPrefetch`).
    RequestAlbumArt(offers);
    // P7's chunking cursor: the server's own page number, never the count of offers kept.
    const int after = NextPageAfter(page, fetched, g_pages);
    if (after == 0) {
      break;  // the site says this was the last page, or the per-visit budget is spent
    }
    if (!PauseBetweenPages(generation)) {
      // Cancelled between pages. Whatever landed is dropped: the filter that produced it is not the
      // current one (or the master is off), so keeping it would be a page the player cannot
      // explain.
      const bool enabled = Settings()->enabled;
      {
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_pending = false;
        g_offers.clear();
        g_state = State::kIdle;
      }
      REXLOG_INFO("store_catalogue: the fetch stopped after {} page(s) ({})", fetched,
                  enabled ? "the filter changed" : "RhythmVerse was switched off");
      PublishStatus(State::kIdle, fetched, 0, {});
      return;
    }
    page_number = after;
  }

  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = false;
    g_state = State::kReady;
    g_fetched_generation = generation;
    g_changed_at = Clock::now();
  }
  REXLOG_INFO("store_catalogue: {} page(s) -> {} offer(s) for the Recent list", fetched,
              offers.size());
  PublishStatus(State::kReady, fetched, offers.size(), {});
}

void Run() {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_wake.wait(lock, [] { return g_quit || g_pending; });
      if (g_quit) {
        return;
      }
    }
    RunOne();
  }
}

class Worker {
 public:
  Worker() : thread_([this] { Run(); }) {}

  ~Worker() {
    {
      const std::lock_guard<std::mutex> lock(g_mutex);
      g_quit = true;
    }
    g_wake.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::thread thread_;
};

Worker& TheWorker() {
  static Worker worker;
  return worker;
}

std::vector<Offer> Snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_offers;
}

// The walk request both public entries share. `ignore_backoff` is P9-10's user-initiated press:
// the player asked, so the wait a previous failure named does not hold the press off; the
// automatic path (boot prewarm, a store opening) keeps it.
void RequestRefresh(std::string_view why, bool ignore_backoff) {
  if (!g_on || !g_fetch_enabled) {
    return;
  }
  const SettingsSnapshot settings = Settings();
  if (!settings->enabled) {
    // Master off: the store must make no network request at all (plan §1.6).
    return;
  }
  std::size_t offers_now = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Clock::time_point now = Clock::now();
    switch (g_state) {
      case State::kFetching:
        return;  // single flight
      case State::kReady:
        // A page that landed inside this visit clears the wait; a partial list keeps it, so the
        // pages that failed are retried after their backoff rather than on the next keypress.
        if (now < g_retry_at && !ignore_backoff) {
          return;
        }
        if (SettingsGeneration() == g_fetched_generation && now - g_changed_at < g_ttl) {
          return;  // fresh enough
        }
        break;
      case State::kFailed:
        if (now < g_retry_at && !ignore_backoff) {
          REXLOG_DEBUG("store_catalogue: not fetching yet; the last failure asked for {} ms more",
                       std::chrono::duration_cast<std::chrono::milliseconds>(g_retry_at - now)
                           .count());
          return;  // politeness: the answer named its own wait
        }
        break;
      case State::kIdle:
        break;
    }
    g_state = State::kFetching;
    g_pending = true;
    g_changed_at = now;
    offers_now = g_offers.size();
  }
  g_wake.notify_one();
  PublishCatalogueFetchState(CatalogueFetchState::kFetching, 0, offers_now, {});
  REXLOG_INFO("store_catalogue: fetching up to {} page(s) ({})", g_pages, why);
}

}  // namespace

void InstallStoreCatalogue() {
  g_fetch_enabled = !REXCVAR_GET(store_catalogue_off);
  const std::int32_t ttl = REXCVAR_GET(store_catalogue_ttl_seconds);
  g_ttl = std::chrono::seconds(ttl > 0 ? ttl : 0);
  const std::int32_t pages = REXCVAR_GET(store_catalogue_pages);
  g_pages = pages < 1 ? 1 : (pages > 20 ? 20 : pages);
  // P7's page cache. Its TTL is this boot's, and the search worker reads the same instance, so the
  // two actions share both the pages and the single-flight marks.
  const std::int32_t page_ttl = REXCVAR_GET(store_page_cache_ttl_seconds);
  SetSharedPageCacheTtl(std::chrono::seconds(page_ttl > 0 ? page_ttl : 0));
  SetCatalogueFetchEnabled(g_fetch_enabled);
  // P9-10's failure instrument: a boot can move the API origin, so the press's failed branch is
  // measurable without touching the machine's network. A refused value is reported rather than
  // silently ignored, because the boot that expected to fail would otherwise quietly succeed.
  const std::string api_base = REXCVAR_GET(store_api_base);
  if (api_base.empty()) {
    SetApiBaseOverride("");
  } else if (SetApiBaseOverride(api_base)) {
    REXLOG_INFO("store_catalogue: the API base is '{}' (store_api_base)", ApiBase());
  } else {
    REXLOG_WARN("store_catalogue: store_api_base '{}' is not an absolute http(s) origin; the "
                "requests keep '{}'",
                api_base, ApiBase());
  }
  PublishCatalogueFetchState(CatalogueFetchState::kIdle, 0, 0);
  g_on = true;
  SetOfferSource([] {
    // Every menu page the store asks for passes through here, so this is where the caption strip
    // learns which surface is on screen (`store_catalogue_status.h`). The front menu is noted by
    // the file-cache hook itself, because it never asks for offers.
    NoteStoreSurface(StoreSurface::kMenuPage);
    return Snapshot();
  });
  TheWorker();  // start the thread now, so the first request only has to wake it
  REXLOG_INFO("store_catalogue: the RhythmVerse list is the store's Recent source (up to {} page(s) "
              "per visit, fetched on demand, fresh for {} s, fetching {})",
              g_pages, g_ttl.count(), g_fetch_enabled ? "on" : "off");
}

void RequestCatalogueRefresh(std::string_view why) { RequestRefresh(why, false); }

void RequestCatalogueRefreshNow(std::string_view why) { RequestRefresh(why, true); }

bool AnswerCatalogueQuery(std::string_view path, bool* answer) {
  if (answer == nullptr || !IsCatalogueFailedQuery(path)) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    *answer = g_state == State::kFailed && g_offers.empty();
  }
  return true;
}

bool HandleCatalogueWrite(std::string_view path) {
  if (!IsCatalogueRetryCommand(path)) {
    return false;
  }
  RequestCatalogueRefreshNow("a press asked");
  return true;
}

std::string_view CatalogueStateToken() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return TokenOf(g_state);
}

}  // namespace rb_blitz::store
