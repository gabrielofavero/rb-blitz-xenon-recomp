// SPDX-License-Identifier: GPL-2.0-only
// Tree identity shared by the menu-music catalog and store staging checks.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "fs/dlc_library.h"

namespace rb_blitz::fs {

// A tree's identity, cheap to recompute and cheap to compare. `hash` folds every
// file's path, size and mtime; `file_count` and `total_size` are kept beside it so a
// collision has to agree on two independent numbers as well.
struct TreeFingerprint {
  uint64_t hash = 0;
  uint64_t file_count = 0;
  uint64_t total_size = 0;

  bool operator==(const TreeFingerprint& other) const {
    return hash == other.hash && file_count == other.file_count && total_size == other.total_size;
  }
  bool operator!=(const TreeFingerprint& other) const { return !(*this == other); }
};

namespace detail {

inline void MixByte(uint64_t* hash, const uint8_t byte) {
  *hash ^= byte;
  *hash *= 0x100000001B3ull;
}

inline void MixU64(uint64_t* hash, uint64_t value) {
  for (uint32_t i = 0; i < 8; ++i) {
    MixByte(hash, static_cast<uint8_t>(value >> (i * 8)));
  }
}

inline void MixBytes(uint64_t* hash, const std::string_view bytes) {
  for (const char c : bytes) {
    MixByte(hash, static_cast<uint8_t>(c));
  }
}

// Every regular file under `root`, as (path, size, mtime). An unavailable root
// cannot prove an empty library: every walk/stat error refuses reuse.
struct TreeEntry {
  std::string key;  // root index + '/' + root-relative path, UTF-8
  uint64_t size = 0;
  int64_t mtime = 0;
};

inline bool CollectTreeEntries(const std::vector<std::filesystem::path>& roots,
                               std::vector<TreeEntry>* entries, std::string* reason) {
  // The root's index is carried with each directory so two roots that hold a file with
  // the same relative path still get distinct keys; without it the sort could order the
  // pair either way and the fingerprint would depend on it.
  std::vector<std::pair<std::size_t, std::filesystem::path>> pending;
  for (std::size_t i = 0; i < roots.size(); ++i) {
    pending.emplace_back(i, roots[i]);
  }

  while (!pending.empty()) {
    const std::size_t root_index = pending.back().first;
    const std::filesystem::path directory = pending.back().second;
    pending.pop_back();

    std::error_code ec;
    std::filesystem::directory_iterator it(
        directory, std::filesystem::directory_options::none, ec);
    if (ec) {
      *reason = directory.string() + ": " + ec.message();
      return false;
    }

    const std::filesystem::directory_iterator end;
    while (it != end) {
      const auto entry = *it;
      it.increment(ec);
      if (ec) { *reason = directory.string() + ": " + ec.message(); return false; }
      std::error_code entry_ec;
      const auto status = entry.status(entry_ec);
      if (entry_ec) { *reason = entry.path().string() + ": " + entry_ec.message(); return false; }
      if (std::filesystem::is_directory(status)) {
        // A directory symlink is not followed (it could loop), exactly as
        // ScanDlcLibraries refuses to follow one.
        const bool symlink = entry.is_symlink(entry_ec);
        if (entry_ec) { *reason = entry.path().string() + ": " + entry_ec.message(); return false; }
        // The store's staging directory is not part of the library's identity: a
        // download in flight must not change the fingerprint (which would discard the
        // cache on every byte written) and a partial package must not be hashed in.
        if (IsStoreStagingEntryName(Utf8FileName(entry.path()))) {
          continue;
        }
        if (!symlink) {
          pending.emplace_back(root_index, entry.path());
        }
        continue;
      }
      if (!std::filesystem::is_regular_file(status)) {
        continue;
      }

      std::error_code size_ec;
      const std::uintmax_t size = entry.file_size(size_ec);
      std::error_code time_ec;
      const std::filesystem::file_time_type mtime = entry.last_write_time(time_ec);
      if (size_ec || time_ec) {
        *reason = entry.path().string() + ": " + (size_ec ? size_ec : time_ec).message();
        return false;
      }

      const std::u8string relative = entry.path().generic_u8string();
      TreeEntry collected;
      collected.key = std::to_string(root_index);
      collected.key += '|';
      collected.key.append(reinterpret_cast<const char*>(relative.data()), relative.size());
      collected.size = static_cast<uint64_t>(size);
      collected.mtime = static_cast<int64_t>(mtime.time_since_epoch().count());
      entries->push_back(std::move(collected));
    }
  }
  return true;
}

}  // namespace detail

// Folds the tree under `roots` into a fingerprint. `reason` is written when a root that
// exists cannot be read; the caller then has no valid proof and must scan.
//
// The entries are sorted by key before hashing so the fingerprint is stable across runs
// even where directory iteration order is not, which makes repeated scans comparable.
inline bool ComputeTreeFingerprint(const std::vector<std::filesystem::path>& roots,
                                      TreeFingerprint* out, std::string* reason) {
  out->hash = 0;
  out->file_count = 0;
  out->total_size = 0;

  std::vector<detail::TreeEntry> entries;
  if (!detail::CollectTreeEntries(roots, &entries, reason)) {
    return false;
  }
  std::sort(entries.begin(), entries.end(),
            [](const detail::TreeEntry& a, const detail::TreeEntry& b) { return a.key < b.key; });

  // The roots themselves are part of the identity: a different folder with identical
  // contents is still a different source, and the file name a package is filed under
  // depends on the whole set (UniqueLibraryFileName), not on one file alone.
  uint64_t hash = 0xCBF29CE484222325ull;
  detail::MixU64(&hash, roots.size());
  for (const auto& root : roots) {
    const std::u8string text = root.generic_u8string();
    const std::string_view view(reinterpret_cast<const char*>(text.data()), text.size());
    detail::MixU64(&hash, view.size());
    detail::MixBytes(&hash, view);
  }
  for (const detail::TreeEntry& entry : entries) {
    detail::MixU64(&hash, entry.key.size());
    detail::MixBytes(&hash, entry.key);
    detail::MixU64(&hash, entry.size);
    detail::MixU64(&hash, static_cast<uint64_t>(entry.mtime));
    out->total_size += entry.size;
  }

  out->hash = hash;
  out->file_count = entries.size();
  return true;
}

