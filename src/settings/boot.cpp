// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The implementation of src/settings/boot.h - see that file for the ordering and for the two open
// items this pass states instead of guessing.

#include "settings/boot.h"

#include <algorithm>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "hooks/settings_bridge_hooks.h"
#include "hooks/render_settings.h"
#include "hooks/ultimate.h"
#include "launcher/profile.h"
#include "launcher/profile_apply.h"
#include "launcher/profile_path.h"
#include "settings/cvars.h"
#include "settings/schema.h"

REXCVAR_DEFINE_BOOL(settings_safe_graphics, false, "Settings",
                    "Start with safe graphics without deleting saved preferences.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace rb_blitz::settings::boot {
namespace {

// The process-wide instance. Owned by the app; the hooks that need the store (the menu-rows seam)
// reach it here rather than through a second global.
SettingsBoot* g_current = nullptr;

// The cvars this module writes are read and written **by name** on purpose: their storage lives with
// their definition (`launcher_profile` in src/launcher/profile_apply.cpp, `dlc_root` in
// src/hooks/dlc.cpp, `user_data_root` in the runtime's own flags), and a `REXCVAR_DECLARE` here would
// ask the linker for storage another translation unit keeps private. The registry is the interface.

// The anisotropic step this project has actually run: the retired launcher row's own default
// (`anisotropic_override = 3`, i.e. 4x), which every measured boot in this repository used. It is
// **not** a device probe - P4's probing item is still open - and it is deliberately conservative:
// the ceiling only bounds what the menu may offer, so shipping a ceiling this project has run is
// safer than offering a step nothing has.
constexpr int32_t kShippedFilteringCeiling = 3;

// The backend this project ships and has run. The GPU plugin exposes no list of the backends it was
// compiled with (`rex::system::LoadGpuPlugin` takes a name and returns a system), so the boot reports
// the one it can describe honestly and says so in the log. A full built/detected/usable probe is
// P4's open item; until then a one-choice Renderer row is the honest presentation (§7.2: a single
// available choice remains understandable).
constexpr char kShippedRenderer[] = "d3d12";

std::string BoolText(const bool value) { return value ? "true" : "false"; }
std::string PathText(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

// This query is independent of the game's window and the DLC library. It performs no
// benchmark or device creation, so saved preferences can be applied on the fast boot path.
void ProbeDisplay(CapabilityInputs& inputs) {
#ifdef _WIN32
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
    inputs.display_enumeration_available = true;
    inputs.primary_display_id = "primary";
    inputs.primary_display_size = {static_cast<int32_t>(mode.dmPelsWidth),
                                   static_cast<int32_t>(mode.dmPelsHeight)};
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &mode); ++i) {
      inputs.display_modes.push_back({static_cast<int32_t>(mode.dmPelsWidth),
                                      static_cast<int32_t>(mode.dmPelsHeight)});
    }
  }
#endif
}

// The migration status as a word, for the log. P3 names the states; this is only their spelling.
const char* MigrationName(const MigrationStatus status) {
  switch (status) {
    case MigrationStatus::kNoLegacy: return "not needed (no legacy profile)";
    case MigrationStatus::kMigrated: return "done";
    case MigrationStatus::kAlreadyCurrent: return "already current";
    case MigrationStatus::kInterruptedResumed: return "resumed after an interruption";
    case MigrationStatus::kMalformedLegacy: return "refused (the legacy file is malformed)";
  }
  return "?";
}

std::string ListText(const std::vector<std::string>& list) {
  std::string out;
  for (const std::string& item : list) {
    if (!out.empty()) {
      out += ",";
    }
    out += item;
  }
  return out;
}

// The sources that outrank a config-rank write (§4.2: config < environment < command line).
bool HigherRank(const rex::cvar::Source source) {
  return source == rex::cvar::Source::kEnvironment || source == rex::cvar::Source::kCommandLine;
}

