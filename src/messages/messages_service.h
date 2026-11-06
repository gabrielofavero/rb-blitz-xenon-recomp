// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "messages/messages_api.h"

namespace rb_blitz::messages {

struct FetchAnswer {
  int status = 0;
  std::string body;
  std::string error;
};

// Host-side coordinator. Init publishes a local snapshot before Start returns; Start makes exactly
// one fetch on a worker. A guest consumer can take a shared, immutable snapshot without I/O.
class FeedService {
 public:
  using Fetch = std::function<FetchAnswer(const std::string&)>;
  using Log = std::function<void(const std::string&)>;

  FeedService() = default;
  ~FeedService();
  FeedService(const FeedService&) = delete;
  FeedService& operator=(const FeedService&) = delete;

  void Init(const std::filesystem::path& user_data_root,
            const std::filesystem::path& executable_dir, std::string game_version, Log log);
  bool Start(std::string url, Fetch fetch);
  void WaitForTests();
  std::shared_ptr<const MessageFeed> Snapshot() const;
  VersionStatus UpdateOffer() const;
  std::string StateToken() const;
  std::string SourceToken() const;

 private:
  void Report(const std::string& line) const;
  mutable std::mutex mutex_;
  std::filesystem::path cache_path_;
  std::string game_version_;
  Log log_;
  std::shared_ptr<const MessageFeed> snapshot_;
  std::string source_ = "none";
  std::string state_ = "idle";
  std::thread worker_;
  bool started_ = false;
};

// Windows boot adapter; implemented in messages_runtime.cpp.
void Install(const std::filesystem::path& user_data_root,
             const std::filesystem::path& executable_dir);
std::shared_ptr<const MessageFeed> Snapshot();
VersionStatus UpdateOffer();
std::string StateToken();
}  // namespace rb_blitz::messages
