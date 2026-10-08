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
#include <rex/system/flags.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

#include "fs/dlc_layout.h"
#include "fs/dlc_library.h"
#include "fs/dlc_cache.h"

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
REXCVAR_DECLARE(bool, enhancements_dlc_cache);

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

// The scan's second lever on a cold library. The per-file cost of a first open is not
// the disk - it is whatever inspects a file the antivirus has not seen (measured ~16 ms
// per file cold against ~0.03 ms warm, on an NVMe SSD, and charged per file rather than
// per byte), so opening several at once overlaps it: the production scan measured 5.6x
// faster per file cold (16.1 ms/file against 2.9 ms/file, disjoint cold trees). The
// walk and the merge stay on the calling thread and in walk order, so the answer -
// items, names, counters, rejections - is exactly the same at any thread count
// (src/fs/dlc_library.h, tests/dlc_library_tests.cpp).
REXCVAR_DEFINE_UINT32(dlc_scan_threads, 1, "Runtime",
                      "Threads used to read a DLC library's package headers: 1 (the "
                      "default) reads them on the calling thread; 0 uses the machine's "
                      "core count, capped at 8; any other value is that many workers. "
                      "The scan's result is identical at any thread count.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// R7's one-shot. With enhancements_dlc_cache on, a normal boot reads the persisted
// enumeration when it can be proved current; this flag says "do not trust it": the
// library is scanned again and the cache rewritten, which is the refresh. It is read
// once at boot and changes nothing on its own, so it is restart-scoped like the rest.
REXCVAR_DEFINE_BOOL(refresh_dlc_cache, false, "Runtime",
                    "R7: ignore the persisted DLC enumeration this boot, re-scan the "
                    "configured libraries and rewrite the cache. Only meaningful with "
                    "enhancements_dlc_cache on. Faithful: a boot reads a current cache "
                    "when there is one.")
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

// The library roots the boot path and the in-game refresh both read: the structured
// root when it is *not* the layout (a dumped song folder), plus every configured
// library. Naming the same folder twice is harmless because the scan keys a package by
// its own identity.
std::vector<std::filesystem::path> DiscoverLibraryRoots(const std::filesystem::path& game_data_root,
                                                        const std::filesystem::path& root,
                                                        const bool root_is_library) {
  std::vector<std::filesystem::path> roots;
  if (root_is_library) {
    roots.push_back(root);
  }
  for (const std::string& entry : fs::SplitPathList(REXCVAR_GET(dlc_library))) {
    std::error_code ec;
    roots.push_back(std::filesystem::absolute(fs::ResolveDlcRoot(entry, game_data_root), ec));
  }
  return roots;
}

// Reads the library enumeration from the cache when the toggle is on, the tree is
// unchanged and no refresh was asked for; otherwise scans and, when the toggle is on,
// rewrites the cache. Anything that makes the proof fail is a miss: the scan always
// runs, so a cache can never hide a package. Shared by the boot path and the in-game
// refresh so the two can never disagree about what the cache means.
//
// `cache_hit`, when given, is set only on the branch that answers out of the persisted
// file - the boot path reports it so the menu can say the enumeration was loaded rather
// than discovered (rb_blitz::dlc::CacheServedThisBoot()).
fs::LibraryScanResult LoadOrScanLibrary(const bool cache_enabled, const bool force_refresh,
                                        const std::vector<std::filesystem::path>& roots,
                                        const uint32_t force_content_type,
                                        const std::filesystem::path& cache_path,
                                        const char* when, bool* cache_hit = nullptr) {
  if (cache_hit != nullptr) {
    *cache_hit = false;
  }
  bool fingerprint_ok = false;
  fs::DlcTreeFingerprint fingerprint;
  if (cache_enabled && !roots.empty()) {
    std::string reason;
    fingerprint_ok = fs::ComputeLibraryFingerprint(roots, &fingerprint, &reason);
    if (!fingerprint_ok) {
      REXLOG_WARN("dlc: the DLC libraries could not be fingerprinted ({}), scanning {}", reason,
                  when);
    }
  }

  if (cache_enabled && !roots.empty() && fingerprint_ok && !force_refresh) {
    fs::DlcLibraryCache cached;
    std::string cache_reason;
    if (!fs::LoadDlcCache(cache_path, &cached, &cache_reason)) {
      REXLOG_INFO("dlc: no usable library cache ({}), scanning", cache_reason);
    } else {
      std::string match_reason;
      if (fs::DlcCacheMatches(cached, roots, force_content_type, fingerprint, &match_reason)) {
        REXLOG_INFO("dlc: library cache hit for {} package(s) over {} file(s) in {}",
                    cached.items.size(), fingerprint.file_count, cache_path.string());
        if (cache_hit != nullptr) {
          *cache_hit = true;
        }
        return fs::ToLibraryScanResult(cached);
      }
      REXLOG_INFO("dlc: library cache ignored ({}), scanning", match_reason);
    }
  } else if (cache_enabled && force_refresh && !roots.empty()) {
    REXLOG_INFO("dlc: refresh requested, re-scanning the libraries and rewriting {}",
                cache_path.string());
  }

  // Read here rather than passed in, so the boot path and the in-game refresh cannot
  // disagree about the thread count the way they cannot disagree about the cache.
  const uint32_t scan_threads = REXCVAR_GET(dlc_scan_threads);
  REXLOG_DEBUG("dlc: scanning {} library root(s) for package headers on {} thread(s)",
               roots.size(), scan_threads == 0 ? fs::DefaultDlcScanThreads() : scan_threads);
  fs::LibraryScanResult library = fs::ScanDlcLibraries(roots, force_content_type, scan_threads);
  for (const auto& rejected : library.rejected) {
    REXLOG_WARN("dlc: ignoring {} ({})", rejected.entry, rejected.reason);
  }
  if (cache_enabled && fingerprint_ok) {
    std::string save_reason;
    const fs::DlcLibraryCache cache =
        fs::MakeDlcLibraryCache(roots, force_content_type, fingerprint, library);
    if (fs::SaveDlcCache(cache_path, cache, &save_reason)) {
      REXLOG_INFO("dlc: library cache written for {} package(s) to {}", library.items.size(),
                  cache_path.string());
    } else {
      REXLOG_WARN("dlc: library cache not written ({}), the libraries are scanned every boot",
                  save_reason);
    }
  }
  return library;
}

// One content type for the whole library when one was named; otherwise
// fs::LibraryContentType() adapts each package. A value that is not hex digits is
// reported and ignored rather than silently dropping the override or the library.
// Read by both the boot path and the refresh so the two agree.
uint32_t ForceContentTypeFromCvar() {
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
  return force_content_type;
}

// The content manager the DLC sources are handed to, or null before the guest exists.
rex::system::xam::ContentManager* ContentManagerOf(rex::Runtime* runtime) {
  rex::system::KernelState* kernel_state = runtime != nullptr ? runtime->kernel_state() : nullptr;
  return kernel_state != nullptr ? kernel_state->content_manager() : nullptr;
}

// What the boot path resolved, kept so the in-game refresh - which runs from a
// filesystem hook with no arguments - can rebuild the same sources and cache.
rex::Runtime* g_runtime = nullptr;
std::filesystem::path g_game_data_root;
bool g_cache_enabled = false;

// R7's two answers about this boot, for the callers that draw the result: whether
// the enumeration came out of the persisted cache, and how many packages the flat
// library holds. Written in Configure(), read from guest threads afterwards.
bool g_cache_served = false;
std::size_t g_content_count = 0;

// The packages a later layer (R9) may draw a song from: the structured root's packages
// and the flat library's, in that order. Kept from the scans so R9 does not walk the
// tree again; the structured half is held separately because a library-only refresh
// replaces just the other half. Written in Configure()/Refresh(), read once at boot by
// src/hooks/shell_music.cpp.
std::vector<std::filesystem::path> g_structured_paths;
std::vector<std::filesystem::path> g_package_paths;

void RebuildPackagePaths(const std::vector<fs::LibraryItem>& library) {
  g_package_paths.clear();
  g_package_paths.reserve(g_structured_paths.size() + library.size());
  g_package_paths.insert(g_package_paths.end(), g_structured_paths.begin(),
                         g_structured_paths.end());
  for (const auto& item : library) {
    g_package_paths.push_back(item.host_path);
  }
}

}  // namespace

