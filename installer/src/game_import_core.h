// SPDX-License-Identifier: GPL-2.0-only
// Portable, SDK-free game-source probe and import transaction.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "install.h"
#include "util/game_fingerprint.h"

namespace rb_blitz::installer::portable {

// The host owns the native dialog. An empty selection means the user canceled.
using SourcePicker = std::function<std::filesystem::path()>;
using Progress = std::function<void(int percent, std::string_view detail)>;
enum class ImportPhase { kStaged, kOldMoved, kNewActivated };
// Returning false cancels. The phase hook also permits deterministic fault tests.
using Continue = std::function<bool(ImportPhase)>;

struct ImportCallbacks {
  Progress progress;
  Continue continue_at;
  // Polled during large file copies so a closed progress dialog can stop work
  // before the transaction reaches its next phase boundary.
  std::function<bool()> cancelled;
};

bool ProbeFolder(const std::filesystem::path& folder,
                 const fingerprint::GameFingerprint& expected,
                 const std::vector<std::string>& required_roles,
                 const std::vector<std::string>& optional_paths,
                 GameSourcePlan* out, std::string* error);
bool ProbePackage(const std::filesystem::path& package,
                  const fingerprint::GameFingerprint& expected,
                  const std::vector<std::string>& required_roles,
                  const std::vector<std::string>& optional_paths,
                  GameSourcePlan* out, std::string* error);
bool ProbePickedSource(const SourcePicker& picker,
                       const fingerprint::GameFingerprint& expected,
                       const std::vector<std::string>& required_roles,
                       const std::vector<std::string>& optional_paths,
                       GameSourcePlan* out, std::string* error);
bool VerifyTree(const std::filesystem::path& root,
                const fingerprint::GameFingerprint& expected,
                const std::vector<std::string>& required_roles,
                std::string* evidence, std::string* error);
// Restores a backup left by an interrupted commit. Safe to call before each launch.
bool RecoverImport(const std::filesystem::path& destination,
                   const fingerprint::GameFingerprint& expected,
                   const std::vector<std::string>& required_roles,
                   std::string* error);
bool Import(const GameSourcePlan& plan, const std::filesystem::path& destination,
            const fingerprint::GameFingerprint& expected,
            const std::vector<std::string>& required_roles,
            const std::vector<std::string>& optional_paths,
            const ImportCallbacks& callbacks, std::string* error);

}  // namespace rb_blitz::installer::portable