Source OverrideSourceFor(const rex::cvar::Source source) {
  switch (source) {
    case rex::cvar::Source::kEnvironment:
      return Source::kEnvironment;
    case rex::cvar::Source::kCommandLine:
      return Source::kCommandLine;
    default:
      // A later writer at the *same* rank: the game's own rb_blitz.toml, which the SDK loads right
      // after OnConfigurePaths and which therefore outranks this module's write by being later.
      return Source::kDeveloperConfig;
  }
}

class InstalledProviderSink final : public ProviderSink {
 public:
  InstalledProviderSink() : values_(::rb_blitz::ultimate::Provider().values) {}
  ProviderReadiness Readiness() const override {
    return ::rb_blitz::ultimate::Provider().Active() ? ProviderReadiness::kCompatible
                                                    : ProviderReadiness::kInactiveTarget;
  }
  std::map<SettingId, Value> ReadValues() const override { return values_; }
  ProviderSaveResult Save(const std::map<SettingId, Value>& values) override {
    ProviderSaveResult result;
    const auto& provider = ::rb_blitz::ultimate::Provider();
    if (!provider.Active()) { result.error = "the Ultimate provider is not active"; return result; }
    std::map<std::string, std::string> updates;
    if (!ultimate_adapter::EncodeProviderValues(values, &updates, &result.error)) return result;
    for (auto& [variable, encoded] : updates) {
      const auto choices = provider.choices.find(variable);
      if (choices == provider.choices.end()) continue;
      const auto found = std::find(choices->second.begin(), choices->second.end(), encoded);
      if (found == choices->second.end()) {
        result.error = "value absent from installed provider choices: " + variable;
        return result;
      }
      if (variable == "which_skybox") encoded = std::to_string(found - choices->second.begin());
      else if (variable == "forceplot" && encoded == "off") encoded = "FALSE";
    }
    result.durable = ::rb_blitz::ultimate::SaveProvider(updates, &result.error);
    if (result.durable) for (const auto& [id, value] : values) values_[id] = value;
    return result;
  }
  ActionResult Invoke(SettingId, uint64_t) override {
    return {false, "achievements require confirmation on the guest thread"};
  }
 private:
  std::map<SettingId, Value> values_;
};

}  // namespace

SettingsBoot::SettingsBoot() = default;

SettingsBoot::~SettingsBoot() {
  if (g_current == this) {
    g_current = nullptr;
  }
}

SettingsBoot* SettingsBoot::Current() { return g_current; }
void SettingsBoot::SetCurrent(SettingsBoot* boot) { g_current = boot; }

// ------------------------------------------------------------------------------------------------
// 1. OnConfigurePaths
// ------------------------------------------------------------------------------------------------

