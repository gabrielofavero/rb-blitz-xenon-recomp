// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_cache.h"
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/file.h>
#include <chrono>
#include <fstream>

using namespace rb_blitz::shell_music;

int main() {
  rb_blitz::test::BeginCase("open native files and source pins prevent cache retirement");
  const auto root = std::filesystem::temp_directory_path() /
      ("rbbz-music-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  { std::ofstream file(root / "audio.bin", std::ios::binary); file << "fixture"; }
  std::map<size_t, std::unique_ptr<rex::filesystem::Device>> owned;
  MusicMountCache<rex::filesystem::Device> cache;
  for (size_t i = 0; i < MusicSession::kResidentLimit; ++i) {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(
        "\\Device\\MusicCacheTest\\" + std::to_string(i), root, true);
    CHECK_TRUE(device->Initialize());
    CHECK_EQ(device->open_file_count(), 0u);
    CHECK_TRUE(cache.Insert(i, device.get()));
    owned.emplace(i, std::move(device));
  }
  CHECK_TRUE(cache.Full());
  CHECK_FALSE(cache.Insert(99, owned[0].get()));
  CHECK_EQ(cache.RetireCold({}, [](size_t) { return false; }), 0u);
  CHECK_EQ(cache.Size(), MusicSession::kResidentLimit);
  auto* entry = owned[0]->ResolvePath("audio.bin");
  CHECK_TRUE(entry != nullptr);
  rex::filesystem::File *first = nullptr, *second = nullptr;
  if (entry) {
    CHECK_EQ(entry->Open(rex::filesystem::FileAccess::kGenericRead, &first), 0u);
    CHECK_EQ(entry->Open(rex::filesystem::FileAccess::kGenericRead, &second), 0u);
    CHECK_EQ(owned[0]->open_file_count(), 2u);
  }
  const auto retire = [&](size_t package) { return owned.erase(package) == 1; };
  CHECK_EQ(cache.RetireCold({}, retire), MusicSession::kResidentLimit - 1);
  CHECK_TRUE(cache.Contains(0));
  if (first) first->Destroy();
  CHECK_EQ(owned[0]->open_file_count(), 1u);
  CHECK_EQ(cache.RetireCold({}, retire), 0u);
  if (second) second->Destroy();
  CHECK_EQ(owned[0]->open_file_count(), 0u);
  CHECK_EQ(cache.RetireCold({0}, retire), 0u); // captured selection still pins it
  CHECK_EQ(cache.RetireCold({}, retire), 1u);
  CHECK_EQ(cache.Size(), 0u);
  CHECK_TRUE(owned.empty());
  std::error_code ec;
  std::filesystem::remove(root / "audio.bin", ec);
  std::filesystem::remove(root, ec);
  return rb_blitz::test::Finish();
}
