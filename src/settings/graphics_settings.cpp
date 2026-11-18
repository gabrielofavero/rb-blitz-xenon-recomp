// SPDX-License-Identifier: GPL-2.0-only
#include "settings/graphics_settings.h"
#include "settings/runtime_values.h"
#include <algorithm>
#include <charconv>

namespace rb_blitz::settings {
namespace {
struct LegacyGraphicsInputs : rex::SettingOwner {
  LegacyGraphicsInputs() : SettingOwner("blitz.graphics_input") {}
  REX_SETTING(bool, fullscreen, false, "Historical exclusive fullscreen input", rex::kUntilLaunch, rex::kNoSave);
  REX_SETTING(bool, borderless, false, "Historical borderless fullscreen input", rex::kUntilLaunch, rex::kNoSave);
  REX_SETTING(rex::i32, resolution_scale, 1, "Historical uniform draw scale input", rex::Range{1,7}, rex::kUntilLaunch, rex::kNoSave);
};
LegacyGraphicsInputs& Inputs() { static LegacyGraphicsInputs value; return value; }

rex::Result<void> Project(std::string_view name, std::string_view value, rex::SettingLayer source) {
  auto setting=REX_SETTINGS().Find(name);
  if (!setting) return rex::Err(rex::ErrorCategory::kConfig,"missing graphics setting: "+std::string(name));
  // An explicitly named canonical value wins at the same or a higher layer.
  if ((*setting)->GetLayer() >= source) return rex::Ok();
  return (*setting)->SetText(source,value);
}
}  // namespace

rex::Result<void> RegisterLegacyGraphicsInputs(rex::SettingsRegistry& registry) {
  REX_TRY_VOID(registry.Register(Inputs()));
  for (const auto* name : {"fullscreen", "borderless", "resolution_scale"})
    registry.AddAlias(name,"blitz.graphics_input."+std::string(name));
  return rex::Ok();
}

rex::Result<void> ApplyLegacyGraphicsInputs() {
  auto& input=Inputs();
  const auto display_source=std::max(input.fullscreen.GetLayer(),input.borderless.GetLayer());
  if (display_source!=rex::SettingLayer::kDefault) {
    const bool exclusive=input.fullscreen.Get();
    const bool fullscreen=exclusive || input.borderless.Get();
    REX_TRY_VOID(Project("ui.fullscreen",fullscreen ? "true" : "false",display_source));
    REX_TRY_VOID(Project("ui.fullscreen_exclusive",exclusive ? "true" : "false",display_source));
  }
  const auto scale_source=input.resolution_scale.GetLayer();
  if (scale_source!=rex::SettingLayer::kDefault) {
    const auto value=std::to_string(input.resolution_scale.Get());
    REX_TRY_VOID(Project("gpu.resolution_scale_x",value,scale_source));
    REX_TRY_VOID(Project("gpu.resolution_scale_y",value,scale_source));
  }
  return rex::Ok();
}

std::optional<int32_t> UniformResolutionScale(bool staged) {
  const auto x=InspectRuntimeSetting("gpu.resolution_scale_x");
  const auto y=InspectRuntimeSetting("gpu.resolution_scale_y");
  const auto& sx=staged ? x.staged_value : x.active_value;
  const auto& sy=staged ? y.staged_value : y.active_value;
  if (!x.registered || !y.registered || sx!=sy) return std::nullopt;
  int32_t value=0;
  const auto parsed=std::from_chars(sx.data(),sx.data()+sx.size(),value);
  if (parsed.ec!=std::errc{} || parsed.ptr!=sx.data()+sx.size()) return std::nullopt;
  return value;
}

std::string ActiveDisplayMode() {
  const auto fullscreen=InspectRuntimeSetting("ui.fullscreen");
  const auto exclusive=InspectRuntimeSetting("ui.fullscreen_exclusive");
  if (!fullscreen.registered || !exclusive.registered) return {};
  if (fullscreen.active_value!="true") return "windowed";
  return exclusive.active_value=="true" ? "fullscreen" : "borderless";
}
}  // namespace rb_blitz::settings
