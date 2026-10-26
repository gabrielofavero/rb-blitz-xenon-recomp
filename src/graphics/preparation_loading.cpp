// SPDX-License-Identifier: GPL-2.0-only
// Post-A host service. The authored record owns every visible widget and the
// continuation; this file only answers its queries.
// See docs/engine/shader-cache.md (part 6, post-A preparation; part 7,
// invalidation, new content and interrupted preparation).
#include "graphics/preparation_loading.h"
#include "graphics/preparation_assets.h"
#include "graphics/preparation_archive.h"
#include "generated/fingerprint_expected.h"
#include "generated/preparation_catalogue.h"
#include "hooks/ultimate.h"
#include "hooks/shell_music.h"
#include "settings/ultimate_adapter.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/filesystem.h>
#include <rex/filesystem/file.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xmemory.h>
#include <xxhash.h>

namespace rb_blitz::graphics {
namespace {
rex::Runtime* runtime = nullptr; // installed before guest launch, retired at close
std::jthread extraction;
std::mutex extraction_mutex; // guards the join, never held by the worker while ending
std::mutex result_mutex;     // guards inputs/seed_report between the worker and the UI
std::atomic<bool> closing{false};
std::atomic<int> extraction_state{0}; // 0 idle, 1 working, 2 complete, 3 failed
std::atomic<uint64_t> content_epoch{0}; // bumped by DLC/store refreshes (P7)
bool seeded = false; // UI owner thread
bool content_ready = false, pending = false, released = false, failed = false, recorded = false;
bool ignored = false; // explicit fallback for this process, never graphics readiness
uint64_t retries = 0;
uint64_t attempt = 0, generation = 0;
PreparationInputs inputs;   // computed once the installed content is known
bool inputs_known = false;
SeedReport seed_report;
bool persisted = true;      // the last seed wrote its ledger
bool cache_writable = true; // pre-flight probe of the cache root
GraphicsFailure failure_reason = GraphicsFailure::kNone;
uint64_t settings_fingerprint = 0;
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

constexpr const char* kSettingNames[] = {
    // Which backend and which of its rendering paths is built: the declared
    // recipe stores are per path, and a path selects different shaders.
    "gpu_backend",
    "render_target_path_d3d12",
    "render_target_path_vulkan",
    // Pipeline descriptions embed resolution scaling, MSAA and render-target
    // formats, so each of these changes the normalized recipes.
    "draw_resolution_scale_x",
    "draw_resolution_scale_y",
    "draw_resolution_scaled_texture_offsets",
    "native_2x_msaa",
    "gamma_render_target_as_unorm16",
    "snorm16_render_target_full_range",
    "depth_float24_convert_in_pixel_shader",
    "gpu_allow_invalid_fetch_constants",
    "d3d12_bindless",
    "vulkan_dynamic_rendering",
    // Readback/dump/resolve modes create different helper pipelines.
    "readback_resolve",
    "d3d12_readback_resolve",
    "vulkan_readback_resolve",
    "readback_memexport",
    "d3d12_readback_memexport",
    "vulkan_readback_memexport",
    "readback_resolve_half_pixel_offset",
    "resolve_resolution_scale_fill_half_pixel_offset",
    "present_render_pass_clear",
    // Swap-chain effects add compute pipelines of their own.
    "swap_post_effect",
    // These change the translated shader itself.
    "vulkan_force_convert_quad_lists_to_triangle_lists",
    "vulkan_force_expand_point_sprites_in_vs",
    // Included so the ledger's record explains which native layer was in play;
    // it changes reuse, not the pipeline descriptions.
    "native_pipeline_cache",
};

// Everything a prepared result depends on outside the installed content. Bias
// towards inclusion: an entry that did not need to be here costs one extra
// preparation, an omitted one would let stale readiness survive a change.
uint64_t PipelineSettingFingerprint() {
  std::string fingerprint;
  for (const auto* name : kSettingNames) {
    fingerprint.append(name);
    fingerprint.push_back('=');
    fingerprint.append(rex::cvar::GetFlagByName(name));
    fingerprint.push_back(';');
  }
  return XXH3_64bits(fingerprint.data(), fingerprint.size());
}

// The boot gate (rb_blitz_app.h) already hashed game/default.xex against this
// value before the guest started, so the executable's identity is a build
// constant here rather than a second nine-megabyte read.
uint64_t ExecutableIdentity() {
  const auto* text = rb_blitz::fingerprint::vanilla::kEntrypointSha256;
  return XXH3_64bits(text, std::strlen(text));
}

// One epoch for the whole requirement set. A change to any input above, to the
// settings list, or to the content epoch yields a different value, which is what
// makes an older generation's result unusable instead of silently trusted.
uint64_t CurrentConfiguration() {
  PreparationInputs copy;
  {
    std::lock_guard lock(result_mutex);
    copy = inputs;
  }
  std::string identity(kPreparationCatalogue);
  const auto put = [&identity](uint64_t value) {
    identity.append(reinterpret_cast<const char*>(&value), sizeof(value));
  };
  put(copy.catalogue);
  put(copy.content);
  put(copy.executable);
  put(copy.d3d12_version);
  put(copy.vulkan_version);
  put(settings_fingerprint);
  put(content_epoch.load());
  return XXH3_64bits(identity.data(), identity.size());
}

// Persisting is not the same fact as preparing. A read-only cache root must be
// reported as such (it needs different advice) and must not be confused with a
// GPU failure.
bool ProbeWritable(const std::filesystem::path& root) {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  const auto probe = root / ".preparation-probe";
  {
    std::ofstream file(probe, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.put('\0');
    if (!file) return false;
  }
  std::filesystem::remove(probe, error);
  return true;
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
      // The content identity decides whether a layer built by an earlier boot may
      // be reused, so it must be stable across boots of one installation. Hashing
      // the mounted ARK headers was not (P8, 2026-10-25: three boots of the same
      // install produced three different values, so nothing was ever reused), so
      // it is derived from the provider state this boot already computed plus the
      // installed headers' sizes. The bytes themselves are still authoritative:
      // every extracted shader and recipe is verified by its own hash.
      const auto& provider = rb_blitz::ultimate::Provider();
      const std::string mode = provider.active_target ? "ultimate" : "retail";
      std::string content_identity = std::string("5841122D:") + mode;
      const auto append_file = [&content_identity](const std::filesystem::path& file) {
        std::error_code error;
        const auto size = std::filesystem::file_size(file, error);
        content_identity.push_back(':');
        content_identity.append(file.filename().string());
        content_identity.push_back('=');
        content_identity.append(error ? std::string("absent") : std::to_string(size));
      };
      const auto root = runtime->game_data_root();
      append_file(root / "gen" / "main_xbox.hdr");
      append_file(root / (mode == "ultimate" ? "ultimate/gen/patch_xbox.hdr"
                                             : "gen/patch_xbox.hdr"));
      content_identity.append(":ark=").append(std::to_string(provider.installation.ark_version));
      PreparationInputs computed;
      computed.catalogue = XXH3_64bits(kPreparationCatalogue, std::strlen(kPreparationCatalogue));
      computed.content = XXH3_64bits(content_identity.data(), content_identity.size());
      computed.executable = ExecutableIdentity();
      computed.d3d12_version = kD3D12RecipeStorageVersion;
      computed.vulkan_version = kVulkanRecipeStorageVersion;
      REXLOG_INFO("graphics_loading: content identity mode={} ({}) -> {:016X}",mode,
                  content_identity,computed.content);
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
      std::string persistence_error;
      SeedReport report = SeedPreparation(kPreparationCatalogue,runtime->cache_root(),reader,
                                          std::span<const uint8_t>(image,0x900000),
                                          [&] { return stop.stop_requested(); },computed,
                                          content_epoch.load(),&persistence_error);
      {
        std::lock_guard result_lock(result_mutex);
        inputs = computed;
        inputs_known = true;
        seed_report = report;
        persisted = report.persisted;
      }
      if (!report.persisted)
        REXLOG_WARN("graphics_loading: prepared graphics but could not save the reusable result ({}); a later launch will prepare again",persistence_error);
      REXLOG_INFO("graphics_loading: layers extraction={} recipes={}/{}/{}/{} repaired={} shaders={} records={}/{}/{}/{}",
                  report.extraction_reused?"reused":"rebuilt",
                  report.recipes_reused[0]?"reused":"rebuilt",report.recipes_reused[1]?"reused":"rebuilt",
                  report.recipes_reused[2]?"reused":"rebuilt",report.recipes_reused[3]?"reused":"rebuilt",
                  report.repaired_layers,report.shaders,
                  report.records[0],report.records[1],report.records[2],report.records[3]);
      extraction_state.store(stop.stop_requested()?0:2);
    } catch (const std::exception& e) {
      REXLOG_ERROR("graphics_loading: local catalogue preparation: {}",e.what());
      extraction_state.store(stop.stop_requested()?0:3);
    }
  });
}
void RecordAttempt(bool cancelled);
void Cancel() {
  pending = false;
  content_ready = false;
  {
    std::lock_guard lock(extraction_mutex);
    if (extraction.joinable()) { extraction.request_stop(); extraction.join(); }
  }
  if (extraction_state.load() == 2) seeded = true;
  if (!seeded) extraction_state.store(0);
  if (runtime && generation && !progress.finished) {
    // Keep the cancelled attempt as evidence: P8 distinguishes "the player left"
    // from "preparation failed", and a later success must not erase that this
    // attempt was interrupted.
    if (auto* gpu = runtime->graphics_system()) progress = gpu->QueryShaderStoragePreparation();
    RecordAttempt(true);
    REXLOG_INFO("graphics_loading: attempt {} cancelled; generation {} retires and the next attempt prepares again",attempt,generation);
    runtime->graphics_system()->CancelShaderStoragePreparation();
  }
  generation = 0;
}
void RecordAttempt(bool cancelled) {
  if (recorded || !runtime || !generation) return;
  recorded = true;
  PreparationLedger ledger;
  ledger.Load(runtime->cache_root());
  ledger.content_epoch = content_epoch.load();
  ledger.last_attempt.attempt = attempt;
  ledger.last_attempt.configuration = progress.key.configuration;
  ledger.last_attempt.phase =
      int32_t(cancelled ? rex::graphics::GraphicsPreparationPhase::kCancelled : progress.phase);
  ledger.last_attempt.complete = !cancelled && progress.ready();
  ledger.last_attempt.ignored = ignored;
  ledger.last_attempt.required = progress.entries.required;
  ledger.last_attempt.succeeded = progress.entries.succeeded;
  ledger.last_attempt.failed = progress.entries.failed;
  ledger.last_attempt.missing = progress.entries.missing;
  ledger.last_attempt.cancelled = cancelled ? std::max<uint64_t>(progress.entries.cancelled, 1)
                                            : progress.entries.cancelled;
  ledger.last_attempt.elapsed_milliseconds = progress.elapsed_milliseconds;
  std::string why;
  if (!ledger.Save(runtime->cache_root(),&why))
    REXLOG_WARN("graphics_loading: attempt {} could not record its result ({}); a later launch will prepare again",attempt,why);
}
// One place sets `failed`, so one place names the reason in the log (P8 reads it
// back; the authored row reads the query form of the same value).
void FailAttempt(GraphicsFailure reason) {
  failed = true;
  failure_reason = reason;
  const char* name = reason == GraphicsFailure::kShaderData ? "shader_data"
                     : reason == GraphicsFailure::kCacheNotWritable ? "cache_not_writable"
                                                                    : "preparation";
  REXLOG_ERROR("graphics_loading: attempt {} failed ({})",attempt,name);
}
}  // namespace

