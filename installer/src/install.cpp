// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the install steps described in install.h.
//
// Every step here follows the same shape: materialise the component into
// <install>/.staging, verify it there, and only then move it into place. A
// failure therefore costs time and nothing else - the previous install, if any,
// is still exactly as the user left it.

#include "install.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <windows.h>

#include "download.h"
#include "util.h"
#include "util/game_fingerprint.h"
#include "zip.h"

namespace rb_blitz::installer {
namespace {

namespace fs = std::filesystem;
using fingerprint::FileCheck;
using fingerprint::GameFingerprint;

// --- paths and names ------------------------------------------------------

fs::path PathOf(std::string_view name) { return fs::path(Widen(name)); }

std::string Display(const fs::path& path) { return Narrow(path.native()); }

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

std::string ShortHash(const std::string& sha256) {
  return sha256.size() > 12 ? sha256.substr(0, 12) + "..." : sha256;
}

template <std::size_t N>
std::vector<std::string> Roles(const std::string_view (&roles)[N]) {
  return std::vector<std::string>(std::begin(roles), std::end(roles));
}

// --- cleanup --------------------------------------------------------------

// Removes a staging directory when the enclosing scope ends, committed or not.
// Staging is always disposable: everything in it can be produced again from the
// source the user pointed at.
class RemoveOnExit {
 public:
  explicit RemoveOnExit(fs::path path) : path_(std::move(path)) {}
  ~RemoveOnExit() { RemoveTree(path_, nullptr); }

 private:
  fs::path path_;
};

// --- progress -------------------------------------------------------------

// Maps "bytes done" onto a slice of the 0-100 range the wizard polls, and only
// writes when the integer percent or the label changes: a 400 MB copy would
// otherwise append thousands of identical lines to the progress file.
class StepProgress {
 public:
  StepProgress(const ProgressSink& sink, std::uint64_t total, int base, int span)
      : sink_(&sink), total_(total), base_(base), span_(span) {}

  void Report(std::uint64_t done, std::string_view label) {
    const int percent =
        total_ == 0 ? base_ + span_
                    : base_ + static_cast<int>(std::min<std::uint64_t>(
                                  static_cast<std::uint64_t>(span_), (done * span_) / total_));
    if (percent == last_percent_ && label == last_label_) {
      return;
    }
    last_percent_ = percent;
    last_label_.assign(label);
    sink_->Report(percent, label);
  }

