// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 host tests for the settings service (src/settings/service.{h,cpp}). No SDK, no boot.
//
// The behaviours the parent plan asks P3 to prove: durable read/edit/reload, a rejected edit
// that leaves a valid manual value alone, requested/effective/pending staying separate, an
// explicit override disabling a row instead of saving something that cannot take effect, a
// provider save that is only ever reported as durable when the sink says so, and the restart
// handshake's stale/already-requested answers.

#include "check.h"

#include "settings/cvars.h"
#include "settings/service.h"
#include "settings/ios_defaults.h"
#include "settings/ultimate_adapter.h"
#include "settings/schema.h"

#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {

using namespace rb_blitz::settings;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

struct Scratch {
  fs::path base;
  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-settings-service";
    fs::remove_all(base, code);
    fs::create_directories(base, code);
  }
  ~Scratch() {
    std::error_code code;
    fs::remove_all(base, code);
  }
};

StorePaths Paths(const Scratch& scratch) {
  StorePaths paths;
  paths.settings_file = scratch.base / "settings.toml";
  paths.paths_file = scratch.base / "paths.toml";
  paths.legacy_profile = scratch.base / "launcher.toml";
  return paths;
}

Value BoolV(bool value) {
  Value out;
  out.kind = ValueKind::kBool;
  out.boolean = value;
  return out;
}

Value EnumV(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

void WriteFile(const fs::path& path, const std::string& text) {
  std::error_code code;
  fs::create_directories(path.parent_path(), code);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

CapabilitySnapshot Capabilities(uint64_t revision = 1) {
  CapabilitySnapshot caps;
  caps.revision = revision;
  caps.logical_processors = 4;
  caps.primary_display_size = {1920, 1080};
  caps.supported_resolutions.push_back(DisplayMode{1280, 720});
  caps.supported_resolutions.push_back(DisplayMode{1920, 1080});
  RendererCapability renderer;
  renderer.token = "d3d12";
  renderer.built = renderer.detected = renderer.usable = renderer.active = true;
  renderer.filtering_ceiling = 5;
  renderer.native_aa_label = "2x";
  caps.renderers.push_back(renderer);
  caps.provider.installed = true;
  caps.provider.compatible = true;
  caps.provider.active = true;
  caps.provider.version = "2.11";
  return caps;
}

// A reconciler that clamps the scan-thread row, standing in for P4's graphics policy.
class ClampingReconciler : public Reconciler {
 public:
  Reconciliation Reconcile(SettingId id, const Descriptor& descriptor, const Value& requested,
                           const CapabilitySnapshot& caps) const override {
    Reconciliation result;
    result.effective = requested;
    if (id == SettingId::kDlcScanThreads) {
      const int32_t ceiling = static_cast<int32_t>(caps.logical_processors < 1 ? 1
                                                                              : caps.logical_processors);
      if (requested.integer > ceiling) {
        result.effective.integer = ceiling;
        result.corrected = true;
        result.correction_reason_key = "settings.corrected.processor_count";
      }
    }
    return result;
  }
};

class NoNativeAaReconciler : public Reconciler {
 public:
  ValidationResult Validate(SettingId id, const Descriptor& descriptor, const Value& value,
                            const CapabilitySnapshot& caps) const override {
    if (id == SettingId::kAntiAliasing && value.text == "native")
      return {false, "settings.graphics.aa_unavailable_choice"};
    return Reconciler::Validate(id, descriptor, value, caps);
  }
};

struct FakeProvider : ProviderSink {
  ProviderReadiness readiness = ProviderReadiness::kCompatible;
  bool durable = true;
  int saves = 0;
  std::map<SettingId, Value> values;

  ProviderReadiness Readiness() const override { return readiness; }
  std::map<SettingId, Value> ReadValues() const override { return values; }
  ProviderSaveResult Save(const std::map<SettingId, Value>& pending) override {
    ++saves;
    return durable ? ProviderSaveResult{true, ""} : ProviderSaveResult{false, "disk full"};
  }
  ActionResult Invoke(SettingId action, uint64_t request_id) override {
    (void)action;
    (void)request_id;
    return ActionResult{true, ""};
  }
};

void TestStartupRoundTrip() {
  BeginCase("startup: normal default, strict choices, cvar binding and durable preference");
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  constexpr auto id = SettingId::kStartup;
  CHECK_TRUE(service.EffectiveValue(id).text == "normal");
  CHECK_TRUE(CvarForSetting(id) == "startup_mode");
  for (const auto* token : {"fast", "normal"}) {
    const auto txn = service.BeginEdit(service.revision());
    CHECK_FALSE(service.Set(txn, id, EnumV("quick"), service.capability_revision()).ok);
    CHECK_TRUE(service.Set(txn, id, EnumV(token), service.capability_revision()).ok);
    const auto result = service.Commit(txn);
    CHECK_TRUE(result.status == CommitStatus::kSaved);
    CHECK_TRUE(result.restart_required);
    Service reloaded(paths);
    reloaded.Boot(nullptr, PathEntry{}, nullptr);
    CHECK_TRUE(reloaded.EffectiveValue(id).text == token);
  }
}

void TestRefreshSongsDefaultAndOverride() {
  BeginCase("Refresh Songs: fresh profile defaults off and an explicit on choice survives reload");
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  constexpr auto id = SettingId::kRefreshMusicLibrary;
  CHECK_FALSE(service.EffectiveValue(id).boolean);
  CHECK_TRUE(service.EffectiveSource(id) == Source::kCompiledDefault);
  CHECK_TRUE(CvarForSetting(id) == "enhancements_refresh_music_library");

  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, id, BoolV(true), service.capability_revision()).ok);
  CHECK_TRUE(service.Commit(txn).status == CommitStatus::kSaved);
  Service reloaded(paths);
  reloaded.Boot(nullptr, PathEntry{}, nullptr);
  CHECK_TRUE(reloaded.EffectiveValue(id).boolean);
}

void TestControllerSchemeRoundTrip() {
  BeginCase("controller layout: stable id persists and an absent package cannot be selected");
  schema::SetControllerSchemes({{"retail", "Xbox 360"}, {"one-glyph", "One Glyph"}});
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  constexpr auto id = SettingId::kButtonScheme;
  CHECK_TRUE(service.EffectiveValue(id).text == "retail");
  const auto txn = service.BeginEdit(service.revision());
  CHECK_FALSE(service.Set(txn, id, EnumV("missing"), service.capability_revision()).ok);
  CHECK_TRUE(service.Set(txn, id, EnumV("one-glyph"), service.capability_revision()).ok);
  const auto result = service.Commit(txn);
  CHECK_TRUE(result.status == CommitStatus::kSaved);
  CHECK_FALSE(result.restart_required);
  Service reloaded(paths);
  reloaded.Boot(nullptr, PathEntry{}, nullptr);
  reloaded.SetCapabilities(CapabilitySnapshot{});
  CHECK_TRUE(reloaded.EffectiveValue(id).text == "one-glyph");
  CHECK_TRUE(reloaded.store().document().values.at(id).text == "one-glyph");
  schema::SetControllerSchemes({{"retail", "Xbox 360"}});
}

void TestCommitRevalidatesCurrentCatalogAndOverride() {
  BeginCase("service: a package removed after Set cannot be saved by Commit");
  schema::SetControllerSchemes({{"retail", "Xbox 360"}, {"one-glyph", "One Glyph"}});
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  const uint64_t capability = service.capability_revision();
  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, SettingId::kButtonScheme, EnumV("one-glyph"), capability).ok);
  schema::SetControllerSchemes({{"retail", "Xbox 360"}});
  service.RefreshControllerSchemes();
  CHECK_EQ(service.capability_revision(), capability);
  const CommitResult absent_package = service.Commit(txn);
  CHECK_TRUE(absent_package.status == CommitStatus::kInvalid);
  CHECK_EQ(service.revision(), 0u);
  CHECK_FALSE(fs::exists(paths.settings_file));
  service.Cancel(txn);

  BeginCase("service: a newly explicit override invalidates an already staged edit");
  const auto override_txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(override_txn, SettingId::kVsync, BoolV(false), capability).ok);
  service.SetOverrides({{SettingId::kVsync, BoolV(true)}},
                       {{SettingId::kVsync, Source::kCommandLine}});
  const CommitResult overridden = service.Commit(override_txn);
  CHECK_TRUE(overridden.status == CommitStatus::kInvalid);
  CHECK_EQ(service.revision(), 0u);
  CHECK_FALSE(fs::exists(paths.settings_file));
}

void TestLoadGuardRoundTrip() {
  BeginCase("load guard: strict native hot enum persists and invalid values cannot commit");
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  constexpr auto id = SettingId::kLoadGuard;
  const auto* descriptor = service.DescriptorFor(id);
  CHECK_TRUE(descriptor != nullptr);
  if (!descriptor) return;
  CHECK_TRUE(descriptor->provider == Provider::kNative);
  CHECK_TRUE(descriptor->strategy == ApplicationStrategy::kHot);
  CHECK_TRUE(service.EffectiveValue(id).text == "60s");
  CHECK_TRUE(CvarForSetting(id) == "song_load_guard");
  for (const auto* token : {"30s", "60s", "off"}) {
    const auto txn = service.BeginEdit(service.revision());
    CHECK_FALSE(service.Set(txn, id, EnumV("15s"), service.capability_revision()).ok);
    CHECK_TRUE(service.Set(txn, id, EnumV(token), service.capability_revision()).ok);
    const auto result = service.Commit(txn);
    CHECK_TRUE(result.status == CommitStatus::kSaved);
    CHECK_FALSE(result.restart_required);
    Service reloaded(paths);
    reloaded.Boot(nullptr, PathEntry{}, nullptr);
    CHECK_TRUE(reloaded.EffectiveValue(id).text == token);
  }
}

void TestTouchButtonsRoundTrip() {
  BeginCase("touch buttons: iOS visibility is live, persistent, and absent on desktop");
  Scratch scratch;
  const auto paths = Paths(scratch);
  const auto release = schema::SettingsForRelease(schema::HostPlatform::kIOS, "test");
  Service service(paths, release);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  for (const auto id : {SettingId::kTouchButtons}) {
    const auto* descriptor = service.DescriptorFor(id);
    CHECK_TRUE(descriptor && descriptor->availability != Availability::kHidden);
    CHECK_TRUE(descriptor && descriptor->strategy == ApplicationStrategy::kHot);
    CHECK_TRUE(service.EffectiveValue(id).boolean);
    const auto cvar = "touch_buttons";
    CHECK_TRUE(CvarForSetting(id) == cvar);
    CHECK_TRUE(schema::IdForToken(std::string("controller.") + cvar) == id);
    for (const bool enabled : {false, true}) {
      const auto txn = service.BeginEdit(service.revision());
      CHECK_TRUE(service.Set(txn, id, BoolV(enabled), service.capability_revision()).ok);
      const auto result = service.Commit(txn);
      CHECK_TRUE(result.status == CommitStatus::kSaved);
      CHECK_FALSE(result.restart_required);
      Service reloaded(paths, release);
      reloaded.Boot(nullptr, PathEntry{}, nullptr);
      CHECK_EQ(reloaded.EffectiveValue(id).boolean, enabled);
    }
    for (const auto platform : {schema::HostPlatform::kWindows, schema::HostPlatform::kMacOS,
                               schema::HostPlatform::kLinux}) {
      const auto rows = schema::BuildDescriptors(CapabilitySnapshot{}, schema::SettingsForRelease(platform, "test"));
      const auto found = std::find_if(rows.begin(), rows.end(), [id](const auto& row) { return row.id == id; });
      CHECK_TRUE(found != rows.end() && found->availability == Availability::kHidden);
    }
  }
}

