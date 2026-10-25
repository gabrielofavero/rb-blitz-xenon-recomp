// SPDX-License-Identifier: GPL-2.0-only
// Post-A host service. The authored record owns every visible widget and the
// continuation; this file only answers its queries.
// See docs/engine/shader-cache.md (part 6, post-A preparation).
#include "graphics/preparation_loading.h"
#include "graphics/preparation_assets.h"
#include "graphics/preparation_archive.h"
#include "generated/preparation_catalogue.h"
#include "settings/ultimate_adapter.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/filesystem/file.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xmemory.h>
#include <xxhash.h>

namespace rb_blitz::graphics {
namespace {
rex::Runtime* runtime = nullptr; // installed before guest launch, retired at close
std::jthread extraction;
std::mutex extraction_mutex;
std::atomic<bool> closing{false};
std::atomic<int> extraction_state{0}; // 0 idle, 1 working, 2 complete, 3 failed
bool seeded = false; // UI owner thread
bool content_ready = false, pending = false, released = false, failed = false;
bool ignored = false; // explicit fallback for this process, never graphics readiness
uint64_t retries = 0;
uint64_t attempt = 0, generation = 0, configuration = 0;
rex::graphics::GraphicsPreparationProgress progress;

Bytes Read(const std::string& path, uint64_t offset = 0, size_t size = 0) {
  auto* entry = runtime->file_system()->ResolvePath("game:\\" + path);
  if (!entry || offset > entry->size()) throw std::runtime_error("missing installed shader archive");
  if (!size) size = size_t(entry->size());
  if (size > 32*1024*1024 || size > entry->size()-offset) throw std::runtime_error("invalid shader archive extent");
  rex::filesystem::File* file = nullptr;
  if (entry->Open(rex::filesystem::FileAccess::kGenericRead,&file) != 0 || !file)
    throw std::runtime_error("cannot open shader archive");
  Bytes bytes(size);
  size_t got = 0;
  const auto status = file->ReadSync(bytes,offset,&got);
  file->Destroy();
  if (status || got != size) throw std::runtime_error("cannot read shader archive");
  return bytes;
}
void StartExtraction() {
  std::lock_guard lock(extraction_mutex);
  if (closing.load()) return;
  if (seeded || extraction_state.load() != 0) return;
  extraction_state.store(1);
  extraction = std::jthread([](std::stop_token stop) {
    try {
      using settings::ultimate_adapter::ArkIndex;
      std::vector<ArkIndex> indexes;
      for (const auto* path : {"gen/patch_xbox.hdr", "gen/main_xbox.hdr"}) {
        if (!runtime->file_system()->ResolvePath(std::string("game:\\")+path)) continue;
        auto bytes = Read(path);
        ArkIndex index;
        std::string why;
        if (!settings::ultimate_adapter::ReadArkIndex(bytes.data(),bytes.size(),&index,&why))
          throw std::runtime_error(why);
        indexes.push_back(std::move(index));
      }
      AssetReader reader = [&](const std::string& path) -> Bytes {
        for (const auto& index : indexes) {
          auto* entry = FindPreparationAsset(index,path);
          if (!entry) continue;
          if (entry->compressed()) throw std::runtime_error("compressed shader asset is unsupported");
          uint64_t offset = entry->offset;
          for (size_t i = 0; i < index.parts.size(); ++i) {
            if (offset >= index.part_sizes[i]) { offset -= index.part_sizes[i]; continue; }
            if (entry->size > index.part_sizes[i]-offset) break;
            return Read(index.parts[i],offset,entry->size);
          }
          throw std::runtime_error("shader crosses archive boundary");
        }
        throw std::runtime_error("installed shader asset is missing: " + path);
      };
      // The supported executable's image is already validated and mapped by the
      // project's boot fingerprint gate. Source recipes use offsets from 0x82000000.
      const auto* image = runtime->memory()->virtual_membase()+0x82000000u;
      SeedPreparation(kPreparationCatalogue,runtime->cache_root(),reader,
                      std::span<const uint8_t>(image,0x900000),[&] { return stop.stop_requested(); });
      extraction_state.store(stop.stop_requested()?0:2);
    } catch (const std::exception& e) {
      REXLOG_ERROR("graphics_loading: local catalogue preparation: {}",e.what());
      extraction_state.store(stop.stop_requested()?0:3);
    }
  });
}
void Cancel() {
  pending = false;
  content_ready = false;
  {
    std::lock_guard lock(extraction_mutex);
    if (extraction.joinable()) { extraction.request_stop(); extraction.join(); }
  }
  if (extraction_state.load() == 2) seeded = true;
  if (!seeded) extraction_state.store(0);
  if (runtime && generation && !progress.finished) runtime->graphics_system()->CancelShaderStoragePreparation();
  generation = 0;
}
}
void ConfigureLoading(rex::Runtime* value) {
  runtime = value;
  // The device, translator and render path supply their own identities. These
  // effective flags are fixed for this boot; later invalidation belongs to P7.
  std::string identity = kPreparationCatalogue;
  for (const auto* name : {"gpu_backend","render_target_path_d3d12","render_target_path_vulkan",
                           "draw_resolution_scale_x","draw_resolution_scale_y","native_pipeline_cache"})
    identity += std::string(name)+"="+rex::cvar::GetFlagByName(name)+";";
  configuration = XXH3_64bits(identity.data(),identity.size());
  REXLOG_INFO("graphics_loading: post-A native loading gate configured epoch={:016X}",configuration);
}
void BeginLoadingAttempt() {
  // Reuse only a live ready generation, never a disk-file existence test.
  if (runtime && generation) progress = runtime->graphics_system()->QueryShaderStoragePreparation();
  const bool ready = generation && progress.generation == generation && progress.ready();
  if (!ready && !ignored) Cancel();
  content_ready = pending = released = failed = false;
  ++attempt;
  retries = 0;
  REXLOG_INFO("graphics_loading: attempt {} started (live generation ready={}, ignored={})",attempt,ready,ignored);
}
void ContentLoadingReady() {
  if (content_ready) return;
  content_ready = true;
  REXLOG_INFO("graphics_loading: attempt {} passed the native content-ready boundary",attempt);
  PollLoading();
}
void ObserveContentLoading(uint8_t* base, uint32_t panel) {
  if (!runtime || closing.load()) return;
  const auto u32 = [&](uint32_t p) { return rex::memory::load_and_swap<uint32_t>(base+p); };
  if (u32(panel+56) != 29) return;
  const auto content = u32(0x8280A64C);
  if (content && u32(content+44) == 1) ContentLoadingReady();
}
void PollLoading() {
  if (closing.load() || !runtime || !content_ready || failed || ignored) return;
  auto* gpu = runtime->graphics_system();
  if (!gpu) { failed = true; return; }
  if (!generation) {
    // A cancelled generation must retire before opening its storage again.
    progress = gpu->QueryShaderStoragePreparation();
    if (progress.active) return;
    StartExtraction();
    const int state = extraction_state.load();
    if (state == 3) { failed = true; return; }
    if (state != 2 && !seeded) return;
    seeded = true;
    if (!gpu->BeginShaderStoragePreparation(runtime->cache_root(),0x5841122D,configuration)) {
      REXLOG_ERROR("graphics_loading: renderer refused preparation; the menu remains gated");
      failed = true;
      return;
    }
    progress = gpu->QueryShaderStoragePreparation();
    generation = progress.generation;
    REXLOG_INFO("graphics_loading: attempt {} owns generation {}",attempt,generation);
  } else progress = gpu->QueryShaderStoragePreparation();
  if (progress.generation != generation || progress.key.configuration != configuration ||
      (progress.finished && !progress.ready())) {
    if (!failed) REXLOG_ERROR("graphics_loading: generation {} {} required={} succeeded={} failed={} missing={}",
        generation,rex::graphics::GraphicsPreparationPhaseName(progress.phase),progress.entries.required,
        progress.entries.succeeded,progress.entries.failed,progress.entries.missing);
    failed = true;
  }
}
bool GraphicsLoadingPending() { return pending; }
bool LoadingQuery(std::string_view path, uint32_t* answer) {
  constexpr std::string_view prefix = "rbbz-graphics/";
  if (!path.starts_with(prefix)) return false;
  *answer = 0;
  path.remove_prefix(prefix.size());
  if (path == "cancel") {
    if (!released) Cancel();
  } else if (path == "retry") {
    if (pending && content_ready && failed && !released) {
      // Stay in this connect attempt: its completed settings/DLC work survives.
      // Only the failed graphics generation is retired and restarted.
      {
        std::lock_guard lock(extraction_mutex);
        if (extraction.joinable()) extraction.join();
      }
      if (extraction_state.load() == 2) seeded = true;
      if (!seeded) extraction_state.store(0);
      if (runtime && generation && !progress.finished)
        runtime->graphics_system()->CancelShaderStoragePreparation();
      generation = 0;
      failed = false;
      progress = {};
      ++retries;
      REXLOG_INFO("graphics_loading: attempt {} graphics retry {}; retaining completed DLC/settings",attempt,retries);
      PollLoading();
      *answer = 1;
    }
  } else if (path == "ignore") {
    if (pending && content_ready && failed && !released) {
      // A failed generation is terminal. If local extraction failed before the
      // renderer opened storage, open the existing learned records asynchronously
      // so ordinary demand-created pipelines continue to be captured.
      auto* gpu = runtime ? runtime->graphics_system() : nullptr;
      if (gpu && !generation && !gpu->QueryShaderStoragePreparation().active) {
        const bool started = gpu->BeginShaderStoragePreparation(runtime->cache_root(),0x5841122D,configuration);
        REXLOG_INFO("graphics_loading: legacy shader storage requested after extraction failure (started={})",started);
      }
      ignored = true;
      released = true;
      pending = false;
      *answer = 1;
      REXLOG_WARN("graphics_loading: attempt {} explicitly ignored; normal on-demand compilation/caching continues, readiness not certified",attempt);
    }
  } else if (path == "request") {
    if (!released) pending = true;
    PollLoading();
  } else {
    PollLoading();
    if (path == "ready") {
      if (pending && content_ready && (ignored || (!failed && generation && progress.generation == generation && progress.ready()))) {
        released = true; pending = false; *answer = 1;
        REXLOG_INFO("graphics_loading: attempt {} released generation {} exactly once ({} ms, {} pipelines, ignored={})",
                    attempt,generation,progress.elapsed_milliseconds,progress.entries.succeeded,ignored);
      }
    } else if (path == "failed") *answer = failed;
    else if (path == "checking") *answer = !generation || progress.phase == rex::graphics::GraphicsPreparationPhase::kImporting;
    else if (path == "current") *answer = uint32_t(progress.completed_entries());
    else if (path == "total") *answer = generation && progress.has_catalogue ? uint32_t(progress.required_entries()) : uint32_t(-1);
  }
  return true;
}
void CloseLoading() {
  // Window close is on the host UI thread while guest polling may still run.
  // Do not mutate guest-owned attempt fields or retire its runtime pointer here.
  closing.store(true);
  std::lock_guard lock(extraction_mutex);
  if (extraction.joinable()) { extraction.request_stop(); extraction.join(); }
  if (runtime && runtime->graphics_system()) runtime->graphics_system()->CancelShaderStoragePreparation();
}
}
