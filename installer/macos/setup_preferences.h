// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <string>

namespace rb_blitz::installer::macos {
// Persist the installer's choice once. Later launches leave the user's target alone.
bool SelectUltimate(const std::filesystem::path& settings_file, std::string* error);
}  // namespace rb_blitz::installer::macos
