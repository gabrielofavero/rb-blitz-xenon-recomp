// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_service.h"

#include <utility>

#include "messages/messages_cache.h"

namespace rb_blitz::messages {
FeedService::~FeedService() { if (worker_.joinable()) worker_.join(); }

void FeedService::Report(const std::string& line) const { if (log_) log_("messages: " + line); }

void FeedService::Init(const std::filesystem::path& user_data_root,
                       const std::filesystem::path& executable_dir,
                       std::string game_version, Log log) {
  cache_path_ = user_data_root / "messages" / "feed.json";
  game_version_ = std::move(game_version);
  log_ = std::move(log);
  MessageFeed feed;
  std::string error;
  if (Load(cache_path_, &feed, &error)) {
    source_ = "cached";
  } else {
    Report("cached feed invalid (" + error + ")");
    const auto bundle = executable_dir / "assets" / "messages" / "feed.json";
    if (Load(bundle, &feed, &error)) source_ = "bundled";
    else Report("bundled feed invalid (" + error + ")");
  }
  if (source_ == "none") {
    Report("no valid feed; the retail inbox is untouched");
    return;
  }
  snapshot_ = std::make_shared<MessageFeed>(std::move(feed));
  Report(source_ + " feed " + std::to_string(snapshot_->messages.size()) +
         " message(s), latest " + snapshot_->latest_version);
  for (const auto& warning : snapshot_->warnings) Report(warning);
  Report("update " + std::string(VersionStatusToken(UpdateOffer())) + ": current " +
         game_version_ + " vs latest " + snapshot_->latest_version);
}

bool FeedService::Start(std::string url, Fetch fetch) {
  if (started_ || !fetch || !IsAllowedFeedUrl(url, true)) return false;
  started_ = true;
  {
    std::lock_guard lock(mutex_);
    state_ = "fetching";
  }
  worker_ = std::thread([this, url = std::move(url), fetch = std::move(fetch)] {
    FetchAnswer answer = fetch(url);
    MessageFeed feed;
    std::string error;
    if (!answer.error.empty()) error = answer.error;
    else if (answer.status != 200) error = "HTTP " + std::to_string(answer.status);
    else if (!ParseFeed(answer.body, &feed, &error)) {}
    if (!error.empty()) {
      std::string source;
      {
        std::lock_guard lock(mutex_);
        state_ = "failed";
        source = source_;
      }
      Report("fetch failed (" + error + "); keeping " + source + " feed");
      return;
    }
    auto published = std::make_shared<MessageFeed>(std::move(feed));
    {
      std::lock_guard lock(mutex_);
      snapshot_ = published;
      source_ = "fetched";
      state_ = "ready";
    }
    Report("fetched " + std::to_string(published->messages.size()) + " message(s), latest " +
           published->latest_version);
    for (const auto& warning : published->warnings) Report(warning);
    Report("update " + std::string(VersionStatusToken(UpdateOffer())) + ": current " +
           game_version_ + " vs latest " + published->latest_version);
    if (!Save(cache_path_, answer.body, &error)) Report("cache write failed (" + error + ")");
  });
  return true;
}

void FeedService::WaitForTests() { if (worker_.joinable()) worker_.join(); }

std::shared_ptr<const MessageFeed> FeedService::Snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}
VersionStatus FeedService::UpdateOffer() const {
  auto feed = Snapshot();
  return feed ? CompareVersions(game_version_, feed->latest_version) : VersionStatus::kMalformed;
}
std::string FeedService::StateToken() const { std::lock_guard lock(mutex_); return state_; }
std::string FeedService::SourceToken() const { std::lock_guard lock(mutex_); return source_; }
}  // namespace rb_blitz::messages
