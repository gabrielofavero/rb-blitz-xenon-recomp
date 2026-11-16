// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "game_import_core.h"

namespace rb_blitz::installer::portable {
// Platform transports write the response to file. InstallUltimate always checks
// its pinned length and SHA-256 before opening it, including injected transports.
using UltimateDownloader = std::function<bool(const std::filesystem::path& file,
    const PinsUltimate& pins, const ImportCallbacks& callbacks, std::string* error)>;
struct UltimateInstallConfig {
  PinsUltimate pins;
  fingerprint::GameFingerprint fingerprint;
  UltimateDownloader download;
};
UltimateInstallConfig EmbeddedUltimateConfig();
bool HasUltimate(const std::filesystem::path& game);
// Empty source downloads the pinned upstream release. Local ZIP/folder revisions
// warn on different fingerprints, as the Windows installer does. No XEX is copied.
bool InstallUltimate(const std::filesystem::path& source, const std::filesystem::path& game,
                     const UltimateInstallConfig& config, const ImportCallbacks& callbacks,
                     std::string* warning, std::string* error);
}  // namespace rb_blitz::installer::portable