 private:
  const ProgressSink* sink_;
  std::uint64_t total_ = 0;
  int base_ = 0;
  int span_ = 100;
  int last_percent_ = -1;
  std::string last_label_;
};

// --- embedded fingerprints -------------------------------------------------

const GameFingerprint& GameFingerprintConfig() {
  static const GameFingerprint value = [] {
    GameFingerprint print;
    std::string reason;
    if (!fingerprint::ParseGameFingerprint(EmbeddedGameFingerprints(), &print, &reason)) {
      throw std::runtime_error("the embedded game fingerprints are invalid: " + reason);
    }
    return print;
  }();
  return value;
}

const GameFingerprint& UltimateFingerprintConfig() {
  static const GameFingerprint value = [] {
    GameFingerprint print;
    std::string reason;
    if (!fingerprint::ParseGameFingerprint(EmbeddedUltimateFingerprints(), &print, &reason)) {
      throw std::runtime_error("the embedded Ultimate fingerprints are invalid: " + reason);
    }
    return print;
  }();
  return value;
}

// --- verification ---------------------------------------------------------

std::string FingerprintLines(const std::vector<FileCheck>& checks) {
  std::vector<std::string> lines;
  lines.reserve(checks.size());
  for (const FileCheck& check : checks) {
    lines.push_back(fingerprint::FormatCheckLine(check));
  }
  return Join(lines, "\n");
}

void LogChecks(const std::vector<FileCheck>& checks) {
  for (const FileCheck& check : checks) {
    log::Detail(fingerprint::FormatCheckLine(check));
  }
}

// Verifies `roles` under `root` and fails the whole step on any mismatch: this is
// used where a wrong file is worse than no file at all.
bool RequireMatch(const GameFingerprint& print, const fs::path& root,
                  const std::vector<std::string>& roles, std::string_view subject,
                  std::string* evidence, std::string* error) {
  std::vector<FileCheck> checks;
  if (!fingerprint::VerifyRoles(print, root, roles, &checks, error)) {
    return false;
  }
  const std::string lines = FingerprintLines(checks);
  if (evidence != nullptr) {
    *evidence = lines;
  }
  LogChecks(checks);
  if (fingerprint::AllMatched(checks)) {
    return true;
  }
  SetError(error, S("the ", subject, " does not match the supported dump:\n", lines));
  return false;
}

bool MissingOrUnreadable(const std::vector<FileCheck>& checks) {
  return std::any_of(checks.begin(), checks.end(), [](const FileCheck& check) {
    return check.verdict == fingerprint::FileVerdict::kMissing ||
           check.verdict == fingerprint::FileVerdict::kUnreadable;
  });
}

// The paths the fingerprint file records for `roles`, in order. Used both to
// identify a candidate game root and to name the files a source must contain.
std::vector<std::string> PathsForRoles(const GameFingerprint& print,
                                       const std::vector<std::string>& roles) {
  std::vector<std::string> paths;
  for (const std::string& role : roles) {
    if (const fingerprint::FileFingerprint* file = fingerprint::FindByRole(print, role)) {
      paths.push_back(file->path);
    }
  }
  return paths;
}

bool IsOptionalGameFile(std::string_view path) {
  return std::find(std::begin(kOptionalGameFiles), std::end(kOptionalGameFiles), path) !=
         std::end(kOptionalGameFiles);
}

// --- file system ----------------------------------------------------------

bool PresentIn(const fs::path& root, std::string_view relative) {
  const fs::path safe = SafeRelativePath(relative, nullptr);
  return !safe.empty() && FileExists(root / safe);
}

std::size_t CountPresent(const fs::path& root, const std::vector<std::string>& relatives,
                         std::vector<std::string>* missing) {
  std::size_t present = 0;
  for (const std::string& relative : relatives) {
    if (PresentIn(root, relative)) {
      ++present;
    } else if (missing != nullptr) {
      missing->push_back(relative);
    }
  }
  return present;
}

void AddPlanFile(GameSourcePlan* plan, std::string_view path, std::uint64_t size, std::string sha256,
                 bool required, bool present) {
  for (const GameFilePlan& existing : plan->files) {
    if (existing.path == path) {
      return;
    }
  }
  GameFilePlan file;
  file.path = std::string(path);
  file.size = size;
  file.sha256 = std::move(sha256);
  file.required = required;
  file.present = present;
  plan->files.push_back(std::move(file));
}

// Shared by both planners: the three fingerprinted files that identify the dump,
// then the extras that make an imported root look like the user's own.
void FillPlanFiles(const GameFingerprint& print, const fs::path& root, GameSourcePlan* plan) {
  const std::vector<std::string> roles = Roles(kGameRoles);
  for (const fingerprint::FileFingerprint& file : print.files) {
    const bool required = std::find(roles.begin(), roles.end(), file.role) != roles.end();
    if (!required && !IsOptionalGameFile(file.path)) {
      continue;
    }
    AddPlanFile(plan, file.path, file.size, file.sha256, required, PresentIn(root, file.path));
  }
  for (const std::string_view path : kOptionalGameFiles) {
    AddPlanFile(plan, path, 0, {}, false, PresentIn(root, path));
  }
}

// Exact match first, then case-insensitively: the file table of a real package
// uses the same casing as the dump, but nothing in the format enforces it.
const StfsEntry* FindStfsEntry(const StfsContainer& container, std::string_view path) {
  if (const StfsEntry* entry = container.Find(path); entry != nullptr && !entry->is_directory) {
    return entry;
  }
  for (const StfsEntry& entry : container.entries()) {
    if (!entry.is_directory && EqualsIgnoreCase(entry.path, path)) {
      return &entry;
    }
  }
  return nullptr;
}

bool CopyOne(const fs::path& source, const fs::path& destination, std::string* error) {
  if (!EnsureParentDirectory(destination, error)) {
    return false;
  }
  std::error_code code;
  fs::copy_file(source, destination, fs::copy_options::overwrite_existing, code);
  if (code) {
    SetError(error, S("cannot copy ", Display(source), " to ", Display(destination), ": ",
                     code.message()));
    return false;
  }
  return true;
}

bool MoveOne(const fs::path& source, const fs::path& destination, std::string* error) {
  if (!EnsureParentDirectory(destination, error)) {
    return false;
  }
  std::error_code code;
  fs::rename(source, destination, code);
  if (!code) {
    return true;
  }
  // rename() is refused across volumes and when the target already exists, so
  // fall back to a copy: importing from a package on another drive still lands
  // in one pass, and re-importing over an existing install overwrites in place.
  code.clear();
  fs::copy_file(source, destination, fs::copy_options::overwrite_existing, code);
  if (code) {
    SetError(error, S("cannot move ", Display(source), " to ", Display(destination), ": ",
                     code.message()));
    return false;
  }
  std::error_code ignored;
  fs::remove(source, ignored);
  return true;
}

// The sink knows why a write failed; the container only knows that its sink
// refused, so the sink's own message wins when there is one.
std::string WriteFailureReason(const FileSink& sink, const std::string& close_reason,
                              const std::string* error) {
  if (!close_reason.empty()) {
    return close_reason;
  }
  if (!sink.error().empty()) {
    return sink.error();
  }
  return error != nullptr ? *error : std::string("the write failed");
}

std::vector<fs::path> Children(const fs::path& directory, std::string* error) {
  std::vector<fs::path> children;
  std::error_code code;
  fs::directory_iterator iterator(directory, code);
  const fs::directory_iterator end;
  for (; !code && iterator != end; iterator.increment(code)) {
    children.push_back(iterator->path());
  }
  if (code) {
    SetError(error, S("cannot read ", Display(directory), ": ", code.message()));
  }
  return children;
}

bool MoveTreeInner(const fs::path& source, const fs::path& destination, std::size_t* moved,
                   std::string* error) {
  if (!EnsureDirectory(destination, error)) {
    return false;
  }
  const std::vector<fs::path> children = Children(source, error);
  if (!error) {
    return false;
  }
  std::size_t count = 0;
  for (const fs::path& child : children) {
    const fs::path target = destination / child.filename();
    if (DirectoryExists(child)) {
      // A directory rename moves the whole subtree at once, which is what makes
      // the commit of a 370 MB import instant.
      std::error_code code;
      if (!DirectoryExists(target)) {
        fs::rename(child, target, code);
        if (!code) {
          continue;
        }
      }
      std::size_t nested = 0;
      if (!MoveTreeInner(child, target, &nested, error)) {
        return false;
      }
      count += nested;
      continue;
    }
    if (!MoveOne(child, target, error)) {
      return false;
    }
    ++count;
  }
  if (moved != nullptr) {
    *moved += count;
  }
  return true;
}

// Moves everything below `source` into `destination`, merging directories.
bool MoveTree(const fs::path& source, const fs::path& destination, std::size_t* moved,
              std::string* error) {
  std::size_t count = 0;
  if (!MoveTreeInner(source, destination, &count, error)) {
    return false;
  }
  if (moved != nullptr) {
    *moved = count;
  }
  return true;
}

// `relative` is the position of `source` inside the copied tree, so `skip` sees
// the same '/'-separated path a zip entry would have carried.
bool CopyTreeInner(const fs::path& source, const fs::path& destination, const fs::path& relative,
                   const std::function<bool(std::string_view)>& skip, std::size_t* copied,
                   std::string* error) {
  if (!EnsureDirectory(destination, error)) {
    return false;
  }
  const std::vector<fs::path> children = Children(source, error);
  if (!error) {
    return false;
  }
  std::size_t count = 0;
  for (const fs::path& child : children) {
    const fs::path child_relative =
        relative.empty() ? fs::path(child.filename()) : relative / child.filename();
    const fs::path target = destination / child.filename();
    if (DirectoryExists(child)) {
      std::size_t nested = 0;
      if (!CopyTreeInner(child, target, child_relative, skip, &nested, error)) {
        return false;
      }
      count += nested;
      continue;
    }
    if (skip != nullptr && skip(PosixPath(child_relative))) {
      continue;
    }
    if (!CopyOne(child, target, error)) {
      return false;
    }
    ++count;
  }
  if (copied != nullptr) {
    *copied += count;
  }
  return true;
}

// Copies a tree the installer does not own: the user's own folder is never
// modified, which is why a folder source is copied into staging instead of moved.
bool CopyTree(const fs::path& source, const fs::path& destination,
              const std::function<bool(std::string_view)>& skip, std::size_t* copied,
              std::string* error) {
  std::size_t count = 0;
  if (!CopyTreeInner(source, destination, {}, skip, &count, error)) {
    return false;
  }
  if (copied != nullptr) {
    *copied = count;
  }
  return true;
}

void MeasureTree(const fs::path& root, std::size_t* files, std::uint64_t* bytes) {
  std::size_t count = 0;
  std::uint64_t total = 0;
  std::error_code code;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                           code);
  const fs::recursive_directory_iterator end;
  for (; !code && iterator != end; iterator.increment(code)) {
    if (iterator->is_directory(code)) {
      continue;
    }
    ++count;
    total += FileSizeOrZero(iterator->path());
  }
  if (files != nullptr) {
    *files = count;
  }
  if (bytes != nullptr) {
    *bytes = total;
  }
}

// The first candidate directory holding every file in `markers`: a preferred
// subdirectory (the archive prefix), the root itself, or any immediate
// subdirectory. Payload archives and the Ultimate release are both routinely
// packed with one wrapper folder, and the user may have picked either level.
fs::path FindTreeWithFiles(const fs::path& root, const fs::path& preferred,
                           const std::vector<std::string>& markers) {
  std::vector<fs::path> candidates;
  if (!preferred.empty()) {
    candidates.push_back(root / preferred);
  }
  candidates.push_back(root);
  for (const fs::path& child : Children(root, nullptr)) {
    if (DirectoryExists(child)) {
      candidates.push_back(child);
    }
  }
  for (const fs::path& candidate : candidates) {
    if (DirectoryExists(candidate) && CountPresent(candidate, markers, nullptr) == markers.size()) {
      return candidate;
    }
  }
  return {};
}

bool Overlaps(const fs::path& left, const fs::path& right) {
  return PathIsWithin(left, right) || PathIsWithin(right, left);
}

// --- archives -------------------------------------------------------------

std::string ArchiveNameFromUrl(std::string_view url) {
  const std::size_t query = url.find_first_of("?#");
  const std::string_view trimmed = url.substr(0, query);
  const std::size_t slash = trimmed.find_last_of('/');
  const std::string_view name = slash == std::string_view::npos ? trimmed : trimmed.substr(slash + 1);
  const fs::path safe = SafeRelativePath(name, nullptr);
  return safe.empty() ? "download.zip" : Narrow(safe.native());
}

// The Ultimate release ships a default.xex that must never be installed - the
// payload reproduces its difference on the host - whether it arrives inside the
// release archive or in a folder the user extracted themselves.
bool RejectedUltimateFile(std::string_view relative_posix) {
  const std::size_t slash = relative_posix.find_last_of('/');
  const std::string_view name =
      slash == std::string_view::npos ? relative_posix : relative_posix.substr(slash + 1);
  return EqualsIgnoreCase(name, "default.xex");
}

// The release archive wraps everything in one folder, so extraction keeps that
// subtree (and drops the file above).
std::function<bool(const ZipEntry&)> UltimateFilter(const std::string& prefix) {
  return [prefix](const ZipEntry& entry) {
    if (RejectedUltimateFile(entry.name)) {
      return false;
    }
    if (prefix.empty()) {
      return true;
    }
    return EqualsIgnoreCase(entry.name, prefix) ||
           (entry.name.size() > prefix.size() && StartsWith(entry.name, prefix) &&
            entry.name[prefix.size()] == '/');
  };
}

bool ExtractArchive(const fs::path& archive, const fs::path& staging,
                    const std::function<bool(const ZipEntry&)>& filter, const ProgressSink& progress,
                    std::size_t* files, std::uint64_t* bytes, std::string* error) {
  ZipArchive zip;
  if (!zip.Open(archive, error)) {
    return false;
  }
  // An unset filter accepts every entry, the convention ZipArchive::ExtractAll already
  // follows, so the byte total has to agree with it.
  std::uint64_t total = 0;
  for (const ZipEntry& entry : zip.entries()) {
    if (!entry.is_directory && (!filter || filter(entry))) {
      total += entry.uncompressed_size;
    }
  }
  std::size_t count = 0;
  std::uint64_t done = 0;
  StepProgress step(progress, total, 0, 100);
  step.Report(0, "extracting");
  const bool extracted = zip.ExtractAll(
      staging, filter,
      [&step, &done, &count](const ZipEntry& entry) {
        if (entry.is_directory) {
          return;
        }
        done += entry.uncompressed_size;
        ++count;
        step.Report(done, entry.name);
      },
      error);
  if (!extracted) {
    return false;
  }
  progress.Report(100, S(count, " files extracted"));
  if (files != nullptr) {
    *files = count;
  }
  if (bytes != nullptr) {
    *bytes = total;
  }
  return true;
}

// Extracts or copies `source` into `staging` for the two components that can be
// fetched or picked, and returns the tree the component's files ended up in.
bool Materialise(const SourceRef& source, const fs::path& staging,
                 const fs::path& preferred_tree, const std::vector<std::string>& markers,
                 const std::function<bool(const ZipEntry&)>& filter,
                 const std::function<bool(std::string_view)>& skip, const ProgressSink& progress,
                 const std::string& subject, fs::path* tree, std::string* error) {
  switch (source.kind) {
    case SourceKind::kZip:
    case SourceKind::kUrl: {
      fs::path archive = source.path;
      if (source.kind == SourceKind::kUrl) {
        archive = staging / PathOf(ArchiveNameFromUrl(source.url));
        DownloadRequest request;
        request.url = source.url;
        request.destination = archive;
        request.expected_size = source.size;
        request.expected_sha256 = source.sha256;
        request.progress_file = progress.file;
        log::Info(S("downloading ", source.url));
        DownloadResult result;
        if (!DownloadToFile(request, &result, error)) {
          return false;
        }
        log::Info(S("downloaded ", HumanBytes(result.size), " (sha256 ", ShortHash(result.sha256),
                    ")"));
      } else if (!FileExists(archive)) {
        SetError(error, S(Display(archive), " does not exist"));
        return false;
      }
      if (!ExtractArchive(archive, staging, filter, progress, nullptr, nullptr, error)) {
        return false;
      }
      if (source.kind == SourceKind::kUrl) {
        // The download only had to live next to the files while the archive was
        // being opened. A payload zip holds its files at the root, so the tree
        // the caller moves would otherwise carry the 15 MB archive into the
        // install directory as well.
        RemoveFile(archive, nullptr);
      }
      break;
    }
    case SourceKind::kFolder: {
      const fs::path found = FindTreeWithFiles(source.path, preferred_tree, markers);
      if (found.empty()) {
        SetError(error, S("the selected folder does not contain ", Join(markers, " or "),
                         " - pick the folder that holds the extracted Xbox 360 game"));
        return false;
      }
      progress.Report(0, "copying files");
      if (!CopyTree(found, staging, skip, nullptr, error)) {
        return false;
      }
      progress.Report(100, "files copied");
      break;
    }
    case SourceKind::kEmbedded:
      SetError(error, S("the ", subject, " is never bundled with this installer"));
      return false;
  }

  const fs::path found = FindTreeWithFiles(staging, preferred_tree, markers);
  if (found.empty()) {
    SetError(error, S("the ", subject, " does not contain ", Join(markers, " or ")));
    return false;
  }
  *tree = found;
  return true;
}

// --- the payload ----------------------------------------------------------

std::string EmbeddedDescription() { return DescribeSource(SourceRef{}); }

// Both payload paths end here: every file the manifest lists must be present and
// match. `origin` names where the files came from, for the failure message.
bool VerifyPayloadTree(const fs::path& root, const Pins& pins, std::string_view origin,
                       ComponentResult* out, std::string* error) {
  const fs::path manifest = root / PathOf(kPayloadManifestName);
  std::string text;
  std::string reason;
  if (!ReadFileText(manifest, &text, &reason)) {
    SetError(error, S("cannot read the build manifest ", Display(manifest), ": ", reason));
    return false;
  }
  GameFingerprint print;
  if (!fingerprint::ParseGameFingerprint(text, &print, &reason)) {
    SetError(error, S("the build manifest ", Display(manifest), " is malformed: ", reason));
    return false;
  }
  std::vector<FileCheck> checks;
  if (!fingerprint::VerifyRoles(print, root, {}, &checks, error)) {
    return false;
  }

  ComponentResult result;
  result.version = pins.payload.version.empty() ? print.name : pins.payload.version;
  result.source = EmbeddedDescription();
  result.files = checks.size();
  for (const FileCheck& check : checks) {
    result.bytes += check.actual_size;
  }
  LogChecks(checks);
  if (!fingerprint::AllMatched(checks)) {
    result.fingerprints_matched = false;
    if (out != nullptr) {
      *out = result;
    }
    SetError(error, S("the recompiled build ", origin,
                     " does not match the release it came from:\n", FingerprintLines(checks),
                     "\nDownload the installer again and re-run it."));
    return false;
  }
  log::Info(S("build verified: ", result.files, " files, ", HumanBytes(result.bytes)));
  if (out != nullptr) {
    *out = result;
  }
  return true;
}

// --- the Ultimate mod -----------------------------------------------------


}  // namespace

