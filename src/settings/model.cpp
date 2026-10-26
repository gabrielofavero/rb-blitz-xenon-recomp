// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The small, SDK-free helpers declared in src/settings/model.h. The id table is the one
// place that maps a SettingId to its stable token; schema.cpp maps it to its storage key.
// Keeping the two apart is what lets the bridge carry a token without knowing the file
// layout.

#include "settings/model.h"

#include <cstdio>

namespace rb_blitz::settings {

std::string_view SettingIdToken(SettingId id) {
  switch (id) {
    case SettingId::kTarget:
      return "target";
    case SettingId::kCacheDlcLibrary:
      return "cache_dlc_library";
    case SettingId::kDlcScanThreads:
      return "dlc_scan_threads";
    case SettingId::kDlcMenuSongs:
      return "dlc_menu_songs";
    case SettingId::kPlayCurrentSong:
      return "play_current_song";
    case SettingId::kRefreshMusicLibrary:
      return "refresh_music_library";
    case SettingId::kUnlockAll:
      return "unlock_all";
    case SettingId::kUnlockOnlineAchievements:
      return "unlock_online_achievements";
    case SettingId::kHideAchievements:
      return "hide_achievements";
    case SettingId::kLoadGuard:
      return "load_guard";
    case SettingId::kResolution:
      return "resolution";
    case SettingId::kVsync:
      return "vsync";
    case SettingId::kRenderer:
      return "renderer";
    case SettingId::kAntiAliasing:
      return "anti_aliasing";
    case SettingId::kFiltering:
      return "filtering";
    case SettingId::kDisplay:
      return "display";
    case SettingId::kLetterbox:
      return "letterbox";
    case SettingId::kHideOfflinePrompts:
      return "hide_offline_prompts";
    case SettingId::kHideMainMenu:
      return "hide_main_menu";
    case SettingId::kHiddenMainMenu:
      return "hidden_main_menu";
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
    case SettingId::kBackgroundMusicNavigation:
      return "background_music_navigation";
    case SettingId::kSettingsLabels:
      return "settings_labels";
    case SettingId::kHideDownloadContent:
      return "hide_download_content";
    case SettingId::kShowMessages:
      return "show_messages";
    case SettingId::kShowRecommendations:
      return "show_recommendations";
    case SettingId::kShowMessagesRecommendations:
      return "show_messages_recommendations";
    case SettingId::kRhythmVerse:
      return "rhythmverse";
    case SettingId::kRhythmVerseGuitar:
      return "rhythmverse_guitar";
    case SettingId::kRhythmVerseBass:
      return "rhythmverse_bass";
    case SettingId::kRhythmVerseDrums:
      return "rhythmverse_drums";
    case SettingId::kRhythmVerseVocals:
      return "rhythmverse_vocals";
    case SettingId::kRhythmVerseKeys:
      return "rhythmverse_keys";
    case SettingId::kPrecisionMode:
      return "precision_mode";
    case SettingId::kDisableRandomWiggles:
      return "disable_random_wiggles";
    case SettingId::kCameraAngle:
      return "camera_angle";
    case SettingId::kGameSpeed:
      return "game_speed";
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
    case SettingId::kUnknown:
      break;
  }
  return "unknown";
}

bool ValueEquals(const Value& lhs, const Value& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  switch (lhs.kind) {
    case ValueKind::kNone:
      return true;
    case ValueKind::kBool:
      return lhs.boolean == rhs.boolean;
    case ValueKind::kInt:
      return lhs.integer == rhs.integer;
    case ValueKind::kEnum:
    case ValueKind::kString:
      return lhs.text == rhs.text;
    case ValueKind::kList:
      return lhs.list == rhs.list;
    case ValueKind::kResolution:
      return lhs.width == rhs.width && lhs.height == rhs.height;
  }
  return false;
}

std::string ValueToString(const Value& value) {
  switch (value.kind) {
    case ValueKind::kNone:
      return "<none>";
    case ValueKind::kBool:
      return value.boolean ? "true" : "false";
    case ValueKind::kInt: {
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%d", value.integer);
      return buffer;
    }
    case ValueKind::kEnum:
    case ValueKind::kString:
      return value.text;
    case ValueKind::kList: {
      std::string out = "[";
      for (std::size_t i = 0; i < value.list.size(); ++i) {
        if (i != 0) {
          out += ", ";
        }
        out += value.list[i];
      }
      out += "]";
      return out;
    }
    case ValueKind::kResolution: {
      char buffer[32];
      std::snprintf(buffer, sizeof(buffer), "%dx%d", value.width, value.height);
      return buffer;
    }
  }
  return "<invalid>";
}

bool IsAction(SettingId id) { return id == SettingId::kUnlockOnlineAchievements; }

std::string_view CategoryToken(Category category) {
  switch (category) {
    case Category::kGeneral:
      return "general";
    case Category::kGraphics:
      return "graphics";
    case Category::kInterface:
      return "interface";
    case Category::kGameplay:
      return "gameplay";
  }
  return "general";
}

std::string_view ProviderToken(Provider provider) {
  switch (provider) {
    case Provider::kNative:
      return "native";
    case Provider::kUltimate:
      return "ultimate";
  }
  return "native";
}

std::string_view WidgetToken(WidgetKind widget) {
  switch (widget) {
    case WidgetKind::kBoolean:
      return "boolean";
    case WidgetKind::kHorizontalSelect:
      return "horizontal_select";
    case WidgetKind::kSubmenu:
      return "submenu";
    case WidgetKind::kBooleanWithSubmenu:
      return "boolean_with_submenu";
    case WidgetKind::kAction:
      return "action";
  }
  return "boolean";
}

// §5.5's two words, and the same two the provider adapter's own table uses, so a log line, a
// handoff doc and the acceptance matrix cannot spell "needs a restart" three different ways.
std::string_view ApplicationStrategyToken(ApplicationStrategy strategy) {
  switch (strategy) {
    case ApplicationStrategy::kHot:
      return "hot";
    case ApplicationStrategy::kRestart:
      return "restart";
  }
  return "restart";
}

std::string_view SourceToken(Source source) {
  switch (source) {
    case Source::kCompiledDefault:
      return "compiled_default";
    case Source::kRecommendedFirstRun:
      return "recommended_first_run";
    case Source::kSettingsFile:
      return "settings_file";
    case Source::kDeveloperConfig:
      return "developer_config";
    case Source::kEnvironment:
      return "environment";
    case Source::kCommandLine:
      return "command_line";
    case Source::kProvider:
      return "provider";
  }
  return "compiled_default";
}

std::string_view OriginToken(Origin origin) {
  switch (origin) {
    case Origin::kDefault:
      return "default";
    case Origin::kAutomatic:
      return "automatic";
    case Origin::kManual:
      return "manual";
  }
  return "default";
}

std::string_view CommitStatusToken(CommitStatus status) {
  switch (status) {
    case CommitStatus::kSaved:
      return "saved";
    case CommitStatus::kNoChanges:
      return "no_changes";
    case CommitStatus::kConflict:
      return "conflict";
    case CommitStatus::kInvalid:
      return "invalid";
    case CommitStatus::kIoError:
      return "io_error";
    case CommitStatus::kPartial:
      return "partial";
  }
  return "no_changes";
}

}  // namespace rb_blitz::settings