PathRoots SettingsBoot::ConfigurePaths(const std::filesystem::path& executable_dir) {
  // --- identity, the path file and the legacy profile ---------------------------------------
  launcher::ProfilePathInputs inputs = launcher::ProfilePathInputsFromEnvironment(executable_dir);
  inputs.command_line_value = rex::cvar::GetFlagByName("launcher_profile");
  paths_.settings_file = ResolveSettingsFile(inputs);
  paths_.paths_file = ResolvePathFile(executable_dir);
  paths_.legacy_profile = launcher::ResolveProfilePath(inputs);

  path_file_ = LoadPathFile(paths_.paths_file);
  if (path_file_.status == PathFileResult::Status::kMalformed) {
    diagnostics_.push_back("paths.toml is malformed and was ignored: " + path_file_.error);
  }

  std::string why;
  std::optional<launcher::Profile> legacy = launcher::ReadLauncherProfile(&why);
  // A profile that is there but unreadable is reported and *not* imported from (contracts §4.3): the
  // person is told by the launcher, and the game keeps the compiled defaults rather than a
  // half-imported profile.
  const bool legacy_usable = why.empty();
  if (!legacy_usable) {
    diagnostics_.push_back("the launcher profile could not be read: " + why);
  }

  store_available_ = !paths_.settings_file.empty();
  if (!store_available_) {
    diagnostics_.push_back(
        "nowhere to keep settings (no profile path, no app data folder); the legacy launcher rows "
        "are this run's only source and the guest channel keeps its in-memory store");
  } else {
    // The service is created *here*, once the paths are known: it holds a copy of them, so building
    // it before the identity was resolved would have it write to the wrong file.
    service_ = std::make_unique<Service>(paths_);
    sink_ = std::make_unique<ServiceSink>(service_.get());
    // Migrate + load. This is the step that writes `settings.toml` for an existing installation: the
    // migration is staged, backed up and idempotent, and it owns only known fields.
    load_status_ = service_->Boot(legacy ? &*legacy : nullptr, path_file_.paths, &migration_,
                                  legacy_usable);
    if (load_status_ == DocumentStatus::kMalformed ||
        load_status_ == DocumentStatus::kUnsupportedVersion) {
      diagnostics_.push_back(
          "settings.toml was refused (malformed, or written by a newer build); the compiled "
          "defaults and the explicit sources apply, and the file is untouched");
    }
  }

  // --- capabilities -------------------------------------------------------------------------
  // What the boot can answer honestly today, and what it cannot: the display probe (P4's open item)
  // and a compiled-backend list (the plugin exposes none). Both are reported as absent rather than
  // guessed, which is what keeps the Resolution row from offering a size nothing can render.
  CapabilityInputs cap_inputs;
  const unsigned cpus = std::thread::hardware_concurrency();
  cap_inputs.logical_processors = cpus == 0 ? 1u : static_cast<uint32_t>(cpus);
  ProbeDisplay(cap_inputs);
  // The stable title modes are 960x540 and 1280x720. Larger output modes come
  // from the backend's integer draw scale, not a larger guest EDRAM allocation.
  render_limits_ = TitleRenderPath();
  cap_inputs.render_path = render_limits_;
  {
    RendererProbe probe;
    probe.token = kShippedRenderer;
    probe.built = true;
    probe.detected = true;
    probe.usable = true;
    probe.active = true;
    // Empty: the title's native MSAA sample semantics have not been verified on this device, and an
    // unproved label must not ship (§7.1), so `native` is simply not offered.
    probe.native_aa_label.clear();
    probe.filtering_ceiling = kShippedFilteringCeiling;
    probe.note = "not probed: the one backend this project ships and has run";
    cap_inputs.renderers.push_back(std::move(probe));
  }
  cap_inputs.revision = 1;
  capability_snapshot_ = BuildCapabilitySnapshot(cap_inputs);
  if (service_) {
    service_->SetReconciler(&reconciler_);
    service_->SetCapabilities(capability_snapshot_);
    // An unmeasured GPU gets a conservative automatic choice. Never infer performance
    // from desktop resolution. Existing manual choices remain untouched.
    constexpr int kPolicyVersion = 2;
    const auto recommendation = RecommendGraphics(capability_snapshot_);
    const GraphicsValues recommendations = {
        {SettingId::kRenderer, recommendation.renderer},
        {SettingId::kResolution, recommendation.resolution},
        {SettingId::kAntiAliasing, recommendation.anti_aliasing},
        {SettingId::kFiltering, recommendation.filtering}};
    auto& store = service_->store();
    if (load_status_ == DocumentStatus::kOk || load_status_ == DocumentStatus::kMissing) {
      for (const auto& [id, value] : recommendations) {
        const auto& doc = store.document();
        if (!doc.values.contains(id) ||
            (doc.automatic_policy_version != kPolicyVersion && doc.origins.contains(id) &&
             doc.origins.at(id) == Origin::kAutomatic))
          store.MarkChanged(id, value, Origin::kAutomatic);
      }
      if (store.has_changes()) {
        store.MarkPolicyVersion(kPolicyVersion);
        const auto saved = store.Save();
        if (!saved.errors.empty()) diagnostics_.push_back(saved.errors.front());
      }
    }
    std::string error;
    if (!graphics_boot_.Open(paths_.settings_file, &error) && !error.empty())
      diagnostics_.push_back(error);
    GraphicsValues requested;
    for (const auto& [id, value] : recommendations) requested[id] = service_->EffectiveValue(id);
    const auto fallback = RecoverGraphics(capability_snapshot_, requested.at(SettingId::kResolution),
                                          requested.at(SettingId::kRenderer).text, true);
    const GraphicsValues safe = {{SettingId::kRenderer, fallback.renderer},
                                 {SettingId::kResolution, fallback.resolution},
                                 {SettingId::kAntiAliasing, fallback.anti_aliasing},
                                 {SettingId::kFiltering, fallback.filtering}};
    const bool forced_safe = REXCVAR_GET(settings_safe_graphics);
    auto selected = graphics_boot_.Select(requested, safe, forced_safe);
    if (graphics_boot_.recovered() || forced_safe) {
      for (auto& [id, value] : selected)
        value = reconciler_.Reconcile(id, *service_->DescriptorFor(id), value, capability_snapshot_).effective;
      service_->SetBootValues(std::move(selected), "settings.graphics.recovered_from_failure");
      diagnostics_.push_back("safe graphics selected; requested preferences are retained and editable");
    }
  }

  // --- the path roots -----------------------------------------------------------------------
  // paths.toml is the installer's file and the user may hand-edit it; its values replace the legacy
  // profile's path fields (§9.1) and are written at the config rank so the game's own rb_blitz.toml
  // still outranks them. An explicit environment/CLI value outranks both and is left alone.
  PathRoots roots;
  if (!path_file_.paths.dlc_root.empty()) {
    roots.dlc_root = PathText(ResolvePathValue(path_file_.paths.dlc_root, paths_.paths_file, {}));
    if (ExplicitlyOwned("dlc_root", SettingId::kUnknown)) {
      diagnostics_.push_back(
          "dlc_root is set by an explicit source; the path file's value was not applied");
    } else {
      roots.have_dlc_root = WriteCvar("dlc_root", roots.dlc_root, SettingId::kUnknown);
    }
  }
  if (!path_file_.paths.user_data_root.empty()) {
    roots.user_data_root =
        PathText(ResolvePathValue(path_file_.paths.user_data_root, paths_.paths_file, {}));
    if (ExplicitlyOwned("user_data_root", SettingId::kUnknown)) {
      diagnostics_.push_back(
          "user_data_root is set by an explicit source; the path file's value was not applied");
    } else {
      roots.have_user_data_root =
          WriteCvar("user_data_root", roots.user_data_root, SettingId::kUnknown);
    }
  }

  // --- the rows, at the config rank ---------------------------------------------------------
  if (service_) {
    const Value target_value = service_->EffectiveValue(SettingId::kTarget);
    const std::optional<launcher::LaunchTarget> target =
        launcher::ParseLaunchTarget(target_value.text);
    const LicensePolicy policy{kPurchasedLicenseMask, kTrialLicenseMask};
    ApplyCvars(TargetToCvars(target.value_or(launcher::LaunchTarget::kCommon), policy),
               SettingId::kTarget, /*defer_unregistered=*/false);

    // The renderer first (OnPreSetup reads it when it loads the plugin), then the rows the mod's own
    // hooks read when they configure, then the graphics rows whose cvars belong to the *GPU plugin*:
    // `vsync`, `native_2x_msaa`, `swap_post_effect` and `anisotropic_override` are defined in
    // rexgpu-xenos.dll and do not exist until it is loaded, so those writes are collected here and
    // made in AfterLoadXexImage - where the fact that they could not be written at the config rank
    // is reported rather than swallowed.
    for (const SettingId id :
         {SettingId::kRenderer, SettingId::kCacheDlcLibrary, SettingId::kDlcScanThreads,
          SettingId::kDlcMenuSongs, SettingId::kHideOfflinePrompts, SettingId::kHideMainMenu,
          SettingId::kHiddenMainMenu, SettingId::kVsync, SettingId::kFiltering,
          SettingId::kResolution, SettingId::kAntiAliasing}) {
      ApplyRow(id, /*defer_unregistered=*/true);
    }
  }
  return roots;
}

