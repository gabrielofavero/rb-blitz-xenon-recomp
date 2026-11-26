// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "graphics/preparation_settings.h"
#include "settings/runtime_settings.h"
#include "settings/title_defaults.h"
#include "platform/bootstrap_arguments.h"
#include <rex/app/app.h>
#include <rex/cli.h>
#include <rex/logging/logger.h>
#include <rex/path_settings.h>
#include <rex/ui/ui_system.h>
#include <gpu/xenos/xenos_settings.h>
#include <gpu/xenos/host/d3d12/d3d12_settings.h>
#include <gpu/xenos/host/vulkan/vulkan_settings.h>
#include <cstdio>
#include <vector>

using namespace rb_blitz::settings;
using namespace rb_blitz::test;
using rex::SettingLayer;
REXLOG_CATEGORY(legacy_fixture, "Late category for config compatibility tests")
REXLOG_TABLE(kLegacyFixtureLog, legacy_fixture)
struct XenosInputs final : rex::gpu::XenosSettings {};

class RegistryApp final : public rex::app::App {
 public:
  RegistryApp() : App("preparation_settings_registry") {}
  XenosInputs xenos;
  rex::gpu::d3d12::D3D12Settings d3d12;
  rex::gpu::vulkan::VulkanSettings vulkan;
  bool late_aliases_applied = false;
  bool startup_defaults_preserved = false;
  std::filesystem::path config_file;
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(RegisterTitleSettings(registry));
    REX_TRY_VOID(RegisterRuntimeSettingAliases(registry));
    REX_TRY(auto* game_root, registry.Find("title.game_root"));
    REX_TRY(auto* update_root, registry.Find("title.update_root"));
    const auto explicit_game = std::filesystem::current_path() / "explicit-game";
    REX_TRY_VOID((*game_root).SetText(SettingLayer::kCommandLine, explicit_game.string()));
    REX_TRY_VOID(GetUI().GetSettings().title.SetText(SettingLayer::kEnvironment, "Explicit title"));
    REX_TRY_VOID(SetTitleDefault((*update_root), "first-default"));
    REX_TRY_VOID(SetTitleDefault((*update_root), "refined-default"));
    const bool refined = (*update_root).GetText() == "refined-default" &&
                         (*update_root).GetLayer() == SettingLayer::kTitle;
    REX_TRY_VOID((*update_root).SetText(SettingLayer::kFile, "explicit-update"));
    REX_TRY_VOID(SetTitleDefault((*update_root), "fallback-update"));
    REX_TRY_VOID(SetTitleDefault((*game_root), "fallback-game"));
    REX_TRY_VOID(SetTitleDefault(GetUI().GetSettings().title, "Fallback title"));
    startup_defaults_preserved = std::filesystem::path(game_root->GetText()) == explicit_game &&
        GetUI().GetSettings().title.Get() == "Explicit title" &&
        (*game_root).GetLayer() == SettingLayer::kCommandLine && refined &&
        (*update_root).GetText() == "explicit-update" &&
        (*update_root).GetLayer() == SettingLayer::kFile;
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle,"native_pipeline_cache","false"));
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle,"vulkan_shared_memory_upload_batching","false"));
    BeginCase("Graphics preferences queue before the Xenos owner registers");
    CHECK_FALSE(xenos.present_source_aspect.Get());
    CHECK_FALSE(xenos.clear_caches_on_present_resize.Get());
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, const char*>>{
         {"native_2x_msaa", "false"}, {"swap_post_effect", "fxaa"},
         {"gpu.xenos.present_source_aspect", "true"},
         {"gpu.xenos.clear_caches_on_present_resize", "true"}}) {
      CHECK_FALSE(InspectRuntimeSetting(name).registered);
      CHECK_TRUE(WriteRuntimeSetting(name, value, SettingLayer::kTitle).state ==
                 RuntimeSettingWrite::kDeferred);
    }
    REX_TRY_VOID(registry.Register(xenos));
    BeginCase("Queued graphics preferences become active before startup freezes");
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, const char*>>{
         {"native_2x_msaa", "false"}, {"swap_post_effect", "fxaa"},
         {"gpu.xenos.present_source_aspect", "true"},
         {"gpu.xenos.clear_caches_on_present_resize", "true"}}) {
      const auto actual = InspectRuntimeSetting(name);
      CHECK_TRUE(actual.registered);
      CHECK_TRUE(actual.active_value == value);
      CHECK_TRUE(actual.staged_value == value);
      CHECK_TRUE(actual.staged_source == SettingLayer::kTitle);
      CHECK_FALSE(actual.restart_pending);
    }
    REX_TRY_VOID(registry.Register(d3d12));
    BeginCase("Exact Vulkan scale admission queues before driver registration");
    CHECK_FALSE(vulkan.resolution_scale_strict.Get());
    REX_TRY_VOID(registry.SetText(SettingLayer::kFile, "gpu.vulkan.resolution_scale_strict", "false"));
    REX_TRY_VOID(registry.SetText(SettingLayer::kRuntime, "gpu.vulkan.resolution_scale_strict", "true"));
    CHECK_FALSE(InspectRuntimeSetting("gpu.vulkan.resolution_scale_strict").registered);
    REX_TRY_VOID(registry.Register(vulkan));
    CHECK_TRUE(vulkan.resolution_scale_strict.Get());
    CHECK_TRUE(vulkan.resolution_scale_strict.GetLayer() == SettingLayer::kRuntime);
    CHECK_FALSE(InspectRuntimeSetting("gpu.vulkan.resolution_scale_strict").restart_pending);
    late_aliases_applied = !xenos.native_pipeline_cache.Get() && !vulkan.shared_memory_upload_batching.Get();
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle,"gpu.xenos.native_pipeline_cache","true"));
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle,"gpu.vulkan.shared_memory_upload_batching","true"));
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    for (const auto* name : {"ui.driver", "hid.drivers", "apu.driver", "gpu.driver"})
      REX_TRY_VOID(registry.SetText(SettingLayer::kTitle, name, "none"));
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle,"log.max_file_size_bytes","2097152"));
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle,"log.max_files","1"));
    REX_TRY_VOID(registry.SetText(SettingLayer::kTitle, "log.file", "false"));
    if (config_file.empty()) return rex::Ok();
    REX_TRY_VOID(REX_PATHS().config_file.SetText(SettingLayer::kTitle, config_file.string()));
    return RegisterLegacyLogCategoryConfig(registry, config_file);
  }
  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry&) override {
    return ApplyLegacyLoggingInputs();
  }
};

