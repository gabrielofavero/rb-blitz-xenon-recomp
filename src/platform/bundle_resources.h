// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>

namespace rb_blitz::platform {
// Installed Apple bundles keep authored assets out of the code directory.
// Ordinary direct builds continue to load resources beside the executable.
inline std::filesystem::path BundleResources(const std::filesystem::path& executable_dir) {
#if defined(__APPLE__)
  if (executable_dir.filename() == "MacOS" && executable_dir.parent_path().filename() == "Contents") {
    return executable_dir.parent_path() / "Resources";
  }
#endif
  return executable_dir;
}
}  // namespace rb_blitz::platform
