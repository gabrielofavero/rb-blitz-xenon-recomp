// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "inflate.h"

#include <algorithm>
#include <cstring>

namespace rb_blitz::installer {
namespace {

constexpr std::size_t kInputBufferSize = 64 * 1024;
constexpr std::size_t kOutputBufferSize = 128 * 1024;
constexpr std::size_t kWindowSize = 32768;
constexpr std::size_t kWindowMask = kWindowSize - 1;

// --- bit reader -----------------------------------------------------------

// LSB-first bit reader over a bounded slice of a source. The first bit of the
// stream is the lowest bit of `bits_`, which is what the fast Huffman table
// below indexes with.
class BitReader {
 public:
  BitReader(ByteSource& source, std::uint64_t offset, std::uint64_t length)
      : source_(source), next_offset_(offset), limit_(offset + length) {}

  // Buffers at least `bits` bits. False when the input runs out first.
  bool Need(int bits) {
    while (bit_count_ < bits) {
      if (!Refill()) {
        return false;
      }
    }
    return true;
  }
  int Peek(int bits) const { return static_cast<int>(bits_ & ((1u << bits) - 1u)); }
  void Drop(int bits) {
    bits_ >>= bits;
    bit_count_ -= bits;
  }
  bool Bit(int* out) {
    if (!Need(1)) {
      return false;
    }
    *out = static_cast<int>(bits_ & 1u);
    Drop(1);
    return true;
  }
  void AlignToByte() { Drop(bit_count_ & 7); }

  // Byte-aligned read that pulls whole bytes out of the buffered bits first and
  // then straight from the source.
  std::size_t Consume(std::uint8_t* out, std::size_t size) {
    std::size_t written = 0;
    while (written < size && bit_count_ >= 8) {
      out[written++] = static_cast<std::uint8_t>(bits_ & 0xFFu);
      Drop(8);
    }
    while (written < size) {
      if (input_pos_ == input_len_ && !RefillBuffer()) {
        break;
      }
      const std::size_t available = input_len_ - input_pos_;
      const std::size_t take = std::min(size - written, available);
      std::memcpy(out + written, input_.data() + input_pos_, take);
      input_pos_ += take;
      written += take;
    }
    return written;
  }

  // Compressed bytes actually consumed, including the bits still buffered.
  std::uint64_t ConsumedInput() const {
    const std::size_t buffered = input_len_ - input_pos_;
    return buffered_total_ - buffered;
  }
  bool Exhausted() const { return next_offset_ >= limit_ && input_pos_ == input_len_; }

 private:
  bool Refill() {
    if (bit_count_ > 56) {
      return false;  // never expected: callers ask for at most 16 bits
    }
    if (input_pos_ == input_len_ && !RefillBuffer()) {
      return false;
    }
    bits_ |= static_cast<std::uint64_t>(input_[input_pos_++]) << bit_count_;
    bit_count_ += 8;
    return true;
  }
  bool RefillBuffer() {
    if (next_offset_ >= limit_) {
      return false;
    }
    const std::uint64_t remaining = limit_ - next_offset_;
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(kInputBufferSize, remaining));
    input_len_ = source_.Read(next_offset_, input_.data(), want);
    input_pos_ = 0;
    if (input_len_ == 0) {
      return false;
    }
    next_offset_ += input_len_;
    buffered_total_ += input_len_;
    return true;
  }

  ByteSource& source_;
  std::uint64_t next_offset_ = 0;
  std::uint64_t limit_ = 0;
  std::uint64_t buffered_total_ = 0;
  std::array<std::uint8_t, kInputBufferSize> input_{};
  std::size_t input_pos_ = 0;
  std::size_t input_len_ = 0;
  std::uint64_t bits_ = 0;
  int bit_count_ = 0;
};

// --- Huffman --------------------------------------------------------------

// Canonical Huffman decoder in the shape of zlib's "puff" reference decoder,
// with a 9-bit first-level table on top so the common codes cost one lookup.
class Huffman {
 public:
  bool Build(const std::uint8_t* lengths, std::size_t count, std::string* error);

