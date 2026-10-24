// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for rexglue-sdk/include/rex/graphics/graphics_preparation.h and the
// cancellation states added to pipeline_preload.h - the lifecycle both graphics
// backends drive from P4 on (docs/plans/in-progress/2026-10-10-shader-cache-plan.md,
// P4).
//
// These exercise the real component the command processor and both caches call,
// with real threads and real waits. The cases pin the rules the plan states:
//
//   * one generation at a time, and a strictly increasing token;
//   * phases move forwards only, terminal results are published once;
//   * progress counts completed work, never queued or running entries;
//   * a zero-entry catalogue is not coverage - it can never be "ready";
//   * cancellation stops the generation without counting cancelled entries as
//     success and without losing work already in flight;
//   * shutdown releases a waiter instead of deadlocking it.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "check.h"

#include <rex/graphics/graphics_preparation.h>
#include <rex/graphics/pipeline_preload.h>

namespace {

using namespace rb_blitz::test;
using rex::graphics::GraphicsPreparationBackend;
using rex::graphics::GraphicsPreparationController;
using rex::graphics::GraphicsPreparationIsTerminal;
using rex::graphics::GraphicsPreparationKey;
using rex::graphics::GraphicsPreparationPhase;
using rex::graphics::GraphicsPreparationPhaseName;
using rex::graphics::PipelinePreloadTracker;

using Clock = std::chrono::steady_clock;

GraphicsPreparationKey MakeKey(uint32_t title_id = 0x5841122D, uint64_t configuration = 1) {
  GraphicsPreparationKey key;
  key.title_id = title_id;
  key.configuration = configuration;
  key.cache_root = 0xC0FFEE;
  return key;
}

uint64_t MillisecondsSince(Clock::time_point begin) {
  return uint64_t(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count());
}

void TestBeginAndGenerations() {
  BeginCase("begin: one generation at a time, tokens increase");
  GraphicsPreparationController controller;
  CHECK_FALSE(controller.Active());
  CHECK_EQ(controller.generation(), 0);

  const GraphicsPreparationKey first_key = MakeKey(0x5841122D, 1);
  CHECK_TRUE(controller.Begin(first_key));
  CHECK_TRUE(controller.Active());
  CHECK_EQ(controller.generation(), 1);
  CHECK_EQ(controller.phase(), GraphicsPreparationPhase::kImporting);
  // A second generation cannot start while the first is alive - two
  // preparations must never write the same caches concurrently.
  CHECK_FALSE(controller.Begin(MakeKey(0x5841122D, 2)));
  CHECK_EQ(controller.generation(), 1);

  CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kFailed));
  CHECK_FALSE(controller.Active());
  CHECK_EQ(controller.phase(), GraphicsPreparationPhase::kFailed);

  // A new key after a terminal phase starts a new generation.
  CHECK_TRUE(controller.Begin(MakeKey(0x5841122D, 2)));
  CHECK_EQ(controller.generation(), 2);
  const auto snapshot = controller.Snapshot();
  CHECK_EQ(snapshot.generation, 2);
  CHECK_EQ(snapshot.key.configuration, 2);
  CHECK_EQ(snapshot.key.title_id, 0x5841122D);
  CHECK_TRUE(snapshot.active);
  CHECK_FALSE(snapshot.finished);

  // Finishing twice does not overwrite the first terminal result.
  CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kIncomplete));
  CHECK_FALSE(controller.Finish(GraphicsPreparationPhase::kSucceeded));
  CHECK_EQ(controller.phase(), GraphicsPreparationPhase::kIncomplete);
}

