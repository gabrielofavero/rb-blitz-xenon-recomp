#pragma once

#include <filesystem>

namespace rb_blitz::diag {

// Move the last session aside before the SDK opens a fresh rotating file sink.
// Returns an empty path if diagnostics cannot be prepared safely.
std::filesystem::path PrepareSessionLog(const std::filesystem::path& user_data_root);

}  // namespace rb_blitz::diag
