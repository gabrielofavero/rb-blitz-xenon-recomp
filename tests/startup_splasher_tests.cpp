// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include <array>
#include "../src/hooks/startup_splasher.cpp"

unsigned calls = 0;
extern "C" REX_FUNC(__imp__sub_82192008) { ++calls; ctx.r3.u64 = 11; }
extern "C" REX_FUNC(__imp__sub_82192A08) { ++calls; ctx.r3.u64 = 12; }
extern "C" REX_FUNC(__imp__sub_82192350) { ++calls; ctx.r3.u64 = 13; }

int main() {
  using namespace rb_blitz::test;
  std::array<uint8_t, 4096> memory{};
  auto* base = memory.data();
  constexpr uint32_t object = 256, head = object + 184;
  PPCContext ctx{};
  ctx.r1.u32 = 2048;
  ctx.r31.u64 = 123;
  ctx.lr = 0x82192B3C;
  BeginCase("empty native splash list never loads or erases its sentinel");
  rex::memory::store_and_swap<uint32_t>(base + head, head);
  rex::memory::store_and_swap<uint32_t>(base + head + 4, head);
  const auto before = memory;
  for (auto* hook : {sub_82192008, sub_82192A08, sub_82192350}) {
    ctx.r3.u32 = object;
    hook(ctx, base);
    CHECK_EQ(ctx.r3.u64, 0u);
    CHECK_EQ(ctx.r1.u32, 2048u);
    CHECK_EQ(ctx.r31.u64, 123u);
    CHECK_EQ(ctx.lr, 0x82192B3Cu);
    CHECK_TRUE(memory == before); // state/event handshake belongs to native worker
  }
  CHECK_EQ(calls, 0u);
  BeginCase("nonempty splash lists keep their native scene work");
  rex::memory::store_and_swap<uint32_t>(base + head, 1024);
  rex::memory::store_and_swap<uint32_t>(base + head + 4, 1024);
  uint64_t expected = 11;
  for (auto* hook : {sub_82192008, sub_82192A08, sub_82192350}) {
    ctx.r3.u32 = object;
    hook(ctx, base);
    CHECK_EQ(ctx.r3.u64, expected++);
  }
  CHECK_EQ(calls, 3u);
  return Finish();
}