void TestPhasesMoveForwardsOnly() {
  BeginCase("phases move forwards and never into a terminal phase");
  GraphicsPreparationController controller;
  CHECK_FALSE(controller.AdvancePhase(GraphicsPreparationPhase::kTranslating));
  CHECK_TRUE(controller.Begin(MakeKey()));
  CHECK_TRUE(controller.AdvancePhase(GraphicsPreparationPhase::kTranslating));
  CHECK_FALSE(controller.AdvancePhase(GraphicsPreparationPhase::kImporting));
  CHECK_FALSE(controller.AdvancePhase(GraphicsPreparationPhase::kTranslating));
  // Terminal phases are published by Finish, not by a phase change.
  CHECK_FALSE(controller.AdvancePhase(GraphicsPreparationPhase::kSucceeded));
  CHECK_TRUE(controller.AdvancePhase(GraphicsPreparationPhase::kCreating));
  CHECK_EQ(controller.phase(), GraphicsPreparationPhase::kCreating);

  CHECK_TRUE(std::string(GraphicsPreparationPhaseName(GraphicsPreparationPhase::kCreating)) ==
             std::string("creating"));
  CHECK_TRUE(GraphicsPreparationIsTerminal(GraphicsPreparationPhase::kSucceeded));
  CHECK_FALSE(GraphicsPreparationIsTerminal(GraphicsPreparationPhase::kCreating));
}

void TestProgressCountsOnlyCompletedWork() {
  BeginCase("progress: queued and running entries are not completed work");
  GraphicsPreparationController controller;
  CHECK_TRUE(controller.Begin(MakeKey()));
  controller.SetRenderer(GraphicsPreparationBackend::kD3D12, 1);
  controller.NoteCatalogue(true);
  for (uint64_t identity = 0; identity < 4; ++identity) {
    CHECK_TRUE(controller.RegisterEntry(identity));
  }
  CHECK_FALSE(controller.RegisterEntry(0));

  controller.BeginEntry(1);
  controller.SucceedEntry(1);
  auto snapshot = controller.Snapshot();
  CHECK_EQ(snapshot.entries.required, 4);
  CHECK_EQ(snapshot.entries.queued, 3);
  CHECK_EQ(snapshot.entries.running, 0);
  CHECK_EQ(snapshot.entries.succeeded, 1);
  CHECK_EQ(snapshot.completed_entries(), 1);
  CHECK_EQ(snapshot.required_entries(), 4);
  CHECK_EQ(snapshot.backend, GraphicsPreparationBackend::kD3D12);
  CHECK_EQ(snapshot.render_path, 1);
  CHECK_TRUE(snapshot.has_catalogue);
  CHECK_FALSE(snapshot.ready());
  CHECK_EQ(controller.unfinished_entries(), 3);

  controller.BeginEntry(0);
  controller.FailEntry(0);
  controller.MissEntry(2);
  controller.SucceedEntry(3);
  snapshot = controller.Snapshot();
  CHECK_EQ(snapshot.entries.queued, 0);
  CHECK_EQ(snapshot.entries.running, 0);
  CHECK_EQ(snapshot.entries.succeeded, 2);
  CHECK_EQ(snapshot.entries.failed, 1);
  CHECK_EQ(snapshot.entries.missing, 1);
  CHECK_TRUE(snapshot.entries.Finished());
  CHECK_FALSE(snapshot.entries.Complete());
  CHECK_EQ(controller.unfinished_entries(), 0);

  CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kIncomplete));
  snapshot = controller.Snapshot();
  CHECK_TRUE(snapshot.finished);
  CHECK_FALSE(snapshot.complete);
  CHECK_FALSE(snapshot.ready());
}

void TestSuccessNeedsARealCatalogue() {
  BeginCase("a zero-entry catalogue can never be ready");
  {
    GraphicsPreparationController controller;
    CHECK_TRUE(controller.Begin(MakeKey()));
    // Nothing registered, nothing declared.
    CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kSucceeded));
    const auto snapshot = controller.Snapshot();
    CHECK_TRUE(snapshot.finished);
    CHECK_FALSE(snapshot.entries.HasWork());
    CHECK_FALSE(snapshot.has_catalogue);
    CHECK_FALSE(snapshot.complete);
    CHECK_FALSE(snapshot.ready());
  }

  BeginCase("a declared catalogue with an entry is ready only when it succeeded");
  {
    GraphicsPreparationController controller;
    CHECK_TRUE(controller.Begin(MakeKey()));
    controller.NoteCatalogue(true);
    CHECK_TRUE(controller.RegisterEntry(1));
    controller.SucceedEntry(1);
    CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kSucceeded));
    const auto snapshot = controller.Snapshot();
    CHECK_TRUE(snapshot.complete);
    CHECK_TRUE(snapshot.ready());
    CHECK_EQ(snapshot.completed_entries(), 1);
  }
}

