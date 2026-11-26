// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "gpu/xenos/host/vulkan/shared_memory_snapshot_cache.h"
#include <array>
#include <cstring>
#include <random>
#include <vector>

int main() {
  using namespace rb_blitz::test;
  using rex::gpu::vulkan::SharedMemorySnapshotCache;
  constexpr uint32_t page_size = 16;
  using Page = std::array<uint8_t, page_size>;
  Page original{}, changed{}; changed[page_size-1] = 7;
  SharedMemorySnapshotCache cache(8 * page_size, page_size, 2 * page_size);
  BeginCase("exact comparison includes the final byte and owns the uploaded version");
  CHECK_FALSE(cache.Matches(0, original.data()));
  auto* snapshot = cache.RememberUpload(0, original.data());
  CHECK_TRUE(snapshot != original.data());
  CHECK_TRUE(cache.Matches(0, original.data()));
  CHECK_FALSE(cache.Matches(0, changed.data()));
  original[0] = 9;
  CHECK_FALSE(cache.Matches(0, original.data()));
  CHECK_EQ(snapshot[0], 0);
  CHECK_EQ(cache.capacity_bytes(), 2u * page_size);

  BeginCase("GPU writes invalidate intersected pages including shared boundaries");
  cache.RememberUpload(0, original.data());cache.RememberUpload(1, changed.data());
  cache.Invalidate(page_size - 1, page_size);
  CHECK_FALSE(cache.Matches(0, original.data()));
  CHECK_FALSE(cache.Matches(1, changed.data()));
  cache.RememberUpload(0, original.data());cache.RememberUpload(1, changed.data());
  cache.Invalidate(2 * page_size, UINT32_MAX);
  CHECK_TRUE(cache.Matches(0, original.data()));
  CHECK_TRUE(cache.Matches(1, changed.data()));
  cache.Clear();
  CHECK_FALSE(cache.Matches(0, original.data()));
  CHECK_FALSE(cache.Matches(1, changed.data()));

  BeginCase("bounded replacement never returns another page's stale data");
  cache.RememberUpload(0, original.data());cache.RememberUpload(1, changed.data());
  cache.RememberUpload(2, original.data());
  CHECK_FALSE(cache.Matches(0, original.data()));
  CHECK_TRUE(cache.Matches(2, original.data()));
  SharedMemorySnapshotCache disabled(8 * page_size, page_size, 0);
  disabled.RememberUpload(0, original.data());
  CHECK_FALSE(disabled.Matches(0, original.data()));
  CHECK_EQ(disabled.capacity_bytes(), 0u);
  CHECK_FALSE(cache.Matches(8, original.data()));

  BeginCase("copy elision preserves an independent GPU mirror across mixed writers");
  std::array<Page, 8> cpu{}, gpu{};
  std::array<bool, 8> known{};
  SharedMemorySnapshotCache model(8 * page_size, page_size, 3 * page_size);
  std::mt19937 random(0x5841122D);
  struct Event { unsigned page; Page data; bool draw; };
  std::vector<Event> pending;
  auto replay = [&] {
    for (const auto& event : pending) {
      if (event.draw) CHECK_TRUE(gpu[event.page] == event.data);
      else gpu[event.page] = event.data;
    }
    pending.clear();
  };
  unsigned reused = 0, copied = 0;
  for (unsigned n = 0; n < 4000; ++n) {
    const unsigned page = random() % cpu.size();
    if (n % 9 == 0) {
      // A GPU write makes an old CPU upload snapshot unsuitable for reuse,
      // even when the CPU bytes equal that old snapshot.
      Page gpu_write; gpu_write.fill(uint8_t(n));
      pending.push_back({page, gpu_write, false});
      model.Invalidate(page * page_size, (page + 1) * page_size - 1);
      known[page] = true;
    }
    if (n % 4 == 0) cpu[page][random() % page_size] = uint8_t(random());
    if (n % 41 == 0) model.Clear();
    const bool matches = model.Matches(page, cpu[page].data());
    if (matches) {
      CHECK_TRUE(known[page]);
      ++reused;
    } else {
      const auto* data = model.RememberUpload(page, cpu[page].data());
      Page staged;
      std::memcpy(staged.data(), data, page_size);
      pending.push_back({page, staged, false});
      known[page] = true;
      ++copied;
    }
    pending.push_back({page, cpu[page], true});
    if (n % 100 == 99) replay();
  }
  replay();
  CHECK_TRUE(reused > 500);
  CHECK_TRUE(copied > 500);
  return Finish();
}
