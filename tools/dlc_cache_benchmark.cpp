// SPDX-License-Identifier: GPL-2.0-only
// Host-only comparison using the production cache path. This does not measure
// guest discovery or claim a cold OS filesystem cache.
#include "fs/dlc_cached_library.h"
#include <cstdio>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: dlc_cache_benchmark <library> <NEW-output-directory>\n");
    return 2;
  }
  namespace fs = std::filesystem;
  using namespace rb_blitz::fs;
  const auto root = fs::absolute(argv[1]);
  const auto output = fs::absolute(argv[2]);
  if (!fs::is_directory(root) || fs::exists(output)) {
    std::fprintf(stderr, "Library must exist; output must be a new disposable directory.\n");
    return 2;
  }
  std::printf("trial,condition,hit,packages,fingerprint_ms,load_ms,scan_ms,verify_ms,write_ms,total_ms\n");
  for (int trial = 1; trial <= 3; ++trial) {
    const auto path = output / std::to_string(trial) / "library.cache";
    const auto run = [&](const char* condition, bool enabled, bool refresh) {
      const auto begin = std::chrono::steady_clock::now();
      const auto r = LoadOrScanDlcLibrary(enabled, refresh, {root}, 0, 1, path);
      const auto ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - begin).count();
      std::printf("%d,%s,%d,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", trial, condition,
                  r.hit, r.library.items.size(), r.fingerprint_ms, r.load_ms, r.scan_ms,
                  r.verify_ms, r.write_ms, ms);
      if ((std::string_view(condition) == "warm") != r.hit ||
          (enabled && !r.hit && !r.written)) {
        std::fprintf(stderr, "Unexpected cache result: %s; write: %s\n",
                      r.reason.c_str(), r.write_reason.c_str());
        return false;
      }
      return true;
    };
    // Reverse the Off leg's position in the middle trial to expose ordering.
    if (trial != 2 && !run("off", false, false)) return 1;
    if (!run("cold", true, false) || !run("warm", true, false) ||
        !run("refresh", true, true)) return 1;
    if (trial == 2 && !run("off", false, false)) return 1;
  }
}
