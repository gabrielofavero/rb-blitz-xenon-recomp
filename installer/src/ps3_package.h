// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rb_blitz::installer {

// Reads only the Buttons scene from the user's package. The supported package
// variant uses its own QA digest stream; no decryption key is stored or logged.
bool ReadPs3PackageScene(const std::filesystem::path& package,
                         std::vector<std::uint8_t>* scene,
                         std::string* content_id, std::string* error);

}  // namespace rb_blitz::installer