void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (runtime == nullptr) {
    return;
  }
  g_runtime = runtime;
  g_game_data_root = game_data_root;

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
  if (is_directory) {
    structured = fs::HasStructuredLayout(root);
    if (structured) {
      scan = fs::ScanDlcRoot(root);
      for (const auto& rejected : scan.rejected) {
        REXLOG_WARN("dlc: ignoring {} ({})", rejected.entry, rejected.reason);
      }
    }
  }

  // A dlc_root that is not the structured layout is a library too, so pointing it at a
  // dumped song folder needs no other configuration; the configured libraries join it.
  const std::vector<std::filesystem::path> library_roots =
      DiscoverLibraryRoots(game_data_root, root, is_directory && !structured);

  const uint32_t force_content_type = ForceContentTypeFromCvar();

  // R7. With enhancements_dlc_cache on, the result of the library scan is persisted
  // beside the user's saves and read back when it can be proved to describe the same
  // tree, so a boot does not open every container and read its header again. Anything
  // that makes the proof fail - no file, a damaged one, a changed tree, a different
  // library list - falls through to the scan below, because a cache must never be able
  // to hide a package. --refresh_dlc_cache forces that scan-and-write path.
  const bool cache_enabled = REXCVAR_GET(enhancements_dlc_cache);
  g_cache_enabled = cache_enabled;
  if (cache_enabled) {
    // R7 answers for the whole wait a large library costs, not just for the host's walk
    // of it. The cache makes that walk instant, but the boot still mounts every package
    // once per enumeration pass, and each of those opens waits out the emulator's
    // emulated storage latency - a fixed 100 ms per deferred overlapped completion
    // (rexglue-sdk/0011). Measured on D:\Games\YARG Songs (1402 packages): 2803 of them,
    // 280 s of the 667 s the "Discovering Downloadable Content" screen takes. Zero keeps
    // the completion asynchronous - it still runs on the dispatch thread, still sets the
    // overlapped, still queues the completion routine - and only drops the wait.
    REXCVAR_SET(deferred_overlapped_delay_ms, 0);
    REXLOG_INFO("dlc: the emulated mount latency the title waits out per package is off "
                "this boot (deferred_overlapped_delay_ms = 0, R7)");
  }
  const bool refresh_requested = REXCVAR_GET(refresh_dlc_cache);
  if (refresh_requested && !cache_enabled) {
    REXLOG_WARN("dlc: --refresh_dlc_cache has no effect while enhancements_dlc_cache is off");
  }

  const std::filesystem::path cache_path = fs::DlcCachePath(runtime->cache_root());
  const fs::LibraryScanResult library =
      LoadOrScanLibrary(cache_enabled, refresh_requested, library_roots, force_content_type,
                        cache_path, "this boot", &g_cache_served);
  g_content_count = library.items.size();
  g_structured_paths.clear();
  g_structured_paths.reserve(scan.packages.size());
  for (const auto& package : scan.packages) {
    g_structured_paths.push_back(package.host_path);
  }
  RebuildPackagePaths(library.items);
  if (g_cache_served) {
    REXLOG_INFO("dlc: the DLC enumeration was loaded from the cache this boot (R7); the "
                "main menu says so (\"Loading Song Cache\")");
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

  rex::system::xam::ContentManager* content_manager = ContentManagerOf(runtime);

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

void Refresh(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (runtime == nullptr) {
    return;
  }

  std::error_code ec;
  const std::filesystem::path root =
      std::filesystem::absolute(fs::ResolveDlcRoot(REXCVAR_GET(dlc_root), game_data_root), ec);
  const bool is_directory = std::filesystem::is_directory(root, ec) && !ec;
  const bool structured = is_directory && fs::HasStructuredLayout(root);
  const std::vector<std::filesystem::path> library_roots =
      DiscoverLibraryRoots(game_data_root, root, is_directory && !structured);

  // The explicit refresh never accepts the cache: the tree is read again and the cache
  // rewritten, which is what the in-game row asks for. It runs on the thread that asked,
  // so the game itself is the loading screen while the scan is happening.
  const fs::LibraryScanResult library = LoadOrScanLibrary(
      /*cache_enabled=*/true, /*force_refresh=*/true, library_roots, ForceContentTypeFromCvar(),
      fs::DlcCachePath(runtime->cache_root()), "the refresh");

  rex::system::xam::ContentManager* content_manager = ContentManagerOf(runtime);
  if (content_manager == nullptr) {
    REXLOG_WARN("dlc: refresh found {} package(s) but there is no content manager to update",
                library.items.size());
    return;
  }

  std::vector<rex::system::xam::ExtraContentItem> items;
  items.reserve(library.items.size());
  for (const auto& item : library.items) {
    items.push_back(
        {item.host_path, item.file_name, item.title_id, rex::system::XContentType(item.content_type)});
  }
  content_manager->set_extra_content_library(std::move(items));
  g_content_count = library.items.size();
  RebuildPackagePaths(library.items);
  REXLOG_INFO("dlc: refreshed the running title's DLC library: {} package(s) in {}",
              library.items.size(), JoinLibraryRoots(library_roots));
}

void RefreshConfigured() {
  if (!g_cache_enabled || g_runtime == nullptr) {
    return;
  }
  Refresh(g_runtime, g_game_data_root);
}

bool RefreshArmed() { return g_cache_enabled && g_runtime != nullptr; }

bool CacheServedThisBoot() { return g_cache_served; }

std::size_t ContentItemCount() { return g_content_count; }

const std::vector<std::filesystem::path>& LibraryPackagePaths() { return g_package_paths; }

}  // namespace rb_blitz::dlc
