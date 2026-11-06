// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::messages {

inline constexpr std::string_view kFeedUrl =
    "https://gabrielofavero.github.io/rb-blitz-xenon-recomp/feed.json";

struct Message {
  std::string id;
  std::string title;
  std::string body;
  std::string published;
  std::string icon = "icon_misc";
};

struct MessageFeed {
  int schema_version = 1;
  std::string latest_version;
  std::string generated_at;
  std::vector<Message> messages;
  std::vector<std::string> warnings;
};

enum class VersionStatus { kCurrent, kUpdateAvailable, kLocalNewer, kMalformed };

// Structural failures reject the source. Bad individual messages are dropped; a bad version or
// timestamp leaves the messages usable and is reported through warnings.
bool ParseFeed(std::string_view body, MessageFeed* feed, std::string* error);
VersionStatus CompareVersions(std::string_view current, std::string_view latest);
std::string_view VersionStatusToken(VersionStatus status);
// The production URL is exact. Only a diagnostic override may use loopback HTTP.
bool IsAllowedFeedUrl(std::string_view url, bool diagnostic_override);

}  // namespace rb_blitz::messages
