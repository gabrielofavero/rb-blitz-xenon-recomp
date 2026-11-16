// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "ultimate_import_core.h"

namespace rb_blitz::installer::ios {

enum class UltimateChoice { kSkip, kLocal, kDownload };
struct ImportSessionConfig {
  fingerprint::GameFingerprint game;
  portable::UltimateInstallConfig ultimate;
};
ImportSessionConfig EmbeddedImportSessionConfig();

// Invoke all operations on a worker, serially. The host owns UIKit, progress,
// cancellation and the game-version preference. Saves never live in this tree.
class ImportSession {
 public:
  explicit ImportSession(std::filesystem::path game_root,
                         ImportSessionConfig config = EmbeddedImportSessionConfig());
  // Recover interrupted transactions and verify cached content. False means
  // setup is required; BaseReady distinguishes the optional-mod step from import.
  bool Open(std::string* error);
  bool ImportSource(const std::filesystem::path& source,
                    const portable::ImportCallbacks& callbacks, std::string* error);
  bool Finish(UltimateChoice choice, const std::filesystem::path& local_source,
              const portable::ImportCallbacks& callbacks, std::string* warning,
              std::string* error);
  bool BaseReady() const { return base_ready_; }
  bool Ready() const { return ready_; }
  UltimateChoice Choice() const { return choice_; }
  const std::filesystem::path& GameRoot() const { return game_root_; }

 private:
  std::filesystem::path game_root_;
  ImportSessionConfig config_;
  bool base_ready_ = false;
  bool ready_ = false;
  UltimateChoice choice_ = UltimateChoice::kSkip;
};

}  // namespace rb_blitz::installer::ios
