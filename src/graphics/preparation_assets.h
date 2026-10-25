// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <functional>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rb_blitz::graphics {
using Bytes = std::vector<uint8_t>;
using AssetReader = std::function<Bytes(const std::string&)>;
// The manifest contains source locations, vertex declarations and renderer
// descriptions only. All instruction bytes come from the installed title.
void SeedPreparation(const std::string& manifest, const std::filesystem::path& cache,
                     const AssetReader& read, std::span<const uint8_t> image,
                     const std::function<bool()>& cancelled);
}
