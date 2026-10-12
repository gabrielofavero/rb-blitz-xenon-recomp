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
// A process-lifetime lock keeps a concurrent game from treating a live boot as a crash.
class GraphicsBootState {
 public:
  bool Open(const std::filesystem::path& settings_file, std::string* error);
  GraphicsValues Select(const GraphicsValues& requested, const GraphicsValues& safe,
                        bool force_safe);
  bool Begin(const GraphicsValues& effective, std::string* error);
  bool Presented(std::string* error);
  bool Closed(std::string* error);
  bool recovered() const { return recovered_; }
  bool active() const { return active_; }
 private:
  bool Save(std::string* error);
  std::filesystem::path file_;
  rb_blitz::fs::FileLock lock_;
  bool active_ = false;
  bool pending_ = false;
  bool recovered_ = false;
  std::string requested_;
  std::string recovery_for_;
  GraphicsValues attempted_;
  GraphicsValues good_;
};
}  // namespace rb_blitz::settings