  // Returns the symbol, or -1 on a corrupt or truncated code.
  int Decode(BitReader& reader) const;

 private:
  struct FastEntry {
    std::int16_t symbol = -1;
    std::int8_t bits = 0;
  };

  std::uint16_t counts_[16] = {};
  std::size_t symbol_count_ = 0;
  bool usable_ = false;
  std::vector<std::uint16_t> symbols_;   // ordered by (code length, symbol)
  std::vector<std::uint8_t> lengths_;    // by symbol
  std::array<FastEntry, 512> fast_{};
};

std::uint32_t ReverseBits(std::uint32_t value, int bits) {
  std::uint32_t reversed = 0;
  for (int index = 0; index < bits; ++index) {
    reversed = (reversed << 1) | (value & 1u);
    value >>= 1;
  }
  return reversed;
}

bool Huffman::Build(const std::uint8_t* lengths, std::size_t count, std::string* error) {
  lengths_.assign(lengths, lengths + count);
  for (std::uint16_t& slot : counts_) {
    slot = 0;
  }
  for (const std::uint8_t length : lengths_) {
    if (length > 15) {
      *error = S("code length ", length, " is out of range");
      return false;
    }
    ++counts_[length];
  }
  counts_[0] = 0;
  symbol_count_ = 0;
  for (int length = 1; length <= 15; ++length) {
    symbol_count_ += counts_[length];
  }

  int left = 1;
  for (int length = 1; length <= 15; ++length) {
    left <<= 1;
    left -= counts_[length];
    if (left < 0) {
      *error = "over-subscribed Huffman code";
      return false;
    }
  }
  // An incomplete code is only meaningful as "one distance code" (zlib accepts
  // it) or as "no code at all", which is legal for an unused distance tree.
  if (left > 0 && symbol_count_ > 1) {
    *error = "incomplete Huffman code";
    return false;
  }
  if (symbol_count_ == 0) {
    usable_ = false;
    return true;
  }

  std::array<std::uint32_t, 16> offsets{};
  for (int length = 2; length <= 15; ++length) {
    offsets[length] = offsets[length - 1] + counts_[length - 1];
  }
  symbols_.assign(symbol_count_, 0);
  for (std::size_t symbol = 0; symbol < lengths_.size(); ++symbol) {
    if (lengths_[symbol] != 0) {
      symbols_[offsets[lengths_[symbol]]++] = static_cast<std::uint16_t>(symbol);
    }
  }

  fast_.fill(FastEntry{});
  std::array<std::uint32_t, 16> next_code{};
  std::uint32_t code = 0;
  for (int length = 1; length <= 15; ++length) {
    code = (code + counts_[length - 1]) << 1;
    next_code[length] = code;
  }
  for (const std::uint16_t symbol : symbols_) {
    const int length = lengths_[symbol];
    const std::uint32_t symbol_code = next_code[length]++;
    if (length > 9) {
      break;  // symbols_ is ordered by length: nothing shorter follows
    }
    const std::uint32_t reversed = ReverseBits(symbol_code, length);
    for (std::uint32_t index = reversed; index < fast_.size(); index += 1u << length) {
      fast_[index].symbol = static_cast<std::int16_t>(symbol);
      fast_[index].bits = static_cast<std::int8_t>(length);
    }
  }
  usable_ = true;
  return true;
}

int Huffman::Decode(BitReader& reader) const {
  if (!usable_) {
    return -1;
  }
  if (reader.Need(9)) {
    const FastEntry& entry = fast_[static_cast<std::size_t>(reader.Peek(9))];
    if (entry.bits != 0) {
      reader.Drop(entry.bits);
      return entry.symbol;
    }
  }
  // Codes longer than nine bits: walk the canonical ranges bit by bit, the way
  // puff does, since the table above only covers the first nine bits.
  int code = 0;
  int first = 0;
  std::size_t index = 0;
  for (int length = 1; length <= 15; ++length) {
    int bit = 0;
    if (!reader.Bit(&bit)) {
      return -1;
    }
    code |= bit;
    const int count = counts_[length];
    if (code - count < first) {
      return symbols_[index + static_cast<std::size_t>(code - first)];
    }
    index += static_cast<std::size_t>(count);
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

const Huffman& FixedLiterals() {
  static const Huffman tree = [] {
    std::uint8_t lengths[288];
    for (int symbol = 0; symbol < 144; ++symbol) {
      lengths[symbol] = 8;
    }
    for (int symbol = 144; symbol < 256; ++symbol) {
      lengths[symbol] = 9;
    }
    for (int symbol = 256; symbol < 280; ++symbol) {
      lengths[symbol] = 7;
    }
    for (int symbol = 280; symbol < 288; ++symbol) {
      lengths[symbol] = 8;
    }
    Huffman tree;
    std::string error;
    tree.Build(lengths, 288, &error);
    return tree;
  }();
  return tree;
}

const Huffman& FixedDistances() {
  static const Huffman tree = [] {
    // The fixed distance code is 32 five-bit codes, of which 30 map to a
    // distance. Building all 32 keeps the code space complete; symbol 30 and 31
    // are rejected where they are used.
    std::uint8_t lengths[32];
    for (std::uint8_t& length : lengths) {
      length = 5;
    }
    Huffman tree;
    std::string error;
    tree.Build(lengths, 32, &error);
    return tree;
  }();
  return tree;
}

constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,
                                          17, 19, 23, 27, 31, 35, 43, 51,  59,  67,  83,
                                          99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistanceBase[30] = {1,    2,    3,    4,    5,    7,     9,
                                             13,   17,   25,   33,   49,   65,    97,
                                             129,  193,  257,  385,  513,  769,   1025,
                                             1537, 2049, 3073, 4097, 6145, 8193,  12289,
                                             16385, 24577};
constexpr std::uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                             4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                             9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace

// --- sources --------------------------------------------------------------

std::size_t MemorySource::Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) {
  if (offset >= size_) {
    return 0;
  }
  const std::size_t available = static_cast<std::size_t>(size_ - offset);
  const std::size_t take = std::min(size, available);
  std::memcpy(out, data_ + offset, take);
  return take;
}

FileSource::~FileSource() { Close(); }

bool FileSource::Open(const std::filesystem::path& path, std::string* error) {
  Close();
  file_ = _wfopen(path.c_str(), L"rb");
  if (file_ == nullptr) {
    *error = S("cannot open ", Narrow(path.native()), ": ", LastWin32Error());
    return false;
  }
  if (_fseeki64(file_, 0, SEEK_END) != 0) {
    *error = S("cannot seek in ", Narrow(path.native()));
    Close();
    return false;
  }
  const __int64 size = _ftelli64(file_);
  if (size < 0) {
    *error = S("cannot determine the size of ", Narrow(path.native()));
    Close();
    return false;
  }
  _fseeki64(file_, 0, SEEK_SET);
  size_ = static_cast<std::uint64_t>(size);
  cache_.resize(kInputBufferSize);
  cache_offset_ = 0;
  cache_size_ = 0;
  return true;
}

void FileSource::Close() {
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  size_ = 0;
  cache_size_ = 0;
  cache_offset_ = 0;
}

std::size_t FileSource::Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) {
  if (file_ == nullptr || size == 0 || offset >= size_) {
    return 0;
  }
  size = static_cast<std::size_t>(std::min<std::uint64_t>(size, size_ - offset));

  if (offset >= cache_offset_ && offset + size <= cache_offset_ + cache_size_) {
    std::memcpy(out, cache_.data() + (offset - cache_offset_), size);
    return size;
  }
  if (size >= cache_.size() / 2) {
    // A big read is cheaper straight from the file than through the cache.
    if (_fseeki64(file_, static_cast<__int64>(offset), SEEK_SET) != 0) {
      return 0;
    }
    return std::fread(out, 1, size, file_);
  }

  if (_fseeki64(file_, static_cast<__int64>(offset), SEEK_SET) != 0) {
    return 0;
  }
  const std::size_t want = static_cast<std::size_t>(
      std::min<std::uint64_t>(cache_.size(), size_ - offset));
  cache_offset_ = offset;
  cache_size_ = std::fread(cache_.data(), 1, want, file_);
  const std::size_t take = std::min(size, cache_size_);
  std::memcpy(out, cache_.data(), take);
  return take;
}

std::size_t SliceSource::Read(std::uint64_t offset, std::uint8_t* out, std::size_t size) {
  if (offset >= length_) {
    return 0;
  }
  const std::uint64_t available = length_ - offset;
  const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(size, available));
  if (take == 0) {
    return 0;
  }
  return inner_->Read(begin_ + offset, out, take);
}

