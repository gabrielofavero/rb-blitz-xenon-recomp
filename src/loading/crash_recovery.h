// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string_view>
namespace rex { class Runtime; }
namespace rb_blitz::loading {
void ConfigureCrashRecovery(rex::Runtime* runtime, bool settings_restart_boot = false);
void BeginCrashRecoveryAttempt();
void BeforeSongLoadJob();
void EndCrashRecoveryAttempt();
void ObserveRecoveryScreen(std::string_view name, bool stable);
bool CrashRecoveryQuery(std::string_view path, bool* answer);
}
