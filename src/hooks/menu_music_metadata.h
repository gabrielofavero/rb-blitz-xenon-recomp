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
  // The display identity (RhythmVerse store plan §3.2): `(song (title ...))` and
  // `(song (artist ...))`. Empty when the record does not carry them; the menu-music catalog retains
  // them for its player, and the store's Downloaded matcher compares exactly these two strings.
  std::string title;
  std::string artist;
};

// Bounded, read-only metadata parsing. Invalid input returns no partial records.
std::vector<SongMetadata> ReadSongMetadata(std::span<const uint8_t> bytes, bool compiled,
                                         std::string* error);
std::string StreamIdentity(std::string path);
// Match the complete package-relative stream path, never just a possibly ambiguous basename.
std::string SongKeyForStream(const std::vector<SongMetadata>& songs, const std::string& stream);

}  // namespace rb_blitz::shell_music
