// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/fs/dlc_song_metadata.h.

#include "fs/dlc_song_metadata.h"

#include <cstdint>
#include <memory>
#include <system_error>
#include <vector>

#include <rex/filesystem/file_system_factory.h>
#include <rex/logging/log_category.h>
#include <rex/string/path.h>
#include <rex/string/text.h>

namespace rb_blitz::fs {

std::string PackageView::Path(std::string_view relative) const {
  return rex::string::utf8_join_guest_paths(mount_.path, relative);
}

std::unique_ptr<PackageView> MountPackage(const std::string& mount_path,
                                         const std::filesystem::path& package,
                                         std::error_code& ec) {
  ec.clear();
  auto* factory = REX_INTERFACE(rex::filesystem::IFileSystemFactory);
  if (!factory) { ec = std::make_error_code(std::errc::not_supported); return {}; }
  auto files = factory->Create();
  if (!files) { ec = std::make_error_code(std::errc::not_enough_memory); return {}; }
  rex::filesystem::MountEntry mount;
  mount.path = mount_path;
  mount.type = std::filesystem::is_directory(package, ec)
      ? rex::filesystem::ProviderType::kHostDirectory : rex::filesystem::ProviderType::kSTFS;
  if (ec) return {};
  mount.source = package;
  auto nodes = std::make_shared<std::vector<rex::filesystem::MountValidationNode>>();
  mount.validator = std::make_shared<const rex::filesystem::MountValidator>(
      [nodes](const rex::filesystem::IMountValidationView& view) -> rex::Result<void> {
        std::string reason;
        if (!IsSafeSongContent(view, &reason))
          return rex::Err(rex::ErrorCategory::kValidation, std::move(reason));
        const auto records = view.GetNodes();
        nodes->assign(records.begin(), records.end());
        return rex::Ok();
      });
  if (auto mounted = files->Mount(mount); !mounted) {
    ec = std::make_error_code(std::errc::invalid_argument);
    REXLOG_WARN("dlc_song_metadata: cannot mount {}: {}", package.string(), mounted.error().message);
    return {};
  }
  // The publication callback retains only validation policy, not this discovery snapshot.
  mount.validator = std::make_shared<const rex::filesystem::MountValidator>(
      [](const rex::filesystem::IMountValidationView& view) -> rex::Result<void> {
        std::string reason;
        return IsSafeSongContent(view, &reason) ? rex::Ok()
            : rex::Err(rex::ErrorCategory::kValidation, std::move(reason));
      });
  return std::make_unique<PackageView>(std::move(files), std::move(mount), std::move(nodes));
}

namespace {

// The three spellings a package may carry its song metadata under, as child paths.
constexpr std::string_view kMetadataPaths[][2] = {
    {"songs", "songs.dta"}, {"songs", "songs.dtb"}, {"songs", "gen\\songs.dtb"}};

}  // namespace

bool IsSafeSongContent(const rex::filesystem::IMountValidationView& view, std::string* reason) {
  if (reason) reason->clear();
  const auto nodes = view.GetNodes();
  if (nodes.empty() || nodes.size() > 100000) {
    if (reason) *reason = nodes.empty() ? "package root cannot be read" : "too many package entries";
    return false;
  }
  for (size_t index = 0; index < nodes.size(); ++index) {
    const auto& info = nodes[index].info;
    const auto name = rex::string::utf8_lower_ascii(info.name);
    if (name != "songs.dta" && name != "songs.dtb") continue;
    if (rex::HasFlags(info.attributes, rex::filesystem::FileAttributes::kDirectory)) continue;
    const auto reject = [&](std::string_view detail) {
      if (reason) *reason = info.path + ": " + std::string(detail);
      return false;
    };
    if (!info.size || info.size > 2 * 1024 * 1024) return reject("empty or oversized metadata");
    std::vector<uint8_t> bytes(static_cast<size_t>(info.size));
    const auto read = view.Read(index, 0, bytes);
    if (!read || *read != bytes.size()) return reject("cannot read metadata completely");
    std::string error;
    if (!shell_music::IsStaticSongMetadata(bytes, name == "songs.dtb", &error)) return reject(error);
  }
  return true;
}

std::vector<shell_music::SongMetadata> ReadDeviceSongMetadata(PackageView& package) {
  std::string skipped;
  for (const auto& path : kMetadataPaths) {
    const auto guest_path = package.Path(std::string(path[0]) + "\\" + std::string(path[1]));
    auto entry = package.Files().Stat(guest_path);
    if (!entry || rex::HasFlags(entry->attributes, rex::filesystem::FileAttributes::kDirectory)) {
      if (!skipped.empty()) skipped += "; ";
      skipped += std::string(path[0]) + "\\" + std::string(path[1]) + " is not in the package";
      continue;
    }
    // The bound keeps a mis-sized entry (an STFS entry whose length field came back garbage) from
    // becoming an 80 MiB allocation - a real `songs.dta` for one package is a few kilobytes.
    if (entry->size == 0 || entry->size > 2 * 1024 * 1024) {
      REXLOG_WARN("dlc_song_metadata: {}\\{} is present but {} byte(s); skipped", path[0], path[1],
                  entry->size);
      continue;
    }
    auto opened = package.Files().Open(guest_path, {});
    if (!opened) {
      REXLOG_WARN("dlc_song_metadata: {} could not be opened", guest_path);
      continue;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(entry->size));
    const auto read = opened->file->Read(bytes, 0);
    if (!read || *read != bytes.size()) {
      REXLOG_WARN("dlc_song_metadata: {} could not be read completely", guest_path);
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
