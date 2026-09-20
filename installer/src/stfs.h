// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Minimal reader for the Xbox 360 STFS (LIVE/PIRS/CON) package format.
//
// The wizard lets the user feed the installer the on-disk package they
// downloaded, instead of requiring them to extract it themselves. The layout
// reproduced here is the one the game's own filesystem device implements
// (rexglue-sdk/src/filesystem/devices/stfs_container_device.cpp), which is also
// the only reader this project ships, so the two agree by construction.
//
// Only what the installer needs is implemented: enumerate the file table, walk
// a block chain, and stream a file out. The hash tables are not validated
// (nothing here writes to a package, so a corrupt one can only produce files
// whose fingerprints the installer then rejects).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "inflate.h"
#include "util.h"

namespace rb_blitz::installer {

struct StfsEntry {
  std::string name;
  std::string path;  // '/'-separated, relative to the package root
  bool is_directory = false;
  bool contiguous = false;
  std::uint32_t start_block = 0;
  std::uint32_t valid_blocks = 0;
  std::uint32_t allocated_blocks = 0;
  std::uint64_t length = 0;
};

struct StfsInfo {
  std::uint32_t header_size = 0;
  std::uint32_t content_type = 0;
  std::uint32_t title_id = 0;
  std::uint32_t media_id = 0;
  std::uint32_t volume_type = 0;
  std::uint32_t data_file_count = 0;
  std::uint32_t total_blocks = 0;
  std::uint32_t free_blocks = 0;
  std::uint8_t flags = 0;
  bool read_only_format = false;
  std::uint64_t data_offset = 0;
  std::uint64_t total_bytes = 0;
  std::uint64_t file_bytes = 0;
};

class StfsContainer {
 public:
  StfsContainer() = default;
  StfsContainer(const StfsContainer&) = delete;
  StfsContainer& operator=(const StfsContainer&) = delete;

  bool Open(const std::filesystem::path& path, std::string* error);

  const StfsInfo& info() const { return info_; }
  const std::vector<StfsEntry>& entries() const { return entries_; }
  const StfsEntry* Find(std::string_view path) const;

  // Streams a file's blocks to `sink`, in order.
  bool Extract(const StfsEntry& entry, const Sink& sink, std::string* error);
  bool ExtractToFile(const StfsEntry& entry, const std::filesystem::path& destination,
                     std::string* error);

 private:
  bool ReadBlock(std::uint32_t block, std::uint8_t* out, std::size_t* read, std::string* error);
  bool NextBlock(std::uint32_t block, std::uint32_t* next, std::string* error);
  std::uint64_t BlockOffset(std::uint32_t block) const;
  std::uint32_t HashBlockNumber(std::uint32_t block, int level) const;
  bool ReadFileTable(std::string* error);
  void SummariseFileTable();

  std::unique_ptr<FileSource> source_;
  StfsInfo info_;
  std::vector<StfsEntry> entries_;
  std::uint32_t file_table_block_number_ = 0;
  std::uint16_t file_table_block_count_ = 0;
  std::uint32_t blocks_per_hash_table_ = 1;
  std::uint32_t block_step_[2] = {};
};

}  // namespace rb_blitz::installer
