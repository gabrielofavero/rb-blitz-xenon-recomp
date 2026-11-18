// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_cache.h"
#include "sdk_file_test_app.h"
#include <rex/filesystem/file_system_factory.h>
#include <rex/filesystem/mount_diagnostics.h>
#include <limits>
#include <rex/filesystem/file.h>
#include <chrono>
#include <fstream>

using namespace rb_blitz::shell_music;

namespace {
struct Resident {
  rex::filesystem::IFileSystem& files;
  rex::filesystem::IMountDiagnostics& diagnostics;
  std::string path;
  size_t open_file_count() const {
    const auto count=diagnostics.GetOpenCount(files,path);
    return count ? *count : std::numeric_limits<size_t>::max();
  }
};
}

int main() {
  FileServiceApp app;
  CHECK_TRUE(app.Init(rex::CLIArgList::Parse(std::span<const std::string>{})).has_value());
  auto* factory=REX_INTERFACE(rex::filesystem::IFileSystemFactory);
  auto* diagnostics=REX_INTERFACE(rex::filesystem::IMountDiagnostics);
  CHECK_TRUE(factory && diagnostics);
  if (!factory || !diagnostics) return rb_blitz::test::Finish();
  auto files=factory->Create();
  rb_blitz::test::BeginCase("open native files and source pins prevent cache retirement");
  const auto root = std::filesystem::temp_directory_path() /
      ("rbbz-music-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  { std::ofstream file(root / "audio.bin", std::ios::binary); file << "fixture"; }
  std::map<size_t, std::unique_ptr<Resident>> owned;
  MusicMountCache<Resident> cache;
  for (size_t i = 0; i < MusicSession::kResidentLimit; ++i) {
    const auto path="\\Device\\MusicCacheTest\\"+std::to_string(i);
    CHECK_TRUE(files->Mount({path,rex::filesystem::ProviderType::kHostDirectory,root}).has_value());
    auto device=std::make_unique<Resident>(*files,*diagnostics,path);
    CHECK_EQ(device->open_file_count(), 0u);
    CHECK_TRUE(cache.Insert(i, device.get()));
    owned.emplace(i, std::move(device));
  }
  CHECK_TRUE(cache.Full());
  CHECK_FALSE(cache.Insert(99, owned[0].get()));
  CHECK_EQ(cache.RetireCold({}, [](size_t) { return false; }), 0u);
  CHECK_EQ(cache.Size(), MusicSession::kResidentLimit);
  auto first=files->Open(owned[0]->path+"\\audio.bin",{});
  auto second=files->Open(owned[0]->path+"\\audio.bin",{});
  CHECK_TRUE(first && second);
  if(!first || !second)return rb_blitz::test::Finish();
  CHECK_EQ(owned[0]->open_file_count(),2u);
  const auto retire = [&](size_t package) {
    if(!files->Unmount(owned.at(package)->path))return false;
    return owned.erase(package)==1;
  };
  CHECK_EQ(cache.RetireCold({}, retire), MusicSession::kResidentLimit - 1);
  CHECK_TRUE(cache.Contains(0));
  first->file.reset();
  CHECK_EQ(owned[0]->open_file_count(), 1u);
  CHECK_EQ(cache.RetireCold({}, retire), 0u);
  second->file.reset();
  CHECK_EQ(owned[0]->open_file_count(), 0u);
  CHECK_EQ(cache.RetireCold({0}, retire), 0u); // captured selection still pins it
  CHECK_EQ(cache.RetireCold({}, retire), 1u);
  CHECK_EQ(cache.Size(), 0u);
  CHECK_TRUE(owned.empty());
  files.reset();
  app.Shutdown();
  std::error_code ec;
  std::filesystem::remove(root / "audio.bin", ec);
  std::filesystem::remove(root, ec);
  return rb_blitz::test::Finish();
}
