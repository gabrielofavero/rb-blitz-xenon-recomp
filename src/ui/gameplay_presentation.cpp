// SPDX-License-Identifier: GPL-2.0-only
#include "ui/gameplay_presentation.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/presenter.h>
#include "hooks/render_settings.h"
#include "input/native_menu.h"

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
    const bool crop = settings::BootPortraitWidth() && input::GameScreenActive();
    if (crop == cropped_) return;
    auto* presenter = presenter_();
    if (!presenter) return;
    auto config = presenter->GetGuestOutputPaintConfigFromUIThread();
    if (crop) {
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
    } else {
      rex::cvar::SetFlagByName("present_safe_area_x", safe_x_);
      rex::cvar::SetFlagByName("present_safe_area_y", safe_y_);
      rex::cvar::SetFlagByName("present_allow_overscan_cutoff", cutoff_);
      config.SetAllowOverscanCutoff(allow_cutoff_);
    }
    presenter->SetGuestOutputPaintConfigFromUIThread(config);
    cropped_ = crop;
    if (crop && preview_notification_) preview_notification_();
    REXLOG_INFO("portrait: {} landscape source, preserving both axes", crop ? "cropping" : "fitting");
  }
  std::function<rex::ui::Presenter*()> presenter_;
  std::function<void()> preview_notification_;
  bool cropped_ = false, allow_cutoff_ = false;
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
