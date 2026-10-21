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

std::vector<shell_music::SongMetadata> ReadDeviceSongMetadata(rex::filesystem::Device& device) {
  for (const char* path : {"\\songs\\songs.dta", "\\songs\\songs.dtb", "\\songs\\gen\\songs.dtb"}) {
    auto* entry = device.ResolvePath(path);
    if (!entry || entry->size() == 0 || entry->size() > 2 * 1024 * 1024) continue;
    rex::filesystem::File* file = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 || !file) continue;
    std::vector<uint8_t> bytes(static_cast<size_t>(entry->size()));
    size_t read = 0;
    const auto status = file->ReadSync(bytes, 0, &read);
    file->Destroy();
    if (status != 0 || read != bytes.size()) continue;
    std::string error;
    auto songs = shell_music::ReadSongMetadata(bytes, std::string_view(path).ends_with(".dtb"),
                                               &error);
    if (!songs.empty()) return songs;
    if (!error.empty()) REXLOG_WARN("dlc_song_metadata: cannot read {}: {}", path, error);
  }
  return {};
}

}  // namespace rb_blitz::fs
