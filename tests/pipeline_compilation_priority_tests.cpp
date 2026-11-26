// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "gpu/xenos/host/vulkan/pipeline_compilation_gate.h"
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

int main() {
  using namespace rb_blitz::test;
  using namespace std::chrono_literals;
  rex::gpu::vulkan::PipelineCompilationGate gate;
  BeginCase("a current-frame pipeline overtakes queued preparation work");
  std::promise<void> started, release;
  auto released = release.get_future();
  std::vector<int> order;
  std::thread running([&] { gate.Run(true, [&] { started.set_value(); released.wait(); }); });
  started.get_future().wait();
  std::thread background([&] { gate.Run(true, [&] { order.push_back(2); }); });
  std::thread foreground([&] { gate.Run(false, [&] { order.push_back(1); }); });
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!gate.WaitingForeground() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  CHECK_EQ(gate.WaitingForeground(), 1);
  release.set_value();
  running.join(); foreground.join(); background.join();
  CHECK_EQ(order.size(), 2); CHECK_EQ(order[0], 1); CHECK_EQ(order[1], 2);
  CHECK_EQ(gate.WaitingForeground(), 0);
  BeginCase("failed compilation releases the driver slot");
  bool caught = false;
  try { gate.Run(true, []() -> int { throw std::runtime_error("driver error"); }); }
  catch (const std::runtime_error&) { caught = true; }
  CHECK_TRUE(caught);
  CHECK_EQ(gate.Run(false, [] { return 17; }), 17);
  CHECK_EQ(gate.Run(true, [] { return 23; }), 23);
  return Finish();
}