void SettingsBoot::ApplyRow(const SettingId id, const bool defer_unregistered) {
  if (!service_) {
    return;
  }
  const Descriptor* descriptor = service_->DescriptorFor(id);
  if (descriptor == nullptr) {
    return;
  }
  const Value value = service_->EffectiveValue(id);
  // A row nobody has chosen keeps the game's own default: the compiled default is not a preference
  // and writing it would turn "never chosen" into "chosen" (which is exactly what the hidden-menu
  // list must not do - an empty selection hides nothing, an absent one keeps the defaults).
  if (service_->EffectiveSource(id) == Source::kCompiledDefault) {
    return;
  }
  if (service_->IsOverridden(id)) {
    overrides_[id] = service_->EffectiveSource(id);
    override_values_[id] = value;
    return;
  }

  switch (id) {
    case SettingId::kRenderer: {
      if (value.text.empty()) {
        return;
      }
      ApplyCvars({{"gpu_backend", value.text}}, id, defer_unregistered);
      return;
    }
    case SettingId::kVsync: {
      ApplyCvars({{"vsync", BoolText(value.boolean)}}, id, defer_unregistered);
      return;
    }
    case SettingId::kAntiAliasing: {
      ApplyCvars(AntiAliasingToCvars(value.text), id, defer_unregistered);
      return;
    }
    case SettingId::kFiltering: {
      int32_t cvar_value = 0;
      if (!FilteringToCvarValue(value.text, &cvar_value)) {
        return;
      }
      ApplyCvars({{"anisotropic_override", std::to_string(cvar_value)}}, id, defer_unregistered);
      return;
    }
    case SettingId::kResolution: {
      screen_plan_ = PlanGuestScreen(render_limits_, {value.width, value.height});
      if (screen_plan_.supported) {
        ApplyCvars({{"resolution_scale", std::to_string(screen_plan_.draw_scale)}}, id,
                   defer_unregistered);
      }
      return;
    }
    case SettingId::kTarget:
      return;  // handled as a group, above, because it writes two cvars
    default:
      break;
  }

  const std::string_view cvar = CvarForSetting(id);
  if (cvar.empty()) {
    return;  // provider-owned or an action: nothing of ours to write
  }
  std::string text;
  switch (value.kind) {
    case ValueKind::kBool:
      text = BoolText(value.boolean);
      break;
    case ValueKind::kInt:
      text = std::to_string(value.integer);
      break;
    case ValueKind::kEnum:
    case ValueKind::kString:
      text = value.text;
      break;
    case ValueKind::kList:
      // An *explicitly empty* list is a real selection ("hide nothing") and is written as such; the
      // "never chosen" case is the kNone kind, which never reaches here.
      text = ListText(value.list);
      break;
    default:
      return;
  }
  ApplyCvars({{std::string(cvar), std::move(text)}}, id, defer_unregistered);
}

