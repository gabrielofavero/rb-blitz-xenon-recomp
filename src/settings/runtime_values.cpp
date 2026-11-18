// SPDX-License-Identifier: GPL-2.0-only
#include "settings/runtime_values.h"
#include "settings/runtime_settings.h"
#include "settings/graphics_settings.h"

#include <format>
#include <limits>
#include <utility>
#include <rex/platform/process.h>

namespace rb_blitz::settings {
namespace {

struct LegacyLoggingInputs : rex::SettingOwner {
  LegacyLoggingInputs() : SettingOwner("blitz.logging_input") {}
  // Historical CLI/environment parsing accepted positive int32 values even
  // beyond the advertised 100 MiB UI range. Keep those diagnostics usable.
  REX_SETTING(rex::i32, max_file_size_mb, 5, "Historical log rotation size in MiB",
              rex::Range{1, std::numeric_limits<rex::i32>::max()}, rex::kUntilLaunch, rex::kNoSave);
};
LegacyLoggingInputs& LoggingInputs() { static LegacyLoggingInputs value; return value; }

// These are bindings used by the title's profile, graphics and diagnostic paths.
// The scalar resolution scale and display mode need explicit paired conversions.
constexpr std::pair<std::string_view, std::string_view> kSdkBindings[] = {
    {"resolution", "ui.resolution"},
    {"draw_resolution_scale_x", "gpu.resolution_scale_x"},
    {"draw_resolution_scale_y", "gpu.resolution_scale_y"},
    {"render_target_path_d3d12", "gpu.d3d12.render_target_path"},
    {"render_target_path_vulkan", "gpu.vulkan.render_target_path"},
    {"vsync", "gpu.vsync"},
    {"frame_limit", "gpu.frame_limit"},
    {"anisotropic_override", "gpu.anisotropic_override"},
    {"resolution_scale_x", "gpu.resolution_scale_x"},
    {"resolution_scale_y", "gpu.resolution_scale_y"},
    {"native_2x_msaa", "gpu.xenos.native_2x_msaa"},
    {"swap_post_effect", "gpu.xenos.swap_post_effect"},
    {"async_shader_compilation", "gpu.xenos.async_shader_compilation"},
    {"store_shaders", "gpu.xenos.store_shaders"},
    {"native_pipeline_cache", "gpu.xenos.native_pipeline_cache"},
    {"draw_visible_region", "gpu.xenos.draw_visible_region"},
    {"draw_visible_source_width", "gpu.xenos.draw_visible_source_width"},
    {"draw_visible_source_height", "gpu.xenos.draw_visible_source_height"},
    {"draw_visible_width", "gpu.xenos.draw_visible_width"},
    {"draw_visible_guard", "gpu.xenos.draw_visible_guard"},
    {"draw_visible_color_info", "gpu.xenos.draw_visible_color_info"},
    {"draw_visible_depth_info", "gpu.xenos.draw_visible_depth_info"},
    {"draw_visible_region_probe", "gpu.xenos.draw_visible_region_probe"},
    {"present_letterbox", "gpu.xenos.present_letterbox"},
    {"present_source_aspect", "gpu.xenos.present_source_aspect"},
    {"clear_caches_on_present_resize", "gpu.xenos.clear_caches_on_present_resize"},
    {"present_safe_area_x", "gpu.xenos.present_safe_area_x"},
    {"present_safe_area_y", "gpu.xenos.present_safe_area_y"},
    {"present_allow_overscan_cutoff", "gpu.xenos.present_allow_overscan_cutoff"},
    {"present_dither", "gpu.xenos.present_dither"},
    {"shader_compile_trace", "diagnostics.shader_compile_trace"},
    {"d3d12_readback_resolve", "gpu.d3d12.readback_resolve"},
    {"d3d12_readback_memexport", "gpu.d3d12.readback_memexport"},
    {"d3d12_pipeline_creation_threads", "gpu.d3d12.pipeline_creation_threads"},
    {"d3d12_render_target_path", "gpu.d3d12.render_target_path"},
    {"vulkan_readback_resolve", "gpu.vulkan.readback_resolve"},
    {"vulkan_readback_memexport", "gpu.vulkan.readback_memexport"},
    {"vulkan_async_skip_incomplete_frames", "gpu.vulkan.async_skip_incomplete_frames"},
    {"vulkan_pipeline_creation_threads", "gpu.vulkan.pipeline_creation_threads"},
    {"vulkan_render_target_path", "gpu.vulkan.render_target_path"},
    {"vulkan_texture_descriptor_cache", "gpu.vulkan.texture_descriptor_cache"},
    {"vulkan_shared_memory_upload_batching", "gpu.vulkan.shared_memory_upload_batching"},
    {"license_mask", "kernel.license_mask"},
    {"deferred_overlapped_delay_ms", "kernel.deferred_overlapped_delay_ms"},
    {"user_language", "kernel.user_language"},
    {"user_country", "kernel.user_country"},
    {"audio_mute", "apu.mute"},
    {"audio_maxqframes", "apu.max_queued_frames"},
    {"monitor", "ui.monitor"},
    {"window_width", "ui.window_width"},
    {"window_height", "ui.window_height"},
    {"video_mode_width", "ui.video_mode_width"},
    {"video_mode_height", "ui.video_mode_height"},
    {"video_mode_refresh_rate", "ui.video_mode_refresh_rate"},
    {"input_backend", "hid.drivers"},
    {"mnk_mode", "hid.mnk_mode"},
    {"mnk_mouse", "hid.mnk_mouse"},
    {"mnk_sensitivity", "hid.mnk_sensitivity"},
    {"mnk_passthrough", "hid.mnk_passthrough"},
    {"guide_button", "hid.guide_button"},
    {"vibration", "hid.vibration"},
    {"user_data_root", "app.user_root"},
    {"cache_root", "app.cache_root"},
    {"game_data_root", "title.game_root"},
    {"update_data_root", "title.update_root"},
    {"log_level", "log.level"},
    {"log_file", "log.file_name"},
    {"log_max_files", "log.max_files"},
    {"log_max_file_size_bytes", "log.max_file_size_bytes"},
    {"log_max_file_size_mb", "blitz.logging_input.max_file_size_mb"},
};

std::string EnvironmentName(std::string_view name) {
  std::string result = "REX_";
  for (const char c : name)
    result += c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c == '.' ? '_' : c;
  return result;
}

template<class T> std::string ActiveText(rex::SettingBase& setting) {
  const auto value = static_cast<rex::Setting<T>&>(setting).Get();
  if constexpr (std::is_same_v<T, std::string>) return value;
  else if constexpr (std::is_same_v<T, std::filesystem::path>) {
    const auto utf8 = value.u8string();
    return {utf8.begin(), utf8.end()};
  } else return std::format("{}", value);
}

std::string ActiveText(rex::SettingBase& setting) {
  switch(setting.GetType()) {
    case rex::SettingType::kBool: return ActiveText<bool>(setting);
    case rex::SettingType::kI32: return ActiveText<rex::i32>(setting);
    case rex::SettingType::kI64: return ActiveText<rex::i64>(setting);
    case rex::SettingType::kU32: return ActiveText<rex::u32>(setting);
    case rex::SettingType::kU64: return ActiveText<rex::u64>(setting);
    case rex::SettingType::kF64: return ActiveText<rex::f64>(setting);
    case rex::SettingType::kString: return ActiveText<std::string>(setting);
    case rex::SettingType::kPath: return ActiveText<std::filesystem::path>(setting);
  }
  return {};
}
}  // namespace

std::optional<std::string> ResolveRuntimeSettingName(std::string_view name) {
  if (auto found=REX_SETTINGS().Find(name); found) return (*found)->GetName();
  for (const auto& [scope,owner] : detail::TitleSettingOwners())
    for (auto* setting : owner->GetOwnedSettings())
      if (std::string_view(setting->GetName()).substr(scope.size()+1)==name)
        return setting->GetName();
  for (const auto& [legacy,canonical] : kSdkBindings)
    if (name==legacy || name==canonical) return std::string(canonical);
  return std::nullopt;
}

RuntimeSettingSnapshot InspectRuntimeSetting(std::string_view name) {
  RuntimeSettingSnapshot result;
  const auto canonical=ResolveRuntimeSettingName(name);
  if (!canonical) return result;
  result.canonical_name=*canonical;
  auto found=REX_SETTINGS().Find(*canonical);
  if (!found) return result;
  result.registered=true;
  result.active_value=ActiveText(**found);
  result.staged_value=(*found)->GetText();
  result.staged_source=(*found)->GetLayer();
  result.restart_pending=(*found)->IsRestartPending();
  return result;
}

RuntimeSettingWriteResult WriteRuntimeSetting(std::string_view name, std::string_view value,
                                             rex::SettingLayer layer) {
  const auto canonical=ResolveRuntimeSettingName(name);
  if (!canonical) return {RuntimeSettingWrite::kUnknown, "unknown setting: "+std::string(name)};
  auto found=REX_SETTINGS().Find(*canonical);
  if (!found) {
    if (layer!=rex::SettingLayer::kTitle)
      return {RuntimeSettingWrite::kUnknown, "setting is not registered: "+*canonical};
    auto queued=REX_SETTINGS().SetText(layer,*canonical,value);
    if (!queued) return {RuntimeSettingWrite::kRefused,queued.error().GetDescription()};
    return {RuntimeSettingWrite::kDeferred,{}};
  }
  if (auto valid=(*found)->Check(value); !valid)
    return {RuntimeSettingWrite::kRefused,valid.error().GetDescription()};
  if (layer<(*found)->GetLayer()) return {RuntimeSettingWrite::kShadowed,{}};
  const auto before=(*found)->GetText();
  auto stored=(*found)->SetText(layer,value);
  if (!stored) return {RuntimeSettingWrite::kRefused,stored.error().GetDescription()};
  if ((*found)->IsRestartPending()) return {RuntimeSettingWrite::kPendingRestart,{}};
  return {before==(*found)->GetText() ? RuntimeSettingWrite::kUnchanged
                                    : RuntimeSettingWrite::kApplied,{}};
}

rex::Result<void> RegisterRuntimeSettingAliases(rex::SettingsRegistry& registry) {
  REX_TRY_VOID(RegisterLegacyGraphicsInputs(registry));
  REX_TRY_VOID(registry.Register(LoggingInputs()));
  // Noisy imports now use ordinary trace categories; retain old diagnostic
  // launchers while log.level and log.categories control their verbosity.
  registry.AddAlias("log_noisy", "");
  for (const auto& [legacy,canonical] : kSdkBindings) registry.AddAlias(legacy,canonical);
  auto apply_environment=[&](std::string_view legacy,std::string_view canonical)->rex::Result<void> {
    const auto value=rex::platform::GetEnvironmentValue(EnvironmentName(legacy));
    if (!value) return rex::Ok();
    return registry.SetText(rex::SettingLayer::kEnvironment,canonical,*value);
  };
  for (const auto& [legacy,canonical] : kSdkBindings)
    REX_TRY_VOID(apply_environment(legacy,canonical));
  for (const auto* name : {"fullscreen", "borderless", "resolution_scale"})
    REX_TRY_VOID(apply_environment(name,"blitz.graphics_input."+std::string(name)));
  for (const auto& [scope,owner] : detail::TitleSettingOwners())
    for (auto* setting : owner->GetOwnedSettings())
      REX_TRY_VOID(apply_environment(std::string_view(setting->GetName()).substr(scope.size()+1),
                                   setting->GetName()));
  return rex::Ok();
}

rex::Result<void> ApplyLegacyLoggingInputs() {
  const auto& input = LoggingInputs().max_file_size_mb;
  if (input.GetLayer() == rex::SettingLayer::kDefault) return rex::Ok();
  auto bytes = REX_SETTINGS().Find("log.max_file_size_bytes");
  if (!bytes) return rex::Err(rex::ErrorCategory::kConfig, "missing log rotation setting");
  // Explicit byte values win at the same source layer; default MiB inputs never
  // replace the title's bounded session logging policy.
  if ((*bytes)->GetLayer() >= input.GetLayer()) return rex::Ok();
  constexpr rex::u64 bytes_per_mib = 1024u * 1024u;
  static_assert(rex::u64(std::numeric_limits<rex::i32>::max()) <=
                std::numeric_limits<rex::u64>::max() / bytes_per_mib);
  return (*bytes)->SetText(input.GetLayer(),
                          std::to_string(rex::u64(input.Get()) * bytes_per_mib));
}
}  // namespace rb_blitz::settings