void TestCancellation() {
  BeginCase("cancellation: queued entries are cancelled, never successes");
  {
    GraphicsPreparationController controller;
    CHECK_TRUE(controller.Begin(MakeKey()));
    controller.NoteCatalogue(true);
    for (uint64_t identity = 0; identity < 3; ++identity) {
      CHECK_TRUE(controller.RegisterEntry(identity));
    }
    controller.BeginEntry(0);
    controller.RequestCancel();
    CHECK_TRUE(controller.CancelRequested());
    CHECK_EQ(controller.CancelPendingEntries(), 2);

    auto snapshot = controller.Snapshot();
    CHECK_EQ(snapshot.entries.cancelled, 2);
    CHECK_EQ(snapshot.entries.running, 1);
    CHECK_EQ(snapshot.entries.succeeded, 0);
    CHECK_FALSE(snapshot.entries.Complete());
    // Work already in flight keeps the generation open: the owner must wait for
    // its own terminal state before it can finalize.
    CHECK_EQ(controller.unfinished_entries(), 1);
    CHECK_TRUE(snapshot.cancel_requested);

    // A late transition on a cancelled entry is ignored.
    controller.SucceedEntry(1);
    controller.BeginEntry(0);
    controller.SucceedEntry(0);
    snapshot = controller.Snapshot();
    CHECK_EQ(snapshot.entries.succeeded, 1);
    CHECK_EQ(snapshot.entries.cancelled, 2);
    CHECK_EQ(controller.unfinished_entries(), 0);

    CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kCancelled));
    snapshot = controller.Snapshot();
    CHECK_EQ(snapshot.phase, GraphicsPreparationPhase::kCancelled);
    CHECK_TRUE(snapshot.finished);
    CHECK_FALSE(snapshot.complete);
    CHECK_FALSE(snapshot.ready());
  }

  BeginCase("cancellation is idempotent and reset clears the request");
  {
    GraphicsPreparationController controller;
    CHECK_TRUE(controller.Begin(MakeKey()));
    controller.RequestCancel();
    controller.RequestCancel();
    CHECK_TRUE(controller.CancelRequested());
    controller.Reset();
    CHECK_FALSE(controller.CancelRequested());
    CHECK_FALSE(controller.Active());
    CHECK_TRUE(controller.Begin(MakeKey(0x5841122D, 9)));
    CHECK_FALSE(controller.CancelRequested());
  }
}

void TestDelayedWorkerBarrier() {
  BeginCase("a delayed worker: the generation wait cannot return early");
  GraphicsPreparationController controller;
  CHECK_TRUE(controller.Begin(MakeKey()));
  controller.NoteCatalogue(true);
  CHECK_TRUE(controller.RegisterEntry(7));

  std::atomic<bool> returned{false};
  GraphicsPreparationPhase observed = GraphicsPreparationPhase::kIdle;
  const Clock::time_point begin = Clock::now();
  std::thread waiter([&controller, &returned, &observed]() {
    const auto progress = controller.WaitForTerminal();
    observed = progress.phase;
    returned.store(true);
  });

  std::thread worker([&controller]() {
    controller.BeginEntry(7);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    controller.SucceedEntry(7);
    controller.Finish(GraphicsPreparationPhase::kSucceeded);
  });

  // The waiter must still be blocked while the entry is running.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(returned.load());
  CHECK_EQ(controller.unfinished_entries(), 1);

  waiter.join();
  worker.join();
  const uint64_t waited_ms = MillisecondsSince(begin);
  CHECK_TRUE(returned.load());
  CHECK_TRUE(waited_ms >= 150);
  CHECK_EQ(observed, GraphicsPreparationPhase::kSucceeded);
  CHECK_TRUE(controller.Snapshot().ready());
}