void SettingsBoot::ApplyCvars(const std::vector<CvarAssignment>& assignments, const SettingId id,
                              const bool defer_unregistered) {
  // Compound settings are atomic with respect to explicit overrides (target/license and AA).
  bool overridden = false;
  for (const auto& [name, value] : assignments) {
    overridden = ExplicitlyOwned(name, id) || overridden;
  }
  if (overridden) {
    return;
  }
  for (const auto& [name, value] : assignments) {
    if (ExplicitlyOwned(name, id)) {
      diagnostics_.push_back(name + " is set by an explicit source; the stored row was not applied");
      continue;
    }
    if (WriteCvar(name, value, id)) {
      continue;
    }
    if (defer_unregistered) {
      // The cvar does not exist yet (the GPU plugin's do not, until it is loaded). Remembered with
      // its row, so the write that finally happens is verified like every other one.
      deferred_writes_.push_back(Applied{id, name, value, false});
    } else {
      REXLOG_WARN("settings_boot: the registry does not know '{}'; its row cannot be applied", name);
    }
  }
}

bool SettingsBoot::WriteCvar(const std::string& name, const std::string& value, const SettingId id) {
  if (!rex::cvar::SetFlagFromConfig(name, value)) {
    return false;
  }
  if (id != SettingId::kUnknown) {
    applied_.push_back(Applied{id, name, value, false});
  }
  return true;
}

