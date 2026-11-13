// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "ui/notifications.h"
#include "store/store_catalogue_status.h"
#include "store/store_search.h"
#include "store/store_settings.h"

#include <thread>
#include <vector>

namespace n = rb_blitz::ui::notifications;
namespace s = rb_blitz::store;
using namespace std::chrono_literals;

void TestTiming() {
  rb_blitz::test::BeginCase("presentation clock, pop animation, duplicate suppression, cooldown");
  n::Queue queue;
  const auto start = n::Queue::Clock::now();
  const n::Notice info{"info", n::Level::kInfo, "Title", "Description"};
  queue.Post(info, start);
  const auto shown = start + 1h;
  CHECK_EQ(queue.Tick(shown)->visibility, 0);
  CHECK_TRUE(queue.Tick(shown + 125ms)->visibility > 0);
  CHECK_TRUE(queue.Tick(shown + 125ms)->visibility < 1);
  CHECK_EQ(queue.Tick(shown + 250ms)->visibility, 1);
  queue.Post(info, shown + 5s);  // Polling/repeated failures cannot extend reading time.
  CHECK_TRUE(queue.Tick(shown + 5900ms)->visibility < 1);
  CHECK_FALSE(queue.Tick(shown + 6s).has_value());
  queue.Post(info, shown + 7s);
  CHECK_FALSE(queue.Tick(shown + 7s).has_value());
  queue.Post(info, shown + 37s);
  CHECK_TRUE(queue.Tick(shown + 37s).has_value());
  queue.Dismiss("info", shown + 38s);
  queue.Post(info, shown + 38100ms);  // A retried operation gets fresh feedback.
  CHECK_EQ(queue.Tick(shown + 38100ms)->visibility, 0);
  CHECK_TRUE(queue.Tick(shown + 38500ms).has_value());
  queue.Dismiss("info", shown + 39s);
  CHECK_FALSE(queue.Tick(shown + 40s).has_value());
  queue.Post(info, shown + 41s);
  CHECK_TRUE(queue.Tick(shown + 41s).has_value());
}

void TestQueue() {
  rb_blitz::test::BeginCase("severity priority, FIFO ties, capacity, replacement and dismissal");
  n::Queue queue;
  const auto now = n::Queue::Clock::now();
  for (size_t i = 0; i < n::Queue::kCapacity; ++i)
    queue.Post({std::to_string(i), n::Level::kError, "Error", "Details"}, now);
  queue.Post({"noise", n::Level::kInfo, "Info", "Details"}, now);
  for (size_t i = 0; i < n::Queue::kCapacity; ++i) {
    auto frame = queue.Tick(now + std::chrono::seconds(10 * i));
    CHECK_TRUE(frame.has_value());
    CHECK_TRUE(frame->notice.key == std::to_string(i));
  }
  CHECK_FALSE(queue.Tick(now + 200s).has_value());

  queue.Post({"info", n::Level::kInfo, "Info", "Details"}, now);
  queue.Post({"warning", n::Level::kWarning, "Warning", "Details"}, now);
  queue.Post({"error", n::Level::kError, "Error", "Details"}, now);
  CHECK_TRUE(queue.Tick(now)->notice.key == "error");
  queue.Dismiss("error", now + 1s);
  CHECK_TRUE(queue.Tick(now + 1100ms)->visibility < 1);
  CHECK_TRUE(queue.Tick(now + 1400ms)->notice.key == "warning");
  queue.Post({"warning", n::Level::kInfo, "Recovered", "New details"}, now + 2s);
  CHECK_TRUE(queue.Tick(now + 2s)->notice.title == "Recovered");
  queue.Dismiss("info", now + 2s);
  CHECK_FALSE(queue.Tick(now + 9s).has_value());
}

void TestThreads() {
  rb_blitz::test::BeginCase("concurrent producers coalesce the same event");
  n::Queue queue;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
    for (int j = 0; j < 100; ++j)
      queue.Post({"same", n::Level::kError, "Error", "Details"});
  });
  for (auto& thread : threads) thread.join();
  const auto now = n::Queue::Clock::now();
  CHECK_TRUE(queue.Tick(now).has_value());
  CHECK_FALSE(queue.Tick(now + 11s).has_value());
}

