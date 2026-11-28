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
inline constexpr float kPortraitCalibrationInstructionsZ = 116;
inline constexpr float kPortraitCalibrationInstructionsHeight = 58;
inline constexpr float kPortraitCalibrationSpeakerZ = -16;
inline constexpr float kPortraitCalibrationPromptZ = 49;
inline constexpr float kPortraitCalibrationMeterZ = -34;
inline constexpr float kPortraitCalibrationProgressZ = -96;
inline constexpr float kPortraitCalibrationHitCountZ = -121;
inline constexpr float kPortraitCalibrationStatusZ = -144;
inline constexpr float kPortraitCalibrationOffsetZ = -167;
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

inline constexpr float kPortraitMenuButtonWidth = 364.5f;

// Title/loading and menu poses in the guest UI camera's 480-unit height.
// Bounds measured from the installed animated logo's thirteen mesh records.
inline PortraitSplashLayout PortraitSplash(float aspect) {
  if (!std::isfinite(aspect) || aspect <= 0) aspect = 9.f / 16.f;
  const float width = 480 * std::clamp(aspect, .3f, .9f);
  // Center the symmetric wordmark, rather than its asymmetric decorations.
  constexpr float logo_width = 499.4504f, logo_z = 15.408f;
  const float list_scale = std::min(width * .76f / kPortraitMenuButtonWidth, .55f);
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

struct PortraitPauseLayout {
  static constexpr int rows = 8;  // Includes the trial upgrade action.
  float width, height, title_size, title_z, list_scale, list_z;
};

inline PortraitPauseLayout PortraitPause(float aspect) {
  const float width = std::min(PortraitSplash(aspect).width * .90f, 246.f);
  const float list_scale = (width - 24) / 367.f;
  const float rows_height = ((PortraitPauseLayout::rows - 1) * kPortraitMenuRowStep + 55.25f) *
                            list_scale;
  const float height = rows_height + 74;
  return {width, height, std::min(22.f, (width - 24) / 7.f), height / 2 - 27,
          list_scale, height / 2 - 59 - 27.625f * list_scale};
}
inline constexpr float kPortraitLibraryContentTop = 148;
inline constexpr float kPortraitLibraryGap = 5;
inline constexpr float kPortraitLibraryInset = 10;
inline constexpr float kPortraitLibraryButtonHeight = 28;
inline constexpr float kPortraitLibraryFooterZ = -203;
inline constexpr float kPortraitLibraryFooterTop = kPortraitLibraryFooterZ + kPortraitLibraryButtonHeight / 2;

inline float PortraitLibraryWidth(float aspect) {
  return PortraitSplash(aspect).width * .92f;
}

inline constexpr float kPortraitSongRowHeight = 55;
inline constexpr float kPortraitSongRowStep = 60;
inline constexpr float kPortraitSongArrowScale = .9f;
inline constexpr float kPortraitSongArrowInset = 22;

struct PortraitSongsLayout {
  float width, list_scale;
  static constexpr float tabs_z = kPortraitHubTitleCenter;
  static constexpr float card_height = 52;
  static constexpr float card_z = kPortraitLibraryFooterTop + 3 + card_height / 2;
  static constexpr float list_top = kPortraitLibraryContentTop - kPortraitLibraryButtonHeight - 15;
  static constexpr float list_bottom = card_z + card_height / 2 + kPortraitLibraryGap;

  float FirstRowZ(float top) const { return top - kPortraitSongRowHeight / 2 * list_scale; }
  int Rows(float top, float bottom, float step = kPortraitSongRowStep) const {
    return 1 + int((top - bottom - kPortraitSongRowHeight * list_scale) / (step * list_scale));
  }
};

inline PortraitSongsLayout PortraitSongs(float aspect) {
  const float width = PortraitLibraryWidth(aspect);
  return {width, width / 420.f};
}

struct PortraitStoreOfferLayout {
  float width, art_size;
  static constexpr float top = 154, art_top = 94, inset = 12;
  float ArtZ() const { return art_top - art_size / 2; }
  float CreditsTop() const { return art_top - art_size - 25; }
  float ExtrasTop() const { return CreditsTop() - 68; }
  float Bottom() const { return ExtrasTop() - 30; }
  float Left() const { return -width / 2 + inset; }
  float TextWidth() const { return width - 2 * inset; }
};

inline PortraitStoreOfferLayout PortraitStoreOffer(float aspect) {
  const float width = PortraitLibraryWidth(aspect);
  return {width, std::min(width * .74f, 142.f)};
}

struct PortraitStoreOfferLabel {
  std::string_view name;
  float top, size, height;
};

inline std::array<PortraitStoreOfferLabel, 6> PortraitStoreOfferLabels(
    const PortraitStoreOfferLayout& offer) {
  const float top = offer.CreditsTop();
  return {{{"song.lbl", 143, 23, 32}, {"drm.lbl", 108, 15, 19},
           {"rating.lbl", top, 13, 16}, {"rating_description.lbl", top - 18, 12, 16},
           {"cost.lbl", top - 36, 12, 16}, {"cost_ps3.lbl", top - 54, 12, 16}}};
}

struct PortraitStoreDialogLayout {
  float width;
  float height = 136, text_top = 42, text_height = 64;
  float action_z = -40, action_height = 32;
  float TextWidth() const { return width - 28; }
  float ActionWidth() const { return (TextWidth() - 6) / 2; }
  float ActionX() const { return (ActionWidth() + 6) / 2; }
  float SingleWidth() const { return TextWidth(); }
};

inline PortraitStoreDialogLayout PortraitStoreDialog(float aspect) {
  return {std::min(PortraitLibraryWidth(aspect), 260.f)};
}

struct PortraitMessagesLayout {
  float width;
  static constexpr float tabs_z = kPortraitHubTitleCenter, row_height = 42;
  static constexpr float list_z = kPortraitLibraryContentTop - row_height / 2;
  static constexpr float row_step = row_height + kPortraitLibraryGap;
  float icon_size = 28, icon_inset = 22, text_inset = 44;
  static constexpr float detail_top = kPortraitLibraryContentTop - row_step;
  static constexpr float detail_bottom = kPortraitLibraryFooterTop + 2 * kPortraitLibraryGap;
  static constexpr float subject_top = detail_top - kPortraitLibraryInset, subject_height = 32;
  static constexpr float body_top = subject_top - subject_height - 6;
  static constexpr float body_height = body_top - detail_bottom - kPortraitLibraryInset;
  int rows = 1;
};

inline PortraitMessagesLayout PortraitMessages(float aspect) {
  return {PortraitLibraryWidth(aspect)};
}

struct PortraitRecommendationsLayout {
  float width, art_size, navigation_size, carousel_z;
  static constexpr float tabs_z = kPortraitHubTitleCenter;
  static constexpr float inset = kPortraitLibraryInset, hero_top = kPortraitLibraryContentTop;
  static constexpr float type_height = 12, title_height = 18, artist_height = 16;
  static constexpr float info_height = 14, reason_height = 43;
  static constexpr float position_top = hero_top - inset, position_height = 12;
  float leaderboard_title_top = -125, leaderboard_z = -148, leaderboard_step = 17;
  float TypeTop() const { return carousel_z - art_size / 2 - 7; }
  float HeroBottom() const { return TypeTop() - type_height - 6; }
  float DetailsTop() const { return HeroBottom() - kPortraitLibraryGap; }
  float TitleTop() const { return DetailsTop() - inset; }
  float ArtistTop() const { return TitleTop() - title_height - 3; }
  float InfoTop() const { return ArtistTop() - artist_height - 2; }
  float ReasonTop() const { return InfoTop() - info_height - 5; }
  float DetailsBottom() const { return ReasonTop() - reason_height - inset; }
  float Left() const { return -width / 2 + inset; }
  float TextWidth() const { return width - 2 * inset; }
  float PositionWidth() const { return std::min(44.f, -Left() - art_size / 2 - 4); }
  float NavigationX() const { return (width - navigation_size) / 2; }
};

inline PortraitRecommendationsLayout PortraitRecommendations(float aspect) {
  const float width = PortraitLibraryWidth(aspect);
  const float art_size = std::min(width * .48f, 112.f);
  return {width, art_size, std::min(width * .21f, 36.f), kPortraitLibraryContentTop - 8 - art_size / 2};
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

struct PortraitSongLoadingLayout {
  float scene_scale;
  float tip_width, tip_size;
  float powerup_step, powerup_radius, powerup_name_width, powerup_name_size;
  float status_width, status_size;
  static constexpr float tip_z = 100, tip_height = 188;
  static constexpr float powerup_z = -36, powerup_name_height = 54;
  static constexpr float spinner_z = -138, status_z = -163, status_height = 60;
};

inline PortraitSongLoadingLayout PortraitSongLoading(float aspect) {
  const float width = PortraitSplash(aspect).width;
  const float scene_scale = width / 640;
  return {scene_scale, width * .93f, std::min(width * .07f, 17.5f),
          width * .31f, std::min(72 * scene_scale, 29.f), width * .30f,
          std::min(width * .051f, 13.5f), width * .86f, std::min(width * .096f, 23.f)};
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

inline PortraitCalibrationAction PortraitHubArrowTarget(const PortraitSongsLayout& layout, bool right) {
  // Leave a gap above the song tools while making near-edge taps easier to hit.
  return {(right ? 1.f : -1.f) * (layout.width / 2 - kPortraitSongArrowInset),
          layout.tabs_z, 68, 56};
}

inline PortraitCalibrationAction PortraitLibraryButton(float width, size_t index, size_t count, float z) {
  const float cell = (width - kPortraitLibraryGap * (count - 1)) / count;
  return {(float(index) - float(count - 1) / 2) * (cell + kPortraitLibraryGap),
          z, cell, kPortraitLibraryButtonHeight};
}

inline PortraitCalibrationAction PortraitSongTool(float aspect, bool search) {
  return PortraitLibraryButton(PortraitLibraryWidth(aspect), search ? 1 : 0, 2,
                               kPortraitLibraryContentTop - kPortraitLibraryButtonHeight / 2);
}

inline PortraitCalibrationAction PortraitLibraryFooter(float aspect, size_t index, size_t count) {
  return PortraitLibraryButton(PortraitLibraryWidth(aspect), index, count, kPortraitLibraryFooterZ);
}

inline const char* PortraitRecommendationFooterSurface(size_t index) {
  constexpr std::array<const char*, 2> names{"portrait_rec_action_0", "portrait_rec_action_1"};
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
         scene == "../options_menu/settings_tutorials.milo" ||
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

inline bool PortraitLibraryScreen(std::string_view screen) {
  return std::find(kPortraitLibraryScreens.begin(), kPortraitLibraryScreens.end(), screen) !=
      kPortraitLibraryScreens.end();
}

inline bool PortraitLibraryTransition(std::string_view screen) {
  if (!screen.starts_with("trans_")) return false;
  screen.remove_prefix(6);
  for (const auto source : kPortraitLibraryScreens) {
    if (!screen.starts_with(source) || screen.size() <= source.size() || screen[source.size()] != '_') continue;
    const auto destination = screen.substr(source.size() + 1);
    return PortraitLibraryScreen(destination);
  }
  return false;
}

inline bool PortraitLibraryBackgroundTitle(std::string_view name) {
  return name == "main_Home.lbl" || name == "main_my_songs.lbl" || name == "main_store.lbl" ||
      name == "main_inbox.lbl" || name == "main_new_msg.lbl" || name == "offline_Mysongs.lbl";
}

inline bool PortraitResultsBreakdown(std::string_view screen) {
  return screen == "results_breakdown_screen";
}

struct PortraitResultsLayout {
  float width;
  static constexpr float top = 226, bottom = -219;
  static constexpr float title_z = 180, score_z = 119, stars_z = 71;
  static constexpr float rail_top = 43, rail_bottom = -172;
  static constexpr float powerup_icon_z = -94, powerup_name_z = -132, powerup_score_z = -153;
  static constexpr float footer_z = -199, footer_height = 36, footer_gap = 6;

  float PowerupX(size_t index) const { return (float(index) - 1) * width / 3; }
  float PowerupScale() const { return std::min(.72f, (width / 3 - 8) / 75); }
  float StarsScale() const { return std::min(.88f, (width - 8) / 280); }
  float RowZ(size_t index) const { return 24 - 26 * float(index); }
  PortraitCalibrationAction Footer(size_t index, size_t count) const {
    const float cell = (width - footer_gap * float(count - 1)) / float(count);
    return {(float(index) - float(count - 1) / 2) * (cell + footer_gap),
            footer_z, cell, footer_height};
  }
};

inline PortraitResultsLayout PortraitResults(float aspect) {
  return {std::min(PortraitLibraryWidth(aspect), 320.f)};
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
  return PortraitStoreScreen(screen) || PortraitLibraryTransition(screen) || PortraitResultsBreakdown(screen) ||
         (screen == "splash_screen" && panel == "splash_panel") ||
         (screen == "autosave_warning_screen" && panel == "autosave_warning_panel") ||
         (screen == "server_connect_screen" && panel == "server_connect_panel") ||
         (screen == "loading_screen" && panel == "loading_panel") ||
         panel == "pause_panel" ||
         panel == "pause_exit_game_panel" || panel == "error_popup_panel" ||
         panel == "rbbz_download_panel" || panel == "rbbz_search_loading_panel" ||
         panel == "rbbz_library_panel" ||
         panel == "av_options_panel" || panel == "credits_panel" || panel == "tutorials_panel" ||
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
