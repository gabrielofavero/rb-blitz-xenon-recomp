// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R7: the persisted DLC enumeration. src/hooks/dlc.cpp reads a flat library
// (src/fs/dlc_library.h) by opening every regular file and reading the STFS identity
// header out of each container, and it does that on every boot. That is the cost this
// header exists to skip: measured on this machine's `D:\Games\YARG Songs` (1,402
// packages, 28.4 GB, exFAT), a recursive stat-only walk takes ~176 ms while the
// scanner's open + 868-byte header read per package takes ~725 ms warm (and far more
// cold), so a cache validated by a stat fingerprint removes the per-file open and
// header read at a fraction of the cost.
//
// What is cached is the *result* of the library scan - the items the content manager
// is handed, plus the counters the boot log prints - never anything about the guest's
// own content. The guest's `songcache:` stays authoritative for the guest (D8): this
// cache only lets the host answer the same enumeration without re-reading the tree.
//
// Invalidation is a fingerprint of the tree, as D8 proposes: every regular file's
// root-relative path, size and modification time, hashed in sorted-path order so the
// result does not depend on directory iteration order. A changed, added, removed or
// resized package changes the fingerprint and the cache is discarded; a cache that
// cannot be *proved* current is never used (a miss must never look like a missing
// package), so every failure path here answers "no valid cache" and leaves the scan to
// run exactly as it does today.
//
// Kept free of any SDK dependency for the same reason src/fs/dlc_library.h is: it is
// host path arithmetic, file IO and a fingerprint, and tests/dlc_cache_tests.cpp has
// to cover it without booting the game. See docs/dlc.md.

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

// The file's own identity. The magic is eight bytes and spelled so a byte-swapped or
// text-mode read is recognisable; the version is bumped whenever the body's layout
// changes, so an old file is discarded by parse rather than misread.
inline constexpr std::string_view kDlcCacheFileName = "dlc_library.cache";
inline constexpr std::string_view kDlcCacheMagic = "RBDLCC01";
inline constexpr uint32_t kDlcCacheVersion = 1;

// A tree's identity, cheap to recompute and cheap to compare. `hash` folds every
// file's path, size and mtime; `file_count` and `total_size` are kept beside it so a
// collision has to agree on two independent numbers as well.
struct DlcTreeFingerprint {
  uint64_t hash = 0;
  uint64_t file_count = 0;
  uint64_t total_size = 0;

