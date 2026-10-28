// SPDX-License-Identifier: GPL-2.0-only
// P6: each aggregate enumerator is one measured phase, never an estimate of
// overall loading. The guest still mounts, parses and persists after enumeration.
//
// The phase is shown by the title's own UI (owner request, 2026-10-15): the connect panel
// carries `progress_bar.grp` / `content_percent.lbl` and a Flow that reads the panel's
// `current_content` / `total_content` properties, so this file drives *those* through the
// engine's own push (`sub_82759190`) instead of drawing a host overlay over the frame. The
// words on the screen are the title's own (its locale entries and its own saveload states);
// the host supplies numbers only. The numbers are the actual enumerators' item counts, summed
// over every batch of the attempt. The host stops pushing at the guest's own end-of-enumeration
// call (`SetTotal`): the engine then resets the same bar for its mounting phase and refills it
// with its own `Increment` calls (measured, out/_swap/dlc-native-bar/bar-2, 2026-10-15).
#include "hooks/content_progress.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xfile.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#if defined(_WIN32)
#include <windows.h>
REXCVAR_DECLARE(bool, enhancements_dlc_uncap_enumeration);
REX_EXTERN(__imp__sub_82759190);  // the engine's own push into the panel
REX_EXTERN(__imp__sub_82759308);
REX_EXTERN(__imp__sub_827593A8);
REX_EXTERN(__imp__sub_827593B8);
REX_EXTERN(__imp__sub_827593C8);

