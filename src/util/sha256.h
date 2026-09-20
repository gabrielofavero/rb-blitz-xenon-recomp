// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// SHA-256 (FIPS 180-4) over host memory and files.
//
// Project-owned instead of taken from the SDK: rex/hash.h only offers XXH3, and
// config/game_fingerprints.toml deliberately uses SHA-256 so that anyone with
// sha256sum or Get-FileHash can verify a dump without building this project.
//
// SDK-free and host-side, so tests/fingerprint_tests.cpp covers it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace rb_blitz::util {

inline constexpr std::size_t kSha256DigestSize = 32;
inline constexpr std::size_t kSha256HexSize = 64;

struct Sha256Digest {
  std::uint8_t bytes[kSha256DigestSize] = {};
};

// Incremental, so a 9 MB XEX or a 360 MB archive can be hashed without ever
// holding it all in memory.
class Sha256 {
 public:
  Sha256();

  void Update(const void* data, std::size_t size);
  void Update(std::string_view data) { Update(data.data(), data.size()); }

  // May only be called once; the object is not reusable afterwards.
  Sha256Digest Final();

 private:
  void Compress(const std::uint8_t* block);

  std::uint32_t state_[8];
  std::uint8_t buffer_[64] = {};
  std::size_t buffered_ = 0;
  std::uint64_t length_ = 0;
};

// Lowercase hex, kSha256HexSize characters.
std::string ToHex(const Sha256Digest& digest);

Sha256Digest HashBytes(const void* data, std::size_t size);
std::string HashHex(std::string_view data);

// Streams the file through the hash. Returns an empty string on failure and sets
// `ec`; `size_out` then holds the number of bytes hashed (0 on failure).
std::string HashFileHex(const std::filesystem::path& path, std::uint64_t* size_out,
                        std::error_code& ec);

}  // namespace rb_blitz::util
