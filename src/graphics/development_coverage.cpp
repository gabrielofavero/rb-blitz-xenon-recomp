#include "graphics/shader_coverage.h"
#include "generated/preparation_catalogue.h"
#include <rex/logging.h>

namespace rb_blitz::graphics {
std::unique_ptr<CoverageRecorder> StartDevelopmentCoverage(
    const std::filesystem::path& executable, const std::filesystem::path& cache) {
  if (!IsDevelopmentExecutable(executable,RBBLITZ_DEVELOPMENT_BUILD_DIR)) return {};
  return std::make_unique<CoverageRecorder>(kPreparationCatalogue,cache,
      RBBLITZ_DEVELOPMENT_COVERAGE_LOG,[](const std::string& message) {
        REXLOG_INFO("shader_coverage: {}",message);
      });
}
}  // namespace rb_blitz::graphics
