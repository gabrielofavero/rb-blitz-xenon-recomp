// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_catalogue.h.

#include "store/store_catalogue.h"

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
#include "store/store_http.h"
#include "store/store_metadata.h"
#include "store/store_search.h"
#include "store/store_serve.h"
#include "store/store_settings.h"

REXCVAR_DEFINE_INT32(store_catalogue_ttl_seconds, 300, "Diagnostics",
                     "How long a fetched RhythmVerse catalogue page stays fresh (RhythmVerse plan "
                     "P7's page cache; the store re-fetches page 1 only after it expires)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(store_catalogue_pages, 4, "Diagnostics",
                     "How many catalogue pages one store visit's Recent list accumulates (25 records "
                     "each, the site's own page size). The retail browser has no paging of its own, "
                     "so the offers of every fetched page go into one list; 1 is the pre-P3 state "
                     "(one page of ~24 eligible offers), the cap keeps a boot's traffic bounded")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_catalogue_off, false, "Diagnostics",
                    "Never fetch the RhythmVerse catalogue (the store then answers every menu with "
                    "an empty page); off is the ordinary boot")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

using Clock = std::chrono::steady_clock;

// Plan P7's politeness rules, as constants: one request at a time, a fresh page reused for the TTL,
// and a failed fetch left alone for a while rather than retried on every store screen.
constexpr auto kFailureBackoff = std::chrono::seconds(30);
// The gap between two pages of one visit. The site is a community server; a burst of four requests
// in a row is still polite, one after another with a pause is politer.
constexpr auto kPagePause = std::chrono::milliseconds(150);

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

// One page's request, end to end, on the worker thread. The settings snapshot is captured by the
// caller so a commit during the fetch cannot change what the answer means. Returns false when the
// page could not be fetched or read, with `failure` naming the step.
bool FetchPage(const SettingsSnapshot& settings, int page_number, ListingPage* page,
               std::string* failure) {
  Query query;
  query.instruments = settings->instruments;
  query.page = page_number;
  query.records = kDefaultPageSize;

  QueryProblem problem = QueryProblem::kNone;
  const std::optional<RequestSpec> spec = BuildListRequest(query, &problem);
  if (!spec) {
    *failure = std::string("the request could not be built: ") +
               std::string(QueryProblemText(problem));
    return false;
  }

  // Plan P7: a real product string (the transport's default), no retry storm, a bounded wait.
  HttpOptions options;
  options.receive_timeout_ms = 20000;
  const HttpResponse response = PostForm(spec->url, spec->body, spec->headers, options);
  if (!response.ok) {
    *failure = response.error.empty() ? "the network is unavailable" : response.error;
    return false;
  }
  if (response.status != 200) {
    *failure = "the catalogue answered HTTP " + std::to_string(response.status);
    return false;
  }
  std::string error;
  std::optional<ListingPage> parsed = ParseListing(response.body, &error);
  if (!parsed) {
    *failure = error.empty() ? "the answer could not be read" : error;
    return false;
  }
  *page = std::move(*parsed);
  return true;
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
    // later boot/commit with the switch on fetches fresh.
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = State::kIdle;
    g_pending = false;
    return;
  }

  std::vector<Offer> offers;
  int fetched = 0;
  for (int page_number = 1; page_number <= g_pages; ++page_number) {
    if (page_number > 1) {
      // Politeness between pages of one visit: the site asks for one request at a time and this is
      // the same single flight, just continued.
      std::this_thread::sleep_for(kPagePause);
    }
    ListingPage page;
    std::string failure;
    if (!FetchPage(settings, page_number, &page, &failure)) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_pending = false;
      if (fetched == 0) {
        g_state = State::kFailed;
        g_reason = failure;
        g_changed_at = Clock::now();
        REXLOG_WARN("store_catalogue: fetch failed: {}", failure);
      } else {
        // Some pages landed: keep them (a partial list is a working store) and back off before
        // trying the rest, instead of throwing away a usable answer.
        g_state = State::kReady;
        g_fetched_generation = generation;
        g_changed_at = Clock::now();
        REXLOG_WARN("store_catalogue: page {} failed after {} page(s): {}; keeping what landed",
                    page_number, fetched, failure);
      }
      return;
    }
    ++fetched;

    // The same eligibility and instrument rules the search uses (`ShapeOffers`), then the record
    // mapping. A page that filtering empties is still a fine answer - the next one continues.
    const std::vector<SearchOffer> shaped = ShapeOffers(page, settings->instruments);
    std::size_t added = 0;
    for (const SearchOffer& offer : shaped) {
      offers.push_back(OfferFromSong(offer.song));
      ++added;
    }

    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (SettingsGeneration() != generation) {
        // A commit landed while the answer was in flight: drop everything and let the next request
        // re-fetch with the new filter. Clearing the published list is part of that - a page
        // written from the old filter must not survive the filter that replaced it.
        g_offers.clear();
        g_state = State::kIdle;
        g_pending = false;
        REXLOG_INFO("store_catalogue: the settings changed during the fetch; the answer was "
                    "dropped");
        return;
      }
      g_offers = offers;  // the snapshot a page is built from, grown page by page
      g_fetched_generation = generation;
      g_changed_at = Clock::now();
    }
    REXLOG_INFO("store_catalogue: page {} -> {} eligible offer(s) of {} record(s) ({} total)",
                page_number, added, page.returned, offers.size());
    // The covers for the offers that just landed. The player is still on the front menu - Recent is
    // pressed seconds later - so most textures are encoded before a row is selected, which matters
    // because the retail art loader asks once per selection and does not retry a path it already
    // set (`album_art.h` has the DTA). The worker's queue is bounded, so a long list simply fills
    // it and the rest is asked for again when the page is served (`StartArtPrefetch`).
    RequestAlbumArt(offers);
    if (!page.HasMorePages()) {
      break;  // the site says this was the last page
    }
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  g_pending = false;
  g_state = State::kReady;
  g_fetched_generation = generation;
  g_changed_at = Clock::now();
  REXLOG_INFO("store_catalogue: {} page(s) -> {} offer(s) for the Recent list", fetched,
              offers.size());
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

}  // namespace