// --- layout ---------------------------------------------------------------

std::filesystem::path GameRoot(const std::filesystem::path& app_dir) {
  return app_dir / PathOf(kGameDirName);
}

std::filesystem::path StagingRoot(const std::filesystem::path& app_dir) {
  return app_dir / PathOf(kStagingDirName);
}

void ProgressSink::Report(int percent) const { Report(percent, {}); }

void ProgressSink::Report(int percent, std::string_view detail) const {
  if (file.empty()) {
    return;
  }
  WriteProgressFile(file, percent, detail);
}

// --- game data ------------------------------------------------------------

std::size_t GameSourcePlan::RequiredCount() const {
  return static_cast<std::size_t>(std::count_if(files.begin(), files.end(),
                                                [](const GameFilePlan& file) { return file.required; }));
}

std::size_t GameSourcePlan::PresentCount() const {
  return static_cast<std::size_t>(std::count_if(files.begin(), files.end(),
                                                [](const GameFilePlan& file) { return file.present; }));
}

std::uint64_t GameSourcePlan::PresentBytes() const {
  std::uint64_t total = 0;
  for (const GameFilePlan& file : files) {
    if (file.present) {
      total += file.size;
    }
  }
  return total;
}

std::vector<std::string> GameSourcePlan::Missing() const {
  std::vector<std::string> missing;
  for (const GameFilePlan& file : files) {
    if (file.required && !file.present) {
      missing.push_back(file.path);
    }
  }
  return missing;
}