namespace {
using namespace rb_blitz::content_progress;
std::mutex g_mutex;
Model g_model;
// The engine's progress object (its constructor returns it: +4 panel, +8 current, +12
// total). Written once, on the guest thread that builds the panel; read on the thread that
// enumerates. Zero until then.
std::atomic<uint32_t> g_object{0};
// The last whole percent handed to the title's bar, under g_mutex; -1 = nothing pushed.
int g_pushed_percent = -1;
uint64_t g_native_increments = 0;
int64_t g_start_us = 0;
int64_t g_last_enum_us = 0;
uint64_t g_metadata_reads = 0, g_cache_reads = 0, g_cache_writes = 0;
int64_t g_metadata_us = 0, g_cache_read_us = 0, g_cache_write_us = 0;

int64_t NowMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
uint32_t Be32(const uint8_t* p) {
  return rex::memory::load_and_swap<uint32_t>(p);
}
// The guest is big-endian, and these fields are plain ints in its own image.
void StoreBe32(uint8_t* p, int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// Writes the two properties the title's Flow reads (`current_content` / `total_content`) and
// pushes them with the engine's own push. The push is a recompiled guest function, so it is
// called the way the engine calls it - object in r3 - but on a *copy* of the context, so the
// registers of the guest call this is nested inside survive. r1 is left alone: this runs on
// the guest thread, inside its own call, and a callee allocating below the caller's r1 is
// the ABI.
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
using ImportFunc = void (*)(PPCContext&, uint8_t*);
ImportFunc ResolveImport(const char* symbol) {
  const auto runtime = GetModuleHandleA("rexruntime.dll");
  return runtime ? reinterpret_cast<ImportFunc>(GetProcAddress(runtime, symbol)) : nullptr;
}
void Forward(const char* symbol, PPCContext& ctx, uint8_t* base) {
  // Each wrapper owns a static resolved function; no mutable cross-thread slot.
  const auto original = ResolveImport(symbol);
  if (original) original(ctx, base);
  else { ctx.r3.u32 = 0x80004005; REXLOG_ERROR("content_pipeline: missing import {}", symbol); }
}
bool g_uncap_enabled = false;

// kIdle -> kFreeing -> kFreed -> kRestoring -> kIdle, claimed by the guest thread and completed
// by the host thread that does the write. One transition at a time is the whole synchronisation,
// and a request that finds the machine in any other state is simply not wanted.
enum class Pacing { kIdle, kFreeing, kFreed, kRestoring };

std::atomic<Pacing> g_pacing{Pacing::kIdle};
std::mutex g_pacing_mutex;
std::string g_player_vsync;  // what to put back, under g_pacing_mutex, while kFreed
std::atomic<bool> g_restore_requested{false};
void RequestFrameRateRestore(const char* why);

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
  if (g_restore_requested.load()) RequestFrameRateRestore("completion raced pacing startup");
  REXLOG_INFO("content_progress: the frame rate is free for the discovery (vsync false, was {}); "
              "the player's own value goes back when the usable menu is ready",
              before);
}

// Host thread: put the player's value back.
void RestoreFrameRate() {
  std::string intended;
  {
    std::lock_guard<std::mutex> lock(g_pacing_mutex);
    intended = g_player_vsync;
  }
  if (intended.empty() || !rex::cvar::SetFlagByName("vsync", intended)) {
    REXLOG_WARN("content_progress: the frame rate could not be put back to vsync = {}; it stays "
                "free for this run",
                intended.empty() ? "unknown" : intended);
    g_pacing.store(Pacing::kIdle);
    return;
  }
  g_pacing.store(Pacing::kIdle);
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
  g_restore_requested.store(false);
  std::thread(FreeFrameRate).detach();
}

void RequestFrameRateRestore(const char* why) {
  g_restore_requested.store(true);
  Pacing freed = Pacing::kFreed;
  if (!g_pacing.compare_exchange_strong(freed, Pacing::kRestoring)) {
    return;
  }
  REXLOG_INFO("content_progress: {}; the frame rate goes back to the player's own", why);
  std::thread(RestoreFrameRate).detach();
}


void BeginAttempt() { // g_mutex held
  g_model.Begin();
  g_start_us = NowMicros();
  g_last_enum_us = 0;
  g_native_increments = 0;
  g_metadata_reads = g_cache_reads = g_cache_writes = 0;
  g_metadata_us = g_cache_read_us = g_cache_write_us = 0;
  g_pushed_percent = -1;
  REXLOG_INFO("content_pipeline: attempt {} started", g_model.Get().attempt);
  REXLOG_INFO("dlc_load: begin attempt={} pacing={}", g_model.Get().attempt,
              g_uncap_enabled);
  if (g_uncap_enabled) RequestFrameRateFree("content discovery started");
}

void WatchXmsg(PPCContext& ctx, uint8_t* base, const char* symbol, bool overlapped_call) {
  const bool enumerate = ctx.r3.u32 == 0xFE && ctx.r4.u32 == 0x2000E;
  const uint32_t data = overlapped_call ? ctx.r6.u32 : ctx.r5.u32;
  const uint32_t overlapped = overlapped_call ? ctx.r5.u32 : 0;
  uint32_t handle = 0, total = 0;
  if (enumerate && data) {
    const uint32_t extra = Be32(base + data + 8);
    if (extra) {
      handle = Be32(base + extra + 4);
      auto e = REX_KERNEL_STATE()->object_table()->LookupObject<rex::system::XEnumerator>(handle);
      // The aggregate app creates exactly XStaticEnumerator<XCONTENT_AGGREGATE_DATA>.
      if (e) total = static_cast<uint32_t>(
          static_cast<rex::system::XStaticUntypedEnumerator*>(e.get())->item_count());
    }
  }
  Forward(symbol, ctx, base);
  if (!enumerate) return;
  // This SDK dispatches the app synchronously even through the Async entry.
  // StartIORequestEx then returns IO_PENDING but stores the actual HRESULT in
  // XAM_OVERLAPPED.result (xam_msg.cpp / AppManager::DispatchMessageAsync).
  uint32_t result = ctx.r3.u32;
  if (overlapped && result == kPending) result = Be32(base + overlapped);
  uint32_t counted = 0, expected = 0;
  int percent = -1;
  {
    std::lock_guard lock(g_mutex);
    if (!g_model.Get().active()) BeginAttempt();
    const auto before = g_model.Get();
    g_model.Enumerated(handle, total, result);
    g_last_enum_us = NowMicros();
    const auto& progress = g_model.Get();
    if (progress.batch != before.batch || result != kSuccess || !progress.determinate()) {
      REXLOG_INFO("content_pipeline: enumeration attempt={} batch={} handle={:08X} "
                  "completed={}/{} result={:08X} elapsed_ms={:.3f}",
                  progress.attempt, progress.batch, handle, progress.current, progress.total,
                  result, (NowMicros() - g_start_us) / 1000.0);
    }
    // The title's bar: the Flow asks the panel for these two numbers, summed over every
    // batch of the attempt, so parallel walks read as one monotonic count.
    counted = g_model.ItemsCounted();
    expected = g_model.ItemsExpected();
    if (expected != 0) {
      const int moved = static_cast<int>((static_cast<uint64_t>(counted) * 100) / expected);
      if (moved != g_pushed_percent) {
        g_pushed_percent = moved;
        percent = moved;
      }
    }
  }
  if (percent < 0) return;
  PushProgress(ctx, base, counted, expected);
  if (percent == 0 || percent == 25 || percent == 50 || percent == 75 || percent == 100) {
    REXLOG_INFO("content_pipeline: the title's own bar is at {} of {} item(s) ({}%)", counted,
                expected, percent);
  }
}
} // namespace

