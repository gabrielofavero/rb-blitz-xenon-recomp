// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_install.h.

#include "store/store_install.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

#include "fs/dlc_layout.h"
#include "util/sha256.h"

namespace rb_blitz::store {
namespace {

// The hex spelling the SDK uses for both directory levels (fmt("{:08X}")).
std::string TitleIdDirectory() {
  char text[9] = {};
  std::snprintf(text, sizeof(text), "%08X", kRb3TitleId);
  return std::string(text);
}

std::string ContentTypeDirectory() {
  char text[9] = {};
  std::snprintf(text, sizeof(text), "%08X", kMarketplaceContentType);
  return std::string(text);
}

// The punctuation a Windows file name cannot hold. `.` is handled separately: it is legal to the
// host but reads as an extension, and a package has none - a name like `Song.1` would look like a
// `1` format to a human and to anything that keys on the suffix.
constexpr std::string_view kInvalidNameChars = "<>:\"/\\|?*";

bool IsReservedDeviceName(std::string_view name) {
  // Windows reserves these as a file's base name, extension or not - and this module produces
  // extensionless names, so the whole name is the base.
  std::string upper;
  upper.reserve(name.size());
  for (const char c : name) {
    upper.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
  }
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") {
    return true;
  }
  return upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
         upper[3] >= '1' && upper[3] <= '9';
}

// The largest prefix of `text` of at most `bytes` bytes that does not cut a UTF-8 code point:
// the guest's field counts bytes, but a half-written character is not a name.
std::size_t Utf8Prefix(const std::string& text, const std::size_t bytes) {
  std::size_t cut = std::min(bytes, text.size());
  while (cut > 0 && cut < text.size() &&
         (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return cut;
}

void TrimTrailingSpaces(std::string* text) {
  while (!text->empty() && text->back() == ' ') {
    text->pop_back();
  }
}

// `base[~<n>]`, cut so the suffix stays inside the guest's field. `base` already passed the
// sanitizer, so a suffix never turns it back into a device name.
std::string CandidateName(const std::string& base, const std::size_t attempt) {
  std::string suffix;
  if (attempt != 0) {
    suffix = "~" + std::to_string(attempt);
  }
  std::string name = base;
  if (name.size() + suffix.size() > kMaxInstallFileNameBytes) {
    name.resize(Utf8Prefix(name, kMaxInstallFileNameBytes - suffix.size()));
    TrimTrailingSpaces(&name);
  }
  return name + suffix;
}

}  // namespace

std::string InstallStem(std::string_view file_id) {
  const std::string digest = util::HashHex(file_id);
  return "rv_" + digest.substr(0, 32);
}

std::string InstallName(std::string_view display_name) {
  std::string name;
  name.reserve(std::min(display_name.size(), kMaxInstallFileNameBytes));
  bool space_pending = false;
  for (const char raw : display_name) {
    const auto c = static_cast<unsigned char>(raw);
    if (c < 0x20 || c == 0x7F ||
        kInvalidNameChars.find(static_cast<char>(c)) != std::string_view::npos) {
      continue;
    }
    if (c == '.') {
      continue;  // a dot would read as a format, and the package has none
    }
    if (c == ' ') {
      space_pending = !name.empty();  // collapse a run, never a leading space
      continue;
    }
    if (space_pending) {
      name.push_back(' ');
      space_pending = false;
    }
    name.push_back(raw);
  }
  if (name.size() > kMaxInstallFileNameBytes) {
    name.resize(Utf8Prefix(name, kMaxInstallFileNameBytes));
    TrimTrailingSpaces(&name);  // the cut can land after a space
  }
  if (IsReservedDeviceName(name)) {
    // `CON` is a device, not a file. The marker goes last, and the name gives a byte up for it
    // when it is already at the ceiling.
    if (name.size() >= kMaxInstallFileNameBytes) {
      name.resize(Utf8Prefix(name, kMaxInstallFileNameBytes - 1));
      TrimTrailingSpaces(&name);
    }
    name.push_back('_');
  }
  return name;
}

bool IsValidInstallName(std::string_view name) {
  if (name.empty() || name.size() > kMaxInstallFileNameBytes) {
    return false;
  }
  if (name.back() == ' ') {
    return false;  // Windows strips it, and then the guest's name is not the one we chose
  }
  for (const char raw : name) {
    const auto c = static_cast<unsigned char>(raw);
    if (c < 0x20 || c == 0x7F || c == '.' ||
        kInvalidNameChars.find(static_cast<char>(c)) != std::string_view::npos) {
      return false;
    }
  }
  return !IsReservedDeviceName(name);
}

std::optional<StagedInstall> PlanInstall(const InstallLayout& layout, std::string_view file_id,
                                         std::string_view display_name, std::string* error) {
  if (layout.destination_root.empty()) {
    if (error != nullptr) {
      *error = "the store has no resolved DLC destination";
    }
    return std::nullopt;
  }
  if (file_id.empty()) {
    if (error != nullptr) {
      *error = "the offer has no file id, so the install cannot be named";
    }
    return std::nullopt;
  }

  StagedInstall install;
  install.staging_dir = layout.destination_root / std::filesystem::path(std::string(fs::kStoreStagingDirName));
  install.final_dir = layout.structured
                          ? layout.destination_root / TitleIdDirectory() / ContentTypeDirectory()
                          : layout.destination_root;
  install.stem = InstallStem(file_id);
  install.part_path = install.staging_dir / (install.stem + ".part");
  // The package is named after the song the player chose; an offer that carries no usable name
  // still installs, under its id's hash.
  const std::string named = InstallName(display_name);
  install.final_name = named.empty() ? install.stem : named;
  return install;
}

bool EnsureStagingDirectory(const StagedInstall& install, std::string* error) {
  std::error_code ec;
  std::filesystem::create_directories(install.staging_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = DescribeFileError(ClassifyFileError(ec), ec.message());
    }
    return false;
  }
  // A structured layout needs its `<title_id>/<content_type>` levels; a flat one must not
  // gain them (see the header).
  if (install.final_dir != install.staging_dir.parent_path() &&
      !std::filesystem::exists(install.final_dir, ec)) {
    std::filesystem::create_directories(install.final_dir, ec);
    if (ec) {
      if (error != nullptr) {
        *error = DescribeFileError(ClassifyFileError(ec), ec.message());
      }
      return false;
    }
  }
  return true;
}

void DiscardStagedFile(const StagedInstall& install) {
  std::error_code ec;
  std::filesystem::remove(install.part_path, ec);
}

bool CommitStagedInstall(const StagedInstall& install, std::filesystem::path* final_path,
                         std::string* error) {
  for (std::size_t attempt = 0; attempt < kMaxInstallAttempts; ++attempt) {
    const std::string name = CandidateName(install.final_name, attempt);
    if (!IsValidInstallName(name)) {
      if (error != nullptr) {
        *error = "no install name fits the guest's 42-byte file-name field";
      }
      return false;
    }
    const std::filesystem::path candidate = install.final_dir / name;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      continue;  // never replace a package the user already has
    }
    std::filesystem::rename(install.part_path, candidate, ec);
    if (ec) {
      if (error != nullptr) {
        *error = DescribeFileError(ClassifyFileError(ec), ec.message());
      }
      return false;
    }
    if (final_path != nullptr) {
      *final_path = candidate;
    }
    return true;
  }
  if (error != nullptr) {
    *error = "every generated name for this package is already taken";
  }
  return false;
}

