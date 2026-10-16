// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rb_blitz::shell_music {

struct SongMetadata {
  std::string key;     // the song manager's Symbol, preserving case
  std::string stream;  // (song (name ...)), without .mogg
};

// Bounded, read-only metadata parsing. Invalid input returns no partial records.
std::vector<SongMetadata> ReadSongMetadata(std::span<const uint8_t> bytes, bool compiled,
                                         std::string* error);
std::string StreamIdentity(std::string path);
// Match the complete package-relative stream path, never just a possibly ambiguous basename.
std::string SongKeyForStream(const std::vector<SongMetadata>& songs, const std::string& stream);

}  // namespace rb_blitz::shell_music
