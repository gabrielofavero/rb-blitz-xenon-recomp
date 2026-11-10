// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>

namespace rb_blitz::platform {
// Install after the cache path is configured, before launching the guest.
// Windows writes a text record and minidump for fatal exceptions. POSIX hosts write a small
// async-signal-safe text record, then restore the default fatal-signal action.
bool InstallCrashDiagnostics(const std::filesystem::path& cache_root);
}
