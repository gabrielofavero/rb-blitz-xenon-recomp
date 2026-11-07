// SPDX-License-Identifier: GPL-2.0-only
// Diagnose large guest bitmap allocations while Milo textures load.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REX_EXTERN(__imp__sub_8220CCA8);  // RndTex::Load
REX_EXTERN(__imp__sub_821FC8A8);  // RndBitmap pixel allocation
REX_EXTERN(__imp__sub_8246F658);  // guest heap free-list statistics
REX_EXTERN(__imp__sub_8246FA20);  // guest heap allocation

namespace {
thread_local std::string g_loading_tex;

bool TraceBitmaps() {
  // This hook observes every large guest texture. Writing two INFO lines per
  // allocation holds the synchronous file/debug sinks on the guest loading
  // thread, so keep the detailed trace for explicit diagnostics only.
  static const bool enabled = [] {
    const char* value = std::getenv("RB_BLITZ_BITMAP_ALLOC_TRACE");
    return value && *value && std::string_view(value) != "0";
  }();
  return enabled;
}

std::string TexName(uint8_t* base, uint32_t object) {
  if (object < 0x40000000 || object >= 0x7F000000) return {};
  const auto name = rex::memory::load_and_swap<uint32_t>(base + object + 32);
  if (!name) return {};
  const char* chars = rex::memory::GuestPtr<const char*>(base, name);
  if (!chars) return {};
  std::string result;
  for (size_t i = 0; i < 96; ++i) {
    const auto ch = static_cast<unsigned char>(chars[i]);
    if (!ch) break;
    if (ch < 0x20 || ch > 0x7E) return {};
    result.push_back(static_cast<char>(ch));
  }
  return result;
}
}  // namespace

REX_HOOK_RAW(sub_8220CCA8) {
  const uint32_t tex = ctx.r3.u32;
  const auto started = std::chrono::steady_clock::now();
  if (!TraceBitmaps()) {
    __imp__sub_8220CCA8(ctx, base);
  } else {
    auto previous = std::move(g_loading_tex);
    g_loading_tex = TexName(base, tex);
    __imp__sub_8220CCA8(ctx, base);
    g_loading_tex = std::move(previous);
  }

  // A few compact buckets distinguish slow Milo texture loading from an
  // unrelated guest stall without emitting two lines for every bitmap.
  const auto finished = std::chrono::steady_clock::now();
  const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
      finished - started).count();
  struct LoadingTimes {
    std::chrono::steady_clock::time_point window_started;
    uint64_t total_us = 0;
    uint64_t max_us = 0;
    uint32_t count = 0;
  };
  thread_local LoadingTimes times;
  thread_local uint32_t slow_logged = 0;
  if (!times.count) times.window_started = started;
  times.total_us += static_cast<uint64_t>(elapsed_us);
  if (static_cast<uint64_t>(elapsed_us) > times.max_us)
    times.max_us = static_cast<uint64_t>(elapsed_us);
  ++times.count;
  if (elapsed_us >= 500000 && slow_logged < 16) {
    ++slow_logged;
    REXLOG_WARN("bitmap_alloc: slow RndTex::Load tex='{}' elapsed_ms={:.1f}",
                TexName(base, tex), elapsed_us / 1000.0);
  }
  if (finished - times.window_started >= std::chrono::seconds(10)) {
    REXLOG_INFO("bitmap_alloc: RndTex::Load over {:.1f} s: {} call(s), {:.1f} ms total, {:.1f} ms max",
                std::chrono::duration<double>(finished - times.window_started).count(),
                times.count, times.total_us / 1000.0, times.max_us / 1000.0);
    times = {};
  }
}

REX_HOOK_RAW(sub_821FC8A8) {
  if (!TraceBitmaps()) {
    __imp__sub_821FC8A8(ctx, base);
    return;
  }
  const uint32_t bitmap = ctx.r3.u32;
  if (bitmap >= 0x40000000 && bitmap < 0x7F000000) {
    const uint32_t width = rex::memory::load_and_swap<uint16_t>(base + bitmap + 6);
    const uint32_t height = rex::memory::load_and_swap<uint16_t>(base + bitmap + 8);
    const uint32_t bits = base[bitmap + 10];
    const uint64_t pixels = uint64_t(width) * height;
    if (pixels >= 65536)
      REXLOG_INFO("bitmap_alloc: tex='{}' bitmap={:08X} {}x{} bits={} pixels={}",
                  g_loading_tex, bitmap, width, height, bits, pixels);
  }
  __imp__sub_821FC8A8(ctx, base);
  if (bitmap >= 0x40000000 && bitmap < 0x7F000000 &&
      uint64_t(rex::memory::load_and_swap<uint16_t>(base + bitmap + 6)) *
          rex::memory::load_and_swap<uint16_t>(base + bitmap + 8) >= 65536) {
    REXLOG_INFO("bitmap_alloc: tex='{}' bitmap={:08X} data={:08X}",
                g_loading_tex, bitmap,
                rex::memory::load_and_swap<uint32_t>(base + bitmap + 24));
  }
}

REX_HOOK_RAW(sub_8246F658) {
  const uint32_t total_out = ctx.r6.u32;
  const uint32_t largest_out = ctx.r7.u32;
  __imp__sub_8246F658(ctx, base);
  if (!g_loading_tex.empty())
    REXLOG_WARN("bitmap_alloc: heap retry tex='{}' total_free={} largest_free={}",
                g_loading_tex,
                rex::memory::load_and_swap<uint32_t>(base + total_out),
                rex::memory::load_and_swap<uint32_t>(base + largest_out));
}

REX_HOOK_RAW(sub_8246FA20) {
  // Once per boot, print the heap's total free bytes on its first allocation: that is the
  // heap size the title set up from config/gen/mem.dtb, and the one number that says whether
  // the raised definition was actually served (retail 137,000,000 vs generated 160,000,000).
  static bool first_logged = false;
  if (!first_logged) {
    first_logged = true;
    uint64_t total = 0;
    uint32_t blocks = 0;
    uint32_t node = rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32);
    while (node >= 0x40000000 && node < 0x7F000000 && blocks < 100000) {
      total += rex::memory::load_and_swap<uint32_t>(base + node) * 4;
      ++blocks;
      node = rex::memory::load_and_swap<uint32_t>(base + node + 8);
    }
    REXLOG_INFO("bitmap_alloc: guest heap first allocation request={} free={} blocks={}",
                ctx.r4.u32 * 4, total, blocks);
  }
  if (g_loading_tex == "rays.tex") {
    uint64_t total = 0;
    uint32_t largest = 0;
    uint32_t blocks = 0;
    uint32_t node = rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32);
    while (node >= 0x40000000 && node < 0x7F000000 && blocks < 100000) {
      const uint32_t bytes = rex::memory::load_and_swap<uint32_t>(base + node) * 4;
      total += bytes;
      if (bytes > largest) largest = bytes;
      ++blocks;
      node = rex::memory::load_and_swap<uint32_t>(base + node + 8);
    }
    REXLOG_INFO("bitmap_alloc: heap before tex='{}' request={} free={} largest={} blocks={} tail={:08X}",
                g_loading_tex, ctx.r4.u32 * 4, total, largest, blocks, node);
  }
  __imp__sub_8246FA20(ctx, base);
}
