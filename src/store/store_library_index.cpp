// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The SDK-free half of the store's Downloaded matcher (src/store/store_library.h): the field
// normalization and the (artist, title) index. It lives in its own translation unit so
// tests/store_library_tests.cpp can link it without the package reader.

#include "store/store_library.h"

namespace rb_blitz::store {

std::string NormalizeSongField(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool pending_space = false;
  for (const char raw : text) {
    const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
    const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
    if (space) {
      pending_space = !out.empty();
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(c);
  }
  return out;
}

void LoadedSongIndex::Add(std::string_view artist, std::string_view title) {
  std::string normalized_artist = NormalizeSongField(artist);
  std::string normalized_title = NormalizeSongField(title);
  if (normalized_artist.empty() || normalized_title.empty()) {
    return;
  }
  songs_.emplace(std::move(normalized_artist), std::move(normalized_title));
}

bool LoadedSongIndex::Contains(std::string_view artist, std::string_view title) const {
  const std::string normalized_artist = NormalizeSongField(artist);
  const std::string normalized_title = NormalizeSongField(title);
  if (normalized_artist.empty() || normalized_title.empty()) {
    return false;
  }
  return songs_.find({normalized_artist, normalized_title}) != songs_.end();
}

}  // namespace rb_blitz::store