FileError ClassifyFileError(const std::error_code& ec) {
  if (!ec) {
    return FileError::kNone;
  }
  // The values are the Windows system errors a write/rename actually reports; the generic
  // category is mapped for a host that answered with std::errc instead.
  if (ec.category() == std::system_category()) {
    switch (ec.value()) {
      case 112:  // ERROR_DISK_FULL
      case 39:   // ERROR_HANDLE_DISK_FULL
        return FileError::kDiskFull;
      case 19:  // ERROR_WRITE_PROTECT
      case 5:   // ERROR_ACCESS_DENIED
        return FileError::kReadOnly;
      case 2:   // ERROR_FILE_NOT_FOUND
      case 3:   // ERROR_PATH_NOT_FOUND
        return FileError::kNotFound;
      default:
        return FileError::kOther;
    }
  }
  if (ec == std::errc::no_space_on_device) {
    return FileError::kDiskFull;
  }
  if (ec == std::errc::read_only_file_system || ec == std::errc::permission_denied) {
    return FileError::kReadOnly;
  }
  if (ec == std::errc::no_such_file_or_directory) {
    return FileError::kNotFound;
  }
  return FileError::kOther;
}

std::string DescribeFileError(const FileError error, std::string_view detail) {
  switch (error) {
    case FileError::kNone:
      return std::string(detail);
    case FileError::kDiskFull:
      return "the DLC drive is full";
    case FileError::kReadOnly:
      return "the DLC folder is read-only";
    case FileError::kNotFound:
      return "the DLC folder disappeared while writing";
    case FileError::kOther:
      break;
  }
  std::string text = "the package could not be written";
  if (!detail.empty()) {
    text += ": ";
    text += detail;
  }
  return text;
}

}  // namespace rb_blitz::store