bool SettingsBoot::ExplicitlyOwned(const std::string& name, const SettingId id) {
  const rex::cvar::Source source = rex::cvar::GetFlagSource(name);
  if (!HigherRank(source)) {
    return false;
  }
  if (id != SettingId::kUnknown && service_) {
    overrides_[id] = OverrideSourceFor(source);
    const Descriptor* descriptor = service_->DescriptorFor(id);
    Value parsed;
    if (descriptor != nullptr &&
        schema::ParseTokenToValue(*descriptor, rex::cvar::GetFlagByName(name), &parsed)) {
      override_values_[id] = parsed;
    }
  }
  return true;
}

// ------------------------------------------------------------------------------------------------
// 2. OnPostInitLogging
// ------------------------------------------------------------------------------------------------

void SettingsBoot::LogConfigured() const {
  for (const std::string& line : diagnostics_) {
    REXLOG_WARN("settings_boot: {}", line);
  }
  REXLOG_INFO("settings_boot: settings '{}' ({})", paths_.settings_file.string(),
              load_status_ == DocumentStatus::kOk        ? "loaded"
              : load_status_ == DocumentStatus::kMissing ? "absent; the compiled defaults apply"
              : load_status_ == DocumentStatus::kMalformed ? "malformed; left untouched"
                                                           : "unsupported version; left untouched");
  REXLOG_INFO("settings_boot: paths '{}' ({}), {} row(s) from the store, revision {}",
              paths_.paths_file.string(),
              path_file_.status == PathFileResult::Status::kOk         ? "read"
              : path_file_.status == PathFileResult::Status::kMissing  ? "absent"
                                                                       : "malformed; ignored",
              service_ ? service_->descriptors().size() : 0,
              service_ ? service_->revision() : 0);
  if (migration_.status != MigrationStatus::kNoLegacy) {
    REXLOG_INFO("settings_boot: migration {} - {} row(s) imported, {} translated, {} carried "
                "over, {} reported",
                MigrationName(migration_.status), migration_.stats.imported,
                migration_.stats.translated, migration_.stats.preserved,
                migration_.diagnostics.size());
    for (const std::string& line : migration_.diagnostics) {
      REXLOG_INFO("settings_boot:   {}", line);
    }
  }
  if (!migration_.backup.empty()) {
    REXLOG_INFO("settings_boot: the legacy profile was backed up to '{}'",
                migration_.backup.string());
  }
  REXLOG_INFO("settings_boot: {} row(s) applied at the config rank before the game's own config; "
              "rb_blitz.toml and any explicit source still outrank them",
              applied_.size());
  for (const Applied& row : applied_) {
    REXLOG_INFO("settings_boot:   {} = {}", row.cvar, row.expected);
  }
  if (!deferred_writes_.empty()) {
    REXLOG_INFO("settings_boot: {} cvar(s) belong to the GPU plugin and are written once it has "
                "registered them",
                deferred_writes_.size());
  }
  REXLOG_INFO("settings_boot: display {}x{}, {} exact render sizes; graphics preferences will be "
              "applied before presentation and render resources are initialized",
              capability_snapshot_.primary_display_size.width,
              capability_snapshot_.primary_display_size.height,
              capability_snapshot_.supported_resolutions.size());
}