bool PlanGameFolder(const std::filesystem::path& folder, GameSourcePlan* out, std::string* error) {
  if (out == nullptr) {
    SetError(error, "PlanGameFolder needs somewhere to write the plan");
    return false;
  }
  if (!DirectoryExists(folder)) {
    SetError(error, S(Display(folder), " is not a folder"));
    return false;
  }

  const GameFingerprint& print = GameFingerprintConfig();
  const std::vector<std::string> required = PathsForRoles(print, Roles(kGameRoles));

  // The user may point at the dump root itself or at the folder that contains
  // it, so both levels are considered and the best match wins.
  std::vector<fs::path> candidates{folder};
  for (const fs::path& child : Children(folder, nullptr)) {
    if (DirectoryExists(child)) {
      candidates.push_back(child);
    }
  }

  fs::path best;
  std::size_t found = 0;
  for (const fs::path& candidate : candidates) {
    const std::size_t count = CountPresent(candidate, required, nullptr);
    if (count == required.size()) {
      best = candidate;
      found = count;
      break;
    }
    if (best.empty() || count > found) {
      best = candidate;
      found = count;
    }
  }
  if (found != required.size()) {
    std::vector<std::string> missing;
    CountPresent(best, required, &missing);
    SetError(error, S("cannot find the supported Rock Band Blitz files in ", Display(best), " (",
                     found, " of ", required.size(), " found, missing ", Join(missing, ", "),
                     "); point the installer at the folder holding your extracted dump"));
    return false;
  }

  GameSourcePlan plan;
  plan.kind = GameSourceKind::kFolder;
  plan.location = best;
  FillPlanFiles(print, best, &plan);
  plan.description = S("folder ", Display(best));
  *out = plan;
  log::Info(S("planned an import from ", plan.description, ": ", plan.PresentCount(), " files"));
  return true;
}

