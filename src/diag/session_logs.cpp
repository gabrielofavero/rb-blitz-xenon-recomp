#include "diag/session_logs.h"

#include <system_error>
#include <chrono>
#include <fstream>
#include <iomanip>

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
  const fs::path directory = user_data_root / "Diagnostics";
  std::error_code error;
  const fs::path legacy_directory = user_data_root / "diagnostics";
  if (fs::exists(legacy_directory, error)) {
    const bool destination_exists = fs::exists(directory, error);
    if (error) return {};
    // Also rename on case-insensitive filesystems so Files shows the new spelling.
    if (!destination_exists || fs::equivalent(legacy_directory, directory, error)) {
      fs::rename(legacy_directory, directory, error);
      if (error) return {};
    }
  }
  if (error) return {};
  fs::create_directories(directory, error);
  if (error) return {};

  // spdlog's rotating_file_sink names its first rotated file game.1.log.
  // Move both pieces so reopening the game keeps the complete preceding run.
  if (!RemoveIfPresent(directory / "previous.1.log") ||
      !RemoveIfPresent(directory / "previous.log") ||
      !RemoveIfPresent(directory / "previous-session-info.txt"))
    return {};
  if (!MoveIfPresent(directory / "game.1.log", directory / "previous.1.log") ||
      !MoveIfPresent(directory / "game.log", directory / "previous.log") ||
      !MoveIfPresent(directory / "session-info.txt", directory / "previous-session-info.txt"))
    return {};
  return directory / "game.log";
}

bool WriteSessionInfo(const fs::path& user_data_root, std::string_view build,
                      std::string_view toolchain, const fs::path& executable) {
  std::ofstream output(user_data_root / "Diagnostics" / "session-info.txt",
                       std::ios::binary | std::ios::trunc);
  output << "schema=1\nstarted_unix_ms="
         << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
         << "\nbuild=" << std::quoted(std::string(build))
         << "\ntoolchain=" << std::quoted(std::string(toolchain))
         << "\nexecutable=" << std::quoted(executable.string())
         << "\ntrace=shader_compile_trace; CPU spans, not GPU execution timestamps\n";
  output.close();
  return bool(output);
}

}  // namespace rb_blitz::diag
