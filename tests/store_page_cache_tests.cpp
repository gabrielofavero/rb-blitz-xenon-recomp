// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for P7's in-memory page cache (src/store/store_page_cache.h).
//
// What these pin down (plan §5/P7, "in-memory page cache with TTL (~5 min) + single-flight"):
//   * the key is the endpoint *and* the form body, so the page number, the page size and the
//     instrument filter each identity a page - a filter commit cannot read another filter's page;
//   * a page is served from memory only while it is fresh, and `ttl = 0` means "never", not "forever";
//   * the entry bound holds, and the eviction order is insertion order (the oldest goes first);
//   * single flight: the second caller of the same key is told so, and the mark is retired by
//     `End` or by the `Put` of the answer;
//   * `Clear` drops pages but not in-flight marks (a running fetch still owns its key).

#include <chrono>
#include <string>
#include <thread>

#include "check.h"
#include "store/rhythmverse_client.h"
#include "store/store_page_cache.h"

namespace {

using namespace rb_blitz::store;

namespace test = rb_blitz::test;

ListingPage PageOf(int page, std::string title, int total = 100, int returned = 25) {
  ListingPage page_value;
  page_value.page = page;
  page_value.records_per_page = 25;
  page_value.start = (page - 1) * 25;
  page_value.returned = returned;
  page_value.total_filtered = total;
  SongFile song;
  song.file_id = "id-" + title;
  song.title = std::move(title);
  page_value.songs.push_back(std::move(song));
  return page_value;
}

RequestSpec RequestOf(std::string body) {
  RequestSpec request;
  request.url = "https://rhythmverse.co/api/rb3xbox/songfiles/list";
  request.body = std::move(body);
  return request;
}

void SleepMs(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void TestKey() {
  test::BeginCase("the key is the endpoint plus the body, so the query and the filter identity a page");
  const RequestSpec page_one = RequestOf("page=1&instrument%5B%5D=guitar");
  const RequestSpec page_two = RequestOf("page=2&instrument%5B%5D=guitar");
  const RequestSpec keys_on = RequestOf("page=1&instrument%5B%5D=guitar&instrument%5B%5D=keys");
  CHECK_TRUE(PageCacheKey(page_one) == PageCacheKey(page_one));
  CHECK_TRUE(PageCacheKey(page_one) != PageCacheKey(page_two));
  CHECK_TRUE(PageCacheKey(page_one) != PageCacheKey(keys_on));

  // The two halves cannot be confused: the separator is not a character either half carries.
  RequestSpec shifted = RequestOf("xpage=1");
  shifted.url = "https://rhythmverse.co/api/rb3xbox/songfiles/lis";
  CHECK_TRUE(PageCacheKey(shifted) != PageCacheKey(page_one));
  CHECK_TRUE(PageCacheKey(shifted).find('\n') != std::string::npos);
}

void TestTtl() {
  test::BeginCase("a page is served from memory only while it is fresh");
  PageCache cache(std::chrono::seconds(1));
  const std::string key = "k";
  CHECK_FALSE(cache.Get(key).has_value());
  cache.Put(key, PageOf(1, "Fresh"));
  const auto hit = cache.Get(key);
  CHECK_TRUE(hit.has_value());
  if (hit) {
    CHECK_EQ(hit->page, 1);
    CHECK_TRUE(hit->songs.size() == 1 && hit->songs[0].title == "Fresh");
  }

  // A short TTL: the same cache with 50 ms proves expiry without a minute of waiting.
  PageCache brief(std::chrono::milliseconds(50));
  brief.Put(key, PageOf(1, "Brief"));
  CHECK_TRUE(brief.Get(key).has_value());
  SleepMs(70);
  CHECK_FALSE(brief.Get(key).has_value());
  CHECK_EQ(brief.Size(), 0u);  // an expired page is dropped, not just hidden

  // TTL 0 switches the cache off in both directions.
  PageCache off(std::chrono::seconds(0));
  off.Put(key, PageOf(1, "Off"));
  CHECK_FALSE(off.Get(key).has_value());
  CHECK_EQ(off.Size(), 0u);
  off.SetTtl(std::chrono::seconds(5));  // and back on again
  off.Put(key, PageOf(1, "On"));
  CHECK_TRUE(off.Get(key).has_value());
}

void TestBound() {
  test::BeginCase("the bound holds and the oldest page is evicted first");
  PageCache cache(std::chrono::seconds(60), 2);
  cache.Put("a", PageOf(1, "A"));
  cache.Put("b", PageOf(2, "B"));
  CHECK_EQ(cache.Size(), 2u);
  cache.Put("c", PageOf(3, "C"));
  CHECK_EQ(cache.Size(), 2u);
  CHECK_FALSE(cache.Get("a").has_value());  // the oldest went
  CHECK_TRUE(cache.Get("b").has_value());
  CHECK_TRUE(cache.Get("c").has_value());

  // Replacing a key keeps its place in the eviction order and updates its value.
  cache.Put("b", PageOf(4, "B2"));
  const auto replaced = cache.Get("b");
  CHECK_TRUE(replaced.has_value());
  if (replaced) {
    CHECK_TRUE(replaced->songs.size() == 1 && replaced->songs[0].title == "B2");
  }
  cache.Put("d", PageOf(5, "D"));
  CHECK_TRUE(cache.Get("c").has_value());  // b was refreshed but kept its place: it is the oldest
  CHECK_FALSE(cache.Get("b").has_value());
  CHECK_EQ(cache.Size(), 2u);
}

void TestSingleFlight() {
  test::BeginCase("single flight: one fetch owns a key, and Put or End retires the mark");
  PageCache cache(std::chrono::seconds(60));
  CHECK_TRUE(cache.Begin("k"));
  CHECK_FALSE(cache.Begin("k"));  // a second caller must not send the same request
  CHECK_TRUE(cache.Begin("other"));
  cache.End("k");
  CHECK_TRUE(cache.Begin("k"));  // the first attempt finished (and failed): the key is free again
  cache.Put("k", PageOf(1, "K"));  // the answer also retires the mark
  CHECK_TRUE(cache.Begin("k"));
  cache.End("other");
  cache.End("other");  // an extra End is harmless
  CHECK_TRUE(cache.Begin("other"));
}

void TestClear() {
  test::BeginCase("clear drops the pages but not a running fetch's claim");
  PageCache cache(std::chrono::seconds(60));
  cache.Put("a", PageOf(1, "A"));
  CHECK_TRUE(cache.Begin("b"));
  cache.Clear();
  CHECK_EQ(cache.Size(), 0u);
  CHECK_FALSE(cache.Get("a").has_value());
  CHECK_FALSE(cache.Begin("b"));  // clearing the cache must not start the same request twice
  cache.End("b");
  CHECK_TRUE(cache.Begin("b"));
}

void TestShared() {
  test::BeginCase("the shared instance is one cache, and its TTL is settable per boot");
  SetSharedPageCacheTtl(std::chrono::seconds(60));
  SharedPageCache().Clear();
  SharedPageCache().Put("shared-key", PageOf(1, "Shared"));
  CHECK_TRUE(SharedPageCache().Get("shared-key").has_value());
  CHECK_TRUE(&SharedPageCache() == &SharedPageCache());  // one instance, not one per call

  // Switched off, as `--store_page_cache_ttl_seconds=0` does: nothing is kept.
  SetSharedPageCacheTtl(std::chrono::seconds(0));
  SharedPageCache().Put("shared-key", PageOf(1, "Shared"));
  CHECK_FALSE(SharedPageCache().Get("shared-key").has_value());
  SetSharedPageCacheTtl(std::chrono::seconds(kDefaultPageCacheTtlSeconds));
  SharedPageCache().Clear();
}

}  // namespace

int main() {
  TestKey();
  TestTtl();
  TestBound();
  TestSingleFlight();
  TestClear();
  TestShared();
  return test::Finish();
}
