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

// One page-1 request, end to end, on the worker thread. The settings snapshot is captured here so a
// commit during the fetch cannot change what the answer means.
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

  Query query;
  query.instruments = settings->instruments;
  query.page = 1;
  query.records = kDefaultPageSize;

  QueryProblem problem = QueryProblem::kNone;
  const std::optional<RequestSpec> spec = BuildListRequest(query, &problem);
  if (!spec) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = State::kFailed;
    g_reason = std::string("the request could not be built: ") + std::string(QueryProblemText(problem));
    g_changed_at = Clock::now();
    g_pending = false;
    REXLOG_WARN("store_catalogue: {}", g_reason);
    return;
  }

  // Plan P7: a real product string (the transport's default), no retry storm, a bounded wait.
  HttpOptions options;
  options.receive_timeout_ms = 20000;
  const HttpResponse response = PostForm(spec->url, spec->body, spec->headers, options);

  std::string failure;
  std::optional<ListingPage> page;
  if (!response.ok) {
    failure = response.error.empty() ? "the network is unavailable" : response.error;
  } else if (response.status != 200) {
    failure = "the catalogue answered HTTP " + std::to_string(response.status);
  } else {
    std::string error;
    page = ParseListing(response.body, &error);
    if (!page) {
      failure = error.empty() ? "the answer could not be read" : error;
    }
  }

  if (!page) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = State::kFailed;
    g_reason = failure;
    g_changed_at = Clock::now();
    g_pending = false;
    REXLOG_WARN("store_catalogue: fetch failed: {}", failure);
    return;
  }

  // The same eligibility and instrument rules the search uses (`ShapeOffers`), then the record
  // mapping. A page that filtering empties is still a fine answer; P7 owns continuing past it.
  const std::vector<SearchOffer> shaped = ShapeOffers(*page, query.instruments);
  std::vector<Offer> offers;
  offers.reserve(shaped.size());
  for (const SearchOffer& offer : shaped) {
    offers.push_back(OfferFromSong(offer.song));
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  g_pending = false;
  if (SettingsGeneration() != generation) {
    // A commit landed while the answer was in flight: drop it and let the next request re-fetch
    // with the new filter. Nothing is published, so a stale filter can never reach the store.
    g_state = State::kIdle;
    REXLOG_INFO("store_catalogue: the settings changed during the fetch; the answer was dropped");
    return;
  }
  g_offers = std::move(offers);
  g_state = State::kReady;
  g_fetched_generation = generation;
  g_changed_at = Clock::now();
  REXLOG_INFO("store_catalogue: page 1 -> {} eligible offer(s) of {} record(s)", g_offers.size(),
              page->returned);
  // The covers for the page that just landed. The player is still on the front menu - Recent is
  // pressed seconds later - so most textures are encoded before a row is selected, which matters
  // because the retail art loader asks once per selection and does not retry a path it already set
  // (`album_art.h` has the DTA).
  RequestAlbumArt(g_offers);
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
  g_on = true;
  SetOfferSource([] { return Snapshot(); });
  TheWorker();  // start the thread now, so the first request only has to wake it
  REXLOG_INFO("store_catalogue: page 1 of the RhythmVerse list is the store's Recent source "
              "(fetched on demand, fresh for {} s, fetching {})",
              g_ttl.count(), g_fetch_enabled ? "on" : "off");
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
  REXLOG_INFO("store_catalogue: fetching page 1 ({})", why);
}

std::string_view CatalogueStateToken() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return TokenOf(g_state);
}

}  // namespace rb_blitz::store
