// SPDX-License-Identifier: GPL-2.0-only
// Read-only local package inspection; does not launch or navigate the game.
#include <rex/filesystem/file.h>
#include <rex/filesystem/mount_diagnostics.h>
#include <rex/app/app.h>
#include <rex/cli.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "assets/texture_codec.h"
#include "fs/dlc_song_metadata.h"
#include "ui/menu_music_artwork.h"

namespace {
class ContentProbeApp final : public rex::app::App {
 public:
  ContentProbeApp() : App("menu_music_artwork_probe") {}
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    for (const auto* name : {"ui.driver", "hid.drivers", "apu.driver", "gpu.driver"})
      REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, name, "none"));
    return settings.SetText(rex::SettingLayer::kTitle, "log.file", "false");
  }
};

void Collect(const rb_blitz::fs::PackageView& package, size_t index, size_t depth,
             std::vector<std::string>& paths) {
  const auto nodes = package.Nodes();
  if (index >= nodes.size() || depth > 6) return;
  for (const auto child : nodes[index].children) {
    if (child >= nodes.size()) continue;
    const auto& info = nodes[child].info;
    if (rex::HasFlags(info.attributes, rex::filesystem::FileAttributes::kDirectory))
      Collect(package, child, depth+1, paths);
    else if (info.path.ends_with(".png_xbox"))
      paths.push_back("\\"+info.path);
  }
}

int InspectPackage(int argc, char** argv) {
  using namespace rb_blitz;
  auto* diagnostics = REX_INTERFACE(rex::filesystem::IMountDiagnostics);
  if (!diagnostics) {
    std::fputs("The SDK mount diagnostics service is unavailable\n", stderr);
    return 1;
  }
  std::error_code ec;
  auto device = fs::MountPackage("\\Device\\ArtProbe",
                                 std::filesystem::path(argv[1]), ec);
  if (!device) return 1;
  const auto songs = fs::ReadDeviceSongMetadata(*device);
  std::vector<std::string> paths;
  Collect(*device, 0, 0, paths);
  size_t decoded = 0;
  for (size_t i = 0; i < songs.size(); ++i) {
    const auto& song = songs[i];
    const auto path = ui::music::ResolveArtwork(song.stream, paths);
    if (path.empty()) continue;
    const auto full_path = device->Path(path);
    auto info = device->Files().Stat(full_path);
    if (!info || !info->size || info->size > 2*1024*1024) continue;
    rex::filesystem::OpenOptions options;
    options.require_file = true;
    auto opened = device->Files().Open(full_path, options);
    if (!opened) continue;
    std::vector<uint8_t> bytes(static_cast<size_t>(info->size));
    const auto pins = diagnostics->GetOpenCount(device->Files(), device->Mount().path);
    const auto read = opened->file->Read(bytes, 0);
    opened->file.reset();
    const auto closed_pins = diagnostics->GetOpenCount(device->Files(), device->Mount().path);
    if (!pins || !closed_pins) {
      std::fputs("Cannot inspect package file leases\n", stderr);
      return 1;
    }
    if (!read || *read != bytes.size()) continue;
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
        "song=%s stream=%s art=%s state=%d pixels=%ux%u lease=%u->%u\n",
        song.key.c_str(), song.stream.c_str(), path.c_str(), int(cover->state),
        cover->width, cover->height, unsigned(*pins), unsigned(*closed_pins));
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::puts("usage: rb_blitz_menu_music_artwork_probe PACKAGE [PPM_OUTPUT]");
    return 2;
  }
  ContentProbeApp app;
  if (!app.Init(rex::CLIArgList{})) return 1;
  const int result = InspectPackage(argc, argv);
  const int shutdown = app.Shutdown();
  return result ? result : shutdown;
}