void TestMotionDeployRoundTrip() {
  BeginCase("motion deploy: Tilt defaults, three live modes persist, invalid modes are refused");
  Scratch scratch;
  const auto paths = Paths(scratch);
  for (const auto platform : {schema::HostPlatform::kIOS, schema::HostPlatform::kAndroid}) {
    const auto release = schema::SettingsForRelease(platform, "test");
    Service service(paths, release);
    service.Boot(nullptr, {}, nullptr);
    constexpr auto id = SettingId::kTouchMotionDeploy;
    const auto* row = service.DescriptorFor(id);
    CHECK_TRUE(row && row->availability == Availability::kVisible);
    CHECK_TRUE(row && row->strategy == ApplicationStrategy::kHot);
    CHECK_TRUE(row && row->choices.size() == 3);
    CHECK_TRUE(service.EffectiveValue(id).text == "tilt");
    CHECK_TRUE(CvarForSetting(id) == "touch_motion_deploy");
    CHECK_TRUE(schema::IdForToken("controller.touch_motion_deploy") == id);
    for (const auto* mode : {"shake", "none", "tilt"}) {
      const auto edit = service.BeginEdit(service.revision());
      CHECK_FALSE(service.Set(edit, id, EnumV("unknown"), service.capability_revision()).ok);
      CHECK_FALSE(service.Set(edit, id, BoolV(true), service.capability_revision()).ok);
      CHECK_TRUE(service.Set(edit, id, EnumV(mode), service.capability_revision()).ok);
      const auto saved = service.Commit(edit);
      CHECK_TRUE(saved.status == CommitStatus::kSaved);
      CHECK_FALSE(saved.restart_required);
      Service reloaded(paths, release);
      reloaded.Boot(nullptr, {}, nullptr);
      CHECK_TRUE(reloaded.EffectiveValue(id).text == mode);
    }
  }
  for (const auto platform : {schema::HostPlatform::kWindows, schema::HostPlatform::kMacOS,
                             schema::HostPlatform::kLinux}) {
    Service desktop(paths, schema::SettingsForRelease(platform, "test"));
    desktop.Boot(nullptr, {}, nullptr);
    CHECK_TRUE(desktop.DescriptorFor(SettingId::kTouchMotionDeploy)->availability == Availability::kHidden);
  }
}

void TestMobileDefaultsAndLaneSwipe() {
  BeginCase("mobile hides Exit Game by default and persists both lane swipe modes");
  Scratch scratch;
  const auto paths = Paths(scratch);
  for (const auto platform : {schema::HostPlatform::kIOS, schema::HostPlatform::kAndroid}) {
    const auto release = schema::SettingsForRelease(platform, "test");
    Service service(paths, release);
    service.Boot(nullptr, {}, nullptr);
    CHECK_TRUE(service.EffectiveValue(SettingId::kHideExitGame).boolean);
    CHECK_FALSE(service.EffectiveValue(SettingId::kShowRunComparison).boolean);
    constexpr auto id = SettingId::kTouchLaneSwipe;
    const auto* row = service.DescriptorFor(id);
    CHECK_TRUE(row && row->availability == Availability::kVisible);
    CHECK_TRUE(row && row->strategy == ApplicationStrategy::kHot);
    CHECK_TRUE(service.EffectiveValue(id).text == "inverted");
    CHECK_TRUE(CvarForSetting(id) == "touch_lane_swipe");
    CHECK_TRUE(schema::IdForToken("controller.touch_lane_swipe") == id);
    for (const auto* mode : {"normal", "inverted"}) {
      const auto edit = service.BeginEdit(service.revision());
      CHECK_FALSE(service.Set(edit, id, EnumV("sideways"), service.capability_revision()).ok);
      CHECK_TRUE(service.Set(edit, id, EnumV(mode), service.capability_revision()).ok);
      const auto saved = service.Commit(edit);
      CHECK_TRUE(saved.status == CommitStatus::kSaved);
      CHECK_FALSE(saved.restart_required);
      Service reloaded(paths, release);
      reloaded.Boot(nullptr, {}, nullptr);
      CHECK_TRUE(reloaded.EffectiveValue(id).text == mode);
    }
    const auto edit = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(edit, SettingId::kHideExitGame, BoolV(false),
                           service.capability_revision()).ok);
    CHECK_TRUE(service.Commit(edit).restart_required);
    Service reloaded(paths, release);
    reloaded.Boot(nullptr, {}, nullptr);
    CHECK_FALSE(reloaded.EffectiveValue(SettingId::kHideExitGame).boolean);
    // Clear the file before checking the other platform's first-run default.
    std::error_code error;
    fs::remove(paths.settings_file, error);
  }
  for (const auto platform : {schema::HostPlatform::kWindows, schema::HostPlatform::kMacOS,
                             schema::HostPlatform::kLinux}) {
    Service desktop(paths, schema::SettingsForRelease(platform, "test"));
    desktop.Boot(nullptr, {}, nullptr);
    CHECK_FALSE(desktop.EffectiveValue(SettingId::kHideExitGame).boolean);
    CHECK_TRUE(desktop.DescriptorFor(SettingId::kTouchLaneSwipe)->availability == Availability::kHidden);
    CHECK_TRUE(desktop.DescriptorFor(SettingId::kHideExitGame)->availability == Availability::kVisible);
  }
}

void TestSettingsLabelSizeRoundTrip() {
  BeginCase("settings label size: live enum persists and rejects unknown sizes");
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  constexpr auto id = SettingId::kSettingsLabelSize;
  const auto* descriptor = service.DescriptorFor(id);
  CHECK_TRUE(descriptor != nullptr);
  if (!descriptor) return;
  CHECK_TRUE(descriptor->provider == Provider::kNative);
  CHECK_TRUE(descriptor->strategy == ApplicationStrategy::kHot);
  CHECK_TRUE(service.EffectiveValue(id).text == "big");
  CHECK_TRUE(CvarForSetting(id) == "settings_help_size");
  for (const auto* token : {"normal", "biggest", "big"}) {
    const auto txn = service.BeginEdit(service.revision());
    CHECK_FALSE(service.Set(txn, id, EnumV("huge"), service.capability_revision()).ok);
    CHECK_TRUE(service.Set(txn, id, EnumV(token), service.capability_revision()).ok);
    const auto result = service.Commit(txn);
    CHECK_TRUE(result.status == CommitStatus::kSaved);
    CHECK_FALSE(result.restart_required);
    Service reloaded(paths);
    reloaded.Boot(nullptr, PathEntry{}, nullptr);
    CHECK_TRUE(reloaded.EffectiveValue(id).text == token);
  }
}

void TestShowHeadersRoundTrip() {
  BeginCase("header defaults: Messages On, Recommendations Off; independent persistence and restart");
  Scratch scratch;
  const auto paths = Paths(scratch);
  constexpr auto messages = SettingId::kShowMessages;
  constexpr auto recommendations = SettingId::kShowRecommendations;
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  CHECK_TRUE(service.EffectiveValue(messages).boolean);
  CHECK_FALSE(service.EffectiveValue(recommendations).boolean);
  for (int mask = 0; mask < 4; ++mask) {
    const auto txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, messages, BoolV(mask & 1), service.capability_revision()).ok);
    CHECK_TRUE(service.Set(txn, recommendations, BoolV(mask & 2), service.capability_revision()).ok);
    const auto result = service.Commit(txn);
    CHECK_TRUE(result.status == CommitStatus::kSaved);
    CHECK_TRUE(result.restart_required);
    Service reloaded(paths);
    reloaded.Boot(nullptr, PathEntry{}, nullptr);
    reloaded.SetCapabilities(Capabilities());
    CHECK_TRUE(reloaded.EffectiveValue(messages).boolean == bool(mask & 1));
    CHECK_TRUE(reloaded.EffectiveValue(recommendations).boolean == bool(mask & 2));
  }
  BeginCase("legacy combined preference seeds only absent split keys and remains untouched");
  WriteFile(paths.settings_file, "schema_version = 1\n[interface]\nshow_messages_recommendations = false # keep\n");
  Service legacy(paths);
  legacy.Boot(nullptr, PathEntry{}, nullptr);
  legacy.SetCapabilities(Capabilities());
  CHECK_FALSE(legacy.EffectiveValue(messages).boolean);
  CHECK_FALSE(legacy.EffectiveValue(recommendations).boolean);
  const auto txn = legacy.BeginEdit(legacy.revision());
  CHECK_TRUE(legacy.Set(txn, messages, BoolV(true), legacy.capability_revision()).ok);
  CHECK_TRUE(legacy.Commit(txn).status == CommitStatus::kSaved);
  const auto loaded = LoadSettingsDocument(paths.settings_file);
  CHECK_TRUE(loaded.document.values.at(messages).boolean);
  CHECK_FALSE(loaded.document.values.at(recommendations).boolean);
  std::ifstream saved(paths.settings_file);
  const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
  CHECK_TRUE(text.find("show_messages_recommendations = false # keep") != std::string::npos);
  // A later edit of the old fallback is still another writer's change.
  Store writer(paths);
  writer.Load();
  WriteFile(paths.settings_file, "schema_version = 1\n[interface]\nshow_messages_recommendations = true\nshow_messages = true\n");
  writer.MarkChanged(recommendations, BoolV(true), Origin::kManual);
  CHECK_TRUE(writer.Save().status == CommitStatus::kConflict);
}

void TestMusicNavigationRoundTrip() {
  BeginCase("service: menu-music navigation is a default-on native restart setting and persists off");
  Scratch scratch;
  const auto paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  const auto id = SettingId::kBackgroundMusicNavigation;
  const auto* descriptor = service.DescriptorFor(id);
  CHECK_TRUE(descriptor);
  if (!descriptor) return;
  CHECK_EQ(static_cast<unsigned>(id), 53u);
  CHECK_TRUE(SettingIdToken(id) == "background_music_navigation");
  CHECK_TRUE(CvarForSetting(id) == "enhancements_background_music_nav");
  CHECK_TRUE(descriptor->category == Category::kInterface);
  CHECK_TRUE(descriptor->provider == Provider::kNative);
  CHECK_TRUE(descriptor->widget == WidgetKind::kBoolean);
  CHECK_TRUE(descriptor->strategy == ApplicationStrategy::kRestart);
  CHECK_TRUE(descriptor->compiled_default.boolean);
  CHECK_TRUE(service.EffectiveValue(id).boolean);
  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, id, BoolV(false), service.capability_revision()).ok);
  const auto committed = service.Commit(txn);
  CHECK_TRUE(committed.status == CommitStatus::kSaved);
  CHECK_TRUE(committed.restart_required);
  Service reloaded(paths);
  reloaded.Boot(nullptr, PathEntry{}, nullptr);
  CHECK_FALSE(reloaded.EffectiveValue(id).boolean);
  CHECK_TRUE(reloaded.EffectiveSource(id) == Source::kSettingsFile);
  const auto enabled = reloaded.BeginEdit(reloaded.revision());
  CHECK_TRUE(reloaded.Set(enabled, id, BoolV(true), reloaded.capability_revision()).ok);
  CHECK_TRUE(reloaded.Commit(enabled).status == CommitStatus::kSaved);
  Service again(paths);
  again.Boot(nullptr, PathEntry{}, nullptr);
  CHECK_TRUE(again.EffectiveValue(id).boolean);
}

