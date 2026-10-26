// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rb_blitz::platform::song_load {
bool ShouldRestart(uint32_t exit_code, bool loading);
std::vector<std::string> RecoveryArguments(const std::vector<std::string>& arguments);

// Owns only host kernel handles. The monitor inherits a real handle to this
// process and an anonymous shared page; it never opens a PID supplied by guests.
class Session {
 public:
  Session();
  ~Session();
  bool Start(const std::filesystem::path& monitor, const std::vector<std::string>& arguments,
             const std::filesystem::path& log, std::string* error);
  void SetLoading(bool loading);
  uint32_t monitor_pid() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Standalone monitor entry. arguments exclude argv[0]. No SDK/UI initialization.
int RunMonitor(const std::vector<std::string>& arguments);
}  // namespace rb_blitz::platform::song_load
