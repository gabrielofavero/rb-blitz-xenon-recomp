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
// That matters in particular for a library on a volume without hard links or
// symlinks (exFAT, typically): it cannot be indexed by
// linking packages into a title_id/content_type tree, so the content manager has to
// resolve the library in place. Nothing here copies or renames a package.
//
// The walk is host path arithmetic, directory reading and a fixed-offset header read,
// and it is kept free of any SDK dependency for the same reason src/fs/dlc_layout.h is:
// tests/dlc_library_tests.cpp has to cover it without booting the game. The header
// reads are the whole cost of a cold scan (~17 ms per package on this machine's
// library, against ~2 ms for the walk itself), so a scan reads them on a small pool;
// boot can also reuse identities from a persistent cache after validating each file's
// size and mtime. The walk and merge stay in walk order, so the answer does not depend
// on how many threads read it. See docs/guides/dlc.md §2.1 and §4.1.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
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
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// A size the caller does not know. The walk in ScanDlcLibraries() reads the size off
// the directory entry, where it is free, so the scanner passes it in rather than
// paying a second stat per package; a caller that only has a path passes this and the
// size is stat'd as it always was.
inline constexpr std::uintmax_t kSizeUnknown = static_cast<std::uintmax_t>(-1);

// Reads the four magic bytes first, so an unrelated file in a library costs one small
// read and is silently not a package; a file that does carry the magic but cannot be
// mounted is reported instead. `reason` is only written for kUnreadable.
inline PackageIdentityResult ReadPackageIdentity(const std::filesystem::path& path,
                                                 const std::uintmax_t known_size,
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

  std::uintmax_t size = known_size;
  if (size == kSizeUnknown) {
    std::error_code ec;
    size = std::filesystem::file_size(path, ec);
    if (ec) {
      *reason = "cannot be read";
      return PackageIdentityResult::kUnreadable;
    }
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

// The size-less form, for a caller that only has the path (the tests, and anything
// outside the scanner).
inline PackageIdentityResult ReadPackageIdentity(const std::filesystem::path& path,
                                                 PackageIdentity* identity, std::string* reason) {
  return ReadPackageIdentity(path, kSizeUnknown, identity, reason);
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

// Counts the work a persistent identity scan did. A hit avoids opening the package;
// the directory entry still has to be visited so added and removed files are noticed.
struct LibraryScanCacheStats {
  std::size_t reused = 0;
  std::size_t read = 0;
  bool loaded = false;
  bool saved = false;
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

// Eight hex digits, the way the SDK and docs/guides/dlc.md spell a content type. Used to
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
// whole library (including for a package that is not saved-game). See docs/guides/dlc.md §2.1.
constexpr uint32_t LibraryContentType(uint32_t own_content_type, uint32_t forced) {
  if (forced != 0) {
    return forced;
  }
  return own_content_type == kSavedGameContentType ? kMarketplaceContentType : own_content_type;
}

// A library holds enough packages for the pool below to be worth starting.
inline constexpr std::size_t kMinParallelScanFiles = 16;

// The worker count a scan uses when it is not told one: enough to overlap the per-file
// cost a cold library pays, without opening a whole directory at once. Measured on this
// machine, a *first* open of a file costs ~16 ms while the same read warmed costs
// ~0.03 ms - a 500x gap that is not the SSD, and one charged per file rather than per
// byte (2,170 never-touched FFmpeg sources of 10-100 KB each measured the same 16.7 ms
// cold). Spreading the opens over eight threads measured **5.6x faster per file cold**
// on the production scan - 16.1 ms/file against 2.9 ms/file, two disjoint cold trees.
// Capped, so a machine with many cores does not open hundreds of files at once.
inline uint32_t DefaultDlcScanThreads() {
  const unsigned hardware = std::thread::hardware_concurrency();
  if (hardware <= 1) {
    return 1;
  }
  return std::min(hardware, 8u);
}

namespace detail {

// One regular file the walk found, with the size the directory entry already carried.
struct LibraryJob {
  std::filesystem::path path;
  std::uintmax_t size = kSizeUnknown;
  std::string cache_key;
  int64_t mtime = 0;
  bool cacheable = false;
};

// What the walk saw, in the order it saw it: either a directory that could not be
// read, or a file whose identity is to be read. Keeping that order is what lets the
// answers computed in parallel be merged back into exactly the result the walk would
// have produced on its own - item order, name assignment (which is order-dependent)
// and the rejected list all included.
struct LibraryWalkEvent {
  bool is_file = false;
  std::size_t job = 0;
  std::string entry;  // the directory that could not be read, when !is_file
};

struct LibraryJobOutcome {
  PackageIdentityResult result = PackageIdentityResult::kNotPackage;
  PackageIdentity identity;
  std::string reason;
  bool reused = false;
};

struct IdentityCacheRecord {
  uint64_t size = 0;
  int64_t mtime = 0;
  PackageIdentityResult result = PackageIdentityResult::kNotPackage;
  PackageIdentity identity;
};

using IdentityCache = std::map<std::string, IdentityCacheRecord>;
inline constexpr std::string_view kIdentityCacheMagic = "RBDLIC01";
inline constexpr std::size_t kMaxIdentityCacheBytes = 64 * 1024 * 1024;
inline constexpr uint32_t kMaxIdentityCacheEntries = 1000000;
inline constexpr uint32_t kMaxIdentityCacheKeyBytes = 32768;

inline std::string CacheKey(const std::filesystem::path& path) {
  const std::u8string utf8 = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

inline void AppendU32(std::string* bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes->push_back(static_cast<char>(value >> (i * 8)));
}

inline void AppendU64(std::string* bytes, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) bytes->push_back(static_cast<char>(value >> (i * 8)));
}

inline bool TakeU32(const std::string_view bytes, std::size_t* offset, uint32_t* value) {
  if (*offset > bytes.size() || bytes.size() - *offset < 4) return false;
  *value = 0;
  for (unsigned i = 0; i < 4; ++i)
    *value |= uint32_t(static_cast<uint8_t>(bytes[(*offset)++])) << (i * 8);
  return true;
}

inline bool TakeU64(const std::string_view bytes, std::size_t* offset, uint64_t* value) {
  if (*offset > bytes.size() || bytes.size() - *offset < 8) return false;
  *value = 0;
  for (unsigned i = 0; i < 8; ++i)
    *value |= uint64_t(static_cast<uint8_t>(bytes[(*offset)++])) << (i * 8);
  return true;
}

inline uint64_t CacheChecksum(const std::string_view bytes) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const char c : bytes) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 0x100000001B3ull;
  }
  return hash;
}

// Corruption, truncation, or an unknown version makes the cache a miss. The walker
// only looks up files it just saw, so cache contents cannot add an arbitrary package.
inline bool LoadIdentityCache(const std::filesystem::path& path, IdentityCache* out) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streampos end = file.tellg();
  if (end < 0 || static_cast<uint64_t>(end) > kMaxIdentityCacheBytes ||
      static_cast<uint64_t>(end) < kIdentityCacheMagic.size() + 4 + 8)
    return false;
  std::string bytes(static_cast<std::size_t>(end), '\0');
  file.seekg(0);
  if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
  const std::string_view body(bytes.data(), bytes.size() - 8);
  std::size_t checksum_offset = body.size();
  uint64_t stored_checksum = 0;
  if (!TakeU64(bytes, &checksum_offset, &stored_checksum) ||
      stored_checksum != CacheChecksum(body) ||
      body.substr(0, kIdentityCacheMagic.size()) != kIdentityCacheMagic)
    return false;

  std::size_t offset = kIdentityCacheMagic.size();
  uint32_t count = 0;
  if (!TakeU32(body, &offset, &count) || count > kMaxIdentityCacheEntries) return false;
  IdentityCache loaded;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t key_length = 0;
    uint64_t size = 0, mtime = 0;
    uint32_t result = 0, title_id = 0, content_type = 0;
    if (!TakeU32(body, &offset, &key_length) || key_length == 0 ||
        key_length > kMaxIdentityCacheKeyBytes || offset > body.size() ||
        body.size() - offset < key_length)
      return false;
    std::string key(body.substr(offset, key_length));
    offset += key_length;
    if (!TakeU64(body, &offset, &size) || !TakeU64(body, &offset, &mtime) ||
        !TakeU32(body, &offset, &result) || !TakeU32(body, &offset, &title_id) ||
        !TakeU32(body, &offset, &content_type) ||
        (result != static_cast<uint32_t>(PackageIdentityResult::kNotPackage) &&
         result != static_cast<uint32_t>(PackageIdentityResult::kRead)) ||
        (result == static_cast<uint32_t>(PackageIdentityResult::kRead) && title_id == 0))
      return false;
    IdentityCacheRecord record{size, static_cast<int64_t>(mtime),
                               static_cast<PackageIdentityResult>(result),
                               {title_id, content_type}};
    if (!loaded.emplace(std::move(key), record).second) return false;
  }
  if (offset != body.size()) return false;
  *out = std::move(loaded);
  return true;
}