void TestHideAchievementsRoundTrip() {
  BeginCase("service: achievement notifications default on and hiding them persists without Ultimate");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(CapabilitySnapshot{});
  const auto id = SettingId::kHideAchievements;
  const auto* descriptor = service.DescriptorFor(id);
  CHECK_TRUE(descriptor != nullptr);
  if (!descriptor) return;
  CHECK_TRUE(descriptor->availability == Availability::kVisible);
  CHECK_TRUE(descriptor->provider == Provider::kNative);
  CHECK_FALSE(service.EffectiveValue(id).boolean);
  CHECK_TRUE(CvarForSetting(id) == "enhancements_hide_achievements");

  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, id, BoolV(true), service.capability_revision()).ok);
  const auto result = service.Commit(txn);
  CHECK_TRUE(result.status == CommitStatus::kSaved);
  CHECK_TRUE(result.restart_required);
  {
    Service reloaded(paths);
    reloaded.Boot(nullptr, PathEntry{}, nullptr);
    CHECK_TRUE(reloaded.EffectiveValue(id).boolean);
    CHECK_TRUE(reloaded.EffectiveSource(id) == Source::kSettingsFile);
  }
  const auto off = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(off, id, BoolV(false), service.capability_revision()).ok);
  CHECK_TRUE(service.Commit(off).status == CommitStatus::kSaved);
  Service reloaded(paths);
  reloaded.Boot(nullptr, PathEntry{}, nullptr);
  CHECK_FALSE(reloaded.EffectiveValue(id).boolean);
}

void TestDurableEditReload() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);

  BeginCase("service: a fresh boot has no file and shows the compiled defaults");
  Service service(paths);
  CHECK_TRUE(service.Boot(nullptr, PathEntry{}, nullptr) == DocumentStatus::kMissing);
  service.SetCapabilities(Capabilities());
  Snapshot snapshot = service.GetSnapshot();
  CHECK_EQ(snapshot.revision, 0);
  const FieldState* vsync = nullptr;
  for (const FieldState& field : snapshot.fields) {
    if (field.id == SettingId::kVsync) {
      vsync = &field;
    }
  }
  CHECK_TRUE(vsync != nullptr);
  CHECK_TRUE(vsync->requested.boolean == true);
  CHECK_TRUE(vsync->effective.boolean == true);
  CHECK_TRUE(vsync->effective_source == Source::kCompiledDefault);
  CHECK_FALSE(vsync->overridden);

  BeginCase("service: a stale base revision is refused");
  CHECK_EQ(service.BeginEdit(99), 0);

  BeginCase("service: edit, commit and a durable read back");
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(txn != 0);
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  const CommitResult committed = service.Commit(txn);
  CHECK_TRUE(committed.status == CommitStatus::kSaved);
  CHECK_EQ(committed.saved_revision, 1);
  // P9: V-Sync is the one row with measured live evidence, so a commit of it alone does not ask
  // for a restart - the application seam applies it to the running game instead (§5.5).
  CHECK_FALSE(committed.restart_required);
  CHECK_EQ(service.revision(), 1);
  CHECK_EQ(service.last_commit_changes().size(), 1u);
  CHECK_EQ(service.last_commit_changes().count(SettingId::kVsync), 1u);

  Service reloaded(paths);
  reloaded.Boot(nullptr, PathEntry{}, nullptr);
  reloaded.SetCapabilities(Capabilities());
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kVsync).boolean == false);
  CHECK_TRUE(reloaded.EffectiveSource(SettingId::kVsync) == Source::kSettingsFile);

  BeginCase("service: a session with a restart row asks for the restart (P9)");
  // The hot rule is per row, not per session: one restart-strategy row in the same edit is what
  // makes the prompt appear, and the commit still reports exactly what it wrote.
  const uint64_t mixed = service.BeginEdit(service.revision());
  CHECK_TRUE(
      service.Set(mixed, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  // Stage *the other* value of the restart row: its compiled default is `true` (it took the setting
  // over from Controller Settings flag, P11b) and a commit that writes the value a row already holds is
  // `kNoChanges` - so staging `true` here would leave the session with nothing to save.
  const bool prompts_now = service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean;
  CHECK_TRUE(service.Set(mixed, SettingId::kHideOfflinePrompts, BoolV(!prompts_now),
                         service.capability_revision()).ok);
  const CommitResult mixed_commit = service.Commit(mixed);
  CHECK_TRUE(mixed_commit.status == CommitStatus::kSaved);
  CHECK_TRUE(mixed_commit.restart_required);
  CHECK_EQ(service.last_commit_changes().size(), 1u);  // unchanged VSync isn't written again

  BeginCase("service: a commit with nothing staged writes nothing");
  const uint64_t idle = reloaded.BeginEdit(reloaded.revision());
  const CommitResult no_changes = reloaded.Commit(idle);
  CHECK_TRUE(no_changes.status == CommitStatus::kNoChanges);
  CHECK_EQ(reloaded.revision(), 0);
}

void TestValidationAndPending() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());

  BeginCase("service: an unknown choice and a wrong kind are refused");
  const uint64_t txn = service.BeginEdit(service.revision());
  const ValidationResult bad_choice =
      service.Set(txn, SettingId::kFiltering, EnumV("3x"), service.capability_revision());
  CHECK_FALSE(bad_choice.ok);
  const ValidationResult bad_kind =
      service.Set(txn, SettingId::kVsync, EnumV("native"), service.capability_revision());
  CHECK_FALSE(bad_kind.ok);

  BeginCase("service: a stale capability revision is refused instead of clamped");
  const ValidationResult stale =
      service.Set(txn, SettingId::kInterfaceResolution, EnumV("native"), 77);
  CHECK_FALSE(stale.ok);
  CHECK_TRUE(stale.reason_key == "settings.capabilities_changed");

  BeginCase("service: pending is separate from requested and CancelField restores it");
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  const Snapshot pending_snapshot = service.GetSnapshot();
  for (const FieldState& field : pending_snapshot.fields) {
    if (field.id == SettingId::kVsync) {
      CHECK_TRUE(field.requested.boolean == true);
      CHECK_TRUE(field.pending.boolean == false);
    }
  }
  const Value restored = service.CancelField(txn, SettingId::kVsync);
  CHECK_TRUE(restored.boolean == true);
  service.Cancel(txn);

  BeginCase("service: Cancel discards the whole transaction");
  const uint64_t txn2 = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn2, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  service.Cancel(txn2);
  const CommitResult after_cancel = service.Commit(txn2);
  CHECK_TRUE(after_cancel.status == CommitStatus::kInvalid);

  BeginCase("service: capability changes invalidate staged values before disk I/O");
  const uint64_t txn3 = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn3, SettingId::kInterfaceResolution, EnumV("1280x720"),
                         service.capability_revision()).ok);
  CapabilitySnapshot changed = Capabilities(2);
  changed.primary_display_size = {1280, 720};
  service.SetCapabilities(std::move(changed));
  const CommitResult stale_commit = service.Commit(txn3);
  CHECK_TRUE(stale_commit.status == CommitStatus::kInvalid);
  CHECK_TRUE(!stale_commit.errors.empty() &&
             stale_commit.errors.front() == "settings.capabilities_changed");
  CHECK_EQ(service.revision(), 0u);
  CHECK_FALSE(fs::exists(paths.settings_file));
}

void TestOverridesAndCorrections() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  const ClampingReconciler reconciler;
  service.SetReconciler(&reconciler);

  BeginCase("service: an explicit override disables the row and shows the effective value");
  std::map<SettingId, Value> overrides;
  overrides[SettingId::kVsync] = BoolV(false);
  std::map<SettingId, Source> sources;
  sources[SettingId::kVsync] = Source::kCommandLine;
  service.SetOverrides(overrides, sources);
  const uint64_t txn = service.BeginEdit(service.revision());
  const ValidationResult refused =
      service.Set(txn, SettingId::kVsync, BoolV(true), service.capability_revision());
  CHECK_FALSE(refused.ok);
  CHECK_TRUE(refused.reason_key.find("overridden") != std::string::npos);
  const Snapshot snapshot = service.GetSnapshot();
  for (const FieldState& field : snapshot.fields) {
    if (field.id == SettingId::kVsync) {
      CHECK_TRUE(field.overridden);
      CHECK_TRUE(field.effective_source == Source::kCommandLine);
      CHECK_TRUE(field.effective.boolean == false);
    }
  }
  service.Cancel(txn);
}

void TestBootCorrection() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  // The stored value was valid when it was chosen (a 32-core machine) and is not a choice on
  // the machine booting now. The request is kept; the effective value is corrected.
  WriteFile(paths.settings_file,
            "schema_version = 1\n\n[general]\ndlc_scan_threads = 16\n");
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  const ClampingReconciler reconciler;
  service.SetReconciler(&reconciler);

  BeginCase("service: a now-invalid manual value is corrected while the request stays");
  bool found = false;
  for (const FieldState& field : service.GetSnapshot().fields) {
    if (field.id != SettingId::kDlcScanThreads) {
      continue;
    }
    found = true;
    CHECK_TRUE(field.requested.integer == 16);
    CHECK_TRUE(field.effective.integer == 4);
    CHECK_TRUE(field.corrected);
    CHECK_TRUE(field.correction_reason_key == "settings.corrected.processor_count");
  }
  CHECK_TRUE(found);
  // The correction is not written back: the manual request survives a temporary change.
  CHECK_TRUE(service.RequestedValue(SettingId::kDlcScanThreads).integer == 16);
}

void TestProviderSeam() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());

  BeginCase("service: provider rows are hidden without a payload and disabled for base");
  {
    CapabilitySnapshot caps = Capabilities();
    caps.provider.installed = false;
    Service bare(paths);
    bare.Boot(nullptr, PathEntry{}, nullptr);
    bare.SetCapabilities(caps);
    CHECK_TRUE(bare.DescriptorFor(SettingId::kHideHud)->availability == Availability::kHidden);
  }
  {
    CapabilitySnapshot caps = Capabilities();
    caps.provider.active = false;
    Service inactive(paths);
    inactive.Boot(nullptr, PathEntry{}, nullptr);
    inactive.SetCapabilities(caps);
    const Descriptor* row = inactive.DescriptorFor(SettingId::kHideHud);
    CHECK_TRUE(row->availability == Availability::kVisibleDisabled);
    CHECK_TRUE(row->disabled_reason_key == "settings.requires_ultimate");
    const uint64_t txn = inactive.BeginEdit(inactive.revision());
    CHECK_FALSE(inactive.Set(txn, SettingId::kHideHud, BoolV(true), inactive.capability_revision()).ok);
    inactive.Cancel(txn);
  }

  BeginCase("service: a provider commit without a sink is a partial save, never a fake success");
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), service.capability_revision()).ok);
    const CommitResult partial = service.Commit(txn);
    CHECK_TRUE(partial.status == CommitStatus::kPartial);
    CHECK_FALSE(partial.provider_written);
    CHECK_FALSE(partial.restart_required);
    CHECK_TRUE(service.last_commit_changes().empty());
  }

  BeginCase("service: a durable provider save is reported and a failed one is not");
  FakeProvider provider;
  service.SetProviderSink(&provider);
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), service.capability_revision()).ok);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kHideHud) == nullptr);
    const CommitResult saved = service.Commit(txn);
    CHECK_TRUE(saved.status == CommitStatus::kSaved);
    CHECK_TRUE(saved.provider_written);
    CHECK_EQ(provider.saves, 1);
    CHECK_TRUE(provider.values.empty());
    const Value* committed = service.CommittedProviderValue(SettingId::kHideHud);
    CHECK_TRUE(committed != nullptr && committed->boolean);
  }
  provider.durable = false;
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kShowTimer, BoolV(true), service.capability_revision()).ok);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kShowTimer) == nullptr);
    const CommitResult failed = service.Commit(txn);
    CHECK_TRUE(failed.status == CommitStatus::kPartial);
    CHECK_FALSE(failed.provider_written);
    CHECK_TRUE(service.last_commit_changes().empty());
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kShowTimer) == nullptr);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kHideHud)->boolean);
    provider.durable = true;
    const CommitResult retried = service.Commit(txn);
    CHECK_TRUE(retried.status == CommitStatus::kSaved && retried.provider_written);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kShowTimer)->boolean);
  }
  BeginCase("service: a later provider save preserves earlier committed runtime values");
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kGoldProgress, BoolV(true),
                           service.capability_revision()).ok);
    const CommitResult later = service.Commit(txn);
    CHECK_TRUE(later.status == CommitStatus::kSaved && later.provider_written);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kGoldProgress)->boolean);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kShowTimer)->boolean);
    CHECK_TRUE(service.CommittedProviderValue(SettingId::kHideHud)->boolean);
  }
  BeginCase("service: runtime provider read excludes native overrides and non-provider rows");
  service.SetOverrides({{SettingId::kHideHud, BoolV(false)}},
                       {{SettingId::kHideHud, Source::kCommandLine}});
  CHECK_FALSE(service.EffectiveValue(SettingId::kHideHud).boolean);
  CHECK_TRUE(service.CommittedProviderValue(SettingId::kHideHud)->boolean);
  CHECK_TRUE(service.CommittedProviderValue(SettingId::kVsync) == nullptr);
}

