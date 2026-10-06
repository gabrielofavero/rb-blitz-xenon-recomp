// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The DLC content root. Blitz enumerates its downloadable songs through the emulated
// Xbox content APIs, not by reading a directory of its own: the guest resolves
// XamContentAggregateCreateEnumerator at boot (it is not in the import table), gets
// one item per content package, and opens what it found. The SDK serves all of that
// from the content root under Documents\Rock Band Blitz, which is writable and holds the
// title's own saves, so this layer registers a second, read-only root instead:
//
//   <game_data_root>/dlc/<title_id>/<content_type>/<package>
//
// The packages are mounted where they lie - a real console stores DLC as STFS
// packages, not as extracted directories - and no write path ever resolves into this
// directory. 45410914 is Rock Band 3's title id, which Blitz lists as an alternate
// title id (XEX optional header 0x000407FF: 45410829, 45410869, 45410914) so RB3 DLC
// enumerates for Blitz; 5841122D is Blitz itself. See docs/dlc.md.
//
// A dumped song folder is not built that way: it is thousands of loose CON/LIVE
// containers in no particular arrangement, and it cannot be restructured on a volume
// without links. So a directory that is *not* the layout above is read as a flat
// library instead - each container is placed under the title id its own header names
// and the content type derived from it, by `--dlc_library` or by pointing `--dlc_root`
// at it - and handed
// to the content manager as such (fs::ScanDlcLibraries, then
// ContentManager::set_extra_content_library()). Nothing is copied or renamed: the
// library is resolved where it lies.
//
// Two adaptations, both measured rather than assumed (docs/dlc.md §2.1):
//   * a saved-game package (00000001) - what a custom-song folder is made of - is
//     presented as marketplace content (00000002), because the title reads its
//     downloadable songs out of the marketplace enumeration and does not list a saved
//     game at all. `dlc_library_content_type` replaces this for the whole library.
//   * a library package with no license entries of its own is licensed with the
//     configured `license_mask` (1 by default), the value XamContentGetLicenseMask
//     answers with: the extra sources have no `.header` file to carry a mask, and an
//     unsigned custom CON carries none, so the title listed such a song and refused to
//     start it. The fallback lives in ContentManager::OpenContent(), beside the
//     `.header` read it stands in for.
//
// Hook hygiene (docs/rb3-references.md §8): the faithful behaviour, and the reason for the
// deviation.
//
// Faithful behaviour: the guest enumerates downloadable content through the emulated
// Xbox content APIs and the SDK serves them from the writable content root under
// Documents\Rock Band Blitz. The title has no notion of a DLC directory of its own, and
// nothing here disables that root - it keeps working exactly as it did.
//
// Deviation: two read-only sources are registered so packages under
// <game_data_root>/dlc - and in a flat library - enumerate and open like installed
// content. The structured one is mounted in place; the library one is resolved by
// each package's own header, because it has no title_id/content_type levels to mount
// against. Every early return below is a refusal to add a source, not a change to the
// guest's existing view: a missing directory, no usable package, no kernel state or no
// content manager leaves the SDK's own root as the only source, which is the faithful
// behaviour, and each is logged.
//
// Which refusal a source and its scan amount to is pure - fs::DecideDlcRegistration
// and fs::HasStructuredLayout in src/fs/dlc_layout.h, fs::ScanDlcLibraries in
// src/fs/dlc_library.h, covered by tests/dlc_layout_tests.cpp and
// tests/dlc_library_tests.cpp - so only handing the sources to the content manager
// needs a runtime.

#include "hooks/dlc.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

#include "fs/dlc_layout.h"
#include "fs/dlc_library.h"

