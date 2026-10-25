// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for rexglue-sdk/include/rex/graphics/native_pipeline_cache.h - the
// driver-specific layer both pipeline caches persist beside the shareable
// recipes (docs/plans/finished/2026-10-10-shader-cache-plan.md, P5).
//
// The cases mirror what makes a native cache different from the portable stores,
// and each one is a rule the backends rely on:
//
//   * an entry round-trips through a real directory, with the header validating
//     identity, key, size and payload hash;
//   * a corrupted payload, a foreign identity, a truncated file and a wrong key
//     are refused - a driver blob is opaque, so "refused" is the only safe
//     answer to anything unexpected;
//   * an entry is replaced atomically (a reader never sees a prefix) and can be
//     forgotten, which is what the backends do when a driver rejects a blob;
//   * staging is bounded and a flush reports exactly what it wrote;
//   * a disabled store answers nothing and writes nothing, so a machine without
//     a writable cache directory still runs.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"

#include <rex/graphics/native_pipeline_cache.h>

namespace {

using namespace rb_blitz::test;
using rex::graphics::GraphicsPreparationBackend;
using rex::graphics::NativePipelineCacheEntryHeader;
using rex::graphics::NativePipelineCacheIdentity;
using rex::graphics::NativePipelineCacheStore;

std::filesystem::path MakeScratchDirectory(const char* label) {
  const uint64_t stamp = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::path path = std::filesystem::temp_directory_path() /
                               ("rb_blitz_native_cache_" + std::string(label) + "_" +
                                std::to_string(stamp));
  std::error_code error;
  std::filesystem::remove_all(path, error);
  std::filesystem::create_directories(path, error);
  return path;
}

NativePipelineCacheIdentity MakeIdentity(GraphicsPreparationBackend backend, uint32_t title_id,
                                         uint32_t render_path) {
  NativePipelineCacheIdentity identity;
  identity.backend = backend;
  identity.title_id = title_id;
  identity.render_path = render_path;
  identity.pipeline_schema_version = 0x20210425u;
  identity.translator_version = 0x20260228u;
  identity.adapter.vendor = 0x10DE;
  identity.adapter.device = 0x13C2;
  identity.adapter.subsystem = 0x1111;
  identity.adapter.revision = 0xA1;
  identity.adapter.driver_version = 32u << 16 | 0u << 0;
  identity.adapter.driver_version_low = 15;
  for (size_t i = 0; i < sizeof(identity.adapter.device_uuid); ++i) {
    identity.adapter.device_uuid[i] = uint8_t(i + 1);
  }
  return identity;
}

std::vector<uint8_t> MakePayload(uint8_t seed, size_t size) {
  std::vector<uint8_t> payload(size);
  for (size_t i = 0; i < size; ++i) {
    payload[i] = uint8_t(seed + i * 7u);
  }
  return payload;
}

void TestRoundTrip() {
  BeginCase("an entry round-trips through the directory");
  const std::filesystem::path directory = MakeScratchDirectory("roundtrip");
  NativePipelineCacheStore store;
  const NativePipelineCacheIdentity identity =
      MakeIdentity(GraphicsPreparationBackend::kD3D12, 0x5841122D, 0);
  CHECK_TRUE(store.Configure(directory / "local", identity));
  CHECK_TRUE(store.enabled());
  CHECK_TRUE(std::filesystem::is_directory(directory / "local"));

  const std::vector<uint8_t> payload = MakePayload(0x11, 4096);
  const uint64_t key = 0x0123456789ABCDEFull;
  CHECK_TRUE(store.Stage(key, payload.data(), payload.size()));
  CHECK_EQ(store.Flush(), 1);

  std::vector<uint8_t> loaded;
  CHECK_TRUE(store.Load(key, loaded));
  CHECK_EQ(loaded.size(), payload.size());
  CHECK_MEM_EQ(loaded.data(), payload.data(), payload.size());

  const NativePipelineCacheStore::Counters counters = store.counters();
  CHECK_EQ(counters.staged, 1);
  CHECK_EQ(counters.written, 1);
  CHECK_EQ(counters.loaded, 1);
  CHECK_EQ(counters.bytes_written, payload.size() + sizeof(NativePipelineCacheEntryHeader));
  CHECK_EQ(counters.directory_entries, 1);

  // An absent key is a miss, not an error.
  std::vector<uint8_t> missing;
  CHECK_FALSE(store.Load(key + 1, missing));
  CHECK_EQ(store.counters().missing, 1);

  std::error_code error;
  std::filesystem::remove_all(directory, error);
}

void TestRefusals() {
  BeginCase("corrupted, foreign, truncated and mis-keyed entries are refused");
  const std::filesystem::path directory = MakeScratchDirectory("refusals");
  const NativePipelineCacheIdentity identity =
      MakeIdentity(GraphicsPreparationBackend::kVulkan, 0x5841122D, 1);
  NativePipelineCacheStore store;
  CHECK_TRUE(store.Configure(directory, identity));

  const std::vector<uint8_t> payload = MakePayload(0x22, 512);
  const uint64_t key = 0x1122334455667788ull;
  CHECK_TRUE(store.Stage(key, payload.data(), payload.size()));
  CHECK_EQ(store.Flush(), 1);

  std::vector<uint8_t> file_data;
  {
    const std::filesystem::path path = directory / "1122334455667788.pso";
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    CHECK_TRUE(file != nullptr);
    if (!file) {
      return;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    file_data.resize(size_t(size));
    CHECK_EQ(std::fread(file_data.data(), 1, file_data.size(), file), file_data.size());
    std::fclose(file);
  }

  // The untouched file is valid for its own identity and key.
  std::vector<uint8_t> decoded;
  CHECK_TRUE(NativePipelineCacheStore::DecodeEntry(file_data, identity.Hash(), key, decoded));
  CHECK_EQ(decoded.size(), payload.size());

  // A flipped payload byte fails the hash.
  {
    std::vector<uint8_t> corrupted = file_data;
    corrupted.back() ^= 0xFF;
    CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(corrupted, identity.Hash(), key, decoded));
  }
  // A truncated file is refused on its size field.
  {
    std::vector<uint8_t> truncated(file_data.begin(), file_data.end() - 1);
    CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(truncated, identity.Hash(), key, decoded));
  }
  // So is a header-only file with a payload length that does not fit.
  {
    NativePipelineCacheEntryHeader header;
    header.identity_hash = identity.Hash();
    header.key = key;
    header.payload_size = 4096;
    std::vector<uint8_t> forged(sizeof(header) + 16);
    std::memcpy(forged.data(), &header, sizeof(header));
    CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(forged, identity.Hash(), key, decoded));
  }
  // A different key, and a different identity, are both refused: a blob is only
  // valid for the pipeline and the driver build that produced it.
  CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(file_data, identity.Hash(), key ^ 1, decoded));
  {
    NativePipelineCacheIdentity other = identity;
    other.adapter.driver_version += 1;
    CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(file_data, other.Hash(), key, decoded));
    other = identity;
    other.render_path ^= 1;
    CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(file_data, other.Hash(), key, decoded));
    other = identity;
    other.translator_version += 1;
    CHECK_FALSE(NativePipelineCacheStore::DecodeEntry(file_data, other.Hash(), key, decoded));
  }

  // A store configured for another identity refuses the entry on Load, without
  // handing the bytes back to a caller that would give them to a driver.
  NativePipelineCacheStore other_store;
  NativePipelineCacheIdentity other_identity = identity;
  other_identity.adapter.device = 0x9999;
  CHECK_TRUE(other_store.Configure(directory, other_identity));
  std::vector<uint8_t> loaded;
  CHECK_FALSE(other_store.Load(key, loaded));
  CHECK_EQ(other_store.counters().rejected, 1);
  // The file is left in place: replacement is the owner's decision.
  CHECK_TRUE(other_store.Has(key));

  std::error_code error;
  std::filesystem::remove_all(directory, error);
}

