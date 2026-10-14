// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The three locations the wizard asks for on its install-folder page - where the
// game goes, where the DLC library is and where the saves go - and the
// `paths.toml` file the last two are stored in.
//
// The file itself is not reimplemented here: `settings::LoadPathFile` /
// `settings::SavePathFile` from the emulator tree are compiled into the helper,
// exactly the way `src/util/sha256.cpp` is, so the bytes the installer writes are
// the bytes the game reads (parent plan §4.4, "a shared SDK-free storage module
// for the game and installer where their ownership overlaps"). This module only
// decides *what* to write: which fields the caller answered, what the defaults
// are, and whether the answers are usable.
//
// Three states per field, because the wizard and an unattended install ask
// different questions (parent plan §9.1):
//
//   absent  - nobody answered (the updater, which has no folder page): leave the
//             key exactly as the file has it, and create no file at all when
//             there is none.
//   present, empty - the answer is "the default": drop the key, so the game's own
//             fallback applies instead of a copied-in absolute path.
//   present, value - store that path, in full, as a TOML literal string.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rb_blitz::installer {

// One answered location.
struct PathChoice {
  bool present = false;
  std::string value;  // UTF-8, as typed; empty means "the default"
};

// `paths.toml` as it is on disk, plus what the game would resolve out of it.
struct PathFileState {
  std::filesystem::path file;
  bool has_file = false;
  bool valid = false;  // has a schema_version this build knows
  std::int32_t schema_version = 0;
  std::string error;
  std::string dlc_raw;   // exactly what the file says
  std::string save_raw;
  std::filesystem::path dlc_dir;  // resolved the way the game resolves it
  std::filesystem::path save_dir;
};

// Reads `<install_dir>/paths.toml`. A missing file is not an error: the result then
// carries the game's own defaults, resolved against `game_data_root`.
PathFileState ReadPathFile(const std::filesystem::path& install_dir,
                           const std::filesystem::path& game_data_root);

// The path fields of the legacy Controller Settings profile - the file the old Controller Settings wrote
// and the game still reads. Only a *prefill* source (§9.1's third step); the
// migration itself is the game's own (`settings::Migrate`), not the installer's.
struct LegacyPaths {
  std::filesystem::path profile;
  bool has_file = false;
  bool usable = false;
  std::string error;
  std::string dlc_dir;   // empty when the profile does not say
  std::string save_dir;
};

// Resolves the profile the way the game does - the explicit override, the portable
// marker beside `install_dir`'s executable, the settings-directory pointer, then
// `app_data_dir` - and reads its `[settings] user_data_root/dlc_root`, falling back
// to the older `[launch] user_data_dir/dlc_dir`. An empty `app_data_dir` and no
// override means there is nowhere to look, and nothing is read.
LegacyPaths ReadLegacyPaths(const std::filesystem::path& install_dir,
                            const std::filesystem::path& app_data_dir);

// What the folder page shows and what the install refuses to start with.
struct PathReport {
  bool ok = true;
  std::string error;                 // the one thing that stops the install
  std::vector<std::string> notes;    // things worth saying, none of them fatal
  std::filesystem::path install_dir;
  std::filesystem::path game_dir;
  std::filesystem::path dlc_dir;
  std::filesystem::path save_dir;
  bool dlc_default = false;  // the answer was "the default": <game>/dlc
  bool save_default = false; // the answer was "the default": the platform folder
  // structured | flat | empty | new: what the DLC folder is, as far as a stat walk
  // can tell (no package is ever opened, which is what keeps the page instant).
  std::string dlc_layout = "new";
  std::uint64_t dlc_packages = 0;
  bool dlc_packages_exact = true;  // false when the walk hit its own ceiling
  // First-level entries of a flat-shaped DLC folder that are not content-root
  // levels - what the game would report as rejected instead of enumerating nothing.
  std::size_t dlc_rejected = 0;
  bool save_inside_game = false;
  // True only after a write probe succeeded; false when the answer was the default
  // (nothing was checked) and false when the caller asked for a read-only check.
  bool save_writable = false;
  bool save_created = false;
};

// `create_save_folder` is false for the check that runs while the folder page is
// merely *shown*: displaying a page must not have side effects, so the folder is
// created and write-probed by the check that runs when the user moves on (Next, and
// again from PrepareToInstall).
struct PathValidationOptions {
  bool create_save_folder = true;
};

// Checks the answers. `dlc` and `save` that are absent are treated as "the
// default", which is what an update means; their values are resolved against
// `install_dir` when relative, so the stored form is always a full path.
PathReport ValidatePaths(const std::filesystem::path& install_dir, const PathChoice& dlc,
                         const PathChoice& save, const PathValidationOptions& options = {});

// Commits the answers to `<install_dir>/paths.toml`. Called only after the install
// itself has succeeded, so a cancelled or failed install never replaces a working
// configuration. Unknown keys and comments survive, an unchanged file is not
// touched at all, and a write is atomic (temp sibling + replace).
struct PathWriteResult {
  bool written = false;  // the file was (re)written
  bool changed = false;  // its bytes are different from what was there
  std::filesystem::path file;
  std::string dlc_raw;
  std::string save_raw;
  std::string error;
};

PathWriteResult WritePathFile(const std::filesystem::path& install_dir, const PathChoice& dlc,
                              const PathChoice& save);

}  // namespace rb_blitz::installer
