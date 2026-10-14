// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R7's progress bar, restored from the guest's own UI rather than added to it.
//
// The connect panel the title shows between the start press and the main menu
// (`ui/net/gen/server_connect.milo_xbox`) already contains the bar: a
// `progress_bar.grp`, a `content_percent.lbl`, and a Flow (`enumerating_content1.flow`)
// whose own comments read
//
//     (How much content we've enumerated so far
//     (How much content we need to enumerate, -1 if we're not enumerating right now
//
// The Flow reads two properties of the panel object, `current_content` and
// `total_content`, and shows `enumerating_content.grp` only while the total is not -1.
// Nothing in the DTA sets them (every .dtb in the ark was decoded and searched), so the
// engine's own C++ does - and it does it through one small object, recompiled here as
// `sub_82759308`..`sub_827593C8`:
//
//   sub_82759308(this, panel)  constructor; current = total = -1, not enumerating
//   sub_827593A8(this, total)  start: total = N, current = 0, then push
//   sub_827593B8(this)         one more item: ++current, then push
//   sub_827593C8(this)         done: current = total, then push
//   sub_82759190(this)         push: panel.current_content = this+8, panel.total_content = this+12
//
// **Why the bar was hidden, measured 2026-10-10** (docs/plans/finished/2026-10-10-song-cache-plan.md §9.4):
// this build's engine calls `SetTotal` exactly **once, after** the whole enumeration, with a
// total of **0** - which the Flow reads as "not enumerating", so it hides the bar - and
// `Increment` is **never called at all**. For the 46.8 s the discovery screen is up,
// `total_content` is still -1 and there is nothing to draw. Handing `SetTotal` a total
// cannot fix that: by the time it runs the enumeration is over.
//
// **What this file does instead.** The enumeration is 2,807 XAM app messages, one per
// presented frame (`XamAppEnumerateContentAggregate`, message 0x0002000E, handled by
// rexglue-sdk's `xam_app.cpp`). The guest reaches that handler through imports it really has
// - `XMsgStartIORequestEx`, `XMsgInProcessCall`, `XMsgSystemProcessCall` - so this file counts
// the items as they are enumerated and pushes the count into the panel with the engine's own
// push (`sub_82759190`). No SDK source is touched; the engine's object and its push are used
// exactly as the engine uses them, and the host only supplies the numbers it alone can know.
//
// The total is `2 x ContentItemCount()`: measured, the title drains two enumerators of the
// library plus one empty one, and each of those makes one terminating call
// (2 x 1,402 + 3 = 2,807 calls for 1,402 packages). The extras are clamped, so the bar is
// monotonic to 100% across the screen.
//
// Hook hygiene (docs/reference/rb3-references.md §8): the faithful behaviour, and the deviation.
//
// Faithful behaviour: with the toggle off (its default) each hook calls the guest's own
// function unchanged and touches no guest state, so the boot is the boot this project had
// before the bar existed. That is also the behaviour with no DLC library registered:
// there is no total to give, so there is nothing to deviate with.
//
// Deviation: with R7 on and a library registered, `sub_827593A8`'s total argument is
// replaced when the engine announces a non-positive one (an unknown total), and
// `sub_827593B8` gives the object the library's count before its first increment if the
// object still has none. Both are the count of packages the content manager will hand
// the title, which is exactly what the Flow wants, and both are logged with the value the
// engine chose, so a boot says which of the two paths was taken.

#include "hooks/content_progress.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/dlc.h"

#if defined(_WIN32)

#include <windows.h>

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
REXCVAR_DECLARE(bool, enhancements_dlc_cache);
REXCVAR_DECLARE(bool, enhancements_dlc_uncap_enumeration);

// The engine's own object, recompiled: its constructor, the setter trio, and the push.
REX_EXTERN(__imp__sub_82759308);
REX_EXTERN(__imp__sub_827593A8);
REX_EXTERN(__imp__sub_827593B8);
REX_EXTERN(__imp__sub_827593C8);
REX_EXTERN(__imp__sub_82759190);

