// SPDX-License-Identifier: GPL-2.0-only
#include "ui/gameplay_presentation.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/presenter.h>
#include "hooks/render_settings.h"
#include "input/native_menu.h"

REXCVAR_DEFINE_BOOL(portrait_render_cutoff, true, "Diagnostics",
                    "Skip hidden main-frame pixels during portrait gameplay; disable for comparison")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::ui {
namespace {
class GameplayPresentation final : public rex::ui::ImGuiDialog {
 public:
  GameplayPresentation(rex::ui::ImGuiDrawer* drawer,
                       std::function<rex::ui::Presenter*()> presenter,
                       std::function<void()> preview_notification)
      : ImGuiDialog(drawer), presenter_(std::move(presenter)),
        preview_notification_(std::move(preview_notification)) {}
 private:
  void OnDraw(ImGuiIO&) override {
    const bool game = input::GameScreenActive();
    const bool crop = settings::BootPortraitWidth() && (game || input::PortraitSplashActive());
    const bool render_cutoff = crop && game && REXCVAR_GET(portrait_render_cutoff);
    if (crop == cropped_ && render_cutoff == render_cutoff_) return;
    auto* presenter = presenter_();
    if (!presenter) return;
    auto config = presenter->GetGuestOutputPaintConfigFromUIThread();
    rex::cvar::SetFlagByName("draw_visible_region", "false");
    if (render_cutoff) {
      const auto height = settings::BootRenderHeight();
      const auto width = settings::BootRenderWidth();
      rex::cvar::SetFlagByName("draw_visible_source_width",
                             std::to_string(width ? width : height * 16 / 9));
      rex::cvar::SetFlagByName("draw_visible_source_height", std::to_string(height));
      rex::cvar::SetFlagByName("draw_visible_width", std::to_string(settings::BootPortraitWidth()));
      // Measured main-frame EDRAM pair. Dimensions and viewport/tile checks in
      // the backend exclude the shadow, bloom, downsample and helper passes.
      rex::cvar::SetFlagByName("draw_visible_color_info", "608");
      rex::cvar::SetFlagByName("draw_visible_depth_info", "66864"); // 0x10530
      rex::cvar::SetFlagByName("draw_visible_region", "true");
    }
    if (crop && !cropped_) {
      safe_x_ = rex::cvar::GetFlagByName("present_safe_area_x");
      safe_y_ = rex::cvar::GetFlagByName("present_safe_area_y");
      cutoff_ = rex::cvar::GetFlagByName("present_allow_overscan_cutoff");
      allow_cutoff_ = config.GetAllowOverscanCutoff();
      // Existing presenter overscan arithmetic scales both axes uniformly to
      // the surface height, with a centred horizontal crop. It never changes
      // the camera, guest projection, or rendered source proportions.
      rex::cvar::SetFlagByName("present_safe_area_x", "1");
      rex::cvar::SetFlagByName("present_safe_area_y", "100");
      rex::cvar::SetFlagByName("present_allow_overscan_cutoff", "true");
      config.SetAllowOverscanCutoff(true);
    } else if (!crop && cropped_) {
      rex::cvar::SetFlagByName("present_safe_area_x", safe_x_);
      rex::cvar::SetFlagByName("present_safe_area_y", safe_y_);
      rex::cvar::SetFlagByName("present_allow_overscan_cutoff", cutoff_);
      config.SetAllowOverscanCutoff(allow_cutoff_);
    }
    presenter->SetGuestOutputPaintConfigFromUIThread(config);
    const bool entering_crop = crop && !cropped_;
    cropped_ = crop;
    render_cutoff_ = render_cutoff;
    if (entering_crop && preview_notification_) preview_notification_();
    REXLOG_INFO("portrait: {} landscape source, preserving both axes", crop ? "cropping" : "fitting");
  }
  std::function<rex::ui::Presenter*()> presenter_;
  std::function<void()> preview_notification_;
  bool cropped_ = false, allow_cutoff_ = false, render_cutoff_ = false;
  std::string safe_x_, safe_y_, cutoff_;
};
}
std::unique_ptr<rex::ui::ImGuiDialog> CreateGameplayPresentation(
    rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Presenter*()> presenter,
    std::function<void()> preview_notification) {
  return std::make_unique<GameplayPresentation>(drawer, std::move(presenter),
                                               std::move(preview_notification));
}
}
