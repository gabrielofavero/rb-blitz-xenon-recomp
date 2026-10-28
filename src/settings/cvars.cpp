// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The cvar data in src/settings/cvars.h. All of it is pure: given a target, a token or a
// row, return the cvar text the game should be given. The actual SetFlagFromConfig calls
// belong to the coordinator's boot wiring (§4.2), not here.
//
// The two allowlist helpers are deliberately *not* here but inline in the header: §8's narrowing is
// used by the game's own profile apply (src/profile/profile_apply.cpp), and the game's source list
// does not carry this file yet - a definition here would be an undefined symbol in a build nobody
// asked to change.

#include "settings/cvars.h"

namespace rb_blitz::settings {

using rb_blitz::profile::LaunchTarget;

std::vector<CvarAssignment> TargetToCvars(LaunchTarget target, const LicensePolicy& policy) {
  std::vector<CvarAssignment> assignments;
  switch (target) {
    case LaunchTarget::kUltimate:
      assignments.push_back({"ultimate_mode", "1"});
      assignments.push_back(
          {"license_mask", std::to_string(static_cast<unsigned long long>(policy.purchased_mask))});
      break;
    case LaunchTarget::kDemo:
      assignments.push_back({"ultimate_mode", "0"});
      assignments.push_back(
          {"license_mask", std::to_string(static_cast<unsigned long long>(policy.trial_mask))});
      break;
    case LaunchTarget::kCommon:
      assignments.push_back({"ultimate_mode", "0"});
      assignments.push_back(
          {"license_mask", std::to_string(static_cast<unsigned long long>(policy.purchased_mask))});
      break;
  }
  return assignments;
}

std::vector<CvarAssignment> AntiAliasingToCvars(std::string_view token) {
  std::vector<CvarAssignment> assignments;
  if (token == "none") {
    assignments.push_back({"native_2x_msaa", "false"});
    assignments.push_back({"swap_post_effect", "none"});
  } else if (token == "native") {
    assignments.push_back({"native_2x_msaa", "true"});
    assignments.push_back({"swap_post_effect", "none"});
  } else if (token == "fxaa") {
    assignments.push_back({"native_2x_msaa", "false"});
    assignments.push_back({"swap_post_effect", "fxaa"});
  } else if (token == "fxaa_extreme") {
    assignments.push_back({"native_2x_msaa", "false"});
    assignments.push_back({"swap_post_effect", "fxaa_extreme"});
  }
  return assignments;
}

bool FilteringToCvarValue(std::string_view token, int32_t* out) {
  if (token == "native") {
    *out = -1;
  } else if (token == "none") {
    *out = 0;
  } else if (token == "1x") {
    *out = 1;
  } else if (token == "2x") {
    *out = 2;
  } else if (token == "4x") {
    *out = 3;
  } else if (token == "8x") {
    *out = 4;
  } else if (token == "16x") {
    *out = 5;
  } else {
    return false;
  }
  return true;
}

std::vector<CvarAssignment> DisplayToCvars(std::string_view token) {
  if (token == "windowed") {
    return {{"fullscreen", "false"}, {"borderless", "false"}};
  }
  if (token == "borderless") {
    return {{"fullscreen", "false"}, {"borderless", "true"}};
  }
  if (token == "fullscreen") {
    return {{"fullscreen", "true"}, {"borderless", "false"}};
  }
  return {};
}

std::string_view CvarForSetting(SettingId id) {
  switch (id) {
    case SettingId::kRefreshMusicLibrary:
      // The main menu's own row for the refresh (owner request, 2026-10-15). Its consumer is
      // src/hooks/menu_filter.cpp, which builds the hide decision at boot and also feeds
      // src/hooks/settings_payload.cpp's locale, so the boot applies it at the config rank like
      // the other enhancement rows and an explicit flag or environment value outranks it.
      return "enhancements_refresh_music_library";
    case SettingId::kDlcMenuSongs:
      // R9's row, whose consumer reads the cvar (src/hooks/shell_music.cpp): the boot applies the
      // row here at the config rank, so an explicit flag or environment value outranks it exactly
      // like every other enhancement row.
      return "enhancements_menu_dlc_songs";
    case SettingId::kDlcScanThreads:
      return "dlc_scan_threads";
    case SettingId::kLoadGuard:
      return "song_load_guard";
    case SettingId::kStartup:
      return "startup_mode";
    case SettingId::kHideOfflinePrompts:
      return "enhancements_skip_offline_dialog";
    case SettingId::kHideMainMenu:
      return "enhancements_hide_menu_options";
    case SettingId::kHiddenMainMenu:
      return "enhancements_hidden_menu_options";
    case SettingId::kBackgroundMusicNavigation:
      return "enhancements_background_music_nav";
    case SettingId::kMusicPlayer:
      return "enhancements_music_player";
    case SettingId::kSettingsLabels:
      // The caption overlay's own switch. The definition lives in the game's own translation unit
      // (src/ui/help_overlay.cpp), not in the SDK, so the write is the ordinary config-rank one and
      // the same name is what the live applier writes (src/hooks/hot_apply_runtime.cpp).
      return "settings_help_overlay";
    case SettingId::kHideAchievements:
      return "enhancements_hide_achievements";
    case SettingId::kVsync:
      return "vsync";
    case SettingId::kRenderer:
      return "gpu_backend";
    default:
      return {};
  }
}

std::string_view ProviderCvarFor(SettingId id) {
  switch (id) {
    case SettingId::kUnlockAll:
      return "unlockall";
    case SettingId::kLetterbox:
      return "letterbox";
    case SettingId::kHideHud:
      return "hide_hud";
    case SettingId::kGoldProgress:
      return "goldprogress";
    case SettingId::kProgressBar:
      return "progressbar";
    case SettingId::kShowTimer:
      return "show_timer";
    case SettingId::kShowPb:
      return "show_pb";
    case SettingId::kShowSteak:
      return "show_steak";
    case SettingId::kInputViewer:
      return "input_viewer";
    case SettingId::kPrecisionMode:
      return "freq_ms";
    case SettingId::kDisableRandomWiggles:
      return "disable_random_wiggles";
    case SettingId::kCameraAngle:
      return "camera_angle";
    case SettingId::kGameSpeed:
      return "speedmod";
    case SettingId::kDisableDof:
      return "disable_dof";
    case SettingId::kDisablePlots:
      return "disable_plots";
    case SettingId::kForcePlot:
      return "forceplot";
    case SettingId::kForceSkybox:
      return "which_skybox";
    case SettingId::kGems:
      return "gems";
    default:
      return {};
  }
}

int SourceRank(Source source) {
  switch (source) {
    case Source::kCompiledDefault:
      return 0;
    case Source::kRecommendedFirstRun:
      return 1;
    case Source::kSettingsFile:
      return 2;
    case Source::kProvider:
      return 2;  // provider-owned rows are their own file: the same rank as settings.toml
    case Source::kDeveloperConfig:
      return 3;
    case Source::kEnvironment:
      return 4;
    case Source::kCommandLine:
      return 5;
  }
  return 0;
}

std::string OverrideExplanation(Source source, std::string_view setting_token) {
  const std::string_view token = SourceToken(source);
  std::string text = "settings.";
  text += setting_token;
  text += ".overridden_by_";
  text += token;
  return text;
}

}  // namespace rb_blitz::settings
