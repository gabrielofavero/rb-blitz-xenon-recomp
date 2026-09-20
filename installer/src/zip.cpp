// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "zip.h"

#include <algorithm>
#include <cstring>

namespace rb_blitz::installer {
namespace {

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50;
constexpr std::uint32_t kZip64EndOfCentralDirectorySignature = 0x06064b50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50;

constexpr std::size_t kEndOfCentralDirectorySize = 22;
constexpr std::size_t kMaxCommentSize = 0xFFFF;
constexpr std::uint32_t kZip64Marker32 = 0xFFFFFFFFu;
constexpr std::uint16_t kZip64Marker16 = 0xFFFFu;

constexpr std::uint16_t kFlagEncrypted = 1u << 0;
constexpr std::uint16_t kMethodStored = 0;
constexpr std::uint16_t kMethodDeflate = 8;

std::uint16_t ReadU16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}
std::uint32_t ReadU32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}
std::uint64_t ReadU64(const std::uint8_t* data) {
  return static_cast<std::uint64_t>(ReadU32(data)) | (static_cast<std::uint64_t>(ReadU32(data + 4)) << 32);
}

// Sink adapter that keeps the running CRC-32 and length, so extraction can
// verify what the central directory promised.
struct VerifyingSink {
  const Sink& inner;
  std::uint32_t crc = 0;
  std::uint64_t count = 0;
  bool operator()(const std::uint8_t* data, std::size_t size) {
    crc = Crc32(data, size, crc);
    count += size;
    return inner(data, size);
  }
};

// Central directory entries are read from the archive and turned into a safe
// relative path. Names are treated as UTF-8; the archives the installer handles
// are produced by tools that set the UTF-8 flag.
bool ParseEntryName(std::string_view raw_name, bool is_directory, std::string* out,
                    std::string* error) {
  std::string name(raw_name);
  if (is_directory && !name.empty() && name.back() == '/') {
    name.pop_back();
  }
  if (name.empty()) {
    *error = "archive contains an entry with an empty name";
    return false;
  }
  std::string reason;
  if (SafeRelativePath(name, &reason).empty()) {
    *error = S("unsafe archive entry '", name, "': ", reason);
    return false;
  }
  *out = name;
  return true;
}

}  // namespace

bool ZipArchive::Open(const std::filesystem::path& path, std::string* error) {
  auto source = std::make_unique<FileSource>();
  if (!source->Open(path, error)) {
    return false;
  }
  return Open(std::move(source), error);
}