namespace {

// Read once in Configure(); guest threads only read it afterwards.
bool g_enabled = false;

// The guest is big-endian, and these fields are plain ints in its own image.
int32_t LoadBe32(const uint8_t* p) {
  return static_cast<int32_t>((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                              (uint32_t(p[2]) << 8) | uint32_t(p[3]));
}

void StoreBe32(uint8_t* p, int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// The object the engine builds for the discovery screen. Written once, on the guest thread
// that builds the panel; read on the thread that enumerates. Zero until then.
std::atomic<uint32_t> g_object{0};

// How many items the host has counted, and the percent it last pushed. Atomic because the
// app message is dispatched from the emulated console's own threads and a boot shows more
// than one of them carrying it (measured: five different thread ids over one enumeration),
// even though the calls arrive one per frame.
std::atomic<uint32_t> g_counted{0};
std::atomic<int> g_pushed_percent{-1};

// The XAM app the content enumerator belongs to, and the message the title sends for one
// item (`rexglue-sdk/src/kernel/xam/apps/xam_app.cpp`, case 0x0002000E).
constexpr uint32_t kXamAppId = 0xFE;
constexpr uint32_t kEnumerateContentAggregate = 0x0002000E;

// The tree's XMsg entry points the guest reaches that handler through. All three take the
// message in r4; which one a title routes its own calls through is a property of its code,
// so all three are watched rather than guessed.
using ImportFunc = void (*)(PPCContext&, uint8_t*);

ImportFunc ResolveImport(const char* symbol) {
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  return runtime != nullptr ? reinterpret_cast<ImportFunc>(GetProcAddress(runtime, symbol))
                            : nullptr;
}

void Forward(ImportFunc* slot, const char* symbol, PPCContext& ctx, uint8_t* base) {
  if (*slot == nullptr) {
    *slot = ResolveImport(symbol);
    if (*slot == nullptr) {
      REXLOG_ERROR("content_progress: cannot resolve {} from rexruntime.dll; the call is dropped",
                   symbol);
      return;
    }
  }
  (*slot)(ctx, base);
}

// Writes the two properties the Flow reads and pushes them with the engine's own push.
//
// The push is a recompiled guest function, so it is called the way the engine calls it -
// with the object in r3 - but on a *copy* of the context, so the registers of the guest
// call this is nested inside survive. r1 is left alone: this runs on the guest thread,
// inside its own call, and a callee allocating below the caller's r1 is the ABI.
void PushProgress(PPCContext& ctx, uint8_t* base, uint32_t current, uint32_t total) {
  const uint32_t object = g_object.load(std::memory_order_relaxed);
  if (object == 0) {
    return;
  }
  uint8_t* memory = rex::memory::GuestPtr<uint8_t*>(base, object);
  if (memory == nullptr) {
    return;
  }
  StoreBe32(memory + 8, static_cast<int32_t>(current));
  StoreBe32(memory + 12, static_cast<int32_t>(total));
  PPCContext call = ctx;
  call.r3.u64 = object;
  __imp__sub_82759190(call, base);
}

// R7's frame pacing for the discovery (`enhancements_dlc_uncap_enumeration`).
//
// The title's enumeration advances one item per *presented* frame: 2,807 items over 46.7 s at
// 60 Hz, 7.0 ms per item and 19.7 s with the frame rate free (measured, docs/guides/dlc.md §4.3). The
// value that decides the frame rate is the SDK's own `vsync` cvar - the launcher's Graphics ▸
// Window row, which belongs to the player - so this asks for it to be turned down for exactly as
// long as the enumeration lasts and puts the player's value back at the first of three signs that
// it is over: the last item counted, the engine's own completion, or a menu frame with no item
// counted for two seconds.
//
// Neither the read nor the write happens on the thread that counts items: this project's rule is
// that a guest thread never touches the cvar registry (the rule `Configure()` obeys), so each
// request goes to a short-lived host thread, the way the refresh's scan is moved off the guest
// thread (src/hooks/dlc.cpp). A boot asks twice, 46.7 s apart, so a detached thread each is
// cheaper than a worker that idles for the whole run.
bool g_uncap_enabled = false;

// kIdle -> kFreeing -> kFreed -> kRestoring -> kIdle, claimed by the guest thread and completed
// by the host thread that does the write. One transition at a time is the whole synchronisation,
// and a request that finds the machine in any other state is simply not wanted.
enum class Pacing { kIdle, kFreeing, kFreed, kRestoring };

std::atomic<Pacing> g_pacing{Pacing::kIdle};
std::mutex g_pacing_mutex;
std::string g_player_vsync;  // what to put back, under g_pacing_mutex, while kFreed
std::atomic<int64_t> g_last_item_ms{0};

// The idle bound for the third sign. Items arrive every 16.66 ms while the discovery runs (and
// thousands of times a second on a fast machine), so two seconds of silence is an enumeration
// that has ended - and the menu frame this is asked from is one only reached after it has.
constexpr int64_t kEnumerationIdleMillis = 2000;

int64_t NowMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// `vsync` is defined in rexruntime.dll's own GPU category, so it is read and written by name:
// REXCVAR_GET would need a storage symbol this module does not define.
bool PacingIsFree(const std::string& value) { return value == "false" || value == "0"; }

// Host thread: turn the frame rate down, remembering the player's own value.
void FreeFrameRate() {
  const std::string before = rex::cvar::GetFlagByName("vsync");
  if (PacingIsFree(before)) {
    REXLOG_INFO("content_progress: the discovery is frame-paced and the frame rate is already "
                "free (vsync = {}); the phase runs as it is",
                before.empty() ? "unknown" : before);
    g_pacing.store(Pacing::kIdle);
    return;
  }
  if (before.empty() || !rex::cvar::SetFlagByName("vsync", "false")) {
    REXLOG_WARN("content_progress: could not free the frame rate for the discovery (vsync = "
                "{}); the phase runs at the player's own frame rate",
                before.empty() ? "unknown" : before);
    g_pacing.store(Pacing::kIdle);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_pacing_mutex);
    g_player_vsync = before;
  }
  g_pacing.store(Pacing::kFreed);
  REXLOG_INFO("content_progress: the frame rate is free for the discovery (vsync false, was {}); "
              "the player's own value goes back when the enumeration ends",
              before);
}

// Host thread: put the player's value back.
void RestoreFrameRate() {
  std::string intended;
  {
    std::lock_guard<std::mutex> lock(g_pacing_mutex);
    intended = g_player_vsync;
  }
  g_pacing.store(Pacing::kIdle);
  if (intended.empty() || !rex::cvar::SetFlagByName("vsync", intended)) {
    REXLOG_WARN("content_progress: the frame rate could not be put back to vsync = {}; it stays "
                "free for this run",
                intended.empty() ? "unknown" : intended);
    return;
  }
  REXLOG_INFO("content_progress: the frame rate is the player's own again (vsync = {})", intended);
}

// The request side, always on the guest thread. Each is logged only when it is the one that won
// the transition, so a boot says once why the frame rate moved and once why it went back.
void RequestFrameRateFree(const char* why) {
  Pacing idle = Pacing::kIdle;
  if (!g_pacing.compare_exchange_strong(idle, Pacing::kFreeing)) {
    return;
  }
  REXLOG_INFO("content_progress: {}; the host lets the frame rate run free for the discovery "
              "(R7's frame pacing)",
              why);
  std::thread(FreeFrameRate).detach();
}

void RequestFrameRateRestore(const char* why) {
  Pacing freed = Pacing::kFreed;
  if (!g_pacing.compare_exchange_strong(freed, Pacing::kRestoring)) {
    return;
  }
  REXLOG_INFO("content_progress: {}; the frame rate goes back to the player's own", why);
  std::thread(RestoreFrameRate).detach();
}

// One item was enumerated. Count it, and push when a whole percent moves, so the panel is
// written ~100 times over the screen rather than once per frame.
void CountItem(PPCContext& ctx, uint8_t* base) {
  if (!g_enabled) {
    return;
  }
  const std::size_t items = rb_blitz::dlc::ContentItemCount();
  if (items == 0) {
    return;
  }
  // Two passes over the library, plus one terminating call per enumerator (see the file
  // comment), so the extras are clamped instead of allowed past 100%.
  const uint32_t total = static_cast<uint32_t>(items) * 2;
  const uint32_t counted = std::min(g_counted.fetch_add(1) + 1, total);
  g_last_item_ms.store(NowMillis(), std::memory_order_relaxed);
  if (counted == 1 && g_uncap_enabled) {
    RequestFrameRateFree("the title is enumerating one item per presented frame");
  }
  const int percent = static_cast<int>((static_cast<uint64_t>(counted) * 100) / total);
  if (percent != g_pushed_percent.load(std::memory_order_relaxed)) {
    g_pushed_percent.store(percent, std::memory_order_relaxed);
    PushProgress(ctx, base, counted, total);
    if (percent == 0 || percent == 25 || percent == 50 || percent == 75 || percent == 100) {
      REXLOG_INFO("content_progress: the discovery bar is at {} of {} item(s) ({}%)", counted,
                  total, percent);
    }
  }
  if (counted >= total) {
    RequestFrameRateRestore("the last item of the enumeration was counted");
  }
}

// All three entry points are the same hook: the app is r3, the message is r4, and the
// counting happens after the handler has run, so the item is already in the guest's buffer.
void WatchXmsg(PPCContext& ctx, uint8_t* base, const char* symbol, ImportFunc* slot) {
  const uint32_t app = ctx.r3.u32;
  const uint32_t message = ctx.r4.u32;
  Forward(slot, symbol, ctx, base);
  if (app == kXamAppId && message == kEnumerateContentAggregate) {
    CountItem(ctx, base);
  }
}

}  // namespace

namespace rb_blitz::content_progress {

void Configure() {
  g_enabled = REXCVAR_GET(enhancements_dlc_cache);
  g_uncap_enabled = g_enabled && REXCVAR_GET(enhancements_dlc_uncap_enumeration);
  if (g_enabled) {
    REXLOG_INFO("content_progress: the discovery screen's progress bar is driven by the host "
                "while the title enumerates (R7); set --enhancements_dlc_cache=false to leave "
                "the bar exactly as the title draws it");
  } else {
    REXLOG_INFO("content_progress: R7 off; the discovery screen's progress bar is the "
                "title's own");
  }
  if (g_uncap_enabled) {
    REXLOG_INFO("content_progress: R7's frame pacing is on for the discovery: the frame rate "
                "runs free while the title enumerates and the player's own value goes back when "
                "it ends (--enhancements_dlc_uncap_enumeration=false leaves it alone)");
  }
}

void Tick() {
  if (g_pacing.load(std::memory_order_relaxed) != Pacing::kFreed) {
    return;
  }
  const int64_t last = g_last_item_ms.load(std::memory_order_relaxed);
  if (last == 0 || NowMillis() - last < kEnumerationIdleMillis) {
    return;
  }
  RequestFrameRateRestore(
      "a menu frame arrived with no content item counted for two seconds, so the discovery is "
      "over");
}

}  // namespace rb_blitz::content_progress

// 0x82759308 (constructor): the object exists from here on and carries the panel it pushes
// into at +4, so this is where the item hook learns where to write. The constructor returns
// the object in r3.
extern "C" REX_FUNC(sub_82759308) {
  __imp__sub_82759308(ctx, base);
  if (g_enabled && ctx.r3.u32 != 0) {
    g_object.store(ctx.r3.u32, std::memory_order_relaxed);
  }
}

// 0x827593A8 (SetTotal): the engine announcing how many items it is about to enumerate.
// A non-positive total is "not enumerating" to the Flow that draws the bar, so with R7 on
// the host's own count stands in for it. The host drives the bar per item (CountItem), and
// the engine's start also zeroes the current count - so if the host has already counted
// something, its numbers are put back straight after this, or the bar would jump back.
extern "C" REX_FUNC(sub_827593A8) {
  const std::size_t known = g_enabled ? rb_blitz::dlc::ContentItemCount() * 2 : 0;
  if (g_enabled) {
    REXLOG_INFO("content_progress: the title announced a total of {} item(s) to enumerate "
                "({} package(s) known to this build)",
                ctx.r4.s32, rb_blitz::dlc::ContentItemCount());
    if (ctx.r4.s32 <= 0 && known != 0) {
      ctx.r4.s64 = static_cast<int64_t>(known);
      REXLOG_INFO("content_progress: the total was not known to the title; the discovery "
                  "bar is given {} instead",
                  known);
    }
  }
  __imp__sub_827593A8(ctx, base);
  const uint32_t counted = g_counted.load(std::memory_order_relaxed);
  if (g_enabled && counted != 0) {
    PushProgress(ctx, base, counted, static_cast<uint32_t>(known));
  }
}

// 0x827593B8 (Increment): one more item enumerated. This build's engine never calls it
// (measured - docs/plans/finished/2026-10-10-song-cache-plan.md §9.4), which is why the host counts the items
// itself; left hooked so the faithful path stays the engine's own function and so a build
// that does call it still gets a total.
extern "C" REX_FUNC(sub_827593B8) {
  if (g_enabled) {
    const std::size_t known = rb_blitz::dlc::ContentItemCount();
    if (known != 0) {
      if (uint8_t* object = rex::memory::GuestPtr<uint8_t*>(base, ctx.r3.u32)) {
        if (LoadBe32(object + 12) <= 0) {
          StoreBe32(object + 12, static_cast<int32_t>(known) * 2);
          REXLOG_INFO("content_progress: the title had no total to enumerate; the discovery "
                      "bar is given {} on its first item",
                      known * 2);
        }
      }
    }
  }
  __imp__sub_827593B8(ctx, base);
}

// 0x827593C8 (Complete): the enumeration finished, so the Flow draws a full bar. The
// engine's own numbers are left to run - current = total is exactly the full bar - and the
// host's counter is reset for whatever enumerates next.
extern "C" REX_FUNC(sub_827593C8) {
  if (g_enabled) {
    REXLOG_INFO("content_progress: the title's content enumeration finished; the discovery "
                "bar is drawn full");
    g_counted.store(0, std::memory_order_relaxed);
    g_pushed_percent.store(-1, std::memory_order_relaxed);
    RequestFrameRateRestore("the title's own completion ran");
  }
  __imp__sub_827593C8(ctx, base);
}

// The per-item count: the XAM app messages the enumeration is made of. All three entry
// points are watched, because which one the title's own code routes them through is a
// property of that code; the message id is what makes a call one of ours.
extern "C" REX_FUNC(__imp__XMsgStartIORequestEx) {
  static ImportFunc original = nullptr;
  WatchXmsg(ctx, base, "__imp__XMsgStartIORequestEx", &original);
}

extern "C" REX_FUNC(__imp__XMsgInProcessCall) {
  static ImportFunc original = nullptr;
  WatchXmsg(ctx, base, "__imp__XMsgInProcessCall", &original);
}

extern "C" REX_FUNC(__imp__XMsgSystemProcessCall) {
  static ImportFunc original = nullptr;
  WatchXmsg(ctx, base, "__imp__XMsgSystemProcessCall", &original);
}

#endif  // _WIN32