void TestReplaceAndForget() {
  BeginCase("a staged entry replaces its file atomically and can be forgotten");
  const std::filesystem::path directory = MakeScratchDirectory("replace");
  NativePipelineCacheStore store;
  CHECK_TRUE(store.Configure(directory, MakeIdentity(GraphicsPreparationBackend::kD3D12,
                                                     0x5841122D, 0)));
  const uint64_t key = 0xABCDEF0123456789ull;

  const std::vector<uint8_t> first = MakePayload(0x33, 1024);
  CHECK_TRUE(store.Stage(key, first.data(), first.size()));
  CHECK_EQ(store.Flush(), 1);
  const std::vector<uint8_t> second = MakePayload(0x44, 2048);
  CHECK_TRUE(store.Stage(key, second.data(), second.size()));
  CHECK_EQ(store.Flush(), 1);

  std::vector<uint8_t> loaded;
  CHECK_TRUE(store.Load(key, loaded));
  CHECK_MEM_EQ(loaded.data(), second.data(), second.size());
  // A replace overwrites the file, so the directory still holds one entry.
  CHECK_EQ(store.counters().directory_entries, 1);

  // Staging twice before a flush keeps the later bytes and counts one pending
  // entry, so a burst of updates cannot multiply the write set.
  CHECK_TRUE(store.Stage(key, first.data(), first.size()));
  CHECK_TRUE(store.Stage(key, second.data(), second.size()));
  CHECK_EQ(store.counters().pending_entries, 1);
  CHECK_EQ(store.counters().pending_bytes, second.size());
  CHECK_EQ(store.Flush(), 1);

  // Forget is what a backend does when the driver rejects a stored blob.
  CHECK_TRUE(store.Forget(key));
  CHECK_FALSE(store.Has(key));
  CHECK_FALSE(store.Load(key, loaded));
  CHECK_EQ(store.counters().forgotten, 1);
  CHECK_EQ(store.counters().directory_entries, 0);

  // No temp file is left behind by a replace or a removal.
  size_t left = 0;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    (void)entry;
    ++left;
  }
  CHECK_EQ(left, 0);
  std::filesystem::remove_all(directory, error);
}

