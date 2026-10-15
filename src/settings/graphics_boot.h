// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include "fs/atomic_file.h"
#include "settings/model.h"

namespace rb_blitz::settings {
using GraphicsValues = std::map<SettingId, Value>;

// Per-profile graphics startup journal. It never rewrites requested preferences.
//
// The policy, as measured against the owner's installed copy (2026-10-15): a request whose boot
// died before the menu frame is recovered on the next boot, and that request is retried exactly
// ONCE on a later boot. A preference that fails twice keeps the last-known-good values until it
// changes; a request that was only killed by something outside the boot (a stale journal from
// another build, a task-manager exit, a harness kill) heals by itself on the retry.
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
  bool recovered_ = false;
  bool retrying_ = false;  // this boot is the one retry (in-memory: only meaningful this session)
  bool retried_ = false;   // the retry has been spent for `recovery_for_` (persisted)
  std::string requested_;
  std::string recovery_for_;
  GraphicsValues attempted_;
  GraphicsValues good_;
};
}  // namespace rb_blitz::settings