bool ZipArchive::Open(std::unique_ptr<ByteSource> source, std::string* error) {
  source_ = std::move(source);
  entries_.clear();
  archive_size_ = source_->size();
  if (archive_size_ < kEndOfCentralDirectorySize) {
    *error = "file is too small to be a zip archive";
    return false;
  }

  // The end-of-central-directory record sits after the comment, so the only way
  // to find it is to scan backwards over the biggest comment a zip can carry.
  const std::uint64_t tail_size =
      std::min<std::uint64_t>(archive_size_, kEndOfCentralDirectorySize + kMaxCommentSize);
  std::vector<std::uint8_t> tail(static_cast<std::size_t>(tail_size));
  if (source_->Read(archive_size_ - tail_size, tail.data(), tail.size()) != tail.size()) {
    *error = "cannot read the end of the archive";
    return false;
  }

  std::size_t eocd_position = std::string::npos;
  for (std::size_t position = tail.size() - kEndOfCentralDirectorySize;; --position) {
    if (ReadU32(tail.data() + position) == kEndOfCentralDirectorySignature) {
      eocd_position = position;
      break;
    }
    if (position == 0) {
      break;
    }
  }
  if (eocd_position == std::string::npos) {
    *error = "no end-of-central-directory record found; the file is not a zip archive";
    return false;
  }
  const std::uint8_t* eocd = tail.data() + eocd_position;
  std::uint64_t entry_count = ReadU16(eocd + 10);
  std::uint64_t directory_size = ReadU32(eocd + 12);
  std::uint64_t directory_offset = ReadU32(eocd + 16);

  // Zip64: the 32-bit fields are saturated and the real values live in the
  // zip64 end-of-central-directory record, which the locator points at.
  if (entry_count == kZip64Marker16 || directory_size == kZip64Marker32 ||
      directory_offset == kZip64Marker32) {
    const std::uint64_t eocd_offset = archive_size_ - tail_size + eocd_position;
    if (eocd_offset < 20) {
      *error = "zip64 archive is missing its end-of-central-directory locator";
      return false;
    }
    std::uint8_t locator[20];
    if (source_->Read(eocd_offset - 20, locator, sizeof(locator)) != sizeof(locator) ||
        ReadU32(locator) != kZip64LocatorSignature) {
      *error = "zip64 archive is missing its end-of-central-directory locator";
      return false;
    }
    const std::uint64_t zip64_offset = ReadU64(locator + 8);
    std::uint8_t record[56];
    if (source_->Read(zip64_offset, record, sizeof(record)) != sizeof(record) ||
        ReadU32(record) != kZip64EndOfCentralDirectorySignature) {
      *error = "zip64 end-of-central-directory record is missing";
      return false;
    }
    entry_count = ReadU64(record + 32);
    directory_size = ReadU64(record + 40);
    directory_offset = ReadU64(record + 48);
  }

  if (directory_offset + directory_size > archive_size_) {
    *error = "central directory lies outside the archive";
    return false;
  }
  if (entry_count == 0) {
    *error = "archive contains no entries";
    return false;
  }
  if (entry_count > 1000000) {
    *error = S("archive claims an implausible ", entry_count, " entries");
    return false;
  }

  std::vector<std::uint8_t> directory(static_cast<std::size_t>(directory_size));
  if (source_->Read(directory_offset, directory.data(), directory.size()) != directory.size()) {
    *error = "cannot read the central directory";
    return false;
  }

  entries_.reserve(static_cast<std::size_t>(entry_count));
  std::size_t position = 0;
  for (std::uint64_t index = 0; index < entry_count; ++index) {
    if (position + 46 > directory.size()) {
      *error = "central directory is truncated";
      return false;
    }
    const std::uint8_t* record = directory.data() + position;
    if (ReadU32(record) != kCentralHeaderSignature) {
      *error = S("central directory entry ", index, " has an unexpected signature");
      return false;
    }
    ZipEntry entry;
    const std::uint16_t flags = ReadU16(record + 8);
    entry.method = ReadU16(record + 10);
    entry.crc32 = ReadU32(record + 16);
    entry.compressed_size = ReadU32(record + 20);
    entry.uncompressed_size = ReadU32(record + 24);
    const std::size_t name_length = ReadU16(record + 28);
    const std::size_t extra_length = ReadU16(record + 30);
    const std::size_t comment_length = ReadU16(record + 32);
    entry.local_header_offset = ReadU32(record + 42);

    if (position + 46 + name_length + extra_length + comment_length > directory.size()) {
      *error = "central directory entry runs past the end of the directory";
      return false;
    }

    // Zip64 placeholders for this entry are resolved from its extra field, whose
    // values appear in the order the 32-bit fields were saturated.
    if (entry.uncompressed_size == kZip64Marker32 || entry.compressed_size == kZip64Marker32 ||
        entry.local_header_offset == kZip64Marker32) {
      std::size_t extra_position = position + 46 + name_length;
      const std::size_t extra_end = extra_position + extra_length;
      bool resolved = false;
      while (extra_position + 4 <= extra_end) {
        const std::uint16_t id = ReadU16(directory.data() + extra_position);
        const std::size_t size = ReadU16(directory.data() + extra_position + 2);
        const std::size_t data_position = extra_position + 4;
        if (data_position + size > extra_end) {
          break;
        }
        if (id == 0x0001) {
          const std::uint8_t* data = directory.data() + data_position;
          std::size_t consumed = 0;
          if (entry.uncompressed_size == kZip64Marker32 && consumed + 8 <= size) {
            entry.uncompressed_size = ReadU64(data + consumed);
            consumed += 8;
          }
          if (entry.compressed_size == kZip64Marker32 && consumed + 8 <= size) {
            entry.compressed_size = ReadU64(data + consumed);
            consumed += 8;
          }
          if (entry.local_header_offset == kZip64Marker32 && consumed + 8 <= size) {
            entry.local_header_offset = ReadU64(data + consumed);
          }
          resolved = true;
          break;
        }
        extra_position = data_position + size;
      }
      if (!resolved) {
        *error = S("zip64 entry '", std::string(reinterpret_cast<const char*>(record + 46), name_length),
                   "' is missing its zip64 extra field");
        return false;
      }
    }

    if ((flags & kFlagEncrypted) != 0) {
      *error = S("entry '",
                 std::string(reinterpret_cast<const char*>(record + 46), name_length),
                 "' is encrypted, which the installer cannot unpack");
      return false;
    }
    const bool is_directory =
        name_length > 0 && (record + 46)[name_length - 1] == '/';
    if (!ParseEntryName(std::string_view(reinterpret_cast<const char*>(record + 46), name_length),
                        is_directory, &entry.name, error)) {
      return false;
    }
    entry.is_directory = is_directory;
    if (entry.method != kMethodStored && entry.method != kMethodDeflate) {
      *error = S("entry '", entry.name, "' uses unsupported compression method ", entry.method);
      return false;
    }

    // The data offset needs the local header, which repeats the name and extra
    // field lengths and may differ from the central directory's copies.
    if (entry.local_header_offset + 30 > archive_size_) {
      *error = S("entry '", entry.name, "' points past the end of the archive");
      return false;
    }
    std::uint8_t local[30];
    if (source_->Read(entry.local_header_offset, local, sizeof(local)) != sizeof(local) ||
        ReadU32(local) != kLocalHeaderSignature) {
      *error = S("entry '", entry.name, "' has no local file header");
      return false;
    }
    const std::uint64_t local_name_length = ReadU16(local + 26);
    const std::uint64_t local_extra_length = ReadU16(local + 28);
    entry.data_offset = entry.local_header_offset + 30 + local_name_length + local_extra_length;
    if (entry.data_offset + entry.compressed_size > archive_size_) {
      *error = S("entry '", entry.name, "' is truncated");
      return false;
    }

    entries_.push_back(std::move(entry));
    position += 46 + name_length + extra_length + comment_length;
  }

  log::Detail(S("opened archive with ", entries_.size(), " entries (", HumanBytes(archive_size_),
                ")"));
  return true;
}

