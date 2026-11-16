// SPDX-License-Identifier: GPL-2.0-only
#include "import_session.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

#include "util.h"
#include "util/sha256.h"

namespace rb_blitz::installer::ios {
namespace {
namespace fs = std::filesystem;
using namespace portable;
const std::vector<std::string> kGameRoles{"entrypoint", "archive-header", "archive-data"};
const std::vector<std::string> kUltimateRoles{"ultimate-archive-header", "ultimate-archive-data"};
constexpr const char* kMarker = ".ios-import-v1";
constexpr std::size_t kMaximumEntries = 4096;
constexpr std::uint64_t kMaximumMarkerSize = 1024 * 1024;

ImportCallbacks WithPhaseCancellation(const ImportCallbacks& callbacks) {
  auto guarded = callbacks;
  guarded.continue_at = [cancelled = callbacks.cancelled, continued = callbacks.continue_at](ImportPhase phase) {
    return (!cancelled || !cancelled()) && (!continued || continued(phase));
  };
  return guarded;
}

struct Snapshot {
  std::string path;
  std::uint64_t size = 0;
  std::string sha256;
};

bool IsHash(std::string_view hash) {
  return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

bool ReadMarker(const fs::path& path, UltimateChoice* choice,
                std::vector<Snapshot>* files, std::string* error) {
  std::error_code code;
  const auto status = fs::symlink_status(path, code);
  if (code || !fs::is_regular_file(status) || fs::file_size(path, code) > kMaximumMarkerSize || code) {
    SetError(error, "Complete the first-run game setup"); return false;
  }
  std::ifstream in(path, std::ios::binary);
  std::string magic, selected;
  unsigned version = 0;
  std::size_t count = 0;
  if (!(in >> magic >> version >> selected >> count) || magic != "rb-blitz-ios-import" ||
      version != 1 || count > kMaximumEntries ||
      (selected != "skip" && selected != "local" && selected != "download") ||
      ((selected == "skip") != (count == 0))) {
    SetError(error, "The first-run setup record is invalid"); return false;
  }
  *choice = selected == "skip" ? UltimateChoice::kSkip :
            selected == "local" ? UltimateChoice::kLocal : UltimateChoice::kDownload;
  std::set<std::string> seen;
  files->clear();
  for (std::size_t index = 0; index < count; ++index) {
    Snapshot item;
    if (!(in >> item.sha256 >> item.size >> std::quoted(item.path)) || !IsHash(item.sha256) ||
        SafeRelativePath(item.path, nullptr).empty() || !seen.insert(item.path).second ||
        EqualsIgnoreCase(fs::path(item.path).filename().string(), "default.xex")) {
      SetError(error, "The first-run setup file record is invalid"); return false;
    }
    files->push_back(std::move(item));
  }
  in >> std::ws;
  if (!in.eof()) { SetError(error, "The first-run setup record has unexpected content"); return false; }
  return true;
}

bool WriteMarker(const fs::path& root, UltimateChoice choice,
                 const std::vector<Snapshot>& files, std::string* error) {
  std::ostringstream text;
  text << "rb-blitz-ios-import 1\n"
       << (choice == UltimateChoice::kSkip ? "skip" :
           choice == UltimateChoice::kLocal ? "local" : "download")
       << ' ' << files.size() << '\n';
  for (const auto& file : files)
    text << file.sha256 << ' ' << file.size << ' ' << std::quoted(file.path) << '\n';
  if (text.str().size() > kMaximumMarkerSize) {
    SetError(error, "The imported file record is too large"); return false;
  }
  const fs::path temporary = root / (std::string(kMarker) + ".tmp");
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << text.str();
    out.flush();
    out.close();
    if (!out) {
      RemoveFile(temporary, nullptr);
      SetError(error, "Cannot save the first-run setup record"); return false;
    }
  }
  std::error_code code;
  fs::rename(temporary, root / kMarker, code);
  if (code) {
    RemoveFile(temporary, nullptr);
    SetError(error, S("Cannot finish first-run setup: ", code.message())); return false;
  }
  return true;
}

bool CaptureUltimate(const fs::path& root, std::vector<Snapshot>* files,
                     const ImportCallbacks& callbacks, std::string* error) {
  std::error_code code;
  files->clear();
  for (fs::recursive_directory_iterator it(root, code), end;
       !code && it != end; it.increment(code)) {
    if (callbacks.cancelled && callbacks.cancelled()) {
      SetError(error, "Game setup canceled"); return false;
    }
    const auto status = it->symlink_status(code);
    if (code) break;
    if (fs::is_symlink(status) || (!fs::is_regular_file(status) && !fs::is_directory(status))) {
      SetError(error, "Linked or special Ultimate files are not supported"); return false;
    }
    if (fs::is_directory(status)) continue;
    if (EqualsIgnoreCase(it->path().filename().string(), "default.xex")) {
      SetError(error, "Ultimate must not replace the retail game executable"); return false;
    }
    if (files->size() >= kMaximumEntries) {
      SetError(error, "The Ultimate file count exceeds the setup limit"); return false;
    }
    Snapshot file;
    file.path = it->path().lexically_relative(root).generic_string();
    file.size = it->file_size(code);
    if (code) break;
    file.sha256 = rb_blitz::util::HashFileHex(it->path(), nullptr, code);
    if (code) break;
    files->push_back(std::move(file));
  }
  if (code) { SetError(error, S("Cannot check Ultimate files: ", code.message())); return false; }
  std::sort(files->begin(), files->end(), [](const auto& a, const auto& b) { return a.path < b.path; });
  return true;
}

fingerprint::GameFingerprint SnapshotFingerprint(const std::vector<Snapshot>& files,
    const fingerprint::GameFingerprint& supported) {
  fingerprint::GameFingerprint result;
  result.schema_version = 1;
  for (const auto& item : files) {
    std::string role = "snapshot-" + std::to_string(result.files.size());
    for (const auto& required : kUltimateRoles) {
      const auto* expected = fingerprint::FindByRole(supported, required);
      if (expected && expected->path == item.path) role = required;
    }
    result.files.push_back({role, item.path, item.size, item.sha256});
  }
  return result;
}

bool VerifySnapshot(const fs::path& root, const std::vector<Snapshot>& files,
                    const fingerprint::GameFingerprint& supported, std::string* error) {
  const auto snapshot = SnapshotFingerprint(files, supported);
  for (const auto& role : kUltimateRoles) {
    const auto* required = fingerprint::FindByRole(snapshot, role);
    if (!required || required->size == 0) {
      SetError(error, "The first-run setup record is missing Ultimate archives"); return false;
    }
  }
  std::set<std::string> expected;
  for (const auto& file : files) expected.insert(file.path);
  std::error_code code;
  std::size_t count = 0;
  for (fs::recursive_directory_iterator it(root, code), end;
       !code && it != end; it.increment(code)) {
    const auto status = it->symlink_status(code);
    if (code) break;
    if (fs::is_directory(status)) continue;
    if (!fs::is_regular_file(status) ||
        !expected.contains(it->path().lexically_relative(root).generic_string())) {
      SetError(error, "The imported Ultimate file set changed; import it again"); return false;
    }
    ++count;
  }
  if (code || count != files.size()) {
    SetError(error, "The imported Ultimate file set is incomplete; import it again"); return false;
  }
  // VerifyTree checks every recorded role when supplied an empty role list.
  return VerifyTree(root, snapshot, {}, nullptr, error);
}
}  // namespace

ImportSessionConfig EmbeddedImportSessionConfig() {
  ImportSessionConfig config;
  std::string error;
  if (!fingerprint::ParseGameFingerprint(EmbeddedGameFingerprints(), &config.game, &error))
    throw std::runtime_error(error);
  config.ultimate = EmbeddedUltimateConfig();
  return config;
}

ImportSession::ImportSession(fs::path game_root, ImportSessionConfig config)
    : game_root_(std::move(game_root)), config_(std::move(config)) {}

bool ImportSession::Open(std::string* error) {
  ready_ = base_ready_ = false;
  choice_ = UltimateChoice::kSkip;
  if (error) error->clear();
  if (!RecoverImport(game_root_, config_.game, kGameRoles, error) ||
      !VerifyTree(game_root_, config_.game, kGameRoles, nullptr, error)) return false;
  base_ready_ = true;
  std::vector<Snapshot> files;
  if (!ReadMarker(game_root_ / kMarker, &choice_, &files, error)) {
    // An interrupted optional import must not force a second retail import.
    std::string ignored;
    RecoverImport(game_root_ / "ultimate", config_.ultimate.fingerprint, kUltimateRoles, &ignored);
    return false;
  }
  if (choice_ != UltimateChoice::kSkip) {
    const auto snapshot = SnapshotFingerprint(files, config_.ultimate.fingerprint);
    if (!RecoverImport(game_root_ / "ultimate", snapshot, kUltimateRoles, error) ||
        !VerifySnapshot(game_root_ / "ultimate", files, config_.ultimate.fingerprint, error)) return false;
  }
  RemoveFile(game_root_ / (std::string(kMarker) + ".tmp"), nullptr);
  ready_ = true;
  return true;
}

bool ImportSession::ImportSource(const fs::path& source, const ImportCallbacks& callbacks,
                                 std::string* error) {
  GameSourcePlan plan;
  if (error) error->clear();
  if (callbacks.cancelled && callbacks.cancelled()) {
    SetError(error, "Game setup canceled"); return false;
  }
  if (!ProbePickedSource([&] { return source; }, config_.game, kGameRoles, {}, &plan, error) ||
      !Import(plan, game_root_, config_.game, kGameRoles, {}, WithPhaseCancellation(callbacks), error)) return false;
  base_ready_ = true;
  ready_ = false;
  return RemoveFile(game_root_ / kMarker, error);
}

bool ImportSession::Finish(UltimateChoice choice, const fs::path& local_source,
                           const ImportCallbacks& callbacks, std::string* warning,
                           std::string* error) {
  if (warning) warning->clear();
  if (error) error->clear();
  if (callbacks.cancelled && callbacks.cancelled()) {
    SetError(error, "Game setup canceled"); return false;
  }
  if (choice != UltimateChoice::kSkip && choice != UltimateChoice::kLocal &&
      choice != UltimateChoice::kDownload) {
    SetError(error, "Choose whether to use Ultimate"); return false;
  }
  base_ready_ = VerifyTree(game_root_, config_.game, kGameRoles, nullptr, error);
  if (!base_ready_) { ready_ = false; return false; }
  if (choice == UltimateChoice::kLocal && local_source.empty()) {
    SetError(error, "Choose an Ultimate ZIP or extracted folder"); return false;
  }
  ready_ = false;
  if (!RemoveFile(game_root_ / kMarker, error)) return false;
  std::vector<Snapshot> files;
  if (choice == UltimateChoice::kSkip) {
    if (!RemoveTree(game_root_ / "ultimate", error)) return false;
  } else {
    const fs::path source = choice == UltimateChoice::kLocal ? local_source : fs::path{};
    if (!InstallUltimate(source, game_root_, config_.ultimate, WithPhaseCancellation(callbacks), warning, error)) return false;
    // Ultimate enumerates this folder before the host redirects actual captures
    // into Documents/Pictures, so retain the same placeholder as bundled IPAs.
    if (!WriteFileText(game_root_ / "ultimate/screenshots/_keepme", "", false, error) ||
        !CaptureUltimate(game_root_ / "ultimate", &files, callbacks, error) ||
        !VerifySnapshot(game_root_ / "ultimate", files, config_.ultimate.fingerprint, error)) return false;
  }
  if (callbacks.cancelled && callbacks.cancelled()) {
    SetError(error, "Game setup canceled"); return false;
  }
  if (!WriteMarker(game_root_, choice, files, error)) return false;
  choice_ = choice;
  ready_ = true;
  return true;
}
}  // namespace rb_blitz::installer::ios
