// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>

namespace rb_blitz::fs {
inline void PrepareIOSDocumentsFolders(const std::filesystem::path& root) {
  std::filesystem::create_directories(root / "DLCs");
  std::filesystem::create_directories(root / "Pictures");
  // Retire the old placeholder only when empty. Never remove imported files.
  for (const auto* name : {"Import", "Imports"}) {
    std::error_code ec;
    const auto path = root / name;
    if (!std::filesystem::is_symlink(path, ec) && std::filesystem::is_directory(path, ec))
      std::filesystem::remove(path, ec);
  }
}
}  // namespace rb_blitz::fs
