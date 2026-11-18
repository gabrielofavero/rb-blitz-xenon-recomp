// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include <array>
#include <cstdint>
#include <random>
#include <rex/gpu/shared_memory_upload_hazards.h>
#include <algorithm>
#include <vector>

int main() {
  using namespace rb_blitz::test;
  using rex::gpu::vulkan::SharedMemoryUploadHazards;
  SharedMemoryUploadHazards hazards;
  constexpr uint32_t page = SharedMemoryUploadHazards::kPageSize;

  BeginCase("only tracked read-only passes allow moving uploads");
  hazards.BeginPass();
  CHECK_FALSE(hazards.CanUpload(0, page));
  hazards.PrepareDraw(true);
  hazards.Read(100, 32);
  hazards.FinishDraw();
  CHECK_FALSE(hazards.CanUpload(0, page));
  CHECK_TRUE(hazards.CanUpload(page, page));
  hazards.PrepareDraw(false);
  hazards.FinishDraw();
  hazards.PrepareDraw(true);
  CHECK_FALSE(hazards.CanUpload(page, page));
  hazards.BeginPass();
  hazards.FinishDraw(); // An untracked helper draw.
  hazards.PrepareDraw(true);
  CHECK_FALSE(hazards.CanUpload(page, page));
  hazards.BeginPass();
  hazards.Disable();
  hazards.PrepareDraw(true);
  CHECK_FALSE(hazards.CanUpload(page, page));

  BeginCase(
      "page edges, overlapping writes, overflow and reset stay conservative");
  hazards.BeginPass();
  hazards.PrepareDraw(true);
  hazards.Read(page - 1, 2);
  hazards.FinishDraw();
  CHECK_FALSE(hazards.CanUpload(0, 1));
  CHECK_FALSE(hazards.CanUpload(page, 1));
  CHECK_TRUE(hazards.CanUpload(2 * page, page));
  hazards.Uploaded(2 * page, page);
  CHECK_FALSE(hazards.CanUpload(2 * page + 16, 8));
  CHECK_TRUE(hazards.CanUpload(3 * page, page));
  CHECK_FALSE(hazards.CanUpload(0, 0));
  CHECK_FALSE(hazards.CanUpload(UINT32_MAX - 3, 8));
  CHECK_FALSE(hazards.CanUpload(SharedMemoryUploadHazards::kBufferSize, 1));
  CHECK_TRUE(hazards.CanUpload(SharedMemoryUploadHazards::kBufferSize - 1, 1));
  hazards.Read(UINT32_MAX, 4);
  CHECK_FALSE(hazards.CanUpload(3 * page, page));
  hazards.BeginPass();
  hazards.PrepareDraw(true);
  CHECK_TRUE(hazards.CanUpload(0, 4 * page));

  BeginCase("reordered batches preserve the values observed by every draw");
  // Independent byte-level oracle: apply each upload immediately, and save the
  // expected draw result. Replay approved uploads before all reads in the pass.
  // Include unaligned ranges, cache-hit reads and repeated updates of a page.
  constexpr uint32_t size = 16 * page;
  std::array<uint8_t, size> reference{}, replay{};
  struct Upload {
    uint32_t start, size;
    uint8_t value;
  };
  struct Read {
    uint32_t start;
    std::vector<uint8_t> expected;
  };
  std::vector<Upload> uploads;
  std::vector<Read> reads;
  auto finish = [&] {
    for (const auto &upload : uploads)
      std::fill_n(replay.begin() + upload.start, upload.size, upload.value);
    for (const auto &read : reads)
      CHECK_TRUE(std::equal(read.expected.begin(), read.expected.end(),
                            replay.begin() + read.start));
    CHECK_TRUE(replay == reference);
    uploads.clear();
    reads.clear();
    hazards.BeginPass();
    hazards.PrepareDraw(true);
  };
  finish();
  std::mt19937 random(0x814770);
  unsigned hoisted = 0, fallbacks = 0;
  for (unsigned i = 0; i < 3000; ++i) {
    const uint32_t start = random() % (size - 1024);
    const uint32_t length = 1 + random() % 1024;
    if (i % 3) {
      if (!hazards.CanUpload(start, length)) {
        finish();
        ++fallbacks;
      } else
        ++hoisted;
      const uint8_t value = uint8_t(i);
      uploads.push_back({start, length, value});
      hazards.Uploaded(start, length);
      std::fill_n(reference.begin() + start, length, value);
    }
    const uint32_t read_start = (i % 4) ? start : random() % (size - 1024);
    reads.push_back({read_start,
                     {reference.begin() + read_start,
                      reference.begin() + read_start + length}});
    hazards.PrepareDraw(true);
    hazards.Read(read_start, length);
    hazards.FinishDraw();
  }
  finish();
  CHECK_TRUE(hoisted > 500);
  CHECK_TRUE(fallbacks > 100);
  return Finish();
}