void ConfigureLoading(rex::Runtime* value) {
  runtime = value;
  // A boot's settings are fixed, so their fingerprint is computed once; the
  // content half is only known after the installed indexes are read (P7).
  settings_fingerprint = PipelineSettingFingerprint();
  cache_writable = ProbeWritable(value->cache_root());
  if (!cache_writable)
    REXLOG_WARN("graphics_loading: the cache root {} is not writable; graphics can still be prepared for this session, but a later launch will have to prepare again",rex::path_to_utf8(value->cache_root()));
  REXLOG_INFO("graphics_loading: post-A native loading gate configured settings={:016X} ({} pipeline-affecting setting(s), cache writable={})",
              settings_fingerprint,std::size(kSettingNames),cache_writable);
}
void BeginLoadingAttempt() {
  rb_blitz::shell_music::StartQueuedPool();
  // Reuse only a live ready generation that was prepared for this exact
  // configuration, never a disk-file existence test.
  if (runtime && generation) progress = runtime->graphics_system()->QueryShaderStoragePreparation();
  const bool ready = generation && progress.generation == generation && progress.ready();
  if (!ready && !ignored) Cancel();
  content_ready = pending = released = failed = recorded = false;
  failure_reason = GraphicsFailure::kNone;
  ++attempt;
  retries = 0;
  REXLOG_INFO("graphics_loading: attempt {} started (live generation ready={}, ignored={}, content epoch={})",attempt,ready,ignored,content_epoch.load());
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
  rb_blitz::shell_music::PublishQueuedPool(true);
  if (closing.load() || !runtime || !content_ready || failed || ignored) return;
  auto* gpu = runtime->graphics_system();
  if (!gpu) { FailAttempt(GraphicsFailure::kPreparation); return; }
  if (!generation) {
    // A cancelled generation must retire before opening its storage again.
    progress = gpu->QueryShaderStoragePreparation();
    if (progress.active) return;
    StartExtraction();
    const int state = extraction_state.load();
    if (state == 3) {
      FailAttempt(cache_writable ? GraphicsFailure::kShaderData : GraphicsFailure::kCacheNotWritable);
      return;
    }
    if (state != 2 && !seeded) return;
    seeded = true;
    const uint64_t configuration = CurrentConfiguration();
    if (!gpu->BeginShaderStoragePreparation(runtime->cache_root(),0x5841122D,configuration)) {
      REXLOG_ERROR("graphics_loading: renderer refused preparation; the menu remains gated");
      FailAttempt(cache_writable ? GraphicsFailure::kPreparation : GraphicsFailure::kCacheNotWritable);
      return;
    }
    progress = gpu->QueryShaderStoragePreparation();
    generation = progress.generation;
    REXLOG_INFO("graphics_loading: attempt {} owns generation {} for configuration {:016X}",attempt,generation,configuration);
  } else progress = gpu->QueryShaderStoragePreparation();

  if (progress.generation == generation && progress.key.configuration != CurrentConfiguration()) {
    // The content or settings moved under this generation (a DLC/store refresh,
    // a setting committed at the gate). Retire it and prepare again with the new
    // key - the valid layers are reused, so only the affected work repeats.
    REXLOG_INFO("graphics_loading: attempt {} configuration changed mid-preparation; retiring generation {} for a fresh one",attempt,generation);
    gpu->CancelShaderStoragePreparation();
    generation = 0;
    return;
  }
  if (progress.generation != generation || (progress.finished && !progress.ready())) {
    if (!failed) {
      REXLOG_ERROR("graphics_loading: generation {} {} required={} succeeded={} failed={} missing={}",
          generation,rex::graphics::GraphicsPreparationPhaseName(progress.phase),progress.entries.required,
          progress.entries.succeeded,progress.entries.failed,progress.entries.missing);
      FailAttempt(cache_writable ? GraphicsFailure::kPreparation : GraphicsFailure::kCacheNotWritable);
      RecordAttempt(false);
    }
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
      recorded = false;
      failure_reason = GraphicsFailure::kNone;
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
        const bool started = gpu->BeginShaderStoragePreparation(runtime->cache_root(),0x5841122D,CurrentConfiguration());
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
  } else if (path == "reason") {
    *answer = uint32_t(failure_reason);
  } else {
    PollLoading();
    if (path == "ready") {
      if (pending && content_ready && (ignored || (!failed && generation && progress.generation == generation && progress.ready()))) {
        released = true; pending = false; *answer = 1;
        if (!ignored) RecordAttempt(false);
        REXLOG_INFO("graphics_loading: attempt {} released generation {} exactly once ({} ms, {} pipelines, ignored={})",
                    attempt,generation,progress.elapsed_milliseconds,progress.entries.succeeded,ignored);
      }
    } else if (path == "failed") *answer = failed;
    else if (path == "failed_data") *answer = failed && failure_reason == GraphicsFailure::kShaderData;
    else if (path == "failed_cache") *answer = failed && failure_reason == GraphicsFailure::kCacheNotWritable;
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
void NoteContentChanged(std::string_view why) {
  const uint64_t epoch = content_epoch.fetch_add(1) + 1;
  REXLOG_INFO("graphics_loading: content changed ({}); readiness retired at content epoch {}, the next preparation reuses the layers that are still valid",why,epoch);
}
}  // namespace rb_blitz::graphics
