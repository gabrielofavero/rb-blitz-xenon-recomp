// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include "fs/atomic_file.h"
#include "settings/model.h"

namespace rb_blitz::settings {
using GraphicsValues = std::map<SettingId, Value>;
namespace schema { struct ReleaseSettings; }
// A recovery journal may contain settings from a different platform/release.
void ApplyReleaseGraphicsRules(GraphicsValues& values, const schema::ReleaseSettings& release);

// Per-profile graphics startup journal. It never rewrites requested preferences.
//
// A graphics startup refusal triggers recovery on the next boot, followed by
// one retry. Two refusals keep the last-known-good values until the request
// changes. An incomplete boot alone does not classify the failure.
//
// The signature keys on what the player CHOSE (`requested`), not on what it resolves to, so
// changing the choice always earns a fresh attempt - `Auto` and the explicit size it resolves to
// are different requests even when they are the same pixels. `effective` is what this boot would
// really apply, and it alone decides whether recovering would be a no-op (then the safe floor is
// used instead).
//
// A process-lifetime lock keeps a concurrent game from treating a live boot as a crash.
class GraphicsBootState {
 public:
  bool Open(const std::filesystem::path& settings_file, std::string* error);
  GraphicsValues Select(const GraphicsValues& requested, const GraphicsValues& effective,
                        const GraphicsValues& safe, bool force_safe);
  bool Begin(const GraphicsValues& effective, std::string* error);
  bool Rejected(std::string* error);
  bool Presented(std::string* error);
  bool Closed(std::string* error);
  bool recovered() const { return recovered_; }
  // True when this boot is the one retry of a request that failed to present before.
  bool retrying() const { return retrying_; }
  bool active() const { return active_; }
 private:
  bool Save(std::string* error);
  std::filesystem::path file_;
  rb_blitz::fs::FileLock lock_;
  bool active_ = false;
  bool pending_ = false;
  bool rejected_ = false;
  bool recovered_ = false;
  bool retrying_ = false;  // this boot is the one retry (in-memory: only meaningful this session)
  bool retried_ = false;   // the retry has been spent for `recovery_for_` (persisted)
  std::string requested_;
  std::string recovery_for_;
  GraphicsValues attempted_;
  GraphicsValues good_;
};
// Records live profile attempts for diagnostics and keeps confirmed allocation
// failures latched until the requested combination changes.
class ResolutionProfileState {
 public:
  bool Open(const std::filesystem::path& settings_file, std::string* error);
  bool Recover(const GraphicsValues& requested, std::string* error);
  bool Begin(const GraphicsValues& requested, SettingId context, std::string fingerprint,
             std::string* error);
  // Preserve backend allocation refusals across clean shutdown and restart.
  bool Rejected(std::string* error);
  bool Closed(std::string* error);
  bool active() const { return active_; }
 private:
  bool Save(std::string* error);
  std::filesystem::path file_;
  rb_blitz::fs::FileLock lock_;
  bool active_ = false;
  std::string request_, failed_;
  std::map<SettingId, std::string> pending_;
};
}  // namespace rb_blitz::settings
