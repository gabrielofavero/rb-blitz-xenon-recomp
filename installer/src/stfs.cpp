// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "stfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rb_blitz::installer {
namespace {

constexpr std::uint32_t kBlockSize = 0x1000;
constexpr std::uint32_t kEntriesPerBlock = kBlockSize / 0x40;
constexpr std::uint32_t kLevelSizes[3] = {170, 28900, 4913000};
constexpr std::uint32_t kEndOfChain = 0xFFFFFF;

constexpr std::size_t kHeaderSizeOffset = 0x340;
constexpr std::size_t kContentTypeOffset = 0x344;
constexpr std::size_t kMediaIdOffset = 0x354;
constexpr std::size_t kTitleIdOffset = 0x360;
constexpr std::size_t kVolumeDescriptorOffset = 0x379;
constexpr std::size_t kDataFileCountOffset = kVolumeDescriptorOffset + 0x24;
constexpr std::size_t kVolumeTypeOffset = kDataFileCountOffset + 4 + 8;

constexpr std::size_t kEntryFlagsOffset = 0x28;
constexpr std::size_t kEntryValidBlocksOffset = 0x29;
constexpr std::size_t kEntryAllocatedBlocksOffset = 0x2C;
constexpr std::size_t kEntryStartBlockOffset = 0x2F;
constexpr std::size_t kEntryParentOffset = 0x32;
constexpr std::size_t kEntryLengthOffset = 0x34;

constexpr std::uint8_t kEntryFlagContiguous = 0x40;
constexpr std::uint8_t kEntryFlagDirectory = 0x80;

constexpr std::uint16_t kRootParent = 0xFFFF;

std::uint32_t ReadU32BigEndian(const std::uint8_t* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}
std::uint16_t ReadU16LittleEndian(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}
std::uint16_t ReadU16BigEndian(const std::uint8_t* data) {
  return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
}
std::uint32_t ReadU24LittleEndian(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16);
}

std::uint64_t RoundUp(std::uint64_t value, std::uint64_t alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

}  // namespace

bool StfsContainer::Open(const std::filesystem::path& path, std::string* error) {
  source_ = std::make_unique<FileSource>();
  if (!source_->Open(path, error)) {
    return false;
  }
  if (source_->size() < kBlockSize * 2) {
    *error = "the file is too small to be an Xbox 360 package";
    return false;
  }

  std::uint8_t header[kBlockSize];
  if (source_->Read(0, header, sizeof(header)) != sizeof(header)) {
    *error = "cannot read the package header";
    return false;
  }
  const std::string magic(reinterpret_cast<const char*>(header), 4);
  if (magic != "LIVE" && magic != "PIRS" && magic != "CON ") {
    std::string printable;
    for (const char character : magic) {
      printable.push_back(character >= 32 && character < 127 ? character : '.');
    }
    *error = S("the file is not an Xbox 360 package (expected a LIVE, PIRS or CON header, got '",
               printable, "')");
    return false;
  }

  info_ = {};
  info_.total_bytes = source_->size();
  info_.header_size = ReadU32BigEndian(header + kHeaderSizeOffset);
  info_.content_type = ReadU32BigEndian(header + kContentTypeOffset);
  info_.media_id = ReadU32BigEndian(header + kMediaIdOffset);
  info_.title_id = ReadU32BigEndian(header + kTitleIdOffset);
  info_.flags = header[kVolumeDescriptorOffset + 2];
  info_.read_only_format = (info_.flags & 1) != 0;
  info_.volume_type = ReadU32BigEndian(header + kVolumeTypeOffset);
  info_.data_file_count = ReadU32BigEndian(header + kDataFileCountOffset);
  file_table_block_count_ = ReadU16LittleEndian(header + kVolumeDescriptorOffset + 3);
  file_table_block_number_ = ReadU24LittleEndian(header + kVolumeDescriptorOffset + 5);
  info_.total_blocks = ReadU32BigEndian(header + kVolumeDescriptorOffset + 0x1C);
  info_.free_blocks = ReadU32BigEndian(header + kVolumeDescriptorOffset + 0x20);
  info_.data_offset = RoundUp(info_.header_size, kBlockSize);

  if (info_.header_size < kBlockSize || info_.data_offset >= source_->size()) {
    *error = S("the package claims ", HumanBytes(info_.header_size),
               " of metadata, which does not fit inside the file");
    return false;
  }
  if (file_table_block_count_ == 0 || file_table_block_count_ > kEntriesPerBlock) {
    *error = S("the package declares an implausible file table of ", file_table_block_count_,
               " blocks");
    return false;
  }

  blocks_per_hash_table_ = info_.read_only_format ? 1 : 2;
  block_step_[0] = kLevelSizes[0] + blocks_per_hash_table_;
  block_step_[1] = kLevelSizes[1] + (kLevelSizes[0] + 1) * blocks_per_hash_table_;

  log::Detail(S("package: ", HumanBytes(info_.total_bytes), ", title ", HexU32(info_.title_id),
                ", media ", HexU32(info_.media_id), ", ", info_.total_blocks, " blocks"));

  return ReadFileTable(error);
}

