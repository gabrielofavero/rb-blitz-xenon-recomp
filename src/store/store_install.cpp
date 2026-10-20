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

std::string CandidateName(const std::string& stem, const std::size_t attempt) {
  std::string name = stem;
  if (attempt != 0) {
    name += "~";
    name += std::to_string(attempt);
  }
  name += ".con";
  return name;
}

}  // namespace

std::string InstallStem(std::string_view file_id) {
  const std::string digest = util::HashHex(file_id);
  return "rv_" + digest.substr(0, 32);
}

bool IsValidInstallName(std::string_view name) {
  if (name.size() > kMaxInstallFileNameBytes) {
    return false;
  }
  // `rv_` + 32 lowercase hex.
  if (name.size() < kInstallNameBytes || !name.starts_with("rv_")) {
    return false;
  }
  std::size_t index = 3;
  for (std::size_t hex = 0; hex < 32; ++hex, ++index) {
    if (index >= name.size()) {
      return false;
    }
    const char c = name[index];
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    if (!digit && !lower) {
      return false;
    }
  }
  // An optional `~<n>` collision suffix (1-3 digits, since kMaxInstallAttempts is 100).
  if (index < name.size() && name[index] == '~') {
    ++index;
    std::size_t digits = 0;
    while (index < name.size() && name[index] >= '0' && name[index] <= '9') {
      ++index;
      ++digits;
    }
    if (digits == 0 || digits > 3) {
      return false;
    }
  }
  return name.substr(index) == ".con";
}

std::optional<StagedInstall> PlanInstall(const InstallLayout& layout, std::string_view file_id,
                                         std::string* error) {
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
  install.final_name = CandidateName(install.stem, 0);
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
    const std::string name =
        attempt == 0 ? install.final_name : CandidateName(install.stem, attempt);
    if (name.size() > kMaxInstallFileNameBytes || !IsValidInstallName(name)) {
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
