// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "fs/dlc_song_metadata.h"

#include <chrono>
#include <fstream>
#include <rex/filesystem/devices/host_path_device.h>

int main(int argc, char** argv) {
  if (argc == 2) {
    std::error_code ec;
    auto device = rb_blitz::fs::MountPackage("\\Device\\SongSafetyProbe", argv[1], ec);
    if (!device) return 2;
    std::string reason;
    const bool safe = rb_blitz::fs::IsSafeSongContent(*device, &reason);
    std::printf("song content: %s%s%s\n", safe ? "static" : "refused",
                reason.empty() ? "" : ": ", reason.c_str());
    return safe ? 0 : 1;
  }
  using namespace rb_blitz::test;
  const auto root = std::filesystem::temp_directory_path() /
      ("rbbz-song-safety-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto inspect = [&] {
    rex::filesystem::HostPathDevice device("\\Device\\SongSafetyTest", root, true);
    CHECK_TRUE(device.Initialize());
    std::string reason;
    return rb_blitz::fs::IsSafeSongContent(device, &reason);
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
