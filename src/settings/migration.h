// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — legacy migration (contracts §4.3/§5.6). It imports the launcher profile's supported
// native fields into settings.toml, translates the ones whose meaning changed, writes the
// path file, and keeps everything it does not own — the controller tables, unknown keys,
// comments — exactly where it was. The legacy file gets a byte-identical backup before the
// first write, and migration is idempotent and staged so an interruption between the two
// files is finished on the next boot rather than restarted.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "launcher/profile.h"
#include "settings/paths.h"
#include "settings/store.h"

namespace rb_blitz::settings {

enum class MigrationStatus : uint8_t {
  kNoLegacy,
  kMigrated,
  kAlreadyCurrent,
  kInterruptedResumed,
  kMalformedLegacy,
};

struct MigrationStats {
  std::size_t imported = 0;
  std::size_t translated = 0;
  std::size_t dropped = 0;
  std::size_t preserved = 0;
  bool target_imported = false;
  bool paths_imported = false;
};

struct MigrationResult {
  MigrationStatus status = MigrationStatus::kNoLegacy;
  MigrationStats stats;
  std::vector<std::string> diagnostics;  // includes the intentional native+FXAA loss
  std::filesystem::path backup;          // byte-identical legacy backup, before any write
  bool wrote_settings = false;           // partial-failure recovery needs both flags
  bool wrote_paths = false;
};

// Extra knobs the 3-argument form defaults; kept separate so the frozen signature stays.
struct MigrationOptions {
  // False when the legacy profile belongs to a different installation: the path fields are
  // not applied, so migrating one install cannot redirect another (parent plan §4.1).
  bool apply_paths = true;
  // False for a legacy file that was read but is not usable (kMalformed).
  bool legacy_usable = true;
};

// The frozen P0 signature (contracts §5.6): imports into `store` and writes.
MigrationResult Migrate(const rb_blitz::launcher::Profile& legacy, Store& store,
                        const PathEntry& existing_paths);

MigrationResult Migrate(const rb_blitz::launcher::Profile& legacy, Store& store,
                        const PathEntry& existing_paths, const MigrationOptions& options);

}  // namespace rb_blitz::settings
