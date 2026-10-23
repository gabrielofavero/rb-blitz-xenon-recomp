// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_search_service.h.

#include "store/store_search_service.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "store/rhythmverse_client.h"
#include "store/store_catalogue_status.h"
#include "store/store_http.h"
#include "store/store_metadata.h"
#include "store/store_page_cache.h"
#include "store/store_search.h"
#include "store/store_serve.h"

// A query already in the field at boot, so a scripted boot can reach a live request and answer
// without typing on the on-screen keyboard: the store screen's keyboard entry asks
// `rbbz-store/search/seed` (`store_search.h`) and submits the field instead of drawing the
// keyboard while a seed is installed. Empty is the ordinary boot. Read on the host thread at
// InstallSearchTransport.
REXCVAR_DEFINE_STRING(store_search_seed, "", "Diagnostics",
                      "Seed the RhythmVerse search field with this text at boot (RhythmVerse plan "
                      "P4's measurement switch; empty leaves the field empty)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(store_search_pages, 4, "Diagnostics",
                     "How many API pages one search may read (clamped 1..20; the site's page size "
                     "is 25 records). RhythmVerse plan P7's per-action budget: the walk continues "
                     "past a page the eligibility/instrument filter emptied, because only the "
                     "server's cursor decides where the results end. 1 is the pre-P7 behaviour")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

// The pause between two pages of one search (P7's politeness), walked in slices so a cancel or a
// newer query is noticed while waiting rather than after the next request.
constexpr auto kPagePause = std::chrono::milliseconds(150);
constexpr auto kPauseSlice = std::chrono::milliseconds(25);

// One request, as the submit answered it. Only the newest is kept: a player who presses Done twice
// wants the second answer, and the session stamps every answer with its generation anyway.
struct Job {
  std::uint64_t generation = 0;
  RequestSpec request;
};

// P7's cancellation test between pages: the job is still the session's, the settings still match,
// and the master is still on. Checked before every request and inside `PauseBetweenPages`.
bool PauseBetweenPages(std::uint64_t generation) {
  const auto deadline = std::chrono::steady_clock::now() + kPagePause;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!SearchPageWanted(generation)) {
      return false;
    }
    std::this_thread::sleep_for(kPauseSlice);
  }
  return SearchPageWanted(generation);
}

class Worker {
 public:
  Worker() : thread_([this] { Run(); }) {}

  ~Worker() {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void Post(Job job) {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      if (quit_) {
        return;
      }
      pending_ = std::move(job);
    }
    wake_.notify_one();
  }

