// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <functional>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "graphics/preparation_state.h"

namespace rb_blitz::graphics {
using Bytes = std::vector<uint8_t>;
using AssetReader = std::function<Bytes(const std::string&)>;

// The value each recipe store's header records: max(PipelineDescription::kVersion,
// <translator>::Modification::kVersion) of the SDK this build compiles against,
// written big-endian like the SDK writes it. tests/shader_pipeline_catalogue_tests.py
// re-reads those SDK headers and fails if these drift; SeedPreparation also
// repairs a store written with another value instead of refusing to boot on it.
inline constexpr uint32_t kD3D12RecipeStorageVersion = 0x20260226;
inline constexpr uint32_t kVulkanRecipeStorageVersion = 0x20260228;

// What one seeding pass did. Every layer reports whether it was reused from the
// ledger or rebuilt, because "prepared" must be explainable afterwards.
struct SeedReport {
  bool extraction_reused = false;
  bool recipes_reused[kPreparationPathCount] = {};
  // Layers whose existing file could not be trusted and were rebuilt.
  uint32_t repaired_layers = 0;
  uint64_t shaders = 0;
  uint64_t records[kPreparationPathCount] = {};
  // The ledger was written. False means the cache is not persistable; live
  // preparation is unaffected, but a later launch will have to prepare again.
  bool persisted = true;
};

// The manifest contains source locations, vertex declarations and renderer
// descriptions only. All instruction bytes come from the installed title.
//
// A layer recorded as valid by the ledger is verified and reused; any other
// layer is rebuilt from the installed content. Stale, corrupt or partial state
// is therefore repaired rather than fatal, and a genuinely unresolvable
// locator still throws (that is a coverage blocker, not a cache problem).
SeedReport SeedPreparation(const std::string& manifest, const std::filesystem::path& cache,
                           const AssetReader& read, std::span<const uint8_t> image,
                           const std::function<bool()>& cancelled,
                           const PreparationInputs& inputs = {}, uint64_t content_epoch = 0,
                           std::string* persistence_error = nullptr);
}  // namespace rb_blitz::graphics