void TestCatalogue() {
  rb_blitz::test::BeginCase("catalogue surface gate, loading coalescence, failure and recovery");
  auto& queue = n::SharedQueue();
  s::ResetCatalogueStatus();
  queue.Clear();
  s::PublishCatalogueFetchState(s::CatalogueFetchState::kFailed, 0, 0, "HTTP 503");
  CHECK_FALSE(queue.Tick().has_value());
  s::NoteStoreSurface(s::StoreSurface::kFrontMenu);
  auto frame = queue.Tick();
  CHECK_TRUE(frame.has_value());
  CHECK_TRUE(frame->notice.level == n::Level::kError);
  CHECK_TRUE(frame->notice.key == "store.catalogue");
  s::PublishCatalogueFetchState(s::CatalogueFetchState::kFetching, 0, 0);
  CHECK_TRUE(queue.Tick()->notice.level == n::Level::kInfo);
  s::PublishCatalogueFetchState(s::CatalogueFetchState::kFetching, 2, 0);
  CHECK_TRUE(queue.Tick()->notice.description == "Loading the song catalogue...");
  s::PublishCatalogueFetchState(s::CatalogueFetchState::kReady, 3, 15);
  CHECK_FALSE(queue.Tick(n::Queue::Clock::now() + 1s).has_value());
  s::PublishCatalogueFetchState(s::CatalogueFetchState::kReady, 3, 15, "HTTP 503");
  CHECK_TRUE(queue.Tick()->notice.level == n::Level::kWarning);
  queue.Clear();
  s::ResetCatalogueStatus();
  s::NoteStoreSurface(s::StoreSurface::kFrontMenu);
  s::PublishCatalogueFetchState(s::CatalogueFetchState::kReady, 1, 0);
  CHECK_TRUE(queue.Tick()->notice.title == "No RhythmVerse songs found");
  queue.Clear();
}

void TestSearch() {
  rb_blitz::test::BeginCase("search outcomes survive keyboard close; stale responses stay silent");
  auto& queue = n::SharedQueue();
  queue.Clear();
  s::SearchCancel();
  s::StoreSettings settings;
  settings.enabled = true;
  s::PublishSettings(settings);
  s::SearchOpen();
  CHECK_FALSE(s::SearchEditingText().empty());
  CHECK_FALSE(queue.Tick().has_value());
  CHECK_FALSE(s::SearchSubmitQuery().started);
  CHECK_TRUE(queue.Tick()->notice.level == n::Level::kWarning);
  CHECK_TRUE(s::SearchEditingText().empty());
  queue.Clear();
  CHECK_TRUE(s::SearchSetText("test song"));
  auto submit = s::SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  s::SearchClose();
  CHECK_TRUE(s::SearchEditingText().empty());
  CHECK_TRUE(s::SearchFail(submit.generation, "HTTP 503"));
  CHECK_TRUE(queue.Tick()->notice.level == n::Level::kError);
  CHECK_TRUE(s::SearchStatusText().empty());  // Old caption visibility cannot swallow the error.
  queue.Clear();
  s::SearchCancel();
  CHECK_FALSE(s::SearchFail(submit.generation, "late response"));
  CHECK_FALSE(queue.Tick().has_value());
  CHECK_TRUE(s::SearchSetText("test song"));
  submit = s::SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  settings.instruments.guitar = !settings.instruments.guitar;
  s::PublishSettings(settings);
  CHECK_FALSE(s::SearchFail(submit.generation, "old filter's failure"));
  CHECK_FALSE(queue.Tick().has_value());
  s::SearchCancel();
  CHECK_TRUE(s::SearchSetText("test song"));
  submit = s::SearchSubmitQuery();
  CHECK_TRUE(submit.started);
  s::ListingPage empty;
  CHECK_TRUE(s::SearchAccept(submit.generation, empty));
  CHECK_TRUE(queue.Tick()->notice.title == "No search results");
  queue.Clear();
  s::SearchCancel();
}

int main() {
  TestTiming();
  TestQueue();
  TestThreads();
  TestCatalogue();
  TestSearch();
  return rb_blitz::test::Finish();
}
