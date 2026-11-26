// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "gpu/xenos/host/graphics_upload_buffer_pool.h"

#include <stdexcept>

using namespace rb_blitz::test;
namespace {
class Pool final : public rex::gpu::GraphicsUploadBufferPool {
 public:
  explicit Pool(rex::u64 age = 0) : GraphicsUploadBufferPool(64, age) {}
  ~Pool() { ClearCache(); }
  int live = 0;
  void* Allocate(rex::u64 submission, size_t bytes = 64) {
    size_t offset = 0;
    Page* page = Request(submission, bytes, 1, offset);
    if (!page || offset + bytes > page->size) throw std::runtime_error("invalid upload span");
    return page;
  }
  bool ValidTail() const {
    const Page* tail = writable_first_;
    while (tail && tail->next) tail = tail->next;
    return tail == writable_last_;
  }
 protected:
  struct CountedPage : Page {
    explicit CountedPage(int& count) : count(count) { ++count; }
    ~CountedPage() override { --count; }
    int& count;
  };
  std::unique_ptr<Page> CreatePageImplementation() override {
    return std::make_unique<CountedPage>(live);
  }
};
}

int main() {
  BeginCase("default pools retain their reusable peak capacity");
  {
    Pool pool;
    for (int i = 0; i < 20; ++i) pool.Allocate(i + 1);
    pool.Reclaim(1000);
    CHECK_EQ(pool.live, 20);
    CHECK_TRUE(pool.ValidTail());
  }
  BeginCase("idle pages retire only after their completion age and preserve the current page");
  {
    Pool pool(120);
    for (int i = 0; i < 20; ++i) pool.Allocate(1);
    auto* current = pool.Allocate(500, 1);
    CHECK_EQ(pool.live, 21);
    pool.Reclaim(1);
    pool.Reclaim(120);
    CHECK_EQ(pool.live, 21);
    pool.Reclaim(121);
    CHECK_EQ(pool.live, 1);
    CHECK_TRUE(pool.Allocate(500, 1) == current);
    CHECK_TRUE(pool.ValidTail());
    pool.Allocate(501);
    CHECK_EQ(pool.live, 2);
    pool.Reclaim(620);
    CHECK_EQ(pool.live, 1);
    CHECK_TRUE(pool.ValidTail());
    pool.ClearCache();
    CHECK_EQ(pool.live, 0);
    CHECK_TRUE(pool.ValidTail());
  }
  BeginCase("pending pages and recent completed pages remain usable");
  {
    Pool pool(10);
    pool.Allocate(1);
    pool.Allocate(50);
    pool.Allocate(100);
    pool.Reclaim(1);
    pool.Reclaim(11);
    CHECK_EQ(pool.live, 2);
    pool.Reclaim(59);
    CHECK_EQ(pool.live, 2);
    pool.Reclaim(60);
    CHECK_EQ(pool.live, 1);
    CHECK_TRUE(pool.ValidTail());
    pool.Allocate(101);
    pool.Reclaim(101);
    pool.Allocate(102);
    CHECK_TRUE(pool.ValidTail());
  }
  return Finish();
}
