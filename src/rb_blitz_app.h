// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <rex/filesystem.h>
#include <rex/logging/macros.h>
#include <rex/rex_app.h>
#include <rex/version.h>

#include "generated/fingerprint_expected.h"
#include "util/sha256.h"

class RbBlitzApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<RbBlitzApp>(new RbBlitzApp(ctx, "rb_blitz",
        PPCImageConfig));
  }

  // Runtime/backend selection. Called before Runtime::Setup(); the GPU plugin
  // is loaded immediately after this hook returns, so set the plugin name here.
  // The `rexgpu-xenos` DLL is staged next to the executable via
  // rexglue_setup_target(rb_blitz GPU_PLUGINS xenos) in CMakeLists.txt.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
  }

  // Path policy. Called before logging is initialized, so keep this silent.
  //
  // game_data_root (read-only) comes from --game_data_root / the cvar and is
  // mounted read-only by the SDK (allow_game_relative_writes defaults off).
  // Writable state (user/update/cache) must never live inside game_data_root.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    auto redirect_if_inside_game_root = [&](std::filesystem::path& p) {
      if (p.empty() || paths.game_data_root.empty()) return;
      std::error_code ec;
      auto gp = std::filesystem::weakly_canonical(
          std::filesystem::absolute(paths.game_data_root, ec), ec);
      auto pp = std::filesystem::weakly_canonical(
          std::filesystem::absolute(p, ec), ec);
      auto rel = std::filesystem::relative(pp, gp, ec);
      bool inside = rel.empty() ||
                    (!rel.is_absolute() && *rel.begin() != std::filesystem::path(".."));
      if (inside) p.clear();
    };

    redirect_if_inside_game_root(paths.user_data_root);
    redirect_if_inside_game_root(paths.cache_root);
    redirect_if_inside_game_root(paths.update_data_root);

    // Writable roots default outside the game tree (platform user dir).
    if (paths.user_data_root.empty()) {
      paths.user_data_root = rex::filesystem::GetUserFolder() / "rb_blitz";
    }
    if (paths.cache_root.empty()) {
      paths.cache_root = paths.user_data_root / "cache";
    }
  }

  // Boot identity. Called once the XEX is loaded and the paths are final, which
  // is the first point where game_data_root() is known and logging is up.
  //
  // The build gate in CMakeLists.txt has already refused to recompile against a
  // dump other than the one config/game_fingerprints.toml describes, but a
  // launcher can still point a built binary at a different game/ tree - a Deluxe
  // content root, for instance. Nothing here aborts: the point is that the log
  // says which revision (if any) the running executable was actually booted
  // against, so a bug report can be matched to it.
  void OnPostLoadXexImage() override {
    REXLOG_INFO("boot identity: {}", REXGLUE_BUILD_STAMP);

    const std::filesystem::path entrypoint =
        game_data_root() / rb_blitz::fingerprint::vanilla::kEntrypointPath;
    std::error_code ec;
    std::uint64_t size = 0;
    const std::string digest = rb_blitz::util::HashFileHex(entrypoint, &size, ec);
    if (digest.empty()) {
      REXLOG_WARN("game data identity: cannot read {} ({})", entrypoint.string(),
                  ec.message());
      return;
    }

    if (size == rb_blitz::fingerprint::vanilla::kEntrypointSize &&
        digest == rb_blitz::fingerprint::vanilla::kEntrypointSha256) {
      REXLOG_INFO("game data identity: {} {} ({} bytes, sha256 {})",
                  rb_blitz::fingerprint::vanilla::kGameName,
                  rb_blitz::fingerprint::vanilla::kXexVersion, size, digest);
      return;
    }

    REXLOG_WARN("game data identity: MODIFIED - {} is {} bytes, sha256 {}",
                entrypoint.string(), size, digest);
    REXLOG_WARN("  expected: {} bytes, sha256 {} (config/game_fingerprints.toml, see"
                " docs/deluxe-compat.md)",
                rb_blitz::fingerprint::vanilla::kEntrypointSize,
                rb_blitz::fingerprint::vanilla::kEntrypointSha256);
  }
};
