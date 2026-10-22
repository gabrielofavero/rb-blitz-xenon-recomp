// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_page_cache.h: the in-memory page cache P7's caching rule asks for.

#include "store/store_page_cache.h"

#include <algorithm>
#include <utility>

namespace rb_blitz::store {
namespace {

// The TTL the shared instance was built with, so `SetSharedPageCacheTtl` can answer before the
// first worker asks for the cache (the installation runs on the host thread, before the guest).
std::mutex g_shared_ttl_mutex;
std::chrono::milliseconds g_shared_ttl{std::chrono::seconds(kDefaultPageCacheTtlSeconds)};

}  // namespace

std::string PageCacheKey(const RequestSpec& request) {
  // A newline cannot appear in a URL (the client refuses control characters) and is percent-encoded
  // in a form body, so the two halves can never be confused for one another.
  std::string key;
  key.reserve(request.url.size() + request.body.size() + 1);
  key.append(request.url);
  key.push_back('\n');
  key.append(request.body);
  return key;
}

PageCache::PageCache(std::chrono::milliseconds ttl, std::size_t max_entries)
    : ttl_(ttl), max_entries_(max_entries == 0 ? 1 : max_entries) {}

bool PageCache::Fresh(const Entry& entry, Clock::time_point now) const {
  if (ttl_.count() <= 0) {
    return false;  // the cache is switched off: nothing is ever served from memory
  }
  return now - entry.stored_at <= ttl_;
}

std::optional<ListingPage> PageCache::Get(std::string_view key) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (ttl_.count() <= 0) {
    return std::nullopt;
  }
  const auto found = entries_.find(std::string(key));
  if (found == entries_.end()) {
    return std::nullopt;
  }
  if (!Fresh(found->second, Clock::now())) {
    // Expired: drop it so a size report and the bound do not count a page nobody can read.
    order_.remove(found->first);
    entries_.erase(found);
    return std::nullopt;
  }
  return found->second.page;
}

void PageCache::Put(std::string_view key, const ListingPage& page) {
  const std::lock_guard<std::mutex> lock(mutex_);
  in_flight_.erase(std::string(key));  // the fetch that owned this key is over, hit or miss
  if (ttl_.count() <= 0) {
    return;
  }
  const std::string owned(key);
  const auto found = entries_.find(owned);
  if (found != entries_.end()) {
    // Replacing keeps the key's place in the eviction order: a page refreshed inside the TTL is
    // not more likely to be dropped than one that sat still.
    found->second.page = page;
    found->second.stored_at = Clock::now();
    return;
  }
  while (entries_.size() >= max_entries_ && !order_.empty()) {
    // The bound is what keeps a long session from growing without end; the oldest page goes
    // first, which is also the least likely to be asked for again.
    const std::string& oldest = order_.front();
    entries_.erase(oldest);
    order_.pop_front();
  }
  entries_.emplace(owned, Entry{page, Clock::now()});
  order_.push_back(owned);
}

bool PageCache::Begin(std::string_view key) {
  const std::lock_guard<std::mutex> lock(mutex_);
  // One owner per key: the second caller is told so and must neither send the request nor end the
  // flight it does not own.
  return in_flight_.insert(std::string(key)).second;
}

void PageCache::End(std::string_view key) {
  const std::lock_guard<std::mutex> lock(mutex_);
  in_flight_.erase(std::string(key));
}

void PageCache::Clear() {
  const std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  order_.clear();
  // In-flight marks are kept: a fetch that is already running still owns its key, and clearing the
  // mark would let a second caller start the same request.
}

void PageCache::SetTtl(std::chrono::milliseconds ttl) {
  const std::lock_guard<std::mutex> lock(mutex_);
  ttl_ = ttl;
}

std::size_t PageCache::Size() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

PageCache& SharedPageCache() {
  // The installation sets the TTL before any worker exists, so the one construction below reads
  // the boot's value; a later `SetSharedPageCacheTtl` updates the instance it created.
  std::chrono::milliseconds ttl{std::chrono::seconds(kDefaultPageCacheTtlSeconds)};
  {
    const std::lock_guard<std::mutex> lock(g_shared_ttl_mutex);
    ttl = g_shared_ttl;
  }
  static PageCache cache(ttl);
  return cache;
}

void SetSharedPageCacheTtl(std::chrono::milliseconds ttl) {
  {
    const std::lock_guard<std::mutex> lock(g_shared_ttl_mutex);
    g_shared_ttl = ttl;
  }
  SharedPageCache().SetTtl(ttl);
}

}  // namespace rb_blitz::store
