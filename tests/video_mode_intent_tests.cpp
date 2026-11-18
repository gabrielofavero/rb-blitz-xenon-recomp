// SPDX-License-Identifier: GPL-2.0-only
// Explicit guest dimensions retain their intent even when equal to SDK defaults.
#include "check.h"
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/settings_registry.h>
#include <rex/ui/ui_system.h>
#include <chrono>
#include <filesystem>
#include <vector>

namespace {
class VideoApp final : public rex::app::App {
 public:
  VideoApp(const std::filesystem::path& folder, int scenario)
      : App("video_intent_test"), folder_(folder), scenario_(scenario) {}
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(root.Set(folder_));
    REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, "log.file", "false"));
    REX_TRY_VOID(GetUI().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    auto& ui = GetUI().GetSettings();
    REX_TRY_VOID(ui.resolution.SetText(rex::SettingLayer::kTitle, "1920x1080"));
    if (scenario_ > 0) {
      REX_TRY_VOID(ui.video_mode_width.SetText(rex::SettingLayer::kFile,
                                               scenario_ == 2 ? "1152" : "1280"));
      REX_TRY_VOID(ui.video_mode_height.SetText(rex::SettingLayer::kFile, "720"));
    }
    if (scenario_ == 3) {
      REX_TRY_VOID(ui.video_mode_width.Reset());
      REX_TRY_VOID(ui.video_mode_height.Reset());
    }
    return rex::Ok();
  }
 private:
  std::filesystem::path folder_;
  int scenario_;
};
}

int main() {
  using namespace rb_blitz::test;
  const auto folder = std::filesystem::temp_directory_path() /
      ("rb_blitz_video_intent_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(folder);
  for (int scenario = 0; scenario < 4; ++scenario) {
    BeginCase(scenario == 0 ? "unset dimensions follow resolution" :
              scenario == 1 ? "explicit SDK-default dimensions override resolution" :
              scenario == 2 ? "explicit 16:10 dimensions preserve native intent" :
                              "reset restores resolution fallback");
    VideoApp app(folder, scenario);
    const auto initialized = app.Init(rex::CLIArgList::Parse(std::vector<std::string>{}));
    CHECK_TRUE(initialized.has_value());
    if (!initialized) { app.Shutdown(); break; }
    const auto mode = app.GetUI().GetVideoMode();
    CHECK_EQ(mode.width, scenario == 1 ? 1280u : scenario == 2 ? 1152u : 1920u);
    CHECK_EQ(mode.height, (scenario == 1 || scenario == 2) ? 720u : 1080u);
    CHECK_TRUE(app.GetUI().GetSettings().video_mode_width.GetLayer() ==
        ((scenario == 1 || scenario == 2) ? rex::SettingLayer::kFile : rex::SettingLayer::kDefault));
    CHECK_EQ(app.Shutdown(), 0);
  }
  std::filesystem::remove_all(folder);
  return Finish();
}