std::uint64_t StfsContainer::BlockOffset(std::uint32_t block) const {
  std::uint32_t base = kLevelSizes[0];
  std::uint32_t out = block;
  for (int level = 0; level < 3; ++level) {
    out += ((block + base) / base) * blocks_per_hash_table_;
    if (block < base) {
      break;
    }
    base *= kLevelSizes[0];
  }
  return info_.data_offset + (static_cast<std::uint64_t>(out) << 12);
}

std::uint32_t StfsContainer::HashBlockNumber(std::uint32_t block, int level) const {
  if (level == 0) {
    if (block < kLevelSizes[0]) {
      return 0;
    }
    std::uint32_t value = (block / kLevelSizes[0]) * block_step_[0];
    value += ((block / kLevelSizes[1]) + 1) * blocks_per_hash_table_;
    if (block < kLevelSizes[1]) {
      return value;
    }
    return value + blocks_per_hash_table_;
  }
  if (level == 1) {
    if (block < kLevelSizes[1]) {
      return block_step_[0];
    }
    return (block / kLevelSizes[1]) * block_step_[1] + blocks_per_hash_table_;
  }
  return block_step_[1];
}

bool StfsContainer::ReadBlock(std::uint32_t block, std::uint8_t* out, std::size_t* read,
                              std::string* error) {
  const std::uint64_t offset = BlockOffset(block);
  if (offset >= source_->size()) {
    *error = S("block ", block, " lies past the end of the package");
    return false;
  }
  const std::size_t available =
      static_cast<std::size_t>(std::min<std::uint64_t>(kBlockSize, source_->size() - offset));
  const std::size_t got = source_->Read(offset, out, available);
  if (got != available) {
    *error = S("cannot read block ", block, " from the package");
    return false;
  }
  if (read != nullptr) {
    *read = got;
  }
  return true;
}

bool StfsContainer::NextBlock(std::uint32_t block, std::uint32_t* next, std::string* error) {
  const std::uint32_t table_block = HashBlockNumber(block, 0);
  std::uint8_t table[kBlockSize];
  std::size_t read = 0;
  if (!ReadBlock(table_block, table, &read, error)) {
    return false;
  }
  const std::size_t entry_offset = static_cast<std::size_t>(block % kLevelSizes[0]) * 0x18;
  if (entry_offset + 0x18 > read) {
    *error = S("the hash table of block ", block, " is shorter than expected");
    return false;
  }
  *next = ReadU32BigEndian(table + entry_offset + 0x14) & 0xFFFFFFu;
  return true;
}

