// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rb_blitz::installer {

// The extracted-folder route consumes only the PS3 Buttons scene. Xbox pad art
// comes from the user's installed 360 game; no game archive is copied to schemes.
bool ProbePs3Folder(const std::filesystem::path& folder, std::filesystem::path* scene,
                    std::string* error);
bool InstallPs3FolderScheme(const std::filesystem::path& folder,
                            const std::filesystem::path& install_root,
                            std::string* error);
bool ProbePs3SceneBytes(const std::vector<std::uint8_t>& bytes, std::string* error);
bool InstallPs3SceneScheme(const std::vector<std::uint8_t>& bytes,
                           const std::filesystem::path& install_root,
                           std::string* error);

}  // namespace rb_blitz::installer