inline bool SaveIdentityCache(const std::filesystem::path& path, const IdentityCache& entries) {
  if (entries.size() > kMaxIdentityCacheEntries) return false;
  std::string bytes(kIdentityCacheMagic);
  AppendU32(&bytes, static_cast<uint32_t>(entries.size()));
  for (const auto& [key, entry] : entries) {
    if (key.empty() || key.size() > kMaxIdentityCacheKeyBytes) return false;
    AppendU32(&bytes, static_cast<uint32_t>(key.size()));
    bytes.append(key);
    AppendU64(&bytes, entry.size);
    AppendU64(&bytes, static_cast<uint64_t>(entry.mtime));
    AppendU32(&bytes, static_cast<uint32_t>(entry.result));
    AppendU32(&bytes, entry.identity.title_id);
    AppendU32(&bytes, entry.identity.content_type);
    if (bytes.size() > kMaxIdentityCacheBytes - 8) return false;
  }
  AppendU64(&bytes, CacheChecksum(bytes));

  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
  }
  static std::atomic<uint64_t> serial{0};
  std::filesystem::path temporary = path;
  temporary += ".tmp-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
      std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
      std::filesystem::remove(temporary, ec);
      return false;
    }
    file.close();
    if (!file) {
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }
#if defined(_WIN32)
  const bool published = MoveFileExW(temporary.c_str(), path.c_str(),
                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::filesystem::rename(temporary, path, ec);
  const bool published = !ec;
#endif
  if (!published) std::filesystem::remove(temporary, ec);
  return published;
}

}  // namespace detail

