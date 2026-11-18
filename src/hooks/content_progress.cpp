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
#include "settings/runtime_values.h"
#include "hooks/content_progress.h"
#include "diag/boot_heartbeat.h"
#include "settings/boot.h"
#include "platform/runtime_symbol.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <rex/ui/ui_system.h>
#include "settings/runtime_settings.h"
#include <rex/ppc/hook.h>
#include <rex/logging/log_category.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/object/file_object.h>
#include <rex/filesystem/file.h>
#include <rex/xam/enumeration_info.h>
#include <rex/ppc/call.h>
#include <rex/memory/endian.h>
#include <rex/memory/mapped_ptr.h>
#include <rex/types.h>


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
int g_delivered_percent = -1;
bool g_guest_owns_panel = false;
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

// Update the authored Flow properties and call its push through an isolated ABI frame.
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
  rex::ppc::Call<void>(ctx, base, __imp__sub_82759190, object);
}
using ImportFunc = void (*)(PPCContext&, uint8_t*);
ImportFunc ResolveImport(const char* symbol) {
  return reinterpret_cast<ImportFunc>(rb_blitz::platform::ResolveRuntimeSymbol(symbol));
}
void Forward(const char* symbol, PPCContext& ctx, uint8_t* base) {
  // Each wrapper owns a static resolved function; no mutable cross-thread slot.
  const auto original = ResolveImport(symbol);
  if (original) original(ctx, base);
  else { ctx.r3.u32 = 0x80004005; REXLOG_ERROR("content_pipeline: missing import {}", symbol); }
}
std::atomic<bool> g_uncap_enabled{false};
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_generation{0};

// kIdle -> kFreeing -> kFreed -> kRestoring -> kIdle, claimed by the guest thread and completed
// by the UI task that performs the write. One transition at a time is the whole synchronisation,
// and a request that finds the machine in any other state is simply not wanted.
enum class Pacing { kIdle, kFreeing, kFreed, kRestoring };

std::atomic<Pacing> g_pacing{Pacing::kIdle};
std::mutex g_pacing_mutex;
rex::ui::IUISystem* g_ui = nullptr;  // Cleared before the app releases its UI service.
std::string g_player_vsync;  // what to put back, under g_pacing_mutex, while kFreed
std::atomic<bool> g_restore_requested{false};
void RequestFrameRateRestore(const char* why);

// `vsync` is defined by the SDK GPU settings owner, so it is read and written by name:
// REXCVAR_GET would need a storage symbol this module does not define.
bool PacingIsFree(const std::string& value) { return value == "false" || value == "0"; }