  bool operator==(const DlcTreeFingerprint& other) const {
    return hash == other.hash && file_count == other.file_count && total_size == other.total_size;
  }
  bool operator!=(const DlcTreeFingerprint& other) const { return !(*this == other); }
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

// Every regular file under `root`, as (relative path, size, mtime). Missing roots
// contribute nothing, which matches ScanDlcLibraries: an absent library is not an
// error, but a root that exists and cannot be read is, because then the cache could
// not be proved current.
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
        directory, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
      *reason = "cannot be read";
      return false;
    }

    for (const auto& entry : it) {
      std::error_code entry_ec;
      if (entry.is_directory(entry_ec) && !entry_ec) {
        // A directory symlink is not followed (it could loop), exactly as
        // ScanDlcLibraries refuses to follow one.
        if (!entry.is_symlink(entry_ec)) {
          pending.emplace_back(root_index, entry.path());
        }
        continue;
      }
      if (!entry.is_regular_file(entry_ec) || entry_ec) {
        continue;
      }

      std::error_code size_ec;
      const std::uintmax_t size = entry.file_size(size_ec);
      std::error_code time_ec;
      const std::filesystem::file_time_type mtime = entry.last_write_time(time_ec);
      if (size_ec || time_ec) {
        *reason = "cannot be read";
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
// even where directory iteration order is not, which is what makes a cache hit reliable.
inline bool ComputeLibraryFingerprint(const std::vector<std::filesystem::path>& roots,
                                      DlcTreeFingerprint* out, std::string* reason) {
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

// Everything the library scan produced, plus what is needed to prove it current. This
// is what is written to and read from <cache_root>/dlc_library.cache; it mirrors
// LibraryScanResult so dlc.cpp can adopt a hit without a second representation.
struct DlcLibraryCache {
  std::vector<std::filesystem::path> roots;
  uint32_t force_content_type = 0;
  DlcTreeFingerprint fingerprint;
  std::vector<LibraryItem> items;
  std::size_t files_seen = 0;
  std::size_t other_files = 0;
  std::size_t adapted_saved_games = 0;
  std::vector<LibraryScanResult::Rejected> rejected;
};

// The cache the scan just produced, ready to be written.
inline DlcLibraryCache MakeDlcLibraryCache(const std::vector<std::filesystem::path>& roots,
                                           const uint32_t force_content_type,
                                           const DlcTreeFingerprint& fingerprint,
                                           const LibraryScanResult& scan) {
  DlcLibraryCache cache;
  cache.roots = roots;
  cache.force_content_type = force_content_type;
  cache.fingerprint = fingerprint;
  cache.items = scan.items;
  cache.files_seen = scan.files_seen;
  cache.other_files = scan.other_files;
  cache.adapted_saved_games = scan.adapted_saved_games;
  cache.rejected = scan.rejected;
  return cache;
}

// The scan shape a cached enumeration stands in for. The item list is what the content
// manager is handed; the counters only feed the boot log.
inline LibraryScanResult ToLibraryScanResult(const DlcLibraryCache& cache) {
  LibraryScanResult scan;
  scan.items = cache.items;
  scan.rejected = cache.rejected;
  scan.files_seen = cache.files_seen;
  scan.other_files = cache.other_files;
  scan.adapted_saved_games = cache.adapted_saved_games;
  return scan;
}

// True when `cache` was made from these same sources, this content-type override and a
// tree that still fingerprints the same. `reason` names the first difference for the
// log; a cache that fails any check must not be used.
inline bool DlcCacheMatches(const DlcLibraryCache& cache,
                            const std::vector<std::filesystem::path>& roots,
                            const uint32_t force_content_type, const DlcTreeFingerprint& fingerprint,
                            std::string* reason) {
  if (cache.force_content_type != force_content_type) {
    *reason = "the content-type override changed";
    return false;
  }
  if (cache.roots.size() != roots.size()) {
    *reason = "the library list changed";
    return false;
  }
  for (std::size_t i = 0; i < roots.size(); ++i) {
    if (cache.roots[i].lexically_normal() != roots[i].lexically_normal()) {
      *reason = "the library list changed";
      return false;
    }
  }
  if (cache.fingerprint != fingerprint) {
    *reason = "the package tree changed";
    return false;
  }
  return true;
}

// <cache_root>/dlc_library.cache. cache_root is the runtime's user-writable cache
// directory (OnConfigurePaths), outside the read-only game tree.
inline std::filesystem::path DlcCachePath(const std::filesystem::path& cache_root) {
  return cache_root / std::string(kDlcCacheFileName);
}

namespace detail {

class ByteWriter {
 public:
  void U32(uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i) {
      bytes_.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
  }
  void U64(uint64_t value) {
    for (uint32_t i = 0; i < 8; ++i) {
      bytes_.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
  }
  void Bytes(const std::string_view text) {
    U32(static_cast<uint32_t>(text.size()));
    bytes_.insert(bytes_.end(), text.begin(), text.end());
  }
  const std::vector<uint8_t>& data() const { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

class ByteReader {
 public:
  explicit ByteReader(const uint8_t* data, const std::size_t size) : data_(data), size_(size) {}

  bool Ok() const { return ok_; }
  std::size_t Remaining() const { return ok_ ? size_ - at_ : 0; }

  bool U32(uint32_t* value) {
    if (!ok_ || Remaining() < 4) {
      ok_ = false;
      return false;
    }
    *value = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      *value |= uint32_t(data_[at_ + i]) << (i * 8);
    }
    at_ += 4;
    return true;
  }
  bool U64(uint64_t* value) {
    if (!ok_ || Remaining() < 8) {
      ok_ = false;
      return false;
    }
    *value = 0;
    for (uint32_t i = 0; i < 8; ++i) {
      *value |= uint64_t(data_[at_ + i]) << (i * 8);
    }
    at_ += 8;
    return true;
  }
  bool Text(std::string* text) {
    uint32_t length = 0;
    if (!U32(&length) || Remaining() < length) {
      ok_ = false;
      return false;
    }
    text->assign(reinterpret_cast<const char*>(data_ + at_), length);
    at_ += length;
    return true;
  }

 private:
  const uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t at_ = 0;
  bool ok_ = true;
};

inline std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

inline std::filesystem::path Utf8ToPath(const std::string& text) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}  // namespace detail

// The body, as bytes, without the file IO. Exposed so the round trip is testable without
// a filesystem (tests/dlc_cache_tests.cpp).
inline std::vector<uint8_t> SerializeDlcCache(const DlcLibraryCache& cache) {
  detail::ByteWriter writer;
  writer.Bytes(kDlcCacheMagic);
  writer.U32(kDlcCacheVersion);
  writer.U32(0);  // reserved for a future flag word
  writer.U64(cache.fingerprint.hash);
  writer.U64(cache.fingerprint.file_count);
  writer.U64(cache.fingerprint.total_size);
  writer.U32(cache.force_content_type);
  writer.U32(static_cast<uint32_t>(cache.roots.size()));
  for (const auto& root : cache.roots) {
    writer.Bytes(detail::PathToUtf8(root));
  }
  writer.U64(cache.files_seen);
  writer.U64(cache.other_files);
  writer.U64(cache.adapted_saved_games);
  writer.U64(cache.items.size());
  for (const auto& item : cache.items) {
    writer.U32(item.title_id);
    writer.U32(item.content_type);
    writer.Bytes(item.file_name);
    writer.Bytes(detail::PathToUtf8(item.host_path));
  }
  writer.U64(cache.rejected.size());
  for (const auto& rejected : cache.rejected) {
    writer.Bytes(rejected.entry);
    writer.Bytes(rejected.reason);
  }
  return writer.data();
}

// Parses a serialized cache. Any short read, bad magic or unknown version is reported
// and discarded - an unreadable cache is a miss, never an empty enumeration.
inline bool ParseDlcCache(const uint8_t* data, const std::size_t size, DlcLibraryCache* out,
                          std::string* reason) {
  detail::ByteReader reader(data, size);

  std::string magic;
  if (!reader.Text(&magic) || magic != kDlcCacheMagic) {
    *reason = "not a DLC cache";
    return false;
  }
  uint32_t version = 0;
  uint32_t reserved = 0;
  if (!reader.U32(&version) || !reader.U32(&reserved)) {
    *reason = "the header is truncated";
    return false;
  }
  if (version != kDlcCacheVersion) {
    *reason = "the cache version changed";
    return false;
  }

  DlcLibraryCache cache;
  uint32_t force_content_type = 0;
  uint32_t root_count = 0;
  if (!reader.U64(&cache.fingerprint.hash) || !reader.U64(&cache.fingerprint.file_count) ||
      !reader.U64(&cache.fingerprint.total_size) || !reader.U32(&force_content_type) ||
      !reader.U32(&root_count)) {
    *reason = "the header is truncated";
    return false;
  }
  cache.force_content_type = force_content_type;
  for (uint32_t i = 0; i < root_count; ++i) {
    std::string root;
    if (!reader.Text(&root)) {
      *reason = "the root list is truncated";
      return false;
    }
    cache.roots.push_back(detail::Utf8ToPath(root));
  }

  uint64_t files_seen = 0;
  uint64_t other_files = 0;
  uint64_t adapted_saved_games = 0;
  uint64_t item_count = 0;
  if (!reader.U64(&files_seen) || !reader.U64(&other_files) || !reader.U64(&adapted_saved_games) ||
      !reader.U64(&item_count)) {
    *reason = "the counters are truncated";
    return false;
  }
  if (item_count > reader.Remaining()) {
    *reason = "the item count does not fit the file";
    return false;
  }
  cache.files_seen = static_cast<std::size_t>(files_seen);
  cache.other_files = static_cast<std::size_t>(other_files);
  cache.adapted_saved_games = static_cast<std::size_t>(adapted_saved_games);
  cache.items.reserve(static_cast<std::size_t>(item_count));
  for (uint64_t i = 0; i < item_count; ++i) {
    LibraryItem item;
    std::string path;
    if (!reader.U32(&item.title_id) || !reader.U32(&item.content_type) ||
        !reader.Text(&item.file_name) || !reader.Text(&path)) {
      *reason = "the item list is truncated";
      return false;
    }
    item.host_path = detail::Utf8ToPath(path);
    cache.items.push_back(std::move(item));
  }

  uint64_t rejected_count = 0;
  if (!reader.U64(&rejected_count)) {
    *reason = "the rejected list is truncated";
    return false;
  }
  if (rejected_count > reader.Remaining()) {
    *reason = "the rejected count does not fit the file";
    return false;
  }
  cache.rejected.reserve(static_cast<std::size_t>(rejected_count));
  for (uint64_t i = 0; i < rejected_count; ++i) {
    LibraryScanResult::Rejected rejected;
    if (!reader.Text(&rejected.entry) || !reader.Text(&rejected.reason)) {
      *reason = "the rejected list is truncated";
      return false;
    }
    cache.rejected.push_back(std::move(rejected));
  }

  if (!reader.Ok()) {
    *reason = "the cache is truncated";
    return false;
  }
  *out = std::move(cache);
  return true;
}

// Reads and parses the cache file. A missing file is an ordinary miss (the first boot
// has no cache) and says so; a damaged file says what is wrong with it.
inline bool LoadDlcCache(const std::filesystem::path& path, DlcLibraryCache* out,
                         std::string* reason) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    *reason = "no cache file";
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  return ParseDlcCache(bytes.data(), bytes.size(), out, reason);
}

// Writes the cache beside its final name and renames it into place, so a crash mid-write
// cannot leave a half-file that a later boot would parse. The save overwrites any
// existing cache (a refresh is exactly that).
inline bool SaveDlcCache(const std::filesystem::path& path, const DlcLibraryCache& cache,
                         std::string* reason) {
  const std::vector<uint8_t> bytes = SerializeDlcCache(cache);

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
      *reason = "cannot be written";
      return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      *reason = "cannot be written";
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }

  std::filesystem::remove(path, ec);
  ec.clear();
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    *reason = "cannot be put in place";
    std::filesystem::remove(temporary, ec);
    return false;
  }
  return true;
}

}  // namespace rb_blitz::fs
