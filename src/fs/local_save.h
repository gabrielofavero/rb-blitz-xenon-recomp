// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "util/sha256.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rb_blitz::local_save {

using Bytes = std::vector<uint8_t>;
constexpr size_t kMaxPayload = 2 * 1024 * 1024;

inline uint32_t Read32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline void Append32(Bytes& bytes, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(value >> shift));
}

// SaveData::Save (821CA2A0), with a big-endian MemStream:
// revision 11, score-count, {song-id, score}[], last stars, controller mapping,
// star-count, {song-id, stars}[], total stars. Validate the complete layout before
// allowing SaveData::Load to allocate or follow any count supplied by a file.
inline bool ValidPayload(std::span<const uint8_t> bytes) {
  if (bytes.size() < 24 || bytes.size() > kMaxPayload || Read32(bytes.data()) != 11)
    return false;
  const auto scores = Read32(bytes.data() + 4);
  if (scores > (bytes.size() - 24) / 8) return false;
  const size_t stars_offset = 16 + size_t(scores) * 8;
  const auto stars = Read32(bytes.data() + stars_offset);
  return stars == (bytes.size() - stars_offset - 8) / 8 &&
         bytes.size() == stars_offset + 8 + size_t(stars) * 8;
}

// How much progress a payload carries. The checkpoint refuses to overwrite a
// checkpoint that has some with one that has none: the guest clears SaveData
// whenever it (re)loads the player's save, and offline nothing fills it again.
struct Progress {
  uint32_t scores = 0;
  uint32_t stars = 0;
  bool empty() const { return scores == 0 && stars == 0; }
};

inline Progress Summarize(std::span<const uint8_t> bytes) {
  Progress progress;
  if (!ValidPayload(bytes)) return progress;
  progress.scores = Read32(bytes.data() + 4);
  progress.stars = Read32(bytes.data() + 16 + size_t(progress.scores) * 8);
  return progress;
}

// A host envelope distinguishes this from Xbox content and detects damaged
// payloads. The inner bytes are produced and consumed by the guest, unchanged.
inline Bytes Encode(std::span<const uint8_t> payload) {
  if (!ValidPayload(payload)) return {};
  Bytes bytes{'R', 'B', 'B', 'Z', 'S', 'A', 'V', 'E'};
  Append32(bytes, 1);
  Append32(bytes, uint32_t(payload.size()));
  const auto digest = util::HashBytes(payload.data(), payload.size());
  bytes.insert(bytes.end(), std::begin(digest.bytes), std::end(digest.bytes));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

inline bool Decode(std::span<const uint8_t> bytes, Bytes& payload) {
  if (bytes.size() < 48 || bytes.size() > kMaxPayload + 48 ||
      std::memcmp(bytes.data(), "RBBZSAVE", 8) != 0 || Read32(bytes.data() + 8) != 1 ||
      Read32(bytes.data() + 12) != bytes.size() - 48) return false;
  const auto body = bytes.subspan(48);
  const auto digest = util::HashBytes(body.data(), body.size());
  if (std::memcmp(bytes.data() + 16, digest.bytes, 32) != 0 || !ValidPayload(body))
    return false;
  payload.assign(body.begin(), body.end());
  return true;
}

enum class LoadResult { Missing, Loaded, Invalid };

inline LoadResult Load(const std::filesystem::path& path, Bytes& payload) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) return LoadResult::Invalid;
  if (!exists) return LoadResult::Missing;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size < 48 || size > kMaxPayload + 48) return LoadResult::Invalid;
  Bytes bytes(static_cast<size_t>(size));
  std::ifstream file(path, std::ios::binary);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
      !Decode(bytes, payload)) return LoadResult::Invalid;
  return LoadResult::Loaded;
}

inline bool Save(const std::filesystem::path& path, std::span<const uint8_t> payload,
                 std::string& error) {
  const auto bytes = Encode(payload);
  if (bytes.empty()) { error = "invalid guest save payload"; return false; }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) { error = ec.message(); return false; }
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.flush();
    file.close();
    if (!file) { error = "cannot write temporary save"; return false; }
  }
  // Never remove the previous save before replacement: a failed rename must
  // leave the last successful checkpoint available on the next launch.
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = std::system_category().message(GetLastError());
    return false;
  }
#else
  std::filesystem::rename(temporary, path, ec);
  if (ec) { error = ec.message(); return false; }
#endif
  return true;
}

}  // namespace rb_blitz::local_save