// Walks every root (recursively, following real directories but not directory
// symlinks, which could loop) and returns one item per mountable container. A missing
// root is reported; an absent library is otherwise not an error.
//
// `force_content_type` overrides LibraryContentType() for every package when it is not
// zero.
//
// `scan_threads` is how many threads read the packages' headers: 0 picks
// DefaultDlcScanThreads(), 1 keeps every read on the calling thread (what this did
// before the pool existed), and any other value is that many workers. The reads are
// independent and are merged in walk order afterwards, so the thread count cannot
// change the answer - the same library answers identically at 1 and at 8.
inline LibraryScanResult ScanDlcLibrariesImpl(const std::vector<std::filesystem::path>& roots,
                                              const uint32_t force_content_type,
                                              const uint32_t scan_threads,
                                              const std::filesystem::path& cache_path,
                                              LibraryScanCacheStats* cache_stats) {
  LibraryScanCacheStats stats;
  detail::IdentityCache cached;
  if (!cache_path.empty()) stats.loaded = detail::LoadIdentityCache(cache_path, &cached);

  // The walk first, on this thread: it is directory reads and the sizes the directory
  // entries already carry (measured 2 ms for 1,402 packages), and it is what fixes the
  // order everything below is merged in.
  std::vector<detail::LibraryJob> jobs;
  std::vector<detail::LibraryWalkEvent> events;
  std::vector<std::filesystem::path> pending(roots.rbegin(), roots.rend());
  while (!pending.empty()) {
    const std::filesystem::path directory = pending.back();
    pending.pop_back();

    std::error_code ec;
    std::filesystem::directory_iterator entries(
        directory, std::filesystem::directory_options::none, ec);
    if (ec) {
      detail::LibraryWalkEvent event;
      event.entry = directory.generic_string();
      events.push_back(std::move(event));
      continue;
    }

    const std::filesystem::directory_iterator end;
    while (entries != end) {
      const auto entry = *entries;
      entries.increment(ec);
      if (ec) { events.push_back({false, 0, directory.generic_string()}); break; }
      const auto status = entry.status(ec);
      if (ec) { events.push_back({false, 0, entry.path().generic_string()}); continue; }
      if (std::filesystem::is_directory(status)) {
        // P5 of the RhythmVerse store plan: a package is staged in
        // `.rbbz-store-staging/` while it is being downloaded, and a partial `CON `
        // must never be enumerated - the scan reads magic, not names, so the
        // directory is skipped by name here as well as in the cache fingerprint.
        if (IsStoreStagingEntryName(Utf8FileName(entry.path()))) {
          continue;
        }
        if (!entry.is_symlink(ec)) {
          pending.push_back(entry.path());
        }
        continue;
      }
      if (!std::filesystem::is_regular_file(status)) {
        continue;
      }

      // The size is here already; passing it on is what saves ReadPackageIdentity() a
      // second stat of the same file.
      std::error_code size_ec;
      const std::uintmax_t size = entry.file_size(size_ec);

      detail::LibraryJob job;
      job.path = entry.path();
      job.size = size_ec ? kSizeUnknown : size;
      if (!cache_path.empty() && !size_ec) {
        std::error_code time_ec;
        const auto time = entry.last_write_time(time_ec);
        if (!time_ec) {
          job.cacheable = true;
          job.mtime = static_cast<int64_t>(time.time_since_epoch().count());
          job.cache_key = detail::CacheKey(job.path);
          if (job.cache_key.size() > detail::kMaxIdentityCacheKeyBytes) {
            job.cacheable = false;
            job.cache_key.clear();
          }
        }
      }

      detail::LibraryWalkEvent event;
      event.is_file = true;
      event.job = jobs.size();
      events.push_back(std::move(event));
      jobs.push_back(std::move(job));
    }
  }

  // Then the headers, on a pool when there is one. Every job writes only its own
  // outcome, so the workers share nothing but the queue.
  std::vector<detail::LibraryJobOutcome> outcomes(jobs.size());
  const detail::IdentityCache& reusable = cached;
  const auto read_one = [&](const std::size_t index) {
    detail::LibraryJobOutcome& outcome = outcomes[index];
    const detail::LibraryJob& job = jobs[index];
    if (job.cacheable && stats.loaded) {
      const auto cached_item = reusable.find(job.cache_key);
      if (cached_item != reusable.end() && cached_item->second.size == job.size &&
          cached_item->second.mtime == job.mtime) {
        outcome.result = cached_item->second.result;
        outcome.identity = cached_item->second.identity;
        outcome.reused = true;
        return;
      }
    }
    outcome.result = ReadPackageIdentity(jobs[index].path, jobs[index].size, &outcome.identity,
                                         &outcome.reason);
  };

  uint32_t threads = scan_threads == 0 ? DefaultDlcScanThreads() : scan_threads;
  if (jobs.size() < kMinParallelScanFiles) {
    threads = 1;
  }

  if (threads <= 1) {
    for (std::size_t i = 0; i < jobs.size(); ++i) {
      read_one(i);
    }
  } else {
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (uint32_t worker = 0; worker < threads; ++worker) {
      pool.emplace_back([&]() {
        for (;;) {
          const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
          if (index >= jobs.size()) {
            break;
          }
          read_one(index);
        }
      });
    }
    for (auto& thread : pool) {
      thread.join();
    }
  }

  if (!cache_path.empty()) {
    detail::IdentityCache current;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
      if (outcomes[i].reused) ++stats.reused;
      else ++stats.read;
      // A transient open/read error must be retried on the next boot. Successful
      // nonpackages are reusable too, so a folder of stray files stays cheap.
      if (!jobs[i].cacheable || outcomes[i].result == PackageIdentityResult::kUnreadable)
        continue;
      current.emplace(jobs[i].cache_key,
                      detail::IdentityCacheRecord{static_cast<uint64_t>(jobs[i].size),
                                                  jobs[i].mtime, outcomes[i].result,
                                                  outcomes[i].identity});
    }
    // A clean hit needs no write. Added, removed, or changed files publish one
    // complete snapshot after all header readers have joined.
    if (!stats.loaded || stats.read != 0 || current.size() != cached.size())
      stats.saved = detail::SaveIdentityCache(cache_path, current);
  }

  // And the merge, back on this thread and in walk order: the counters, the rejected
  // list and the name a package is filed under are all order-dependent, so replaying
  // the events is what makes the answer the one the sequential scan produced.
  LibraryScanResult result;
  std::map<uint64_t, std::set<std::string>> used_names;
  for (const detail::LibraryWalkEvent& event : events) {
    if (!event.is_file) {
      result.rejected.push_back({event.entry, "cannot be read"});
      continue;
    }

    ++result.files_seen;
    detail::LibraryJobOutcome& outcome = outcomes[event.job];
    switch (outcome.result) {
      case PackageIdentityResult::kNotPackage:
        ++result.other_files;
        continue;
      case PackageIdentityResult::kUnreadable:
        result.rejected.push_back(
            {jobs[event.job].path.generic_string(), std::move(outcome.reason)});
        continue;
      case PackageIdentityResult::kRead:
        break;
    }

    const PackageIdentity& identity = outcome.identity;
    const uint32_t content_type = LibraryContentType(identity.content_type, force_content_type);
    if (force_content_type == 0 && identity.content_type != content_type) {
      ++result.adapted_saved_games;
    }
    const uint64_t group = (uint64_t(identity.title_id) << 32) | content_type;
    std::string name =
        UniqueLibraryFileName(Utf8FileName(jobs[event.job].path), identity, &used_names[group]);
    result.items.push_back(
        {jobs[event.job].path, std::move(name), identity.title_id, content_type});
  }

  if (cache_stats) *cache_stats = stats;
  return result;
}

// The original no-cache path remains for callers that need a fresh read, including
// the in-game Refresh Songs route.
inline LibraryScanResult ScanDlcLibraries(const std::vector<std::filesystem::path>& roots,
                                          const uint32_t force_content_type = 0,
                                          const uint32_t scan_threads = 0) {
  return ScanDlcLibrariesImpl(roots, force_content_type, scan_threads, {}, nullptr);
}

// A boot scan reuses raw header identities for files whose path, size and mtime have
// not changed. A corrupt or absent cache reads every header and rewrites the snapshot.
inline LibraryScanResult ScanDlcLibrariesCached(const std::vector<std::filesystem::path>& roots,
                                                const std::filesystem::path& cache_path,
                                                const uint32_t force_content_type = 0,
                                                const uint32_t scan_threads = 0,
                                                LibraryScanCacheStats* stats = nullptr) {
  return ScanDlcLibrariesImpl(roots, force_content_type, scan_threads, cache_path, stats);
}

}  // namespace rb_blitz::fs
