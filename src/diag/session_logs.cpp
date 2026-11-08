#include "diag/session_logs.h"

#include <system_error>

namespace rb_blitz::diag {
namespace {
namespace fs = std::filesystem;

bool MoveIfPresent(const fs::path& from, const fs::path& to) {
  std::error_code error;
  if (!fs::exists(from, error)) return !error;
  fs::remove(to, error);
  if (error) return false;
  fs::rename(from, to, error);
  return !error;
}

bool RemoveIfPresent(const fs::path& path) {
  std::error_code error;
  fs::remove(path, error);
  return !error;
}
}  // namespace

fs::path PrepareSessionLog(const fs::path& user_data_root) {
  if (user_data_root.empty()) return {};
  const fs::path directory = user_data_root / "diagnostics";
  std::error_code error;
  fs::create_directories(directory, error);
  if (error) return {};

  // spdlog's rotating_file_sink names its first rotated file game.1.log.
  // Move both pieces so reopening the game keeps the complete preceding run.
  if (!RemoveIfPresent(directory / "previous.1.log") ||
      !RemoveIfPresent(directory / "previous.log"))
    return {};
  if (!MoveIfPresent(directory / "game.1.log", directory / "previous.1.log") ||
      !MoveIfPresent(directory / "game.log", directory / "previous.log"))
    return {};
  return directory / "game.log";
}

}  // namespace rb_blitz::diag
