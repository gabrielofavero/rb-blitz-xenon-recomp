// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace rb_blitz::ui {

struct PortraitSplashLayout {
  float width;
  float title_logo_scale, title_logo_x, title_logo_z;
  float menu_logo_scale, menu_logo_x, menu_logo_z;
  float start_scale, start_z;
  float list_scale, list_z;
  float city_scale, city_z;
};

// The reference's first two screens, in the guest UI camera's 480-unit height.
// Bounds measured from the installed animated logo's thirteen mesh records.
inline PortraitSplashLayout PortraitSplash(float aspect) {
  if (!std::isfinite(aspect) || aspect <= 0) aspect = 9.f / 16.f;
  const float width = 480 * std::clamp(aspect, .3f, .9f);
  // Center the symmetric wordmark, rather than its asymmetric decorations.
  constexpr float logo_width = 499.4504f, logo_z = 15.408f;
  const float title_scale = std::min(width * .94f / logo_width, .68f);
  const float menu_scale = std::min(width * .755f / logo_width, .45f);
  const float list_scale = std::min(width * .76f / 364.5f, .6f);
  // Like the reference's cqw units, top spacing follows visible width.
  const float flow_width = std::min(width, 270.f);
  const float list_z = 240 - flow_width * .7825f;
  const float city_scale = .205f;
  return {width, title_scale, 0, 240 - flow_width * .625f - logo_z * title_scale,
          menu_scale, 0, 240 - flow_width * .42625f - logo_z * menu_scale,
          width * .65f / 256, 240 - flow_width * 1.438f,
          list_scale, list_z, city_scale, -240 + 131.94f * city_scale};
}

inline float PortraitPromptScale(float aspect, bool error_popup = false) {
  return std::min(PortraitSplash(aspect).width * .84f / (error_popup ? 640.f : 350.f), 1.f);
}

struct PortraitLoadingLayout {
  float width, logo_scale, logo_z;
  float text_width, button_width, button_x, button_scale;
};

inline PortraitLoadingLayout PortraitLoading(float aspect) {
  const float width = PortraitSplash(aspect).width;
  const float button_width = (width * .90f - 10) / 2;
  return {width, std::min(width * .48f / 499.4504f, .36f), 158,
          width * .84f, button_width, (button_width + 10) / 2,
          std::min(1.f, button_width / 90)};
}

struct PortraitSettingsLayout {
  float content_scale;
  float back_width;
  float back_top = -182;
  float back_bottom = -212;
};

inline PortraitSettingsLayout PortraitSettings(float aspect) {
  const float width = PortraitSplash(aspect).width;
  return {std::min(width * .92f / 300.f, 1.f), std::min(width * .92f, 300.f)};
}

inline bool PortraitSettingsScene(std::string_view scene) {
  return scene == "settings_menu.milo" || scene == "settings_menu_pause.milo";
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
         (screen == "server_connect_screen" && panel == "server_connect_panel") ||
         (screen == "loading_screen" && panel == "loading_panel") ||
         panel == "pause_exit_game_panel" || panel == "error_popup_panel" ||
         panel == "rbbz_download_panel" || panel == "rbbz_search_loading_panel" ||
         panel == "rbbz_library_panel" ||
         PortraitSettingsScene(scene) ||
         PortraitPromptScene(scene);
}

}  // namespace rb_blitz::ui
