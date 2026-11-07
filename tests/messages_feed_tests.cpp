// SPDX-License-Identifier: GPL-2.0-only
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "check.h"
#include "messages/messages_api.h"
#include "messages/messages_cache.h"
#include "messages/messages_service.h"

using namespace rb_blitz::messages;
namespace test = rb_blitz::test;
namespace fs = std::filesystem;

namespace {
const std::string kBody = R"({"schema_version":1,"latest_version":"0.10.0","generated_at":"2026-11-06T18:00:00Z","messages":[{"id":"welcome","title":"Welcome","body":"Hello\nthere","published":"2026-11-06","icon":"icon_misc"}]})";

void Write(const fs::path& path, const std::string& value) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << value;
}

void Parser() {
  test::BeginCase("schema, bounds, dates, individual messages and version order");
  MessageFeed feed;
  std::string error;
  CHECK_TRUE(ParseFeed(kBody, &feed, &error));
  CHECK_EQ(feed.messages.size(), 1);
  CHECK_TRUE(feed.messages[0].body == "Hello\nthere");
  CHECK_TRUE(ParseFeed(R"({"schema_version":1,"latest_version":"1.0.0","generated_at":"2026-11-06T18:00:00.123Z","messages":[]})", &feed, &error));
  CHECK_TRUE(feed.generated_at == "2026-11-06T18:00:00.123Z");
  CHECK_FALSE(ParseFeed("oops", &feed, &error));
  CHECK_FALSE(ParseFeed(R"({"schema_version":2,"messages":[]})", &feed, &error));
  CHECK_FALSE(ParseFeed(R"({"schema_version":1,"messages":{}})", &feed, &error));
  std::string many = R"({"schema_version":1,"messages":[)";
  for (int i = 0; i < 33; ++i) many += (i ? ",{}" : "{}");
  many += "]}";
  CHECK_FALSE(ParseFeed(many, &feed, &error));
  CHECK_TRUE(ParseFeed(R"({"schema_version":1,"latest_version":"bad","messages":[{"id":"bad","title":"x","body":"y","published":"2026-02-30"},{"id":"ok","title":"x","body":"y","published":"2024-02-29","icon":"../bad.png"}]})", &feed, &error));
  CHECK_EQ(feed.messages.size(), 1);
  CHECK_TRUE(feed.messages[0].icon == "icon_misc");
  CHECK_TRUE(ParseFeed(R"({"schema_version":1,"messages":[{"id":"ok","title":"x","body":"y","published":"2024-02-29","icon":"icon_more_stars"},{"id":"typo","title":"x","body":"y","published":"2024-02-28","icon":"icon_more_star"}]})", &feed, &error));
  CHECK_EQ(feed.messages.size(), 2);
  CHECK_TRUE(feed.messages[0].icon == "icon_more_stars");
  CHECK_TRUE(feed.messages[1].icon == "icon_misc");
  CHECK_TRUE(feed.latest_version.empty());
  CHECK_TRUE(ParseFeed(R"feed({"schema_version":1,"messages":[{"id":"short","notification":"Welcome","title":"Welcome to version 0.1.0 :)","body":"Hello","published":"2026-11-06"},{"id":"fallback","notification":"   ","title":"Full title","body":"Hello","published":"2026-11-05"}]})feed", &feed, &error));
  CHECK_EQ(feed.messages.size(), 2);
  CHECK_TRUE(feed.messages[0].notification == "Welcome");
  CHECK_TRUE(feed.messages[1].notification.empty());
  CHECK_TRUE(feed.messages[1].title == "Full title");
  CHECK_EQ(static_cast<int>(CompareVersions("0.9.0", "0.10.0")),
           static_cast<int>(VersionStatus::kUpdateAvailable));
  CHECK_EQ(static_cast<int>(CompareVersions("1.0.0", "0.10.0")),
           static_cast<int>(VersionStatus::kLocalNewer));
  CHECK_EQ(static_cast<int>(CompareVersions("0.1.0", "0.1.0")),
           static_cast<int>(VersionStatus::kCurrent));
  CHECK_EQ(static_cast<int>(CompareVersions("0.1", "0.1.0")),
           static_cast<int>(VersionStatus::kMalformed));
  CHECK_TRUE(IsAllowedFeedUrl(kFeedUrl, false));
  CHECK_TRUE(IsAllowedFeedUrl("http://127.0.0.1:8080/feed.json", true));
  CHECK_FALSE(IsAllowedFeedUrl("http://127.0.0.1:8080/feed.json", false));
  CHECK_FALSE(IsAllowedFeedUrl("https://example.com/feed.json", true));
  CHECK_FALSE(IsAllowedFeedUrl("http://127.0.0.1:8080/feed.json?x=1", true));
}

