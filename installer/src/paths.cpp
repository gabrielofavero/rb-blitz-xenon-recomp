// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The installer's half of `paths.toml`: what to ask, what the answers mean, and
// whether the chosen folders can work. The file format itself comes from the game
// (`settings/paths.h`, compiled into the helper); this module is only the policy.

#include "paths.h"

#include <algorithm>
#include <system_error>
#include <utility>

#include "fs/dlc_layout.h"
#include "fs/path_policy.h"
#include "profile/profile.h"
#include "profile/profile_path.h"
#include "settings/paths.h"
#include "util.h"

namespace rb_blitz::installer {

namespace fs = std::filesystem;
namespace rbfs = rb_blitz::fs;
namespace profile = rb_blitz::profile;
namespace settings = rb_blitz::settings;

namespace {

// How much of a DLC folder the folder page is allowed to look at. Counting is
// stat-only - the per-package header read `src/fs/dlc_library.h` needs costs
// ~17 ms each and is deliberately not done here - so the ceiling is only a guard
// against a pathological tree, not a performance limit.
constexpr std::size_t kMaxDlcEntries = 4096;

std::string Display(const fs::path& path) { return Narrow(path.native()); }

// A choice is either the default (empty) or a path. A relative one resolves against
// the install folder, which is where `paths.toml` lives, so the file never depends
// on the working directory that happened to run the installer.
fs::path ResolveChoice(const std::string& value, const fs::path& install_dir) {
  fs::path path(Widen(value));
  if (path.is_relative()) {
    path = install_dir / path;
  }
  std::error_code code;
  const fs::path absolute = fs::absolute(path, code);
  return (code ? path : absolute).lexically_normal();
}

// A stored value, resolved the way the game resolves it. Kept lexical for the same
// reason settings::ResolvePathValue is: a value that does not exist yet must still
// resolve, so the page can prefill it.
fs::path ResolveStored(const std::string& value, const fs::path& file, const fs::path& fallback) {
  return settings::ResolvePathValue(value, file, fallback).lexically_normal();
}

void AddNote(PathReport* report, std::string note) { report->notes.push_back(std::move(note)); }

void Fail(PathReport* report, std::string reason) {
  report->ok = false;
  report->error = std::move(reason);
}

// What a folder holds, without opening a single package: the structured
// `<title_id>/<content_type>/<package>` tree `src/fs/dlc_layout.h` describes, or
// loose containers nested to any depth `src/fs/dlc_library.h` describes.
void ScanDlcFolder(const fs::path& root, PathReport* report) {
  std::error_code code;
  if (!DirectoryExists(root)) {
    report->dlc_layout = "new";
    return;
  }

  if (rbfs::HasStructuredLayout(root)) {
    report->dlc_layout = "structured";
    std::size_t inspected = 0;
    for (fs::directory_iterator titles(root, code); !code && titles != fs::directory_iterator();
         titles.increment(code)) {
      if (!titles->is_directory(code) || code) {
        continue;
      }
      for (fs::directory_iterator types(titles->path(), code); !code && types != fs::directory_iterator();
           types.increment(code)) {
        if (!types->is_directory(code) || code) {
          continue;
        }
        for (fs::directory_iterator packages(types->path(), code);
             !code && packages != fs::directory_iterator(); packages.increment(code)) {
          if (inspected >= kMaxDlcEntries) {
            report->dlc_packages_exact = false;
            return;
          }
          ++inspected;
          ++report->dlc_packages;
        }
      }
    }
    return;
  }

  // Not a content-root tree: a library folder, or a folder of things that are not
  // packages at all. Only the walk is done - no package header is read - so "flat"
  // says the folder holds files, not that any of them is mountable.
  std::size_t files = 0;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                            code);
  for (; !code && iterator != fs::recursive_directory_iterator(); iterator.increment(code)) {
    if (files >= kMaxDlcEntries) {
      report->dlc_packages_exact = false;
      break;
    }
    if (iterator->is_regular_file(code) && !code) {
      ++files;
    }
  }
  report->dlc_packages = files;
  report->dlc_layout = files == 0 ? "empty" : "flat";