void InstallStoreCatalogue() {
  g_fetch_enabled = !REXCVAR_GET(store_catalogue_off);
  const std::int32_t ttl = REXCVAR_GET(store_catalogue_ttl_seconds);
  g_ttl = std::chrono::seconds(ttl > 0 ? ttl : 0);
  const std::int32_t pages = REXCVAR_GET(store_catalogue_pages);
  g_pages = pages < 1 ? 1 : (pages > 20 ? 20 : pages);
  g_on = true;
  SetOfferSource([] { return Snapshot(); });
  TheWorker();  // start the thread now, so the first request only has to wake it
  REXLOG_INFO("store_catalogue: the RhythmVerse list is the store's Recent source (up to {} page(s) "
              "per visit, fetched on demand, fresh for {} s, fetching {})",
              g_pages, g_ttl.count(), g_fetch_enabled ? "on" : "off");
}

void RequestCatalogueRefresh(std::string_view why) {
  if (!g_on || !g_fetch_enabled) {
    return;
  }
  const SettingsSnapshot settings = Settings();
  if (!settings->enabled) {
    // Master off: the store must make no network request at all (plan §1.6).
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Clock::time_point now = Clock::now();
    switch (g_state) {
      case State::kFetching:
        return;  // single flight
      case State::kReady:
        if (SettingsGeneration() == g_fetched_generation && now - g_changed_at < g_ttl) {
          return;  // fresh enough
        }
        break;
      case State::kFailed:
        if (now - g_changed_at < kFailureBackoff) {
          return;  // politeness: do not hammer a failing catalogue
        }
        break;
      case State::kIdle:
        break;
    }
    g_state = State::kFetching;
    g_pending = true;
    g_changed_at = now;
  }
  g_wake.notify_one();
  REXLOG_INFO("store_catalogue: fetching up to {} page(s) ({})", g_pages, why);
}

std::string_view CatalogueStateToken() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return TokenOf(g_state);
}

}  // namespace rb_blitz::store
