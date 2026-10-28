// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"

#include <algorithm>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[1]) != "--validate" &&
                    std::string(argv[1]) != "--validate-install")) {
    std::cerr << "usage: rb_blitz_controller_schemes "
                 "--validate|--validate-install <folder>\n";
    return 2;
  }
  rb_blitz::controller_schemes::Scheme scheme;
  std::string reason;
  const auto folder = std::filesystem::path(argv[2]);
  const bool trusted = std::string(argv[1]) == "--validate-install";
  if (!rb_blitz::controller_schemes::LoadScheme(
          folder, trusted, rb_blitz::controller_schemes::DecodePngWic, &scheme, &reason)) {
    std::cerr << folder << ": " << reason << '\n';
    return 1;
  }
  std::cout << folder << ": valid " << scheme.id << " (" << scheme.buttons.size()
            << " button, " << scheme.pads.size() << " pad slots"
            << (scheme.has_shared_pad ? ", shared pad" : "") << ")\n";
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
