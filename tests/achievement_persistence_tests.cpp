// SPDX-License-Identifier: GPL-2.0-only
// Test the runtime's achievement persistence independently of the guest and UI.
#include "check.h"

#include <chrono>
#include <filesystem>
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/ui/ui_system.h>
#include <rex/cli.h>
#include <rex/kernel/title_runtime.h>
#include <rex/ppc/image.h>
#include <rex/xam/achievement_manager.h>

namespace {
class AchievementApp final : public rex::app::App {
 public:
  explicit AchievementApp(std::filesystem::path directory)
      : App("achievement_persistence_test"), directory_(std::move(directory)) {}
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(root.Set(directory_));
    REX_TRY_VOID(GetUI().GetSettings().driver.Set("none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.Set("none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.Set("none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.Set("none"));
    REX_TRY_VOID(registry.SetText(rex::SettingLayer::kTitle, "title.entry", ""));
    REX_TRY_VOID(registry.SetText(rex::SettingLayer::kTitle, "title.game_root", directory_.string()));
    return registry.SetText(rex::SettingLayer::kTitle, "log.file", "false");
  }
 private:
  std::filesystem::path directory_;
};
}  // namespace

int main() {
  using namespace rex::xam;
  using namespace rb_blitz::test;
  namespace fs = std::filesystem;
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() /
                    ("rb_blitz-achievements-" + std::to_string(suffix));
  const auto save_path = root / "achievements" / "5841122D.toml";
  std::error_code setup_error;
  fs::create_directories(root, setup_error);
  CHECK_TRUE(!setup_error);
  AchievementApp app(root);
  CHECK_TRUE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{})).has_value());
  rex::Achievement info;
  info.id = 1;
  info.label = "Persistence test";
  uint64_t unlock_time = 0;
  const rex::ppc::PPCImage image{};

  BeginCase("achievement unlock is saved immediately, including with no notification UI");
  {
    rex::kernel::TitleRuntime runtime(image);
    CHECK_TRUE(runtime.Load().has_value());
    auto& manager = runtime.GetAchievementManager();
    CHECK_TRUE(manager.RegisterAchievement(info).has_value());
    manager.SetUnlockSavePath(save_path);
    manager.LoadUnlockState();
    CHECK_FALSE(manager.IsUnlocked(info.id));
    // kShow is the same path KernelState uses. No UI listener is required to save.
    CHECK_TRUE(manager.UnlockAchievement(info.id, AchievementNotification::kShow) ==
               AchievementUnlockResult::kUnlocked);
    CHECK_TRUE(fs::is_regular_file(save_path));
    unlock_time = manager.GetUnlockTime(info.id).value_or(0);
    CHECK_TRUE(unlock_time != 0);
  }

  BeginCase("a fresh manager restores the unlock and timestamp without notifying again");
  {
    rex::kernel::TitleRuntime runtime(image);
    CHECK_TRUE(runtime.Load().has_value());
    auto& manager = runtime.GetAchievementManager();
    CHECK_TRUE(manager.RegisterAchievement(info).has_value());
    manager.SetUnlockSavePath(save_path);
    int notifications = 0;
    auto notification = manager.RegisterNotificationCallback(
        [&](const AchievementEvent&) { ++notifications; });
    manager.LoadUnlockState();
    CHECK_TRUE(manager.IsUnlocked(info.id));
    CHECK_EQ(manager.GetUnlockTime(info.id).value_or(0), unlock_time);
    CHECK_EQ(notifications, 0);
    CHECK_TRUE(manager.UnlockAchievement(info.id, AchievementNotification::kShow) ==
               AchievementUnlockResult::kAlreadyUnlocked);
    CHECK_EQ(notifications, 0);
  }

  CHECK_EQ(app.Shutdown(), 0);
  std::error_code ec;
  fs::remove_all(root, ec);
  CHECK_TRUE(!ec);
  return Finish();
}
