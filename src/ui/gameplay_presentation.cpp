// SPDX-License-Identifier: GPL-2.0-only
#include "settings/runtime_values.h"
#include "ui/gameplay_presentation.h"
#include "settings/runtime_settings.h"
#include <rex/logging/log_category.h>
#include <rex/ui/ui_system.h>
#include <rex/gpu/graphics_presentation.h>
#include "hooks/render_settings.h"
#include "input/native_menu.h"



namespace rb_blitz::ui {
namespace {
class GameplayPresentation final : public rex::ui::DialogBase {
 public:
  GameplayPresentation(std::function<rex::gpu::IGraphicsPresentation*()> presentation,
                       std::function<void()> preview_notification)
      : presentation_(std::move(presentation)),
        preview_notification_(std::move(preview_notification)) {}
 private:
  bool Draw(ImGuiIO&) override {
    const bool game = input::GameScreenActive();
    const bool crop = settings::BootPortraitWidth() && (game || input::PortraitSplashActive());
    const bool render_cutoff = crop && game && rb_blitz::settings::title::portrait_render_cutoff.Get();
    if (crop == cropped_ && render_cutoff == render_cutoff_) return true;
    if (!presentation_()) return true;
    rb_blitz::settings::WriteRuntimeSetting("draw_visible_region", "false", rex::SettingLayer::kRuntime).accepted();
    if (render_cutoff) {
      const auto height = settings::BootRenderHeight();
      const auto width = settings::BootRenderWidth();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_source_width",
                             std::to_string(width ? width : height * 16 / 9), rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_source_height", std::to_string(height), rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_width", std::to_string(settings::BootPortraitWidth()), rex::SettingLayer::kRuntime).accepted();
      // Measured main-frame EDRAM pair. Dimensions and viewport/tile checks in
      // the backend exclude the shadow, bloom, downsample and helper passes.
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_color_info", "608", rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_depth_info", "66864", rex::SettingLayer::kRuntime).accepted(); // 0x10530
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_region", "true", rex::SettingLayer::kRuntime).accepted();
    }
    if (crop && !cropped_) {
      safe_x_ = rb_blitz::settings::InspectRuntimeSetting("present_safe_area_x").active_value;
      safe_y_ = rb_blitz::settings::InspectRuntimeSetting("present_safe_area_y").active_value;
      cutoff_ = rb_blitz::settings::InspectRuntimeSetting("present_allow_overscan_cutoff").active_value;
      // Existing presenter overscan arithmetic scales both axes uniformly to
      // the surface height, with a centred horizontal crop. It never changes
      // the camera, guest projection, or rendered source proportions.
      rb_blitz::settings::WriteRuntimeSetting("present_safe_area_x", "1", rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("present_safe_area_y", "100", rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("present_allow_overscan_cutoff", "true", rex::SettingLayer::kRuntime).accepted();
    } else if (!crop && cropped_) {
      rb_blitz::settings::WriteRuntimeSetting("present_safe_area_x", safe_x_, rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("present_safe_area_y", safe_y_, rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("present_allow_overscan_cutoff", cutoff_, rex::SettingLayer::kRuntime).accepted();
    }
    const bool entering_crop = crop && !cropped_;
    cropped_ = crop;
    render_cutoff_ = render_cutoff;
    if (entering_crop && preview_notification_) preview_notification_();
    REXLOG_INFO("portrait: {} landscape source, preserving both axes", crop ? "cropping" : "fitting");
    return true;
  }
  std::function<rex::gpu::IGraphicsPresentation*()> presentation_;
  std::function<void()> preview_notification_;
  bool cropped_ = false, render_cutoff_ = false;
  std::string safe_x_, safe_y_, cutoff_;
};
}
std::unique_ptr<rex::ui::IDialog> CreateGameplayPresentation(
    std::function<rex::gpu::IGraphicsPresentation*()> presentation,
    std::function<void()> preview_notification) {
  return std::make_unique<GameplayPresentation>(std::move(presentation),
                                               std::move(preview_notification));
}
}