bool PlanGamePackage(const std::filesystem::path& package, GameSourcePlan* out, std::string* error) {
  if (out == nullptr) {
    SetError(error, "PlanGamePackage needs somewhere to write the plan");
    return false;
  }
  if (!FileExists(package)) {
    SetError(error, S(Display(package), " does not exist"));
    return false;
  }

  StfsContainer container;
  if (!container.Open(package, error)) {
    return false;
  }

  const GameFingerprint& print = GameFingerprintConfig();
  const StfsInfo& info = container.info();
  GameSourcePlan plan;
  plan.kind = GameSourceKind::kPackage;
  plan.location = package;
  plan.volume = info;
  plan.package_entries = container.entries().size();

  const std::vector<std::string> roles = Roles(kGameRoles);
  for (const fingerprint::FileFingerprint& file : print.files) {
    const bool required = std::find(roles.begin(), roles.end(), file.role) != roles.end();
    const bool extra = IsOptionalGameFile(file.path);
    if (!required && !extra) {
      continue;
    }
    const StfsEntry* entry = FindStfsEntry(container, file.path);
    AddPlanFile(&plan, file.path, file.size, file.sha256, required, entry != nullptr);
  }
  for (std::string_view path : kOptionalGameFiles) {
    AddPlanFile(&plan, path, 0, {}, false, FindStfsEntry(container, path) != nullptr);
  }

  const std::vector<std::string> missing = plan.Missing();
  if (!missing.empty()) {
    SetError(error, S(Display(package), " (title ", HexU32(info.title_id), ") does not hold the "
                     "supported Rock Band Blitz content; missing ", Join(missing, ", ")));
    return false;
  }

  plan.description = S("package ", Display(package), " (title ", HexU32(info.title_id), ", ",
                       plan.package_entries, " entries, ", HumanBytes(info.file_bytes), ")");
  *out = plan;
  log::Info(S("planned an import from ", plan.description, ": ", plan.PresentCount(), " files"));
  return true;
}