namespace rb_blitz::dlc {
namespace {

REXCVAR_DEFINE_STRING(dlc_root, "", "Runtime",
                      "DLC directory, laid out as <title_id>/<content_type>/<package> "
                      "(default <game_data_root>/dlc, relative paths resolve against "
                      "game_data_root). Packages are mounted where they lie, read-only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// A second, independent source: folders holding STFS packages in any arrangement,
// each package identified by the title id and content type in its own header
// (src/fs/dlc_library.h). This is what makes a dumped song folder - thousands of
// loose CON/LIVE containers - usable without restructuring it.
REXCVAR_DEFINE_STRING(dlc_library, "", "Runtime",
                      "Flat DLC library directories, ';'-separated: folders of STFS "
                      "packages nested any way, each mounted under the title id and "
                      "content type its own header names. Relative paths resolve against "
                      "game_data_root. A dlc_root that is not the structured layout is "
                      "scanned as a library too.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// The content type is how the guest decides what to do with an item, and a custom-song
// library is packaged as saved-game content (00000001) even though the title reads it
// out of the marketplace (00000002) one. Empty keeps each package's own type, which is
// the faithful reading; naming one presents the whole library under it.
REXCVAR_DEFINE_STRING(dlc_library_content_type, "", "Runtime",
                      "Content type to present every library package under, as 8 hex "
                      "digits (e.g. 00000002). Empty (the default) presents only the "
                      "saved-game packages (00000001) as marketplace content (00000002), "
                      "which is what makes a dumped custom-song folder listable and "
                      "playable; the other packages keep their own type.")
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

std::string Hex8(uint32_t value) {
  char text[9];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

std::string JoinLibraryRoots(const std::vector<std::filesystem::path>& roots) {
  std::string text;
  for (const auto& root : roots) {
    if (!text.empty()) {
      text += ", ";
    }
    text += root.string();
  }
  return text;
}

// What the library holds, summarised the way the structured log line summarises title
// directories: one entry per (title id, content type) group, with its package count.
std::string JoinLibraryGroups(const std::vector<fs::LibraryItem>& items) {
  std::map<uint64_t, std::size_t> counts;
  for (const auto& item : items) {
    ++counts[(uint64_t(item.title_id) << 32) | item.content_type];
  }

  std::string text;
  for (const auto& [key, count] : counts) {
    if (!text.empty()) {
      text += ", ";
    }
    text += Hex8(static_cast<uint32_t>(key >> 32)) + "/" +
            Hex8(static_cast<uint32_t>(key)) + " x" + std::to_string(count);
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
  const bool is_directory = std::filesystem::is_directory(root, ec) && !ec;

  // The structured root is scanned only when the directory is there and actually holds
  // the <title_id>/<content_type>/<package> layout. A folder of loose containers - the
  // case the library exists for - is scanned as a library instead, so pointing
  // --dlc_root at a dumped song folder just works. An absent folder is the normal case
  // and produces no scan's worth of noise.
  fs::DlcScanResult scan;
  bool structured = false;
  std::vector<std::filesystem::path> library_roots;
  if (is_directory) {
    structured = fs::HasStructuredLayout(root);
    if (structured) {
      scan = fs::ScanDlcRoot(root);
      for (const auto& rejected : scan.rejected) {
        REXLOG_WARN("dlc: ignoring {} ({})", rejected.entry, rejected.reason);
      }
    } else {
      library_roots.push_back(root);
    }
  }

  // Configured libraries join it; naming the same folder twice is harmless because the
  // scan keys a package by its own identity.
  for (const std::string& entry : fs::SplitPathList(REXCVAR_GET(dlc_library))) {
    std::error_code entry_ec;
    library_roots.push_back(
        std::filesystem::absolute(fs::ResolveDlcRoot(entry, game_data_root), entry_ec));
  }

  // One content type for the whole library when one was named; otherwise
  // fs::LibraryContentType() adapts each package. A value that is not hex digits is
  // reported and ignored rather than silently dropping the override or the library.
  uint32_t force_content_type = 0;
  const std::string content_type_override = REXCVAR_GET(dlc_library_content_type);
  if (!content_type_override.empty()) {
    if (!fs::ParseHex32(content_type_override, &force_content_type) || force_content_type == 0) {
      REXLOG_WARN("dlc: dlc_library_content_type '{}' is not an 8-digit hex content type, "
                  "adapting each package instead",
                  content_type_override);
      force_content_type = 0;
    }
  }

  const fs::LibraryScanResult library = fs::ScanDlcLibraries(library_roots, force_content_type);
  for (const auto& rejected : library.rejected) {
    REXLOG_WARN("dlc: ignoring {} ({})", rejected.entry, rejected.reason);
  }
  if (!library.items.empty()) {
    if (force_content_type != 0) {
      REXLOG_INFO("dlc: library packages presented as content type {} (dlc_library_content_type)",
                  Hex8(force_content_type));
    } else if (library.adapted_saved_games != 0) {
      REXLOG_INFO("dlc: {} saved-game library package(s) presented as content type {} so the "
                  "title lists and plays them (dlc_library_content_type overrides this)",
                  library.adapted_saved_games, Hex8(fs::kMarketplaceContentType));
    }
  }

  rex::system::KernelState* kernel_state = runtime->kernel_state();
  rex::system::xam::ContentManager* content_manager =
      kernel_state != nullptr ? kernel_state->content_manager() : nullptr;

  const std::size_t structured_count = structured ? scan.packages.size() : 0;
  const std::size_t library_count = library.items.size();
  // Whether anything at all was asked for: a root to scan or a library to scan.
  // Without either, an absent DLC folder is just the normal case.
  const bool configured = is_directory || !library_roots.empty();

  // Which refusal, if any, the sources amount to. The decision itself is host logic
  // and is pinned by tests/dlc_layout_tests.cpp; only the effects below need a runtime.
  switch (fs::DecideDlcRegistration(configured, structured_count, library_count,
                                    content_manager != nullptr)) {
    case fs::DlcRegistration::kNoDirectory:
      REXLOG_INFO("dlc: no content directory at {}, DLC is served from the content root alone",
                  root.string());
      return;
    case fs::DlcRegistration::kNoSource:
      REXLOG_WARN(
          "dlc: {} or the configured libraries hold no mountable package, nothing to mount",
          root.string());
      return;
    case fs::DlcRegistration::kNoContentManager:
      REXLOG_WARN("dlc: no content manager to register the DLC sources with, nothing to mount");
      return;
    case fs::DlcRegistration::kRegisterStructured:
    case fs::DlcRegistration::kRegisterLibrary:
    case fs::DlcRegistration::kRegisterBoth:
      break;
  }

  // Registering is all that needs doing: enumeration and opening both resolve against
  // these sources, and nothing writes to either.
  if (structured_count != 0) {
    content_manager->set_extra_content_root(root);
    REXLOG_INFO("dlc: {} package(s) for title(s) {} in {} (read-only, mounted in place)",
                structured_count, JoinTitles(scan.packages), root.string());
  }
  if (library_count != 0) {
    std::vector<rex::system::xam::ExtraContentItem> items;
    items.reserve(library.items.size());
    for (const auto& item : library.items) {
      items.push_back({item.host_path, item.file_name, item.title_id,
                       rex::system::XContentType(item.content_type)});
    }
    content_manager->set_extra_content_library(std::move(items));
    // Starts with the count the way the structured line does, so the acceptance
    // harness's `dlc: (\d+) package\(s\)` reads either source.
    REXLOG_INFO("dlc: {} package(s), {} not a container, in {} file(s) walked ({}) in {} "
                "by their own headers (read-only, mounted in place)",
                library_count, library.other_files, library.files_seen,
                JoinLibraryGroups(library.items), JoinLibraryRoots(library_roots));
  }
}

}  // namespace rb_blitz::dlc