// --- inflate --------------------------------------------------------------

FileSink::~FileSink() {
  if (file_ != nullptr) {
    std::fclose(file_);
  }
}

bool FileSink::Open(const std::filesystem::path& path, std::string* error) {
  if (!EnsureParentDirectory(path, error)) {
    return false;
  }
  file_ = _wfopen(path.c_str(), L"wb");
  if (file_ == nullptr) {
    error_ = S("cannot create ", Narrow(path.native()), ": ", LastWin32Error());
    *error = error_;
    return false;
  }
  written_ = 0;
  return true;
}

bool FileSink::operator()(const std::uint8_t* data, std::size_t size) {
  if (file_ == nullptr || size == 0) {
    return file_ != nullptr;
  }
  if (std::fwrite(data, 1, size, file_) != size) {
    error_ = S("write failed: ", LastWin32Error());
    return false;
  }
  written_ += size;
  return true;
}

bool FileSink::Close(std::string* error) {
  if (file_ == nullptr) {
    return true;
  }
  const bool flushed = std::fflush(file_) == 0;
  const bool closed = std::fclose(file_) == 0;
  file_ = nullptr;
  if (!flushed || !closed) {
    error_ = S("cannot finalise the file: ", LastWin32Error());
  }
  if (!error_.empty()) {
    if (error != nullptr) {
      *error = error_;
    }
    return false;
  }
  return true;
}