void TestCombinedCommitAndRestart() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  FakeProvider provider;
  service.SetProviderSink(&provider);

  BeginCase("service: a native+provider commit completes and clears its journal");
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), service.capability_revision()).ok);
  const CommitResult combined = service.Commit(txn);
  CHECK_TRUE(combined.status == CommitStatus::kSaved);
  CHECK_TRUE(combined.provider_written);
  CHECK_TRUE(combined.restart_required);
  CHECK_EQ(combined.saved_revision, 1);
  fs::path journal = paths.settings_file;
  journal += ".commit-journal";
  CHECK_FALSE(fs::exists(journal));
  CHECK_TRUE(LoadSettingsDocument(paths.settings_file).document.values.at(SettingId::kVsync).boolean ==
             false);

  BeginCase("service: the restart handshake refuses a stale or repeated request");
  CHECK_TRUE(service.RequestRestart(999, 1).status == RestartStatus::kStaleRevision);
  CHECK_TRUE(service.RequestRestart(service.revision(), 1).status == RestartStatus::kOk);
  CHECK_TRUE(service.RequestRestart(service.revision(), 2).status == RestartStatus::kAlreadyRequested);
}

void TestInterruptedCommitRecovery() {
  BeginCase("combined save: partial failure blocks restart and is recoverable after process exit");
  Scratch scratch;
  const auto paths = Paths(scratch);
  fs::path journal = paths.settings_file;
  journal += ".commit-journal";
  {
    Service service(paths);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    FakeProvider provider;
    provider.durable = false;
    service.SetProviderSink(&provider);
    const auto txn = service.BeginEdit(0);
    CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), 1).ok);
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), 1).ok);
    const auto result = service.Commit(txn);
    CHECK_TRUE(result.status == CommitStatus::kPartial);
    CHECK_EQ(service.last_commit_changes().size(), 1u);
    CHECK_EQ(service.last_commit_changes().count(SettingId::kVsync), 1u);
    CHECK_EQ(service.last_commit_changes().count(SettingId::kHideHud), 0u);
    CHECK_EQ(service.revision(), 0u);
    CHECK_FALSE(result.restart_required);
    CHECK_TRUE(fs::exists(journal));
    CHECK_FALSE(LoadSettingsDocument(paths.settings_file).document.values.at(SettingId::kVsync).boolean);
    CHECK_TRUE(service.RequestRestart(0, 1).status == RestartStatus::kNotSaved);
  }
  {
    Service service(paths);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    FakeProvider provider;
    service.SetProviderSink(&provider);
    CHECK_EQ(provider.saves, 1);
    CHECK_TRUE(service.RequestedValue(SettingId::kHideHud).boolean);
    CHECK_FALSE(fs::exists(journal));
    CHECK_EQ(service.revision(), 1u);
    const auto no_op = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(no_op, SettingId::kHideHud, BoolV(true), 1).ok);
    CHECK_TRUE(service.Commit(no_op).status == CommitStatus::kNoChanges);
    CHECK_EQ(provider.saves, 1);
  }

  BeginCase("Native profiles survive a durable save and reload as intent, without fixing a pixel size");
  Service service(paths);
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, SettingId::kInterfaceResolution, EnumV("1600x900"), 1).ok);
  CHECK_TRUE(service.Set(txn, SettingId::kGameplayResolution, EnumV("1600x900"), 1).ok);
  CHECK_TRUE(service.Commit(txn).status == CommitStatus::kSaved);
  const auto automatic = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(automatic, SettingId::kInterfaceResolution, EnumV("native"), 1).ok);
  CHECK_TRUE(service.Set(automatic, SettingId::kGameplayResolution, EnumV("native"), 1).ok);
  CHECK_TRUE(service.Commit(automatic).status == CommitStatus::kSaved);
  const auto saved = LoadSettingsDocument(paths.settings_file);
  for (const auto id : {SettingId::kInterfaceResolution, SettingId::kGameplayResolution}) {
    const auto found = saved.document.values.find(id);
    CHECK_TRUE(found != saved.document.values.end() && found->second.text == "native");
  }
  Service reloaded(paths);
  CHECK_TRUE(reloaded.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
  reloaded.SetCapabilities(Capabilities());
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kInterfaceResolution).text == "native");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kGameplayResolution).text == "native");
}

void TestUnfinishableCommitJournal() {
  BeginCase("combined save: a journal nobody can finish is set aside, and saving still works");
  Scratch scratch;
  const auto paths = Paths(scratch);
  fs::path journal = paths.settings_file;
  journal += ".commit-journal";
  {
    // An interrupted save: the settings file is written, the provider's half is not, so the journal
    // stays behind for the next process to finish.
    Service service(paths);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    FakeProvider provider;
    provider.durable = false;
    service.SetProviderSink(&provider);
    const auto txn = service.BeginEdit(0);
    CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), 1).ok);
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), 1).ok);
    CHECK_TRUE(service.Commit(txn).status == CommitStatus::kPartial);
    CHECK_TRUE(fs::exists(journal));
  }
  {
    // The next process cannot finish it either (the provider's file still cannot be written).
    Service service(paths);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    FakeProvider provider;
    provider.durable = false;
    service.SetProviderSink(&provider);
    // Reported, and deliberately not moved yet: a later run with a working provider may still be
    // able to finish it, so only a save that is actually blocked sets it aside.
    CHECK_FALSE(service.recovery_error().empty());
    CHECK_TRUE(fs::exists(journal));

    // Any row press is a native edit, and it has to save. This is the installed-copy dead end: it
    // used to answer `io error` here and the menu blamed the settings folder.
    const auto txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(true), 1).ok);
    const auto saved = service.Commit(txn);
    CHECK_TRUE(saved.status == CommitStatus::kSaved);
    CHECK_TRUE(LoadSettingsDocument(paths.settings_file)
                   .document.values.at(SettingId::kVsync)
                   .boolean);
    CHECK_FALSE(fs::exists(journal));
    fs::path parked = journal;
    parked += ".stale";
    // The bytes are kept, not deleted, and the reason is recorded where the boot can log it.
    CHECK_TRUE(fs::exists(parked));
    CHECK_TRUE(service.recovery_error().find(parked.filename().string()) != std::string::npos);
    CHECK_TRUE(service.recovery_error().find("disk full") != std::string::npos);

    // And the next save is an ordinary one: no journal, nothing to recover, nothing to explain.
    const auto again = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(again, SettingId::kVsync, BoolV(false), 1).ok);
    CHECK_TRUE(service.Commit(again).status == CommitStatus::kSaved);
  }
}

void TestCvarVocabulary() {
  BeginCase("cvars: the controller allowlist excludes Guide");
  CHECK_TRUE(IsControllerCvar("mouse_ui_nav"));
  CHECK_TRUE(IsControllerCvar("input_backend"));
  CHECK_FALSE(IsControllerCvar("guide_button"));
  CHECK_FALSE(IsControllerCvar("keybind_guide"));

  BeginCase("cvars: the allowlist keeps the controller rows and retires the rest");
  {
    // What a legacy Controller Settings profile carries: two controller rows, the retired Guide pair, and the
    // native rows that are about to become the game's own settings file's business (P8).
    rb_blitz::profile::ProfileSetting rows[6];
    rows[0].key = "mouse_ui_nav";
    rows[0].value = "true";
    rows[1].key = "input_backend";
    rows[1].value = "xinput";
    rows[2].key = "guide_button";
    rows[2].value = "true";
    rows[3].key = "keybind_guide";
    rows[3].value = "F1";
    rows[4].key = "user_data_root";
    rows[4].value = "D:\\Saves";
    rows[5].key = "dlc_root";
    rows[5].value = "D:\\DLC";
    const std::vector<rb_blitz::profile::ProfileSetting> all(
        std::begin(rows), std::end(rows));

    const std::vector<rb_blitz::profile::ProfileSetting> kept = ControllerProfileRows(all);
    CHECK_EQ(kept.size(), 2u);
    if (kept.size() == 2) {
      CHECK_TRUE(kept[0].key == "mouse_ui_nav" && kept[0].value == "true");
      CHECK_TRUE(kept[1].key == "input_backend" && kept[1].value == "xinput");
    }
    // Nothing to keep is not an error, and a table of only retired rows keeps nothing.
    CHECK_EQ(ControllerProfileRows({}).size(), 0u);
    const std::vector<rb_blitz::profile::ProfileSetting> retired(rows + 2, rows + 4);
    CHECK_EQ(ControllerProfileRows(retired).size(), 0u);
  }

  BeginCase("cvars: the profile rows the game applies keep the host rows too");
  {
    // P11b's cutover narrows the applied set to what the in-game tree cannot own: the controller
    // rows and the host-only host rows. A row the game has a settings row for is *not* applied
    // (that is what keeps the in-game copy editable), and neither is the retired Guide pair.
    rb_blitz::profile::ProfileSetting rows[9];
    rows[0].key = "mouse_ui_nav";
    rows[0].value = "false";
    rows[1].key = "audio_mute";
    rows[1].value = "true";
    rows[2].key = "monitor";
    rows[2].value = "2";
    rows[3].key = "present_safe_area_x";
    rows[3].value = "0.9";
    rows[4].key = "vsync";
    rows[4].value = "false";  // the game's own row now
    rows[5].key = "enhancements_dlc_cache";
    rows[5].value = "true";  // likewise
    rows[6].key = "present_letterbox";
    rows[6].value = "false";  // the Interface page's row, not this cvar
    rows[7].key = "user_data_root";
    rows[7].value = "D:\\Saves";  // a path row: installer/paths.toml, not the profile
    rows[8].key = "guide_button";
    rows[8].value = "true";

    const std::vector<rb_blitz::profile::ProfileSetting> all(std::begin(rows), std::end(rows));
    const std::vector<rb_blitz::profile::ProfileSetting> kept =
        ProfileRowsTheGameApplies(all);
    CHECK_EQ(kept.size(), 4u);
    if (kept.size() == 4) {
      CHECK_TRUE(kept[0].key == "mouse_ui_nav");
      CHECK_TRUE(kept[1].key == "audio_mute");
      CHECK_TRUE(kept[2].key == "monitor");
      CHECK_TRUE(kept[3].key == "present_safe_area_x" && kept[3].value == "0.9");
    }
    CHECK_TRUE(IsHostOnlyCvar("present_dither"));
    CHECK_TRUE(IsHostOnlyCvar("present_allow_overscan_cutoff"));
    CHECK_TRUE(IsHostOnlyCvar("audio_maxqframes"));
    CHECK_FALSE(IsHostOnlyCvar("mouse_ui_nav"));
    CHECK_FALSE(IsHostOnlyCvar("vsync"));
    CHECK_EQ(ProfileRowsTheGameApplies({}).size(), 0u);
  }

  BeginCase("cvars: the AA enum maps to the backend's two mechanisms");
  const std::vector<CvarAssignment> native = AntiAliasingToCvars("native");
  CHECK_EQ(native.size(), 2);
  CHECK_TRUE(native[0].name == "native_2x_msaa" && native[0].value == "true");
  CHECK_TRUE(native[1].name == "swap_post_effect" && native[1].value == "none");
  const std::vector<CvarAssignment> fxaa = AntiAliasingToCvars("fxaa");
  CHECK_TRUE(fxaa[0].value == "false" && fxaa[1].value == "fxaa");

  BeginCase("cvars: filtering never writes the label as the value");
  int32_t value = 0;
  CHECK_TRUE(FilteringToCvarValue("16x", &value));
  CHECK_EQ(value, 5);
  CHECK_TRUE(FilteringToCvarValue("native", &value));
  CHECK_EQ(value, -1);

  BeginCase("cvars: target translates to ultimate_mode and license_mask");
  LicensePolicy policy;
  policy.purchased_mask = 0xFF;
  policy.trial_mask = 0x01;
  const std::vector<CvarAssignment> ultimate =
      TargetToCvars(rb_blitz::profile::LaunchTarget::kUltimate, policy);
  CHECK_TRUE(ultimate[0].name == "ultimate_mode" && ultimate[0].value == "1");
  CHECK_TRUE(ultimate[1].name == "license_mask" && ultimate[1].value == "255");
  const std::vector<CvarAssignment> demo =
      TargetToCvars(rb_blitz::profile::LaunchTarget::kDemo, policy);
  CHECK_TRUE(demo[0].value == "0" && demo[1].value == "1");

  BeginCase("cvars: precedence ranks put explicit sources above the settings file");
  CHECK_TRUE(SourceRank(Source::kSettingsFile) > SourceRank(Source::kCompiledDefault));
  CHECK_TRUE(SourceRank(Source::kCommandLine) > SourceRank(Source::kSettingsFile));
  CHECK_TRUE(SourceRank(Source::kEnvironment) > SourceRank(Source::kDeveloperConfig));
}

