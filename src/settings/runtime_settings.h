// SPDX-License-Identifier: GPL-2.0-only
// Typed title settings retain their feature ownership and legacy persisted names.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <rex/setting.h>
#include <rex/settings_registry.h>

namespace rb_blitz::settings {
namespace detail {
inline auto& TitleSettingOwners() {
  static std::map<std::string, std::unique_ptr<rex::SettingOwner>> owners;
  return owners;
}
}  // namespace detail

// Owners are assembled by feature declarations before runtime threads start.
inline rex::SettingOwner& TitleSettingOwner(std::string_view scope) {
  auto& owner = detail::TitleSettingOwners()[std::string(scope)];
  if (!owner) owner = std::make_unique<rex::SettingOwner>(scope);
  return *owner;
}

inline rex::Result<void> RegisterTitleSettings(rex::SettingsRegistry& registry) {
  for (auto& [scope, owner] : detail::TitleSettingOwners()) {
    auto registered = registry.Register(*owner);
    if (!registered) return registered;
    for (auto* setting : owner->GetOwnedSettings()) {
      const auto name = std::string_view(setting->GetName()).substr(scope.size()+1);
      registry.AddAlias(name, setting->GetName());
    }
  }
  return rex::Ok();
}

template<class T> T ReadSdkSetting(std::string_view name) {
  auto found = REX_SETTINGS().Find(name);
  if (!found) throw std::runtime_error(found.error().GetDescription());
  if ((*found)->GetType() != rex::detail::TypeOf<T>())
    throw std::runtime_error("Unexpected type for SDK setting " + std::string(name));
  return static_cast<rex::Setting<T>*>(*found)->Get();
}

inline std::string ReadSdkPath(std::string_view name) {
  const auto text = ReadSdkSetting<std::filesystem::path>(name).u8string();
  return {text.begin(), text.end()};
}

namespace title {
extern rex::Setting<bool> window_resizable;
extern rex::Setting<bool> enhancements_hide_achievements;
extern rex::Setting<bool> enhancements_expand_resolution;
extern rex::Setting<bool> enhancements_ui_scale;
extern rex::Setting<bool> enhancements_skip_offline_dialog;
extern rex::Setting<bool> enhancements_icon_schemes;
extern rex::Setting<bool> enhancements_hide_menu_options;
extern rex::Setting<bool> hide_exit_game;
extern rex::Setting<std::string> enhancements_hidden_menu_options;
extern rex::Setting<bool> enhancements_rename_mod_settings;
extern rex::Setting<bool> enhancements_refresh_music_library;
extern rex::Setting<bool> enhancements_dlc_uncap_enumeration;
extern rex::Setting<bool> enhancements_force_controller_scheme;
extern rex::Setting<std::string> enhancements_controller_scheme;
extern rex::Setting<bool> enhancements_menu_dlc_songs;
extern rex::Setting<bool> enhancements_background_music_nav;
extern rex::Setting<bool> enhancements_music_player;
extern rex::Setting<int32_t> enhancements_menu_dlc_song_count;
extern rex::Setting<std::string> gpu_backend;
extern rex::Setting<bool> probe_framerate;
extern rex::Setting<bool> probe_gameplay;
extern rex::Setting<bool> probe_gameplay_bypass;
extern rex::Setting<bool> probe_gameplay_inject;
extern rex::Setting<bool> probe_gameplay_inject_track;
extern rex::Setting<bool> probe_gameplay_block_dispatch;
extern rex::Setting<bool> probe_gameplay_inject_spam;
extern rex::Setting<bool> probe_gameplay_challenge;
extern rex::Setting<bool> probe_gameplay_sources;
extern rex::Setting<bool> probe_overlay;
extern rex::Setting<bool> probe_trace;
extern rex::Setting<int32_t> probe_trace_capacity;
extern rex::Setting<std::string> probe_trace_path;
extern rex::Setting<bool> probe_screen_1080p;
extern rex::Setting<bool> probe_halfres_flag;
extern rex::Setting<std::string> probe_aspect;
extern rex::Setting<bool> probe_dump_guest_swap;
extern rex::Setting<bool> probe_screen_fields;
extern rex::Setting<std::string> probe_screen_fields_filter;
extern rex::Setting<bool> audio_stream_catchup;
extern rex::Setting<std::string> dlc_root;
extern rex::Setting<std::string> dlc_library;
extern rex::Setting<std::string> dlc_library_content_type;
extern rex::Setting<uint32_t> dlc_scan_threads;
extern rex::Setting<bool> hud_reanchor;
extern rex::Setting<bool> hud_reanchor_apply;
extern rex::Setting<double> hud_reanchor_hud_half_height;
extern rex::Setting<bool> hud_reanchor_probe;
extern rex::Setting<bool> hud_reanchor_scan;
extern rex::Setting<bool> local_save;
extern rex::Setting<bool> settings_bridge;
extern rex::Setting<bool> settings_bridge_forward_only;
extern rex::Setting<bool> settings_bridge_provider_active;
extern rex::Setting<bool> settings_hot_apply_diagnostic;
extern rex::Setting<bool> settings_ultimate_refresh_trial;
extern rex::Setting<std::string> background_music_nav_probe;
extern rex::Setting<int32_t> background_music_nav_probe_delay;
extern rex::Setting<bool> background_music_nav_pause_trial;
extern rex::Setting<int32_t> ultimate_mode;
extern rex::Setting<std::string> ultimate_payload_root;
extern rex::Setting<uint32_t> ultimate_patches;
extern rex::Setting<bool> mouse_ui_nav;
extern rex::Setting<bool> mouse_native_trace;
extern rex::Setting<bool> keyboard_support;
extern rex::Setting<bool> input_source_trace;
extern rex::Setting<std::string> touch_lane_swipe;
extern rex::Setting<bool> touch_buttons;
extern rex::Setting<std::string> touch_motion_deploy;
extern rex::Setting<bool> touch_preview;
extern rex::Setting<std::string> startup_mode;
extern rex::Setting<bool> song_load_crash_recovery;
extern rex::Setting<bool> song_load_recovered;
extern rex::Setting<bool> song_load_test_crash;
extern rex::Setting<std::string> song_load_guard;
extern rex::Setting<int32_t> song_load_timeout;
extern rex::Setting<bool> song_load_test_stall;
extern rex::Setting<bool> song_load_trace;
extern rex::Setting<std::string> messages_api_url;
extern rex::Setting<std::string> messages_browser_opener_path;
extern rex::Setting<std::string> launcher_profile;
extern rex::Setting<bool> profile_controller_rows_only;
extern rex::Setting<bool> settings_safe_graphics;
extern rex::Setting<bool> store_art_off;
extern rex::Setting<std::string> store_art_dump_dir;
extern rex::Setting<int32_t> store_catalogue_ttl_seconds;
extern rex::Setting<int32_t> store_catalogue_pages;
extern rex::Setting<int32_t> store_page_cache_ttl_seconds;
extern rex::Setting<bool> store_catalogue_off;
extern rex::Setting<std::string> store_api_base;
extern rex::Setting<std::string> store_serve_dir;
extern rex::Setting<bool> store_fetch_deliver;
extern rex::Setting<bool> store_offer_type_symbol;
extern rex::Setting<std::string> store_front_menu_row;
extern rex::Setting<bool> store_trace;
extern rex::Setting<std::string> store_local_server_root;
extern rex::Setting<std::string> store_search_seed;
extern rex::Setting<int32_t> store_search_pages;
extern rex::Setting<bool> portrait_render_cutoff;
extern rex::Setting<bool> settings_help_overlay;
extern rex::Setting<std::string> settings_help_size;
}  // namespace title
}  // namespace rb_blitz::settings
