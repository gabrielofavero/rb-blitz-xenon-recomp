// Exercise the actual worker handoff without a GPU or game navigation.
#include <rex/graphics/pipeline_completion_queue.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "check.h"

using namespace rb_blitz::test;
using rex::graphics::PipelineCompletionQueue;
using namespace std::chrono_literals;

namespace {
void TestUnfinishedDriverDoesNotBlockSubmission() {
  BeginCase("taking completions does not wait for an unfinished driver call");
  PipelineCompletionQueue<int> queue;
  std::promise<void> started, release;
  auto gate = release.get_future();
  std::thread worker([&] {
    started.set_value();
    gate.wait();  // A driver compile that cannot be interrupted or time-bounded.
    queue.Push(42);
  });
  started.get_future().wait();
  auto submission = std::async(std::launch::async, [&] { return queue.Take(); });
  // Release the simulated driver even on failure, so a regression cannot hang
  // the test process. The deadline is a deadlock guard, not a timing benchmark.
  bool returned_before_driver = submission.wait_for(2s) == std::future_status::ready;
  CHECK_TRUE(returned_before_driver);
  if (returned_before_driver) CHECK_TRUE(submission.get().empty());
  release.set_value();
  worker.join();
  if (!returned_before_driver) submission.get();
  const auto completed = queue.Take();
  if (returned_before_driver) {
    CHECK_EQ(completed.size(), 1);
    if (!completed.empty()) CHECK_EQ(completed.front(), 42);
  }
  CHECK_TRUE(queue.Take().empty());
}

void TestCompletedWorkIsAvailableBehindSlowWorker() {
  BeginCase("a slow worker does not hold back another worker's completion");
  PipelineCompletionQueue<int> queue;
  std::promise<void> release;
  auto gate = release.get_future();
  std::thread slow([&] { gate.wait(); queue.Push(1); });
  std::thread fast([&] { queue.Push(2); });
  fast.join();
  auto first = queue.Take();
  CHECK_EQ(first.size(), 1);
  if (!first.empty()) CHECK_EQ(first.front(), 2);
  // Taken batches remain owned by the consumer after later worker pushes.
  release.set_value();
  slow.join();
  auto second = queue.Take();
  CHECK_EQ(second.size(), 1);
  if (!second.empty()) CHECK_EQ(second.front(), 1);
  if (!first.empty()) CHECK_EQ(first.front(), 2);
  CHECK_TRUE(queue.Take().empty());
}

void TestConcurrentCompletionsAreTakenExactlyOnce() {
  BeginCase("concurrent workers and submission drains lose no completed handles");
  PipelineCompletionQueue<int> queue;
  constexpr int kWorkers = 4, kPerWorker = 1000;
  std::vector<std::thread> workers;
  for (int w = 0; w < kWorkers; ++w) {
    workers.emplace_back([&, w] {
      for (int i = 0; i < kPerWorker; ++i) queue.Push(w * kPerWorker + i);
    });
  }
  std::vector<int> all;
  // Interleave consumer drains with worker pushes without waiting for them.
  for (int i = 0; i < 100; ++i) {
    auto batch = queue.Take();
    all.insert(all.end(), batch.begin(), batch.end());
    std::this_thread::yield();
  }
  for (auto& worker : workers) worker.join();
  auto last = queue.Take();  // Shutdown drains after joining the workers.
  all.insert(all.end(), last.begin(), last.end());
  std::sort(all.begin(), all.end());
  CHECK_EQ(all.size(), kWorkers * kPerWorker);
  for (size_t i = 0; i < all.size(); ++i) CHECK_EQ(all[i], i);
  CHECK_TRUE(queue.Take().empty());
}
}  // namespace

int main() {
  TestUnfinishedDriverDoesNotBlockSubmission();
  TestCompletedWorkIsAvailableBehindSlowWorker();
  TestConcurrentCompletionsAreTakenExactlyOnce();
  return Finish();
}
