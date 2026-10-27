// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "hooks/menu_music_pool.h"
#include "fs/dlc_cache.h"
#include <iomanip>
#include <optional>
#include <sstream>

namespace rb_blitz::shell_music::catalog {

// Only metadata, never audio or mounted devices. File stats validate containers;
// extracted packages use a stat-only tree fingerprint (including their streams).
struct Package {
  std::filesystem::path path;
  std::optional<fs::DlcTreeFingerprint> identity;
  std::vector<PoolEntry> songs;
};

inline std::optional<fs::DlcTreeFingerprint> Identity(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    fs::DlcTreeFingerprint result;
    std::string why;
    if (fs::ComputeLibraryFingerprint({path}, &result, &why)) return result;
    return {};
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return {};
  const auto time = std::filesystem::last_write_time(path, ec);
  if (ec) return {};
  return fs::DlcTreeFingerprint{uint64_t(time.time_since_epoch().count()), 1, size};
}

// Windows directory entries already carry size and write time. Grouping siblings
// avoids thousands of individual attribute queries/opens on a large library.
inline std::vector<std::optional<fs::DlcTreeFingerprint>> Identities(
    const std::vector<std::filesystem::path>& paths) {
  std::vector<std::optional<fs::DlcTreeFingerprint>> result(paths.size());
  std::map<std::filesystem::path, std::map<std::filesystem::path, size_t>> directories;
  for (size_t i = 0; i < paths.size(); ++i)
    directories[paths[i].parent_path()][paths[i].filename()] = i;
  for (const auto& [directory, wanted] : directories) {
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec), end;
    while (!ec && it != end) {
      const auto& entry = *it;
      const auto found = wanted.find(entry.path().filename());
      if (found != wanted.end()) {
        if (entry.is_directory(ec) && !ec) result[found->second] = Identity(entry.path());
        else if (!ec && entry.is_regular_file(ec) && !ec) {
          const auto size = entry.file_size(ec);
          if (!ec) {
            const auto time = entry.last_write_time(ec);
            if (!ec) result[found->second] =
                fs::DlcTreeFingerprint{uint64_t(time.time_since_epoch().count()), 1, size};
          }
        }
      }
      it.increment(ec);
    }
  }
  return result;
}

inline std::string Encode(const std::vector<Package>& packages) {
  std::ostringstream out;
  out << "RBBMUSIC1\n" << packages.size() << '\n';
  for (const auto& package : packages) {
    const auto identity = package.identity.value_or(fs::DlcTreeFingerprint{});
    const auto utf8 = package.path.generic_u8string();
    const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    out << std::quoted(path) << ' ' << bool(package.identity) << ' '
        << identity.hash << ' ' << identity.file_count << ' ' << identity.total_size << ' '
        << package.songs.size() << '\n';
    for (const auto& song : package.songs)
      out << std::quoted(song.stream) << ' ' << std::quoted(song.song_key) << ' '
          << std::quoted(song.title) << ' ' << std::quoted(song.artist) << ' '
          << std::quoted(song.artwork_locator) << '\n';
  }
  return out.str();
}

inline bool DecodeUnchecked(const std::string& bytes, std::vector<Package>* packages) {
  if (bytes.size() > 32 * 1024 * 1024) return false;
  std::istringstream in(bytes);
  std::string magic;
  size_t count = 0, total = 0;
  if (!(in >> magic >> count) || magic != "RBBMUSIC1" || count > 100000) return false;
  std::vector<Package> decoded;
  for (size_t i = 0; i < count; ++i) {
    Package package;
    std::string path;
    bool valid = false;
    fs::DlcTreeFingerprint identity;
    size_t songs = 0;
    if (!(in >> std::quoted(path) >> valid >> identity.hash >> identity.file_count >>
          identity.total_size >> songs) || path.empty() || path.size() > 4096 ||
        songs > 100000 || (total += songs) > 100000) return false;
    package.path = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size()));
    if (valid) package.identity = identity;
    for (size_t j = 0; j < songs; ++j) {
      PoolEntry song;
      if (!(in >> std::quoted(song.stream) >> std::quoted(song.song_key) >>
            std::quoted(song.title) >> std::quoted(song.artist) >> std::quoted(song.artwork_locator)) ||
          song.stream.empty() || song.stream.size() > 4096 || song.song_key.size() > 4096 ||
          song.title.size() > 4096 || song.artist.size() > 4096 || song.artwork_locator.size() > 4096)
        return false;
      // A catalog can describe only package-relative sources, never another device.
      for (const auto& locator : {song.stream, song.artwork_locator}) {
        if (locator.find(':') != std::string::npos) return false;
        std::string path = locator;
        std::replace(path.begin(), path.end(), '\\', '/');
        if (("/" + path + "/").find("/../") != std::string::npos) return false;
      }
      package.songs.push_back(std::move(song));
    }
    decoded.push_back(std::move(package));
  }
  in >> std::ws;
  if (!in.eof()) return false;
  *packages = std::move(decoded);
  return true;
}

inline bool Decode(const std::string& bytes, std::vector<Package>* packages) {
  try { return DecodeUnchecked(bytes, packages); }
  catch (const std::exception&) { return false; }
}

// Returns only entries whose path and current file/tree identity still match.
// Changed packages are individually rebuilt; reordering enumeration is harmless.
inline std::vector<Package> Load(const std::filesystem::path& file,
                                 const std::vector<std::filesystem::path>& paths) {
  std::vector<Package> saved;
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (!ec && size <= 32 * 1024 * 1024) {
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), {});
    if (!Decode(bytes, &saved)) saved.clear();
  }
  std::map<std::filesystem::path, Package> by_path;
  for (auto& package : saved) by_path.emplace(package.path, std::move(package));
  std::vector<Package> result;
  // No cached records means there is nothing to validate on the boot thread.
  const auto identities = saved.empty() ? std::vector<std::optional<fs::DlcTreeFingerprint>>(paths.size())
                                        : Identities(paths);
  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    Package package{path, {}, {}};
    const auto found = by_path.find(path);
    if (found != by_path.end() && found->second.identity &&
        identities[i] == found->second.identity) package = std::move(found->second);
    result.push_back(std::move(package));
  }
  return result;
}

inline bool Save(const std::filesystem::path& file, const std::vector<Package>& packages,
                 std::string* why) {
  return fs::WriteAtomic(file, Encode(packages), why);
}

}  // namespace rb_blitz::shell_music::catalog