void TestShutdownReleasesWaiters() {
  BeginCase("shutdown: a reset releases a waiting generation");
  GraphicsPreparationController controller;
  CHECK_TRUE(controller.Begin(MakeKey()));
  controller.NoteCatalogue(true);
  CHECK_TRUE(controller.RegisterEntry(1));
  controller.BeginEntry(1);

  std::atomic<bool> returned{false};
  std::thread waiter([&controller, &returned]() {
    controller.WaitForTerminal();
    returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(returned.load());
  const uint64_t generation_before = controller.generation();

  controller.Reset();
  waiter.join();
  CHECK_TRUE(returned.load());
  CHECK_EQ(controller.generation(), generation_before + 1);
  CHECK_FALSE(controller.Active());
  const auto after = controller.Snapshot();
  CHECK_EQ(after.entries.required, 0);
  CHECK_FALSE(after.entries.HasWork());
  CHECK_EQ(after.phase, GraphicsPreparationPhase::kCancelled);
}

void TestIdleWaitAndSnapshots() {
  BeginCase("an idle controller waits nowhere and reports an empty generation");
  GraphicsPreparationController controller;
  const auto idle = controller.WaitForTerminal();
  CHECK_FALSE(idle.active);
  CHECK_FALSE(idle.finished);
  CHECK_EQ(idle.generation, 0);
  CHECK_EQ(idle.entries.required, 0);
  CHECK_FALSE(idle.ready());

  CHECK_TRUE(controller.Begin(MakeKey()));
  controller.NoteServiceSlice();
  controller.NoteServiceSlice();
  const auto after = controller.Snapshot();
  CHECK_EQ(after.service_slices, 2);
  CHECK_TRUE(after.active);
  CHECK_TRUE(controller.Finish(GraphicsPreparationPhase::kFailed));
  // Elapsed time is published for a finished generation.
  const auto finished = controller.Snapshot();
  CHECK_TRUE(finished.finished);
  CHECK_EQ(finished.phase, GraphicsPreparationPhase::kFailed);
}

void TestTrackerCancellationStates() {
  BeginCase("the preload tracker reports cancellation as terminal, not success");
  PipelinePreloadTracker tracker;
  CHECK_TRUE(tracker.Register(1));
  CHECK_TRUE(tracker.Register(2));
  tracker.BeginWork(2);
  CHECK_EQ(tracker.CancelPending(), 1);
  auto result = tracker.Read();
  CHECK_EQ(result.required, 2);
  CHECK_EQ(result.cancelled, 1);
  CHECK_EQ(result.running, 1);
  CHECK_FALSE(result.Complete());
  CHECK_EQ(tracker.UnfinishedCount(), 1);
  // Cancellation is idempotent - the running entry was not cancelled.
  CHECK_EQ(tracker.CancelPending(), 0);
  tracker.Succeed(2);
  result = tracker.WaitUntilFinished();
  CHECK_EQ(result.succeeded, 1);
  CHECK_EQ(result.cancelled, 1);
  CHECK_TRUE(result.Finished());
  CHECK_FALSE(result.Complete());
}

}  // namespace

int main() {
  TestBeginAndGenerations();
  TestPhasesMoveForwardsOnly();
  TestProgressCountsOnlyCompletedWork();
  TestSuccessNeedsARealCatalogue();
  TestCancellation();
  TestDelayedWorkerBarrier();
  TestShutdownReleasesWaiters();
  TestIdleWaitAndSnapshots();
  TestTrackerCancellationStates();
  return Finish();
}