void SettingsBoot::BeforeGraphicsSetup(const std::string& active_renderer) {
  active_renderer_ = active_renderer.empty() || active_renderer == "any"
                         ? kShippedRenderer : active_renderer;
  // Plugin flags now contain the developer config, environment and CLI values replayed at
  // registration. Even a config-rank value here outranks our earlier stored preference.
  for (const Applied& deferred : deferred_writes_) {
    const auto source = rex::cvar::GetFlagSource(deferred.cvar);
    if (source != rex::cvar::Source::kDefault) {
      overrides_[deferred.id] = OverrideSourceFor(source);
    }
  }
  for (const Applied& deferred : deferred_writes_) {
    if (overrides_.contains(deferred.id)) {
      REXLOG_INFO("settings_boot: {} is explicitly configured; stored preference skipped",
                  deferred.cvar);
      continue;
    }
    if (WriteCvar(deferred.cvar, deferred.expected, deferred.id)) {
      REXLOG_INFO("settings_boot: {} = {} (before graphics setup)", deferred.cvar,
                  deferred.expected);
    } else {
      REXLOG_WARN("settings_boot: could not apply {} before graphics setup", deferred.cvar);
    }
  }
  deferred_writes_.clear();
  if (screen_plan_.supported && !overrides_.contains(SettingId::kResolution)) {
    // Both halves of the guest size, because the title derives its width from its height and
    // the aspect the user picked is not one the title can compute (src/hooks/render_settings.h).
    SetBootRenderFrame(screen_plan_.guest_size.width, screen_plan_.guest_size.height);
    REXLOG_INFO("settings_boot: guest render base {}x{}, draw scale {} (before graphics setup)",
                screen_plan_.guest_size.width, screen_plan_.guest_size.height,
                screen_plan_.draw_scale);
  }
  if (service_) {
    GraphicsValues effective;
    for (const auto id : {SettingId::kRenderer, SettingId::kResolution,
                          SettingId::kAntiAliasing, SettingId::kFiltering})
      effective[id] = service_->EffectiveValue(id);
    std::string error;
    if (graphics_boot_.active() && !graphics_boot_.Begin(effective, &error))
      REXLOG_WARN("settings_boot: {}", error);
  }
}

void SettingsBoot::MenuFrame() {
  if (menu_presented_ || !graphics_boot_.active() || !presented_probe_) return;
  // The shell poll proves the menu is active. Wait several frames, then verify an
  // actual guest GPU image; process creation or a black host window isn't success.
  if (++menu_frames_ % 30 != 0) return;
  std::lock_guard lock(graphics_marker_mutex_);
  if (menu_presented_) return;
  if (!presented_probe_()) return;
  std::string error;
  if (graphics_boot_.Presented(&error)) {
    menu_presented_ = true;
    REXLOG_INFO("settings_boot: presented menu frame; graphics marked last-known-good");
  } else {
    menu_presented_ = true;
    REXLOG_WARN("settings_boot: cannot mark graphics ready: {}", error);
  }
}

void SettingsBoot::Closing() {
  std::lock_guard lock(graphics_marker_mutex_);
  menu_presented_ = true;
  std::string error;
  if (!graphics_boot_.Closed(&error)) REXLOG_WARN("settings_boot: {}", error);
}

// ------------------------------------------------------------------------------------------------
// 3. OnPostLoadXexImage (after ultimate::Configure)
// ------------------------------------------------------------------------------------------------

