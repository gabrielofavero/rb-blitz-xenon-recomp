// SPDX-License-Identifier: GPL-2.0-only
// Verify that cancellation never damages an existing base game or overlay.
#include "ultimate_import_core.h"
#include <chrono>
#include <fstream>
#include <iostream>

int main() {
  namespace fs = std::filesystem;
  namespace p = rb_blitz::installer::portable;
  const fs::path root = fs::temp_directory_path() / ("blitz-ultimate-cancel-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{root};
  const fs::path game = root / "game", source = root / "source";
  fs::create_directories(game / "ultimate/gen");
  fs::create_directories(source / "gen");
  const auto write = [](const fs::path& path, const char* data) { std::ofstream(path) << data; };
  const auto read = [](const fs::path& path) {
    std::ifstream input(path); return std::string(std::istreambuf_iterator<char>(input), {});
  };
  write(game / "default.xex", "base fixture");
  write(game / "ultimate/gen/patch_xbox.hdr", "previous header");
  write(game / "ultimate/gen/patch_xbox_0.ark", "previous archive");
  write(source / "gen/patch_xbox.hdr", "replacement header");
  write(source / "gen/patch_xbox_0.ark", "replacement archive");
  const auto config = p::EmbeddedUltimateConfig();
  p::ImportCallbacks callbacks;
  callbacks.cancelled = [] { return true; };
  std::string warning, error;
  if (p::InstallUltimate({}, game, config, callbacks, &warning, &error) ||
      fs::exists(root / "game.ultimate-work")) {
    std::cerr << "Canceled download started work\n"; return 1;
  }
  callbacks.cancelled = {};
  callbacks.continue_at = [](p::ImportPhase phase) { return phase != p::ImportPhase::kOldMoved; };
  if (p::InstallUltimate(source, game, config, callbacks, &warning, &error) ||
      read(game / "ultimate/gen/patch_xbox.hdr") != "previous header" ||
      read(game / "ultimate/gen/patch_xbox_0.ark") != "previous archive" ||
      read(game / "default.xex") != "base fixture") {
    std::cerr << "Canceled Ultimate commit did not restore the previous files\n"; return 1;
  }
  std::cout << "Ultimate cancellation and rollback passed\n";
}
