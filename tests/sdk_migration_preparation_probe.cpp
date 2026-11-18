// SPDX-License-Identifier: GPL-2.0-only
#include <filesystem>
#include <string>
#include <vector>

#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/gpu/graphics_preparation_service.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/settings_registry.h>
#include <rex/ui/ui_system.h>

namespace {
class PreparationApp final : public rex::app::App {
 public:
  PreparationApp() : App("preparation_probe") {}

  rex::gpu::IGraphicsPreparation* Preparation() {
    return GetControlFactory().Get<rex::gpu::IGraphicsPreparation>();
  }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    REX_TRY_VOID(GetUI().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    return registry.SetText(rex::SettingLayer::kTitle, "log.file", "false");
  }
};
}  // namespace

int main() {
  PreparationApp app;
  if (!app.Init(rex::CLIArgList::Parse(std::vector<std::string>{}))) return 1;
  auto* service = app.Preparation();
  if (!service || !service->SetShaderStorageDeferred(true)) return 2;
  if (service->BeginShaderStoragePreparation({}, 0x5841122D, 1)) return 3;
  service->RequestCancelShaderStoragePreparation();
  if (service->QueryShaderStoragePreparation().phase != rex::gpu::GraphicsPreparationPhase::kIdle)
    return 4;
  return app.Shutdown();
}