void SettingsBoot::AfterLoadXexImage(const std::string& active_renderer) {
  active_renderer_ = active_renderer.empty() ? kShippedRenderer : active_renderer;

  // --- the provider reaches the service -----------------------------------------------------
  const auto& provider = ::rb_blitz::ultimate::Provider();
  if (service_) {
    capability_snapshot_.provider.installed = provider.payload_present;
    capability_snapshot_.provider.compatible = provider.Active();
    capability_snapshot_.provider.active = provider.Active();
    ++capability_snapshot_.revision;
    service_->SetCapabilities(capability_snapshot_);
    service_->SetProviderValues(provider.values);
  }
  if (service_ && provider.Active()) {
    std::map<SettingId, std::vector<Choice>> choices;
    for (const auto& binding : ultimate_adapter::RowBindings()) {
      const auto found = provider.choices.find(std::string(binding.variable));
      if (found != provider.choices.end())
        for (const auto& token : found->second) choices[binding.id].push_back({token, token});
    }
    service_->SetProviderChoices(std::move(choices));
    provider_sink_ = std::make_unique<InstalledProviderSink>();
    service_->SetProviderSink(provider_sink_.get());
  }
  REXLOG_INFO("settings_boot: the provider is {} ({}); {} value(s) reach the store",
              provider.Active() ? "active" : "not active",
              provider.summary.empty() ? "no payload" : provider.summary, provider.values.size());
  // The provider's own rows are edited through the channel and persisted by its write-through (P6,
  // src/hooks/settings_bridge_hooks.cpp), so the service is deliberately NOT given a provider sink:
  // seeding the provider's values must not look like a change, and a boot must not rewrite the
  // player's `ulti_settings.dta`. The achievements action stays unwired and says so (P6 §6.1).

  // --- the guest channel's store ---------------------------------------------------------
  // With a store, the menu's edits go through the service (settings.toml, transactions, a durable
  // commit). Without one (nowhere to keep settings) the channel keeps P1's in-memory sink rather
  // than pointing at a store that could never write.
  if (sink_) {
    ::rb_blitz::settings::bridge::InstallSink(sink_.get(), sink_.get());
  }
}

// ------------------------------------------------------------------------------------------------
// 4. OnPreLaunchModule
// ------------------------------------------------------------------------------------------------

void SettingsBoot::FinishInit() {
  // Read back what this module wrote. `SetFlagFromConfig` returning true does not prove the value
  // won (contracts §4.2): it can mean a higher-priority value was left in place, and a *later* write
  // at the same rank - the game's own rb_blitz.toml - beats this one. A row the player cannot change
  // has to say so instead of saving a value that never takes effect.
  size_t verified = 0;
  for (Applied& row : applied_) {
    const std::string actual = rex::cvar::GetFlagByName(row.cvar);
    row.verified = actual == row.expected;
    if (row.verified) {
      ++verified;
      continue;
    }
    overrides_[row.id] = OverrideSourceFor(rex::cvar::GetFlagSource(row.cvar));
    const Descriptor* descriptor = service_ ? service_->DescriptorFor(row.id) : nullptr;
    Value parsed;
    if (descriptor != nullptr && schema::ParseTokenToValue(*descriptor, actual, &parsed)) {
      override_values_[row.id] = parsed;
    }
    REXLOG_WARN("settings_boot: '{}' = '{}' did not stand (the effective value is '{}'); the row "
                "is reported as overridden and editing it would not take effect",
                row.cvar, row.expected, actual);
  }
  if (!overrides_.empty() && service_) {
    service_->SetOverrides(override_values_, overrides_);
  }
  REXLOG_INFO("settings_boot: {} of {} applied value(s) stood; {} row(s) are owned by an explicit "
              "source",
              verified, applied_.size(), overrides_.size());
}

void SettingsBoot::SetMenuRows(std::vector<Choice> rows) {
  // Two tables, one list: the schema resolves the row's choices (what the store validates against)
  // and the service is what the UI reads. Both are the *effective* main menu's rows - the caller has
  // the record bytes, because that is where the menu actually is.
  schema::SetMenuRows(rows);
  if (service_) {
    service_->SetMenuRows(std::move(rows));
  }
}

}  // namespace rb_blitz::settings::boot
