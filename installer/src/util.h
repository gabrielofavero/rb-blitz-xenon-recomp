// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Shared plumbing for the setup helper: formatting, Windows/path helpers, small
// file primitives, path-traversal safety and the install log.
//
// The helper is a console program that the Inno Setup wizard runs with
// generated arguments; it is deliberately free of any dependency on the
// emulator beyond src/util/sha256.* and src/util/game_fingerprint.*, because it
// has to run on a machine that has never seen this repository.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::installer {

// --- formatting -----------------------------------------------------------

template <typename... Args>
std::string S(const Args&... args) {
  std::ostringstream stream;
  (stream << ... << args);
  return stream.str();
}

// "1.2 GB", "512 MB", "37 bytes": for user-facing sizes, always two significant
// parts so the wizard never shows a bare digit count.
std::string HumanBytes(std::uint64_t bytes);

std::string ToHex(const std::uint8_t* data, std::size_t size);
// "0x1A2B3C4D": the fixed-width form used for ids, hashes and CRCs in logs.
std::string HexU32(std::uint32_t value);
std::string Lower(std::string_view text);
bool EqualsIgnoreCase(std::string_view left, std::string_view right);
bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix);
std::string Trim(std::string_view text);
std::vector<std::string> SplitLines(std::string_view text);
std::string Join(const std::vector<std::string>& parts, std::string_view separator);

// Windows message text for a Win32 error code, trimmed of the trailing CR/LF.
std::string FormatWin32Error(unsigned long code);
std::string LastWin32Error();

// Records `message` when the caller asked for the reason. Every function in this
// header that takes an error out-parameter tolerates nullptr, so best-effort
// cleanup (deleting a partial download, for instance) can ignore the text.
void SetError(std::string* error, std::string message);

// --- Windows helpers ------------------------------------------------------

std::wstring Widen(std::string_view utf8);
std::string Narrow(std::wstring_view wide);
void EnableUtf8Console();
bool ContainsIgnoreCase(std::string_view text, std::string_view needle);

// LOCALAPPDATA etc. Empty optional when the variable is unset or empty.
std::optional<std::filesystem::path> EnvironmentPath(std::wstring_view name);
// Windows build number, e.g. 19045, or 0 when it cannot be determined.
std::uint32_t WindowsBuildNumber();
std::uint64_t FreeBytesAvailable(const std::filesystem::path& path);
std::uint64_t TotalBytes(const std::filesystem::path& path);
std::string MachineArchitecture();

// --- file primitives ------------------------------------------------------

using Bytes = std::vector<std::uint8_t>;

bool ReadFileBytes(const std::filesystem::path& path, Bytes* out, std::string* error);
bool ReadFileText(const std::filesystem::path& path, std::string* out, std::string* error);
bool WriteFileBytes(const std::filesystem::path& path, const std::uint8_t* data, std::size_t size,
                    bool append, std::string* error);
bool WriteFileText(const std::filesystem::path& path, std::string_view text, bool append,
                   std::string* error);
bool EnsureDirectory(const std::filesystem::path& path, std::string* error);
bool EnsureParentDirectory(const std::filesystem::path& path, std::string* error);
// Both succeed when the target does not exist.
bool RemoveFile(const std::filesystem::path& path, std::string* error);
bool RemoveTree(const std::filesystem::path& path, std::string* error);
bool FileExists(const std::filesystem::path& path);
bool DirectoryExists(const std::filesystem::path& path);
std::uint64_t FileSizeOrZero(const std::filesystem::path& path);
// Growable progress file for the wizard's page timer: one integer percent and a
// newline. Errors are logged, never fatal.
void WriteProgressFile(const std::filesystem::path& path, int percent);
void WriteProgressFile(const std::filesystem::path& path, int percent, std::string_view detail);
// `--summary` output: `key=value;key=value` on a single line, written atomically
// enough for the wizard to poll it.
void WriteSummaryFile(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& pairs);
std::optional<std::string> ReadSummaryValue(const std::string& text, std::string_view key);

// --- path safety ----------------------------------------------------------

// Converts a manifest path ('/'-separated, relative) to a host path. Returns an
// empty path and a reason when the manifest path is unsafe: absolute, drive
// relative, containing a `..` component, an alternate data stream, a reserved
// device name or a character Windows strips or forbids.
std::filesystem::path SafeRelativePath(std::string_view posix_relative, std::string* error);
// True when `candidate` is `root` itself or lives underneath it. Both sides are
// weakly canonicalised first, so a junction pointing outside `root` is caught.
bool PathIsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate);
std::string PosixPath(const std::filesystem::path& path);

// --- crc32 ----------------------------------------------------------------

namespace detail {
consteval std::array<std::uint32_t, 256> MakeCrc32Table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t index = 0; index < 256; ++index) {
    std::uint32_t value = index;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
    }
    table[index] = value;
  }
  return table;
}
inline constexpr auto kCrc32Table = MakeCrc32Table();
}  // namespace detail

// Standard zlib/PKZIP CRC-32 (reflected, polynomial 0xEDB88320).
inline std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed = 0) {
  std::uint32_t crc = seed ^ 0xFFFFFFFFu;
  for (std::size_t index = 0; index < size; ++index) {
    crc = detail::kCrc32Table[(crc ^ data[index]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// --- logging --------------------------------------------------------------

// One log per helper run, appended to `--log`, mirrored to stdout so a manual
// run in a terminal is readable. The wizard shows the tail after a failure.
namespace log {
void Open(const std::filesystem::path& path);
void Info(std::string_view message);
void Detail(std::string_view message);
void Warn(std::string_view message);
void Error(std::string_view message);
// Everything logged since Open(), for the install report.
std::string Contents();
}  // namespace log

}  // namespace rb_blitz::installer
