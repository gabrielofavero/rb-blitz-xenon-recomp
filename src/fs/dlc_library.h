// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// A flat DLC library: a folder, nested to any depth, of STFS containers with no
// <title_id>/<content_type> levels. A container names its own title and content type
// in the XContent metadata its header carries - XContentMetadata::execution_info.
// title_id and XContentMetadata::content_type - so a library can be presented to the
// SDK's content manager as the structure it resolves, without restructuring anything
// on disk (ContentManager::set_extra_content_library() in
// rexglue-sdk/src/system/xam/content_manager.cpp, carried by patch 0006).
//
// That matters on this machine in particular: a library living on a volume without
// hard links or symlinks (D:\Games\YARG Songs is on exFAT) cannot be indexed by
// linking packages into a title_id/content_type tree, so the content manager has to
// resolve the library in place. Nothing here copies or renames a package.
//
// Kept free of any SDK dependency for the same reason src/fs/dlc_layout.h is: it is
// host path arithmetic, directory reading and a fixed-offset header read, and
// tests/dlc_library_tests.cpp has to cover it without booting the game.
// See docs/dlc.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "fs/dlc_layout.h"

namespace rb_blitz::fs {

// The most an item's file name may use: XCONTENT_DATA::file_name_raw is 42 bytes, and
// it is the file name - not the display name - that the guest hands back to open the
// item, so a longer name would be truncated by the guest and then not found.
inline constexpr std::size_t kMaxContentFileName = 42;

// The length the SDK's own StfsContainerDevice::ReadPackageHeader() requires
// (sizeof(rex::filesystem::StfsHeader)). A shorter file cannot be mounted, so it is
// not a library package either.
inline constexpr std::size_t kStfsHeaderSize = 0x971A;

// Where the two fields live in that header, both big-endian:
// XContentMetadata::content_type at 0x344 and
// XContentMetadata::execution_info.title_id at 0x360. See
// rexglue-sdk/include/rex/filesystem/devices/stfs_xbox.h.
inline constexpr std::size_t kStfsContentTypeOffset = 0x344;
inline constexpr std::size_t kStfsTitleIdOffset = 0x360;

// Everything the scanner needs to read: the title id's last byte.
inline constexpr std::size_t kStfsIdentitySize = kStfsTitleIdOffset + 4;

// A package's own idea of what it is. Not the folder it sits in - a library has no
// such folder.
struct PackageIdentity {
  uint32_t title_id = 0;
  uint32_t content_type = 0;
};

enum class PackageIdentityResult {
  kNotPackage,  // no CON/LIVE/PIRS magic: a library holds other files too
  kUnreadable,  // a container too short to mount, or whose identity is zero
  kRead,
};

inline uint32_t LoadBigEndian32(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
         uint32_t(bytes[3]);
}

// Reads the four magic bytes first, so an unrelated file in a library costs one small
// read and is silently not a package; a file that does carry the magic but cannot be
// mounted is reported instead. `reason` is only written for kUnreadable.
inline PackageIdentityResult ReadPackageIdentity(const std::filesystem::path& path,
                                                 PackageIdentity* identity, std::string* reason) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    *reason = "cannot be read";
    return PackageIdentityResult::kUnreadable;
  }

  char prefix[kStfsIdentitySize] = {};
  file.read(prefix, sizeof(prefix));
  const std::streamsize read = file.gcount();
  const std::string_view magic(prefix, read >= 4 ? 4 : static_cast<std::size_t>(read));
  if (magic != "CON " && magic != "LIVE" && magic != "PIRS") {
    return PackageIdentityResult::kNotPackage;
  }

  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) {
    *reason = "cannot be read";
    return PackageIdentityResult::kUnreadable;
  }
  if (size < kStfsHeaderSize) {
    *reason = "shorter than an STFS header, so it cannot be mounted";
    return PackageIdentityResult::kUnreadable;
  }
  if (read != static_cast<std::streamsize>(sizeof(prefix))) {
    *reason = "its header cannot be read";
    return PackageIdentityResult::kUnreadable;
  }

  const auto* bytes = reinterpret_cast<const uint8_t*>(prefix);
  identity->content_type = LoadBigEndian32(bytes + kStfsContentTypeOffset);
  identity->title_id = LoadBigEndian32(bytes + kStfsTitleIdOffset);
  if (identity->title_id == 0) {
    *reason = "its header names no title id";
    return PackageIdentityResult::kUnreadable;
  }
  return PackageIdentityResult::kRead;
}