void TestApplicationSnapshot() {
  BeginCase("snapshot: saved, running and uncommitted values have separate fields");
  Scratch scratch;
  Service service(Paths(scratch));
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false),
                         service.capability_revision()).ok);
  CHECK_TRUE(service.Commit(txn).status == CommitStatus::kSaved);
  ApplicationRecord record;
  record.saved = BoolV(false);
  record.running = BoolV(true);
  record.boundary = ApplyBoundary::kNow;
  record.phase = ApplyPhase::kFailed;
  record.generation = 7;
  record.reason = "write failed";
  service.SetApplicationRecord(SettingId::kVsync, record);
  bool found = false;
  for (const FieldState& field : service.GetSnapshot().fields) {
    if (field.id != SettingId::kVsync) continue;
    found = true;
    CHECK_TRUE(field.running.boolean);
    CHECK_FALSE(field.requested.boolean == field.running.boolean);
    CHECK_TRUE(field.application_phase == ApplyPhase::kFailed);
    CHECK_EQ(field.application_generation, 7u);
    CHECK_TRUE(field.restart_required);
  }
  CHECK_TRUE(found);
}

void TestIndependentBootRecoveryReasons() {
  BeginCase("service: graphics and display recovery keep independent effective values");
  Scratch scratch;
  // Model a release that permits backend selection; Vulkan-only platform rules intentionally
  // supply their own Renderer correction reason and would mask the recovery reason under test.
  Service service(Paths(scratch),
                  schema::SettingsForRelease(schema::HostPlatform::kWindows, "test"));
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  service.SetBootValues({{SettingId::kRenderer, EnumV("vulkan")}},
                        "settings.graphics.recovered_from_failure");
  service.SetBootValues({{SettingId::kDisplay, EnumV("windowed")}},
                        "settings.display.recovered_unconfirmed_preview");
  CHECK_TRUE(service.EffectiveValue(SettingId::kRenderer).text == "vulkan");
  CHECK_TRUE(service.EffectiveValue(SettingId::kDisplay).text == "windowed");
  for (const FieldState& field : service.GetSnapshot().fields) {
    if (field.id == SettingId::kRenderer)
      CHECK_TRUE(field.correction_reason_key == "settings.graphics.recovered_from_failure");
    if (field.id == SettingId::kDisplay)
      CHECK_TRUE(field.correction_reason_key ==
                 "settings.display.recovered_unconfirmed_preview");
  }
}

void TestProviderRestoreUsesInstalledDefaults() {
  BeginCase("restore defaults uses the installed provider's default values");
  Scratch scratch;
  Service service(Paths(scratch));
  service.Boot(nullptr, {}, nullptr);
  CapabilitySnapshot caps = Capabilities();
  caps.provider.active = false;  // Restore also covers an installed, inactive Ultimate target.
  caps.provider.compatible = false;
  service.SetCapabilities(caps);
  FakeProvider provider;
  provider.values[SettingId::kHideHud] = BoolV(true);
  service.SetProviderSink(&provider);
  service.SetProviderDefaults({{SettingId::kHideHud, BoolV(false)}});
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageDefaults(txn).ok);
  const CommitResult saved = service.Commit(txn);
  CHECK_TRUE(saved.status == CommitStatus::kSaved);
  CHECK_EQ(provider.saves, 1);
  CHECK_FALSE(service.CommittedProviderValue(SettingId::kHideHud)->boolean);
}

void TestRestoreUsesInstalledFirstRunTarget() {
  BeginCase("restore selects the installed Ultimate first-run target");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  FakeProvider provider;
  service.SetProviderSink(&provider);

  const uint64_t restore = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageDefaults(restore).ok);
  CHECK_TRUE(service.Commit(restore).status == CommitStatus::kSaved);
  CHECK_TRUE(service.RequestedValue(SettingId::kTarget).text == "ultimate");

  Service reloaded(paths);
  reloaded.Boot(nullptr, {}, nullptr);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kTarget).text == "ultimate");
}

void TestRestoreHidesFreshMenuRows() {
  BeginCase("restore brings back the fresh-install hidden main-menu rows");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  CHECK_TRUE(service.RequestedValue(SettingId::kHideMainMenu).boolean);
  CHECK_TRUE(service.RequestedValue(SettingId::kHiddenMainMenu).kind == ValueKind::kNone);

  const uint64_t edit = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(edit, SettingId::kHideMainMenu, BoolV(false),
                         service.capability_revision()).ok);
  CHECK_TRUE(service.Commit(edit).status == CommitStatus::kSaved);

  const uint64_t restore = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageDefaults(restore).ok);
  CHECK_TRUE(service.Commit(restore).status == CommitStatus::kSaved);
  CHECK_TRUE(service.RequestedValue(SettingId::kHideMainMenu).boolean);
  CHECK_FALSE(service.store().document().values.contains(SettingId::kHideMainMenu));

  Service reloaded(paths);
  reloaded.Boot(nullptr, {}, nullptr);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kHideMainMenu).boolean);
}

void TestRestoreRemovesRetiredHeaderFallback() {
  BeginCase("restore removes the retired combined header preference");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  WriteFile(paths.settings_file,
            "schema_version = 1\n[interface]\nshow_messages_recommendations = false\n");
  Service service(paths);
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  CHECK_FALSE(service.RequestedValue(SettingId::kShowMessages).boolean);

  const uint64_t restore = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageDefaults(restore).ok);
  CHECK_TRUE(service.Commit(restore).status == CommitStatus::kSaved);

  Service reloaded(paths);
  reloaded.Boot(nullptr, {}, nullptr);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kShowMessages).boolean);
  CHECK_FALSE(reloaded.RequestedValue(SettingId::kShowRecommendations).boolean);
}

void TestProviderRestoreUsesOfferedDefaultChoice() {
  BeginCase("restore chooses the provider's offered default when it declares no value");
  Scratch scratch;
  Service service(Paths(scratch));
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  FakeProvider provider;
  provider.values[SettingId::kForceSkybox] = EnumV("no_skybox");
  service.SetProviderChoices({{SettingId::kForceSkybox,
                               {{"provider_default_skybox", "Default"},
                                {"no_skybox", "None"}}}});
  service.SetProviderSink(&provider);
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageDefaults(txn).ok);
  const CommitResult saved = service.Commit(txn);
  CHECK_TRUE(saved.status == CommitStatus::kSaved);
  CHECK_EQ(provider.saves, 1);
  const Value* restored = service.CommittedProviderValue(SettingId::kForceSkybox);
  CHECK_TRUE(restored != nullptr && restored->text == "provider_default_skybox");
}

void TestRestoreWithInstalledProvider(const char* payload_root) {
  if (payload_root == nullptr) return;
  BeginCase("restore accepts every default from the installed provider");
  namespace adapter = rb_blitz::settings::ultimate_adapter;
  adapter::RecordBytes records;
  std::string error;
  CHECK_TRUE(adapter::ReadProviderRecords(payload_root, &records, &error));
  const adapter::ShapeReport shape = adapter::CheckShape(records);
  CHECK_TRUE(shape.ok);
  Scratch scratch;
  Service service(Paths(scratch));
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  std::map<SettingId, std::vector<Choice>> choices;
  FakeProvider provider;
  for (const auto& binding : adapter::RowBindings()) {
    const auto found = shape.choices.find(std::string(binding.variable));
    if (found == shape.choices.end()) continue;
    for (const std::string& token : found->second)
      choices[binding.id].push_back({token, token});
    if (found->second.size() > 1)
      provider.values[binding.id] = EnumV(found->second.back());
  }
  service.SetProviderChoices(std::move(choices));
  service.SetProviderDefaults(adapter::DefaultsFromShape(shape));
  service.SetProviderSink(&provider);
  const uint64_t txn = service.BeginEdit(service.revision());
  const ValidationResult staged = service.StageDefaults(txn);
  if (!staged.ok) std::printf("         installed default refused: %s\n", staged.reason_key.c_str());
  CHECK_TRUE(staged.ok);
  if (staged.ok) CHECK_TRUE(service.Commit(txn).status == CommitStatus::kSaved);
}

void TestRestoreClearsUnsupportedNativeDefaultWithProvider() {
  BeginCase("restore clears native keys even when a compiled default is unavailable");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, {}, nullptr);
  NoNativeAaReconciler reconciler;
  service.SetReconciler(&reconciler);
  service.SetCapabilities(Capabilities());
  const uint64_t edit = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(edit, SettingId::kAntiAliasing, EnumV("none"),
                         service.capability_revision()).ok);
  CHECK_TRUE(service.Commit(edit).status == CommitStatus::kSaved);
  CHECK_TRUE(service.store().document().values.contains(SettingId::kAntiAliasing));

  FakeProvider provider;
  provider.values[SettingId::kHideHud] = BoolV(true);
  service.SetProviderSink(&provider);
  service.SetProviderDefaults({{SettingId::kHideHud, BoolV(false)}});
  const uint64_t restore = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageDefaults(restore).ok);
  const CommitResult saved = service.Commit(restore);
  CHECK_TRUE(saved.status == CommitStatus::kSaved);
  CHECK_TRUE(saved.restart_required);
  CHECK_EQ(provider.saves, 1);
  CHECK_FALSE(service.store().document().values.contains(SettingId::kAntiAliasing));
  CHECK_FALSE(service.CommittedProviderValue(SettingId::kHideHud)->boolean);

  Service reloaded(paths);
  reloaded.Boot(nullptr, {}, nullptr);
  CHECK_FALSE(reloaded.store().document().values.contains(SettingId::kAntiAliasing));
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kAntiAliasing).text == "native");
}

