// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <string_view>

namespace rb_blitz::loading {
void ConfigureCrashRecovery(const std::filesystem::path& cache_root, bool settings_restart_boot = false);
void CloseCrashRecovery();
void BeginCrashRecoveryAttempt();
void BeforeSongLoadJob();
void EndCrashRecoveryAttempt();
void ObserveRecoveryScreen(std::string_view name, bool stable);
bool CrashRecoveryQuery(std::string_view path, bool* answer);
}
