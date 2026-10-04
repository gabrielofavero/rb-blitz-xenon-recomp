// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for the probe's ring buffer and its line rendering
// (src/diag/probe_trace.{h,cpp}). No SDK, no boot - see tests/check.h.

#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "diag/probe_trace.h"

using rb_blitz::diag::FormatEvent;
using rb_blitz::diag::TraceBuffer;
using rb_blitz::diag::TraceEvent;
using rb_blitz::diag::TraceKind;

namespace {

TraceEvent MakeEntry(uint32_t address, const char* tag) {
  TraceEvent event;
  event.address = address;
  event.kind = TraceKind::kEntry;
  std::snprintf(event.tag, sizeof(event.tag), "%s", tag);
  return event;
}

void TestPushAssignsSequenceAndOrder() {
  rb_blitz::test::BeginCase("push assigns a 1-based sequence and preserves order");
  TraceBuffer buffer;
  buffer.SetCapacity(8);
  buffer.Push(MakeEntry(0x823DE070, "a"));
  buffer.Push(MakeEntry(0x823DE0C0, "b"));
  buffer.Push(MakeEntry(0x82768C88, "c"));

  CHECK_EQ(buffer.size(), 3);
  CHECK_EQ(buffer.total(), 3);
  CHECK_EQ(buffer.dropped(), 0);

  const std::vector<TraceEvent> events = buffer.Snapshot();
  CHECK_EQ(events.size(), 3);
  CHECK_EQ(events[0].seq, 1);
  CHECK_EQ(events[0].address, 0x823DE070);
  CHECK_EQ(events[1].seq, 2);
  CHECK_EQ(events[1].address, 0x823DE0C0);
  CHECK_EQ(events[2].seq, 3);
  CHECK_EQ(events[2].address, 0x82768C88);
  CHECK_TRUE(std::strcmp(events[2].tag, "c") == 0);
}

void TestOverflowDropsTheOldest() {
  rb_blitz::test::BeginCase("a full ring overwrites the oldest and counts it as dropped");
  TraceBuffer buffer;
  buffer.SetCapacity(4);
  for (uint32_t i = 0; i < 6; ++i) {
    buffer.Push(MakeEntry(0x80000000u + i, "x"));
  }

  CHECK_EQ(buffer.size(), 4);
  CHECK_EQ(buffer.total(), 6);
  CHECK_EQ(buffer.dropped(), 2);

  const std::vector<TraceEvent> events = buffer.Snapshot();
  CHECK_EQ(events.size(), 4);
  // The two oldest (seq 1 and 2) are gone; the newest four remain, in order.
  CHECK_EQ(events[0].seq, 3);
  CHECK_EQ(events[0].address, 0x80000002);
  CHECK_EQ(events[3].seq, 6);
  CHECK_EQ(events[3].address, 0x80000005);
}

void TestSnapshotReturnsTheNewestN() {
  rb_blitz::test::BeginCase("a bounded snapshot returns the newest events, oldest first");
  TraceBuffer buffer;
  buffer.SetCapacity(32);
  for (uint32_t i = 0; i < 10; ++i) {
    buffer.Push(MakeEntry(i, "x"));
  }

  const std::vector<TraceEvent> tail = buffer.Snapshot(3);
  CHECK_EQ(tail.size(), 3);
  CHECK_EQ(tail[0].seq, 8);
  CHECK_EQ(tail[1].seq, 9);
  CHECK_EQ(tail[2].seq, 10);

  // More than is held is not an error: it is everything.
  CHECK_EQ(buffer.Snapshot(64).size(), 10);
  CHECK_EQ(buffer.Snapshot(0).size(), 10);
  CHECK_EQ(buffer.Snapshot(1)[0].seq, 10);
}

void TestClearResets() {
  rb_blitz::test::BeginCase("clear forgets the events, the counters and the sequence");
  TraceBuffer buffer;
  buffer.SetCapacity(4);
  for (uint32_t i = 0; i < 9; ++i) {
    buffer.Push(MakeEntry(i, "x"));
  }
  CHECK_EQ(buffer.dropped(), 5);

  buffer.Clear();
  CHECK_EQ(buffer.size(), 0);
  CHECK_EQ(buffer.total(), 0);
  CHECK_EQ(buffer.dropped(), 0);
  CHECK_EQ(buffer.Snapshot().size(), 0);
  // The capacity survives a clear.
  CHECK_EQ(buffer.capacity(), 4);

  buffer.Push(MakeEntry(0xAB, "after"));
  CHECK_EQ(buffer.Snapshot()[0].seq, 1);
}

void TestCapacityClampAndResize() {
  rb_blitz::test::BeginCase("capacity clamps, and a shrink keeps the newest in order");
  TraceBuffer buffer;
  buffer.SetCapacity(0);
  CHECK_EQ(buffer.capacity(), TraceBuffer::kMinCapacity);
  buffer.SetCapacity(TraceBuffer::kMaxCapacity + 1024);
  CHECK_EQ(buffer.capacity(), TraceBuffer::kMaxCapacity);

  buffer.Clear();
  buffer.SetCapacity(32);
  for (uint32_t i = 0; i < 10; ++i) {
    buffer.Push(MakeEntry(i, "x"));
  }
  buffer.SetCapacity(3);
  CHECK_EQ(buffer.capacity(), 3);
  CHECK_EQ(buffer.size(), 3);
  const std::vector<TraceEvent> events = buffer.Snapshot();
  CHECK_EQ(events[0].seq, 8);
  CHECK_EQ(events[2].seq, 10);
  // Shrinking discarded seven events; growing again does not resurrect them.
  CHECK_EQ(buffer.dropped(), 7);
  buffer.SetCapacity(16);
  CHECK_EQ(buffer.size(), 3);
  CHECK_EQ(buffer.Snapshot()[0].seq, 8);
}

void TestTagIsTruncatedNotLost() {
  rb_blitz::test::BeginCase("an over-long tag is truncated and still names the point");
  TraceBuffer buffer;
  buffer.SetCapacity(4);
  buffer.Push(MakeEntry(0x1234, "a-very-long-probe-tag-that-overflows"));
  const std::vector<TraceEvent> events = buffer.Snapshot();
  CHECK_EQ(events[0].tag[sizeof(TraceEvent::tag) - 1], 0);
  CHECK_TRUE(std::strlen(events[0].tag) == sizeof(TraceEvent::tag) - 1);
  CHECK_TRUE(std::strncmp(events[0].tag, "a-very-long-probe-tag", 21) == 0);
}

void TestFormatIsStable() {
  rb_blitz::test::BeginCase("one line names the sequence, address, tag and arguments");
  TraceEvent event = MakeEntry(0x82768C88, "mogg.header");
  event.seq = 42;
  event.time_ms = 16384.0;  // +16.384s
  event.thread_id = 0x1234;
  event.lr = 0x821A3C04;
  event.args[0] = 0xDEADBEEF;
  event.args[7] = 0x10;

  const std::string line = FormatEvent(event);
  CHECK_TRUE(line.find("#000042") != std::string::npos);
  CHECK_TRUE(line.find("+   16.384s") != std::string::npos);
  CHECK_TRUE(line.find("t00001234") != std::string::npos);
  CHECK_TRUE(line.find("0x82768C88") != std::string::npos);
  CHECK_TRUE(line.find("entry") != std::string::npos);
  CHECK_TRUE(line.find("\"mogg.header\"") != std::string::npos);
  CHECK_TRUE(line.find("lr=0x821A3C04") != std::string::npos);
  CHECK_TRUE(line.find("r3=00000000DEADBEEF") != std::string::npos);
  CHECK_TRUE(line.find("r10=0000000000000010") != std::string::npos);

  TraceEvent mark = event;
  mark.kind = TraceKind::kMark;
  mark.address = 0;
  CHECK_TRUE(FormatEvent(mark).find("mark") != std::string::npos);
}

}  // namespace

int main() {
  TestPushAssignsSequenceAndOrder();
  TestOverflowDropsTheOldest();
  TestSnapshotReturnsTheNewestN();
  TestClearResets();
  TestCapacityClampAndResize();
  TestTagIsTruncatedNotLost();
  TestFormatIsStable();
  return rb_blitz::test::Finish();
}
