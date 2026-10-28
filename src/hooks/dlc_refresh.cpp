// SPDX-License-Identifier: GPL-2.0-only
// The guest's native content discovery route. A subsequent visit scans the flat
// host library on a worker, then resumes the title before its enumerators snapshot it.
#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/types.h>

#include "hooks/dlc.h"
#include "graphics/preparation_loading.h"

#if defined(_WIN32)
REX_EXTERN(__imp__sub_822CA460);
REX_EXTERN(__imp__sub_822CB618);
REX_EXTERN(__imp__sub_824B5F68);

namespace {
uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
}  // namespace

// Native state 28 is kServerConnectPanel_StartEnumeratingContent (+56).
// Its advance at 0x822CA754 calls ContentMgr::Refresh through vtable +100,
// creating enumerators that snapshot the host library, then sets state 29.
// Defer that one side effect until the worker result is published. The rest of
// the UI keeps polling/drawing; no guest stack or panel pointer is retained.
std::atomic<bool> g_seen_discovery{false};
std::atomic<bool> g_state28_started{false};

extern "C" REX_FUNC(sub_822CA460) {
  const bool enumerating = Be32(base + ctx.r3.u32 + 56) == 28;
  if (!enumerating) g_state28_started.store(false);
  if (enumerating && !g_state28_started.exchange(true) && g_seen_discovery.exchange(true)) {
    // The initial boot already scanned in Configure(). Each later native pass
    // refreshes the host list before the title creates its aggregate enumerators.
    rb_blitz::dlc::RefreshConfigured();
  }
  if (enumerating && !rb_blitz::dlc::RefreshReadyForDiscovery()) {
    REXLOG_INFO("dlc: discovery is waiting for the library scan; yielding to the next frame");
    return;
  }
  __imp__sub_822CA460(ctx, base);
}

extern "C" REX_FUNC(sub_822CB618) {
  const uint32_t panel = ctx.r3.u32;
  if (Be32(base + panel + 56) != 28) g_state28_started.store(false);
  // Retail has no poll branch for state 28: advance normally runs synchronously
  // from set_state. Retry it here, only while this very panel is still in 28.
  // Leaving/unloading the screen therefore cancels the continuation naturally.
  if (Be32(base + ctx.r3.u32 + 56) == 28 &&
      rb_blitz::dlc::RefreshReadyForDiscovery()) {
    // Resume as this frame's native operation on the live guest context. A
    // CallFrame copies only r1/r13/FPSCR: its uninitialized nonvolatile registers
    // and LR must not enter this native continuation or its callbacks. Returning
    // here also avoids polling the state we just entered in the same frame.
    REXLOG_INFO("dlc: the fresh library is ready; resuming native discovery");
    __imp__sub_822CA460(ctx, base);
    return;
  }
  // P6: exactly the content-ready predicate the native state-29 poll uses,
  // after the required save/global-options sequence. Enumeration totals are
  // insufficient. Latch before native advance can reenter R3's continuation.
  rb_blitz::graphics::ObserveContentLoading(base,panel);
  __imp__sub_822CB618(ctx, base);
  rb_blitz::graphics::PollLoading();
  if (rb_blitz::graphics::GraphicsLoadingPending()) {
    PPCContext call = ctx;
    call.r1.u32 -= 256;
    call.r3.u32 = panel;
    __imp__sub_824B5F68(call,base);
  }
}

#endif  // _WIN32
