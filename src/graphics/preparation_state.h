// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace rb_blitz::graphics {

// The persistent preparation state: which reusable layer is still valid, and
// what the previous attempt actually achieved.
//
// P7's rule (docs/engine/shader-cache.md, part 7) is that four things have
// separate validity, and a single "the cache exists" flag merges facts that must
// not be merged:
//   * local shader extraction - guest microcode resolved from installed content;
//   * portable recipes        - the renderer's normalized pipeline descriptions,
//                               one store per backend and rendering path;
//   * native driver data      - validated per entry by the SDK's own identity
//                               (adapter, driver build, render path, versions);
//   * live readiness          - a generation in *this* process that finished
//                               kSucceeded for the current inputs.
// Only the first two are recorded here. Native data is deliberately excluded: a
// driver-cache hit must never be able to say "prepared" on its own, and
// readiness is always the generation's answer, never the disk's.
//
// The ledger is written with one atomic replace, and a layer is only marked
// complete after its file was re-read and verified, so a crash, a cancellation
// or a failed write leaves the layer invalid rather than falsely reusable.

// Everything the recorded layers were built from, recomputed each boot.
struct PreparationInputs {
  // XXH3 of the embedded portable manifest: a new build with different recipes
  // invalidates the stores it produced.
  uint64_t catalogue = 0;
  // XXH3 over the installed content the shader bytes were read from (the ARK
  // indexes actually used plus the validated entrypoint identity), so another
  // dump, or retail versus Ultimate content, cannot reuse an extraction.
  uint64_t content = 0;
  // XXH3 of the validated title executable's identity: the source locators
  // point into that image.
  uint64_t executable = 0;
  // max(PipelineDescription::kVersion, translator Modification::kVersion) per
  // backend - the value each recipe store's header is written with. A
  // translator or layout bump invalidates recipes but not the guest shaders.
  uint32_t d3d12_version = 0;
  uint32_t vulkan_version = 0;
};

// The four rendering paths the embedded catalogue declares, in the order the
// ledger stores them. A backend switch must leave the other backend's entries
// intact, so validity is per path, not per "the cache".
inline constexpr const char* kPreparationPaths[] = {"d3d12/rtv", "d3d12/rov", "vulkan/fbo",
                                                    "vulkan/fsi"};
inline constexpr size_t kPreparationPathCount = 4;

bool IsPreparationPath(const std::string& path);
// kPreparationPathCount when `path` is unknown.
size_t PreparationPathIndex(const std::string& path);

struct PreparationLayer {
  // The file was written and verified end to end by this ledger's own rules.
  bool complete = false;
  uint64_t catalogue = 0;
  uint64_t content = 0;
  uint64_t executable = 0;
  uint32_t d3d12_version = 0;
  uint32_t vulkan_version = 0;
  // Shader records (extraction) or pipeline records (one recipe store).
  uint64_t entries = 0;

  // Where the bytes came from - not how they are translated. A translator or
  // pipeline-layout bump may therefore reuse the extraction.
  bool ExtractionValid(const PreparationInputs& inputs) const {
    return complete && catalogue == inputs.catalogue && content == inputs.content &&
           executable == inputs.executable;
  }

  // A recipe store carries backend-specific pipeline descriptions, so that
  // backend's version participates as well.
  bool RecipesValid(const PreparationInputs& inputs, bool d3d12) const {
    return complete && catalogue == inputs.catalogue && content == inputs.content &&
           executable == inputs.executable &&
           (d3d12 ? d3d12_version == inputs.d3d12_version
                  : vulkan_version == inputs.vulkan_version);
  }
};

// The last attempt, recorded for the next boot's log and for support. A
// cancelled, failed or explicitly ignored attempt is never a readiness claim.
struct PreparationAttempt {
  uint64_t attempt = 0;
  uint64_t configuration = 0;
  // rex::graphics::GraphicsPreparationPhase as an int, so this header stays
  // SDK-free and testable without a boot.
  int32_t phase = -1;
  bool complete = false;
  bool ignored = false;
  uint64_t required = 0;
  uint64_t succeeded = 0;
  uint64_t failed = 0;
  uint64_t missing = 0;
  uint64_t cancelled = 0;
  uint64_t elapsed_milliseconds = 0;
};

class PreparationLedger {
 public:
  static constexpr uint32_t kSchema = 1;
  static constexpr const char* kFileName = "preparation-state.json";

  // `<cache_root>/shaders/preparation-state.json`.
  static std::filesystem::path PathFor(const std::filesystem::path& cache_root);

  // Never throws. A missing, unreadable, wrong-schema or malformed file leaves
  // an empty ledger and reports why, so damaged state is rebuilt rather than
  // fatal.
  void Load(const std::filesystem::path& cache_root, std::string* why = nullptr);
  // One atomic replace. `false` means the cache root is not writable: a
  // persistence fact, never a live-preparation failure.
  bool Save(const std::filesystem::path& cache_root, std::string* why = nullptr);

  PreparationLayer extraction;
  PreparationLayer recipes[kPreparationPathCount];
  // Bumped by a successful DLC or store refresh. It does not invalidate the
  // layers above (the title's own shader bytes did not change), but a readiness
  // claim recorded under another epoch may not be described as current.
  uint64_t content_epoch = 0;
  PreparationAttempt last_attempt;
  bool loaded = false;
  // The last Save() outcome; separate from preparation, which may still have
  // created every live pipeline.
  bool writable = true;
};

}  // namespace rb_blitz::graphics
