#pragma once

#include <filesystem>
#include <string_view>

namespace rb_blitz::diag {

// Move the last session aside before the SDK opens a fresh rotating file sink.
// Returns an empty path if diagnostics cannot be prepared safely.
std::filesystem::path PrepareSessionLog(const std::filesystem::path& user_data_root);

// Keep build provenance after the rotating log's boot lines have rolled away.
bool WriteSessionInfo(const std::filesystem::path& user_data_root,
                      std::string_view build, std::string_view toolchain,
                      const std::filesystem::path& executable);

}  // namespace rb_blitz::diag