namespace rb_blitz::content_progress {
void Configure() {
  // The bar and pacing policy remain truthful for every content root.
  g_uncap_enabled = REXCVAR_GET(enhancements_dlc_uncap_enumeration);
  REXLOG_INFO("content_pipeline: the title's own discovery bar follows the actual aggregate "
              "batches; pacing={}",
              g_uncap_enabled);
}
void Tick() {} // kept for the shell poll; completion is UIManager's verified panel

void ObserveScreen(std::string_view screen, std::string_view panel, bool available) {
  std::lock_guard lock(g_mutex);
  const bool discovery = panel == "server_connect_panel";
  if (discovery && !g_model.Get().active()) BeginAttempt();
  if (available && screen == "splash_screen" && panel == "splash_panel" &&
      g_model.MenuReady()) {
    const auto& p = g_model.Get();
    REXLOG_INFO("content_pipeline: usable menu attempt={} total_ms={:.3f} "
                "after_last_enumeration_ms={:.3f} last_batch={} successful={} failures={} "
                "metadata_reads={} metadata_io_ms={:.3f} songcache_reads={} read_ms={:.3f} "
                "songcache_writes={} write_ms={:.3f}",
                p.attempt, (NowMicros() - g_start_us) / 1000.0,
                g_last_enum_us ? (NowMicros() - g_last_enum_us) / 1000.0 : 0.0,
                p.batch, p.successful, p.failures, g_metadata_reads, g_metadata_us / 1000.0,
                g_cache_reads, g_cache_read_us / 1000.0, g_cache_writes, g_cache_write_us / 1000.0);
    REXLOG_INFO("dlc_load: end attempt={} total_ms={:.3f} "
                "after_last_enumeration_ms={:.3f} successful={} failures={}",
                p.attempt, (NowMicros() - g_start_us) / 1000.0,
                g_last_enum_us ? (NowMicros() - g_last_enum_us) / 1000.0 : 0.0,
                p.successful, p.failures);
    RequestFrameRateRestore("the usable main menu is current with no transition pending");
  }
}
IoObservation BeginIo(uint32_t handle, bool write) {
  auto file = REX_KERNEL_STATE()->object_table()->LookupObject<rex::system::XFile>(handle);
  if (!file) return {};
  const auto& name = file->name();
  int kind = name == "songcache" ? (write ? 3 : 2)
             : !write && (name == "songs.dta" || name == "songs.dtb") ? 1 : 0;
  if (!kind) return {};
  std::lock_guard lock(g_mutex);
  if (!g_model.Get().active()) {
    if (kind != 2) return {};
    BeginAttempt();
  }
  g_model.Finalizing();
  return {kind, NowMicros()};
}
void EndIo(IoObservation io, uint32_t status) {
  if (!io.kind) return;
  const auto elapsed = NowMicros() - io.start_us;
  std::lock_guard lock(g_mutex);
  if (status == 0) {
    if (io.kind == 1) { ++g_metadata_reads; g_metadata_us += elapsed; }
    if (io.kind == 2) { ++g_cache_reads; g_cache_read_us += elapsed; }
    if (io.kind == 3) { ++g_cache_writes; g_cache_write_us += elapsed; }
  }
  REXLOG_INFO("content_pipeline: guest_io kind={} result={:08X} elapsed_ms={:.3f}",
               io.kind, status, elapsed / 1000.0);
}
} // namespace rb_blitz::content_progress

