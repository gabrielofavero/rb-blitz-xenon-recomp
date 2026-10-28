// SPDX-License-Identifier: GPL-2.0-only
// Host-only comparison using the production cache path. "first_cache_write"
// means an absent application cache, not a cold OS filesystem cache.
#include "fs/dlc_cached_library.h"
#include <charconv>
#include <cstdio>

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    std::fprintf(stderr, "usage: dlc_cache_benchmark <library> <NEW-output-directory> [scan-threads]\n");
    return 2;
  }
  uint32_t scan_threads = 1;
  if (argc == 4) {
    const std::string_view value(argv[3]);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), scan_threads);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
      std::fprintf(stderr, "scan-threads must be a nonnegative integer.\n");
      return 2;
    }
  }
  namespace fs = std::filesystem;
  using namespace rb_blitz::fs;
  const auto root = fs::absolute(argv[1]);
  const auto output = fs::absolute(argv[2]);
  if (!fs::is_directory(root) || fs::exists(output)) {
    std::fprintf(stderr, "Library must exist; output must be a new disposable directory.\n");
    return 2;
  }
  std::printf("trial,condition,threads,hit,packages,fingerprint_ms,load_ms,scan_ms,verify_ms,write_ms,total_ms\n");
  for (int trial = 1; trial <= 3; ++trial) {
    const auto path = output / std::to_string(trial) / "library.cache";
    const auto run = [&](const char* condition, bool enabled, bool refresh) {
      const auto begin = std::chrono::steady_clock::now();
      const auto r = LoadOrScanDlcLibrary(enabled, refresh, {root}, 0, scan_threads, path);
      const auto ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - begin).count();
      std::printf("%d,%s,%u,%d,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", trial, condition,
                  scan_threads, r.hit, r.library.items.size(), r.fingerprint_ms, r.load_ms, r.scan_ms,
                  r.verify_ms, r.write_ms, ms);
      if ((std::string_view(condition) == "cache_hit") != r.hit ||
          (enabled && !r.hit && !r.written)) {
        std::fprintf(stderr, "Unexpected cache result: %s; write: %s\n",
                      r.reason.c_str(), r.write_reason.c_str());
        return false;
      }
      return true;
    };
    // Reverse the Off leg's position in the middle trial to expose ordering.
    if (trial != 2 && !run("no_cache", false, false)) return 1;
    if (!run("first_cache_write", true, false) || !run("cache_hit", true, false) ||
        !run("forced_refresh", true, true)) return 1;
    if (trial == 2 && !run("no_cache", false, false)) return 1;
  }
}