// UI thread: turn the frame rate down, remembering the player's own value.
void FreeFrameRate() {
  const std::string before = rb_blitz::settings::InspectRuntimeSetting("vsync").active_value;
  if (PacingIsFree(before)) {
    REXLOG_INFO("content_progress: the discovery is frame-paced and the frame rate is already "
                "free (vsync = {}); the phase runs as it is",
                before.empty() ? "unknown" : before);
    g_pacing.store(Pacing::kIdle);
    return;
  }
  if (before.empty() || !rb_blitz::settings::WriteRuntimeSetting("vsync", "false", rex::SettingLayer::kRuntime).accepted() ||
      !PacingIsFree(rb_blitz::settings::InspectRuntimeSetting("vsync").active_value)) {
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

// UI thread: put the player's value back.
void RestoreFrameRate() {
  std::string intended;
  {
    std::lock_guard<std::mutex> lock(g_pacing_mutex);
    intended = g_player_vsync;
  }
  if (intended.empty() || !rb_blitz::settings::WriteRuntimeSetting("vsync", intended, rex::SettingLayer::kRuntime).accepted() ||
      rb_blitz::settings::InspectRuntimeSetting("vsync").active_value != intended) {
    REXLOG_WARN("content_progress: the frame rate could not be put back to vsync = {}; it stays "
                "free for this run",
                intended.empty() ? "unknown" : intended);
    g_pacing.store(Pacing::kIdle);
    return;
  }
  {
    std::lock_guard lock(g_pacing_mutex);
    g_player_vsync.clear();
  }
  g_pacing.store(Pacing::kIdle);
  REXLOG_INFO("content_progress: the frame rate is the player's own again (vsync = {})", intended);
}

// Queue setting writes on the owning UI thread. A closing or later launch retires
// queued requests before they can touch settings from another content attempt.
void SchedulePacing(bool free) {
  std::lock_guard lock(g_pacing_mutex);
  if (!g_active.load() || !g_ui) return;
  const auto generation = g_generation.load();
  g_ui->PostTask([generation, free] {
    if (!g_active.load() || generation != g_generation.load()) return;
    if (free) FreeFrameRate(); else RestoreFrameRate();
  });
}

void ScheduleAsyncCompilationRestore() {
  std::lock_guard lock(g_pacing_mutex);
  if (!g_active.load() || !g_ui) return;
  const auto generation = g_generation.load();
  g_ui->PostTask([generation] {
    if (!g_active.load() || generation != g_generation.load()) return;
    if (auto* settings = rb_blitz::settings::boot::SettingsBoot::Current())
      settings->RestoreAsyncShaderCompilation();
  });
}

// The request side, always on the guest thread. Each is logged only when it is the one that won
// the transition, so a boot says once why the frame rate moved and once why it went back.
void RequestFrameRateFree(const char* why) {
  if (!g_active.load()) return;
  Pacing idle = Pacing::kIdle;
  if (!g_pacing.compare_exchange_strong(idle, Pacing::kFreeing)) {
    return;
  }
  REXLOG_INFO("content_progress: {}; the host lets the frame rate run free for the discovery "
              "(temporary frame pacing)",
              why);
  g_restore_requested.store(false);
  SchedulePacing(true);
}

void RequestFrameRateRestore(const char* why) {
  if (!g_active.load()) return;
  g_restore_requested.store(true);
  Pacing freed = Pacing::kFreed;
  if (!g_pacing.compare_exchange_strong(freed, Pacing::kRestoring)) {
    return;
  }
  REXLOG_INFO("content_progress: {}; the frame rate goes back to the player's own", why);
  SchedulePacing(false);
}


void BeginAttempt() { // g_mutex held
  rb_blitz::diag::boot_heartbeat::Phase("content_discovery");
  g_model.Begin();
  g_start_us = NowMicros();
  g_last_enum_us = 0;
  g_native_increments = 0;
  g_metadata_reads = g_cache_reads = g_cache_writes = 0;
  g_metadata_us = g_cache_read_us = g_cache_write_us = 0;
  g_pushed_percent = -1;
  g_delivered_percent = -1;
  g_guest_owns_panel = false;
  REXLOG_INFO("content_pipeline: attempt {} started", g_model.Get().attempt);
  REXLOG_INFO("dlc_load: begin attempt={} pacing={}", g_model.Get().attempt,
              g_uncap_enabled.load());
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
      if (auto* info = REX_INTERFACE(rex::xam::IContentEnumerationInfo)) {
        const auto count = info->GetAggregateItemCount(handle);
        if (count && *count <= UINT32_MAX) total = static_cast<uint32_t>(*count);
      }
    }
  }
  Forward(symbol, ctx, base);
  if (!enumerate || !g_active.load()) return;
  // This SDK dispatches the app synchronously even through the Async entry.
  // StartIORequestEx then returns IO_PENDING but stores the actual HRESULT in
  // XAM_OVERLAPPED.result (xam_msg.cpp / AppManager::DispatchMessageAsync).
  uint32_t result = ctx.r3.u32;
  if (overlapped && result == kPending) result = Be32(base + overlapped);
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
    const auto counted = g_model.ItemsCounted();
    const auto expected = g_model.ItemsExpected();
    if (expected != 0) {
      g_pushed_percent = static_cast<int>((static_cast<uint64_t>(counted) * 100) / expected);
    }
  }
}
} // namespace