void TestReleaseSettingsMigration() {
  BeginCase("release settings: supported choices survive, obsolete choices use the new default");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  WriteFile(paths.settings_file,
            "schema_version = 1\n[general]\nstartup = 'normal'\n[graphics]\nrenderer = 'd3d12'\n");
  schema::RowRule startup;
  startup.default_value = EnumV("fast");
  startup.choices = std::vector<Choice>{{"fast", "Fast"}};
  schema::RowRule hidden_load_guard;
  hidden_load_guard.hidden = true;
  const std::vector<schema::VersionRule> rules{
      {schema::HostPlatform::kWindows, "2.0.0", SettingId::kStartup, startup},
      {schema::HostPlatform::kWindows, "2.0.0", SettingId::kLoadGuard, hidden_load_guard}};
  auto older = schema::SettingsForRelease(schema::HostPlatform::kWindows, "1.0.0", rules);
  Service old_service(paths, older);
  CHECK_TRUE(old_service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
  CHECK_TRUE(old_service.EffectiveValue(SettingId::kStartup).text == "normal");
  CHECK_TRUE(old_service.EffectiveValue(SettingId::kRenderer).text == "d3d12");
  CHECK_TRUE(old_service.DescriptorFor(SettingId::kLoadGuard)->availability == Availability::kVisible);

  auto newer = schema::SettingsForRelease(schema::HostPlatform::kWindows, "2.0.0", rules);
  Service new_service(paths, newer);
  CHECK_TRUE(new_service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
  CHECK_TRUE(new_service.RequestedValue(SettingId::kStartup).text == "normal");
  CHECK_TRUE(new_service.EffectiveValue(SettingId::kStartup).text == "fast");
  const auto* changed = new_service.DescriptorFor(SettingId::kStartup);
  CHECK_TRUE(changed != nullptr && changed->choices.size() == 1 &&
             changed->compiled_default.text == "fast");
  CHECK_TRUE(new_service.DescriptorFor(SettingId::kLoadGuard)->availability == Availability::kHidden);
  const uint64_t restore = new_service.BeginEdit(new_service.revision());
  CHECK_TRUE(new_service.StageDefaults(restore).ok);
  CHECK_TRUE(new_service.Commit(restore).status == CommitStatus::kSaved);
  Service restored(paths, newer);
  CHECK_TRUE(restored.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
  CHECK_TRUE(restored.RequestedValue(SettingId::kStartup).text == "fast");

  WriteFile(paths.settings_file,
            "schema_version = 1\n[general]\nstartup = 'fast'\n[graphics]\nrenderer = 'd3d12'\n");
  Service supported(paths, newer);
  CHECK_TRUE(supported.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
  CHECK_TRUE(supported.EffectiveValue(SettingId::kStartup).text == "fast");
  CHECK_TRUE(supported.RequestedValue(SettingId::kStartup).text == "fast");
}

void TestVulkanOnlyReleaseSettings() {
  BeginCase("Vulkan-only releases: Renderer hidden and saved DirectX cannot replace Vulkan");
  for (const auto platform : {schema::HostPlatform::kLinux, schema::HostPlatform::kMacOS,
                              schema::HostPlatform::kIOS}) {
    Scratch scratch;
    const StorePaths paths = Paths(scratch);
    WriteFile(paths.settings_file, "schema_version = 1\n[graphics]\nrenderer = 'd3d12'\n");
    const auto release = schema::SettingsForRelease(platform, "1.0.0");
    Service service(paths, release);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    const auto* renderer = service.DescriptorFor(SettingId::kRenderer);
    CHECK_TRUE(renderer != nullptr && renderer->availability == Availability::kHidden);
    CHECK_TRUE(renderer != nullptr && renderer->compiled_default.text == "vulkan");
    CHECK_TRUE(renderer != nullptr && renderer->choices.size() == 1 &&
               renderer->choices.front().token == "vulkan");
    CHECK_TRUE(service.RequestedValue(SettingId::kRenderer).text == "d3d12");
    CHECK_TRUE(service.EffectiveValue(SettingId::kRenderer).text == "vulkan");
    service.SetOverrides({{SettingId::kRenderer, EnumV("d3d12")}},
                         {{SettingId::kRenderer, Source::kCommandLine}});
    CHECK_TRUE(service.EffectiveValue(SettingId::kRenderer).text == "vulkan");
    const uint64_t edit = service.BeginEdit(service.revision());
    CHECK_FALSE(service.Set(edit, SettingId::kRenderer, EnumV("d3d12"),
                            service.capability_revision()).ok);
    CHECK_TRUE(service.StageDefaults(edit).ok);
    CHECK_TRUE(service.Commit(edit).status == CommitStatus::kSaved);
    Service reloaded(paths, release);
    CHECK_TRUE(reloaded.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    CHECK_TRUE(reloaded.RequestedValue(SettingId::kRenderer).text == "vulkan");
  }
}

void TestIOSUltimateDefaultUpgrade() {
  BeginCase("iOS upgrades the old retail default once, then preserves later choices");
  Scratch scratch;
  const auto paths = Paths(scratch);
  const auto ios = schema::SettingsForRelease(schema::HostPlatform::kIOS, "test");
  WriteFile(paths.settings_file, "schema_version = 1\n[automatic]\npolicy_version = 4\n"
            "[general]\ntarget = 'common'\n");
  {
    Service service(paths, ios);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    CHECK_TRUE(UpgradeIOSLaunchDefault(service.store()));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "ultimate");
    CHECK_TRUE(service.store().Save().errors.empty());
    CHECK_FALSE(UpgradeIOSLaunchDefault(service.store()));
    Value retail;
    retail.kind = ValueKind::kEnum;
    retail.text = "common";
    service.store().MarkChanged(SettingId::kTarget, retail, Origin::kManual);
    CHECK_TRUE(service.store().Save().errors.empty());
  }
  {
    Service service(paths, ios);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    CHECK_FALSE(UpgradeIOSLaunchDefault(service.store()));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "common");
  }
  WriteFile(paths.settings_file, "schema_version = 1\n[automatic]\npolicy_version = 4\n"
            "[general]\ntarget = 'demo'\n");
  {
    Service service(paths, ios);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    CHECK_TRUE(UpgradeIOSLaunchDefault(service.store()));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "demo");
  }
  Scratch fresh;
  Service service(Paths(fresh), ios);
  CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kMissing);
  CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "ultimate");
}

void TestIOSImporterDefault() {
  BeginCase("iOS importer persists skip/import choice and preserves later settings");
  Scratch scratch;
  const auto paths = Paths(scratch);
  const auto ios = schema::SettingsForRelease(schema::HostPlatform::kIOS, "test");
  {
    Service service(paths, ios);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kMissing);
    CHECK_TRUE(ApplyIOSImportLaunchDefault(service.store(), false));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "common");
    CHECK_TRUE(service.store().Save().errors.empty());
  }
  {
    Service service(paths, ios);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    CHECK_FALSE(ApplyIOSImportLaunchDefault(service.store(), true));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "common");
    CHECK_TRUE(ApplyIOSImportLaunchDefault(service.store(), true, true));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "ultimate");
    CHECK_TRUE(service.store().Save().errors.empty());
  }
  WriteFile(paths.settings_file, "schema_version = 1\n[automatic]\npolicy_version = 5\n"
            "[general]\ntarget = 'ultimate'\n");
  {
    Service service(paths, ios);
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    CHECK_TRUE(ApplyIOSImportLaunchDefault(service.store(), false));
    CHECK_TRUE(service.EffectiveValue(SettingId::kTarget).text == "common");
  }
}

void TestMobilePreviewSettings() {
  BeginCase("desktop mobile preview matches mobile rows and keeps the native graphics backend");
  const auto mobile_rows = schema::BuildDescriptors(CapabilitySnapshot{},
      schema::SettingsForRelease(schema::HostPlatform::kIOS, "test"));
  for (const auto platform : {schema::HostPlatform::kWindows, schema::HostPlatform::kLinux,
                              schema::HostPlatform::kMacOS}) {
    Scratch scratch;
    const auto release = schema::SettingsForRelease(platform, "test", {}, true);
    CHECK_TRUE(release.platform == platform);
    CHECK_TRUE(schema::IsMobile(release));
    Service service(Paths(scratch), release);
    service.Boot(nullptr, {}, nullptr);
    for (const auto& row : mobile_rows) {
      const auto* preview = service.DescriptorFor(row.id);
      CHECK_TRUE(preview && preview->availability == row.availability);
    }
    CHECK_TRUE(service.EffectiveValue(SettingId::kInterfaceOrientation).text == "portrait");
    CHECK_TRUE(service.EffectiveValue(SettingId::kGameplayOrientation).text == "portrait");
    CHECK_FALSE(service.EffectiveValue(SettingId::kShowRunComparison).boolean);
    CHECK_TRUE(service.EffectiveValue(SettingId::kHideExitGame).boolean);
    for (const auto native : {DisplayMode{2560, 1080}, {3440, 1440}, {5120, 1440}}) {
      auto caps = Capabilities();
      caps.primary_display_size = native;
      service.SetCapabilities(caps);
      const auto basis = service.ResolutionProfileDisplay();
      CHECK_TRUE(std::abs(double(basis.width) / basis.height - 437.0 / 201) < .004);
      CHECK_TRUE(basis.width <= native.width && basis.height <= native.height);
      // A copied desktop preference or recovery value cannot restore ultrawide preview.
      service.SetBootValues({{SettingId::kForce16By9, BoolV(false)}}, "settings.graphics.recovery");
      CHECK_EQ(service.ResolutionProfileDisplay().width, basis.width);
      CHECK_EQ(service.ResolutionProfileDisplay().height, basis.height);
    }
    const auto desktop = schema::BuildDescriptors(CapabilitySnapshot{},
        schema::SettingsForRelease(platform, "test"));
    for (const auto& row : desktop)
      if (row.id == SettingId::kRenderer || row.id == SettingId::kTarget)
        CHECK_TRUE(ValueEquals(service.DescriptorFor(row.id)->compiled_default, row.compiled_default));
  }
  const auto desktop = schema::CurrentReleaseSettings();
  schema::SetMobilePreviewEnabled(true);
  CHECK_TRUE(schema::IsMobile(schema::CurrentReleaseSettings()));
  schema::SetMobilePreviewEnabled(false);
  CHECK_TRUE(schema::CurrentReleaseSettings().platform == desktop.platform);
  CHECK_FALSE(schema::CurrentReleaseSettings().mobile_preview);
}

