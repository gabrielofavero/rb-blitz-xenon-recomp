// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"

#include <algorithm>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--benchmark-discover") {
    const auto started = std::chrono::steady_clock::now();
    auto registry = rb_blitz::controller_schemes::Discover(
        argv[2], {}, rb_blitz::controller_schemes::DecodePngWic);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << registry.Summary() << "; elapsed_ms=" << elapsed << '\n';
    return 0;
  }
  const bool write_cache = argc == 4 && std::string(argv[1]) == "--write-cache";
  if ((!write_cache && argc != 3) || (!write_cache &&
      std::string(argv[1]) != "--validate" &&
                    std::string(argv[1]) != "--validate-install" &&
                    std::string(argv[1]) != "--validate-internal-keyboard" &&
                    std::string(argv[1]) != "--list-declared" &&
                    std::string(argv[1]) != "--list-internal-keyboard")) {
    std::cerr << "usage: rb_blitz_controller_schemes "
                 "--validate|--validate-install|--validate-internal-keyboard|"
                 "--list-declared|--list-internal-keyboard <folder>\n"
                 "       rb_blitz_controller_schemes --write-cache <folder> <cache-file>\n"
                 "       rb_blitz_controller_schemes --benchmark-discover <install-root>\n";
    return 2;
  }
  rb_blitz::controller_schemes::Scheme scheme;
  std::string reason;
  const auto folder = std::filesystem::path(argv[2]);
  const bool trusted = std::string(argv[1]) == "--validate-install" || write_cache;
  const bool internal = std::string(argv[1]) == "--validate-internal-keyboard" ||
                        std::string(argv[1]) == "--list-internal-keyboard" ||
                        (write_cache && folder.filename() == "keyboard-mouse");
  const bool valid = internal ? rb_blitz::controller_schemes::LoadInternalKeyboard(
      folder, rb_blitz::controller_schemes::DecodePngWic, &scheme, &reason) :
      rb_blitz::controller_schemes::LoadScheme(
          folder, trusted, rb_blitz::controller_schemes::DecodePngWic, &scheme, &reason);
  if (!valid) {
    std::cerr << folder << ": " << reason << '\n';
    return 1;
  }
  if (write_cache) {
    if (!rb_blitz::controller_schemes::WriteSchemeCache(scheme, argv[3], &reason)) {
      std::cerr << folder << ": " << reason << '\n';
      return 1;
    }
    // The packer copies precisely these validated source files beside the
    // cache. One parse/decode pass now serves validation, listing and caching.
    std::cout << "scheme.toml\n";
    for (const auto& path : scheme.declared_files)
      std::cout << path.generic_string() << '\n';
    return 0;
  }
  if (std::string(argv[1]) == "--list-declared" ||
      std::string(argv[1]) == "--list-internal-keyboard") {
    std::cout << "scheme.toml\n";
    for (const auto& path : scheme.declared_files)
      std::cout << path.generic_string() << '\n';
    return 0;
  }
  std::cout << folder << ": valid " << scheme.id << " (" << scheme.buttons.size()
            << " button, " << scheme.pads.size() << " pad slots, "
            << scheme.inputs.size() << " source images"
            << (scheme.has_shared_pad ? ", shared pad" : "")
            << (scheme.use_retail_ps_pads ? ", retail PS pads for Xbox slots" : "")
            << ")\n";
  auto report = [](const std::string& slot, const rb_blitz::controller_schemes::Image& image,
                   uint32_t width, uint32_t height) {
    const double scale = std::min(double(width) / image.width, double(height) / image.height);
    const auto fitted_width = std::clamp(uint32_t(image.width * scale + 0.5), 1u, width);
    const auto fitted_height = std::clamp(uint32_t(image.height * scale + 0.5), 1u, height);
    std::cout << "  " << slot << ": " << image.width << 'x' << image.height
              << " -> " << fitted_width << 'x' << fitted_height << " in "
              << width << 'x' << height << "\n";
  };
  for (const auto& [id, image] : scheme.buttons) {
    const auto& slots = rb_blitz::controller_schemes::ButtonSlots();
    const auto slot = std::find_if(slots.begin(), slots.end(), [&](const auto& item) {
      return item.id == id;
    });
    report(id, image, slot->rect.width, slot->rect.height);
  }
  for (const auto& [id, image] : scheme.pads) report(id, image, 1024, 1024);
  if (scheme.has_shared_pad) report("shared pad", scheme.shared_pad, 1024, 1024);
  if (scheme.has_icons) report("extra icons", scheme.icons, 1024, 512);
  for (const auto& extra : scheme.extra_files)
    std::cout << "  ignored extra file: " << extra.generic_string() << '\n';
}
