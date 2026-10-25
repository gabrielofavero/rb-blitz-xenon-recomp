// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for rexglue-sdk/include/rex/graphics/pipeline_preload.h - the
// accounting and the completion barrier both pipeline caches use for a shader
// storage preload (docs/plans/finished/2026-10-10-shader-cache-plan.md, P1).
//
// These exercise the component the D3D12 and Vulkan caches actually call, with
// real threads and real waits - not a copy of the logic. The cases mirror the
// defects the plan names:
//
//   * the worker-count arithmetic that the D3D12 growth loop could not express
//     (one entry, or an already-large pool, asks for no new worker);
//   * the barrier closing on explicit entry states, so "the queue is empty" or
//     "a worker stopped" is never mistaken for readiness - including the
//     no-growth case, synchronous creation and a delayed worker;
//   * failure, missing data, duplicates and terminal states;
//   * shutdown: a reset releases a waiting barrier instead of deadlocking it.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "check.h"

#include <rex/graphics/pipeline_preload.h>

namespace {

using namespace rb_blitz::test;
using rex::graphics::PipelinePreloadTracker;
using rex::graphics::PreloadCreationThreadCount;

using Clock = std::chrono::steady_clock;

uint64_t MillisecondsSince(Clock::time_point begin) {
  return uint64_t(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count());
}

// The one-entry / large-pool shapes that the old D3D12 growth loop could not
// express: `while (size < original_count)` never grew the pool, and the blocking
// wait lived inside the branch that required growth.
void TestCreationThreadCount() {
  BeginCase("worker count: one entry or a full pool asks for no new worker");
  CHECK_EQ(PreloadCreationThreadCount(0, 8, 0), 0);
  CHECK_EQ(PreloadCreationThreadCount(1, 8, 0), 0);
  CHECK_EQ(PreloadCreationThreadCount(1, 8, 3), 3);
  CHECK_EQ(PreloadCreationThreadCount(2, 2, 0), 1);
  // An unknown core count is treated as one processor, so the calling thread
  // does the work and no worker is asked for.
  CHECK_EQ(PreloadCreationThreadCount(3, 0, 0), 0);

  BeginCase("worker count: a burst grows to one worker fewer than the cores");
  CHECK_EQ(PreloadCreationThreadCount(100, 8, 0), 7);
  CHECK_EQ(PreloadCreationThreadCount(100, 8, 3), 7);
  CHECK_EQ(PreloadCreationThreadCount(100, 1, 0), 0);
  CHECK_EQ(PreloadCreationThreadCount(100, 1, 2), 2);
}

void TestEmptyAndSynchronous() {
  BeginCase("an empty preload is finished but not completion");
  {
    PipelinePreloadTracker tracker;
    const auto result = tracker.WaitUntilFinished();
    CHECK_EQ(result.required, 0);
    CHECK_EQ(result.succeeded, 0);
    CHECK_FALSE(result.HasWork());
    CHECK_TRUE(result.Finished());
    CHECK_FALSE(result.Complete());
  }

  BeginCase("synchronous creation resolves without a worker");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(11));
    tracker.Succeed(11);
    const auto result = tracker.WaitUntilFinished();
    CHECK_EQ(result.required, 1);
    CHECK_EQ(result.succeeded, 1);
    CHECK_EQ(result.queued, 0);
    CHECK_EQ(result.running, 0);
    CHECK_TRUE(result.Finished());
    CHECK_TRUE(result.Complete());
  }

  BeginCase("queued and running entries are not ready");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(1));
    CHECK_TRUE(tracker.Register(2));
    tracker.BeginWork(2);
    const auto snapshot = tracker.Read();
    CHECK_EQ(snapshot.required, 2);
    CHECK_EQ(snapshot.queued, 1);
    CHECK_EQ(snapshot.running, 1);
    CHECK_EQ(snapshot.succeeded, 0);
    CHECK_FALSE(snapshot.Finished());
    CHECK_FALSE(snapshot.Complete());
    CHECK_EQ(tracker.UnfinishedCount(), 2);
    tracker.Succeed(2);
    tracker.Fail(1);
    const auto finished = tracker.WaitUntilFinished();
    CHECK_TRUE(finished.Finished());
    CHECK_FALSE(finished.Complete());
    CHECK_EQ(finished.failed, 1);
  }
}