void TestFirstBootMobileHudDefaults() {
  BeginCase("fresh mobile profiles persist a minimal HUD, while existing choices and desktop stay intact");
  const auto hud_toggles = {SettingId::kHideHud, SettingId::kGoldProgress, SettingId::kShowPb,
                            SettingId::kProgressBar, SettingId::kShowTimer, SettingId::kShowSteak,
                            SettingId::kInputViewer};
  for (const auto platform : {schema::HostPlatform::kIOS, schema::HostPlatform::kAndroid,
                              schema::HostPlatform::kWindows}) {
    for (const bool existing : {false, true}) {
      Scratch scratch;
      const auto paths = Paths(scratch);
      if (existing) WriteFile(paths.settings_file, "schema_version = 1\n");
      Service service(paths, schema::SettingsForRelease(platform, "test"));
      service.Boot(nullptr, {}, nullptr);
      service.SetCapabilities(Capabilities());
      FakeProvider provider;
      for (const auto id : hud_toggles)
        provider.values[id] = BoolV(true);
      service.SetProviderSink(&provider);
      const bool initialize = !existing && schema::IsMobile(platform);
      const auto applied = service.ApplyFirstBootMobileHudDefaults();
      CHECK_EQ(applied.provider_written, initialize);
      CHECK_EQ(provider.saves, initialize ? 1 : 0);
      for (const auto id : hud_toggles)
        CHECK_EQ(service.EffectiveValue(id).boolean, !initialize);
      if (!initialize) continue;
      const auto edit = service.BeginEdit(service.revision());
      CHECK_TRUE(service.Set(edit, SettingId::kShowTimer, BoolV(true), service.capability_revision()).ok);
      CHECK_TRUE(service.Commit(edit).status == CommitStatus::kSaved);
      CHECK_TRUE(service.ApplyFirstBootMobileHudDefaults().status == CommitStatus::kNoChanges);
      CHECK_TRUE(service.EffectiveValue(SettingId::kShowTimer).boolean);
    }
  }
  BeginCase("mobile HUD initialization respects explicit ownership and can retry a failed provider save");
  Scratch scratch;
  Service service(Paths(scratch), schema::SettingsForRelease(schema::HostPlatform::kIOS, "test"));
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  FakeProvider provider;
  provider.durable = false;
  provider.values = {{SettingId::kShowPb, BoolV(true)}, {SettingId::kProgressBar, BoolV(true)},
                     {SettingId::kShowTimer, BoolV(true)}};
  service.SetProviderSink(&provider);
  service.SetOverrides({{SettingId::kShowTimer, BoolV(true)}}, {{SettingId::kShowTimer, Source::kCommandLine}});
  CHECK_TRUE(service.ApplyFirstBootMobileHudDefaults().status == CommitStatus::kPartial);
  CHECK_TRUE(service.CommittedProviderValue(SettingId::kShowPb)->boolean);
  provider.durable = true;
  CHECK_TRUE(service.ApplyFirstBootMobileHudDefaults().status == CommitStatus::kSaved);
  CHECK_FALSE(service.CommittedProviderValue(SettingId::kShowPb)->boolean);
  CHECK_FALSE(service.CommittedProviderValue(SettingId::kProgressBar)->boolean);
  CHECK_TRUE(service.CommittedProviderValue(SettingId::kShowTimer)->boolean);

  BeginCase("native run comparison defaults to hidden on mobile without requiring Ultimate");
  for (const auto platform : {schema::HostPlatform::kIOS, schema::HostPlatform::kAndroid,
                              schema::HostPlatform::kWindows, schema::HostPlatform::kLinux,
                              schema::HostPlatform::kMacOS}) {
    Scratch native_scratch;
    const auto paths = Paths(native_scratch);
    const auto release = schema::SettingsForRelease(platform, "test");
    Service native(paths, release);
    native.Boot(nullptr, {}, nullptr);
    auto caps = Capabilities();
    caps.provider = {};
    native.SetCapabilities(caps);
    CHECK_EQ(native.EffectiveValue(SettingId::kShowRunComparison).boolean, !schema::IsMobile(platform));
    const auto* row = native.DescriptorFor(SettingId::kShowRunComparison);
    CHECK_TRUE(row && row->provider == Provider::kNative && row->availability == Availability::kVisible);
    const auto edit = native.BeginEdit(native.revision());
    CHECK_TRUE(native.Set(edit, SettingId::kShowRunComparison, BoolV(schema::IsMobile(platform)),
                          native.capability_revision()).ok);
    CHECK_TRUE(native.Commit(edit).status == CommitStatus::kSaved);
    Service reloaded(paths, release);
    reloaded.Boot(nullptr, {}, nullptr);
    reloaded.SetCapabilities(caps);
    CHECK_EQ(reloaded.EffectiveValue(SettingId::kShowRunComparison).boolean, schema::IsMobile(platform));
  }
}

void TestIOSReleaseSettings() {
  BeginCase("iOS: hidden desktop display preferences cannot replace the device baseline");
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  WriteFile(paths.settings_file,
            "schema_version = 1\n[graphics]\ndisplay = 'windowed'\n"
            "resolution = [3840, 2160]\naspect_ratio = '32:9'\nrender_scale = '3x'\n"
            "vsync = false\n[controller]\nscheme = 'retail'\n");
  Service service(paths, schema::SettingsForRelease(schema::HostPlatform::kIOS, "test"));
  CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
  CHECK_TRUE(service.RequestedValue(SettingId::kDisplay).text == "windowed");
  CHECK_TRUE(service.EffectiveValue(SettingId::kDisplay).text == "fullscreen");
  CHECK_TRUE(service.EffectiveValue(SettingId::kAspectRatio).text == "auto");
  CHECK_TRUE(service.EffectiveValue(SettingId::kRenderScale).text == "1x");
  // The boot's device planner replaces the stored desktop pixel size.
  Value device_pixels;
  device_pixels.kind = ValueKind::kResolution;
  device_pixels.width = 1568;
  device_pixels.height = 720;
  service.SetBootValues({{SettingId::kResolution, device_pixels}}, "settings.graphics.aspect_scale");
  CHECK_TRUE(service.EffectiveValue(SettingId::kResolution).width == 1568);
  CHECK_TRUE(service.EffectiveValue(SettingId::kResolution).height == 720);
  CHECK_TRUE(service.EffectiveValue(SettingId::kVsync).boolean);
  const uint64_t edit = service.BeginEdit(service.revision());
  for (const auto id : {SettingId::kDisplay, SettingId::kResolution, SettingId::kAspectRatio,
                        SettingId::kRenderScale, SettingId::kVsync, SettingId::kRenderer}) {
    const auto* row = service.DescriptorFor(id);
    CHECK_TRUE(row != nullptr && row->availability == Availability::kHidden);
    if (row == nullptr) continue;
    CHECK_FALSE(service.Set(edit, id, row->compiled_default, service.capability_revision()).ok);
    if (id == SettingId::kResolution) continue;  // internal, device-derived size
    service.SetOverrides({{id, service.RequestedValue(id)}}, {{id, Source::kCommandLine}});
    CHECK_TRUE(ValueEquals(service.EffectiveValue(id), row->compiled_default));
  }
  for (const auto id : {SettingId::kAntiAliasing, SettingId::kFiltering, SettingId::kButtonScheme}) {
    const auto* row = service.DescriptorFor(id);
    CHECK_TRUE(row != nullptr && row->availability == Availability::kVisible);
  }
}

void TestOrientationProfiles() {
  BeginCase("orientation profiles persist mixed choices and restore platform defaults");
  for (const auto platform : {schema::HostPlatform::kWindows, schema::HostPlatform::kLinux,
                              schema::HostPlatform::kMacOS, schema::HostPlatform::kIOS,
                              schema::HostPlatform::kAndroid}) {
    Scratch scratch;
    const auto paths = Paths(scratch);
    const auto release = schema::SettingsForRelease(platform, "test");
    const auto expected = schema::IsMobile(platform) ? "portrait" : "landscape";
    Service service(paths, release);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    CHECK_TRUE(service.RequestedValue(SettingId::kInterfaceOrientation).text == expected);
    CHECK_TRUE(service.RequestedValue(SettingId::kGameplayOrientation).text == expected);
    for (const bool portrait_interface : {false, true}) {
      const auto txn = service.BeginEdit(service.revision());
      const auto interface_value = EnumV(portrait_interface ? "portrait" : "landscape");
      const auto gameplay_value = EnumV(portrait_interface ? "landscape" : "portrait");
      CHECK_TRUE(service.Set(txn, SettingId::kInterfaceOrientation, interface_value,
                             service.capability_revision()).ok);
      CHECK_TRUE(service.Set(txn, SettingId::kGameplayOrientation, gameplay_value,
                             service.capability_revision()).ok);
      const auto saved = service.Commit(txn);
      CHECK_TRUE(saved.status == CommitStatus::kSaved);
      CHECK_TRUE(saved.restart_required);
      Service reloaded(paths, release);
      reloaded.Boot(nullptr, {}, nullptr);
      reloaded.SetCapabilities(Capabilities());
      CHECK_TRUE(ValueEquals(reloaded.EffectiveValue(SettingId::kInterfaceOrientation), interface_value));
      CHECK_TRUE(ValueEquals(reloaded.EffectiveValue(SettingId::kGameplayOrientation), gameplay_value));
    }
    const auto restore = service.BeginEdit(service.revision());
    CHECK_TRUE(service.StageRestore(restore, Service::RestoreProfile::kRecommended).ok);
    CHECK_TRUE(service.Commit(restore).status == CommitStatus::kSaved);
    Service restored(paths, release);
    restored.Boot(nullptr, {}, nullptr);
    CHECK_TRUE(restored.RequestedValue(SettingId::kInterfaceOrientation).text == expected);
    CHECK_TRUE(restored.RequestedValue(SettingId::kGameplayOrientation).text == expected);
  }
}

void TestRestoreProfilesRoundTrip() {
  BeginCase("Vanilla presentation persists, then Recommended restores all first-boot preferences");
  Scratch scratch;
  const auto paths = Paths(scratch);
  const auto release = schema::SettingsForRelease(schema::HostPlatform::kWindows, "test");
  Service service(paths, release);
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  FakeProvider provider;
  service.SetProviderSink(&provider);
  service.SetProviderDefaults({{SettingId::kHideHud, BoolV(false)}});
  // The retail files are outside the settings transaction and must remain byte-identical.
  const auto native_save = scratch.base / "local_save.bin";
  const auto console_save = scratch.base / "globaloptions";
  WriteFile(native_save, "scores, stars, achievements and controller preset");
  WriteFile(console_save, "calibration, audio and overscan");
  const auto vanilla = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageRestore(vanilla, Service::RestoreProfile::kVanilla).ok);
  CHECK_TRUE(service.RequestedValue(SettingId::kRhythmVerse).boolean); // staged, not saved
  service.Cancel(vanilla);
  CHECK_TRUE(service.RequestedValue(SettingId::kRhythmVerse).boolean);
  const auto apply = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageRestore(apply, Service::RestoreProfile::kVanilla).ok);
  CHECK_TRUE(service.Commit(apply).status == CommitStatus::kSaved);
  Service reloaded(paths, release);
  reloaded.Boot(nullptr, {}, nullptr);
  reloaded.SetCapabilities(Capabilities());
  for (const auto id : {SettingId::kRhythmVerse, SettingId::kDlcMenuSongs,
      SettingId::kMusicPlayer, SettingId::kBackgroundMusicNavigation, SettingId::kPlayCurrentSong,
      SettingId::kRefreshMusicLibrary, SettingId::kShowMessages, SettingId::kShowRecommendations,
      SettingId::kSettingsLabels, SettingId::kHideOfflinePrompts,
      SettingId::kHideAchievements, SettingId::kHideDownloadContent}) {
    CHECK_FALSE(reloaded.RequestedValue(id).boolean);
  }
  CHECK_FALSE(reloaded.RequestedValue(SettingId::kHideExitGame).boolean);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kShowCurrency).boolean);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kVsync).boolean);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kTarget).text == "common");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kStartup).text == "normal");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kDisplay).text == "fullscreen");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kOrientation).text == "landscape");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kInterfaceOrientation).text == "landscape");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kGameplayOrientation).text == "landscape");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kInterfaceResolution).text == "1280x720");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kGameplayResolution).text == "1280x720");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kAntiAliasing).text == "native");
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kFiltering).text == "native");
  const auto hidden = reloaded.RequestedValue(SettingId::kHiddenMainMenu).list;
  CHECK_TRUE(hidden == std::vector<std::string>({"rbbz_rhythmverse"}));

  const auto repeat = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageRestore(repeat, Service::RestoreProfile::kVanilla).ok);
  CHECK_TRUE(service.Commit(repeat).status == CommitStatus::kNoChanges);
  CHECK_FALSE(service.RequestedValue(SettingId::kRhythmVerse).boolean);
  CHECK_TRUE(service.RequestedValue(SettingId::kInterfaceResolution).text == "1280x720");

  const auto recommended = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageRestore(recommended, Service::RestoreProfile::kRecommended).ok);
  CHECK_TRUE(service.Commit(recommended).status == CommitStatus::kSaved);
  // A missing settings file is the native default source; installed Ultimate supplies its
  // own provider defaults and selects the same first-run target as the launcher policy.
  for (const auto& descriptor : service.descriptors()) {
    if (descriptor.widget == WidgetKind::kAction || descriptor.provider == Provider::kUltimate) continue;
    const auto actual = service.RequestedValue(descriptor.id);
    CHECK_TRUE(descriptor.id == SettingId::kTarget ? actual.text == "ultimate"
        : ValueEquals(actual, descriptor.compiled_default));
  }
  std::ifstream player(native_save, std::ios::binary), console(console_save, std::ios::binary);
  CHECK_TRUE(std::string(std::istreambuf_iterator<char>(player), {}) == "scores, stars, achievements and controller preset");
  CHECK_TRUE(std::string(std::istreambuf_iterator<char>(console), {}) == "calibration, audio and overscan");
}

