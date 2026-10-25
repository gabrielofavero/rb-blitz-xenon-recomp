// SPDX-License-Identifier: GPL-2.0-only
// Exercise the production native hooks; the scan and retail callees are stand-ins.
#include "check.h"
#include "hooks/dlc.h"
#include "graphics/preparation_loading.h"
#include <array>
#include <rex/hook.h>
#include <rex/system/xmemory.h>

REX_EXTERN(sub_822CA460);
REX_EXTERN(sub_822CB618);

namespace {
constexpr uint32_t kPanel = 64;
bool ready = false;
unsigned advances = 0, polls = 0, readiness_checks = 0;

void State(uint8_t* base, uint32_t state) {
  rex::memory::store_and_swap<uint32_t>(base + kPanel + 56, state);
}
}  // namespace

namespace rb_blitz::dlc {
bool RefreshArmed() { return false; }
void RefreshConfigured() {}
void PublishPendingRefresh() {}
bool RefreshReadyForDiscovery() {
  ++readiness_checks;
  return ready;
}
}  // namespace rb_blitz::dlc

namespace rb_blitz::graphics {
void ObserveContentLoading(uint8_t*, uint32_t panel) { CHECK_EQ(panel,kPanel); }
void PollLoading() {}
bool GraphicsLoadingPending() { return false; }
}
extern "C" REX_FUNC(__imp__sub_824B5F68) {
  rb_blitz::test::Fail(__FILE__,__LINE__,"graphics base poll without a pending gate");
}

extern "C" REX_FUNC(__imp__sub_822CA460) {
  ++advances;
  CHECK_EQ(ctx.r3.u32, kPanel);
  State(base, 29);
  // The continuation needs the caller's live native context, including registers
  // absent from rex::CallFrame. The old deferred side call failed these checks.
  CHECK_EQ(ctx.r31.u64, 0x81234560);
  CHECK_EQ(ctx.lr, 0x824B3000);
  // Native functions may clobber volatile registers; the guest caller reloads
  // arguments before its next poll.
  ctx.r3.u64 = 0;
  ctx.r4.u64 = 0;
}

extern "C" REX_FUNC(__imp__sub_822CB618) {
  ++polls;
  CHECK_EQ(ctx.r3.u32, kPanel);
  CHECK_EQ(ctx.r4.u32, 1234);
}

int main() {
  using namespace rb_blitz::test;
  std::array<uint8_t, 256> memory{};
  PPCContext ctx{};
  ctx.r3.u32 = kPanel;
  ctx.r4.u32 = 1234;
  ctx.r31.u64 = 0x81234560;
  ctx.lr = 0x824B3000;
  State(memory.data(), 28);

  BeginCase("discovery yields every frame while scanning, including a prolonged scan");
  sub_822CA460(ctx, memory.data());
  CHECK_EQ(advances, 0);
  for (unsigned frame = 0; frame < 2000; ++frame) {
    sub_822CB618(ctx, memory.data());
  }
  CHECK_EQ(advances, 0);
  CHECK_EQ(polls, 2000);
  CHECK_EQ(rex::memory::load_and_swap<uint32_t>(memory.data() + kPanel + 56), 28);

  BeginCase("the ready result resumes with the live context and polls the new state next frame");
  ready = true;
  sub_822CB618(ctx, memory.data());
  CHECK_EQ(advances, 1);
  CHECK_EQ(polls, 2000);  // no second native state operation in the resume frame
  ctx.r3.u32 = kPanel;
  ctx.r4.u32 = 1234;
  sub_822CB618(ctx, memory.data());
  CHECK_EQ(advances, 1);
  CHECK_EQ(polls, 2001);

  BeginCase("leaving the deferred state abandons its continuation");
  ready = false;
  State(memory.data(), 28);
  sub_822CB618(ctx, memory.data());
  State(memory.data(), 0);
  ready = true;
  const auto before = readiness_checks;
  sub_822CB618(ctx, memory.data());
  CHECK_EQ(advances, 1);
  CHECK_EQ(readiness_checks, before);

  BeginCase("unrelated native advances do not wait on a refresh");
  ready = false;
  sub_822CA460(ctx, memory.data());
  CHECK_EQ(advances, 2);
  CHECK_EQ(readiness_checks, before);
  return Finish();
}
