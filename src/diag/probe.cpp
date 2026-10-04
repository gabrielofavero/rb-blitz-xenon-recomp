// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The probe's cvars, its one ring buffer and the recording path. See
// src/diag/probe.h for the interface and docs/engine/probe.md for how to use it.
//
// No SDK patch and no second renderer: the trace is fed from ordinary hooks, and
// the overlay is an ordinary ImGuiDialog (src/diag/probe_overlay.cpp).

#include "diag/probe.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/thread.h>

#include "diag/probe_overlay.h"

REXCVAR_DEFINE_BOOL(probe_overlay, false, "Diagnostics",
                    "Draw the probe overlay: the guest's requested video mode, the window and "
                    "present-path settings, and the newest probe trace events. Live - it is "
                    "re-read every frame");
REXCVAR_DEFINE_BOOL(probe_trace, false, "Diagnostics",
                    "Record probe events (guest entry points placed with RBBLITZ_PROBE and "
                    "breadcrumbs) into the ring buffer the overlay shows and DumpToLog writes");
REXCVAR_DEFINE_INT32(probe_trace_capacity, 256, "Diagnostics",
                     "How many probe events the ring buffer holds; the oldest are overwritten and "
                     "counted as dropped. The floor is the probe's own policy - fewer than 16 "
                     "events is not a trace - while the buffer itself accepts any non-zero size")
    .range(16, int32_t(rb_blitz::diag::TraceBuffer::kMaxCapacity));
REXCVAR_DEFINE_STRING(probe_trace_path, "", "Diagnostics",
                      "When set and probe_trace is on, the newest events are rewritten to this "
                      "file about twice a second, oldest first, so a script can read the trace "
                      "after a run without a console. Empty writes nothing");

namespace rb_blitz::diag {

namespace {

// One clock for the whole process, so events from different threads share a
// timeline. Started on first use, which is the first recorded event.
std::chrono::steady_clock::time_point StartTime() {
  static const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  return start;
}

double ElapsedMs() {
  // StartTime() first: the static is created on the first call, and reading the
  // clock before it would date the first event before the probe's own origin.
  const std::chrono::steady_clock::time_point start = StartTime();
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(now - start).count();
}

TraceBuffer& Buffer() {
  static TraceBuffer buffer;
  return buffer;
}

// The cvar is authoritative and can change at runtime; the buffer is only
// resized when the value actually moved, so the common path is one relaxed load.
std::atomic<int32_t> g_applied_capacity{0};

void ApplyCapacity() {
  const int32_t configured = REXCVAR_GET(probe_trace_capacity);
  if (g_applied_capacity.load(std::memory_order_relaxed) == configured) {
    return;
  }
  Buffer().SetCapacity(static_cast<size_t>(configured));
  g_applied_capacity.store(configured, std::memory_order_relaxed);
}

// The guest thread id when the calling host thread is running guest code, and
// the host thread id otherwise - the same number the log's `[tNNNN]` prefix
// carries, so a trace line and a log line from the same thread agree.
uint32_t CurrentThreadId() { return rex::thread::current_thread_id(); }

void Fill(const PPCContext& ctx, TraceEvent& event) {
  event.time_ms = ElapsedMs();
  event.thread_id = CurrentThreadId();
  event.lr = static_cast<uint32_t>(ctx.lr);
  event.args[0] = ctx.r3.u64;
  event.args[1] = ctx.r4.u64;
  event.args[2] = ctx.r5.u64;
  event.args[3] = ctx.r6.u64;
  event.args[4] = ctx.r7.u64;
  event.args[5] = ctx.r8.u64;
  event.args[6] = ctx.r9.u64;
  event.args[7] = ctx.r10.u64;
}

}  // namespace

bool tracing_enabled() { return REXCVAR_GET(probe_trace); }

bool overlay_enabled() { return REXCVAR_GET(probe_overlay); }

TraceBuffer& buffer() { return Buffer(); }

void Emit(const PPCContext& ctx, uint32_t address, const char* tag) {
  if (!REXCVAR_GET(probe_trace)) {
    return;
  }
  ApplyCapacity();
  TraceEvent event;
  Fill(ctx, event);
  event.address = address;
  event.kind = TraceKind::kEntry;
  std::snprintf(event.tag, sizeof(event.tag), "%s", tag != nullptr ? tag : "");
  Buffer().Push(event);
}

void EmitRegisters(const PPCContext& ctx, const char* tag) {
  if (!REXCVAR_GET(probe_trace)) {
    return;
  }
  ApplyCapacity();
  TraceEvent event;
  Fill(ctx, event);
  event.address = 0;
  event.kind = TraceKind::kEntry;
  std::snprintf(event.tag, sizeof(event.tag), "%s", tag != nullptr ? tag : "");
  Buffer().Push(event);
}

void Mark(const char* tag) {
  if (!REXCVAR_GET(probe_trace)) {
    return;
  }
  ApplyCapacity();
  TraceEvent event;
  event.time_ms = ElapsedMs();
  event.thread_id = CurrentThreadId();
  event.address = 0;
  event.kind = TraceKind::kMark;
  std::snprintf(event.tag, sizeof(event.tag), "%s", tag != nullptr ? tag : "");
  Buffer().Push(event);
}

void Tick() {
  static std::chrono::steady_clock::time_point last_flush{};
  static std::string last_path;
  static bool warned_write_failure = false;

  const std::string path = REXCVAR_GET(probe_trace_path);
  if (path.empty() || !REXCVAR_GET(probe_trace)) {
    last_path.clear();
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const bool path_changed = path != last_path;
  if (!path_changed && now - last_flush < std::chrono::milliseconds(500)) {
    return;
  }
  last_flush = now;
  last_path = path;

  std::ofstream out(path, std::ios::trunc | std::ios::binary);
  if (!out) {
    if (!warned_write_failure) {
      warned_write_failure = true;
      REXLOG_WARN("probe: cannot write the trace file '{}' (probe_trace_path)", path);
    }
    return;
  }
  warned_write_failure = false;

  const TraceBuffer& trace = Buffer();
  out << "# probe trace: " << trace.size() << " event(s) held, " << trace.dropped()
      << " dropped, " << trace.total() << " recorded since the last clear\n";
  for (const TraceEvent& event : trace.Snapshot()) {
    out << FormatEvent(event) << '\n';
  }
}

void DumpToLog(const char* reason, size_t max_events) {
  const TraceBuffer& trace = Buffer();
  const std::vector<TraceEvent> events = trace.Snapshot(max_events);
  REXLOG_INFO("probe: dump ({}) - {} event(s), {} dropped, {} recorded since the last clear",
              reason != nullptr ? reason : "", events.size(),
              static_cast<unsigned long long>(trace.dropped()),
              static_cast<unsigned long long>(trace.total()));
  for (const TraceEvent& event : events) {
    REXLOG_INFO("probe: {}", FormatEvent(event));
  }
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlayDialog(rex::ui::ImGuiDrawer* drawer,
                                                          WindowStateProvider provider) {
  return std::make_unique<ProbeOverlayDialog>(drawer, std::move(provider));
}

}  // namespace rb_blitz::diag
