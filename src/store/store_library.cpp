// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/store/store_library.h.

#include "store/store_library.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/logging.h>

#include "fs/dlc_song_metadata.h"
#include "hooks/dlc.h"
#include "store/store_serve.h"

namespace rb_blitz::store {
namespace {

using Clock = std::chrono::steady_clock;

void Run();  // the worker loop, defined below its build step

std::mutex g_mutex;
std::condition_variable g_wake;
std::shared_ptr<const LoadedSongIndex> g_index;  // null until the first build publishes
std::uint64_t g_generation = 0;
bool g_building = false;
bool g_pending = false;
bool g_quit = false;
bool g_installed = false;
bool g_stale = true;  // no build published, or the library changed since the last one
std::string g_reason;  // why the last build ran

class Worker {
 public:
  Worker() : thread_([this] { Run(); }) {}

  ~Worker() {
    {
      const std::lock_guard<std::mutex> lock(g_mutex);
      g_quit = true;
    }
    g_wake.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::thread thread_;
};

Worker& TheWorker() {
  static Worker worker;
  return worker;
}

// One pass over every package the boot enumerated. Runs on the worker thread only.
std::shared_ptr<LoadedSongIndex> Build() {
  auto index = std::make_shared<LoadedSongIndex>();
  // A copy: `LibraryPackagePaths()` is the boot's own list and must not be read while the
  // refresh row replaces it.
  const std::vector<std::filesystem::path> packages = rb_blitz::dlc::LibraryPackagePaths();
  const Clock::time_point started = Clock::now();
  std::size_t mounted = 0;
  std::size_t failed = 0;
  for (std::size_t i = 0; i < packages.size(); ++i) {
    // A fresh mount path per package: the device is destroyed at the end of the iteration, so
    // the path may be reused, but a unique one keeps a diagnostic readable.
    const std::string mount_path = fmt::format("\\Device\\BlitzStoreLibrary\\{}\\", i);
    std::error_code ec;
    std::unique_ptr<rex::filesystem::Device> device =
        rb_blitz::fs::MountPackage(mount_path, packages[i], ec);
    if (device == nullptr) {
      ++failed;
      continue;
    }
    ++mounted;
    for (const auto& song : rb_blitz::fs::ReadDeviceSongMetadata(*device)) {
      index->Add(song.artist, song.title);
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
  const std::string reason = [&] {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_reason;
  }();
  REXLOG_INFO("store_library: the loaded DLC library declares {} song(s) across {} package(s) "
              "({} could not be mounted) in {} ms ({})",
              index->size(), mounted, failed, elapsed.count(), reason);
  return index;
}

void Run() {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_wake.wait(lock, [] { return g_quit || g_pending; });
      if (g_quit) {
        return;
      }
      g_pending = false;
      g_building = true;
    }
    std::shared_ptr<LoadedSongIndex> index = Build();
    {
      const std::lock_guard<std::mutex> lock(g_mutex);
      g_index = std::move(index);
      g_stale = false;
      ++g_generation;
      g_building = false;
    }
  }
}

}  // namespace

void InstallStoreLibrary() {
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_installed) {
      return;
    }
    g_installed = true;
  }
  // The serving policy asks this once per offer when a page is written (store_serve.h). Reading the
  // published snapshot is a mutex plus a set lookup - cheap enough for the guest thread, and it
  // never scans a package from there.
  SetDownloadedPredicate(
      [](const Offer& offer) { return IsDownloadedSong(offer.artist, offer.name); });
  TheWorker();  // start the thread now, so the first request only has to wake it
  REXLOG_INFO("store_library: the Downloaded mark is built from the loaded DLC packages' songs.dta");
}

void RequestLibraryRefresh(std::string_view why) {
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_installed || g_quit || g_building || g_pending) {
      return;  // single flight; a build in progress already covers this request
    }
    if (g_index && !g_stale) {
      return;  // the published index still describes the library
    }
    g_pending = true;
    g_reason = std::string(why);
  }
  g_wake.notify_one();
  REXLOG_INFO("store_library: indexing the loaded library ({})", why);
}

void InvalidateLibrary() {
  std::uint64_t dropped = 0;
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_stale = true;
    if (g_index) {
      dropped = g_index->size();
      g_index.reset();
    }
  }
  if (dropped != 0) {
    REXLOG_INFO("store_library: the published index ({} song(s)) was dropped; the store's next "
                "page rebuilds it",
                dropped);
  }
}

bool LibraryReady() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_index != nullptr;
}

std::size_t LoadedSongCount() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_index ? g_index->size() : 0;
}

std::uint64_t LibraryGeneration() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_generation;
}

bool IsDownloadedSong(std::string_view artist, std::string_view title) {
  std::shared_ptr<const LoadedSongIndex> index;
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    index = g_index;
  }
  return index != nullptr && index->Contains(artist, title);
}

}  // namespace rb_blitz::store