  // A first level that is not a content-root level is the mistake
  // `DlcScanResult::Rejected` exists to report: a whole content root (whose first
  // level is a 16-digit xuid) dropped in, or a package still zipped up.
  std::error_code level_code;
  for (fs::directory_iterator entries(root, level_code);
       !level_code && entries != fs::directory_iterator(); entries.increment(level_code)) {
    if (!rbfs::IsContentRootName(entries->path().filename().string())) {
      ++report->dlc_rejected;
    }
  }
}

// Creating a folder the user asked for is not destructive; a folder that cannot be
// written to is a save that would be lost, so it is checked by writing once and
// removing the file again.
bool CheckWritable(const fs::path& dir, bool* created, std::string* error) {
  if (!DirectoryExists(dir)) {
    std::error_code code;
    if (fs::exists(dir, code) && !code) {
      SetError(error, S(Display(dir), " is a file, not a folder"));
      return false;
    }
    std::string reason;
    if (!EnsureDirectory(dir, &reason)) {
      SetError(error, S("cannot create ", Display(dir), ": ", reason));
      return false;
    }
    *created = true;
  }
  const fs::path probe = dir / ".rb-blitz-write-probe";
  std::string reason;
  if (!WriteFileText(probe, "written by the Rock Band Blitz installer, safe to delete\n", false,
                     &reason)) {
    SetError(error, S(Display(dir), " cannot be written to: ", reason));
    return false;
  }
  if (!RemoveFile(probe, &reason)) {
    log::Warn(S("could not remove the write probe ", Display(probe), ": ", reason));
  }
  return true;
}

}  // namespace

PathFileState ReadPathFile(const fs::path& install_dir, const fs::path& game_data_root) {
  PathFileState state;
  state.file = settings::ResolvePathFile(install_dir);
  if (state.file.empty()) {
    state.error = "no install folder was given";
    return state;
  }

  const settings::PathFileResult loaded = settings::LoadPathFile(state.file);
  state.has_file = loaded.status != settings::PathFileResult::Status::kMissing;
  state.valid = loaded.status == settings::PathFileResult::Status::kOk;
  state.schema_version = loaded.schema_version;
  state.error = loaded.error;
  state.dlc_raw = loaded.paths.dlc_root;
  state.save_raw = loaded.paths.user_data_root;

  const fs::path dlc_default = game_data_root.empty() ? fs::path() : game_data_root / "dlc";
  state.dlc_dir = ResolveStored(state.dlc_raw, state.file, dlc_default);
  state.save_dir = ResolveStored(state.save_raw, state.file, fs::path());
  return state;
}

LegacyPaths ReadLegacyPaths(const fs::path& install_dir, const fs::path& app_data_dir) {
  LegacyPaths result;
  if (install_dir.empty() || app_data_dir.empty()) {
    // No user side to look in: an elevated install run by an administrator must not
    // read that administrator's own Controller Settings profile and call it the player's
    // (parent plan §9.1). The caller passes an empty folder for exactly that case.
    return result;
  }

  // The game's own resolution order, including the environment override and the
  // settings-folder pointer, so a profile the user moved is still found.
  profile::ProfilePathInputs inputs = profile::ProfilePathInputsFromEnvironment(install_dir);
  inputs.app_data_dir = app_data_dir;
  result.profile = profile::ResolveProfilePath(inputs);
  if (result.profile.empty()) {
    return result;
  }

  const profile::ProfileLoadResult loaded = profile::LoadProfile(result.profile);
  // The same meaning as PathFileState::has_file: a file that is there but does not
  // parse is still a file that is there.
  result.has_file = loaded.status != profile::ProfileStatus::kMissingFile;
  result.usable = loaded.usable();
  result.error = loaded.error;
  if (!result.usable) {
    return result;
  }

  // The same two fields the game's migration imports, in the same order: the
  // `[settings]` table first, the older `[launch]` values as the fallback.
  const profile::ProfileSetting* save = loaded.profile.FindSetting("user_data_root");
  result.save_dir = save != nullptr ? save->value : loaded.profile.user_data_dir;
  const profile::ProfileSetting* dlc = loaded.profile.FindSetting("dlc_root");
  result.dlc_dir = dlc != nullptr ? dlc->value : loaded.profile.dlc_dir;
  return result;
}