bool ImportGame(const GameSourcePlan& plan, const std::filesystem::path& game_root,
                const ProgressSink& progress, std::string* error) {
  const std::vector<std::string> plan_missing = plan.Missing();
  if (!plan_missing.empty()) {
    SetError(error, S("the source is missing ", Join(plan_missing, ", ")));
    return false;
  }
  if (plan.location.empty()) {
    SetError(error, "the source has no location");
    return false;
  }
  if (plan.kind == GameSourceKind::kFolder && Overlaps(plan.location, game_root)) {
    SetError(error, S("the folder you picked (", Display(plan.location),
                     ") overlaps the game data folder (", Display(game_root),
                     "); pick the folder you extracted your own dump to"));
    return false;
  }

  const fs::path staging = StagingRoot(game_root);
  RemoveOnExit cleanup(staging);
  if (!RemoveTree(staging, error) || !EnsureDirectory(staging, error)) {
    return false;
  }

  StfsContainer container;
  if (plan.kind == GameSourceKind::kPackage && !container.Open(plan.location, error)) {
    return false;
  }

  const std::uint64_t total = plan.PresentBytes();
  StepProgress step(progress, total, 0, 90);
  progress.Report(0, plan.kind == GameSourceKind::kFolder ? "copying your dump"
                                                          : "unpacking the package");

  std::uint64_t done = 0;
  std::size_t copied = 0;
  for (const GameFilePlan& file : plan.files) {
    if (!file.present) {
      continue;
    }
    const fs::path relative = SafeRelativePath(file.path, error);
    if (relative.empty()) {
      return false;
    }
    const fs::path target = staging / relative;

    if (plan.kind == GameSourceKind::kFolder) {
      if (!CopyOne(plan.location / relative, target, error)) {
        return false;
      }
      done += file.size;
    } else {
      const StfsEntry* entry = FindStfsEntry(container, file.path);
      if (entry == nullptr) {
        SetError(error, S("the package changed while it was being read: ", file.path, " is gone"));
        return false;
      }
      FileSink sink;
      if (!sink.Open(target, error)) {
        return false;
      }
      const Sink write = std::ref(sink);
      const bool extracted = container.Extract(*entry, write, error);
      std::string close_reason;
      const bool closed = sink.Close(&close_reason);
      if (!extracted || !closed) {
        SetError(error, S("cannot write ", Display(target), ": ",
                         WriteFailureReason(sink, close_reason, error)));
        return false;
      }
      done += sink.bytes_written();
    }
    ++copied;
    step.Report(done, file.path);
  }

  progress.Report(90, "checking the imported files");
  std::string evidence;
  if (!RequireMatch(GameFingerprintConfig(), staging, Roles(kGameRoles),
                    S("data read from ", plan.description), &evidence, error)) {
    return false;
  }
  log::Info(evidence);

  progress.Report(95, "installing the game files");
  if (!EnsureDirectory(game_root, error)) {
    return false;
  }
  std::size_t moved = 0;
  if (!MoveTree(staging, game_root, &moved, error)) {
    return false;
  }
  progress.Report(100, "installed");
  log::Info(S("imported ", copied, " of ", plan.files.size(), " known files (", HumanBytes(total),
              ") from ", plan.description));
  return true;
}

bool VerifyGameTree(const std::filesystem::path& game_root, std::string* evidence,
                    std::string* error) {
  if (!DirectoryExists(game_root)) {
    SetError(error, S(Display(game_root), " does not exist"));
    return false;
  }
  return RequireMatch(GameFingerprintConfig(), game_root, Roles(kGameRoles),
                      S("game data in ", Display(game_root)), evidence, error);
}

// --- sources --------------------------------------------------------------

std::string DescribeSource(const SourceRef& source) {
  switch (source.kind) {
    case SourceKind::kEmbedded:
      return "included in this installer";
    case SourceKind::kUrl:
      return S("download ", source.url);
    case SourceKind::kZip:
      return S("archive ", Display(source.path));
    case SourceKind::kFolder:
      return S("folder ", Display(source.path));
  }
  return "unknown source";
}

bool IsRemote(const SourceRef& source) { return source.kind == SourceKind::kUrl; }

// --- payload --------------------------------------------------------------

bool VerifyPayload(const std::filesystem::path& app_dir, const Pins& pins, ComponentResult* out,
                   std::string* error) {
  return VerifyPayloadTree(app_dir, pins, "in this install", out, error);
}

bool InstallPayload(const SourceRef& source, const std::filesystem::path& app_dir, const Pins& pins,
                    const ProgressSink& progress, ComponentResult* out, std::string* error) {
  if (source.kind == SourceKind::kEmbedded) {
    return VerifyPayload(app_dir, pins, out, error);
  }
  if (!EnsureDirectory(app_dir, error)) {
    return false;
  }

  const fs::path staging = StagingRoot(app_dir);
  RemoveOnExit cleanup(staging);
  if (!RemoveTree(staging, error) || !EnsureDirectory(staging, error)) {
    return false;
  }

  fs::path tree;
  if (!Materialise(source, staging, {}, {std::string(kPayloadManifestName)}, nullptr, nullptr,
                   progress, "recompiled build", &tree, error)) {
    return false;
  }

  ComponentResult result;
  if (!VerifyPayloadTree(tree, pins, S("from ", DescribeSource(source)), &result, error)) {
    return false;
  }
  result.source = DescribeSource(source);
  if (!source.version.empty()) {
    result.version = source.version;
  }

  std::size_t moved = 0;
  if (!MoveTree(tree, app_dir, &moved, error)) {
    return false;
  }
  progress.Report(100, "installed");
  log::Info(S("installed the recompiled build (", result.files, " files, ",
              HumanBytes(result.bytes), ") from ", result.source));
  if (out != nullptr) {
    *out = result;
  }
  return true;
}

// --- the Ultimate mod -----------------------------------------------------

