// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Random-access byte sources and a streaming DEFLATE decoder.
//
// The installer unpacks two things: the payload archive it distributes itself
// and the Rock Band Blitz Ultimate release archive. Both are zip/deflate, and
// neither may be assumed small, so inflate works from a random-access source
// with a bounded input window and pushes bytes to a sink instead of building
// the whole output in memory. Nothing here is game-specific.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "util.h"

namespace rb_blitz::installer {

// --- byte sources ---------------------------------------------------------

class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual std::uint64_t size() const = 0;
  // Reads up to `size` bytes; a short read means end of source.
  virtual std::size_t Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) = 0;
};

class MemorySource final : public ByteSource {
 public:
  MemorySource(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  explicit MemorySource(const std::vector<std::uint8_t>& data)
      : data_(data.data()), size_(data.size()) {}
  std::uint64_t size() const override { return size_; }
  std::size_t Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) override;

 private:
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
};

// Buffered file reader. Held by the zip and STFS readers, both of which jump
// around, so the reads go through a read-ahead cache.
class FileSource final : public ByteSource {
 public:
  ~FileSource() override;
  FileSource() = default;
  FileSource(const FileSource&) = delete;
  FileSource& operator=(const FileSource&) = delete;

  bool Open(const std::filesystem::path& path, std::string* error);
  void Close();
  std::uint64_t size() const override { return size_; }
  std::size_t Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) override;

 private:
  std::FILE* file_ = nullptr;
  std::uint64_t size_ = 0;
  // Read-ahead cache: `cache_offset_` is the source offset of cache_[0].
  std::vector<std::uint8_t> cache_;
  std::uint64_t cache_offset_ = 0;
  std::size_t cache_size_ = 0;
};

// A window into another source, for members of a container.
class SliceSource final : public ByteSource {
 public:
  SliceSource(ByteSource* inner, std::uint64_t begin, std::uint64_t length)
      : inner_(inner), begin_(begin), length_(length) {}
  std::uint64_t size() const override { return length_; }
  std::size_t Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) override;

 private:
  ByteSource* inner_ = nullptr;
  std::uint64_t begin_ = 0;
  std::uint64_t length_ = 0;
};

// --- inflate --------------------------------------------------------------

// Receives decompressed bytes. Returning false aborts the decode; the caller
// then reports its own error (a full disk, a size guard, ...).
using Sink = std::function<bool(const std::uint8_t* data, std::size_t size)>;

// A sink that collects everything in memory, for the small members the
// installer handles.
class VectorSink {
 public:
  bool operator()(const std::uint8_t* data, std::size_t size) {
    bytes_.insert(bytes_.end(), data, data + size);
    return true;
  }
  const std::vector<std::uint8_t>& bytes() const { return bytes_; }
  std::vector<std::uint8_t> TakeBytes() { return std::move(bytes_); }

 private:
  std::vector<std::uint8_t> bytes_;
};

struct InflateStats {
  std::uint64_t input_bytes = 0;   // compressed bytes consumed
  std::uint64_t output_bytes = 0;  // decompressed bytes produced
  std::uint32_t crc32 = 0;         // of the decompressed data
};

// A sink that streams to a file, creating the parent directory first. It has to
// be passed to InflateRaw through std::ref because it owns the file handle.
class FileSink {
 public:
  ~FileSink();
  FileSink() = default;
  FileSink(const FileSink&) = delete;
  FileSink& operator=(const FileSink&) = delete;

  bool Open(const std::filesystem::path& path, std::string* error);
  bool operator()(const std::uint8_t* data, std::size_t size);
  bool Close(std::string* error);

  std::uint64_t bytes_written() const { return written_; }
  const std::string& error() const { return error_; }

 private:
  std::FILE* file_ = nullptr;
  std::uint64_t written_ = 0;
  std::string error_;
};

// Decompresses the raw deflate stream that starts at `offset` and spans the next
// `compressed_size` bytes of `source`. `max_output` of 0 means "as much as the
// stream produces"; anything else is a hard ceiling, so a malicious or corrupt
// stream cannot exhaust memory or fill the disk.
bool InflateRaw(ByteSource& source, std::uint64_t offset, std::uint64_t compressed_size,
                std::uint64_t max_output, const Sink& sink, InflateStats* stats,
                std::string* error);

// Convenience wrapper for the small members the installer handles (manifests,
// the Ultimate payload): decompresses into memory with a 512 MiB guard.
bool InflateRawToVector(ByteSource& source, std::uint64_t offset, std::uint64_t compressed_size,
                        std::vector<std::uint8_t>* out, std::uint32_t* crc32, std::string* error);

}  // namespace rb_blitz::installer
