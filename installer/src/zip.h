// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Read-only zip archive support.
//
// The installer unpacks two zip files it did not create: the payload archive
// that ships the recompiled build, and the Rock Band Blitz Ultimate release
// archive. Only what those need is implemented: stored and deflate members,
// names validated with SafeRelativePath so an archive can never write outside
// the destination directory, and streaming extraction that verifies the
// uncompressed size and CRC-32 from the central directory.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "inflate.h"
#include "util.h"

namespace rb_blitz::installer {

struct ZipEntry {
  std::string name;  // '/'-separated, validated, no trailing slash
  bool is_directory = false;
  std::uint16_t method = 0;  // 0 = stored, 8 = deflate
  std::uint32_t crc32 = 0;
  std::uint64_t compressed_size = 0;
  std::uint64_t uncompressed_size = 0;
  std::uint64_t local_header_offset = 0;
  std::uint64_t data_offset = 0;  // resolved from the local header at Open()
};

class ZipArchive {
 public:
  ZipArchive() = default;
  ZipArchive(const ZipArchive&) = delete;
  ZipArchive& operator=(const ZipArchive&) = delete;

  bool Open(const std::filesystem::path& path, std::string* error);
  // Takes ownership of the source, for archives that arrive in memory.
  bool Open(std::unique_ptr<ByteSource> source, std::string* error);

  const std::vector<ZipEntry>& entries() const { return entries_; }
  const ZipEntry* Find(std::string_view name) const;

  // Streams one member to `sink`, verifying the size and CRC-32 recorded in the
  // central directory.
  bool Extract(const ZipEntry& entry, const Sink& sink, std::string* error);
  bool ExtractToFile(const ZipEntry& entry, const std::filesystem::path& destination,
                     std::string* error);

  // Extracts everything `filter` accepts below `destination`, creating the
  // directories the entries imply. `on_entry` is called before each member, so
  // the caller can report progress. An unset (empty) `filter` accepts every
  // member, which is what a caller extracting a whole payload wants.
  bool ExtractAll(const std::filesystem::path& destination,
                  const std::function<bool(const ZipEntry&)>& filter,
                  const std::function<void(const ZipEntry&)>& on_entry, std::string* error);

 private:
  std::unique_ptr<ByteSource> source_;
  std::vector<ZipEntry> entries_;
  std::uint64_t archive_size_ = 0;
};

}  // namespace rb_blitz::installer
