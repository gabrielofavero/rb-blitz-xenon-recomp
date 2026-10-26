// SPDX-License-Identifier: GPL-2.0-only
// Read-only local package inspection; does not launch or navigate the game.
#include <rex/filesystem/file.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "assets/texture_codec.h"
#include "fs/dlc_song_metadata.h"
#include "ui/menu_music_artwork.h"

namespace {
void Collect(rex::filesystem::Entry* root, const std::string& path, int depth,
             std::vector<std::string>& paths) {
  if (!root || depth > 6) return;
  for (const auto& child : root->children()) {
    const auto name = path + "\\" + child->name();
    if (child->attributes() & rex::filesystem::kFileAttributeDirectory)
      Collect(child.get(), name, depth + 1, paths);
    else if (name.ends_with(".png_xbox"))
      paths.push_back(name);
  }
}
}  // namespace
int main(int argc, char** argv) {
  using namespace rb_blitz;
  if (argc < 2) {
    std::puts("usage: rb_blitz_menu_music_artwork_probe PACKAGE [RGBA_OUTPUT]");
    return 2;
  }
  std::error_code ec;
  auto device = fs::MountPackage("\\Device\\ArtProbe",
                                 std::filesystem::path(argv[1]), ec);
  if (!device) return 1;
  const auto songs = fs::ReadDeviceSongMetadata(*device);
  std::vector<std::string> paths;
  Collect(device->ResolvePath(""), "", 0, paths);
  size_t decoded = 0;
  for (size_t i = 0; i < songs.size(); ++i) {
    const auto& song = songs[i];
    const auto path = ui::music::ResolveArtwork(song.stream, paths);
    if (path.empty()) continue;
    auto* entry = device->ResolvePath(path);
    if (!entry || entry->size() > 2 * 1024 * 1024) continue;
    rex::filesystem::File* file = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 ||
        !file)
      continue;
    std::vector<uint8_t> bytes(size_t(entry->size()));
    size_t read = 0;
    const auto pins = device->open_file_count();
    const auto status = file->ReadSync(bytes, 0, &read);
    file->Destroy();
    if (status || read != bytes.size()) continue;
    const auto cover = ui::music::DecodeCover({1, i, path}, bytes);
    if (cover->rgba.empty()) {
      assets::TextureEnvelope envelope;
      std::string why;
      assets::ParseTextureEnvelope(bytes.data(), bytes.size(), &envelope, &why);
      std::printf("decode failed: %s; bytes=%zu header=", why.c_str(),
                  bytes.size());
      for (size_t p = 0; p < std::min(size_t(32), bytes.size()); ++p)
        std::printf("%02X", bytes[p]);
      std::puts("");
    }
    std::printf(
        "song=%s stream=%s art=%s state=%d pixels=%ux%u lease=%zu->%zu\n",
        song.key.c_str(), song.stream.c_str(), path.c_str(), int(cover->state),
        cover->width, cover->height, pins, device->open_file_count());
    if (cover->rgba.empty()) continue;
    if (!decoded && argc > 2) {
      // Portable pixmap for visual inspection without a GPU.
      std::ofstream output(argv[2], std::ios::binary);
      output << "P6\n" << cover->width << " " << cover->height << "\n255\n";
      for (size_t p = 0; p < cover->rgba.size(); p += 4)
        output.write(reinterpret_cast<const char*>(cover->rgba.data() + p), 3);
    }
    ++decoded;
  }
  std::printf("songs=%zu art_entries=%zu decoded=%zu\n", songs.size(),
              paths.size(), decoded);
  return decoded ? 0 : 1;
}