void TestBounds() {
  BeginCase("staging and flushing are bounded");
  const std::filesystem::path directory = MakeScratchDirectory("bounds");
  NativePipelineCacheStore store;
  CHECK_TRUE(store.Configure(directory, MakeIdentity(GraphicsPreparationBackend::kD3D12,
                                                     0x5841122D, 0)));

  // A single oversized blob is refused rather than written.
  std::vector<uint8_t> huge(NativePipelineCacheStore::kMaxEntryBytes + 1, 0x5A);
  CHECK_FALSE(store.Stage(1, huge.data(), huge.size()));
  CHECK_EQ(store.counters().refused, 1);
  // Empty payloads are refused too: an entry that carries nothing is not a
  // reusable result.
  CHECK_FALSE(store.Stage(2, huge.data(), 0));

  // Pending bytes are bounded, and the refusal is counted.
  const size_t chunk = 16u * 1024u * 1024u;
  std::vector<uint8_t> block = MakePayload(0x66, chunk);
  size_t accepted = 0;
  for (uint64_t key = 100; key < 100 + 16; ++key) {
    if (store.Stage(key, block.data(), block.size())) {
      ++accepted;
    }
  }
  CHECK_TRUE(accepted * chunk <= NativePipelineCacheStore::kMaxPendingBytes);
  CHECK_TRUE(accepted > 0);
  CHECK_TRUE(store.counters().refused > 0);
  CHECK_EQ(store.Flush(), accepted);

  std::error_code error;
  std::filesystem::remove_all(directory, error);
}

void TestDisabled() {
  BeginCase("a disabled store answers nothing and touches nothing");
  NativePipelineCacheStore store;
  std::vector<uint8_t> loaded;
  CHECK_FALSE(store.enabled());
  CHECK_FALSE(store.Load(1, loaded));
  CHECK_FALSE(store.Stage(1, "x", 1));
  CHECK_EQ(store.Flush(), 0);
  CHECK_FALSE(store.Forget(1));
  CHECK_FALSE(store.Has(1));
  CHECK_TRUE(store.Summary() == std::string("native pipeline cache disabled"));

  // An empty directory path cannot be configured.
  CHECK_FALSE(store.Configure({}, MakeIdentity(GraphicsPreparationBackend::kVulkan, 1, 0)));
  CHECK_FALSE(store.enabled());

  // A configured store reports where it writes, which the backends log.
  const std::filesystem::path directory = MakeScratchDirectory("summary");
  CHECK_TRUE(store.Configure(directory, MakeIdentity(GraphicsPreparationBackend::kVulkan, 1, 0)));
  CHECK_TRUE(store.directory() == directory);
  CHECK_TRUE(store.Summary().find("vulkan") != std::string::npos);
  CHECK_TRUE(store.Summary().find("0 reused") != std::string::npos);
  std::error_code error;
  std::filesystem::remove_all(directory, error);
}

void TestIdentity() {
  BeginCase("the identity hash covers every compatibility field");
  const NativePipelineCacheIdentity base =
      MakeIdentity(GraphicsPreparationBackend::kD3D12, 0x5841122D, 0);
  CHECK_TRUE(base.SameAs(base));
  CHECK_EQ(base.Hash(), base.Hash());

  NativePipelineCacheIdentity other = base;
  other.backend = GraphicsPreparationBackend::kVulkan;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.title_id += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.render_path += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.pipeline_schema_version += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.translator_version += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.format_version += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.adapter.vendor += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.adapter.driver_version += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.adapter.driver_version_low += 1;
  CHECK_TRUE(base.Hash() != other.Hash());
  other = base;
  other.adapter.device_uuid[5] ^= 0x80;
  CHECK_TRUE(base.Hash() != other.Hash());
  CHECK_FALSE(base.SameAs(other));
}

}  // namespace

int main() {
  TestIdentity();
  TestRoundTrip();
  TestRefusals();
  TestReplaceAndForget();
  TestBounds();
  TestDisabled();
  return Finish();
}
