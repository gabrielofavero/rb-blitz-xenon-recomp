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
// Hub marquee rails settle at 204.37975 and 169.79509 in the background panel.
inline constexpr float kPortraitHubTitleCenter = (204.37975f + 169.79509f) / 2;
inline constexpr float kPortraitCalibrationInstructionsZ = 78;
inline constexpr float kPortraitCalibrationInstructionsHeight = 58;
inline constexpr float kPortraitCalibrationMeterZ = -16;
inline constexpr std::array<const char*, 6> kPortraitCalibrationGraphics{
    "cal_metronome.grp", "cal_bg.grp", "visuals_anim.grp", "audio_anim.grp",
    "prog_bar.grp", "cal_reps.lbl"};
// Conservative bounds across the native logo meshes and their animation keys.
inline constexpr float kPortraitLogoTop = 206.f;
inline constexpr float kPortraitLogoBottom = -176.f;
inline constexpr float kPortraitStatusTextBottomZ = -150.f;

inline std::array<float, 4> FooterButtonColor(bool border, bool hovered = false) {
  auto color = border ? kMenuButtonBorder : hovered ? kMenuButtonHover : kMenuButtonNormal;
  color[3] *= kFooterOpacity;
  return color;
}

inline std::array<float, 4> LoadingButtonColor(bool border, bool hovered = false) {
  // A light plum tint lets the warm backdrop show through without a yellow highlight.
  return border ? std::array<float, 4>{.90f, .85f, .95f, .09f} :
      std::array<float, 4>{.72f, .66f, .79f, hovered ? .28f : .18f};
}

struct PortraitSplashLayout {
  float width;
  float logo_scale, logo_x, logo_z;
  float title_logo_z;
  float start_scale, start_z;
  float list_scale, menu_list_scale, list_z;
  float city_scale, city_z;
};

// Title/loading and menu poses in the guest UI camera's 480-unit height.
// Bounds measured from the installed animated logo's thirteen mesh records.
inline PortraitSplashLayout PortraitSplash(float aspect) {
  if (!std::isfinite(aspect) || aspect <= 0) aspect = 9.f / 16.f;
  const float width = 480 * std::clamp(aspect, .3f, .9f);
  // Center the symmetric wordmark, rather than its asymmetric decorations.
  constexpr float logo_width = 499.4504f, logo_z = 15.408f;
  const float list_scale = std::min(width * .76f / 364.5f, .55f);
  // Like the reference's cqw units, top spacing follows visible width.
  const float flow_width = std::min(width, 270.f);
  // Preserve the title's existing size and pose across all splash states.
  const float list_top = 240 - flow_width * .8f + 26.375f * list_scale;
  const float logo_scale = std::min(.95f * std::min(width * .94f / logo_width, .68f),
      (236 - list_top - 8) / (kPortraitLogoTop - kPortraitLogoBottom));
  const float title_logo_z = 240 - flow_width * .625f - logo_z * logo_scale;
  const float menu_top = title_logo_z + kPortraitLogoBottom * logo_scale - 8;
  // Fit all six rows below the fixed logo, above the city and footer region.
  const float menu_list_scale = std::min(list_scale, (menu_top + 164) / (5 * 62 + 52.75f));
  const float list_z = menu_top - 26.375f * menu_list_scale;
  const float city_scale = .205f;
  return {width, logo_scale, 0, title_logo_z, title_logo_z,
          // Center the Start prompt at the two-line discovery message's height.
          .95f * width * .65f / 256, kPortraitStatusTextBottomZ + 10,
          // Crop the solid strip beneath the road and overlap the bottom edge.
          list_scale, menu_list_scale, list_z, city_scale, -248 + 131.94f * city_scale};
}

inline constexpr float kPortraitMenuRowStep = 62.f;
inline constexpr float kPortraitSongRowHeight = 55;
inline constexpr float kPortraitSongRowStep = 60;
inline constexpr float kPortraitSongArrowScale = .9f;
inline constexpr float kPortraitSongArrowInset = 22;
inline constexpr float kPortraitSongArrowTargetSize = 40;

struct PortraitSongsLayout {
  float width, list_scale;
  float tabs_z = kPortraitHubTitleCenter, tools_z = 133, list_z = 92;
  float card_z = -158, card_height = 52, footer_z = -201;
  int rows = 7;
};

