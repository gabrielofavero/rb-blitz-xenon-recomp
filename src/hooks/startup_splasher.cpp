// SPDX-License-Identifier: GPL-2.0-only
#include <rex/hook.h>
#include <rex/system/xmemory.h>

REX_EXTERN(__imp__sub_82192008); // load current splash
REX_EXTERN(__imp__sub_82192A08); // poll/advance current splash
REX_EXTERN(__imp__sub_82192350); // retire current splash and advance

namespace {
bool EmptySplashList(const PPCContext& ctx, const uint8_t* base) {
  // Splasher owns an intrusive list at +184. Retail unconditionally treats
  // its head as a scene in Load (scene+428 -> virtual Enter). An empty authored
  // splash_order instead points back to the sentinel, causing a call to zero.
  // Also guard Advance: its empty-list path erases that same sentinel.
  const uint32_t sentinel = ctx.r3.u32 + 184;
  return rex::memory::load_and_swap<uint32_t>(base + sentinel) == sentinel &&
         rex::memory::load_and_swap<uint32_t>(base + sentinel + 4) == sentinel;
}
}

// Leave the worker's 5/6/7/8 state/event handshake intact. Only scene work is
// empty; returning from the worker itself would leave the main thread waiting.
extern "C" REX_FUNC(sub_82192008) {
  if (EmptySplashList(ctx, base)) ctx.r3.u64 = 0;
  else __imp__sub_82192008(ctx, base);
}
extern "C" REX_FUNC(sub_82192A08) {
  if (EmptySplashList(ctx, base)) ctx.r3.u64 = 0;
  else __imp__sub_82192A08(ctx, base);
}
extern "C" REX_FUNC(sub_82192350) {
  if (EmptySplashList(ctx, base)) ctx.r3.u64 = 0;
  else __imp__sub_82192350(ctx, base);
}