void OfflineAndNetwork(const fs::path& scratch) {
  test::BeginCase("invalid cache falls back to bundled feed and retains it on fetch failure");
  const auto exe = scratch / "exe";
  const auto user = scratch / "user";
  Write(exe / "assets/messages/feed.json", kBody);
  Write(user / "messages/feed.json", "invalid");
  std::mutex log_mutex;
  std::vector<std::string> logs;
  auto log = [&](const std::string& line) { std::lock_guard lock(log_mutex); logs.push_back(line); };
  {
    FeedService service;
    service.Init(user, exe, "0.9.0", log);
    CHECK_TRUE(service.Snapshot() != nullptr);
    CHECK_TRUE(service.SourceToken() == "bundled");
    CHECK_EQ(static_cast<int>(service.UpdateOffer()),
             static_cast<int>(VersionStatus::kUpdateAvailable));
    CHECK_TRUE(service.Start(std::string(kFeedUrl), [](const std::string&) {
      return FetchAnswer{503, {}, {}};
    }));
    service.WaitForTests();
    CHECK_TRUE(service.SourceToken() == "bundled");
    CHECK_TRUE(service.StateToken() == "failed");
  }
  bool bundled = false, failed = false, update = false;
  for (const auto& line : logs) {
    bundled |= line.find("messages: bundled feed 1 message(s)") != std::string::npos;
    failed |= line.find("messages: fetch failed (HTTP 503); keeping bundled feed") != std::string::npos;
    update |= line.find("messages: update available") != std::string::npos;
  }
  CHECK_TRUE(bundled && failed && update);

  test::BeginCase("a fetched snapshot survives a failed atomic cache write");
  fs::remove(user / "messages/feed.json");
  fs::create_directory(user / "messages/feed.json");  // makes the destination unwritable
  {
    FeedService service;
    service.Init(user, exe, "0.9.0", log);
    CHECK_TRUE(service.SourceToken() == "bundled");
    CHECK_TRUE(service.Start(std::string(kFeedUrl), [](const std::string&) {
      return FetchAnswer{200, kBody, {}};
    }));
    service.WaitForTests();
    CHECK_TRUE(service.SourceToken() == "fetched");
    CHECK_TRUE(service.StateToken() == "ready");
    CHECK_EQ(service.Snapshot()->messages.size(), 1);
  }
  bool write_failed = false;
  for (const auto& line : logs) write_failed |= line.find("cache write failed") != std::string::npos;
  CHECK_TRUE(write_failed);

  test::BeginCase("valid cache precedes bundled feed and fetched body replaces cache");
  fs::remove_all(user / "messages/feed.json");
  Write(user / "messages/feed.json", R"({"schema_version":1,"latest_version":"0.1.0","messages":[]})");
  {
    FeedService service;
    service.Init(user, exe, "0.9.0", log);
    CHECK_TRUE(service.SourceToken() == "cached");
    CHECK_EQ(service.Snapshot()->messages.size(), 0);
    CHECK_TRUE(service.Start(std::string(kFeedUrl), [](const std::string&) {
      return FetchAnswer{200, kBody, {}};
    }));
    service.WaitForTests();
    CHECK_TRUE(service.SourceToken() == "fetched");
    MessageFeed reloaded;
    std::string error;
    CHECK_TRUE(Load(user / "messages/feed.json", &reloaded, &error));
    CHECK_EQ(reloaded.messages.size(), 1);
  }
}

void BundledBuildAndNoSource(const fs::path& build_dir, const fs::path& scratch) {
  test::BeginCase("the build-tree bundled feed is readable offline");
  MessageFeed bundled;
  std::string error;
  CHECK_TRUE(Load(build_dir / "assets/messages/feed.json", &bundled, &error));
  CHECK_TRUE(!bundled.messages.empty());
  CHECK_TRUE(bundled.latest_version == "0.1.0");
  CHECK_TRUE(bundled.messages[0].notification == "Welcome");

  test::BeginCase("without cache or bundle, a failed fetch leaves no snapshot");
  FeedService service;
  service.Init(scratch / "absent-user", scratch / "absent-exe", "0.1.0", {});
  CHECK_TRUE(service.Snapshot() == nullptr);
  CHECK_TRUE(service.Start(std::string(kFeedUrl), [](const std::string&) {
    return FetchAnswer{0, {}, "offline"};
  }));
  service.WaitForTests();
  CHECK_TRUE(service.Snapshot() == nullptr);
  CHECK_TRUE(service.SourceToken() == "none");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const fs::path scratch = fs::path(argv[1]) / "messages-feed-test-scratch";
  fs::remove_all(scratch);
  Parser();
  OfflineAndNetwork(scratch);
  BundledBuildAndNoSource(argv[1], scratch);
  fs::remove_all(scratch);
  return test::Finish();
}
