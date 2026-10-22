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
// enumerates for Blitz; 5841122D is Blitz itself. See docs/guides/dlc.md.
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
// Two adaptations, both measured rather than assumed (docs/guides/dlc.md §2.1):
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
// Hook hygiene (docs/reference/rb3-references.md §8): the faithful behaviour, and the reason for the
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
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
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
#include "fs/dlc_cached_library.h"
#include <chrono>
#include "fs/refresh_state.h"
#include "store/store_download_service.h"
#include "store/store_library.h"
#include "store/store_serve.h"
#if defined(_WIN32)
#include "store/store_transfer.h"
#endif

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
// library. The same folder named twice - a `dlc_root` a `dlc_library` entry also names,
// which is the shape the refresh harness drives - is one root, not two: walked twice,
// the scan files every package twice under two names (fs::UniqueLibraryRoots).
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
  return fs::UniqueLibraryRoots(roots);
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
//
// `scan_threads` comes from the caller rather than being read here, so the boot path and
// the in-game refresh cannot disagree about the thread count the way they cannot disagree
// about the cache - and so neither reads the registry off the worker thread (docs/guides/dlc.md).
fs::LibraryScanResult LoadOrScanLibrary(const bool cache_enabled, const bool force_refresh,
                                        const std::vector<std::filesystem::path>& roots,
                                        const uint32_t force_content_type,
                                        const uint32_t scan_threads,
                                        const std::filesystem::path& cache_path,
                                        const char* when, bool* cache_hit = nullptr) {
  const auto result = fs::LoadOrScanDlcLibrary(cache_enabled, force_refresh, roots,
                                              force_content_type, scan_threads, cache_path);
  if (cache_hit) *cache_hit = result.hit;
  REXLOG_INFO("dlc_cache: when={} enabled={} refresh={} result={} reason='{}' roots='{}' "
              "content_type={:08X} path='{}' packages={} files={}", when, cache_enabled,
              force_refresh, result.hit ? "hit" : "scan", result.reason, JoinLibraryRoots(roots),
              force_content_type, cache_path.string(), result.library.items.size(), result.library.files_seen);
  REXLOG_INFO("dlc_cache: fingerprint_ms={:.3f} load_ms={:.3f} scan_ms={:.3f} "
              "verify_ms={:.3f} write_ms={:.3f} written={} write_reason='{}'",
              result.fingerprint_ms, result.load_ms, result.scan_ms, result.verify_ms,
              result.write_ms, result.written, result.write_reason);
  for (const auto& rejected : result.library.rejected)
    REXLOG_WARN("dlc: ignoring {} ({})", rejected.entry, rejected.reason);
  return result.library;
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

// Everything the refresh needs, resolved once in Configure() and read from there by both
// paths. The refresh is triggered from a guest thread, and this project's rule is that a
// guest thread never touches the cvar registry (the reason src/hooks/menu_filter.cpp and
// src/hooks/content_progress.cpp capture their settings at boot), so nothing below reads a
// cvar outside Configure().
std::vector<std::filesystem::path> g_library_roots;
uint32_t g_force_content_type = 0;
uint32_t g_scan_threads = 1;
std::filesystem::path g_cache_path;

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

// R7's in-game refresh. One runs at a time, on a worker thread, so the frame that asked
// for it keeps drawing (the scan is ~31 s on a 1,402-package library - docs/guides/dlc.md §4.2);
// the result is handed to the content manager by PublishPendingRefresh() from a *guest*
// thread, because extra_content_library_ is a plain vector with no lock of its own and a
// worker writing it while the guest lists or resolves content would be a data race.
//
// `g_refresh_state` decides both rules (src/fs/refresh_state.h); the payload between the
// two threads is `g_refresh_items`/`g_refresh_paths`/`g_refresh_count`, guarded by this
// mutex, written by the worker and moved out by the guest thread that takes it.
fs::RefreshState g_refresh_state;
std::mutex g_refresh_payload_mutex;
std::vector<rex::system::xam::ExtraContentItem> g_refresh_items;
std::vector<std::filesystem::path> g_refresh_paths;
std::size_t g_refresh_count = 0;
// True from the instant a publisher takes the finished payload until the list is really
// handed to the content manager (see PublishPendingRefresh). Waiters must not resume on the
// take alone.
std::atomic<bool> g_refresh_publishing{false};

// The worker, kept so a finished one can be joined before the next starts. Its destructor
// detaches rather than joining, so a process that exits mid-scan cannot std::terminate on
// a still-joinable thread - the scan is host I/O and has nothing to tear down.
struct RefreshWorker {
  std::thread thread;
  ~RefreshWorker() {
    if (thread.joinable()) {
      thread.detach();
    }
  }
};
RefreshWorker g_refresh_worker;
std::mutex g_refresh_worker_mutex;
bool g_restarting = false;

// The scan half of a refresh: reads the configuration Configure() captured, scans the
// libraries and rewrites the cache. Pure host work - no guest state, no content manager,
// no cvar registry - which is what makes it safe on a worker thread.
struct RefreshScan {
  std::vector<rex::system::xam::ExtraContentItem> items;
  std::vector<std::filesystem::path> paths;  // what R9 draws its menu pool from
};

RefreshScan ScanLibraryForRefresh() {
  // The explicit refresh never accepts the cache: the tree is read again and the cache
  // rewritten, which is what the in-game row asks for.
  const fs::LibraryScanResult library =
      LoadOrScanLibrary(/*cache_enabled=*/true, /*force_refresh=*/true, g_library_roots,
                        g_force_content_type, g_scan_threads, g_cache_path, "the refresh");

  RefreshScan scan;
  scan.items.reserve(library.items.size());
  for (const auto& item : library.items) {
    scan.items.push_back({item.host_path, item.file_name, item.title_id,
                          rex::system::XContentType(item.content_type)});
  }
  // The same order RebuildPackagePaths() produces, built locally: the worker must not
  // touch g_package_paths, which a guest thread reads.
  scan.paths.reserve(g_structured_paths.size() + library.items.size());
  scan.paths.insert(scan.paths.end(), g_structured_paths.begin(), g_structured_paths.end());
  for (const auto& item : library.items) {
    scan.paths.push_back(item.host_path);
  }
  return scan;
}

// The refresh's worker half, shared by R7's in-game row and the store's post-install refresh.
// The caller holds g_refresh_worker_mutex and has already won fs::RefreshState::Begin().
void StartRefreshWorker(const char* why) {
  if (g_refresh_worker.thread.joinable()) {
    g_refresh_worker.thread.join();
  }
  REXLOG_INFO("dlc: {} asked for a refresh; scanning {} library root(s) on a worker thread so "
              "the frame keeps drawing",
              why, g_library_roots.size());
  g_refresh_worker.thread = std::thread([] {
    RefreshScan scan = ScanLibraryForRefresh();
    const std::size_t count = scan.items.size();
    {
      std::lock_guard<std::mutex> lock(g_refresh_payload_mutex);
      g_refresh_items = std::move(scan.items);
      g_refresh_paths = std::move(scan.paths);
      g_refresh_count = count;
    }
    g_refresh_state.Finish();
    REXLOG_INFO("dlc: the refresh finished ({} package(s)); the running title's library is "
                "handed over on the next menu frame",
                count);
  });
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
  const auto structured_start = std::chrono::steady_clock::now();
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

  REXLOG_INFO("dlc_sources: structured_root='{}' structured={} packages={} scan_ms={:.3f} "
              "user_root='{}' cache_root='{}'", root.string(), structured, scan.packages.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - structured_start).count(),
              runtime->user_data_root().string(), runtime->cache_root().string());

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
  // The emulated mount latency is **off on every boot** (owner directive, 2026-10-13: "it should be
  // as fast as we can"). It costs 100 ms per deferred overlapped completion and a large library
  // completes thousands of them: measured on a 1,402-package library, 2,803 completions, which is
  // 280 s of the 667 s the "Discovering Downloadable Content" screen takes *before* the host's own
  // walk is even counted. The completion still runs on the dispatch thread, still sets the
  // overlapped, still queues the completion routine - only the wait goes, exactly as R7's version
  // of this write did. The cvar keeps the faithful 100 ms for a caller who wants it
  // (rb_blitz.toml: `deferred_overlapped_delay_ms = 100`).
  REXCVAR_SET(deferred_overlapped_delay_ms, 0);
  REXLOG_INFO("dlc: the emulated mount latency is off this boot (deferred_overlapped_delay_ms = 0; "
              "the faithful 100 ms is one line in rb_blitz.toml away)");
  if (cache_enabled) {
    // R7 answers for the whole wait a large library costs, not just for the host's walk
    // of it: the cache makes that walk instant, and `deferred_overlapped_delay_ms` is
    // already 0 above for every boot, so what is left is the title's own one-item-per-frame
    // pacing of the enumeration.
    REXLOG_INFO("dlc: R7 on - the library scan is cached, so this boot reads no container header "
                "it can avoid");
  }
  const bool refresh_requested = REXCVAR_GET(refresh_dlc_cache);
  if (refresh_requested && !cache_enabled) {
    REXLOG_WARN("dlc: --refresh_dlc_cache has no effect while enhancements_dlc_cache is off");
  }

  const std::filesystem::path cache_path = fs::DlcCachePath(runtime->cache_root());

  // Captured for the in-game refresh, which runs from a guest thread and must not read
  // the registry (see the comment on g_library_roots). The refresh therefore resolves the
  // same roots, content type and thread count this boot did, and cannot disagree with it.
  g_library_roots = library_roots;
  g_force_content_type = force_content_type;
  g_scan_threads = REXCVAR_GET(dlc_scan_threads);
  g_cache_path = cache_path;

  // P5 of the RhythmVerse store plan: the store's download destination is this boot's resolved
  // DLC root, and its layout is the one the boot measured - a structured root gets
  // 45410914/00000002/, a flat one gets a loose container, and an absent root is created as a
  // flat library (never with a title-ID level, which would reclassify it). Nothing is created
  // here; the first install does that.
  {
    store::DownloadConfig downloads;
    downloads.layout.destination_root = root;
    downloads.layout.structured = structured;
    downloads.receipt_path = runtime->user_data_root() / "store" / "downloads.tsv";
#if defined(_WIN32)
    downloads.transfer = store::WinHttpTransfer();
#endif
    downloads.refresh = [] { RefreshAfterStoreInstall(); };
    downloads.log = [](std::string_view line, bool warning) {
      if (warning) {
        REXLOG_WARN("{}", line);
      } else {
        REXLOG_INFO("{}", line);
      }
    };
    store::ConfigureDownloads(std::move(downloads));
  }
  // The registration half (P5): a served row's job token is the worker's handle on that offer, so
  // the serving path gets the boot's one registrar here - beside the destination, before any page
  // can be served, and never in a test, which installs its own (store_serve.h).
  store::SetOfferRegistrar([](std::string_view file_id, std::string_view url,
                              std::optional<std::int64_t> api_size, std::string_view title) {
    return store::RegisterOffer(store::OfferRecord{std::string(file_id), std::string(url),
                                                   api_size, std::string(title)})
        .value_or(std::string());
  });
  // The registration half (P5): a served row's job token is the worker's handle on that offer, so
  // the serving path gets the boot's one registrar here - beside the destination, before any page
  // can be served, and never in a test, which installs its own (store_serve.h).
  store::SetOfferRegistrar([](std::string_view file_id, std::string_view url,
                              std::optional<std::int64_t> api_size, std::string_view title) {
    return store::RegisterOffer(store::OfferRecord{std::string(file_id), std::string(url),
                                                   api_size, std::string(title)})
        .value_or(std::string());
  });

  const fs::LibraryScanResult library =
      LoadOrScanLibrary(cache_enabled, refresh_requested, library_roots, force_content_type,
                        g_scan_threads, cache_path, "this boot", &g_cache_served);
  g_content_count = scan.packages.size() + library.items.size();
  g_structured_paths.clear();
  g_structured_paths.reserve(scan.packages.size());
  for (const auto& package : scan.packages) {
    g_structured_paths.push_back(package.host_path);
  }
  RebuildPackagePaths(library.items);
  if (g_cache_served) {
    REXLOG_INFO("dlc: the DLC enumeration was loaded from the cache this boot (R7); the "
                "status reports the cached content list");
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

void PublishPendingRefresh() {
  std::vector<rex::system::xam::ExtraContentItem> items;
  std::vector<std::filesystem::path> paths;
  std::size_t count = 0;
  {
    std::lock_guard<std::mutex> lock(g_refresh_payload_mutex);
    // Take() is the gate, not the check above it: two guest threads arriving together
    // must publish one result once, and the payload behind it is only moved by the
    // thread that won.
    if (!g_refresh_state.Take()) {
      return;
    }
    // Marked under the same lock as the take, so a thread that sees the pending flag
    // already consumed also sees that the write below has not happened yet - the
    // enumeration wait resumes on both. Cleared after the write: the wait's contract is
    // that a list handed over is visible to whoever resumes (measured 2026-10-15:
    // without this, two of three waiting enumeration messages resumed between the take
    // and the write and created their enumerators against the OLD list).
    g_refresh_publishing.store(true);
    items = std::move(g_refresh_items);
    paths = std::move(g_refresh_paths);
    count = g_refresh_count;
  }

  rex::system::xam::ContentManager* content_manager = ContentManagerOf(g_runtime);
  if (content_manager == nullptr) {
    REXLOG_WARN("dlc: the refresh found {} package(s) but there is no content manager to "
                "update",
                count);
    g_refresh_publishing.store(false);
    return;
  }
  content_manager->set_extra_content_library(std::move(items));
  g_content_count = g_structured_paths.size() + count;
  g_package_paths = std::move(paths);
  // The list just changed, so the store's Downloaded index (§3.2) is stale: drop it rather than
  // letting a page keep marking songs the library no longer (or newly) holds.
  rb_blitz::store::InvalidateLibrary();
  // After the list is in place, never before: everything that waits on a refresh waits for
  // this to clear.
  g_refresh_publishing.store(false);
  REXLOG_INFO("dlc: refreshed the running title's DLC library: {} package(s) in {}", count,
              JoinLibraryRoots(g_library_roots));
}

// Defined below, next to its contract: the press's bounded wait for the scan.
void WaitForRefreshToLand();

void RefreshConfigured() {
  {
    std::lock_guard<std::mutex> worker_lock(g_refresh_worker_mutex);
    if (g_restarting) return;
    if (!g_cache_enabled || g_runtime == nullptr) {
      return;
    }
    if (g_library_roots.empty()) {
      REXLOG_WARN("dlc: a refresh was asked for but no DLC library is configured, nothing to "
                  "scan");
      return;
    }
    // One at a time. A second ask while the scan is in flight rides the first one rather than
    // queueing a second scan of the same tree - and it still waits below, because what it
    // needs is the finished list, not a second worker
    // (docs/plans/finished/2026-10-10-song-cache-plan.md §9.5).
    if (!g_refresh_state.Begin()) {
      REXLOG_INFO("dlc: a refresh is already running; this ask rides it");
    } else {
      // The previous worker finished before Begin() above succeeded, so this joins an idle
      // thread rather than waiting for work.
      StartRefreshWorker("the main menu's refresh row");
    }
  }
  // The press waits (bounded) for the scan: see WaitForRefreshToLand below.
  WaitForRefreshToLand();
}

// P5's half of OQ6: a verified store install changed a flat library, so the host scans it and
// hands the result to the content manager exactly as R7's row does - but *without* the R7 cache
// gate, because the store's install is not a cache feature. The guest's own discovery is still
// not started from here (the store says a restart is required until that path is proven); what
// this guarantees is that a later menu-boundary publish and the next boot both see the package.
void RefreshAfterStoreInstall() {
  std::lock_guard<std::mutex> worker_lock(g_refresh_worker_mutex);
  if (g_restarting || g_runtime == nullptr) {
    return;
  }
  if (g_library_roots.empty()) {
    REXLOG_INFO("dlc: the store installed a package but no flat library is configured, so no "
                "host rescan is owed; a structured root's package list is captured at boot and "
                "the next boot is what lists it");
    return;
  }
  if (!g_refresh_state.Begin()) {
    REXLOG_INFO("dlc: a refresh is already running; the store's install rides it");
    return;
  }
  StartRefreshWorker("the store's install");
}

void DrainRefreshForRestart() {
  // P5: a restart must not race a download into the library. The service cancels the transfer
  // and joins its worker before the refresh barrier below.
  store::StopDownloads();
  std::lock_guard<std::mutex> worker_lock(g_refresh_worker_mutex);
  g_restarting = true;
  if (g_refresh_worker.thread.joinable()) g_refresh_worker.thread.join();
}

// The refresh row's press re-enters the title's own connect/discovery pass (src/ui/menu_options.cpp's
// TapRefreshCache navigates into it), and that pass must enumerate the list its own re-scan
// produced - not the one this boot loaded. The pass's aggregate enumerators are built from the
// content manager's list *at the create call*, and measured 2026-10-15 the creates run before
// the pass's first message (and are made by guest code the mod cannot wait on: the create is an
// ordinal-resolved import). So the waiting belongs here, inside the press: the row's action has
// only scheduled the screen transition at this point - nothing of the pass has run yet - and by
// the time this returns the list is either the finished one or the scan sat out its bound.
//
// PublishPendingRefresh() is called on this (guest) thread, exactly like the file-call hooks do
// at the menu, because the content manager's list is a plain vector the worker must not write.
//
// Bounded on purpose: a refresh always re-reads every container header - that is what the row
// asks for - so the press pays the per-file cost the cold boot pays. Measured 2026-10-15
// (out/_swap/menu-run/refresh12): 8.1 s of scan for a 600-file walk, 13.6 ms each on the default
// single thread, 8.9 s of press wait; docs/guides/dlc.md §4.2 has the per-file table and
// `dlc_scan_threads` divides it. The bound is where that stops being an acceptable hold: a scan
// still running after it must not hold the press any longer. The press returns, the pass runs over
// the previous list, and the fresh one lands mid-pass for the next enumeration (the shell poll and
// the file hooks publish it). The scan rewrote the cache, so the next *boot* is the cheap one; a
// second press in the same session scans again, because an explicit refresh never accepts the cache.
void WaitForRefreshToLand() {
  if (!g_cache_enabled || g_runtime == nullptr) {
    return;
  }
  constexpr int kBoundMs = 15000;
  const auto started = std::chrono::steady_clock::now();
  bool waited = false;
  const auto note_waiting = [&] {
    if (!waited) {
      waited = true;
      REXLOG_INFO("dlc: the refresh is still scanning; the press waits for it so the discovery "
                  "pass enumerates the fresh list");
    }
  };
  for (;;) {
    if (g_refresh_state.Pending()) {
      note_waiting();
      PublishPendingRefresh();
      continue;
    }
    if (!g_refresh_state.Running() && !g_refresh_publishing.load()) {
      break;
    }
    note_waiting();
    if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(kBoundMs)) {
      REXLOG_WARN("dlc: the library refresh did not finish within {} ms; the press returns and "
                  "the discovery pass runs over the previous list",
                  kBoundMs);
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (waited) {
    REXLOG_INFO("dlc: the refresh finished before the press returned ({} ms); the discovery "
                "pass enumerates the fresh list",
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count());
  }
}

bool RefreshArmed() { return g_cache_enabled && g_runtime != nullptr; }

bool CacheServedThisBoot() { return g_cache_served; }

std::size_t ContentItemCount() { return g_content_count; }

const std::vector<std::filesystem::path>& LibraryPackagePaths() { return g_package_paths; }

}  // namespace rb_blitz::dlc
