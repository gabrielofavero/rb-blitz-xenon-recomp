// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "fs/dlc_song_metadata.h"

#include <chrono>
#include <fstream>
#include "sdk_file_test_app.h"
#include <rex/filesystem/file_system_factory.h>

int main(int argc, char** argv) {
  FileServiceApp app;
  CHECK_TRUE(app.Init(rex::CLIArgList::Parse(std::span<const std::string>{})).has_value());
  auto* factory=REX_INTERFACE(rex::filesystem::IFileSystemFactory);
  CHECK_TRUE(factory); if(!factory)return rb_blitz::test::Finish();
  if (argc == 2) {
    std::error_code ec;
    const auto package=rb_blitz::fs::MountPackage("\\Device\\SongSafetyProbe",argv[1],ec);
    std::printf("song content: %s\n",package ? "static" : "refused or unreadable");
    return package ? 0 : 1;
  }
  using namespace rb_blitz::test;
  const auto root = std::filesystem::temp_directory_path() /
      ("rbbz-song-safety-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto inspect = [&] {
    auto files=factory->Create();
    rex::filesystem::MountEntry entry{"\\Device\\SongSafetyTest",rex::filesystem::ProviderType::kHostDirectory,root};
    std::string reason;
    entry.validator=std::make_shared<const rex::filesystem::MountValidator>(
        [&](const rex::filesystem::IMountValidationView& view)->rex::Result<void>{
          return rb_blitz::fs::IsSafeSongContent(view,&reason) ? rex::Ok()
              : rex::Err(rex::ErrorCategory::kValidation,reason);
        });
    auto mounted=files->Mount(entry);
    if(!mounted) CHECK_TRUE(files->GetMounts().empty());
    return mounted.has_value();
  };

  BeginCase("a content package without song metadata remains mountable");
  CHECK_TRUE(inspect());

  BeginCase("a static songs.dta remains mountable");
  std::filesystem::create_directories(root / "songs");
  {
    std::ofstream file(root / "songs" / "songs.dta", std::ios::binary);
    file << "(demo (name \"Stage {dive} #1\") (artist 'Blitz'))";
  }
  CHECK_TRUE(inspect());

  BeginCase("an autorun DTA in any package directory is refused");
  std::filesystem::create_directories(root / "unexpected");
  {
    std::ofstream file(root / "unexpected" / "songs.dta", std::ios::binary);
    file << "#ifdef INIT_SYNC #autorun {do {sprint \"hello\"}} #endif";
  }
  CHECK_FALSE(inspect());

  BeginCase("a script-bearing metadata file is refused even beside a safe one");
  std::filesystem::remove(root / "unexpected" / "songs.dta");
  std::filesystem::create_directories(root / "songs" / "gen");
  {
    std::ofstream file(root / "songs" / "gen" / "songs.dta", std::ios::binary);
    file << "(song (name {run exploit}))";
  }
  CHECK_FALSE(inspect());

  std::error_code ec;
  std::filesystem::remove(root / "songs" / "gen" / "songs.dta", ec);
  std::filesystem::remove(root / "songs" / "songs.dta", ec);
  std::filesystem::remove(root / "songs" / "gen", ec);
  std::filesystem::remove(root / "songs", ec);
  std::filesystem::remove(root / "unexpected", ec);
  std::filesystem::remove(root, ec);
  return Finish();
}