namespace rb_blitz::content_progress {
void Close() {
  rex::ui::IUISystem* ui;
  {
    std::lock_guard lock(g_pacing_mutex);
    ui = g_ui;
  }
  if (ui && !ui->IsUIThread()) {
    if (!ui->RunSynchronous([] { Close(); }))
      REXLOG_WARN("content_progress: UI stopped before pacing cleanup could run");
    return;
  }
  bool restore = false;
  {
    std::lock_guard lock(g_pacing_mutex);
    g_active.store(false);
    g_generation.fetch_add(1);
    g_ui = nullptr;
    restore = !g_player_vsync.empty();
  }
  if (restore) RestoreFrameRate();
  g_pacing.store(Pacing::kIdle);
  g_restore_requested.store(false);
  g_uncap_enabled.store(false);
  g_object.store(0);
  std::lock_guard lock(g_mutex);
  g_model = Model{};
  g_pushed_percent = g_delivered_percent = -1;
  g_guest_owns_panel = false;
}

void Configure() {
  Close();
  auto* ui = REX_INTERFACE(rex::ui::IUISystem);
  {
    std::lock_guard lock(g_pacing_mutex);
    g_ui = ui;
    g_generation.fetch_add(1);
    g_active.store(true);
  }
  g_uncap_enabled.store(ui && rb_blitz::settings::title::enhancements_dlc_uncap_enumeration.Get());
  REXLOG_INFO("content_pipeline: the title's own discovery bar follows the actual aggregate "
              "batches; pacing={}", g_uncap_enabled.load());
}
void Tick(PPCContext& ctx, uint8_t* base) {
  if (!g_active.load()) return;
  uint32_t counted = 0, expected = 0;
  int percent = -1;
  {
    std::lock_guard lock(g_mutex);
    if (g_guest_owns_panel || g_pushed_percent < 0 ||
        g_pushed_percent == g_delivered_percent) {
      return;
    }
    counted = g_model.ItemsCounted();
    expected = g_model.ItemsExpected();
    percent = g_pushed_percent;
    g_delivered_percent = percent;
  }
  // The enumeration callbacks run on workers. Only the main guest poll may
  // update the title's Flow object while its UI is being traversed.
  PushProgress(ctx, base, counted, expected);
  if (percent == 0 || percent == 25 || percent == 50 || percent == 75 || percent == 100) {
    REXLOG_INFO("content_pipeline: the title's own bar is at {} of {} item(s) ({}%)", counted,
                expected, percent);
  }
}

void ObserveScreen(std::string_view screen, std::string_view panel, bool available) {
  if (!g_active.load()) return;
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
    ScheduleAsyncCompilationRestore();
    rb_blitz::diag::boot_heartbeat::Finish("usable menu");
  }
}
IoObservation BeginIo(uint32_t handle, bool write) {
  if (!g_active.load()) return {};
  auto* runtime = REX_RUNTIME();
  if (!runtime || !runtime->IsLoaded()) return {};
  auto file = runtime->GetObjectTable().Get<rex::kernel::FileObject>(handle);
  if (!file) return {};
  std::string name;
  {
    std::lock_guard lock(file->file_lock);
    if (!file->file) return {};
    name = file->file->GetInfo().name;
  }
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
// 2026-10-10-song-cache-plan.md Ã‚Â§9.4) and with 3546 - the enumeration's own count - on
// 2026-10-15. The Flow reads a non-positive total as "not enumerating ... -1", so only such an
// announcement is rewritten to -1; a positive one runs as the title wrote it. The host never
// re-pushes after it: the engine's own `Increment` calls refill the bar for the mounting phase.
extern "C" REX_FUNC(sub_827593A8) {
  uint32_t counted = 0, expected = 0;
  {
    std::lock_guard lock(g_mutex);
    g_guest_owns_panel = true;
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
    g_guest_owns_panel = true;
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
