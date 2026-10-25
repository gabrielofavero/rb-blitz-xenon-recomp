// Development-only discovery of portable recipes missing from the catalogue.
#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace rb_blitz::graphics {
bool IsDevelopmentExecutable(const std::filesystem::path& executable,
                             const std::filesystem::path& build_directory);

// Read-only cache scans; appends only validated, previously unseen recipes.
// This object belongs to one background thread, never to the render thread.
class ShaderCoverageJournal {
 public:
  ShaderCoverageJournal(std::string catalogue, std::filesystem::path cache,
                        std::filesystem::path journal);
  ~ShaderCoverageJournal();
  size_t Scan();
 private:
  struct State;
  std::unique_ptr<State> state_;
};

class CoverageRecorder {
 public:
  CoverageRecorder(std::string catalogue, std::filesystem::path cache,
                   std::filesystem::path journal,
                   std::function<void(const std::string&)> report);
 private:
  std::jthread worker_;
};

std::unique_ptr<CoverageRecorder> StartDevelopmentCoverage(
    const std::filesystem::path& executable, const std::filesystem::path& cache);
}  // namespace rb_blitz::graphics
