// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_queue.h"
#include <atomic>
#include <chrono>
#include <future>

using namespace rb_blitz::shell_music;
using namespace std::chrono_literals;

namespace {
template <typename T> std::optional<T> Await(MenuMusicQueue<T>& queue) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto result = queue.Take()) return result;
    std::this_thread::yield();
  }
  return {};
}
}

int main() {
  rb_blitz::test::BeginCase("a blocked mount cannot block a poll or a close request");
  {
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    std::atomic<int> calls{0};
    MenuMusicQueue<int> queue;
    queue.Start([&](size_t i) -> std::optional<int> {
      ++calls;
      entered.set_value();
      gate.wait();
      return int(i);
    });
    CHECK_TRUE(queue.Idle());
    CHECK_TRUE(queue.Request(1));
    CHECK_FALSE(queue.Request(1));
    CHECK_TRUE(entered.get_future().wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(queue.Take().has_value());
    queue.Stop();
    CHECK_FALSE(queue.Take().has_value());
    CHECK_FALSE(queue.Request(2));
    release.set_value();
    CHECK_EQ(calls.load(), 1u);
  }

  rb_blitz::test::BeginCase("demand requests pause, deduplicate, prioritize and stay bounded");
  {
    MenuMusicQueue<int> queue;
    queue.Start([](size_t i) -> std::optional<int> { return int(i); });
    queue.Pause(true);
    CHECK_TRUE(queue.Request(2));
    CHECK_FALSE(queue.Request(2));
    CHECK_TRUE(queue.Request(1, true));
    CHECK_FALSE(queue.Request(3));
    CHECK_FALSE(queue.Take().has_value());
    queue.Pause(false);
    auto first = Await(queue), second = Await(queue);
    CHECK_TRUE(first.has_value());
    CHECK_TRUE(second.has_value());
    if (first) CHECK_EQ(*first, 1u);
    if (second) CHECK_EQ(*second, 2u);
    CHECK_TRUE(queue.Idle());
    CHECK_TRUE(queue.Request(1));  // a retired package can be remounted later
    const auto remounted = Await(queue);
    CHECK_TRUE(remounted.has_value());
    if (remounted) CHECK_EQ(*remounted, 1u);
  }

  rb_blitz::test::BeginCase("failed preparation releases its demand slot without an autonomous walk");
  {
    std::atomic<unsigned> calls{0};
    MenuMusicQueue<int> queue;
    queue.Start([&](size_t i) -> std::optional<int> {
      ++calls;
      return i == 1 ? std::nullopt : std::optional<int>(int(i));
    });
    CHECK_TRUE(queue.Request(1));
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!queue.Idle() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    CHECK_TRUE(queue.Idle());
    CHECK_EQ(calls.load(), 1u);
    CHECK_TRUE(queue.Request(2));
    CHECK_TRUE(Await(queue).has_value());
    CHECK_EQ(calls.load(), 2u);
  }
  return rb_blitz::test::Finish();
}