void TestDelayedWorker() {
  BeginCase("a delayed worker: the barrier cannot return before it finishes");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(7));
    const Clock::time_point begin = Clock::now();
    std::thread worker([&tracker]() {
      tracker.BeginWork(7);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      tracker.Succeed(7);
    });
    const auto result = tracker.WaitUntilFinished();
    const uint64_t waited_ms = MillisecondsSince(begin);
    worker.join();
    CHECK_TRUE(waited_ms >= 150);
    CHECK_EQ(result.required, 1);
    CHECK_EQ(result.succeeded, 1);
    CHECK_TRUE(result.Complete());
  }
}

void TestMultiWorker() {
  BeginCase("four workers resolve one hundred entries");
  {
    PipelinePreloadTracker tracker;
    for (uint64_t identity = 0; identity < 100; ++identity) {
      CHECK_TRUE(tracker.Register(identity));
    }
    std::vector<std::thread> workers;
    for (uint64_t slot = 0; slot < 4; ++slot) {
      workers.emplace_back([&tracker, slot]() {
        for (uint64_t identity = slot; identity < 100; identity += 4) {
          tracker.BeginWork(identity);
          if (identity % 10 == 0) {
            tracker.Fail(identity);
          } else {
            tracker.Succeed(identity);
          }
        }
      });
    }
    for (std::thread& worker : workers) {
      worker.join();
    }
    const auto result = tracker.WaitUntilFinished();
    CHECK_EQ(result.required, 100);
    CHECK_EQ(result.succeeded, 90);
    CHECK_EQ(result.failed, 10);
    CHECK_EQ(result.queued, 0);
    CHECK_EQ(result.running, 0);
    CHECK_TRUE(result.Finished());
    CHECK_FALSE(result.Complete());
  }
}

void TestFailuresMissingAndDuplicates() {
  BeginCase("failure and missing data are not readiness");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(1));
    CHECK_TRUE(tracker.Register(2));
    tracker.BeginWork(1);
    tracker.Fail(1);
    tracker.Miss(2);
    const auto result = tracker.WaitUntilFinished();
    CHECK_EQ(result.required, 2);
    CHECK_EQ(result.succeeded, 0);
    CHECK_EQ(result.failed, 1);
    CHECK_EQ(result.missing, 1);
    CHECK_TRUE(result.Finished());
    CHECK_FALSE(result.Complete());
  }

  BeginCase("a duplicate request is refused, not required twice");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(5));
    CHECK_FALSE(tracker.Register(5));
    tracker.Succeed(5);
    const auto result = tracker.WaitUntilFinished();
    CHECK_EQ(result.required, 1);
    CHECK_EQ(result.succeeded, 1);
    CHECK_EQ(result.duplicates, 1);
    CHECK_TRUE(result.Complete());
  }

  BeginCase("terminal states are final and unknown identities are ignored");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(3));
    tracker.BeginWork(3);
    tracker.Succeed(3);
    tracker.Fail(3);
    tracker.Miss(3);
    tracker.BeginWork(99);
    tracker.Succeed(99);
    const auto result = tracker.Read();
    CHECK_EQ(result.required, 1);
    CHECK_EQ(result.succeeded, 1);
    CHECK_EQ(result.failed, 0);
    CHECK_EQ(result.missing, 0);
    CHECK_TRUE(result.Complete());

    // A failure arriving after a success cannot downgrade it, and a success
    // cannot resolve a failed entry either.
    CHECK_TRUE(tracker.Register(4));
    tracker.BeginWork(4);
    tracker.Fail(4);
    tracker.Succeed(4);
    const auto failed = tracker.Read();
    CHECK_EQ(failed.failed, 1);
    CHECK_EQ(failed.succeeded, 1);
    CHECK_FALSE(failed.Complete());
  }
}

void TestShutdownReleasesWaiters() {
  BeginCase("shutdown: a reset releases a waiting barrier");
  {
    PipelinePreloadTracker tracker;
    CHECK_TRUE(tracker.Register(1));
    tracker.BeginWork(1);
    std::atomic<bool> returned{false};
    std::thread waiter([&tracker, &returned]() {
      tracker.WaitUntilFinished();
      returned.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(returned.load());
    tracker.Reset();
    waiter.join();
    CHECK_TRUE(returned.load());
    const auto after = tracker.Read();
    CHECK_EQ(after.required, 0);
    CHECK_FALSE(after.HasWork());
  }
}

}  // namespace

int main() {
  TestCreationThreadCount();
  TestEmptyAndSynchronous();
  TestDelayedWorker();
  TestMultiWorker();
  TestFailuresMissingAndDuplicates();
  TestShutdownReleasesWaiters();
  return Finish();
}