bool InstallUltimate(const SourceRef& source, const std::filesystem::path& game_root,
                     const Pins& pins, const ProgressSink& progress, ComponentResult* out,
                     std::string* error) {
  if (!DirectoryExists(game_root)) {
    SetError(error, S("there is no game data in ", Display(game_root),
                     "; import your dump before adding the Ultimate mod"));
    return false;
  }

  const GameFingerprint& print = UltimateFingerprintConfig();
  const std::vector<std::string> roles = Roles(kUltimateRoles);
  const std::vector<std::string> markers = PathsForRoles(print, roles);
  if (markers.empty()) {
    SetError(error, "the embedded Ultimate fingerprints do not describe the mod's archives");
    return false;
  }

  const std::string prefix = pins.ultimate.archive_prefix;
  const fs::path preferred = prefix.empty() ? fs::path{} : PathOf(prefix);
  const fs::path staging = StagingRoot(game_root);
  RemoveOnExit cleanup(staging);
  if (!RemoveTree(staging, error) || !EnsureDirectory(staging, error)) {
    return false;
  }

  fs::path tree;
  if (!Materialise(source, staging, preferred, markers, UltimateFilter(prefix),
                   RejectedUltimateFile, progress, "Ultimate archive", &tree, error)) {
    return false;
  }

  std::vector<FileCheck> checks;
  if (!fingerprint::VerifyRoles(print, tree, roles, &checks, error)) {
    return false;
  }
  LogChecks(checks);
  if (MissingOrUnreadable(checks)) {
    SetError(error, S("the Ultimate archive is incomplete:\n", FingerprintLines(checks)));
    return false;
  }

  ComponentResult result;
  result.version = source.version.empty() ? pins.ultimate.version : source.version;
  result.source = DescribeSource(source);
  MeasureTree(tree, &result.files, &result.bytes);
  if (!fingerprint::AllMatched(checks)) {
    // Not fatal: the overlay is data the runtime reads, and a newer or older
    // release still loads. It is worth recording loudly, though.
    result.fingerprints_matched = false;
    result.version = source.version.empty() ? "unknown" : source.version;
    result.notes.push_back(
        S("the archive does not match Rock Band Blitz Ultimate ", pins.ultimate.version, ":\n",
          FingerprintLines(checks)));
    log::Warn(result.notes.back());
  }

  const fs::path ultimate_root = game_root / PathOf(kUltimateDirName);
  if (DirectoryExists(ultimate_root) && !RemoveTree(ultimate_root, error)) {
    return false;
  }
  std::size_t moved = 0;
  if (!MoveTree(tree, ultimate_root, &moved, error)) {
    return false;
  }
  progress.Report(100, "installed");
  log::Info(S("installed the Ultimate mod (", result.files, " files, ", HumanBytes(result.bytes),
              ") from ", result.source));
  if (out != nullptr) {
    *out = result;
  }
  return true;
}

// --- manifest and report --------------------------------------------------

std::string UtcTimestamp() {
  SYSTEMTIME time{};
  GetSystemTime(&time);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear, time.wMonth,
                time.wDay, time.wHour, time.wMinute, time.wSecond);
  return buffer;
}

// TOML basic string: backslashes and quotes escaped, so a Windows path survives
// the round trip through the project's own parser.
std::string TomlString(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() + 2);
  escaped.push_back('"');
  for (const char character : text) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  escaped.push_back('"');
  return escaped;
}

namespace {

void AddComponentLines(std::vector<std::string>* lines, std::string_view title,
                       const ComponentResult& component) {
  lines->push_back(S(title));
  lines->push_back(std::string(title.size(), '-'));
  lines->push_back(S("Version    : ", component.version.empty() ? "unknown" : component.version));
  lines->push_back(S("Source     : ", component.source));
  lines->push_back(S("Files      : ", component.files, " (", HumanBytes(component.bytes), ")"));
  lines->push_back(S("Verified   : ", component.fingerprints_matched ? "yes" : "no"));
  for (const std::string& note : component.notes) {
    lines->push_back(S("Note       : ", note));
  }
  lines->push_back(std::string{});
}

void AddManifestComponent(std::vector<std::string>* lines, std::string_view section,
                          const ComponentResult& component) {
  lines->push_back(S("[", section, "]"));
  lines->push_back(S("version = ", TomlString(component.version)));
  lines->push_back(S("source = ", TomlString(component.source)));
  lines->push_back(S("files = ", component.files));
  lines->push_back(S("bytes = ", component.bytes));
  lines->push_back(S("fingerprints_matched = ", component.fingerprints_matched ? "true" : "false"));
  lines->push_back(S("warnings = ", component.notes.size()));
  for (std::size_t index = 0; index < component.notes.size(); ++index) {
    lines->push_back(S("warning_", index + 1, " = ", TomlString(component.notes[index])));
  }
  lines->push_back(std::string{});
}

}  // namespace

// The report carries the whole install log rather than this run's lines: every
// helper command appends to the same file, so it is the one place where a user
// can see what the wizard did, and a bug report should not need two attachments.
std::string LogForReport(const std::filesystem::path& app_dir) {
  std::string text;
  if (ReadFileText(app_dir / PathOf(kInstallLogName), &text, nullptr) && !text.empty()) {
    return text;
  }
  return log::Contents();
}

