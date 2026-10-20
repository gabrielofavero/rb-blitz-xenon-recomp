// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_probe.h"

#include <mutex>
#include <set>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "fs/payload_overlay.h"

REXCVAR_DEFINE_BOOL(store_trace, false, "Diagnostics",
                    "Log the store paths the guest resolves and opens under the game root "
                    "(RhythmVerse plan P2's data-layer probe)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(store_local_server_root, "", "Diagnostics",
                      "Point the retail store's netcache at a local server rooted at this "
                      "guest-relative directory (RhythmVerse plan P2's local-server spike; empty "
                      "leaves config/gen/store.dtb as shipped)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

bool IsStorePath(std::string_view path) {
  // `config/gen/store.dtb` is the netcache definition itself (`netcache_init`): whether the guest
  // opens it, and through which device, is the first question the P2 gate has to answer - a record
  // nobody reads cannot be pointed at a local server. The UI's own `ui/store/gen/store.dtb` carries
  // the same name, and the suffix test catches both without naming every directory.
  if (path.size() >= 9 && path.substr(path.size() - 9) == "store.dtb") {
    return true;
  }
  if (path.find("ui/store") != std::string_view::npos ||
      path.find("dlc_store") != std::string_view::npos ||
      path.find("dlc_top") != std::string_view::npos ||
      path.find("remote_metadata") != std::string_view::npos) {
    return true;
  }
  // The plan's two offer fragments are ordinary words, so they only count inside a store path:
  // `<root>/offers/<id>/` and `<root>/related/<id>.dta` are the templates, and neither word means
  // anything on its own.
  if (path.find("store") != std::string_view::npos &&
      (path.find("/offers") != std::string_view::npos ||
       path.find("/related") != std::string_view::npos)) {
    return true;
  }
  return false;
}

// One line per distinct (kind, path) per boot: a store screen resolves the same directory
// repeatedly, and the point of the probe is the *list* of names, not a count.
constexpr size_t kMaxDistinctPaths = 512;

void Observe(std::string_view relative_path, bool open, uint32_t desired_access, int32_t status) {
  if (!IsStorePath(relative_path)) {
    return;
  }
  static std::mutex mutex;
  static std::set<std::string> seen;
  const std::string key = std::string(open ? "open " : "resolve ") + std::string(relative_path);
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() >= kMaxDistinctPaths) {
      return;
    }
    if (!seen.insert(key).second) {
      return;
    }
  }
  if (open) {
    REXLOG_INFO("store-probe: open {} (access {:#x}) -> {:#x}", relative_path, desired_access,
                static_cast<uint32_t>(status));
  } else {
    REXLOG_INFO("store-probe: resolve {}", relative_path);
  }
}

}  // namespace

void InstallPathProbe() {
  const bool on = REXCVAR_GET(store_trace);
  fs::SetStorePathObserver(on ? &Observe : nullptr);
  REXLOG_INFO("store-probe: path trace {}; the local server root is '{}'", on ? "on" : "off",
              LocalServerRoot());
}

std::string LocalServerRoot() { return REXCVAR_GET(store_local_server_root); }

}  // namespace rb_blitz::store
