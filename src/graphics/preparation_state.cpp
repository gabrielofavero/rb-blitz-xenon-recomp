// SPDX-License-Identifier: GPL-2.0-only
// See preparation_state.h and docs/engine/shader-cache.md (part 7).
#include "graphics/preparation_state.h"
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include "fs/atomic_file.h"

namespace rb_blitz::graphics {
namespace {
using Json = nlohmann::json;

constexpr uint64_t kMaxLedgerBytes = 256 * 1024;

Json LayerToJson(const PreparationLayer& layer) {
  return Json{{"complete", layer.complete},
              {"catalogue", layer.catalogue},
              {"content", layer.content},
              {"executable", layer.executable},
              {"d3d12_version", layer.d3d12_version},
              {"vulkan_version", layer.vulkan_version},
              {"entries", layer.entries}};
}

PreparationLayer LayerFromJson(const Json& value) {
  PreparationLayer layer;
  if (!value.is_object()) return layer;
  layer.complete = value.value("complete", false);
  layer.catalogue = value.value("catalogue", uint64_t(0));
  layer.content = value.value("content", uint64_t(0));
  layer.executable = value.value("executable", uint64_t(0));
  layer.d3d12_version = value.value("d3d12_version", uint32_t(0));
  layer.vulkan_version = value.value("vulkan_version", uint32_t(0));
  layer.entries = value.value("entries", uint64_t(0));
  return layer;
}
}  // namespace

bool IsPreparationPath(const std::string& path) {
  return PreparationPathIndex(path) < kPreparationPathCount;
}

size_t PreparationPathIndex(const std::string& path) {
  for (size_t i = 0; i < kPreparationPathCount; ++i) {
    if (path == kPreparationPaths[i]) return i;
  }
  return kPreparationPathCount;
}

std::filesystem::path PreparationLedger::PathFor(const std::filesystem::path& cache_root) {
  return cache_root / "shaders" / kFileName;
}

void PreparationLedger::Load(const std::filesystem::path& cache_root, std::string* why) {
  *this = PreparationLedger();
  const auto path = PathFor(cache_root);
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (why) *why = "no preparation state yet";
    return;
  }
  std::uintmax_t size = 0;
  size = std::filesystem::file_size(path, error);
  if (error || size > kMaxLedgerBytes) {
    // An oversized or unreadable ledger is corruption, not a covered state.
    if (why) *why = "preparation state is unreadable or oversized";
    return;
  }
  std::ifstream file(path, std::ios::binary);
  std::string text(std::istreambuf_iterator<char>(file), {});
  if (!file.good() && !file.eof()) {
    if (why) *why = "cannot read preparation state";
    return;
  }
  try {
    const Json json = Json::parse(text);
    if (!json.is_object() || json.value("schema", 0u) != kSchema) {
      if (why) *why = "unsupported preparation state schema";
      return;
    }
    content_epoch = json.value("content_epoch", uint64_t(0));
    if (json.contains("extraction")) extraction = LayerFromJson(json.at("extraction"));
    if (const auto recipes_json = json.find("recipes");
        recipes_json != json.end() && recipes_json->is_object()) {
      for (size_t i = 0; i < kPreparationPathCount; ++i) {
        const auto entry = recipes_json->find(kPreparationPaths[i]);
        if (entry != recipes_json->end()) recipes[i] = LayerFromJson(*entry);
      }
    }
    if (const auto attempt_json = json.find("last_attempt");
        attempt_json != json.end() && attempt_json->is_object()) {
      const Json& value = *attempt_json;
      last_attempt.attempt = value.value("attempt", uint64_t(0));
      last_attempt.configuration = value.value("configuration", uint64_t(0));
      last_attempt.phase = value.value("phase", int32_t(-1));
      last_attempt.complete = value.value("complete", false);
      last_attempt.ignored = value.value("ignored", false);
      last_attempt.required = value.value("required", uint64_t(0));
      last_attempt.succeeded = value.value("succeeded", uint64_t(0));
      last_attempt.failed = value.value("failed", uint64_t(0));
      last_attempt.missing = value.value("missing", uint64_t(0));
      last_attempt.cancelled = value.value("cancelled", uint64_t(0));
      last_attempt.elapsed_milliseconds = value.value("elapsed_milliseconds", uint64_t(0));
    }
    loaded = true;
  } catch (const std::exception& e) {
    // Malformed values are discarded as a whole: a partially trusted ledger is
    // exactly what "stale data bypasses preparation" would look like.
    *this = PreparationLedger();
    if (why) *why = std::string("malformed preparation state: ") + e.what();
  }
}

bool PreparationLedger::Save(const std::filesystem::path& cache_root, std::string* why) {
  Json json{{"schema", kSchema},
            {"title_id", "5841122D"},
            {"content_epoch", content_epoch},
            {"extraction", LayerToJson(extraction)},
            {"last_attempt", Json{{"attempt", last_attempt.attempt},
                                  {"configuration", last_attempt.configuration},
                                  {"phase", last_attempt.phase},
                                  {"complete", last_attempt.complete},
                                  {"ignored", last_attempt.ignored},
                                  {"required", last_attempt.required},
                                  {"succeeded", last_attempt.succeeded},
                                  {"failed", last_attempt.failed},
                                  {"missing", last_attempt.missing},
                                  {"cancelled", last_attempt.cancelled},
                                  {"elapsed_milliseconds", last_attempt.elapsed_milliseconds}}}};
  Json recipes_json = Json::object();
  for (size_t i = 0; i < kPreparationPathCount; ++i) {
    recipes_json[kPreparationPaths[i]] = LayerToJson(this->recipes[i]);
  }
  json["recipes"] = std::move(recipes_json);
  std::string error;
  if (!fs::WriteAtomic(PathFor(cache_root), json.dump(2), &error)) {
    writable = false;
    if (why) *why = error;
    return false;
  }
  writable = true;
  loaded = true;
  return true;
}

}  // namespace rb_blitz::graphics
