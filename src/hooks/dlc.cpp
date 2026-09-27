// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The DLC content root. Blitz enumerates its downloadable songs through the emulated
// Xbox content APIs, not by reading a directory of its own: the guest resolves
// XamContentAggregateCreateEnumerator at boot (it is not in the import table), gets
// one item per content package, and opens what it found. The SDK serves all of that
// from the content root under Documents\rb_blitz, which is writable and holds the
// title's own saves, so this layer registers a second, read-only root instead:
//
//   <game_data_root>/dlc/<title_id>/<content_type>/<package>
//
// The packages are mounted where they lie - a real console stores DLC as STFS
// packages, not as extracted directories - and no write path ever resolves into this
// directory. 45410914 is Rock Band 3's title id, which Blitz lists as an alternate
// title id (XEX optional header 0x000407FF: 45410829, 45410869, 45410914) so RB3 DLC
// enumerates for Blitz; 5841122D is Blitz itself. See docs/dlc.md.

#include "hooks/dlc.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

#include "fs/dlc_layout.h"

namespace rb_blitz::dlc {
namespace {

REXCVAR_DEFINE_STRING(dlc_root, "", "Runtime",
                      "DLC directory, laid out as <title_id>/<content_type>/<package> "
                      "(default <game_data_root>/dlc, relative paths resolve against "
                      "game_data_root). Packages are mounted where they lie, read-only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

std::string JoinTitles(const std::vector<fs::DlcPackage>& packages) {
  std::vector<std::string> titles;
  for (const auto& package : packages) {
    if (std::find(titles.begin(), titles.end(), package.title_id) == titles.end()) {
      titles.push_back(package.title_id);
    }
  }
  std::sort(titles.begin(), titles.end());

  std::string text;
  for (const auto& title : titles) {
    if (!text.empty()) {
      text += ", ";
    }
    text += title;
  }
  return text;
}

}  // namespace

void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (runtime == nullptr) {
    return;
  }

  std::error_code ec;
  const std::filesystem::path root =
      std::filesystem::absolute(fs::ResolveDlcRoot(REXCVAR_GET(dlc_root), game_data_root), ec);

  if (!std::filesystem::is_directory(root, ec) || ec) {
    REXLOG_INFO("dlc: no content directory at {}, DLC is served from the content root alone",
                root.string());
    return;
  }

  const fs::DlcScanResult scan = fs::ScanDlcRoot(root);
  for (const auto& rejected : scan.rejected) {
    REXLOG_WARN("dlc: ignoring {} ({})", rejected.entry, rejected.reason);
  }

  if (scan.packages.empty()) {
    REXLOG_WARN("dlc: {} holds no <title_id>/<content_type>/<package> entry, nothing to mount",
                root.string());
    return;
  }

  rex::system::KernelState* kernel_state = runtime->kernel_state();
  rex::system::xam::ContentManager* content_manager =
      kernel_state != nullptr ? kernel_state->content_manager() : nullptr;
  if (content_manager == nullptr) {
    REXLOG_WARN("dlc: no content manager to register {} with, nothing to mount", root.string());
    return;
  }

  // Registering is all that is needed: enumeration and opening both resolve against
  // this root, and nothing writes to it.
  content_manager->set_extra_content_root(root);

  REXLOG_INFO("dlc: {} package(s) for title(s) {} in {} (read-only, mounted in place)",
              scan.packages.size(), JoinTitles(scan.packages), root.string());
}

}  // namespace rb_blitz::dlc
