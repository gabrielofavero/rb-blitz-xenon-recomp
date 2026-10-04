// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The ring buffer behind src/diag/probe_trace.h. See that header for what it is
// for and why it has no SDK dependency.

#include "diag/probe_trace.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rb_blitz::diag {

namespace {

size_t ClampCapacity(size_t capacity) {
  if (capacity < TraceBuffer::kMinCapacity) {
    return TraceBuffer::kMinCapacity;
  }
  if (capacity > TraceBuffer::kMaxCapacity) {
    return TraceBuffer::kMaxCapacity;
  }
  return capacity;
}

// The tag is a fixed field so an event can be copied without allocating; a
// longer label is truncated rather than dropped, because half a label still
// names the point.
void CopyTag(char (&dest)[24], const char* tag) {
  if (tag == nullptr) {
    dest[0] = '\0';
    return;
  }
  std::snprintf(dest, sizeof(dest), "%s", tag);
}

}  // namespace

void TraceBuffer::SetCapacity(size_t capacity) {
  const size_t target = ClampCapacity(capacity);
  std::lock_guard<std::mutex> lock(mutex_);
  if (target == ring_.size()) {
    return;
  }

  const std::vector<TraceEvent> kept = SnapshotUnlocked(target);
  ring_.assign(target, TraceEvent{});
  const size_t n = kept.size();
  for (size_t i = 0; i < n; ++i) {
    ring_[i] = kept[i];
  }
  count_ = n;
  head_ = (n == target) ? 0 : n;
}

size_t TraceBuffer::capacity() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ring_.size();
}

void TraceBuffer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::fill(ring_.begin(), ring_.end(), TraceEvent{});
  head_ = 0;
  count_ = 0;
  total_ = 0;
}

void TraceBuffer::Push(const TraceEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ring_.empty()) {
    return;
  }
  TraceEvent stored = event;
  stored.seq = ++total_;
  CopyTag(stored.tag, event.tag);
  ring_[head_] = stored;
  head_ = (head_ + 1) % ring_.size();
  if (count_ < ring_.size()) {
    ++count_;
  }
}

size_t TraceBuffer::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

uint64_t TraceBuffer::dropped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_ - count_;
}

uint64_t TraceBuffer::total() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_;
}

std::vector<TraceEvent> TraceBuffer::SnapshotUnlocked(size_t max_events) const {
  size_t take = count_;
  if (max_events != 0) {
    take = std::min(take, max_events);
  }
  std::vector<TraceEvent> out;
  out.reserve(take);
  if (take == 0) {
    return out;
  }
  // Newest `take` events, oldest first: start `count - take` entries past the
  // oldest event the ring holds.
  const size_t oldest = (head_ + ring_.size() - count_) % ring_.size();
  const size_t first = (oldest + (count_ - take)) % ring_.size();
  for (size_t i = 0; i < take; ++i) {
    out.push_back(ring_[(first + i) % ring_.size()]);
  }
  return out;
}

std::vector<TraceEvent> TraceBuffer::Snapshot(size_t max_events) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return SnapshotUnlocked(max_events);
}

std::string FormatEvent(const TraceEvent& event) {
  char line[320];
  std::snprintf(line, sizeof(line),
                "#%06llu +%9.3fs t%08X 0x%08X %-5s \"%s\" lr=0x%08X"
                " r3=%016llX r4=%016llX r5=%016llX r6=%016llX"
                " r7=%016llX r8=%016llX r9=%016llX r10=%016llX",
                static_cast<unsigned long long>(event.seq), event.time_ms / 1000.0,
                event.thread_id, event.address,
                event.kind == TraceKind::kMark ? "mark" : "entry", event.tag, event.lr,
                static_cast<unsigned long long>(event.args[0]),
                static_cast<unsigned long long>(event.args[1]),
                static_cast<unsigned long long>(event.args[2]),
                static_cast<unsigned long long>(event.args[3]),
                static_cast<unsigned long long>(event.args[4]),
                static_cast<unsigned long long>(event.args[5]),
                static_cast<unsigned long long>(event.args[6]),
                static_cast<unsigned long long>(event.args[7]));
  return std::string(line);
}

}  // namespace rb_blitz::diag
