// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "util/sha256.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace rb_blitz::util {
namespace {

// FIPS 180-4 §4.2.2.
constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

constexpr char kHexDigits[] = "0123456789abcdef";

// 1 MiB: big enough to keep file hashing sequential, small enough to stay off
// the stack.
constexpr std::size_t kFileBufferSize = 1u << 20;

inline std::uint32_t RotateRight(std::uint32_t value, std::uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

inline std::uint32_t Choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
  return (x & y) ^ (~x & z);
}

inline std::uint32_t Majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

inline std::uint32_t BigSigma0(std::uint32_t x) {
  return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22);
}

inline std::uint32_t BigSigma1(std::uint32_t x) {
  return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25);
}

inline std::uint32_t SmallSigma0(std::uint32_t x) {
  return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3);
}

inline std::uint32_t SmallSigma1(std::uint32_t x) {
  return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10);
}

}  // namespace

Sha256::Sha256()
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu,
             0x1f83d9abu, 0x5be0cd19u} {}

void Sha256::Compress(const std::uint8_t* block) {
  std::uint32_t w[64];
  for (std::size_t i = 0; i < 16; ++i) {
    const std::uint8_t* bytes = block + i * 4;
    w[i] = (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
  }

  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t t1 = h + BigSigma1(e) + Choose(e, f, g) + kRoundConstants[i] + w[i];
    const std::uint32_t t2 = BigSigma0(a) + Majority(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::Update(const void* data, std::size_t size) {
  if (size == 0) {
    return;
  }
  const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
  length_ += size;

  if (buffered_ != 0) {
    const std::size_t take = std::min(size, sizeof(buffer_) - buffered_);
    std::memcpy(buffer_ + buffered_, bytes, take);
    buffered_ += take;
    bytes += take;
    size -= take;
    if (buffered_ == sizeof(buffer_)) {
      Compress(buffer_);
      buffered_ = 0;
    }
  }

  while (size >= sizeof(buffer_)) {
    Compress(bytes);
    bytes += sizeof(buffer_);
    size -= sizeof(buffer_);
  }

  if (size != 0) {
    std::memcpy(buffer_, bytes, size);
    buffered_ = size;
  }
}

Sha256Digest Sha256::Final() {
  const std::uint64_t bit_length = length_ * 8;

  // 0x80 then zeros up to the 8-byte length field, which sits at offset 56 of
  // the final block. One more block is needed when the length field would not
  // fit in the current one.
  std::uint8_t padding[64] = {};
  padding[0] = 0x80;
  const std::size_t padding_size = (buffered_ <= 55) ? 56 - buffered_ : 120 - buffered_;
  Update(padding, padding_size);

  std::uint8_t length_bytes[8];
  for (std::size_t i = 0; i < sizeof(length_bytes); ++i) {
    length_bytes[i] = static_cast<std::uint8_t>(bit_length >> (56 - i * 8));
  }
  Update(length_bytes, sizeof(length_bytes));

  Sha256Digest digest;
  for (std::size_t i = 0; i < 8; ++i) {
    digest.bytes[i * 4 + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
    digest.bytes[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
    digest.bytes[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
    digest.bytes[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return digest;
}

std::string ToHex(const Sha256Digest& digest) {
  std::string out(kSha256HexSize, '0');
  for (std::size_t i = 0; i < kSha256DigestSize; ++i) {
    out[i * 2] = kHexDigits[digest.bytes[i] >> 4];
    out[i * 2 + 1] = kHexDigits[digest.bytes[i] & 0x0F];
  }
  return out;
}

Sha256Digest HashBytes(const void* data, std::size_t size) {
  Sha256 sha;
  sha.Update(data, size);
  return sha.Final();
}

std::string HashHex(std::string_view data) {
  return ToHex(HashBytes(data.data(), data.size()));
}

std::string HashFileHex(const std::filesystem::path& path, std::uint64_t* size_out,
                        std::error_code& ec) {
  ec.clear();
  if (size_out != nullptr) {
    *size_out = 0;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    ec = std::make_error_code(std::errc::io_error);
    return {};
  }

  Sha256 sha;
  std::vector<std::uint8_t> buffer(kFileBufferSize);
  std::uint64_t total = 0;
  while (file) {
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = file.gcount();
    if (read > 0) {
      sha.Update(buffer.data(), static_cast<std::size_t>(read));
      total += static_cast<std::uint64_t>(read);
    }
  }
  if (file.bad()) {
    ec = std::make_error_code(std::errc::io_error);
    return {};
  }

  if (size_out != nullptr) {
    *size_out = total;
  }
  return ToHex(sha.Final());
}

}  // namespace rb_blitz::util
