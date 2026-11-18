// SPDX-License-Identifier: GPL-2.0-only
// Post-A host service. The authored record owns every visible widget and the
// continuation; this file only answers its queries.
// See docs/engine/shader-cache.md (part 6, post-A preparation; part 7,
// invalidation, new content and interrupted preparation).
#include "graphics/preparation_loading.h"
#include "graphics/preparation_assets.h"
#include "graphics/preparation_archive.h"
#include "graphics/preparation_settings.h"
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
#include "settings/runtime_settings.h"
#include "settings/runtime_values.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/title_paths.h>
#include <rex/path_settings.h>
#include <rex/gpu/graphics_preparation_service.h>
#include <rex/gpu/graphics_system.h>
#include <rex/memory/endian.h>
#include <rex/string/text.h>
#include <rex/logging/log_category.h>
#include <rex/filesystem/file_system.h>
#include <rex/filesystem/file.h>
#include <xxhash.h>

namespace rb_blitz::graphics {
namespace {
rex::kernel::TitleRuntime* runtime = nullptr; // installed before guest launch, retired at close
std::filesystem::path cache_root;
uint32_t shader_storage_version = kD3D12ShaderStorageVersion;

rex::gpu::IGraphicsPreparation* PreparationService() {
  return REX_INTERFACE(rex::gpu::IGraphicsPreparation);
}

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
rex::gpu::GraphicsPreparationProgress progress;

Bytes Read(const std::string& path, uint64_t offset = 0, size_t size = 0) {
  rex::filesystem::OpenOptions options;
  options.require_file = true;
  auto opened = runtime->GetFileSystem().Open("game:\\" + path, options);
  if (!opened) throw std::runtime_error("cannot open installed shader archive");
  auto& file = *opened->file;
  const auto length = file.GetInfo().size;
  if (offset > length) throw std::runtime_error("invalid shader archive offset");
  if (!size) size = size_t(length - offset);
  if (size > 32*1024*1024 || size > length-offset)
    throw std::runtime_error("invalid shader archive extent");
  Bytes bytes(size);
  const auto got = file.Read(bytes, offset);
  if (!got || *got != size) throw std::runtime_error("cannot read shader archive");
  return bytes;
}

// The canonical registry includes the concrete backend's translator and feature
// settings. Length-prefixed fields keep arbitrary string values unambiguous.
uint64_t PipelineSettingFingerprint() {
  const auto* graphics = REX_INTERFACE(rex::gpu::IGraphicsSystem);
  const auto identity = PreparationSettingsIdentity(graphics ? graphics->GetDriverName() : "none");
  return XXH3_64bits(identity.data(), identity.size());
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
  put(PipelineSettingFingerprint());
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
        if (!runtime->GetFileSystem().Stat(std::string("game:\\")+path)) continue;
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
      const auto root = runtime->GetTitlePaths().game_root.Get();
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
      const auto* image = runtime->GetMembase().TranslateVirtual<const uint8_t*>(0x82000000u);
      std::string persistence_error;
      SeedReport report = SeedPreparation(kPreparationCatalogue,cache_root,reader,
                                          std::span<const uint8_t>(image,0x900000),
                                          [&] { return stop.stop_requested(); },computed,
                                          content_epoch.load(),&persistence_error,shader_storage_version);
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
    if (auto* gpu = PreparationService()) progress = gpu->QueryShaderStoragePreparation();
    RecordAttempt(true);
    REXLOG_INFO("graphics_loading: attempt {} cancelled; generation {} retires and the next attempt prepares again",attempt,generation);
    if (auto* gpu = PreparationService()) gpu->RequestCancelShaderStoragePreparation();
  }
  generation = 0;
}
void RecordAttempt(bool cancelled) {
  if (recorded || !runtime || !generation) return;
  recorded = true;
  PreparationLedger ledger;
  ledger.Load(cache_root);
  ledger.content_epoch = content_epoch.load();
  ledger.last_attempt.attempt = attempt;
  ledger.last_attempt.configuration = progress.key.configuration;
  ledger.last_attempt.phase =
      int32_t(cancelled ? rex::gpu::GraphicsPreparationPhase::kCancelled : progress.phase);
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
  if (!ledger.Save(cache_root,&why))
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

void ConfigureLoading(rex::kernel::TitleRuntime* value) {
  // The stopping hook joined extraction before the previous mapping was freed.
  // Reset owner-thread state before another title can start guest polling.
  CloseLoading();
  runtime = value;
  closing.store(false);
  extraction_state.store(0);
  content_epoch.store(0);
  seeded = content_ready = pending = released = failed = recorded = ignored = false;
  attempt = generation = retries = 0;
  progress = {};
  failure_reason = GraphicsFailure::kNone;
  {
    std::lock_guard lock(result_mutex);
    inputs = {};
    inputs_known = false;
    seed_report = {};
    persisted = true;
  }
  cache_root = REX_PATHS().cache_root.Get();
  const auto* graphics = REX_INTERFACE(rex::gpu::IGraphicsSystem);
  shader_storage_version = graphics && graphics->GetDriverName() == "vulkan"
      ? kVulkanShaderStorageVersion : kD3D12ShaderStorageVersion;
  // Log the initial active inputs. Generation comparisons read them again so
  // live graphics edits retire readiness while pending restart edits stay inert.
  settings_fingerprint = PipelineSettingFingerprint();
  cache_writable = ProbeWritable(cache_root);
  if (!cache_writable)
    REXLOG_WARN("graphics_loading: the cache root {} is not writable; graphics can still be prepared for this session, but a later launch will have to prepare again",rex::string::PathToUTF8(cache_root));
  REXLOG_INFO("graphics_loading: post-A native loading gate configured settings={:016X} ({} pipeline-affecting setting(s), cache writable={})",
              settings_fingerprint,REX_SETTINGS().Enumerate("gpu").size(),cache_writable);
}
void BeginLoadingAttempt() {
  rb_blitz::shell_music::StartQueuedPool();
  // Reuse only a live ready generation that was prepared for this exact
  // configuration, never a disk-file existence test.
  if (runtime && generation) {
    if (auto* gpu = PreparationService()) progress = gpu->QueryShaderStoragePreparation();
    else progress = {};
  }
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
  auto* gpu = PreparationService();
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
    if (!gpu->BeginShaderStoragePreparation(cache_root,0x5841122D,configuration)) {
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
    gpu->RequestCancelShaderStoragePreparation();
    generation = 0;
    return;
  }
  if (progress.generation != generation || (progress.finished && !progress.ready())) {
    if (!failed) {
      REXLOG_ERROR("graphics_loading: generation {} {} required={} succeeded={} failed={} missing={}",
          generation,rex::gpu::GraphicsPreparationPhaseName(progress.phase),progress.entries.required,
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
  if (closing.load()) return true;
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
        if (auto* gpu = PreparationService()) gpu->RequestCancelShaderStoragePreparation();
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
  } else if (path == "skip") {
    if (pending && content_ready && !released) {
      // Skip is a forward continuation, not the connect panel's native Back.
      // Retire optional preparation without joining its worker on the guest
      // thread; normal content/save loading has already completed here.
      {
        std::lock_guard lock(extraction_mutex);
        if (extraction.joinable()) extraction.request_stop();
      }
      ignored = true;
      released = true;
      pending = false;
      auto* gpu = runtime ? PreparationService() : nullptr;
      if (gpu && generation) {
        progress = gpu->QueryShaderStoragePreparation();
        RecordAttempt(true);
        gpu->RequestCancelShaderStoragePreparation();
      } else if (gpu) {
        // No preparation generation opened storage yet. Preserve ordinary
        // shader learning/caching without certifying graphics readiness.
        const bool started = gpu->BeginShaderStoragePreparation(cache_root, 0x5841122D, CurrentConfiguration());
        REXLOG_INFO("graphics_loading: shader storage requested after skip (started={})", started);
      }
      *answer = 1;
      REXLOG_WARN("graphics_loading: attempt {} skipped; continuing to the main menu with normal demand compilation/caching, readiness not certified", attempt);
    }
  } else if (path == "ignore") {
    if (pending && content_ready && failed && !released) {
      // A failed generation is terminal. If local extraction failed before the
      // renderer opened storage, open the existing learned records asynchronously
      // so ordinary demand-created pipelines continue to be captured.
      auto* gpu = runtime ? PreparationService() : nullptr;
      if (gpu && !generation && !gpu->QueryShaderStoragePreparation().active) {
        const bool started = gpu->BeginShaderStoragePreparation(cache_root,0x5841122D,CurrentConfiguration());
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
    else if (path == "checking") *answer = !generation || progress.phase == rex::gpu::GraphicsPreparationPhase::kImporting;
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
  if (runtime && PreparationService()) PreparationService()->RequestCancelShaderStoragePreparation();
}
void NoteContentChanged(std::string_view why) {
  const uint64_t epoch = content_epoch.fetch_add(1) + 1;
  REXLOG_INFO("graphics_loading: content changed ({}); readiness retired at content epoch {}, the next preparation reuses the layers that are still valid",why,epoch);
}
}  // namespace rb_blitz::graphics
