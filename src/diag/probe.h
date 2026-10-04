// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The probe: the runtime instrument that makes the guest's own state visible
// without a debugger. See docs/engine/probe.md for the operator's view (what
// each cvar does, how to read a trace, how to add a point) and
// docs/plans/customization-plan.md P2 for why it exists - it is the instrument
// the flow maps S2-S7 are measured with.
//
// Two halves, deliberately separate:
//
//   * The trace. A bounded ring buffer of events (src/diag/probe_trace.h, no SDK
//     dependency, unit-tested) fed by points placed inside hooks. A point is one
//     line of code: `RBBLITZ_PROBE(0x82768C88, "mogg.header");`. Nothing is
//     recorded unless `probe_trace` is on, so an unconfigured boot pays a
//     predictable-not-taken branch and nothing else.
//
//   * The overlay. An ImGuiDialog added through ReXApp::OnCreateDialogs - the
//     SDK's own extension point, the same mechanism as its F3 debug overlay. It
//     shows the guest's video mode, the window and present-path settings, and the
//     last events the ring holds. It is drawn only while `probe_overlay` is on,
//     which makes the toggle live.
//
// What the probe is not: it is not a profiler (no timings are attributed), it is
// not a full instruction trace (a hook is a compile-time override of one
// function, so a trace only ever sees the points that were placed), and it does
// not read the guest's memory. It answers one question - "in what order did the
// guest reach the points I care about, with what arguments" - which is exactly
// what naming and flow-mapping need.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <rex/ppc/context.h>

#include "diag/probe_overlay.h"
#include "diag/probe_trace.h"

namespace rb_blitz::diag {

// Records one guest entry point. `address` is the guest address the enclosing
// hook overrides, `tag` names it (a literal, kept to 23 characters), and `ctx`
// supplies the arguments (r3..r10) and the return address. No-op unless
// `probe_trace` is on.
void Emit(const PPCContext& ctx, uint32_t address, const char* tag);

// The same, for a point that has no guest address of its own - the tag says what
// it is, the argument registers are still recorded.
void EmitRegisters(const PPCContext& ctx, const char* tag);

// Records a breadcrumb that is not a guest address - a boot milestone or a
// host-side decision - so it lands in the same ordered trace as the guest points
// around it. Callable from anywhere, including code that never sees a
// PPCContext.
void Mark(const char* tag);

bool tracing_enabled();
TraceBuffer& buffer();

// The probe's per-frame work, called from the UI thread (the overlay dialog's
// OnDraw calls it before deciding whether to draw). Today that is the live trace
// file, when `probe_trace_path` names one; it is off the guest threads on purpose,
// because writing a file from a guest call would be measured by the trace.
void Tick();

// Writes the newest `max_events` held events to the log, oldest first, one line
// each, under `reason`. This is how a trace leaves the process.
void DumpToLog(const char* reason, size_t max_events = 128);

bool overlay_enabled();

// Creates the overlay dialog. It registers itself with `drawer` (ImGuiDialog's
// constructor does) and unregisters when the returned pointer is destroyed, so
// the caller owns it for as long as the overlay should be available. The
// provider is called from the UI thread each frame the overlay draws.
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlayDialog(rex::ui::ImGuiDrawer* drawer,
                                                          WindowStateProvider provider);

}  // namespace rb_blitz::diag

// Places a probe point. `ctx` must be in scope, which it is inside any
// REX_HOOK_RAW/REX_HOOK body and inside a hand-written REX_FUNC.
#define RBBLITZ_PROBE(address, tag) ::rb_blitz::diag::Emit(ctx, (address), (tag))
// A point that records the argument registers under a title instead of an
// address.
#define RBBLITZ_PROBE_REGISTERS(tag) ::rb_blitz::diag::EmitRegisters(ctx, (tag))
// A breadcrumb with no guest address.
#define RBBLITZ_PROBE_MARK(tag) ::rb_blitz::diag::Mark((tag))
