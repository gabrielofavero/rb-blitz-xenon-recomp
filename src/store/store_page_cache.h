// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P7 of the RhythmVerse store plan (docs/plans/finished/2026-10-13-rhythmverse-store-plan.md
// §5/P7): the in-memory **page cache** the store's two workers share.
//
// What it exists for, in the plan's words: "in-memory page cache with TTL (~5 min) + single-flight".
// The community site is a small server, so a page that was already fetched is answered from memory
// until the TTL expires, and two callers that want the same page at the same time cost it one
// request. Both halves matter here for different reasons:
//
//   * Recent is asked for again every time the store opens, and a player who backs out of the
//     browser and presses Recent again within a few minutes asks the same question twice. The
//     catalogue's own freshness (`store_catalogue_ttl_seconds`) already holds the *list*; this
//     cache holds the *pages*, so a refetch that the TTL does allow still re-uses any page the
//     site says has not changed shape (the request body is the key).
//   * Search is one request per submit, and a player who presses Done twice for the same query
//     (or re-enters the same search) gets the same answer. The cache makes that free.
//
// The key is the endpoint **and** the form body, so everything that makes two answers different is
// part of the identity: the query text, the page number, the page size and the instrument filter
// (which is a body field, plan §1.6). A settings commit therefore cannot read a page that was
// fetched for the filter it replaced - the body differs.
//
// What this deliberately is **not**: a disk cache. The plan marks `<user_data_root>/store/cache/`
// optional, and it stays unbuilt - it would need the user-data root and a serialization format
// inside a module whose whole point is being SDK-free, and the in-memory TTL already covers one
// session. `store_page_cache_ttl_seconds` = 0 turns the cache off for a diagnostic boot.
//
// Thread-safe by construction: every entry point takes the same mutex, the values are copies, and
// nothing in here knows about the network, the guest or the settings (project rule for worker code).
// SDK-free, so tests/store_page_cache_tests.cpp walks the TTL, the bound, the eviction and
// single-flight without a boot or a socket.

#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "store/rhythmverse_client.h"

namespace rb_blitz::store {

// Plan §P7's "~5 min", and a bound that keeps a long session's searches from growing without end
// (an entry is one page of 25 records; 64 of them is a few hundred kilobytes).
inline constexpr int kDefaultPageCacheTtlSeconds = 300;
inline constexpr std::size_t kDefaultPageCacheEntries = 64;

// The identity of a page: the request's URL and its form body, one string. Two requests with the
// same key must have the same answer, which is why nothing else is folded in and nothing that
// differs is left out.
std::string PageCacheKey(const RequestSpec& request);

class PageCache {
 public:
  using Clock = std::chrono::steady_clock;

  explicit PageCache(
      std::chrono::milliseconds ttl = std::chrono::seconds(kDefaultPageCacheTtlSeconds),
      std::size_t max_entries = kDefaultPageCacheEntries);

  // A fresh page for this key, or nothing (absent, expired, or the cache is switched off).
  std::optional<ListingPage> Get(std::string_view key) const;

  // Stores a page and retires its in-flight mark. A cache with TTL 0 stores nothing.
  void Put(std::string_view key, const ListingPage& page);

  // Single flight: true when this caller owns the fetch for `key` (nobody else is fetching it),
  // false when another caller already is - it must then not start a second request **and must not
  // call `End`** (the owner does). A caller that begins a fetch must end it on every path;
  // `Put` also retires it, because storing the answer is the end of the fetch.
  bool Begin(std::string_view key);
  void End(std::string_view key);

  void Clear();
  void SetTtl(std::chrono::milliseconds ttl);

  // Diagnostics: entries currently held (expired ones are dropped by Get, not counted here).
  std::size_t Size() const;

 private:
  struct Entry {
    ListingPage page;
    Clock::time_point stored_at;
  };

  bool Fresh(const Entry& entry, Clock::time_point now) const;

  mutable std::mutex mutex_;
  std::chrono::milliseconds ttl_;
  std::size_t max_entries_;
  // `Get` drops an expired page as it finds it (otherwise the bound and a size report would count
  // a page nobody can read), which is why the containers are mutable behind a const getter.
  mutable std::unordered_map<std::string, Entry> entries_;
  mutable std::list<std::string> order_;  // insertion order, oldest first: the eviction order
  std::unordered_set<std::string> in_flight_;
};

// The one cache the workers share: single-flight has to span the catalogue and the search, and a
// page either of them fetched is worth keeping for the other.
PageCache& SharedPageCache();

// The TTL the shared cache uses. Read once per boot by the catalogue's installation (which owns the
// cvar, because this module is SDK-free); a boot that never calls it keeps the 300 s default.
void SetSharedPageCacheTtl(std::chrono::milliseconds ttl);

}  // namespace rb_blitz::store
