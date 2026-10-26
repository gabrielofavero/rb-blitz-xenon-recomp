// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_queue.h"
#include "hooks/menu_music_pool.h"
#include "hooks/menu_music_playback.h"

#include <atomic>
#include <chrono>
#include <future>

using namespace rb_blitz::shell_music;
using namespace std::chrono_literals;

int main() {
  rb_blitz::test::BeginCase("a blocked mount cannot block a poll or a close request");
  {
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    std::atomic<int> calls{0};
    MenuMusicQueue<int> queue;
    CHECK_FALSE(queue.Take().has_value());
    queue.Start(1, 50, [&](size_t i) -> std::optional<int> {
      ++calls;
      entered.set_value();
      gate.wait();
      return int(i);
    });
    CHECK_TRUE(entered.get_future().wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(queue.Take().has_value());
    queue.Stop();  // must return before release, even with a mount in flight
    CHECK_FALSE(queue.Take().has_value());
    release.set_value();
    CHECK_EQ(calls.load(), 1u);
  }

  rb_blitz::test::BeginCase("prefetch stays bounded and advances only when a result is consumed");
  {
    std::promise<void> second, third;
    auto second_ready = second.get_future();
    auto third_ready = third.get_future();
    MenuMusicQueue<int> queue;
    queue.Start(1, 4, [&](size_t i) -> std::optional<int> {
      if (i == 2) second.set_value();
      if (i == 3) third.set_value();
      return int(i);
    });
    CHECK_TRUE(second_ready.wait_for(5s) == std::future_status::ready);
    CHECK_TRUE(third_ready.wait_for(20ms) == std::future_status::timeout);
    const auto first = queue.Take();
    CHECK_TRUE(first.has_value());
    if (first) CHECK_EQ(*first, 1u);
    CHECK_TRUE(third_ready.wait_for(5s) == std::future_status::ready);
    // Stop discards queued publication and rejects a second start.
    queue.Stop();
    queue.Start(0, 1, [](size_t) -> std::optional<int> { return 99; });
    CHECK_FALSE(queue.Take().has_value());
  }

  rb_blitz::test::BeginCase("failed packages do not end the background walk");
  {
    std::promise<void> done;
    MenuMusicQueue<int> queue;
    queue.Start(1, 4, [&](size_t i) -> std::optional<int> {
      if (i < 3) return std::nullopt;
      done.set_value();
      return int(i);
    });
    CHECK_TRUE(done.get_future().wait_for(5s) == std::future_status::ready);
    std::optional<int> result;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!result && std::chrono::steady_clock::now() < deadline) {
      result = queue.Take();
      std::this_thread::yield();
    }
    CHECK_TRUE(result.has_value());
    if (result) CHECK_EQ(*result, 3u);
  }

  rb_blitz::test::BeginCase("growing the pool preserves playback history and a captured press");
  std::mt19937 rng(42);
  Rotation rotation(rng);
  rotation.SetPool({"ActualStartupKey"});
  CHECK_EQ(rotation.Next(), 0u);
  PlaySelection selection;
  selection.Loading(0, 0);
  selection.Audible(0);
  CHECK_EQ(selection.Capture(10), 0u);
  rotation.SetPool({"ActualStartupKey", "QueuedKey", "AnotherQueuedKey"});
  CHECK_EQ(selection.Query(11), 0u);
  const auto next = rotation.Next();
  CHECK_TRUE(next == 1 || next == 2);
  CHECK_EQ(rotation.Next(), next == 1 ? 2u : 1u);
  return rb_blitz::test::Finish();
}
