// SPDX-License-Identifier: GPL-2.0-only
// Actual hook forwarding contracts against isolated memory; no game or UI.
#include "check.h"
#include "loading/load_trace.h"
#include <array>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/xmemory.h>

REX_EXTERN(sub_822C3428);
REX_EXTERN(sub_822C3878);
REX_EXTERN(sub_822C38F8);
REX_EXTERN(sub_822C3F48);
REXCVAR_DECLARE(bool, song_load_trace);

namespace rb_blitz::loading {
void BeginCrashRecoveryAttempt() {}
void BeforeSongLoadJob() {}
void EndCrashRecoveryAttempt() {}
}

namespace {
std::array<unsigned, 4> calls{};
void Original(PPCContext& ctx, unsigned index) {
  ++calls[index];
  // Every argument reaches the original unchanged. In particular completion's
  // r3 is a result address, not its screen (which is r4).
  CHECK_EQ(ctx.r3.u32, index == 2 ? 0x40 : 0x80);
  CHECK_EQ(ctx.r4.u32, index == 2 ? 0x80 : 0x1234);
  CHECK_EQ(ctx.r5.u32, 0x5678);
  CHECK_EQ(ctx.r6.u32, 1);
  CHECK_EQ(ctx.lr, 0x822C47BC);
  ctx.r3.u64 = 0x1122334455667788;
  ctx.r4.u64 = 0x8877665544332211;
  ctx.lr = 0x12345678;
}
}
extern "C" REX_FUNC(__imp__sub_822C3428) { Original(ctx, 0); }
extern "C" REX_FUNC(__imp__sub_822C3878) { Original(ctx, 1); }
extern "C" REX_FUNC(__imp__sub_822C38F8) { Original(ctx, 2); }
extern "C" REX_FUNC(__imp__sub_822C3F48) { Original(ctx, 3); }

int main() {
  using namespace rb_blitz::test;
  std::array<uint8_t, 512> memory{};
  rex::memory::store_and_swap<uint32_t>(memory.data() + 0x80 + 72, 3);
  const auto before = memory;
  using Hook = void (*)(PPCContext&, uint8_t*);
  const Hook hooks[] = {sub_822C3428, sub_822C3878, sub_822C38F8, sub_822C3F48};
  for (bool trace_on : {false, true}) {
    BeginCase(trace_on ? "trace forwards each original once and preserves results"
                       : "disabled observer forwards without touching guest memory");
    CHECK_TRUE(rex::cvar::SetFlagByName("song_load_trace", trace_on ? "true" : "false"));
    rb_blitz::loading::ConfigureLoadTrace();
    for (unsigned index = 0; index < 4; ++index) {
      PPCContext ctx{};
      ctx.r3.u32 = index == 2 ? 0x40 : 0x80;
      ctx.r4.u32 = index == 2 ? 0x80 : 0x1234;
      ctx.r5.u32 = 0x5678;
      ctx.r6.u32 = 1;
      ctx.lr = 0x822C47BC;
      const auto count = calls[index];
      hooks[index](ctx, memory.data());
      CHECK_EQ(calls[index], count + 1);
      CHECK_EQ(ctx.r3.u64, 0x1122334455667788);
      CHECK_EQ(ctx.r4.u64, 0x8877665544332211);
      CHECK_EQ(ctx.lr, 0x12345678);
    }
    CHECK_MEM_EQ(memory.data(), before.data(), before.size());
  }
  return Finish();
}