int main(int argc, char** argv) {
  BeginCase("Consumed process handoff options leave user CLI semantics intact");
  const std::vector<std::string> transport{
      "first positional", "--ui.title", "A title with \"quotes\" and spaces",
      "--app.user_root=C:\\fixture folder\\user", "--unknown-user-flag",
      "--window-host=1.2.3", "second positional", "--restart-handoff=v1.1.2.3.common",
      "--restart-bootstrap", "--gpu.driver=vulkan", "--gpu.driver=d3d12",
      "--window-hostile=keep", "--restart-handoffs=keep", "--restart-bootstrap-extra"};
  const auto normalized = rb_blitz::platform::NormalizeBootstrapArguments(rex::CLIArgList::Parse(transport));
  CHECK_FALSE(normalized.Has("window-host"));
  CHECK_FALSE(normalized.Has("restart-handoff"));
  CHECK_FALSE(normalized.Has("restart-bootstrap"));
  CHECK_TRUE(normalized.Get("ui.title") == "A title with \"quotes\" and spaces");
  CHECK_TRUE(normalized.Get("app.user_root") == "C:\\fixture folder\\user");
  CHECK_TRUE(normalized.Has("unknown-user-flag") && !normalized.Get("unknown-user-flag"));
  CHECK_TRUE(normalized.GetPositionals().size() == 2);
  CHECK_TRUE(normalized.GetPositionals()[0] == "first positional");
  CHECK_TRUE(normalized.GetPositionals()[1] == "second positional");
  CHECK_TRUE(normalized.Get("gpu.driver") == "d3d12");
  CHECK_TRUE(normalized.GetOptions()[3].name == "gpu.driver" &&
             normalized.GetOptions()[3].value == "vulkan");
  CHECK_TRUE(normalized.Get("window-hostile") == "keep");
  CHECK_TRUE(normalized.Get("restart-handoffs") == "keep");
  CHECK_TRUE(normalized.Has("restart-bootstrap-extra"));
  const std::vector<std::string> bare_flags{"--first", "--second", "--window-host=1.2.3", "literal"};
  const auto bare = rb_blitz::platform::NormalizeBootstrapArguments(rex::CLIArgList::Parse(bare_flags));
  CHECK_TRUE(bare.GetOptions().size() == 2);
  CHECK_TRUE(bare.GetOptions()[0].name == "first" && !bare.GetOptions()[0].value);
  CHECK_TRUE(bare.GetOptions()[1].name == "second" && !bare.GetOptions()[1].value);
  CHECK_TRUE(bare.GetPositionals().size() == 1 && bare.GetPositionals()[0] == "literal");
  const std::vector<std::string> legacy_negation{
      "--no_mouse_ui_nav", "--gpu.driver=d3d12", "--mouse_ui_nav=true", "--no_keyboard_support=false"};
  const auto negated = rb_blitz::platform::NormalizeBootstrapArguments(rex::CLIArgList::Parse(legacy_negation));
  CHECK_TRUE(negated.GetOptions().size() == 4);
  CHECK_TRUE(negated.GetOptions()[0].name == "no-mouse_ui_nav" && !negated.GetOptions()[0].value);
  CHECK_TRUE(negated.GetOptions()[2].name == "mouse_ui_nav" && negated.GetOptions()[2].value == "true");
  CHECK_TRUE(negated.GetOptions()[3].name == "no-keyboard_support" && negated.GetOptions()[3].value == "false");
  RegistryApp app;
  std::vector<std::string> args;
  for (int i=1;i<argc;++i) args.emplace_back(argv[i]);
  std::vector<std::string> sdk_args;
  for (const auto& arg : args) {
    if (arg.starts_with("--fixture-config=")) app.config_file = arg.substr(17);
    else if (!arg.starts_with("--expect-")) sdk_args.push_back(arg);
  }
  if (!app.Init(rb_blitz::platform::NormalizeBootstrapArguments(rex::CLIArgList::Parse(sdk_args)))) return 2;
  const auto expect = [&](std::string_view key, std::string_view fallback) {
    for (const auto& arg : args) if (arg.starts_with(std::string(key)+"=")) return arg.substr(key.size()+1);
    return std::string(fallback);
  };
  BeginCase("Canonical active graphics inputs include both concrete backend definitions");
  CHECK_TRUE(app.startup_defaults_preserved);
  CHECK_TRUE(app.late_aliases_applied);
  const auto identity = rb_blitz::graphics::PreparationSettingsIdentity("vulkan");
  CHECK_TRUE(identity == rb_blitz::graphics::PreparationSettingsIdentity("vulkan"));
  CHECK_TRUE(identity != rb_blitz::graphics::PreparationSettingsIdentity("d3d12"));
  for (const auto* name : {"gpu.xenos.native_pipeline_cache", "gpu.xenos.depth_float24_round",
       "gpu.xenos.half_pixel_offset", "gpu.d3d12.dxbc_switch", "gpu.d3d12.native_stencil_value_output",
       "gpu.d3d12.use_fuzzy_alpha_epsilon", "gpu.vulkan.force_expand_rectangle_lists_in_vs",
       "gpu.vulkan.dynamic_rendering", "gpu.vulkan.prefer_geometry_shader"}) {
    CHECK_TRUE(InspectRuntimeSetting(name).registered);
    CHECK_TRUE(identity.find(name) != std::string::npos);
  }
  CHECK_FALSE(InspectRuntimeSetting("gpu.native_pipeline_cache").registered);
  CHECK_TRUE(WriteRuntimeSetting("depth_float24_round", "true", SettingLayer::kRuntime).state
             == RuntimeSettingWrite::kUnknown);
  CHECK_TRUE(WriteRuntimeSetting("gpu.xenos.depth_float24_round", "true", SettingLayer::kRuntime).accepted());
  auto changed = rb_blitz::graphics::PreparationSettingsIdentity("vulkan");
  CHECK_TRUE(identity != changed);
  CHECK_TRUE(WriteRuntimeSetting("gpu.d3d12.dxbc_switch", "false", SettingLayer::kRuntime).accepted());
  CHECK_TRUE(changed != rb_blitz::graphics::PreparationSettingsIdentity("vulkan"));
  changed = rb_blitz::graphics::PreparationSettingsIdentity("vulkan");
  CHECK_TRUE(WriteRuntimeSetting("gpu.vulkan.dynamic_rendering", "false", SettingLayer::kRuntime).accepted());
  CHECK_TRUE(changed != rb_blitz::graphics::PreparationSettingsIdentity("vulkan"));
  BeginCase("Restart staging does not change the identity of active pipelines");
  changed = rb_blitz::graphics::PreparationSettingsIdentity("vulkan");
  const auto active = InspectRuntimeSetting("native_pipeline_cache");
  CHECK_TRUE(active.registered);
  CHECK_TRUE(active.active_value == expect("--expect-native", "true"));
  const auto batching = InspectRuntimeSetting("vulkan_shared_memory_upload_batching");
  CHECK_TRUE(InspectRuntimeSetting("mouse_ui_nav").active_value == expect("--expect-mouse", "true"));
  CHECK_TRUE(batching.registered);
  CHECK_TRUE(batching.active_value == expect("--expect-batching", "true"));
  CHECK_TRUE(WriteRuntimeSetting("native_pipeline_cache", active.active_value=="true"?"false":"true",
                                SettingLayer::kRuntime).state == RuntimeSettingWrite::kPendingRestart);
  CHECK_TRUE(InspectRuntimeSetting("native_pipeline_cache").restart_pending);
  CHECK_TRUE(changed == rb_blitz::graphics::PreparationSettingsIdentity("vulkan"));
  BeginCase("Logging aliases retain canonical units and selected source precedence");
  CHECK_TRUE(ResolveRuntimeSettingName("log_max_files") == "log.max_files");
  CHECK_TRUE(ResolveRuntimeSettingName("log_max_file_size_bytes") == "log.max_file_size_bytes");
  CHECK_TRUE(ResolveRuntimeSettingName("vulkan_shared_memory_upload_batching") == "gpu.vulkan.shared_memory_upload_batching");
  const auto bytes = InspectRuntimeSetting("log.max_file_size_bytes");
  const auto files = InspectRuntimeSetting("log.max_files");
  // Expected values are supplied by the test's CLI/environment scenario.
  CHECK_TRUE(bytes.active_value == expect("--expect-bytes","2097152"));
  CHECK_TRUE(files.active_value == expect("--expect-files","1"));
  if (!app.config_file.empty()) {
    BeginCase("Historical category levels use the canonical logger and preserve explicit canonical values");
    CHECK_TRUE(REX_LOGGER().AddCategories(rex::log::kLegacyFixtureLog));
    for (const auto& [category, expected] : std::initializer_list<std::pair<std::string_view,std::string_view>>{
         {"filesystem","warn"},{"system","error"},{"kernel","off"},{"gpu","debug"},{"legacy_fixture","trace"}}) {
      const auto value = InspectRuntimeSetting("log.categories."+std::string(category));
      CHECK_TRUE(value.registered);
      CHECK_TRUE(value.active_value == expect("--expect-"+std::string(category),expected));
    }
    CHECK_FALSE(rex::log::filesystem.IsEnabled(rex::log::Level::kInfo));
    CHECK_TRUE(rex::log::filesystem.IsEnabled(rex::log::Level::kWarn));
    CHECK_TRUE(rex::log::legacy_fixture.IsEnabled(rex::log::Level::kTrace));
    CHECK_TRUE(InspectRuntimeSetting("log.categories.apu").active_value.empty());
    CHECK_TRUE(InspectRuntimeSetting("log.categories.cpu").active_value.empty());
    REX_LOGGER().RemoveCategories(rex::log::kLegacyFixtureLog);
  }
  REX_SETTINGS().Unregister(app.vulkan);
  REX_SETTINGS().Unregister(app.d3d12);
  REX_SETTINGS().Unregister(app.xenos);
  const auto shutdown = app.Shutdown();
  return shutdown ? shutdown : Finish();
}
