// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "platform/ios_auto_pause.h"
#include <rex/chrono.h>
#include <rex/gpu/queue_lifecycle.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace rb_blitz::test;
using rex::gpu::QueueLifecycle;
using namespace std::chrono_literals;
namespace {
QueueLifecycle* pumped_gate;
unsigned pumps = 0;
bool PumpForeground() {
  if (++pumps == 2) pumped_gate->SetActive(true, [] { return true; });
  return true;
}
}
int main() {
  BeginCase("iOS inactivity queues one pause, consumed only after returning");
  rb_blitz::platform::IOSAutoPause pause;
  pause.SetInSong(true);
  CHECK_FALSE(pause.Consume());
  pause.SetActive(false);
  CHECK_FALSE(pause.Consume());
  pause.SetActive(false);
  pause.SetActive(true);
  CHECK_TRUE(pause.Consume());
  CHECK_FALSE(pause.Consume());
  pause.SetActive(true);
  CHECK_FALSE(pause.Consume());

  BeginCase("menu inactivity and a retired song never pause the next song");
  pause.SetInSong(false);
  pause.SetActive(false);
  pause.SetActive(true);
  CHECK_FALSE(pause.Consume());
  pause.SetInSong(true);
  pause.SetActive(false);
  pause.SetActive(true);
  pause.SetInSong(false);
  pause.SetInSong(true);
  CHECK_FALSE(pause.Consume());

  BeginCase("a song entering during suspension pauses when gameplay becomes available");
  pause.SetInSong(false);
  pause.SetActive(false);
  pause.SetInSong(true);
  CHECK_FALSE(pause.Consume());
  pause.SetActive(true);
  CHECK_TRUE(pause.Consume());
  for (unsigned i = 0; i < 8; ++i) {
    pause.SetActive(false);
    pause.SetActive(true);
    CHECK_TRUE(pause.Consume());
    CHECK_FALSE(pause.Consume());
  }

  BeginCase("suspending waits for an in-flight queue call before draining the GPU");
  QueueLifecycle gate;
  auto operation = std::make_unique<QueueLifecycle::Permit>(&gate);
  std::atomic<unsigned> drains = 0;
  std::promise<void> starting_suspend;
  auto suspend = std::async(std::launch::async, [&] {
    starting_suspend.set_value();
    gate.SetActive(false, [&] { ++drains; return true; });
  });
  starting_suspend.get_future().wait();
  CHECK_TRUE(suspend.wait_for(20ms) == std::future_status::timeout);
  CHECK_EQ(drains.load(), 0u);
  operation.reset();
  CHECK_TRUE(suspend.wait_for(1s) == std::future_status::ready);
  suspend.get();
  CHECK_EQ(drains.load(), 1u);

  BeginCase("background operations wait and duplicate notifications do not drain twice");
  gate.SetActive(false, [&] { ++drains; return true; });
  CHECK_EQ(drains.load(), 1u);
  std::promise<void> attempting;
  auto submit = std::async(std::launch::async, [&] {
    attempting.set_value();
    QueueLifecycle::Permit permit(&gate);
    return true;
  });
  attempting.get_future().wait();
  CHECK_TRUE(submit.wait_for(20ms) == std::future_status::timeout);
  gate.SetActive(true, [&] { ++drains; return true; });
  CHECK_TRUE(submit.wait_for(1s) == std::future_status::ready);
  CHECK_TRUE(submit.get());
  CHECK_EQ(drains.load(), 1u);

  BeginCase("repeated background/foreground cycles never strand a queue waiter");
  for (unsigned i = 0; i < 8; ++i) {
    gate.SetActive(false, [&] { ++drains; return true; });
    auto pending = std::async(std::launch::async, [&] { QueueLifecycle::Permit permit(&gate); });
    gate.SetActive(true, [] { return true; });
    CHECK_TRUE(pending.wait_for(1s) == std::future_status::ready);
    pending.get();
  }
  CHECK_EQ(drains.load(), 9u);

  BeginCase("an interrupted synchronous startup upload can pump foreground notification");
  QueueLifecycle startup_gate(PumpForeground);
  pumped_gate = &startup_gate;
  startup_gate.SetActive(false, [] { return true; });
  { QueueLifecycle::Permit resumed(&startup_gate); }
  CHECK_EQ(pumps, 2u);
  startup_gate.SetActive(false, [&] { ++drains; return true; });
  CHECK_EQ(drains.load(), 10u);
  startup_gate.SetActive(true, [] { return true; });

  BeginCase("suspension freezes guest time while host time advances; resume excludes the gap");
  using rex::Clock;
  Clock::SetGuestPaused(false);
  Clock::SetGuestPaused(true);
  const auto frozen = Clock::QueryGuestTickCount();
  const auto host = Clock::GetHostTickCount();
  std::this_thread::sleep_for(30ms);
  CHECK_EQ(Clock::QueryGuestTickCount(), frozen);
  CHECK_TRUE(Clock::GetHostTickCount() > host);
  Clock::SetGuestPaused(true);  // Duplicate notification must preserve the same frozen instant.
  CHECK_EQ(Clock::QueryGuestTickCount(), frozen);
  CHECK_EQ(Clock::ScaleGuestDurationMillis(10), 10u);
  Clock::SetGuestPaused(false);
  const auto resumed = Clock::QueryGuestTickCount();
  CHECK_TRUE(resumed >= frozen);
  CHECK_TRUE(resumed - frozen < Clock::GetGuestTickFrequency() / 50); // No 30ms jump.
  std::this_thread::sleep_for(5ms);
  CHECK_TRUE(Clock::QueryGuestTickCount() > resumed);
  return Finish();
}
