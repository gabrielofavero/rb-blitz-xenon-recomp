// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace rb_blitz::ui {

inline constexpr std::array<float, 4> kMenuButtonNormal{.063f, .051f, .11f, .67f};
inline constexpr std::array<float, 4> kMenuButtonBorder{.90f, .85f, .95f, .19f};
inline constexpr std::array<float, 4> kMenuButtonHover{1, .855f, .086f, 1};
inline constexpr float kFooterOpacity = .8f;
// Midpoint of the marquee's two rows of lights in the guest scene.
inline constexpr float kPortraitTitleCenter = 164.6512f;

inline std::array<float, 4> FooterButtonColor(bool border, bool hovered = false) {
  auto color = border ? kMenuButtonBorder : hovered ? kMenuButtonHover : kMenuButtonNormal;
  color[3] *= kFooterOpacity;
  return color;
}

struct PortraitSplashLayout {
  float width;
  float logo_scale, logo_x, logo_z;
  float title_logo_scale, title_logo_z;
  float start_scale, start_z;
  float list_scale, list_z;
  float city_scale, city_z;
};

// Title/loading and menu poses in the guest UI camera's 480-unit height.
// Bounds measured from the installed animated logo's thirteen mesh records.
inline PortraitSplashLayout PortraitSplash(float aspect) {
  if (!std::isfinite(aspect) || aspect <= 0) aspect = 9.f / 16.f;
  const float width = 480 * std::clamp(aspect, .3f, .9f);
  // Center the symmetric wordmark, rather than its asymmetric decorations.
  constexpr float logo_width = 499.4504f, logo_z = 15.408f;
  const float logo_scale = .85f * std::min(width * .755f / logo_width, .45f);
  const float title_scale = .95f * std::min(width * .94f / logo_width, .68f);
  const float list_scale = std::min(width * .76f / 364.5f, .55f);
  // Like the reference's cqw units, top spacing follows visible width.
  const float flow_width = std::min(width, 270.f);
  const float list_z = 240 - flow_width * .8f;
  const float city_scale = .205f;
  return {width, logo_scale, 0, 240 - flow_width * .42625f - logo_z * logo_scale,
          title_scale, 240 - flow_width * .625f - logo_z * title_scale,
          .95f * width * .65f / 256, 240 - flow_width * 1.438f,
          // Crop the solid strip beneath the road and overlap the bottom edge.
          list_scale, list_z, city_scale, -248 + 131.94f * city_scale};
}

inline constexpr float kPortraitMenuRowStep = 62.f;

inline float PortraitPromptScale(float aspect, bool error_popup = false) {
  return std::min(PortraitSplash(aspect).width * .84f / (error_popup ? 640.f : 350.f), 1.f);
}

struct PortraitLoadingLayout {
  float width, logo_scale, logo_z;
  float text_width, text_size, button_width, button_x, button_scale;
  float bar_width, bar_height, bar_gap, text_bottom_z, percent_z;
};

inline PortraitLoadingLayout PortraitLoading(float aspect) {
  const auto splash = PortraitSplash(aspect);
  const float width = splash.width;
  const float button_width = (364.5f * splash.list_scale - 10) / 2;
  return {width, splash.title_logo_scale, splash.title_logo_z,
          width * .84f, 18 * splash.list_scale / .55f, button_width, (button_width + 10) / 2,
          1, width * .5f, 20, 3,
          -150, -167.5f};
}

struct PortraitSettingsLayout {
  float content_scale;
  float back_width;
  float back_top = -182;
  float back_bottom = -212;
};

inline PortraitSettingsLayout PortraitSettings(float aspect) {
  const auto menu = PortraitSplash(aspect);
  return {menu.list_scale, 364.5f * menu.list_scale * .55f};
}

struct PortraitCalibrationAction {
  float x, z, width, height;
};

// Keep the native calibration footer's changing actions in separate touch areas.
inline PortraitCalibrationAction PortraitCalibrationFooter(float aspect, size_t index) {
  const float width = (364.5f * PortraitSplash(aspect).list_scale - 10) / 2;
  return {index < 2 ? (index ? 1.f : -1.f) * (width + 10) / 2 : 0.f,
          index < 2 ? -215.f : -177.f, width, 30};
}

inline PortraitCalibrationAction PortraitFooterAction(float aspect, size_t index, size_t count) {
  if (count > 2) return PortraitCalibrationFooter(aspect, index);
  const float row = 364.5f * PortraitSplash(aspect).list_scale;
  const float width = count == 1 ? row * .55f : (row - 10) / 2;
  return {count == 1 ? 0.f : (index ? 1.f : -1.f) * (width + 10) / 2,
          -197, width, 30};
}

inline bool PortraitControlsScene(std::string_view scene) {
  scene = scene.substr(scene.find_last_of('/') + 1);
  if (scene.ends_with(".milo")) scene.remove_suffix(5);
  return scene == "controller_config" ||
         scene == "cal_welcome" || scene == "cal_auto" || scene == "cal_manual" ||
         scene == "cal_quick" || scene == "cal_complete" || scene == "cal_failed";
}

inline bool PortraitSettingsScene(std::string_view scene) {
  return scene == "settings_menu.milo" || scene == "settings_menu_pause.milo" ||
         scene == "../av_options/settings_scale.milo" ||
         scene == "../av_options/settings_scale_pause.milo" ||
         scene == "av_options.milo" || scene == "credits.milo";
}

inline bool PortraitControlsPanel(std::string_view panel) {
  return (panel.ends_with("_panel") && PortraitControlsScene(panel.substr(0, panel.size() - 6))) ||
         panel == "cal_audio_panel" || panel == "cal_visuals_panel" ||
         panel == "cal_hw_audio_panel" || panel == "cal_hw_visuals_panel" ||
         panel == "cal_manual_audio_panel" || panel == "cal_manual_video_panel";
}

// Confirmation scenes are also used by settings under private file aliases.
// Recognize the authored scene so new prompts using that scene inherit portrait.
inline bool PortraitPromptScene(std::string_view scene) {
  return scene == "confirmation_message" || scene == "settings_confirmation" ||
         scene == "settings_confirmation_pause" || scene == "error_general" ||
         scene == "../pause/settings_confirmation.milo" ||
         scene == "../pause/settings_confirmation_pause.milo";
}

// Panels with authored portrait content fill the surface, including modal
// prompts. Other menus keep their existing fitted presentation.
inline bool PortraitSplashScreen(std::string_view screen, std::string_view panel,
                                std::string_view scene = {}) {
  return (screen == "splash_screen" && panel == "splash_panel") ||
         (screen == "autosave_warning_screen" && panel == "autosave_warning_panel") ||
         (screen == "server_connect_screen" && panel == "server_connect_panel") ||
         (screen == "loading_screen" && panel == "loading_panel") ||
         panel == "pause_exit_game_panel" || panel == "error_popup_panel" ||
         panel == "rbbz_download_panel" || panel == "rbbz_search_loading_panel" ||
         panel == "rbbz_library_panel" ||
         panel == "av_options_panel" || panel == "credits_panel" ||
         PortraitSettingsScene(scene) ||
         PortraitControlsScene(scene) ||
         PortraitControlsPanel(panel) ||
         PortraitPromptScene(scene);
}

}  // namespace rb_blitz::ui