// 0x82759308 (constructor): the object exists from here on and carries the panel the push
// writes into at +4, so this is where the host learns where to write. Returns the object in r3.
extern "C" REX_FUNC(sub_82759308) {
  __imp__sub_82759308(ctx, base);
  if (ctx.r3.u32 != 0) {
    g_object.store(ctx.r3.u32, std::memory_order_relaxed);
  }
}

// 0x827593A8 (SetTotal): the engine announcing a phase's total. It also zeroes the current
// count and pushes, so the Flow draws the bar from 0 again. This build calls it once, after the
// enumeration: measured with a total of 0 (2026-10-10, docs/plans/finished/
// 2026-10-10-song-cache-plan.md §9.4) and with 3546 - the enumeration's own count - on
// 2026-10-15. The Flow reads a non-positive total as "not enumerating ... -1", so only such an
// announcement is rewritten to -1; a positive one runs as the title wrote it. The host never
// re-pushes after it: the engine's own `Increment` calls refill the bar for the mounting phase.
extern "C" REX_FUNC(sub_827593A8) {
  uint32_t counted = 0, expected = 0;
  {
    std::lock_guard lock(g_mutex);
    counted = g_model.ItemsCounted();
    expected = g_model.ItemsExpected();
  }
  REXLOG_INFO("content_pipeline: the title announced a total of {} item(s) to enumerate (the "
              "host counted {} of {}); the bar returns to the title's own numbers",
              ctx.r4.s32, counted, expected);
  if (ctx.r4.s32 <= 0) {
    ctx.r4.s32 = -1;  // the Flow's own 'not enumerating' value
  }
  __imp__sub_827593A8(ctx, base);
}

// 0x827593B8 (Increment): one more item, in the engine's own bookkeeping. This build never
// calls it (same measurement), which is why the host counts the XAM messages itself; hooked
// so a build that does call it is still counted.
extern "C" REX_FUNC(sub_827593B8) {
  { std::lock_guard lock(g_mutex); ++g_native_increments; }
  __imp__sub_827593B8(ctx, base);
}

// 0x827593C8 (Complete): the engine says the enumeration is over. No push from here: this
// call sets current = total on the object, and with the -1 above the bar is hidden - the
// title's own calls own the panel until the next attempt.
extern "C" REX_FUNC(sub_827593C8) {
  {
    std::lock_guard lock(g_mutex);
    REXLOG_INFO("content_pipeline: guest subphase complete native_increments={}; the title "
                "owns the panel from here",
                g_native_increments);
  }
  __imp__sub_827593C8(ctx, base);
}
extern "C" REX_FUNC(__imp__XMsgStartIORequestEx) {
  WatchXmsg(ctx, base, "__imp__XMsgStartIORequestEx", true);
}
extern "C" REX_FUNC(__imp__XMsgInProcessCall) {
  WatchXmsg(ctx, base, "__imp__XMsgInProcessCall", false);
}
extern "C" REX_FUNC(__imp__XMsgSystemProcessCall) {
  WatchXmsg(ctx, base, "__imp__XMsgSystemProcessCall", false);
}
extern "C" REX_FUNC(__imp__NtWriteFile) {
  const auto io = rb_blitz::content_progress::BeginIo(ctx.r3.u32, true);
  const auto iosb = ctx.r7.u32;
  Forward("__imp__NtWriteFile", ctx, base);
  const auto result = iosb && !(ctx.r3.u32 & 0x80000000u) ? Be32(base + iosb) : ctx.r3.u32;
  rb_blitz::content_progress::EndIo(io, result);
}
#endif