inline PortraitSongsLayout PortraitSongs(float aspect) {
  const float width = PortraitSplash(aspect).width * .92f;
  PortraitSongsLayout layout{width, width / 420.f};
  layout.rows = std::min(7, 1 + int((layout.list_z - (layout.card_z + layout.card_height / 2 + 14) -
                                   kPortraitSongRowHeight / 2 * layout.list_scale) /
                                   (kPortraitSongRowStep * layout.list_scale)));
  return layout;
}

struct PortraitMessagesLayout {
  float width;
  float tabs_z = kPortraitHubTitleCenter, list_z = 130, row_step = 48, row_height = 42;
  float icon_size = 28, icon_inset = 22, text_inset = 44;
  float detail_top = -10, detail_bottom = -179;
  float subject_top = -19, subject_height = 32;
  float body_top = -57, body_height = 112;
  int rows = 3;
};

inline PortraitMessagesLayout PortraitMessages(float aspect) {
  return {PortraitSongs(aspect).width};
}

struct PortraitRecommendationsLayout {
  float width, art_size, navigation_size;
  float tabs_z = kPortraitHubTitleCenter, carousel_z = 104;
  float title_top = 20, title_height = 30;
  float artist_top = -16, artist_height = 18, info_top = -38, info_height = 14;
  float reason_top = -59, reason_height = 43;
  float leaderboard_title_top = -114, leaderboard_z = -137, leaderboard_step = 17;
  float TypeZ() const { return carousel_z - art_size / 2 - 14; }
  float NavigationX() const { return (width - navigation_size) / 2; }
};

inline PortraitRecommendationsLayout PortraitRecommendations(float aspect) {
  const float width = PortraitSongs(aspect).width;
  return {width, std::min(width * .48f, 112.f), std::min(width * .21f, 36.f)};
}

struct PortraitPowerupsLayout {
  float width, category_scale, choices_scale;
  float title_z = 188;
  float card_z = 128, card_height = 56;
  float categories_z = 44, choices_z = -45;
  float currency_z = -150, currency_height = 28;
  float name_z = 100, description_z = 35;
  float details_z = 50, details_height = 180, description_height = 100;
  int choices = 3;
  float CategoryCaptionZ() const { return categories_z - 45 * category_scale - 14; }
  float InfoZ() const { return choices_z - 30 * choices_scale - 18; }
};

inline PortraitPowerupsLayout PortraitPowerups(float aspect) {
  const float width = std::min(PortraitSplash(aspect).width * .92f, 270.f);
  // Three 50-unit icons at 64-unit spacing, plus both 32-unit page arrows.
  return {width, width / 360, width / 260};
}

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
  // The native 256 x 32 frame and animated fill share an 8:1 aspect.
  const float bar_width = width * .5f;
  return {width, splash.logo_scale, splash.title_logo_z,
          width * .84f, 18 * splash.list_scale / .55f, button_width, (button_width + 10) / 2,
          1, bar_width, bar_width / 8, 3,
          kPortraitStatusTextBottomZ, -167.5f};
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

inline PortraitCalibrationAction PortraitSongsFooter(float aspect, size_t index, size_t count) {
  const auto layout = PortraitSongs(aspect);
  const float width = (layout.width - 5 * (count - 1)) / count;
  return {(float(index) - float(count - 1) / 2) * (width + 5),
          layout.footer_z, width, 27};
}

inline PortraitCalibrationAction PortraitRecommendationsFooter(float aspect, size_t index, size_t count) {
  const float width = PortraitRecommendations(aspect).width;
  const bool primary = index < 2;
  const size_t columns = primary ? std::min(count, size_t{2}) : count - 2;
  const size_t column = primary ? index : index - 2;
  const float cell = (width - 5 * (columns - 1)) / columns;
  return {(float(column) - float(columns - 1) / 2) * (cell + 5),
          primary ? -211.f : -179.f, cell, primary ? 29.f : 25.f};
}

inline const char* PortraitRecommendationFooterSurface(size_t index) {
  constexpr std::array<const char*, 5> names{"portrait_rec_action_0", "portrait_rec_action_1",
      "portrait_rec_action_2", "portrait_rec_action_3", "portrait_rec_action_4"};
  return index < names.size() ? names[index] : "";
}

