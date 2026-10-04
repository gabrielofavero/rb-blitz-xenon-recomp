// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The decidable half of the probe in src/diag/probe.{h,cpp}: a bounded,
// thread-safe ring buffer of guest trace events, and the one-line rendering of
// one.
//
// Kept free of any SDK dependency for the same reason src/fs/path_policy.h and
// src/input/ui_nav.h are: the interesting part is arithmetic over a ring (what
// is retained, what is counted as dropped, in what order a snapshot comes back),
// and tests/probe_trace_tests.cpp has to cover it without booting the game
// (docs/rb3-references.md §7.3, and the split-the-decision rule in
// docs/plans/customization-plan.md contract 4).
//
// Why a ring and not a log. A trace is written from guest threads, and the
// interesting window is one screen transition - thousands of events over a
// handful of frames. A log line per event would be both slower than the thing it
// measures and unreadable; a ring keeps the last N with no allocation on the hot
// path and hands the whole window to the overlay or to one dump.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rb_blitz::diag {

// A guest function entry (a point a hook was placed on), or a titled breadcrumb
// that is not tied to a guest address (a boot milestone, a host-side decision).
enum class TraceKind : uint8_t {
  kEntry = 0,
  kMark = 1,
};

// One recorded event. Deliberately plain data: the buffer copies it, the overlay
// reads it and the tests construct it by hand.
struct TraceEvent {
  uint64_t seq = 0;         // 1-based, assigned by the buffer
  double time_ms = 0.0;     // since the probe's process-wide start
  uint32_t thread_id = 0;   // guest thread id when on one, else the host's
  uint32_t address = 0;     // guest address probed, 0 for a mark
  uint32_t lr = 0;          // guest return address (where the call came from)
  TraceKind kind = TraceKind::kEntry;
  char tag[24] = {};        // short label, truncated on store
  uint64_t args[8] = {};    // r3..r10 as raw 64-bit, so signedness is the reader's
};

class TraceBuffer {
 public:
  // A ring of one event is degenerate but well defined; the *probe's* policy
  // minimum (a trace has to hold more than one screen's worth to be useful) is
  // the `probe_trace_capacity` cvar's range, not this class's bound. The maximum
  // is a memory bound: a capacity above it is a leak with a trace attached.
  static constexpr size_t kMinCapacity = 1;
  static constexpr size_t kMaxCapacity = size_t{1} << 20;

  TraceBuffer() = default;
  TraceBuffer(const TraceBuffer&) = delete;
  TraceBuffer& operator=(const TraceBuffer&) = delete;

  // Resizes, keeping the newest events in order. Clamped to
  // [kMinCapacity, kMaxCapacity].
  void SetCapacity(size_t capacity);
  size_t capacity() const;

  // Forgets every held event and restarts the sequence at 1. The capacity and
  // the trace configuration are untouched.
  void Clear();

  // Stores `event`, assigning it the next sequence number. When the ring is
  // full the oldest event is overwritten - that is what `dropped()` counts.
  void Push(const TraceEvent& event);

  size_t size() const;
  // Events ever stored since the last Clear() minus those still held: what the
  // ring has discarded.
  uint64_t dropped() const;
  // Events ever stored since the last Clear().
  uint64_t total() const;

  // Up to `max_events` of the newest events, oldest first - the reading order a
  // trace is printed in. `max_events` of 0 means "all held".
  std::vector<TraceEvent> Snapshot(size_t max_events = 0) const;

 private:
  std::vector<TraceEvent> SnapshotUnlocked(size_t max_events) const;

  mutable std::mutex mutex_;
  std::vector<TraceEvent> ring_;
  size_t head_ = 0;   // index the next Push writes to
  size_t count_ = 0;  // events currently held, <= ring_.size()
  uint64_t total_ = 0;
};

// One line, stable for a fixed event so a test can assert it and a reader can
// diff two dumps: sequence, time, thread, address, kind, tag, return address and
// the eight argument registers.
std::string FormatEvent(const TraceEvent& event);

}  // namespace rb_blitz::diag