bool InflateRaw(ByteSource& source, std::uint64_t offset, std::uint64_t compressed_size,
                std::uint64_t max_output, const Sink& sink, InflateStats* stats,
                std::string* error) {
  BitReader reader(source, offset, compressed_size);

  std::vector<std::uint8_t> window(kWindowSize, 0);
  std::size_t window_pos = 0;
  std::vector<std::uint8_t> pending;
  pending.reserve(kOutputBufferSize);

  std::uint64_t produced = 0;
  std::uint32_t crc = 0;
  bool limit_hit = false;
  bool sink_failed = false;

  const auto flush = [&]() -> bool {
    if (pending.empty()) {
      return true;
    }
    if (max_output != 0 && produced + pending.size() > max_output) {
      limit_hit = true;
      return false;
    }
    crc = Crc32(pending.data(), pending.size(), crc);
    produced += pending.size();
    if (!sink(pending.data(), pending.size())) {
      sink_failed = true;
      return false;
    }
    pending.clear();
    return true;
  };
  const auto emit = [&](std::uint8_t byte) -> bool {
    window[window_pos] = byte;
    window_pos = (window_pos + 1) & kWindowMask;
    pending.push_back(byte);
    if (pending.size() >= kOutputBufferSize) {
      if (max_output != 0 && produced + pending.size() > max_output) {
        limit_hit = true;
        return false;
      }
      return flush();
    }
    return true;
  };

  Huffman literals;
  Huffman distances;

  for (;;) {
    int final_block = 0;
    if (!reader.Bit(&final_block)) {
      *error = "truncated deflate stream (block header)";
      return false;
    }
    if (!reader.Need(2)) {
      *error = "truncated deflate stream (block type)";
      return false;
    }
    const int block_type = reader.Peek(2);
    reader.Drop(2);

    if (block_type == 0) {
      reader.AlignToByte();
      std::uint8_t header[4];
      if (reader.Consume(header, sizeof(header)) != sizeof(header)) {
        *error = "truncated stored block header";
        return false;
      }
      const auto length = static_cast<std::uint16_t>(header[0] | (header[1] << 8));
      const auto inverse = static_cast<std::uint16_t>(header[2] | (header[3] << 8));
      if (static_cast<std::uint16_t>(~length) != inverse) {
        *error = "stored block length and its complement disagree";
        return false;
      }
      for (std::uint16_t index = 0; index < length; ++index) {
        std::uint8_t byte = 0;
        if (reader.Consume(&byte, 1) != 1) {
          *error = "truncated stored block";
          return false;
        }
        if (!emit(byte)) {
          *error = limit_hit ? S("decompressed data exceeds the ", HumanBytes(max_output), " limit")
                             : std::string("output sink rejected the decompressed data");
          return false;
        }
      }
    } else if (block_type == 1 || block_type == 2) {
      const Huffman* literal_tree = &literals;
      const Huffman* distance_tree = &distances;
      if (block_type == 1) {
        literal_tree = &FixedLiterals();
        distance_tree = &FixedDistances();
      } else {
        if (!reader.Need(5 + 5 + 4)) {
          *error = "truncated dynamic block header";
          return false;
        }
        const int literal_count = reader.Peek(5) + 257;
        reader.Drop(5);
        const int distance_count = reader.Peek(5) + 1;
        reader.Drop(5);
        const int code_length_count = reader.Peek(4) + 4;
        reader.Drop(4);
        if (literal_count > 286 || distance_count > 30) {
          *error = S("invalid dynamic block counts (", literal_count, " literals, ",
                     distance_count, " distances)");
          return false;
        }

        static constexpr int kCodeLengthOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                     11, 4,  12, 3, 13, 2, 14, 1, 15};
        std::uint8_t code_length_lengths[19] = {};
        for (int index = 0; index < code_length_count; ++index) {
          if (!reader.Need(3)) {
            *error = "truncated code length table";
            return false;
          }
          code_length_lengths[kCodeLengthOrder[index]] = static_cast<std::uint8_t>(reader.Peek(3));
          reader.Drop(3);
        }
        Huffman code_length_tree;
        if (!code_length_tree.Build(code_length_lengths, 19, error)) {
          return false;
        }

        std::uint8_t lengths[286 + 30] = {};
        int index = 0;
        while (index < literal_count + distance_count) {
          const int symbol = code_length_tree.Decode(reader);
          if (symbol < 0) {
            *error = "corrupt code length code";
            return false;
          }
          if (symbol < 16) {
            lengths[index++] = static_cast<std::uint8_t>(symbol);
            continue;
          }
          int repeat = 0;
          std::uint8_t value = 0;
          if (symbol == 16) {
            if (index == 0) {
              *error = "code length repeat with no previous length";
              return false;
            }
            value = lengths[index - 1];
            if (!reader.Need(2)) {
              *error = "truncated code length repeat";
              return false;
            }
            repeat = 3 + reader.Peek(2);
            reader.Drop(2);
          } else if (symbol == 17) {
            if (!reader.Need(3)) {
              *error = "truncated code length repeat";
              return false;
            }
            repeat = 3 + reader.Peek(3);
            reader.Drop(3);
          } else {
            if (!reader.Need(7)) {
              *error = "truncated code length repeat";
              return false;
            }
            repeat = 11 + reader.Peek(7);
            reader.Drop(7);
          }
          if (index + repeat > literal_count + distance_count) {
            *error = "code length repeat overruns the table";
            return false;
          }
          for (int step = 0; step < repeat; ++step) {
            lengths[index++] = value;
          }
        }
        if (lengths[256] == 0) {
          *error = "dynamic block has no end-of-block code";
          return false;
        }
        if (!literals.Build(lengths, static_cast<std::size_t>(literal_count), error)) {
          return false;
        }
        if (!distances.Build(lengths + literal_count, static_cast<std::size_t>(distance_count),
                             error)) {
          return false;
        }
      }

      for (;;) {
        const int symbol = literal_tree->Decode(reader);
        if (symbol < 0) {
          *error = "corrupt literal/length code";
          return false;
        }
        if (symbol < 256) {
          if (!emit(static_cast<std::uint8_t>(symbol))) {
            *error = limit_hit ? S("decompressed data exceeds the ", HumanBytes(max_output), " limit")
                               : std::string("output sink rejected the decompressed data");
            return false;
          }
          continue;
        }
        if (symbol == 256) {
          break;
        }
        const int length_index = symbol - 257;
        if (length_index >= 29) {
          *error = S("invalid length code ", symbol);
          return false;
        }
        int length = kLengthBase[length_index];
        if (const int extra = kLengthExtra[length_index]; extra != 0) {
          if (!reader.Need(extra)) {
            *error = "truncated match length";
            return false;
          }
          length += reader.Peek(extra);
          reader.Drop(extra);
        }

        const int distance_symbol = distance_tree->Decode(reader);
        if (distance_symbol < 0) {
          *error = "corrupt distance code";
          return false;
        }
        if (distance_symbol >= 30) {
          *error = S("invalid distance code ", distance_symbol);
          return false;
        }
        int distance = kDistanceBase[distance_symbol];
        if (const int extra = kDistanceExtra[distance_symbol]; extra != 0) {
          if (!reader.Need(extra)) {
            *error = "truncated match distance";
            return false;
          }
          distance += reader.Peek(extra);
          reader.Drop(extra);
        }
        if (static_cast<std::uint64_t>(distance) > produced + pending.size()) {
          *error = "match distance reaches before the start of the output";
          return false;
        }
        for (int step = 0; step < length; ++step) {
          const std::uint8_t byte = window[(window_pos - static_cast<std::size_t>(distance)) & kWindowMask];
          if (!emit(byte)) {
            *error = limit_hit ? S("decompressed data exceeds the ", HumanBytes(max_output), " limit")
                               : std::string("output sink rejected the decompressed data");
            return false;
          }
        }
      }
    } else {
      *error = "reserved deflate block type";
      return false;
    }

    if (final_block != 0) {
      break;
    }
  }

  if (!flush()) {
    *error = limit_hit
                 ? S("decompressed data exceeds the ", HumanBytes(max_output), " limit")
                 : std::string(sink_failed ? "output sink rejected the decompressed data"
                                           : "decompressed data exceeds the size limit");
    return false;
  }

  if (stats != nullptr) {
    stats->input_bytes = reader.ConsumedInput();
    stats->output_bytes = produced;
    stats->crc32 = crc;
  }
  return true;
}

bool InflateRawToVector(ByteSource& source, std::uint64_t offset, std::uint64_t compressed_size,
                        std::vector<std::uint8_t>* out, std::uint32_t* crc32, std::string* error) {
  constexpr std::uint64_t kMemoryGuard = 512ull * 1024 * 1024;
  VectorSink sink;
  InflateStats stats;
  if (!InflateRaw(source, offset, compressed_size, kMemoryGuard, std::ref(sink), &stats, error)) {
    return false;
  }
  *out = sink.TakeBytes();
  if (crc32 != nullptr) {
    *crc32 = stats.crc32;
  }
  return true;
}

}  // namespace rb_blitz::installer