 private:
  void Run() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return quit_ || pending_.has_value(); });
        if (quit_) {
          return;
        }
        job = std::move(*pending_);
        pending_.reset();
      }
      RunOne(job);
    }
  }

  static void RunOne(const Job& job) {
    // Plan P7's chunking: the session owns the query, the cursor and the budget, so the worker is
    // a loop - ask the session whether the job still wants a page, fetch it (or read it from the
    // page cache), hand the page back, and let the session name the next page or the end.
    const std::uint64_t generation = job.generation;
    int page_number = 1;
    RequestSpec request = job.request;
    for (;;) {
      if (!SearchPageWanted(generation)) {
        REXLOG_INFO("store_search: the job ended before page {}; nothing more was sent",
                    page_number);
        return;
      }
      std::optional<ListingPage> page;
      const std::string key = PageCacheKey(request);
      if (std::optional<ListingPage> cached = SharedPageCache().Get(key)) {
        page = std::move(*cached);
        REXLOG_INFO("store_search: page {} answered from the page cache", page_number);
      } else {
        if (!SharedPageCache().Begin(key)) {
          // Two identical pages at once cannot happen within one job (the body carries the page
          // number), but the rule is the rule: never send the same request twice concurrently.
          SearchFailAfter(generation, "an identical request is already in flight",
                          std::chrono::seconds(kBackoffNetworkSeconds));
          return;
        }
        struct Flight {
          std::string key;
          ~Flight() { SharedPageCache().End(key); }
        } flight{key};
        // Politeness (plan P7/R2): the catalogue is a small, cacheable page and the site asks for
        // `records=25`; one request per submit, no prefetch, no retry storm. A transient failure is
        // reported to the player, who can press Done again after the backoff the answer named -
        // the store does not hammer a 5xx.
        HttpOptions options;
        options.receive_timeout_ms = 15000;
        const HttpResponse response =
            PostForm(request.url, request.body, request.headers, options);
        if (!response.ok) {
          REXLOG_WARN("store_search: the request failed: {}", response.error);
          SearchFailAfter(generation,
                          response.error.empty() ? "the network is unavailable" : response.error,
                          std::chrono::seconds(FailureBackoffSeconds(0, std::nullopt)));
          return;
        }
        if (response.status != 200) {
          const int wait = FailureBackoffSeconds(response.status, RetryAfterSeconds(response));
          REXLOG_WARN("store_search: HTTP {} from the catalogue; the next attempt waits {} s",
                      response.status, wait);
          SearchFailAfter(generation,
                          "the catalogue answered HTTP " + std::to_string(response.status),
                          std::chrono::seconds(wait));
          return;
        }
        std::string error;
        page = ParseListing(response.body, &error);
        if (!page) {
          REXLOG_WARN("store_search: the answer could not be read: {}", error);
          SearchFailAfter(generation, error.empty() ? "the answer could not be read" : error,
                          std::chrono::seconds(FailureBackoffSeconds(200, std::nullopt)));
          return;
        }
        SharedPageCache().Put(key, *page);
      }
      const SearchPageAnswer accepted = SearchAcceptPage(generation, *page);
      const SearchSnapshot snapshot = SearchCurrent();
      REXLOG_INFO("store_search: page {} -> {} record(s) answered, {} eligible offer(s) in "
                  "total, state {} ({})",
                  page_number, page->returned, snapshot.offers.size(),
                  SearchStateToken(snapshot.state),
                  accepted.accepted ? "current" : "stale, dropped");
      if (!accepted.accepted || accepted.next_page == 0) {
        return;  // the answer is terminal, or a newer query owns the session now
      }
      if (!PauseBetweenPages(generation)) {
        REXLOG_INFO("store_search: the walk stopped after {} page(s)", page_number);
        return;
      }
      std::string why;
      std::optional<RequestSpec> next = SearchBuildPageRequest(generation, accepted.next_page, &why);
      if (!next) {
        REXLOG_INFO("store_search: page {} was not requested: {}", accepted.next_page, why);
        return;
      }
      request = std::move(*next);
      page_number = accepted.next_page;
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<Job> pending_;
  bool quit_ = false;
  std::thread thread_;
};

std::mutex g_install_mutex;
std::shared_ptr<Worker> g_worker;

}  // namespace

void InstallSearchTransport() {
  const std::lock_guard<std::mutex> lock(g_install_mutex);
  if (g_worker) {
    return;
  }
  g_worker = std::make_shared<Worker>();
  // The sink outlives the guest: `g_worker` is a process-lifetime object and the lambda holds a
  // reference to it, so a submit that arrives after boot cannot post into a destroyed thread.
  const std::shared_ptr<Worker> worker = g_worker;
  SetSubmitSink([worker](std::uint64_t generation, const RequestSpec& request) {
    worker->Post(Job{generation, request});
  });
  // The search page's offers (P4's second half): the accepted answer, mapped through the same
  // `OfferFromSong` Recently Added's catalogue uses, and marked presented as it is taken - the
  // retail browser then lists it exactly as it lists Recently Added.
  SetSearchPageSource([] {
    std::vector<Offer> page;
    for (const SearchOffer& answer : SearchOffers()) {
      page.push_back(OfferFromSong(answer.song, answer.download_url));
    }
    SearchMarkShown();
    // The search page is a store menu page like Recently Added's: the caption strip's copy uses
    // this to know the store is still on screen (`store_catalogue_status.h`).
    NoteStoreSurface(StoreSurface::kMenuPage);
    return page;
  });
  // Plan P7's per-action budget: the session owns the walk, this is where it learns the bound.
  const std::int32_t pages = REXCVAR_GET(store_search_pages);
  SetSearchPageBudget(pages < 1 ? 1 : (pages > 20 ? 20 : pages));
  REXLOG_INFO("store_search: transport ready (one worker thread, {}, up to {} page(s) per search)",
              kSearchPath, SearchPageBudget());
  // The measurement's seed: a field already holding a query, so Done alone exercises the request.
  // It fills the field without opening the surface - the strip belongs to the key pages, and a
  // seeded boot drew `Search: <query>` over the store's own screens (measured 2026-10-22).
  const std::string seed = REXCVAR_GET(store_search_seed);
  if (!seed.empty()) {
    if (SearchSeedField(seed)) {
      REXLOG_INFO("store_search: the field was seeded with '{}'", seed);
    } else {
      REXLOG_WARN("store_search: the seed '{}' is not a usable query (UTF-8, 3..64 code points)",
                  seed);
    }
  }
}

}  // namespace rb_blitz::store
