// SPDX-License-Identifier: GPL-2.0-only
#include "settings/runtime_values.h"
#include "ui/gameplay_presentation.h"
#include "ui/portrait_viewport.h"
#include "settings/runtime_settings.h"
#include <rex/logging/log_category.h>
#include <rex/ui/ui_system.h>
#include <rex/gpu/graphics_presentation.h>
#include "hooks/render_settings.h"
#include "input/native_menu.h"
#include "platform/window_host.h"
#if defined(RB_BLITZ_IOS)
#include "platform/ios_runtime.h"
#endif
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <optional>



namespace rb_blitz::ui {
namespace {
class GameplayPresentation final : public rex::ui::DialogBase {
 public:
  GameplayPresentation(rex::ui::IUISystem& context,
                       std::function<rex::gpu::IGraphicsPresentation*()> presentation,
                       std::function<void()> preview_notification)
      : context_(context), presentation_(std::move(presentation)),
        preview_notification_(std::move(preview_notification)) {}
 private:
  bool Draw(ImGuiIO&) override {
    const auto native = context_.GetNativeWindow();
    auto* window = native ? static_cast<SDL_Window*>(native->sdl) : nullptr;
    int width = 0, height = 0;
    SDL_Rect safe{};
    if (window) {
      SDL_GetWindowSize(window, &width, &height);
      SDL_GetWindowSafeArea(window, &safe);
    }
    PublishPortraitViewport({float(width), float(height), float(safe.x), float(safe.y),
                             float(safe.w), float(safe.h)});
    const bool game = input::GameScreenActive();
    const bool portrait = game ? settings::GameplayPortraitEnabled() : settings::InterfacePortraitEnabled();
    if (!orientation_ || *orientation_ != portrait) {
      if (ApplyOrientation(portrait)) orientation_ = portrait;
    }
    const bool crop = portrait && (game ||
        (settings::InterfacePortraitEnabled() && input::PortraitSplashActive()));
    const bool render_cutoff = crop && game && rb_blitz::settings::title::portrait_render_cutoff.Get();
    const auto source_width = settings::BootRenderWidth();
    const auto source_height = settings::BootRenderHeight();
    const auto visible_width = portrait ? (game
        ? settings::GameplayPortraitWidth() : settings::BootPortraitWidth()) : 0;
    if (crop == cropped_ && render_cutoff == render_cutoff_ && source_width == source_width_ &&
        source_height == source_height_ && visible_width == visible_width_) return true;
    if (!presentation_()) return true;
    rb_blitz::settings::WriteRuntimeSetting("draw_visible_region", "false", rex::SettingLayer::kRuntime).accepted();
    if (render_cutoff) {
      const auto height = settings::BootRenderHeight();
      const auto width = settings::BootRenderWidth();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_source_width",
                             std::to_string(width ? width : height * 16 / 9), rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_source_height", std::to_string(height), rex::SettingLayer::kRuntime).accepted();
      rb_blitz::settings::WriteRuntimeSetting("draw_visible_width", std::to_string(visible_width), rex::SettingLayer::kRuntime).accepted();
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
    source_width_ = source_width;
    source_height_ = source_height;
    visible_width_ = visible_width;
    if (entering_crop && preview_notification_) preview_notification_();
    REXLOG_INFO("portrait: {} landscape source, preserving both axes", crop ? "cropping" : "fitting");
    return true;
  }
  bool ApplyOrientation(bool portrait) {
#if defined(RB_BLITZ_IOS) || defined(__ANDROID__)
    if (!SDL_SetHint(SDL_HINT_ORIENTATIONS,
                     portrait ? "Portrait" : "LandscapeLeft LandscapeRight")) return false;
    const auto native = context_.GetNativeWindow();
    if (!native || !native->sdl) return false;
    auto* window = static_cast<SDL_Window*>(native->sdl);
#if defined(RB_BLITZ_IOS)
    return platform::ApplyIOSOrientation(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr), portrait);
#else
    // SDL's Android driver refreshes the Activity orientation when this flag changes.
    const bool resizable = (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) != 0;
    if (!SDL_SetWindowResizable(window, !resizable)) return false;
    return SDL_SetWindowResizable(window, resizable);
#endif
#else
    if (platform::window_host::Active()) return platform::window_host::SetOrientation(portrait);
    const auto native = context_.GetNativeWindow();
    if (!native || !native->sdl) return false;
    auto* window = static_cast<SDL_Window*>(native->sdl);
    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
      return true;
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) return false;
    if ((width < height) == portrait) return true;
    window_sizes_[width < height] = {width, height};
    auto target = window_sizes_[portrait];
    if (!target.x) target = {height, width};
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable)) {
      int top = 0, left = 0, bottom = 0, right = 0;
      SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
      const double fit = std::min({1., double(std::max(1, usable.w - left - right)) / target.x,
                                      double(std::max(1, usable.h - top - bottom)) / target.y});
      target = {std::max(1, int(target.x * fit)), std::max(1, int(target.y * fit))};
    }
    return SDL_SetWindowSize(window, target.x, target.y);
#endif
  }
  rex::ui::IUISystem& context_;
  std::optional<bool> orientation_;
  std::array<SDL_Point, 2> window_sizes_{};
  std::function<rex::gpu::IGraphicsPresentation*()> presentation_;
  std::function<void()> preview_notification_;
  bool cropped_ = false, render_cutoff_ = false;
  uint32_t source_width_ = 0, source_height_ = 0, visible_width_ = 0;
  std::string safe_x_, safe_y_, cutoff_;
};
}
std::unique_ptr<rex::ui::IDialog> CreateGameplayPresentation(
    rex::ui::IUISystem& context,
    std::function<rex::gpu::IGraphicsPresentation*()> presentation,
    std::function<void()> preview_notification) {
  return std::make_unique<GameplayPresentation>(context, std::move(presentation),
                                               std::move(preview_notification));
}
}
