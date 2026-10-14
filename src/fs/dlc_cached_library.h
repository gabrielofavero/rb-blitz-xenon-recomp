// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <chrono>
#include "fs/dlc_cache.h"

namespace rb_blitz::fs {

struct CachedLibraryResult {
  LibraryScanResult library;
  bool hit = false, written = false;
  std::string reason, write_reason;
  double fingerprint_ms = 0, load_ms = 0, scan_ms = 0, verify_ms = 0, write_ms = 0;
};

// Shared by boot, refresh and the host benchmark. A failed or changing scan must
// never become a valid empty/partial cache on the next boot.
inline CachedLibraryResult LoadOrScanDlcLibrary(bool enabled, bool refresh,
    const std::vector<std::filesystem::path>& roots, uint32_t content_type,
    uint32_t threads, const std::filesystem::path& path) {
  CachedLibraryResult result;
  const auto timed = [](double& ms, auto&& operation) {
    const auto start = std::chrono::steady_clock::now();
    const bool ok = operation();
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return ok;
  };
  DlcTreeFingerprint before;
  bool fingerprint_ok = false;
  if (!enabled) result.reason = "disabled";
  else if (roots.empty()) result.reason = "no flat library roots";
  else {
    fingerprint_ok = timed(result.fingerprint_ms, [&] {
      return ComputeLibraryFingerprint(roots, &before, &result.reason);
    });
    if (fingerprint_ok && !refresh) {
      DlcLibraryCache cache;
      result.hit = timed(result.load_ms, [&] {
        return LoadDlcCache(path, &cache, &result.reason) &&
               DlcCacheMatches(cache, roots, content_type, before, &result.reason);
      });
      if (result.hit) {
        result.reason = "unchanged library";
        result.library = ToLibraryScanResult(cache);
        return result;
      }
    } else if (fingerprint_ok) result.reason = "explicit refresh";
  }
  timed(result.scan_ms, [&] {
    result.library = ScanDlcLibraries(roots, content_type, threads);
    return true;
  });
  if (enabled && fingerprint_ok) {
    // Malformed packages can be fixed without changing their size. Refuse to
    // persist any rejected scan, including transient header-open failures.
    if (!result.library.rejected.empty()) result.write_reason = "scan has rejected entries";
    else {
      DlcTreeFingerprint after;
      const bool stable = timed(result.verify_ms, [&] {
        return ComputeLibraryFingerprint(roots, &after, &result.write_reason);
      });
      if (stable && after == before) {
        result.written = timed(result.write_ms, [&] {
          return SaveDlcCache(path, MakeDlcLibraryCache(roots, content_type, after, result.library),
                              &result.write_reason);
        });
      } else if (stable) result.write_reason = "library changed during scan";
    }
  } else result.write_reason = enabled ? "no complete fingerprint" : "disabled";
  return result;
}
}  // namespace rb_blitz::fs
