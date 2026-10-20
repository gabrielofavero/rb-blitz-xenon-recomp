// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_search_service.h.

#include "store/store_search_service.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <rex/logging.h>

#include "store/rhythmverse_client.h"
#include "store/store_http.h"
#include "store/store_search.h"

REXCVAR_DEFINE_BOOL(store_search_diagnostic, false, "Diagnostics",
                    "Open the authored RhythmVerse search keys when the retail store screen is "
                    "entered (RhythmVerse plan P4's measurement switch; off leaves every record as "
                    "shipped). The shipping entry is the store's own Search row, served by P3's "
                    "front menu")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// The same measurement's second half: a query already in the field when the keys open, so a boot
// can press Done and read the live answer without typing on the (harness-flaky) key pages. Empty
// is the ordinary boot. Read on the host thread at InstallSearchTransport.
REXCVAR_DEFINE_STRING(store_search_seed, "", "Diagnostics",
                      "Seed the RhythmVerse search field with this text at boot (RhythmVerse plan "
                      "P4's measurement switch; empty leaves the field empty)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

// One request, as the submit answered it. Only the newest is kept: a player who presses Done twice
// wants the second answer, and the session stamps every answer with its generation anyway.
struct Job {
  std::uint64_t generation = 0;
  RequestSpec request;
};

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
    // Politeness (plan P7/R2): the catalogue is a small, cacheable page and the site asks for
    // `records=25`; one request per submit, no prefetch, no retry storm. A transient failure is
    // reported to the player, who can press Done again - the store does not hammer a 5xx.
    HttpOptions options;
    options.receive_timeout_ms = 15000;
    const HttpResponse response =
        PostForm(job.request.url, job.request.body, job.request.headers, options);
    if (!response.ok) {
      REXLOG_WARN("store_search: the request failed: {}", response.error);
      SearchFail(job.generation, response.error.empty() ? "the network is unavailable"
                                                        : response.error);
      return;
    }
    if (response.status != 200) {
      REXLOG_WARN("store_search: HTTP {} from the catalogue", response.status);
      SearchFail(job.generation, "the catalogue answered HTTP " + std::to_string(response.status));
      return;
    }
    std::string error;
    const std::optional<ListingPage> page = ParseListing(response.body, &error);
    if (!page) {
      REXLOG_WARN("store_search: the answer could not be read: {}", error);
      SearchFail(job.generation, error.empty() ? "the answer could not be read" : error);
      return;
    }
    const bool accepted = SearchAccept(job.generation, *page);
    const SearchSnapshot snapshot = SearchCurrent();
    REXLOG_INFO("store_search: {} record(s) answered, {} eligible offer(s), state {} ({})",
                page->returned, snapshot.offers.size(), SearchStateToken(snapshot.state),
                accepted ? "current" : "stale, dropped");
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
  REXLOG_INFO("store_search: transport ready (one worker thread, {})", kSearchPath);
  // The measurement's seed: a field already holding a query, so Done alone exercises the request.
  const std::string seed = REXCVAR_GET(store_search_seed);
  if (!seed.empty()) {
    if (SearchSetText(seed)) {
      REXLOG_INFO("store_search: the field was seeded with '{}'", seed);
    } else {
      REXLOG_WARN("store_search: the seed '{}' is not a usable query (UTF-8, 3..64 code points)",
                  seed);
    }
  }
}

bool SearchDiagnostic() { return REXCVAR_GET(store_search_diagnostic); }

}  // namespace rb_blitz::store
