// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>

namespace rb_blitz::platform {
// Install after the cache path is configured, before launching the guest.
// Writes a small text record and minidump for fatal Windows exceptions.
bool InstallCrashDiagnostics(const std::filesystem::path& cache_root);
}
