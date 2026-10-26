// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_guest.h"
#include <array>

namespace {
std::string interned;
constexpr uint32_t kSymbol = 0x12345678;
}

// The guest's intern constructor is the only stand-in. Exercise the production
// adapter's scratch frame, result type and preservation of the native context.
extern "C" REX_FUNC(__imp__sub_8245C878) {
  interned = reinterpret_cast<const char*>(base + ctx.r4.u32);
  rex::memory::store_and_swap<uint32_t>(base + ctx.r3.u32, kSymbol);
  CHECK_EQ(ctx.r1.u32 & 15, 0u);
  ctx.r3.u64 = 0xFFFFFFFF;
  ctx.r4.u64 = 0xFFFFFFFF;
}

int main() {
  rb_blitz::test::BeginCase("a live queued song is returned as a case-preserving native Symbol");
  std::array<uint8_t, 16384> memory{};
  PPCContext ctx{};
  ctx.r1.u32 = 16000;
  ctx.r3.u32 = 64;
  ctx.r4.u32 = 1234;
  ctx.r31.u64 = 0xABCDEF;
  ctx.lr = 0x82410000;
  for (const auto& key : {std::string("QueuedSong_ActualKey"), std::string(),
                          std::string(4096, 'x'), std::string(4097, 'x')}) {
    rb_blitz::shell_music::ReturnSongSymbol(ctx, memory.data(), key);
    CHECK_TRUE(interned == (key.size() > 4096 ? std::string() : key));
    CHECK_EQ(rex::memory::load_and_swap<uint32_t>(memory.data() + 64), kSymbol);
    CHECK_EQ(rex::memory::load_and_swap<uint32_t>(memory.data() + 68), 5u);
    CHECK_EQ(ctx.r1.u32, 16000u);
    CHECK_EQ(ctx.r3.u32, 64u);
    CHECK_EQ(ctx.r4.u32, 1234u);
    CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
    CHECK_EQ(ctx.lr, 0x82410000u);
  }
  return rb_blitz::test::Finish();
}
