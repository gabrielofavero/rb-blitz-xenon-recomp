// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P11a — the boot wiring: the settings service, the path file and the legacy migration, wired into
// the game's own lifecycle.
//
// P3 built the store, the path file and the service as a host-tested lane and left the five
// insertion points named in docs/plans/in-game-settings-storage.md §6 to the coordinator; P9 left
// the guest channel's sink (`docs/plans/in-game-settings-hot-apply-restart.md` §6.3) and the
// restart bootstrap to the same place. This module is that wiring, and it is deliberately *only*
// wiring: every decision it makes is a call into a module that is already tested without a boot
// (settings/service.h, settings/bridge_sink.h, settings/capabilities.h, settings/graphics_policy.h,
// settings/cvars.h, settings/paths.h).
//
// ---------------------------------------------------------------------------------------------
// Order of operations, and why each step is where it is (§4.2 / §7)
// ---------------------------------------------------------------------------------------------
//
//   1. ConfigurePaths  (OnConfigurePaths - silent, logging is not up yet)
//        identity -> paths.toml -> legacy profile -> migrate -> load settings.toml
//        apply the target and licence, the renderer, the DLC/interface rows and the two path roots
//        at the *config* rank, so the game's own rb_blitz.toml - loaded by the SDK right after this
//        hook - still outranks every one of them.
//   2. LogConfigured   (OnPostInitLogging - the first point a line goes anywhere)
//   3. AfterLoadXexImage (OnPostLoadXexImage, after ultimate::Configure)
//        the provider reaches the service; the GPU plugin's own cvars are written now, because
//        they do not exist at step 1; the guest channel is pointed at the service.
//   4. FinishInit      (OnPreLaunchModule - the last hook before the guest thread exists)
//        read the applied cvars back and record any row an explicit source won, so a row the player
//        cannot change says so instead of silently saving a value that never takes effect.
//
// The one honest limitation this module states in its own log lines rather than hiding: the
// *display* probe and the exact-render route are P4's open items, so the capability snapshot is
// built with no display enumeration and the resolution/anti-aliasing rows are recorded as not
// applied yet instead of being written from an unverified guess.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "settings/bridge_sink.h"
#include "settings/capabilities.h"
#include "settings/cvars.h"
#include "settings/graphics_policy.h"
#include "settings/migration.h"
#include "settings/model.h"
#include "settings/paths.h"
#include "settings/service.h"
#include "settings/store.h"

namespace rb_blitz::settings::boot {

// The licence masks the target translation writes. The same values the app's own default uses
// (`license_mask = 1` means "this console owns the title"), and 0 is the emulated console that owns
// nothing - the trial path (contracts §12 keeps the bit values out of the model, so the game passes
// them in).
inline constexpr uint32_t kPurchasedLicenseMask = 1;
inline constexpr uint32_t kTrialLicenseMask = 0;

// The roots the path file selects, resolved the way P3's `ResolvePathValue` resolves them (empty =
// the caller's fallback). Returned rather than applied, because the app owns `PathConfig` and this
// module never sees it.
struct PathRoots {
  bool have_dlc_root = false;
  std::string dlc_root;
  bool have_user_data_root = false;
  std::string user_data_root;
};

class SettingsBoot {
 public:
  SettingsBoot();
  ~SettingsBoot();

  SettingsBoot(const SettingsBoot&) = delete;
  SettingsBoot& operator=(const SettingsBoot&) = delete;

  // --- 1. OnConfigurePaths ----------------------------------------------------------------
  // `executable_dir` is the folder holding rb_blitz.exe: where paths.toml lives, and where a portable
  // profile marker is looked for. Returns the roots the path file names, for the caller to put into
  // its PathConfig; everything else (the cvars, the migration, the store) is done here.
  PathRoots ConfigurePaths(const std::filesystem::path& executable_dir);

  // --- 2. OnPostInitLogging ---------------------------------------------------------------
  void LogConfigured() const;

  // --- 3. OnPostLoadXexImage (after ultimate::Configure) ----------------------------------
  // `active_renderer` is the backend token this boot selected ("d3d12" / "vulkan" / empty when
  // nothing was named), which is what the capability snapshot reports as active.
  void AfterLoadXexImage(const std::string& active_renderer);

  // --- 4. OnPreLaunchModule ---------------------------------------------------------------
  void FinishInit();

  // The store. Null only when this machine has nowhere to keep settings (no profile path and no app
  // data folder), which leaves the guest channel on its in-memory stand-in.
  Service* service() { return service_.get(); }
  const Service* service() const { return service_.get(); }
  ServiceSink* sink() { return sink_.get(); }

  bool store_available() const { return store_available_; }
  const StorePaths& paths() const { return paths_; }
  const PathFileResult& path_file() const { return path_file_; }
  const MigrationResult& migration() const { return migration_; }
  DocumentStatus load_status() const { return load_status_; }
  const CapabilitySnapshot& capabilities() const { return capability_snapshot_; }
  const std::string& active_renderer() const { return active_renderer_; }

  // P10's seam: the effective main menu's rows, so the Hide Items list offers the menu that really
  // exists. The *caller* has the record bytes (the menu filter's read buffer); this forwards the
  // same list to the two tables that need it (the schema's choices and the service's descriptors).
  void SetMenuRows(std::vector<Choice> rows);

  // One row this boot applied at the config rank, and whether the value stood.
  struct Applied {
    SettingId id = SettingId::kUnknown;
    std::string cvar;
    std::string expected;
    bool verified = false;
  };
  const std::vector<Applied>& applied() const { return applied_; }
  const std::map<SettingId, Source>& overrides() const { return overrides_; }

  // The process-wide instance, for the hooks that need the store (the menu-rows seam). Owned by the
  // app; null before it is set.
  static SettingsBoot* Current();
  static void SetCurrent(SettingsBoot* boot);

 private:
  // Applies one row's cvars at the config rank. `defer_unregistered` keeps the rows whose cvar does
  // not exist yet (the GPU plugin's) for AfterLoadXexImage, which is where the plugin has loaded.
  void ApplyRow(SettingId id, bool defer_unregistered);
  void ApplyCvars(const std::vector<CvarAssignment>& assignments, SettingId id,
                  bool defer_unregistered);
  // Writes one cvar and remembers it for the read-back verification. False when the registry refused
  // it (an unknown name: a plugin cvar before its plugin loaded).
  bool WriteCvar(const std::string& name, const std::string& value, SettingId id);
  // True when an explicit source (environment / command line) already owns this cvar, so the stored
  // row must not be written over it.
  bool ExplicitlyOwned(const std::string& name, SettingId id);

  StorePaths paths_;
  PathFileResult path_file_;
  MigrationResult migration_;
  DocumentStatus load_status_ = DocumentStatus::kMissing;
  bool store_available_ = false;
  std::unique_ptr<Service> service_;
  std::unique_ptr<ServiceSink> sink_;
  GraphicsReconciler reconciler_;
  CapabilitySnapshot capability_snapshot_;
  std::vector<Applied> applied_;
  std::vector<Applied> deferred_writes_;
  // The rows staged locally, for diagnostics.
  std::map<SettingId, Source> overrides_;
  std::map<SettingId, Value> override_values_;
  std::string active_renderer_;
  std::vector<std::string> diagnostics_;  // lines to write once logging exists
};

}  // namespace rb_blitz::settings::boot
