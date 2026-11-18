// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_service.h"

#include "settings/runtime_settings.h"
#include <rex/logging/log_category.h>

#include "store/store_http.h"



namespace rb_blitz::messages {
namespace {
FeedService g_service;
bool g_installed = false;
}  // namespace

void Install(const std::filesystem::path& user_data_root,
             const std::filesystem::path& executable_dir) {
  if (g_installed) return;
  g_installed = true;
  g_service.Init(user_data_root, executable_dir, RBBLITZ_GAME_VERSION,
                 [](const std::string& line) { REXLOG_INFO("{}", line); });
  std::string url = rb_blitz::settings::title::messages_api_url.Get();
  if (url.empty()) url = std::string(kFeedUrl);
  if (!IsAllowedFeedUrl(url, true)) {
    REXLOG_WARN("messages: messages_api_url rejected: {}", url);
    return;
  }
  if (url != kFeedUrl) REXLOG_INFO("messages: diagnostic feed URL {}", url);
  g_service.Start(std::move(url), [](const std::string& feed_url) {
    store::HttpOptions options;
    options.user_agent = L"rb-blitz-messages/0.1 (+https://github.com/gabrielofavero/rb-blitz-xenon-recomp)";
    options.max_body_bytes = 256 * 1024;
    auto response = store::Get(feed_url, {}, options);
    return FetchAnswer{response.status, std::move(response.body),
                       response.ok ? std::string{} : std::move(response.error)};
  });
}

std::shared_ptr<const MessageFeed> Snapshot() { return g_service.Snapshot(); }
VersionStatus UpdateOffer() { return g_service.UpdateOffer(); }
std::string StateToken() { return g_service.StateToken(); }
}  // namespace rb_blitz::messages