bool StfsContainer::ReadFileTable(std::string* error) {
  entries_.clear();
  std::vector<std::string> paths;
  std::uint32_t table_block = file_table_block_number_;

  for (std::uint16_t block_index = 0; block_index < file_table_block_count_; ++block_index) {
    std::uint8_t block[kBlockSize];
    std::size_t read = 0;
    if (!ReadBlock(table_block, block, &read, error)) {
      return false;
    }
    for (std::uint32_t slot = 0; slot < kEntriesPerBlock; ++slot) {
      const std::uint8_t* entry = block + static_cast<std::size_t>(slot) * 0x40;
      if (entry[0] == 0) {
        // A zero first byte of the name marks the end of the table.
        SummariseFileTable();
        return true;
      }
      const std::uint8_t flags = entry[kEntryFlagsOffset];
      const std::size_t name_length = flags & 0x3F;
      if (name_length == 0 || name_length > 40) {
        *error = S("file table entry ", entries_.size(), " has an invalid name length");
        return false;
      }
      std::string name(reinterpret_cast<const char*>(entry), name_length);
      if (const std::size_t terminator = name.find('\0'); terminator != std::string::npos) {
        name.resize(terminator);
      }
      std::string reason;
      if (name.empty() || SafeRelativePath(name, &reason).empty()) {
        *error = S("file table entry ", entries_.size(), " has an unusable name: ",
                   reason.empty() ? "empty" : reason);
        return false;
      }
      if (name.find('/') != std::string::npos) {
        *error = S("file table entry name '", name, "' contains a path separator");
        return false;
      }

      StfsEntry file;
      file.name = name;
      file.is_directory = (flags & kEntryFlagDirectory) != 0;
      file.contiguous = (flags & kEntryFlagContiguous) != 0;
      file.start_block = ReadU24LittleEndian(entry + kEntryStartBlockOffset);
      file.valid_blocks = ReadU24LittleEndian(entry + kEntryValidBlocksOffset);
      file.allocated_blocks = ReadU24LittleEndian(entry + kEntryAllocatedBlocksOffset);
      file.length = ReadU32BigEndian(entry + kEntryLengthOffset);

      const std::uint16_t parent = ReadU16BigEndian(entry + kEntryParentOffset);
      if (parent != kRootParent) {
        if (parent >= paths.size()) {
          *error = S("file table entry '", name, "' refers to parent ", parent,
                     ", which comes after it");
          return false;
        }
        file.path = paths[parent] + "/" + name;
      } else {
        file.path = name;
      }

      if (!file.is_directory) {
        const std::uint64_t blocks = (file.length + kBlockSize - 1) / kBlockSize;
        if (blocks > 0 && file.start_block >= info_.total_blocks) {
          *error = S("file '", file.path, "' starts at block ", file.start_block,
                     ", past the end of the package");
          return false;
        }
        if (!file.contiguous && blocks > file.valid_blocks) {
          *error = S("file '", file.path, "' spans ", blocks, " blocks but only ", file.valid_blocks,
                     " are valid");
          return false;
        }
      }

      paths.push_back(file.path);
      entries_.push_back(std::move(file));
    }
    std::uint32_t next = kEndOfChain;
    if (!NextBlock(table_block, &next, error)) {
      return false;
    }
    if (next == kEndOfChain) {
      break;
    }
    table_block = next;
  }

  SummariseFileTable();
  return true;
}

void StfsContainer::SummariseFileTable() {
  info_.file_bytes = 0;
  std::uint64_t files = 0;
  for (const StfsEntry& entry : entries_) {
    if (!entry.is_directory) {
      info_.file_bytes += entry.length;
      ++files;
    }
  }
  log::Detail(S("package file table: ", entries_.size(), " entries, ", files, " files, ",
                HumanBytes(info_.file_bytes)));
}

const StfsEntry* StfsContainer::Find(std::string_view path) const {
  for (const StfsEntry& entry : entries_) {
    if (entry.path == path) {
      return &entry;
    }
  }
  return nullptr;
}

bool StfsContainer::Extract(const StfsEntry& entry, const Sink& sink, std::string* error) {
  if (entry.is_directory) {
    return true;
  }
  std::vector<std::uint8_t> block(kBlockSize);
  std::uint32_t current = entry.start_block;
  std::uint64_t remaining = entry.length;
  std::uint64_t blocks_walked = 0;

  while (remaining > 0) {
    std::size_t read = 0;
    if (!ReadBlock(current, block.data(), &read, error)) {
      *error = S("file '", entry.path, "': ", *error);
      return false;
    }
    const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(read, remaining));
    if (!sink(block.data(), take)) {
      *error = S("cannot write the contents of '", entry.path, "'");
      return false;
    }
    remaining -= take;
    ++blocks_walked;
    if (remaining == 0) {
      break;
    }
    if (entry.contiguous) {
      ++current;
    } else {
      std::uint32_t next = kEndOfChain;
      if (!NextBlock(current, &next, error)) {
        *error = S("file '", entry.path, "': ", *error);
        return false;
      }
      if (next == kEndOfChain) {
        *error = S("file '", entry.path, "' is truncated: its block chain ends after ",
                   blocks_walked, " blocks");
        return false;
      }
      current = next;
    }
  }
  return true;
}

bool StfsContainer::ExtractToFile(const StfsEntry& entry, const std::filesystem::path& destination,
                                  std::string* error) {
  FileSink file;
  if (!file.Open(destination, error)) {
    return false;
  }
  std::string sink_error;
  if (!Extract(entry, std::ref(file), &sink_error)) {
    file.Close(nullptr);
    RemoveFile(destination, nullptr);
    *error = sink_error;
    return false;
  }
  return file.Close(error);
}

}  // namespace rb_blitz::installer