// The file name as UTF-8, which is the encoding XCONTENT_DATA carries (rex::to_path()
// and rex::path_to_utf8() interpret it the same way on the SDK side).
inline std::string Utf8FileName(const std::filesystem::path& path) {
  const std::u8string utf8 = path.filename().u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

// `text` cut to at most `max_bytes`, never splitting a UTF-8 sequence.
inline std::string TruncateUtf8(const std::string_view text, const std::size_t max_bytes) {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 1;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
    }
    if (out.size() + length > max_bytes || i + length > text.size()) {
      break;
    }
    out.append(text.substr(i, length));
    i += length;
  }
  return out;
}

// FNV-1a over the name and the identity it is about to be filed under. Only needs to
// be stable and spread names apart; uniqueness is enforced by the caller.
inline uint64_t HashFileName(const std::string_view text, const PackageIdentity& identity,
                             const uint32_t attempt) {
  uint64_t hash = 0xCBF29CE484222325ull;
  const auto mix = [&hash](const uint8_t byte) {
    hash ^= byte;
    hash *= 0x100000001B3ull;
  };
  for (const char c : text) {
    mix(static_cast<uint8_t>(c));
  }
  mix(static_cast<uint8_t>(identity.title_id >> 24));
  mix(static_cast<uint8_t>(identity.title_id >> 16));
  mix(static_cast<uint8_t>(identity.title_id >> 8));
  mix(static_cast<uint8_t>(identity.title_id));
  mix(static_cast<uint8_t>(identity.content_type >> 24));
  mix(static_cast<uint8_t>(identity.content_type >> 16));
  mix(static_cast<uint8_t>(identity.content_type >> 8));
  mix(static_cast<uint8_t>(identity.content_type));
  for (uint32_t i = 0; i < 4; ++i) {
    mix(static_cast<uint8_t>(attempt >> (i * 8)));
  }
  return hash;
}

// A name the guest can carry and hand back: the package's own file name when it fits
// XCONTENT_DATA and is not already taken under this (title_id, content_type), and
// otherwise a UTF-8-safe prefix plus a hash of the full name. Deterministic, so a
// library rescanned on the next boot produces the same names. `used` is that
// (title_id, content_type) group's set of assigned names.
inline std::string UniqueLibraryFileName(const std::string_view original,
                                         const PackageIdentity& identity,
                                         std::set<std::string>* used) {
  if (original.size() <= kMaxContentFileName && used->insert(std::string(original)).second) {
    return std::string(original);
  }

  const std::string prefix = TruncateUtf8(original, 32);
  for (uint32_t attempt = 0;; ++attempt) {
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "~%08X",
                  static_cast<uint32_t>(HashFileName(original, identity, attempt)));
    // prefix(<=32) + '~' + 8 hex = at most 41 bytes, comfortably inside the field.
    std::string candidate = prefix + suffix;
    if (used->insert(candidate).second) {
      return candidate;
    }
  }
}

// One package the content manager can list and open.
struct LibraryItem {
  std::filesystem::path host_path;
  std::string file_name;  // <= 42 bytes, unique within (title_id, content_type)
  uint32_t title_id = 0;
  uint32_t content_type = 0;
};

struct LibraryScanResult {
  std::vector<LibraryItem> items;

  // Containers whose identity could not be read, with the reason. Files that are not
  // containers at all are not mistakes in a library and are only counted.
  struct Rejected {
    std::string entry;  // host path, '/'-separated
    std::string reason;
  };
  std::vector<Rejected> rejected;

  std::size_t files_seen = 0;    // every regular file walked
  std::size_t other_files = 0;   // of those, the ones that are not containers
  std::size_t adapted_saved_games = 0;  // saved-game packages presented as marketplace
};

