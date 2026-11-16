// SPDX-License-Identifier: GPL-2.0-only
// Noninteractive source preparation for local builds; never starts the game.
#include "game_import_core.h"
#if defined(RBBLITZ_ULTIMATE_IMPORT)
#include "ultimate_import_core.h"
#endif

#include <iostream>

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  namespace portable = rb_blitz::installer::portable;
  fs::path source, destination, fingerprints;
  bool verify = false;
  bool ultimate = false;
  fs::path ultimate_source;
  for (int i = 1; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option == "--verify") { verify = true; continue; }
    if (option == "--ultimate") { ultimate = true; continue; }
    if (option == "--ultimate-source" && i + 1 < argc) {
      ultimate_source = argv[++i]; ultimate = true; continue;
    }
    if ((option == "--source" || option == "--destination" || option == "--fingerprints") && i + 1 < argc) {
      const fs::path value(argv[++i]);
      if (option == "--source") source = value;
      else if (option == "--destination") destination = value;
      else fingerprints = value;
      continue;
    }
    std::cerr << "Usage: rb_blitz_import --fingerprints FILE --destination DIR [--verify | --source FILE_OR_DIR] [--ultimate | --ultimate-source ZIP_OR_FOLDER]\n";
    return 2;
  }
  if (destination.empty() || fingerprints.empty() || (verify == !source.empty())) {
    std::cerr << "Supply fingerprints, destination, and exactly one of --verify or --source.\n";
    return 2;
  }
  const std::vector<std::string> roles{"entrypoint", "archive-header", "archive-data"};
  const std::vector<std::string> optional{"ArcadeInfo.xml", "charnames.zbm", "nxeart"};
  const auto install_ultimate = [&]() -> int {
    if (!ultimate) return 0;
#if defined(RBBLITZ_ULTIMATE_IMPORT)
    std::string warning, why;
    if (!portable::InstallUltimate(ultimate_source, destination, portable::EmbeddedUltimateConfig(), {}, &warning, &why)) {
      std::cerr << why << '\n'; return 1;
    }
    std::cout << "Ultimate installed. Select it in the in-game Game Version setting.\n";
    if (!warning.empty()) std::cerr << warning << '\n';
    return 0;
#else
    std::cerr << "This build does not include the native Ultimate installer.\n";
    return 2;
#endif
  };
  rb_blitz::fingerprint::GameFingerprint expected;
  rb_blitz::installer::GameSourcePlan plan;
  std::string error;
  if (!rb_blitz::fingerprint::LoadGameFingerprint(fingerprints, &expected, &error) ||
      !portable::RecoverImport(destination, expected, roles, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  if (!verify) {
    const bool probed = fs::is_directory(source)
        ? portable::ProbeFolder(source, expected, roles, optional, &plan, &error)
        : portable::ProbePackage(source, expected, roles, optional, &plan, &error);
    if (!probed) { std::cerr << error << '\n'; return 1; }
  }
  if (verify) {
    if (!portable::VerifyTree(destination, expected, roles, nullptr, &error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << "Supported game input verified.\n";
    return install_ultimate();
  }
  // The import transaction preserves extra existing files, including local DLC.
  error.clear();
  if (!portable::Import(plan, destination, expected, roles, optional, {}, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  std::cout << "Supported game input imported and verified.\n";
  return install_ultimate();
}