void TestVanillaRespectsOverridesAndRelease() {
  BeginCase("Vanilla honors external ownership and mobile display constraints; Exit is editable everywhere");
  for (const auto platform : {schema::HostPlatform::kWindows, schema::HostPlatform::kLinux,
      schema::HostPlatform::kMacOS, schema::HostPlatform::kIOS, schema::HostPlatform::kAndroid}) {
    Scratch scratch;
    Service service(Paths(scratch), schema::SettingsForRelease(platform, "test"));
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    CHECK_TRUE(service.DescriptorFor(SettingId::kHideExitGame)->availability == Availability::kVisible);
    service.SetOverrides({{SettingId::kSettingsLabels, BoolV(true)}}, {{SettingId::kSettingsLabels, Source::kCommandLine}});
    const auto txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.StageRestore(txn, Service::RestoreProfile::kVanilla).ok);
    CHECK_TRUE(service.Commit(txn).status == CommitStatus::kSaved);
    CHECK_TRUE(service.EffectiveValue(SettingId::kSettingsLabels).boolean);
    CHECK_FALSE(service.store().document().values.contains(SettingId::kSettingsLabels));
    if (platform == schema::HostPlatform::kIOS) {
      CHECK_TRUE(service.EffectiveValue(SettingId::kAspectRatio).text == "auto");
      CHECK_TRUE(service.EffectiveValue(SettingId::kRenderScale).text == "1x");
    }
  }
}

void TestVanillaResolutionProfilesAfterRestart() {
  BeginCase("Vanilla restores the 720p quality tier across a desktop aspect restart");
  Scratch scratch;
  const auto paths = Paths(scratch);
  WriteFile(paths.settings_file, "schema_version = 1\n[graphics]\nforce_16_by_9 = false\n"
      "interface_resolution = 'native'\ngameplay_resolution = '2133x900'\n");
  const auto release = schema::SettingsForRelease(schema::HostPlatform::kWindows, "test");
  Service service(paths, release);
  service.Boot(nullptr, {}, nullptr);
  auto caps = Capabilities();
  caps.primary_display_size = {2560, 1080};
  service.SetCapabilities(caps);
  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.StageRestore(txn, Service::RestoreProfile::kVanilla).ok);
  const auto saved = service.Commit(txn);
  CHECK_TRUE(saved.status == CommitStatus::kSaved);
  CHECK_TRUE(saved.restart_required);
  CHECK_TRUE(service.RequestedValue(SettingId::kForce16By9).boolean);
  CHECK_EQ(service.ResolutionProfileDisplay().width, 2560);
  for (const auto id : {SettingId::kInterfaceResolution, SettingId::kGameplayResolution}) {
    CHECK_TRUE(service.RequestedValue(id).text == "1707x720");
    CHECK_TRUE(service.EffectiveValue(id).text == "1707x720");
  }

  Service reloaded(paths, release);
  reloaded.Boot(nullptr, {}, nullptr);
  reloaded.SetCapabilities(caps);
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kForce16By9).boolean);
  CHECK_EQ(reloaded.ResolutionProfileDisplay().width, 1920);
  for (const auto id : {SettingId::kInterfaceResolution, SettingId::kGameplayResolution}) {
    CHECK_TRUE(reloaded.RequestedValue(id).text == "1707x720");
    CHECK_TRUE(reloaded.EffectiveValue(id).text == "1280x720");
  }
}

void TestResolutionProfileAspectBootPolicy() {
  BeginCase("resolution profiles retain saved quality tiers under the boot aspect policy");
  for (const bool force : {false, true}) {
    Scratch scratch;
    const auto paths = Paths(scratch);
    WriteFile(paths.settings_file, std::string("schema_version = 1\n[graphics]\nforce_16_by_9 = ") +
        (force ? "true" : "false") +
        "\ninterface_resolution = '1280x720'\ngameplay_resolution = '1600x900'\n");
    Service service(paths, schema::SettingsForRelease(schema::HostPlatform::kWindows, "test"));
    CHECK_TRUE(service.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    auto caps = Capabilities();
    caps.primary_display_size = {2560, 1080};
    service.SetCapabilities(caps);
    const auto tokens = [](const Service& state) {
      std::vector<std::string> result;
      for (const auto& choice : state.DescriptorFor(SettingId::kInterfaceResolution)->choices)
        result.push_back(choice.token);
      return result;
    };
    const auto expected = force ? std::vector<std::string>{"native", "1600x900", "1280x720"}
                                : std::vector<std::string>{"native", "2133x900", "1707x720"};
    CHECK_TRUE(tokens(service) == expected);
    CHECK_EQ(service.ResolutionProfileDisplay().width, force ? 1920 : 2560);
    CHECK_EQ(service.ResolutionProfileDisplay().height, 1080);
    CHECK_TRUE(service.RequestedValue(SettingId::kInterfaceResolution).text == "1280x720");
    CHECK_TRUE(service.RequestedValue(SettingId::kGameplayResolution).text == "1600x900");
    CHECK_TRUE(service.EffectiveValue(SettingId::kInterfaceResolution).text ==
               (force ? "1280x720" : "1707x720"));
    CHECK_TRUE(service.EffectiveValue(SettingId::kGameplayResolution).text ==
               (force ? "1600x900" : "2133x900"));
    const ClampingReconciler reconciler;
    service.SetReconciler(&reconciler);
    CHECK_TRUE(service.EffectiveValue(SettingId::kInterfaceResolution).text ==
               (force ? "1280x720" : "1707x720"));
    for (const auto& field : service.GetSnapshot().fields) {
      if (field.id != SettingId::kInterfaceResolution && field.id != SettingId::kGameplayResolution)
        continue;
      CHECK_EQ(field.corrected, !force);
      if (!force) CHECK_TRUE(field.correction_reason_key == "settings.graphics.for_display");
    }

    BeginCase("Force 16:9 saves a restart change without replacing the active profile choices");
    const auto txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kForce16By9, BoolV(!force),
                           service.capability_revision()).ok);
    const auto saved = service.Commit(txn);
    CHECK_TRUE(saved.status == CommitStatus::kSaved);
    CHECK_TRUE(saved.restart_required);
    ++caps.revision;
    service.SetCapabilities(caps);
    CHECK_TRUE(tokens(service) == expected);
    CHECK_EQ(service.ResolutionProfileDisplay().width, force ? 1920 : 2560);
    CHECK_TRUE(service.store().document().values.at(SettingId::kInterfaceResolution).text == "1280x720");
    CHECK_TRUE(service.store().document().values.at(SettingId::kGameplayResolution).text == "1600x900");

    Service reloaded(paths, schema::SettingsForRelease(schema::HostPlatform::kWindows, "test"));
    CHECK_TRUE(reloaded.Boot(nullptr, {}, nullptr) == DocumentStatus::kOk);
    reloaded.SetCapabilities(caps);
    CHECK_TRUE(reloaded.RequestedValue(SettingId::kForce16By9).boolean == !force);
    CHECK_EQ(reloaded.ResolutionProfileDisplay().width, force ? 2560 : 1920);
    CHECK_TRUE(reloaded.EffectiveValue(SettingId::kInterfaceResolution).text ==
               (force ? "1707x720" : "1280x720"));
    CHECK_TRUE(reloaded.EffectiveValue(SettingId::kGameplayResolution).text ==
               (force ? "2133x900" : "1600x900"));
  }

  BeginCase("mobile profiles ignore a saved desktop aspect preference");
  for (const auto platform : {schema::HostPlatform::kIOS, schema::HostPlatform::kAndroid}) {
    Scratch scratch;
    const auto paths = Paths(scratch);
    WriteFile(paths.settings_file, "schema_version = 1\n[graphics]\nforce_16_by_9 = true\n");
    Service service(paths, schema::SettingsForRelease(platform, "test"));
    service.Boot(nullptr, {}, nullptr);
    auto caps = Capabilities();
    caps.primary_display_size = {2622, 1206};
    service.SetCapabilities(caps);
    CHECK_EQ(service.ResolutionProfileDisplay().width, 2622);
    CHECK_EQ(service.ResolutionProfileDisplay().height, 1206);
    CHECK_TRUE(service.DescriptorFor(SettingId::kForce16By9)->availability == Availability::kHidden);
  }
}

}  // namespace

int main(int argc, char** argv) {
  TestVanillaResolutionProfilesAfterRestart();
  TestResolutionProfileAspectBootPolicy();
  TestOrientationProfiles();
  TestRestoreProfilesRoundTrip();
  TestVanillaRespectsOverridesAndRelease();
  TestControllerSchemeRoundTrip();
  TestCommitRevalidatesCurrentCatalogAndOverride();
  TestStartupRoundTrip();
  TestRefreshSongsDefaultAndOverride();
  TestLoadGuardRoundTrip();
  TestSettingsLabelSizeRoundTrip();
  TestTouchButtonsRoundTrip();
  TestMotionDeployRoundTrip();
  TestMobileDefaultsAndLaneSwipe();
  TestHideAchievementsRoundTrip();
  TestMusicNavigationRoundTrip();
  TestShowHeadersRoundTrip();
  TestDurableEditReload();
  TestValidationAndPending();
  TestOverridesAndCorrections();
  TestBootCorrection();
  TestProviderSeam();
  TestCombinedCommitAndRestart();
  TestInterruptedCommitRecovery();
  TestUnfinishableCommitJournal();
  TestCvarVocabulary();
  TestApplicationSnapshot();
  TestIndependentBootRecoveryReasons();
  TestProviderRestoreUsesInstalledDefaults();
  TestRestoreUsesInstalledFirstRunTarget();
  TestRestoreHidesFreshMenuRows();
  TestRestoreRemovesRetiredHeaderFallback();
  TestProviderRestoreUsesOfferedDefaultChoice();
  TestRestoreWithInstalledProvider(argc > 1 ? argv[1] : nullptr);
  TestRestoreClearsUnsupportedNativeDefaultWithProvider();
  TestReleaseSettingsMigration();
  TestVulkanOnlyReleaseSettings();
  TestIOSReleaseSettings();
  TestFirstBootMobileHudDefaults();
  TestMobilePreviewSettings();
  TestIOSUltimateDefaultUpgrade();
  TestIOSImporterDefault();
  return Finish();
}
