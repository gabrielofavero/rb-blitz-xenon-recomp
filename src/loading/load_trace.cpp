// SPDX-License-Identifier: GPL-2.0-only
// Song-load diagnostics and process-monitor lifecycle publication. These hooks
// never attempt to continue a faulted guest or manufacture native completion.
#include "loading/load_trace.h"
#include "loading/crash_recovery.h"

#include <chrono>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

REX_EXTERN(__imp__sub_822C3428);
REX_EXTERN(__imp__sub_822C3878);
REX_EXTERN(__imp__sub_822C38F8);
REX_EXTERN(__imp__sub_822C3F48);

REXCVAR_DEFINE_BOOL(song_load_trace, false, "Diagnostics",
    "Read-only song-load lifecycle trace: load setup, job start, native completion, "
    "unload and UI heartbeat. Does not cancel loads or recover guest faults.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::loading {
namespace {
bool enabled = false; // configured once before guest launch
// Hooks run on their calling guest thread. Keeping each observation thread-local
// avoids turning a trace into a cross-thread screen/object owner.
struct Trace {
  uint64_t generation = 0;
  uint32_t screen = 0;
  int64_t began = 0, last_heartbeat = 0;
  uint32_t last_screen = 0, last_transition = ~0u;
  bool active = false;
};
thread_local Trace trace;
int64_t Now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
uint32_t U32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
uint32_t Thread() { return rex::runtime::ThreadState::GetThreadID(); }

void Event(const char* phase, uint8_t* base, uint32_t screen, uint32_t caller) {
  REXLOG_INFO("song_load: generation={} phase={} elapsed_ms={} thread={:08X} "
              "screen={:08X} native_state={} caller={:08X}",
              trace.generation, phase, Now() - trace.began, Thread(), screen,
              U32(base, screen + 72), caller);
}
}

void ConfigureLoadTrace() {
  enabled = REXCVAR_GET(song_load_trace);
  if (enabled) REXLOG_INFO("song_load: read-only lifecycle tracing enabled");
}

void ObserveLoadFrame(uint8_t* base, uint32_t manager) {
  if (!enabled || !manager || !trace.active) return;
  const auto screen = U32(base, manager + 44);
  const auto transition = U32(base, manager + 16);
  const auto now = Now();
  if (screen != trace.last_screen || transition != trace.last_transition ||
      now - trace.last_heartbeat >= 1000) {
    REXLOG_INFO("song_load: generation={} phase=ui_poll elapsed_ms={} thread={:08X} "
                "screen={:08X} transition={} native_state={}", trace.generation,
                now - trace.began, Thread(), screen, transition,
                screen == trace.screen ? U32(base, screen + 72) : ~0u);
    trace.last_screen = screen;
    trace.last_transition = transition;
    trace.last_heartbeat = now;
  }
}

// These helpers observe the live object only inside its own method invocation.
// No saved pointer is dereferenced after unload, and no host timer calls guests.
void TraceBegin(uint8_t* base, uint32_t screen, uint32_t caller) {
  if (!enabled) return;
  ++trace.generation;
  trace.screen = screen;
  trace.began = Now();
  trace.active = true;
  trace.last_heartbeat = trace.began;
  Event("setup_enter", base, screen, caller);
}
void TraceEvent(const char* phase, uint8_t* base, uint32_t screen, uint32_t caller) {
  if (enabled) Event(phase, base, screen, caller);
}
void TraceEnd() { if (enabled) trace.active = false; }
}  // namespace rb_blitz::loading

// LoadingScreen vtable 0x8203DD64: setup +116, unload +124. Both
// wrappers preserve the original arguments/result and forward exactly once.
REX_HOOK_RAW(sub_822C3428) {
  const auto screen = ctx.r3.u32, caller = uint32_t(ctx.lr);
  rb_blitz::loading::BeginCrashRecoveryAttempt();
  rb_blitz::loading::TraceBegin(base, screen, caller);
  __imp__sub_822C3428(ctx, base);
  rb_blitz::loading::TraceEvent("setup_return", base, screen, caller);
}
REX_HOOK_RAW(sub_822C3878) {
  const auto screen = ctx.r3.u32, caller = uint32_t(ctx.lr);
  rb_blitz::loading::BeforeSongLoadJob();
  rb_blitz::loading::TraceEvent("job_enter", base, screen, caller);
  __imp__sub_822C3878(ctx, base);
  rb_blitz::loading::TraceEvent("job_return", base, screen, caller);
}
REX_HOOK_RAW(sub_822C38F8) {
  // r3 is the caller's result cell; r4 is the screen. Never call this
  // function to abort or fabricate completion.
  const auto screen = ctx.r4.u32, caller = uint32_t(ctx.lr);
  rb_blitz::loading::TraceEvent("completion_enter", base, screen, caller);
  __imp__sub_822C38F8(ctx, base);
  rb_blitz::loading::EndCrashRecoveryAttempt();
  rb_blitz::loading::TraceEvent("completion_return", base, screen, caller);
}
REX_HOOK_RAW(sub_822C3F48) {
  const auto screen = ctx.r3.u32, caller = uint32_t(ctx.lr);
  // Native departure/manual cancellation retires the deadline before cleanup,
  // which may itself block. Do not let an old load timer outlive that decision.
  rb_blitz::loading::EndCrashRecoveryAttempt();
  rb_blitz::loading::TraceEvent("unload_enter", base, screen, caller);
  __imp__sub_822C3F48(ctx, base);
  rb_blitz::loading::TraceEvent("unload_return", base, screen, caller);
  rb_blitz::loading::TraceEnd();
}
