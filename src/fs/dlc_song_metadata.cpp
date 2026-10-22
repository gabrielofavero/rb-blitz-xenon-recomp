// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/fs/dlc_song_metadata.h.

#include "fs/dlc_song_metadata.h"

#include <cstdint>
#include <memory>
#include <system_error>
#include <vector>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>

namespace rb_blitz::fs {

std::unique_ptr<rex::filesystem::Device> MountPackage(const std::string& mount_path,
                                                      const std::filesystem::path& package,
                                                      std::error_code& ec) {
  if (std::filesystem::is_directory(package, ec)) {
    auto device =
        std::make_unique<rex::filesystem::HostPathDevice>(mount_path, package, /*read_only=*/true);
    if (!device->Initialize()) {
      return nullptr;
    }
    return device;
  }
  auto device = std::make_unique<rex::filesystem::StfsContainerDevice>(mount_path, package);
  if (!device->Initialize()) {
    return nullptr;
  }
  return device;
}

namespace {

// Case-insensitive child lookup on the entry tree. `Entry::ResolvePath` is the obvious call and it
// answered null for a package that carries `songs/songs.dta` (measured 2026-10-21, boot
// `out/_swap/store-probe/20261021-220911`: the device mounted, the read took 0 ms and declared no
// song), so the walk is explicit and names the level it could not find instead of failing silently.
rex::filesystem::Entry* FindChild(rex::filesystem::Entry* parent, std::string_view name) {
  if (parent == nullptr) {
    return nullptr;
  }
  const auto same = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      const char ca = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
      const char cb = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
      if (ca != cb) return false;
    }
    return true;
  };
  for (const auto& child : parent->children()) {
    if (child != nullptr && same(child->name(), name)) {
      return child.get();
    }
  }
  return nullptr;
}

// The three spellings a package may carry its song metadata under, as child paths.
constexpr std::string_view kMetadataPaths[][2] = {
    {"songs", "songs.dta"}, {"songs", "songs.dtb"}, {"songs", "gen\\songs.dtb"}};

}  // namespace

std::vector<shell_music::SongMetadata> ReadDeviceSongMetadata(rex::filesystem::Device& device) {
  auto* root = device.ResolvePath("");
  std::string skipped;
  for (const auto& path : kMetadataPaths) {
    auto* dir = FindChild(root, path[0]);
    rex::filesystem::Entry* entry = nullptr;
    if (dir != nullptr) {
      // `gen\songs.dtb` is two levels; `songs.dta` is one.
      const std::string_view tail = path[1];
      const std::size_t slash = tail.find('\\');
      entry = slash == std::string_view::npos
                  ? FindChild(dir, tail)
                  : FindChild(FindChild(dir, tail.substr(0, slash)), tail.substr(slash + 1));
    }
    if (entry == nullptr) {
      if (!skipped.empty()) skipped += "; ";
      skipped += std::string(path[0]) + "\\" + std::string(path[1]) + " is not in the package";
      continue;
    }
    // The bound keeps a mis-sized entry (an STFS entry whose length field came back garbage) from
    // becoming an 80 MiB allocation - a real `songs.dta` for one package is a few kilobytes.
    if (entry->size() == 0 || entry->size() > 2 * 1024 * 1024) {
      REXLOG_WARN("dlc_song_metadata: {}\\{} is present but {} byte(s); skipped", path[0], path[1],
                  entry->size());
      continue;
    }
    rex::filesystem::File* file = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 || !file) {
      REXLOG_WARN("dlc_song_metadata: {}\\{} could not be opened", path[0], path[1]);
      continue;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(entry->size()));
    size_t read = 0;
    const auto status = file->ReadSync(bytes, 0, &read);
    file->Destroy();
    if (status != 0 || read != bytes.size()) {
      REXLOG_WARN("dlc_song_metadata: {}\\{} read {} of {} byte(s) (status {})", path[0], path[1],
                  read, bytes.size(), status);
      continue;
    }
    std::string error;
    auto songs = shell_music::ReadSongMetadata(bytes, std::string_view(path[1]).ends_with(".dtb"),
                                               &error);
    if (!songs.empty()) {
      // A parsed song with an empty field is dropped by `LoadedSongIndex::Add`, which would leave
      // the library declaring nothing and no clue why - name it here instead.
      for (const auto& song : songs) {
        if (song.artist.empty() || song.title.empty()) {
          REXLOG_WARN("dlc_song_metadata: {}\\{} entry `{}` has no {}; the Downloaded mark cannot "
                      "use it",
                      path[0], path[1], song.key, song.artist.empty() ? "artist" : "title");
        }
      }
      return songs;
    }
    if (!error.empty()) {
      REXLOG_WARN("dlc_song_metadata: cannot read {}\\{}: {}", path[0], path[1], error);
      continue;
    }
    // A parse that succeeds but declares nothing is the shape question, not a read failure: print
    // the head of the file so the layout can be seen without another instrument.
    std::string head(reinterpret_cast<const char*>(bytes.data()),
                     bytes.size() < 200 ? bytes.size() : 200);
    for (char& c : head) {
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
      if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = '.';
    }
    REXLOG_WARN("dlc_song_metadata: {}\\{} parsed but declares no song: {}", path[0], path[1], head);
  }
  if (!skipped.empty()) REXLOG_DEBUG("dlc_song_metadata: {}", skipped);
  return {};
}

}  // namespace rb_blitz::fs
