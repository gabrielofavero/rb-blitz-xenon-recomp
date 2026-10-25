// SPDX-License-Identifier: GPL-2.0-only
// Test the runtime's achievement persistence independently of the guest and UI.
#include "check.h"

#include <chrono>
#include <filesystem>
#include <rex/system/achievement_manager.h>

int main() {
  using namespace rex::system;
  using namespace rb_blitz::test;
  namespace fs = std::filesystem;
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() /
                    ("rb_blitz-achievements-" + std::to_string(suffix));
  const auto save_path = root / "achievements" / "5841122D.toml";
  AchievementInfo info;
  info.id = 1;
  info.label = "Persistence test";
  uint64_t unlock_time = 0;

  BeginCase("achievement unlock is saved immediately, including with no notification UI");
  {
    AchievementManager manager;
    manager.RegisterAchievement(info);
    manager.SetUnlockSavePath(save_path);
    manager.LoadUnlockState();
    CHECK_FALSE(manager.IsUnlocked(info.id));
    // kShow is the same path KernelState uses. No UI listener is required to save.
    CHECK_TRUE(manager.UnlockAchievement(info.id, AchievementNotification::kShow) ==
               AchievementUnlockResult::kUnlocked);
    CHECK_TRUE(fs::is_regular_file(save_path));
    unlock_time = manager.GetUnlockTime(info.id);
    CHECK_TRUE(unlock_time != 0);
  }

  BeginCase("a fresh manager restores the unlock and timestamp without notifying again");
  {
    AchievementManager manager;
    manager.RegisterAchievement(info);
    manager.SetUnlockSavePath(save_path);
    int notifications = 0;
    manager.RegisterNotificationCallback([&](const AchievementEvent&) { ++notifications; });
    manager.LoadUnlockState();
    CHECK_TRUE(manager.IsUnlocked(info.id));
    CHECK_EQ(manager.GetUnlockTime(info.id), unlock_time);
    CHECK_EQ(notifications, 0);
    CHECK_TRUE(manager.UnlockAchievement(info.id, AchievementNotification::kShow) ==
               AchievementUnlockResult::kAlreadyUnlocked);
    CHECK_EQ(notifications, 0);
  }

  std::error_code ec;
  fs::remove_all(root, ec);
  return Finish();
}
