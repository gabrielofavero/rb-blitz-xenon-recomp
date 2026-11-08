#include "graphics/shader_coverage.h"
#include "generated/preparation_catalogue.h"
#include <rex/logging.h>

namespace rb_blitz::graphics {
bool IsDevelopmentBuild(const std::filesystem::path& executable) {
  return DevelopmentDirectoryIdentity(executable.parent_path()) ==
         RBBLITZ_DEVELOPMENT_DIRECTORY_HASH;
}

std::unique_ptr<CoverageRecorder> StartDevelopmentCoverage(
    const std::filesystem::path& executable, const std::filesystem::path& cache) {
  if (!IsDevelopmentBuild(executable))
    return {};
  return std::make_unique<CoverageRecorder>(kPreparationCatalogue,cache,
      executable.parent_path()/RBBLITZ_DEVELOPMENT_JOURNAL,[](const std::string& message) {
        REXLOG_INFO("shader_coverage: {}",message);
      });
}

std::unique_ptr<CoverageRecorder> StartCoverage(
    const std::filesystem::path& executable, const std::filesystem::path& cache,
    const std::filesystem::path& user_data_root) {
  if (IsDevelopmentBuild(executable)) return StartDevelopmentCoverage(executable, cache);
  if (user_data_root.empty()) return {};
  return std::make_unique<CoverageRecorder>(
      kPreparationCatalogue, cache, user_data_root / "diagnostics" / "unmapped-shaders.jsonl",
      [](const std::string& message) { REXLOG_INFO("shader_coverage: {}", message); });
}
}  // namespace rb_blitz::graphics
