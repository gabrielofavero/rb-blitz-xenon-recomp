// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The SDK-free half of the store's Downloaded matcher (src/store/store_library.h): the field
// normalization and the (artist, title) index. It lives in its own translation unit so
// tests/store_library_tests.cpp can link it without the package reader.

#include "store/store_library.h"
#include "store/rhythmverse_client.h"

namespace rb_blitz::store {

std::string NormalizeSongField(std::string_view text) {
  return FoldForMatch(text);
}

void LoadedSongIndex::Add(std::string_view artist, std::string_view title,
                          const std::filesystem::path& package) {
  std::string normalized_artist = NormalizeSongField(artist);
  std::string normalized_title = NormalizeSongField(title);
  if (normalized_artist.empty() || normalized_title.empty()) {
    return;
  }
  songs_[{std::move(normalized_artist), std::move(normalized_title)}].insert(package);
}

std::vector<std::filesystem::path> LoadedSongIndex::Packages(std::string_view artist,
                                                           std::string_view title) const {
  const auto found = songs_.find({NormalizeSongField(artist), NormalizeSongField(title)});
  if (found == songs_.end()) return {};
  std::vector<std::filesystem::path> paths;
  for (const auto& path : found->second) if (!path.empty()) paths.push_back(path);
  return paths;
}

void LoadedSongIndex::RemovePackage(const std::filesystem::path& package) {
  for (auto it = songs_.begin(); it != songs_.end();) {
    it->second.erase(package);
    if (it->second.empty()) it = songs_.erase(it);
    else ++it;
  }
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