// Keep the native calibration footer's changing actions in separate touch areas.
inline PortraitCalibrationAction PortraitCalibrationFooter(float aspect, size_t index) {
  const float width = (364.5f * PortraitSplash(aspect).list_scale - 10) / 2;
  return {index < 2 ? (index ? 1.f : -1.f) * (width + 10) / 2 : 0.f,
          index < 2 ? -197.f : -159.f, width, 30};
}

inline PortraitCalibrationAction PortraitFooterAction(float aspect, size_t index, size_t count) {
  // Back and Next share the lower row; Retry occupies its own row above them.
  if (count > 2) return PortraitCalibrationFooter(aspect, index);
  const float row = 364.5f * PortraitSplash(aspect).list_scale;
  const float width = count == 1 ? row * .55f : (row - 10) / 2;
  return {count == 1 ? 0.f : (index ? 1.f : -1.f) * (width + 10) / 2,
          -197, width, 30};
}

inline const char* PortraitFooterSurface(size_t index, size_t count) {
  if (count > 2) return index == 0 ? "portrait_confirm" : index == 1 ? "portrait_back" : "portrait_retry";
  return index == 0 ? "portrait_confirm" : "portrait_back";
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

inline bool PortraitStoreScreen(std::string_view screen) {
  return screen == "store_menu_screen" || screen == "store_browser_screen" || screen == "store_info_screen";
}

inline constexpr std::array<std::string_view, 5> kPortraitLibraryScreens{
    "song_select_screen", "store_menu_screen", "store_browser_screen",
    "inbox_screen", "recommendation_screen"};

inline bool PortraitLibraryTransition(std::string_view screen) {
  if (!screen.starts_with("trans_")) return false;
  screen.remove_prefix(6);
  for (const auto source : kPortraitLibraryScreens) {
    if (!screen.starts_with(source) || screen.size() <= source.size() || screen[source.size()] != '_') continue;
    const auto destination = screen.substr(source.size() + 1);
    return std::find(kPortraitLibraryScreens.begin(), kPortraitLibraryScreens.end(), destination) !=
        kPortraitLibraryScreens.end();
  }
  return false;
}

inline std::string_view PortraitLibraryPanelScreen(std::string_view panel, bool store_browser = false) {
  if (panel == "songselect") return store_browser ? "store_browser_screen" : "song_select_screen";
  if (panel == "StoreMainPanel" || panel == "StoreMenuPanel") return "store_menu_screen";
  if (panel == "storedetails") return "store_browser_screen";
  if (panel == "InboxPanel") return "inbox_screen";
  if (panel == "RecommendationsPanel") return "recommendation_screen";
  return {};
}

// Panels with authored portrait content fill the surface, including modal
// prompts. Other menus keep their existing fitted presentation.
inline bool PortraitSplashScreen(std::string_view screen, std::string_view panel,
                                std::string_view scene = {}) {
  return PortraitStoreScreen(screen) || PortraitLibraryTransition(screen) ||
         (screen == "splash_screen" && panel == "splash_panel") ||
         (screen == "autosave_warning_screen" && panel == "autosave_warning_panel") ||
         (screen == "server_connect_screen" && panel == "server_connect_panel") ||
         (screen == "loading_screen" && panel == "loading_panel") ||
         panel == "pause_exit_game_panel" || panel == "error_popup_panel" ||
         panel == "rbbz_download_panel" || panel == "rbbz_search_loading_panel" ||
         panel == "rbbz_library_panel" ||
         panel == "av_options_panel" || panel == "credits_panel" ||
         (screen == "song_select_screen" && panel == "song_select_panel") ||
         (screen == "inbox_screen" && panel == "inbox_panel") ||
         (screen == "recommendation_screen" && panel == "recommendation_panel") ||
         (screen == "powerup_select_screen" && panel == "powerup_select_panel") ||
         PortraitSettingsScene(scene) ||
         PortraitControlsScene(scene) ||
         PortraitControlsPanel(panel) ||
         PortraitPromptScene(scene);
}

}  // namespace rb_blitz::ui