// Splits a ';'-separated cvar value into trimmed, non-empty entries. Paths may
// contain spaces, so only ';' separates them.
inline std::vector<std::string> SplitPathList(const std::string_view value) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find(';', start);
    std::string_view part =
        value.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
      part.remove_prefix(1);
    }
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
      part.remove_suffix(1);
    }
    if (!part.empty()) {
      parts.emplace_back(part);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

// Eight hex digits, the way the SDK and docs/dlc.md spell a content type. Used to
// read --dlc_library_content_type.
inline bool ParseHex32(const std::string_view text, uint32_t* value) {
  if (text.empty() || text.size() > 8) {
    return false;
  }
  uint32_t parsed = 0;
  for (const char c : text) {
    uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<uint32_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<uint32_t>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<uint32_t>(c - 'A' + 10);
    } else {
      return false;
    }
    parsed = (parsed << 4) | digit;
  }
  *value = parsed;
  return true;
}

// The two content types the adaptation below is about, spelled the way the SDK spells
// them (rex::system::XContentType).
inline constexpr uint32_t kSavedGameContentType = 0x00000001;
inline constexpr uint32_t kMarketplaceContentType = 0x00000002;

// The content type a library package is presented under. A custom-song library is
// packaged as saved-game content - that is what a dumped folder of RB3 CONs is - while
// the title reads its downloadable songs out of the marketplace enumeration and does
// not list a saved game at all: measured, Blitz enumerates a saved-game library,
// mounts every package, and adds none of it to its song list, while the same packages
// presented as marketplace content list and play. So a saved-game package is presented
// as marketplace content, and a `forced` non-zero value replaces the answer for the
// whole library (including for a package that is not saved-game). See docs/dlc.md §2.1.
constexpr uint32_t LibraryContentType(uint32_t own_content_type, uint32_t forced) {
  if (forced != 0) {
    return forced;
  }
  return own_content_type == kSavedGameContentType ? kMarketplaceContentType : own_content_type;
}

// Walks every root (recursively, following real directories but not directory
// symlinks, which could loop) and returns one item per mountable container. A missing
// root is reported; an absent library is otherwise not an error.
//
// `force_content_type` overrides LibraryContentType() for every package when it is not
// zero.
inline LibraryScanResult ScanDlcLibraries(const std::vector<std::filesystem::path>& roots,
                                          const uint32_t force_content_type = 0) {
  LibraryScanResult result;
  std::map<uint64_t, std::set<std::string>> used_names;

  std::vector<std::filesystem::path> pending(roots.rbegin(), roots.rend());
  while (!pending.empty()) {
    const std::filesystem::path directory = pending.back();
    pending.pop_back();

    std::error_code ec;
    std::filesystem::directory_iterator entries(
        directory, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
      result.rejected.push_back({directory.generic_string(), "cannot be read"});
      continue;
    }

    for (const auto& entry : entries) {
      if (entry.is_directory(ec) && !ec) {
        if (!entry.is_symlink(ec)) {
          pending.push_back(entry.path());
        }
        continue;
      }
      if (!entry.is_regular_file(ec) || ec) {
        continue;
      }

      ++result.files_seen;

      PackageIdentity identity;
      std::string reason;
      switch (ReadPackageIdentity(entry.path(), &identity, &reason)) {
        case PackageIdentityResult::kNotPackage:
          ++result.other_files;
          continue;
        case PackageIdentityResult::kUnreadable:
          result.rejected.push_back({entry.path().generic_string(), std::move(reason)});
          continue;
        case PackageIdentityResult::kRead:
          break;
      }

      const uint32_t content_type =
          LibraryContentType(identity.content_type, force_content_type);
      if (force_content_type == 0 && identity.content_type != content_type) {
        ++result.adapted_saved_games;
      }
      const uint64_t group = (uint64_t(identity.title_id) << 32) | content_type;
      std::string name = UniqueLibraryFileName(Utf8FileName(entry.path()), identity,
                                               &used_names[group]);
      result.items.push_back({entry.path(), std::move(name), identity.title_id, content_type});
    }
  }

  return result;
}

}  // namespace rb_blitz::fs
