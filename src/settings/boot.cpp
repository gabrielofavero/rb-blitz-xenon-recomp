// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The implementation of src/settings/boot.h - see that file for the ordering and for the two open
// items this pass states instead of guessing.

#include "settings/boot.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
#include <rex/platform/dynlib.h>

#include "hooks/settings_bridge_hooks.h"
#include "hooks/hud_layout.h"
#include "hooks/render_settings.h"
#include "hooks/ultimate.h"
#include "profile/profile.h"
#include "profile/profile_apply.h"
#include "profile/profile_path.h"
#include "settings/cvars.h"
#include "settings/hot_apply.h"
#include "settings/schema.h"
#include "settings/window_policy.h"

REXCVAR_DEFINE_BOOL(settings_safe_graphics, false, "Settings",
                    "Start with safe graphics without deleting saved preferences.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace rb_blitz::settings::boot {
namespace {

// The process-wide instance. Owned by the app; the hooks that need the store (the menu-rows seam)
// reach it here rather than through a second global.
SettingsBoot* g_current = nullptr;

// The cvars this module writes are read and written **by name** on purpose: their storage lives with
// their definition (`launcher_profile` in src/profile/profile_apply.cpp, `dlc_root` in
// src/hooks/dlc.cpp, `user_data_root` in the runtime's own flags), and a `REXCVAR_DECLARE` here would
// ask the linker for storage another translation unit keeps private. The registry is the interface.

// The anisotropic step this project has actually run: the retired Controller Settings row's own default
// (`anisotropic_override = 3`, i.e. 4x), which every measured boot in this repository used. It is
// **not** a device probe - P4's probing item is still open - and it is deliberately conservative:
// the ceiling only bounds what the menu may offer, so shipping a ceiling this project has run is
// safer than offering a step nothing has.
constexpr int32_t kShippedFilteringCeiling = 3;

// The backends the GPU plugin was compiled with, from the build's own switch: CMakeLists.txt passes
// the same `REXGLUE_USE_D3D12`/`REXGLUE_USE_VULKAN` values the plugin is built from and the
// Controller Settings's Renderer row reads (`--backends=`), so the three cannot disagree about what the plugin
// contains. A target that compiles this file without that definition (a unit-test build) reports the
// backend this project has always shipped.
#if !defined(RBBLITZ_BACKENDS)
#define RBBLITZ_BACKENDS "d3d12"
#endif

std::vector<std::string_view> ShippedBackends() {
  std::vector<std::string_view> out;
  const std::string_view list = RBBLITZ_BACKENDS;
  for (std::size_t start = 0; start < list.size();) {
    const std::size_t comma = list.find(',', start);
    const std::string_view token =
        comma == std::string_view::npos ? list.substr(start) : list.substr(start, comma - start);
    if (!token.empty()) out.push_back(token);
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return out;
}

// The backend an unnamed/"any" choice lands on, and the one the boot names when nobody named one:
// this project has always run D3D12, so it stays the default even now that Vulkan is offered.
constexpr char kFallbackBackend[] = "d3d12";

// True when the Vulkan loader is present and looks like a loader - the same check
// `RbBlitzApp::SelectGpuBackend` makes before committing to the backend. The SDK opens the loader
// at run time rather than linking it, so "the plugin was built with Vulkan" and "this machine can
// run Vulkan" are different facts: with no loader, a Vulkan boot builds a Vulkan system and only
// fails inside presentation setup, where there is nothing left to fall back to.
bool VulkanLoaderIsUsable() {
  rex::platform::DynamicLibrary loader;
  if (!loader.Load(rex::platform::lib_names::kVulkanLoader)) return false;
  return loader.GetRawSymbol("vkGetInstanceProcAddr") != nullptr;
}

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

// --- P2: the desktop the game window has to fit into --------------------------------------------
//
// Win32 rather than SDL, for the same reason ProbeDisplay is Win32: this translation unit is the
// game's own half and already asks the display through the OS. The frame metrics come from the very
// function the window manager uses to turn a client rectangle into a window rectangle for a
// WS_OVERLAPPEDWINDOW, so the fit is measured against the real decorations rather than a guess.
//
// `monitor` is the SDK's own cvar (0 = system default, 1 = primary, 2.. = the enumeration order its
// window code uses); a value naming no display leaves the metrics unavailable, and an unavailable
// measurement means no fit at all rather than a fit against the wrong screen.

#ifdef _WIN32

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
  reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
  return TRUE;
}

// What a decorated window adds to its client area, at `dpi`. The DPI-aware entry point is resolved
// at run time: it is the correct one on any Windows that has it, and the call below it is the
// system-DPI approximation for the Windows versions that do not.
bool FrameExtentForDpi(const UINT dpi, DisplayMode* out) {
  RECT frame{0, 0, 0, 0};
  using AdjustForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
  static const AdjustForDpiFn adjust_for_dpi = reinterpret_cast<AdjustForDpiFn>(
      ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
  if (adjust_for_dpi != nullptr) {
    if (adjust_for_dpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi) == FALSE) {
      return false;
    }
  } else if (::AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0) == FALSE) {
    return false;
  }
  out->width = int32_t(frame.right - frame.left);
  out->height = int32_t(frame.bottom - frame.top);
  return out->width > 0 && out->height > 0;
}

uint32_t SystemDpi() {
  using GetDpiForSystemFn = UINT(WINAPI*)();
  static const GetDpiForSystemFn get_dpi_for_system = reinterpret_cast<GetDpiForSystemFn>(
      ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
  if (get_dpi_for_system != nullptr) {
    if (const UINT dpi = get_dpi_for_system(); dpi != 0) {
      return dpi;
    }
  } else if (HDC screen = ::GetDC(nullptr); screen != nullptr) {
    const UINT dpi = static_cast<UINT>(::GetDeviceCaps(screen, LOGPIXELSX));
    ::ReleaseDC(nullptr, screen);
    if (dpi != 0) {
      return dpi;
    }
  }
  return 96;  // the medium DPI, i.e. 100%
}

DesktopMetrics ProbeDesktop(const int32_t monitor_index) {
  DesktopMetrics desktop;
  std::vector<HMONITOR> monitors;
  if (::EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
                            reinterpret_cast<LPARAM>(&monitors)) == FALSE ||
      monitors.empty()) {
    return desktop;
  }
  std::size_t index = 0;
  if (monitor_index > 1) {
    if (static_cast<std::size_t>(monitor_index - 1) >= monitors.size()) {
      return desktop;  // a display that is not there: measure nothing rather than the wrong screen
    }
    index = static_cast<std::size_t>(monitor_index - 1);
  }
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (::GetMonitorInfoW(monitors[index], &info) == FALSE) {
    return desktop;
  }
  const uint32_t dpi = SystemDpi();
  desktop.work_area = {int32_t(info.rcWork.right - info.rcWork.left),
                       int32_t(info.rcWork.bottom - info.rcWork.top)};
  if (!FrameExtentForDpi(dpi, &desktop.frame_extent)) {
    // No frame metrics means no fit: leaving `available` false is the honest answer.
    return desktop;
  }
  desktop.scale_percent = static_cast<uint32_t>(uint64_t(dpi) * 100 / 96);
  desktop.available = desktop.work_area.width > 0 && desktop.work_area.height > 0;
  return desktop;
}

#else

DesktopMetrics ProbeDesktop(int32_t) { return DesktopMetrics{}; }

#endif  // _WIN32

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
  profile::ProfilePathInputs inputs = profile::ProfilePathInputsFromEnvironment(executable_dir);
  // The cvar is `launcher_profile` because the flag is `--launcher_profile` (src/profile/profile_apply.cpp).
  inputs.command_line_value = rex::cvar::GetFlagByName("launcher_profile");
  paths_.settings_file = ResolveSettingsFile(inputs);
  paths_.paths_file = ResolvePathFile(executable_dir);
  paths_.legacy_profile = profile::ResolveProfilePath(inputs);

  path_file_ = LoadPathFile(paths_.paths_file);
  if (path_file_.status == PathFileResult::Status::kMalformed) {
    diagnostics_.push_back("paths.toml is malformed and was ignored: " + path_file_.error);
  }

  std::string why;
  std::optional<profile::Profile> legacy = profile::ReadProfile(&why);
  // A profile that is there but unreadable is reported and *not* imported from (contracts §4.3): the
  // person is told by Controller Settings, and the game keeps the compiled defaults rather than a
  // half-imported profile.
  const bool legacy_usable = why.empty();
  if (!legacy_usable) {
    diagnostics_.push_back("the profile could not be read: " + why);
  }

  store_available_ = !paths_.settings_file.empty();
  if (!store_available_) {
    diagnostics_.push_back(
        "nowhere to keep settings (no profile path, no app data folder); the legacy Controller Settings rows "
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
  // What the boot can answer honestly today, and what it cannot: the display probe answers with the
  // primary mode, and each backend the build compiled gets a probe of its own. "Usable" is a real
  // check where this machine can answer one - the Vulkan loader, which the SDK opens at run time -
  // and the honest constant elsewhere (D3D12 is the path every measured boot of this project took;
  // there is nothing to detect before the plugin loads). A compiled backend this machine cannot run
  // is reported as such rather than offered, which is what keeps the Renderer row honest.
  CapabilityInputs cap_inputs;
  const unsigned cpus = std::thread::hardware_concurrency();
  cap_inputs.logical_processors = cpus == 0 ? 1u : static_cast<uint32_t>(cpus);
  ProbeDisplay(cap_inputs);
  // The stable title modes are 960x540 and 1280x720. Larger output modes come
  // from the backend's integer draw scale, not a larger guest EDRAM allocation.
  render_limits_ = TitleRenderPath();
  cap_inputs.render_path = render_limits_;
  for (const std::string_view token : ShippedBackends()) {
    RendererProbe probe;
    probe.token = std::string(token);
    probe.built = true;
    probe.detected = true;
    probe.usable = true;
    // The backend a boot with no stored choice lands on. Named in the snapshot so the graphics
    // policy has a stable default; which backend a *loaded* plugin really initialised is reported
    // by the app (`active_renderer_`) and is P4's device-probe item, not this table's.
    probe.active = token == kFallbackBackend;
    // Empty: the title's native MSAA sample semantics have not been verified on this device, and an
    // unproved label must not ship (§7.1), so `native` is simply not offered.
    probe.native_aa_label.clear();
    probe.filtering_ceiling = kShippedFilteringCeiling;
    if (token == "vulkan") {
      const bool loader = VulkanLoaderIsUsable();
      probe.detected = loader;
      probe.usable = loader;
      probe.note = loader ? "not probed: a backend this project ships and has run"
                          : "the Vulkan loader (vulkan-1.dll) is not present here";
    } else {
      probe.note = "not probed: a backend this project ships and has run";
    }
    cap_inputs.renderers.push_back(std::move(probe));
  }
  cap_inputs.revision = 1;
  capability_snapshot_ = BuildCapabilitySnapshot(cap_inputs);
  if (service_) {
    service_->SetReconciler(&reconciler_);
    service_->SetCapabilities(capability_snapshot_);
    // Auto follows the current display on every boot, without running a benchmark.
    constexpr int kPolicyVersion = 3;
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
      const auto resolution = service_->RequestedValue(SettingId::kResolution);
      const auto compatible = ResolutionPreferenceForBoot(capability_snapshot_, resolution);
      if (!ValueEquals(resolution, compatible)) {
        store.MarkChanged(SettingId::kResolution, compatible, Origin::kAutomatic);
        diagnostics_.push_back("saved resolution is incompatible with this display; switched to Auto");
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
    const std::optional<profile::LaunchTarget> target =
        profile::ParseLaunchTarget(target_value.text);
    const LicensePolicy policy{kPurchasedLicenseMask, kTrialLicenseMask};
    ApplyCvars(TargetToCvars(target.value_or(profile::LaunchTarget::kCommon), policy),
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
          SettingId::kResolution, SettingId::kAntiAliasing, SettingId::kDisplay,
          SettingId::kSettingsLabels}) {
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
    case SettingId::kDisplay: {
      // Two cvars, one row: the window mode is not one SDK flag. An empty assignment set (a token
      // this build does not know) is reported instead of guessed at.
      const auto assignments = DisplayToCvars(value.text);
      if (assignments.empty()) {
        REXLOG_WARN("settings_boot: '{}' is not a window mode this build knows; the row was not "
                    "applied", value.text);
        return;
      }
      ApplyCvars(assignments, id, defer_unregistered);
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

// ------------------------------------------------------------------------------------------------
// P2: the window and the presenter follow the render size
// ------------------------------------------------------------------------------------------------

bool SettingsBoot::WriteWindowCvar(const std::string& name, const std::string& value) {
  if (name.empty()) {
    return false;
  }
  // This runs *after* the game's own rb_blitz.toml (loaded right after OnConfigurePaths) and at the
  // same rank, so a plain config write would beat the file a developer put the value in. A cvar
  // somebody else already named - the file, the environment or the command line - is therefore left
  // completely alone: the window policy is a default for a boot nobody configured, not an override
  // (contracts §4.2).
  if (rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault) {
    REXLOG_INFO("settings_boot: '{}' is explicitly configured ('{}'); the window policy left it "
                "alone", name, rex::cvar::GetFlagByName(name));
    return false;
  }
  if (!rex::cvar::SetFlagFromConfig(name, value)) {
    REXLOG_WARN("settings_boot: the registry does not know '{}'; the window policy could not set "
                "it", name);
    return false;
  }
  // `SetFlagFromConfig` reports success for a skipped write as well as an applied one, so the value
  // is read back - the same rule FinishInit applies to the stored rows.
  if (const std::string actual = rex::cvar::GetFlagByName(name); actual != value) {
    REXLOG_WARN("settings_boot: '{}' = '{}' did not stand (the effective value is '{}')", name,
                value, actual);
    return false;
  }
  // Named on the way out: the boot's own evidence that the window really was given this size and
  // this handle policy, rather than a line whose absence has to be read as success.
  REXLOG_INFO("settings_boot: window policy: {} = {}", name, value);
  return true;
}

void SettingsBoot::ApplyWindowPolicy() {
  // The render size this run really produces: the planner's guest size times its draw scale is the
  // request back again, exactly (`PlanGuestScreen` is RenderPathSizes' inverse). With no plan the
  // title renders its own 1280x720, and nothing about the window should move.
  if (!screen_plan_.supported) {
    REXLOG_INFO("settings_boot: no render plan this boot; the window keeps the title's own size");
    return;
  }
  const DisplayMode render{screen_plan_.guest_size.width * screen_plan_.draw_scale,
                           screen_plan_.guest_size.height * screen_plan_.draw_scale};

  // The presenter derives the shape of its output region from the *video mode* aspect, not from the
  // frame's (docs/settings/graphics.md part 1 §3), so a size the title would not have chosen has to
  // be reported there or the correct frame is squashed into a 16:9-shaped region. The title itself
  // never reads the video mode (part 1 §4); the presenter and the startup window's own default size
  // are what do.
  WriteWindowCvar("video_mode_width", std::to_string(render.width));
  WriteWindowCvar("video_mode_height", std::to_string(render.height));

  // The window itself: the same size, unless the decorated window would not fit the display's
  // usable area (a render size equal to the display mode never does). `window_width`/`window_height`
  // are *logical* pixels and outrank the video mode in the SDK's own resolution order, so they are
  // written only when the fit really shrinks the window: a boot that fits keeps the exact size the
  // video mode gives it, with no unit round trip at all.
  const long monitor_index =
      std::strtol(rex::cvar::GetFlagByName("monitor").c_str(), nullptr, 10);
  const DesktopMetrics desktop = ProbeDesktop(static_cast<int32_t>(monitor_index));
  const ClientFit fit = FitClientToDesktop(render, desktop);
  const double aspect = double(render.width) / double(render.height);
  if (!desktop.available) {
    REXLOG_INFO("settings_boot: window {}x{} (aspect {:.4f}); no usable desktop measurement, so it "
                "is created at the render size", render.width, render.height, aspect);
  } else if (!fit.shrunk) {
    // The video mode already gives exactly this size, so nothing is written. If somebody named the
    // window's size instead, it is theirs and the line says so rather than implying this policy
    // chose it.
    const bool window_named =
        rex::cvar::GetFlagSource("window_width") != rex::cvar::Source::kDefault ||
        rex::cvar::GetFlagSource("window_height") != rex::cvar::Source::kDefault;
    REXLOG_INFO("settings_boot: window {}x{} (aspect {:.4f}) fits the usable desktop {}x{} with "
                "{}+{} px of decorations{}",
                render.width, render.height, aspect, desktop.work_area.width,
                desktop.work_area.height, desktop.frame_extent.width, desktop.frame_extent.height,
                window_named ? "; the window size is explicitly configured" : "");
  } else {
    const DisplayMode logical = PhysicalToLogical(fit.client, desktop.scale_percent);
    // Both halves, attempted even if the first is refused: the two are one size, and a half-applied
    // pair would be worse than either outcome. The line below reports what happened, not what was
    // wanted - a boot whose window size is explicitly configured keeps its own size and says so.
    const bool wrote_width = WriteWindowCvar("window_width", std::to_string(logical.width));
    const bool wrote_height = WriteWindowCvar("window_height", std::to_string(logical.height));
    if (wrote_width && wrote_height) {
      REXLOG_INFO(
          "settings_boot: window {}x{} -> client {}x{} physical ({}x{} logical at {}%), aspect "
          "{:.4f} kept, inside the usable desktop {}x{} minus {}x{} of decorations",
          render.width, render.height, fit.client.width, fit.client.height, logical.width,
          logical.height, desktop.scale_percent, aspect, desktop.work_area.width,
          desktop.work_area.height, desktop.frame_extent.width, desktop.frame_extent.height);
    } else {
      REXLOG_WARN("settings_boot: a {}x{} render wants a {}x{} client here, but this boot's window "
                  "size is explicitly configured; the fit was not applied and the window may not "
                  "match the picture's shape",
                  render.width, render.height, fit.client.width, fit.client.height);
    }
  }

  // One size, decided here: the window is a picture, and dragging it into a shape the render size
  // cannot fill is exactly the distortion this project removed. The separate Controller Settings
  // utility builds its own SDL window (controller-settings/main.cpp) and never reads this cvar.
  WriteWindowCvar("window_resizable", "false");
  // The aspect the window shows and the aspect the render size has must be the same, or the
  // presenter letterboxes what it was handed. Stated rather than assumed, so a mismatch is a log
  // line and not a screenshot somebody has to interpret.
  if (desktop.available && fit.shrunk && !SameAspect(fit.client, render)) {
    REXLOG_WARN("settings_boot: the fitted window {}x{} is not the render size's aspect {:.4f}",
                fit.client.width, fit.client.height, aspect);
  }
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
  // The two identities a "my settings did not save" report needs, and which the two lines above do
  // not carry: the directory every write lands in, and the profile that chose it. The failure itself
  // names the OS reason (fs::OsErrorText), so a log then holds both halves of P3/D5's evidence.
  REXLOG_INFO("settings_boot: writes land in '{}'; profile '{}'",
              paths_.settings_file.parent_path().string(), paths_.legacy_profile.string());
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
                         ? kFallbackBackend : active_renderer;
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
  if (REXCVAR_GET(settings_safe_graphics)) {
    for (const auto& [name, value] : std::map<std::string, std::string>{
        {"resolution_scale", "1"}, {"native_2x_msaa", "false"},
        {"swap_post_effect", "none"}, {"anisotropic_override", "-1"}})
      rex::cvar::SetFlagByName(name, value);
    screen_plan_ = PlanGuestScreen(render_limits_, {1280, 720});
    overrides_.erase(SettingId::kResolution);
    if (service_) {
      auto safe = RecoverGraphics(capability_snapshot_, service_->RequestedValue(SettingId::kResolution),
                                  kFallbackBackend, true);
      service_->SetBootValues({{SettingId::kResolution, safe.resolution}, {SettingId::kRenderer, safe.renderer},
          {SettingId::kAntiAliasing, safe.anti_aliasing}, {SettingId::kFiltering, safe.filtering}},
          "settings.graphics.safe_mode");
    }
  }
  if (screen_plan_.supported && !overrides_.contains(SettingId::kResolution)) {
    // Both halves of the guest size, because the title derives its width from its height and
    // the aspect the user picked is not one the title can compute (src/hooks/render_settings.h).
    SetBootRenderFrame(screen_plan_.guest_size.width, screen_plan_.guest_size.height);
    REXLOG_INFO("settings_boot: guest render base {}x{}, draw scale {} (before graphics setup)",
                screen_plan_.guest_size.width, screen_plan_.guest_size.height,
                screen_plan_.draw_scale);
    // The HUD half of the same decision (src/hooks/hud_layout.h): how far every off-centre
    // HUD element moves to keep the pixel distance it has to its nearest edge. Exactly 0 for
    // a 16:9 base, so this line is the check that a 16:9 boot is untouched, and 320 px is the
    // number the 2560x1080 in-song capture is measured against.
    REXLOG_INFO("settings_boot: HUD re-anchor shift {:.4f} layout units, {:.1f} px",
                BootHudShiftUnits(), BootHudShiftPixels());
  }
  // The window and the presenter (P2): the frame was just decided, so the window that shows it and
  // the region the presenter scales it into are decided from the same number, here.
  ApplyWindowPolicy();
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
  active_renderer_ = active_renderer.empty() ? kFallbackBackend : active_renderer;

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
  // Provider edits share the service's transaction and recovery journal. Merely opening
  // the menu never writes the provider file; achievements use the confirmed guest action.

  // --- the guest channel's store ---------------------------------------------------------
  // With a store, the menu's edits go through the service (settings.toml, transactions, a durable
  // commit). Without one (nowhere to keep settings) the channel keeps P1's in-memory sink rather
  // than pointing at a store that could never write.
  if (sink_) {
    ::rb_blitz::settings::bridge::InstallSink(sink_.get(), sink_.get());
    // The restart prompt asks *this* rather than the sink's sticky "a restart-scoped change was
    // committed" bit: the two differ for a value changed and changed back (no prompt), and for a
    // hot row whose live write failed (prompt, and the sticky bit alone would miss it when the
    // commit itself reported no restart).
    sink_->SetRestartPendingQuery([this] { return RestartPending(); });
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
  if (!service_) return;
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
  // Include explicit flags for rows with no saved preference, and decode compound
  // settings as a group rather than parsing a backend integer as a UI token.
  for (const auto& descriptor : service_->descriptors()) {
    const auto id = descriptor.id;
    std::vector<std::string> names;
    if (id == SettingId::kTarget) names = {"ultimate_mode", "license_mask"};
    else if (id == SettingId::kResolution) names = {"resolution_scale"};
    else if (id == SettingId::kAntiAliasing) names = {"native_2x_msaa", "swap_post_effect"};
    else if (id == SettingId::kFiltering) names = {"anisotropic_override"};
    else if (id == SettingId::kDisplay) names = {"fullscreen", "borderless"};
    else if (!CvarForSetting(id).empty()) names.emplace_back(CvarForSetting(id));
    for (const auto& name : names) {
      const auto source = rex::cvar::GetFlagSource(name);
      if (source == rex::cvar::Source::kDefault) continue;
      const auto actual = rex::cvar::GetFlagByName(name);
      const bool ours = !HigherRank(source) && std::any_of(applied_.begin(), applied_.end(),
          [&](const Applied& row) { return row.cvar == name && row.expected == actual; });
      if (!ours) {
        const auto candidate = OverrideSourceFor(source);
        if (!overrides_.contains(id) || SourceRank(candidate) > SourceRank(overrides_[id]))
          overrides_[id] = candidate;
      }
    }
    if (!overrides_.contains(id) || names.empty()) continue;
    Value value = service_->EffectiveValue(id);
    const auto flag = [](const char* name) { return rex::cvar::GetFlagByName(name); };
    if (id == SettingId::kTarget) {
      value.text = flag("ultimate_mode") == "1" ? "ultimate" :
          flag("license_mask") == "0" ? "demo" : "common";
    } else if (id == SettingId::kResolution) {
      int64_t scale = 1;
      toml::ParseBareInt(flag("resolution_scale"), &scale);
      scale = std::clamp<int64_t>(scale, 1, 7);
      value.width = static_cast<int32_t>(1280 * scale);
      value.height = static_cast<int32_t>(720 * scale);
    } else if (id == SettingId::kAntiAliasing) {
      const auto post = flag("swap_post_effect");
      value.text = post == "fxaa" || post == "fxaa_extreme" ? post :
          flag("native_2x_msaa") == "true" ? "native" : "none";
    } else if (id == SettingId::kFiltering) {
      const auto actual = flag("anisotropic_override");
      for (const auto token : {"native", "none", "1x", "2x", "4x", "8x", "16x"}) {
        int32_t encoded;
        if (FilteringToCvarValue(token, &encoded) && actual == std::to_string(encoded)) value.text = token;
      }
    } else if (id == SettingId::kDisplay) {
      // Decode the pair back into one token: fullscreen wins when it is set, else borderless, else
      // the plain window.
      value.text = flag("fullscreen") == "true"    ? "fullscreen"
                   : flag("borderless") == "true" ? "borderless"
                                                  : "windowed";
    } else if (value.kind == ValueKind::kList) {
      value.list.clear();
      const std::string csv = rex::cvar::GetFlagByName(names.front());
      for (size_t start = 0; start < csv.size();) {
        const size_t end = csv.find(',', start);
        value.list.push_back(csv.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
      }
    } else {
      schema::ParseTokenToValue(descriptor, rex::cvar::GetFlagByName(names.front()), &value);
    }
    override_values_[id] = std::move(value);
  }
  service_->SetOverrides(override_values_, overrides_);
  if (REXCVAR_GET(settings_safe_graphics)) {
    for (const auto id : {SettingId::kResolution, SettingId::kRenderer,
                          SettingId::kAntiAliasing, SettingId::kFiltering}) {
      overrides_.erase(id);
      override_values_.erase(id);
    }
    service_->SetOverrides(override_values_, overrides_);
  }
  // The restart prompt's baseline: what every row's effective value is *now*, with the process
  // running it. A later `RestartPending()` compares the store against this, so a value the player
  // put back the way it was is not a pending restart.
  for (const auto& descriptor : service_->descriptors()) {
    boot_values_[descriptor.id] = service_->EffectiveValue(descriptor.id);
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

void SettingsBoot::NoteRestartRows(const std::vector<SettingId>& rows) {
  for (const SettingId id : rows) {
    if (std::find(restart_rows_noted_.begin(), restart_rows_noted_.end(), id) ==
        restart_rows_noted_.end()) {
      restart_rows_noted_.push_back(id);
    }
  }
}

bool SettingsBoot::RestartPending() const {
  if (!service_ || boot_values_.empty()) {
    return false;
  }
  // Rows an explicit source owns are not editable, so they can never be the reason for a prompt.
  std::map<SettingId, Value> current;
  for (const auto& descriptor : service_->descriptors()) {
    if (overrides_.contains(descriptor.id) || !boot_values_.contains(descriptor.id)) {
      continue;
    }
    current[descriptor.id] = service_->EffectiveValue(descriptor.id);
  }
  return !PendingRestartRows(boot_values_, current, service_->descriptors(),
                             restart_rows_noted_)
              .empty();
}

}  // namespace rb_blitz::settings::boot
