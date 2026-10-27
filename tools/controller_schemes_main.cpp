// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"

#include <iostream>

int main(int argc, char** argv) {
  if (argc != 3 || std::string(argv[1]) != "--validate") {
    std::cerr << "usage: rb_blitz_controller_schemes --validate <folder>\n";
    return 2;
  }
  rb_blitz::controller_schemes::Scheme scheme;
  std::string reason;
  const auto folder = std::filesystem::path(argv[2]);
  const std::string id = folder.filename().string();
  const bool trusted = id == "xbox-series" || id == "playstation-series" ||
      id == "nintendo-switch" || id == "steam-controller" || id == "steam-deck";
  if (!rb_blitz::controller_schemes::LoadScheme(
          folder, trusted, rb_blitz::controller_schemes::DecodePngWic, &scheme, &reason)) {
    std::cerr << folder << ": " << reason << '\n';
    return 1;
  }
  std::cout << folder << ": valid " << scheme.id << " (" << scheme.buttons.size()
            << " button, " << scheme.pads.size() << " pad slots"
            << (scheme.has_shared_pad ? ", shared pad" : "") << ")\n";
  for (const auto& extra : scheme.extra_files)
    std::cout << "  ignored extra file: " << extra.generic_string() << '\n';
}
