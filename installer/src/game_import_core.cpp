// SPDX-License-Identifier: GPL-2.0-only
#include "game_import_core.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#include "util.h"

namespace rb_blitz::installer {

std::size_t GameSourcePlan::RequiredCount() const {
  return static_cast<std::size_t>(std::count_if(files.begin(), files.end(),
                                                [](const GameFilePlan& f) { return f.required; }));
}
std::size_t GameSourcePlan::PresentCount() const {
  return static_cast<std::size_t>(std::count_if(files.begin(), files.end(),
                                                [](const GameFilePlan& f) { return f.present; }));
}
std::uint64_t GameSourcePlan::PresentBytes() const {
  std::uint64_t bytes = 0;
  for (const auto& f : files) if (f.present) bytes += f.size;
  return bytes;
}
std::vector<std::string> GameSourcePlan::Missing() const {
  std::vector<std::string> missing;
  for (const auto& f : files) if (f.required && !f.present) missing.push_back(f.path);
  return missing;
}

namespace portable {
namespace {
namespace fs = std::filesystem;
using fingerprint::FileCheck;
using fingerprint::GameFingerprint;

std::string Display(const fs::path& path) { return PosixPath(path); }

fs::path StagePath(const fs::path& root) {
  fs::path name = root.filename();
  name += fs::path(".import-stage").native();
  return root.parent_path() / name;
}
fs::path BackupPath(const fs::path& root) {
  fs::path name = root.filename();
  name += fs::path(".import-backup").native();
  return root.parent_path() / name;
}

bool Exists(const fs::path& path) {
  std::error_code code;
  return fs::exists(path, code);
}

bool Rename(const fs::path& from, const fs::path& to, std::string* error) {
  std::error_code code;
  fs::rename(from, to, code);
  if (code) {
    SetError(error, S("cannot rename ", Display(from), " to ", Display(to), ": ", code.message()));
    return false;
  }
  return true;
}

bool CheckPath(const fs::path& path, std::string* error) {
  std::error_code code;
  if (fs::is_symlink(fs::symlink_status(path, code))) {
    SetError(error, S("symbolic links are not accepted in game data: ", Display(path)));
    return false;
  }
  if (code && code != std::errc::no_such_file_or_directory) {
    SetError(error, S("cannot inspect ", Display(path), ": ", code.message()));
    return false;
  }
  return true;
}

std::vector<std::string> RequiredPaths(const GameFingerprint& print,
                                       const std::vector<std::string>& roles,
                                       std::string* error) {
  std::vector<std::string> paths;
  for (const std::string& role : roles) {
    const auto* file = fingerprint::FindByRole(print, role);
    if (file == nullptr) {
      SetError(error, S("fingerprint does not describe role ", role));
      return {};
    }
    paths.push_back(file->path);
  }
  return paths;
}

bool HasFile(const fs::path& root, std::string_view relative) {
  const fs::path safe = SafeRelativePath(relative, nullptr);
  return !safe.empty() && FileExists(root / safe) && !fs::is_symlink(root / safe);
}

const StfsEntry* FindEntry(const StfsContainer& container, std::string_view path) {
  if (const StfsEntry* exact = container.Find(path); exact != nullptr && !exact->is_directory) {
    return exact;
  }
  for (const StfsEntry& entry : container.entries()) {
    if (!entry.is_directory && EqualsIgnoreCase(entry.path, path)) {
      return &entry;
    }
  }
  return nullptr;
}

void FillPlan(GameSourcePlan* plan, const GameFingerprint& print,
              const std::vector<std::string>& roles,
              const std::vector<std::string>& optional,
              const std::function<bool(std::string_view)>& present) {
  const auto add = [&](std::string_view path, std::uint64_t size, std::string sha,
                       bool required) {
    const auto found = std::find_if(plan->files.begin(), plan->files.end(),
                                    [&](const GameFilePlan& file) { return file.path == path; });
    if (found == plan->files.end()) {
      plan->files.push_back({std::string(path), size, std::move(sha), required, present(path)});
    }
  };
  for (const auto& file : print.files) {
    const bool required = std::find(roles.begin(), roles.end(), file.role) != roles.end();
    const bool extra = std::find(optional.begin(), optional.end(), file.path) != optional.end();
    if (required || extra) {
      add(file.path, file.size, file.sha256, required);
    }
  }
  for (const auto& path : optional) {
    add(path, 0, {}, false);
  }
}

bool CheckPlan(const GameSourcePlan& plan, const GameFingerprint& print,
               const std::vector<std::string>& roles,
               const std::vector<std::string>& optional, std::string* error) {
  if (plan.location.empty()) {
    SetError(error, "the source has no location");
    return false;
  }
  const auto required = RequiredPaths(print, roles, error);
  if (required.size() != roles.size()) return false;
  for (const auto& path : required) {
    const auto found = std::find_if(plan.files.begin(), plan.files.end(),
                                    [&](const GameFilePlan& file) { return file.path == path; });
    if (found == plan.files.end() || !found->present || !found->required) {
      SetError(error, S("the source is missing ", path));
      return false;
    }
  }
  for (const auto& file : plan.files) {
    if (SafeRelativePath(file.path, error).empty()) return false;
    if (std::count_if(plan.files.begin(), plan.files.end(),
                      [&](const GameFilePlan& item) { return item.path == file.path; }) != 1) {
      SetError(error, S("duplicate import file ", file.path));
      return false;
    }
    if (std::find(required.begin(), required.end(), file.path) == required.end() &&
        std::find(optional.begin(), optional.end(), file.path) == optional.end()) {
      SetError(error, S("unexpected import file ", file.path));
      return false;
    }
  }
  return true;
}

bool CopyFile(const fs::path& source, const fs::path& target,
              const std::function<bool(std::uint64_t)>& on_bytes, std::string* error) {
  if (!CheckPath(source, error) || !FileExists(source) || !EnsureParentDirectory(target, error)) {
    if (error != nullptr && error->empty()) SetError(error, S("cannot read ", Display(source)));
    return false;
  }
  std::ifstream in(source, std::ios::binary);
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!in || !out) {
    SetError(error, S("cannot copy ", Display(source), " to ", Display(target)));
    return false;
  }
  std::vector<char> bytes(1 << 20);
  std::uint64_t done = 0;
  while (in) {
    in.read(bytes.data(), bytes.size());
    const std::streamsize count = in.gcount();
    if (count > 0) {
      out.write(bytes.data(), count);
      done += static_cast<std::uint64_t>(count);
      if (on_bytes && !on_bytes(done)) {
        SetError(error, "import canceled");
        return false;
      }
    }
  }
  out.flush();
  if (!in.eof() || !out) {
    SetError(error, S("cannot complete copy of ", Display(source)));
    return false;
  }
  return true;
}

bool CopyExisting(const fs::path& source, const fs::path& stage, std::string* error) {
  if (!Exists(source)) return true;
  if (!CheckPath(source, error) || !DirectoryExists(source)) {
    SetError(error, S("existing game data is not a directory: ", Display(source)));
    return false;
  }
  std::error_code code;
  fs::recursive_directory_iterator it(source, code), end;
  for (; !code && it != end; it.increment(code)) {
    const fs::path path = it->path();
    if (!CheckPath(path, error)) return false;
    const fs::path target = stage / path.lexically_relative(source);
    if (it->is_directory(code)) {
      if (!EnsureDirectory(target, error)) return false;
    } else if (it->is_regular_file(code)) {
      if (!CopyFile(path, target, {}, error)) return false;
    } else {
      SetError(error, S("unsupported existing game-data entry: ", Display(path)));
      return false;
    }
  }
  if (code) {
    SetError(error, S("cannot copy existing game data: ", code.message()));
    return false;
  }
  return true;
}

void ProgressAt(const ImportCallbacks& callbacks, int percent, std::string_view detail) {
  if (callbacks.progress) callbacks.progress(percent, detail);
}

bool ContinueAt(const ImportCallbacks& callbacks, ImportPhase phase, std::string* error) {
  if (!callbacks.continue_at || callbacks.continue_at(phase)) return true;
  SetError(error, "import canceled");
  return false;
}
}  // namespace

bool ProbeFolder(const fs::path& folder, const GameFingerprint& print,
                 const std::vector<std::string>& roles, const std::vector<std::string>& optional,
                 GameSourcePlan* out, std::string* error) {
  if (out == nullptr || !CheckPath(folder, error) || !DirectoryExists(folder)) {
    SetError(error, S("not a game source folder: ", Display(folder)));
    return false;
  }
  const auto required = RequiredPaths(print, roles, error);
  if (required.size() != roles.size()) return false;
  std::vector<fs::path> candidates{folder};
  std::error_code code;
  for (fs::directory_iterator it(folder, code), end; !code && it != end; it.increment(code)) {
    if (it->is_directory(code) && !it->is_symlink(code)) candidates.push_back(it->path());
  }
  if (code) {
    SetError(error, S("cannot inspect folder: ", code.message()));
    return false;
  }
  fs::path best;
  std::size_t count = 0;
  for (const auto& path : candidates) {
    const auto found = static_cast<std::size_t>(std::count_if(required.begin(), required.end(),
                                   [&](const std::string& file) { return HasFile(path, file); }));
    if (best.empty() || found > count) { best = path; count = found; }
    if (found == required.size()) break;
  }
  if (count != required.size()) {
    std::vector<std::string> missing;
    for (const auto& path : required) if (!HasFile(best, path)) missing.push_back(path);
    SetError(error, S("cannot find the supported Rock Band Blitz files in ", Display(best),
                     " (", count, " of ", required.size(), " found, missing ",
                     Join(missing, ", "), ")"));
    return false;
  }
  GameSourcePlan plan;
  plan.kind = GameSourceKind::kFolder;
  plan.location = best;
  FillPlan(&plan, print, roles, optional,
           [&](std::string_view path) { return HasFile(best, path); });
  plan.description = S("folder ", Display(best));
  *out = std::move(plan);
  return true;
}

bool ProbePackage(const fs::path& package, const GameFingerprint& print,
                  const std::vector<std::string>& roles, const std::vector<std::string>& optional,
                  GameSourcePlan* out, std::string* error) {
  if (out == nullptr || !CheckPath(package, error) || !FileExists(package)) {
    SetError(error, S("not a package file: ", Display(package)));
    return false;
  }
  if (RequiredPaths(print, roles, error).size() != roles.size()) return false;
  StfsContainer container;
  if (!container.Open(package, error)) return false;
  GameSourcePlan plan;
  plan.kind = GameSourceKind::kPackage;
  plan.location = package;
  plan.volume = container.info();
  plan.package_entries = container.entries().size();
  FillPlan(&plan, print, roles, optional,
           [&](std::string_view path) { return FindEntry(container, path) != nullptr; });
  if (!plan.Complete()) {
    SetError(error, S(Display(package), " does not hold the supported Rock Band Blitz content; missing ",
                     Join(plan.Missing(), ", ")));
    return false;
  }
  plan.description = S("package ", Display(package), " (title ",
                       HexU32(plan.volume.title_id), ")");
  *out = std::move(plan);
  return true;
}

bool ProbePickedSource(const SourcePicker& picker, const GameFingerprint& print,
                       const std::vector<std::string>& roles, const std::vector<std::string>& optional,
                       GameSourcePlan* out, std::string* error) {
  if (!picker) { SetError(error, "no source picker supplied"); return false; }
  const fs::path picked = picker();
  if (picked.empty()) { SetError(error, "source selection canceled"); return false; }
  if (DirectoryExists(picked)) return ProbeFolder(picked, print, roles, optional, out, error);
  return ProbePackage(picked, print, roles, optional, out, error);
}

bool VerifyTree(const fs::path& root, const GameFingerprint& print,
                const std::vector<std::string>& roles, std::string* evidence,
                std::string* error) {
  if (!DirectoryExists(root)) {
    SetError(error, S(Display(root), " does not exist"));
    return false;
  }
  std::vector<FileCheck> checks;
  if (!fingerprint::VerifyRoles(print, root, roles, &checks, error)) return false;
  std::vector<std::string> lines;
  for (const auto& check : checks) lines.push_back(fingerprint::FormatCheckLine(check));
  if (evidence) *evidence = Join(lines, "\n");
  if (fingerprint::AllMatched(checks)) return true;
  SetError(error, S("the game data does not match the supported dump:\n", Join(lines, "\n")));
  return false;
}

bool RecoverImport(const fs::path& root, const GameFingerprint& print,
                   const std::vector<std::string>& roles, std::string* error) {
  const fs::path stage = StagePath(root), backup = BackupPath(root);
  if (Exists(backup)) {
    std::string ignored;
    if (Exists(root) && VerifyTree(root, print, roles, nullptr, &ignored)) {
      if (!RemoveTree(backup, error)) return false;
    } else {
      if (Exists(root) && !RemoveTree(root, error)) return false;
      if (!Rename(backup, root, error)) return false;
    }
  }
  return RemoveTree(stage, error);
}

bool Import(const GameSourcePlan& plan, const fs::path& root, const GameFingerprint& print,
            const std::vector<std::string>& roles, const std::vector<std::string>& optional,
            const ImportCallbacks& callbacks, std::string* error) {
  if (!CheckPlan(plan, print, roles, optional, error)) return false;
  if (root.empty() || root.filename().empty()) {
    SetError(error, "the game-data destination is empty"); return false;
  }
  if (plan.kind == GameSourceKind::kFolder &&
      (PathIsWithin(plan.location, root) || PathIsWithin(root, plan.location))) {
    SetError(error, "source folder overlaps the game-data destination"); return false;
  }
  if (!EnsureDirectory(root.parent_path(), error) || !RecoverImport(root, print, roles, error))
    return false;
  const fs::path stage = StagePath(root), backup = BackupPath(root);
  if (!EnsureDirectory(stage, error) || !CopyExisting(root, stage, error)) {
    RemoveTree(stage, nullptr); return false;
  }
  StfsContainer container;
  if (plan.kind == GameSourceKind::kPackage && !container.Open(plan.location, error)) {
    RemoveTree(stage, nullptr); return false;
  }
  const std::uint64_t total = plan.PresentBytes();
  std::uint64_t copied_bytes = 0;
  const auto report_copy = [&](std::string_view detail) {
    const int percent = total == 0 ? 75 : static_cast<int>(
        75.0L * static_cast<long double>(std::min(copied_bytes, total)) /
        static_cast<long double>(total));
    ProgressAt(callbacks, percent, detail);
  };
  ProgressAt(callbacks, 0, plan.kind == GameSourceKind::kFolder
                               ? "copying game data" : "unpacking the package");
  for (const auto& file : plan.files) {
    if (callbacks.cancelled && callbacks.cancelled()) {
      SetError(error, "import canceled");
      RemoveTree(stage, nullptr); return false;
    }
    if (!file.present) continue;
    const fs::path relative = SafeRelativePath(file.path, error);
    if (relative.empty()) { RemoveTree(stage, nullptr); return false; }
    const fs::path target = stage / relative;
    if (plan.kind == GameSourceKind::kFolder) {
      std::uint64_t reported = 0;
      if (!CopyFile(plan.location / relative, target,
                    [&](std::uint64_t bytes) {
                      copied_bytes += bytes - reported;
                      reported = bytes;
                      report_copy(file.path);
                      return !callbacks.cancelled || !callbacks.cancelled();
                    }, error)) {
        RemoveTree(stage, nullptr); return false;
      }
    } else {
      const StfsEntry* entry = FindEntry(container, file.path);
      if (entry == nullptr) {
        SetError(error, S("package changed: ", file.path));
        RemoveTree(stage, nullptr); return false;
      }
      FileSink sink;
      if (!sink.Open(target, error)) { RemoveTree(stage, nullptr); return false; }
      const Sink write = [&](const std::uint8_t* data, std::size_t size) {
        if (callbacks.cancelled && callbacks.cancelled()) {
          SetError(error, "import canceled");
          return false;
        }
        if (!sink(data, size)) return false;
        copied_bytes += size;
        report_copy(file.path);
        return true;
      };
      const bool extracted = container.Extract(*entry, write, error);
      std::string close_error;
      const bool closed = sink.Close(&close_error);
      if (!extracted || !closed) {
        if (error && error->empty())
          SetError(error, !sink.error().empty() ? sink.error() : close_error);
        RemoveTree(stage, nullptr); return false;
      }
    }
    report_copy(file.path);
  }
  ProgressAt(callbacks, 75, "checking imported files");
  if (!VerifyTree(stage, print, roles, nullptr, error) ||
      !ContinueAt(callbacks, ImportPhase::kStaged, error)) {
    RemoveTree(stage, nullptr); return false;
  }
  ProgressAt(callbacks, 95, "installing game data");
  const bool had_old = Exists(root);
  if (had_old && !Rename(root, backup, error)) { RemoveTree(stage, nullptr); return false; }
  if (!ContinueAt(callbacks, ImportPhase::kOldMoved, error)) {
    if (had_old) Rename(backup, root, nullptr);
    RemoveTree(stage, nullptr); return false;
  }
  if (!Rename(stage, root, error)) {
    if (had_old) Rename(backup, root, nullptr);
    RemoveTree(stage, nullptr); return false;
  }
  if (!ContinueAt(callbacks, ImportPhase::kNewActivated, error)) {
    RemoveTree(root, nullptr);
    if (had_old) Rename(backup, root, nullptr);
    return false;
  }
  if (had_old && !RemoveTree(backup, error)) return false;
  ProgressAt(callbacks, 100, "installed");
  return true;
}
}  // namespace portable
}  // namespace rb_blitz::installer