bool FinalizeInstall(const InstallSummary& summary, std::string* error) {
  if (!DirectoryExists(summary.install_dir)) {
    SetError(error, S(Display(summary.install_dir), " does not exist"));
    return false;
  }
  const fs::path game_dir =
      summary.game_dir.empty() ? GameRoot(summary.install_dir) : summary.game_dir;
  const std::string stamp = UtcTimestamp();

  std::vector<std::string> manifest;
  manifest.push_back("schema_version = 1");
  manifest.push_back(std::string{});
  manifest.push_back("[install]");
  manifest.push_back(S("installed_at = ", TomlString(stamp)));
  manifest.push_back(S("installer_version = ", TomlString(summary.installer_version)));
  manifest.push_back(S("helper_version = ", TomlString(kHelperVersion)));
  manifest.push_back(S("payload_commit = ", TomlString(summary.payload_commit)));
  manifest.push_back(S("directory = ", TomlString(Display(summary.install_dir))));
  manifest.push_back(S("game_directory = ", TomlString(Display(game_dir))));
  manifest.push_back(S("windows_build = ", WindowsBuildNumber()));
  manifest.push_back(S("architecture = ", TomlString(MachineArchitecture())));
  manifest.push_back(std::string{});
  AddManifestComponent(&manifest, "payload", summary.payload);
  manifest.push_back("[game_data]");
  manifest.push_back(S("source = ", TomlString(summary.game_source)));
  manifest.push_back(S("directory = ", TomlString(Display(game_dir))));
  manifest.push_back(S("ultimate_installed = ", summary.ultimate_installed ? "true" : "false"));
  manifest.push_back(std::string{});
  AddManifestComponent(&manifest, "ultimate", summary.ultimate);

  const fs::path manifest_path = summary.install_dir / PathOf(kInstallManifestName);
  if (!WriteFileText(manifest_path, Join(manifest, "\n"), false, error)) {
    return false;
  }

  std::vector<std::string> report;
  report.push_back("Rock Band Blitz - install report");
  report.push_back("================================");
  report.push_back(std::string{});
  report.push_back(S("Installed  : ", stamp));
  report.push_back(S("Folder     : ", Display(summary.install_dir)));
  report.push_back(S("Game data  : ", Display(game_dir)));
  report.push_back(S("Installer  : version ", summary.installer_version,
                     " (helper ", kHelperVersion, ")"));
  report.push_back(S("System     : Windows build ", WindowsBuildNumber(), " (",
                     MachineArchitecture(), ")"));
  report.push_back(std::string{});
  report.push_back("Recompiled build");
  report.push_back("----------------");
  report.push_back(S("Version    : ",
                     summary.payload.version.empty() ? "unknown" : summary.payload.version));
  report.push_back(S("Commit     : ",
                     summary.payload_commit.empty() ? "unknown" : summary.payload_commit));
  report.push_back(S("Source     : ", summary.payload.source));
  report.push_back(S("Files      : ", summary.payload.files, " (",
                     HumanBytes(summary.payload.bytes), ")"));
  report.push_back(S("Verified   : ", summary.payload.fingerprints_matched ? "yes" : "no"));
  report.push_back(std::string{});
  report.push_back("Game data");
  report.push_back("---------");
  report.push_back(S("Source     : ", summary.game_source));
  if (!summary.game_evidence.empty()) {
    report.push_back("Checks     :");
    for (const std::string& line : SplitLines(summary.game_evidence)) {
      report.push_back(S("  ", line));
    }
  }
  report.push_back(std::string{});
  if (summary.ultimate_installed) {
    AddComponentLines(&report, "Ultimate mod", summary.ultimate);
  } else {
    report.push_back("Ultimate mod");
    report.push_back("------------");
    report.push_back("Not installed.");
    report.push_back(std::string{});
  }
  report.push_back("Log");
  report.push_back("---");
  report.push_back(LogForReport(summary.install_dir));

  const fs::path report_path = summary.install_dir / PathOf(kInstallReportName);
  if (!WriteFileText(report_path, Join(report, "\r\n"), false, error)) {
    return false;
  }
  log::Info(S("wrote ", Display(manifest_path), " and ", Display(report_path)));
  return true;
}

// The uninstaller only knows about the files it placed itself, which covers the
// payload when it was embedded in the setup file but not when it was downloaded.
// The manifest lists the build in both cases, so it is used here as well;
// removing a file the uninstaller would have removed anyway costs nothing, and
// failures are logged rather than fatal so a locked file cannot block the
// uninstall.
void RemovePayloadFiles(const std::filesystem::path& app_dir, std::vector<std::string>* removed) {
  const fs::path manifest = app_dir / PathOf(kPayloadManifestName);
  std::string text;
  std::string reason;
  if (!ReadFileText(manifest, &text, &reason)) {
    return;
  }
  GameFingerprint print;
  if (!fingerprint::ParseGameFingerprint(text, &print, &reason)) {
    log::Warn(S("not removing the installed build files: ", reason));
    return;
  }
  for (const fingerprint::FileFingerprint& file : print.files) {
    const fs::path relative = SafeRelativePath(file.path, &reason);
    if (relative.empty()) {
      log::Warn(S("not removing payload entry '", file.path, "': ", reason));
      continue;
    }
    const fs::path path = app_dir / relative;
    std::error_code code;
    if (!fs::remove(path, code)) {
      continue;
    }
    if (removed != nullptr) {
      removed->push_back(Display(path));
    }
  }
}

bool UninstallCleanup(const std::filesystem::path& app_dir, bool keep_game_data,
                      std::vector<std::string>* removed, std::string* error) {
  if (!DirectoryExists(app_dir)) {
    return true;
  }

  if (!keep_game_data) {
    const fs::path game_dir = GameRoot(app_dir);
    if (DirectoryExists(game_dir)) {
      if (!RemoveTree(game_dir, error)) {
        return false;
      }
      if (removed != nullptr) {
        removed->push_back(S("game data (", Display(game_dir), ")"));
      }
    }
  }

  RemovePayloadFiles(app_dir, removed);

  // Files the installer wrote that Inno Setup does not know about. Removing
  // something that is already gone is not an error.
  const std::string_view files[] = {kPayloadManifestName, kInstallManifestName, kInstallReportName,
                                    kInstallLogName};
  for (const std::string_view name : files) {
    const fs::path path = app_dir / PathOf(name);
    if (!FileExists(path)) {
      continue;
    }
    if (!RemoveFile(path, error)) {
      return false;
    }
    if (removed != nullptr) {
      removed->push_back(Display(path));
    }
  }

  const fs::path staging = StagingRoot(app_dir);
  if (DirectoryExists(staging)) {
    if (!RemoveTree(staging, error)) {
      return false;
    }
    if (removed != nullptr) {
      removed->push_back(S("staging (", Display(staging), ")"));
    }
  }
  return true;
}

}  // namespace rb_blitz::installer