PathReport ValidatePaths(const fs::path& install_dir, const PathChoice& dlc,
                         const PathChoice& save, const PathValidationOptions& options) {
  PathReport report;
  if (install_dir.empty()) {
    Fail(&report, "the install folder is empty");
    return report;
  }
  report.install_dir = fs::absolute(install_dir).lexically_normal();
  report.game_dir = (report.install_dir / "game").lexically_normal();

  // --- the DLC folder ----------------------------------------------------
  if (!dlc.present || dlc.value.empty()) {
    report.dlc_default = true;
    report.dlc_dir = report.game_dir / "dlc";
  } else {
    report.dlc_dir = ResolveChoice(dlc.value, report.install_dir);
    std::error_code code;
    if (fs::exists(report.dlc_dir, code) && !code && !DirectoryExists(report.dlc_dir)) {
      Fail(&report, S(Display(report.dlc_dir),
                      " is a file, not a folder. Pick the folder that holds your DLC, or leave "
                      "the box empty to use the game's own DLC folder."));
      return report;
    }
  }
  ScanDlcFolder(report.dlc_dir, &report);
  if (!report.dlc_default) {
    if (report.dlc_layout == "new") {
      AddNote(&report, S("the DLC folder ", Display(report.dlc_dir),
                         " does not exist yet, so the game starts with an empty library"));
    } else if (report.dlc_layout == "empty") {
      AddNote(&report, S("the DLC folder ", Display(report.dlc_dir), " holds no content package"));
    }
    if (rbfs::IsSameOrInside(report.dlc_dir, report.game_dir)) {
      AddNote(&report, "the DLC folder is inside the game data folder, which the game only "
                       "reads");
    }
  }

  // --- the save folder ---------------------------------------------------
  if (!save.present || save.value.empty()) {
    // The game's own default (a platform Documents folder) is not the installer's
    // to guess: an empty answer means "do not store one", which is also what keeps
    // a stored absolute path from outliving the machine it was chosen on.
    report.save_default = true;
    return report;
  }

  report.save_dir = ResolveChoice(save.value, report.install_dir);
  // The one refusal: the game data tree is the read-only root the runtime mounts,
  // and a save written into it is both invisible to the game (it is not a writable
  // root) and destroyed by an uninstall that removes the game data.
  report.save_inside_game = rbfs::IsSameOrInside(report.save_dir, report.game_dir);
  if (report.save_inside_game) {
    Fail(&report, S(Display(report.save_dir),
                    " is inside the game data folder ", Display(report.game_dir),
                    ", which the game only reads. Choose a folder of its own, or leave the box "
                    "empty to use the game's default save folder."));
    return report;
  }
  if (!options.create_save_folder) {
    // The page's own check: read the answer, refuse the one folder that cannot work,
    // and touch nothing. Creating the folder happens when the user moves on.
    std::error_code code;
    if (fs::exists(report.save_dir, code) && !code && !DirectoryExists(report.save_dir)) {
      Fail(&report, S(Display(report.save_dir), " is a file, not a folder"));
    }
    return report;
  }
  if (!CheckWritable(report.save_dir, &report.save_created, &report.error)) {
    report.ok = false;
    return report;
  }
  report.save_writable = true;
  if (report.save_created) {
    AddNote(&report, S("created ", Display(report.save_dir)));
  }
  if (rbfs::IsSameOrInside(report.save_dir, report.install_dir)) {
    AddNote(&report, "the save folder is inside the install folder, so the saves go with the "
                     "install folder when it is deleted");
  }
  if (!report.dlc_default && report.dlc_dir == report.save_dir) {
    AddNote(&report, "the DLC folder and the save folder are the same folder");
  }
  return report;
}

PathWriteResult WritePathFile(const fs::path& install_dir, const PathChoice& dlc,
                              const PathChoice& save) {
  PathWriteResult result;
  result.file = settings::ResolvePathFile(install_dir);
  // Nothing was answered: an update has no folder page, and the file it finds - or
  // does not find - is the one to keep (parent plan §9.1).
  if (!dlc.present && !save.present) {
    return result;
  }
  if (result.file.empty()) {
    result.error = "no install folder was given";
    return result;
  }

  const settings::PathFileResult loaded = settings::LoadPathFile(result.file);
  if (loaded.status == settings::PathFileResult::Status::kMalformed) {
    // Never overwrite a file this build cannot read: the user may have edited it,
    // and the game refuses it the same way the installer just did.
    result.error = S(Display(result.file), " was left alone: ", loaded.error);
    return result;
  }

  settings::PathEntry entry = loaded.paths;
  if (dlc.present) {
    entry.dlc_root =
        dlc.value.empty() ? std::string() : Display(ResolveChoice(dlc.value, install_dir));
  }
  if (save.present) {
    entry.user_data_root =
        save.value.empty() ? std::string() : Display(ResolveChoice(save.value, install_dir));
  }

  std::string before;
  const bool existed = loaded.status != settings::PathFileResult::Status::kMissing;
  ReadFileText(result.file, &before, nullptr);

  std::string error;
  if (!settings::SavePathFile(result.file, entry, &error)) {
    result.error = error;
    return result;
  }

  std::string after;
  ReadFileText(result.file, &after, nullptr);
  result.changed = !existed || before != after;
  result.written = result.changed;
  result.dlc_raw = entry.dlc_root;
  result.save_raw = entry.user_data_root;
  if (result.changed) {
    log::Info(S("wrote ", Display(result.file)));
  } else {
    log::Info(S(Display(result.file), " already says this; left untouched"));
  }
  return result;
}

}  // namespace rb_blitz::installer