const ZipEntry* ZipArchive::Find(std::string_view name) const {
  for (const ZipEntry& entry : entries_) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

bool ZipArchive::Extract(const ZipEntry& entry, const Sink& sink, std::string* error) {
  if (entry.is_directory) {
    return true;
  }
  VerifyingSink verifying{sink};
  const Sink wrapper = std::ref(verifying);

  if (entry.method == kMethodStored) {
    if (entry.compressed_size != entry.uncompressed_size) {
      *error = S("entry '", entry.name, "' has mismatched stored sizes");
      return false;
    }
    std::vector<std::uint8_t> buffer(64 * 1024);
    std::uint64_t remaining = entry.compressed_size;
    std::uint64_t offset = entry.data_offset;
    while (remaining > 0) {
      const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
      const std::size_t read = source_->Read(offset, buffer.data(), want);
      if (read == 0) {
        *error = S("entry '", entry.name, "' ended unexpectedly");
        return false;
      }
      if (!wrapper(buffer.data(), read)) {
        *error = S("cannot write entry '", entry.name, "'");
        return false;
      }
      offset += read;
      remaining -= read;
    }
  } else {
    std::string inflate_error;
    if (!InflateRaw(*source_, entry.data_offset, entry.compressed_size, entry.uncompressed_size,
                    wrapper, nullptr, &inflate_error)) {
      *error = S("entry '", entry.name, "' does not decompress: ", inflate_error);
      return false;
    }
  }

  if (verifying.count != entry.uncompressed_size) {
    *error = S("entry '", entry.name, "' expanded to ", HumanBytes(verifying.count), " instead of ",
               HumanBytes(entry.uncompressed_size));
    return false;
  }
  if (verifying.crc != entry.crc32) {
    *error = S("entry '", entry.name, "' failed its CRC-32 check: expected ",
               HexU32(entry.crc32), ", got ", HexU32(verifying.crc));
    return false;
  }
  return true;
}

bool ZipArchive::ExtractToFile(const ZipEntry& entry, const std::filesystem::path& destination,
                               std::string* error) {
  if (entry.is_directory) {
    return EnsureDirectory(destination, error);
  }
  FileSink file;
  if (!file.Open(destination, error)) {
    return false;
  }
  if (!Extract(entry, std::ref(file), error)) {
    const std::string reason = *error;
    file.Close(nullptr);
    RemoveFile(destination, nullptr);
    *error = reason;
    return false;
  }
  return file.Close(error);
}

bool ZipArchive::ExtractAll(const std::filesystem::path& destination,
                            const std::function<bool(const ZipEntry&)>& filter,
                            const std::function<void(const ZipEntry&)>& on_entry,
                            std::string* error) {
  if (!EnsureDirectory(destination, error)) {
    return false;
  }
  std::uint64_t extracted = 0;
  for (const ZipEntry& entry : entries_) {
    if (filter && !filter(entry)) {
      continue;
    }
    if (on_entry) {
      on_entry(entry);
    }
    std::string relative_error;
    const std::filesystem::path target =
        SafeRelativePath(entry.name, &relative_error);
    if (target.empty()) {
      *error = S("unsafe archive entry '", entry.name, "': ", relative_error);
      return false;
    }
    const std::filesystem::path full = destination / target;
    if (!PathIsWithin(destination, full)) {
      *error = S("entry '", entry.name, "' would be written outside the destination directory");
      return false;
    }
    if (entry.is_directory) {
      if (!EnsureDirectory(full, error)) {
        return false;
      }
      continue;
    }
    if (!ExtractToFile(entry, full, error)) {
      return false;
    }
    ++extracted;
  }
  log::Detail(S("extracted ", extracted, " files into ", Narrow(destination.native())));
  return true;
}

}  // namespace rb_blitz::installer
