// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the DLC directory layout in src/fs/dlc_layout.h, which
// src/hooks/dlc.cpp registers with the SDK as an extra read-only content root. No
// SDK, no game image, no boot: the questions are which directory names the SDK will
// look up, which entries are packages, and what a misplaced file reports.
//
// What is pinned here is the layout the guest's own enumeration depends on: the SDK
// formats the search path with fmt("{:08X}") and looks up the *exact* directory name,
// so a lower-case or wider-level spelling enumerates nothing, and a package is either
// a directory or an STFS container - not a stray file with a promising name.

#include "check.h"

#include "fs/dlc_layout.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::fs;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

// A file with whatever content the case needs, so the STFS magic check is exercised
// on real bytes rather than on names.
void WriteFile(const fs::path& path, const std::string& contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary);
  file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// A scratch tree shaped like the shipped one:
//
//   <temp>/rb_blitz-dlc-layout/game/dlc/...
//
// `game` stands in for game_data_root, so ResolveDlcRoot's default is checked against
// a directory that exists.
struct Scratch {
  fs::path base;
  fs::path game;
  fs::path root;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-dlc-layout";
    fs::remove_all(base, ec);
    game = base / "game";
    root = game / "dlc";
    fs::create_directories(root, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
};

const DlcPackage* FindPackage(const DlcScanResult& scan, const std::string& file_name) {
  for (const auto& package : scan.packages) {
    if (package.file_name == file_name) {
      return &package;
    }
  }
  return nullptr;
}

const DlcScanResult::Rejected* FindRejected(const DlcScanResult& scan,
                                            const std::string& entry) {
  for (const auto& rejected : scan.rejected) {
    if (rejected.entry == entry) {
      return &rejected;
    }
  }
  return nullptr;
}

bool Mentions(const DlcScanResult::Rejected* rejected, const std::string& text) {
  return rejected != nullptr && rejected->reason.find(text) != std::string::npos;
}

// A fraction of the real world: RB3's DLC packages are LIVE containers named with a
// 40-digit content id, Blitz's own are 32-digit, and both are files.
const char* kRb3Package = "65CC8CBB5AE04F1917CF420B5F6346F93109177445";
const char* kBlitzPackage = "0000000818E2A242C8CFE9375841122D";

}  // namespace

int main() {
  BeginCase("the configured root resolves against the game data root");
  {
    Scratch s;
    CHECK_TRUE(ResolveDlcRoot("", s.game) == s.game / "dlc");
    CHECK_TRUE(ResolveDlcRoot("dlc", s.game) == s.game / "dlc");
    CHECK_TRUE(ResolveDlcRoot("./dlc", s.game) == s.game / "dlc");
    CHECK_TRUE(ResolveDlcRoot("extra/dlc", s.game) == s.game / "extra" / "dlc");
    CHECK_TRUE(ResolveDlcRoot("../other", s.game) == s.game.parent_path() / "other");
    // An absolute value is honoured as given, the way --game_data_root is.
    const fs::path elsewhere = s.base / "elsewhere";
    CHECK_TRUE(ResolveDlcRoot(elsewhere.string(), s.game) == elsewhere);
  }

  BeginCase("content root names are 8 upper-case hex digits");
  {
    CHECK_TRUE(IsContentRootName("5841122D"));
    CHECK_TRUE(IsContentRootName("45410914"));
    CHECK_TRUE(IsContentRootName("00000002"));
    CHECK_TRUE(IsContentRootName("00009000"));
    // The content root's own xuid level: a whole content-root subtree copied in here
    // must be reported, not enumerate nothing.
    CHECK_FALSE(IsContentRootName("0000000000000000"));
    CHECK_FALSE(IsContentRootName("B13EBABEBABEBABE"));
    // Lower case: the SDK looks the name up with fmt("{:08X}").
    CHECK_FALSE(IsContentRootName("5841122d"));
    CHECK_FALSE(IsContentRootName("00000002 "));
    CHECK_FALSE(IsContentRootName("Headers"));
    CHECK_FALSE(IsContentRootName("0000000G"));
    CHECK_FALSE(IsContentRootName("5841122"));
    CHECK_FALSE(IsContentRootName(""));
  }

  BeginCase("a package is an STFS container, not just any file");
  {
    Scratch s;
    const fs::path type_root = s.root / "5841122D" / "00009000";
    WriteFile(type_root / "live-container", "LIVE\x00\x01\x02\x03");
    WriteFile(type_root / "con-container", "CON more-header");
    WriteFile(type_root / "pirs-container", "PIRS more-header");
    WriteFile(type_root / "notes.txt", "where do the songs go?");

    CHECK_TRUE(LooksLikeContentPackage(type_root / "live-container"));
    CHECK_TRUE(LooksLikeContentPackage(type_root / "con-container"));
    CHECK_TRUE(LooksLikeContentPackage(type_root / "pirs-container"));
    CHECK_FALSE(LooksLikeContentPackage(type_root / "notes.txt"));
    CHECK_FALSE(LooksLikeContentPackage(type_root / "does-not-exist"));

    const DlcScanResult scan = ScanDlcRoot(s.root);
    CHECK_EQ(scan.packages.size(), 3);
    CHECK_EQ(scan.rejected.size(), 1);
    CHECK_TRUE(Mentions(FindRejected(scan, "5841122D/00009000/notes.txt"), "content package"));
  }

  BeginCase("the shipped layout enumerates both titles");
  {
    Scratch s;
    const fs::path rb3 = s.root / "45410914" / "00000002";
    const fs::path blitz = s.root / "5841122D" / "00009000";

    // RB3 DLC: one 803 MB LIVE container.
    WriteFile(rb3 / kRb3Package, "LIVE\x00\x00\x00\x00");
    // Blitz's own DLC: the packages live in the common xuid directory in the content
    // root, and here they are what they are on disk.
    WriteFile(blitz / kBlitzPackage, "LIVE\x00\x00\x00\x00");
    // The extracted form has to keep working: one directory per installed item.
    std::error_code ec;
    fs::create_directories(blitz / "00000100D0CD8243CA4C115D5841122D", ec);

    const DlcScanResult scan = ScanDlcRoot(s.root);
    CHECK_EQ(scan.packages.size(), 3);
    CHECK_EQ(scan.rejected.size(), 0);

    const DlcPackage* rb3_package = FindPackage(scan, kRb3Package);
    CHECK_TRUE(rb3_package != nullptr);
    if (rb3_package != nullptr) {
      CHECK_TRUE(rb3_package->title_id == "45410914");
      CHECK_TRUE(rb3_package->content_type == "00000002");
      CHECK_TRUE(fs::is_regular_file(rb3_package->host_path));
    }

    const DlcPackage* blitz_package = FindPackage(scan, kBlitzPackage);
    CHECK_TRUE(blitz_package != nullptr);
    if (blitz_package != nullptr) {
      CHECK_TRUE(blitz_package->title_id == "5841122D");
      CHECK_TRUE(blitz_package->content_type == "00009000");
    }

    const DlcPackage* extracted = FindPackage(scan, "00000100D0CD8243CA4C115D5841122D");
    CHECK_TRUE(extracted != nullptr);
    if (extracted != nullptr) {
      CHECK_TRUE(fs::is_directory(extracted->host_path));
    }
  }

  BeginCase("what cannot be content is reported, not ignored");
  {
    Scratch s;
    // A file where a title directory belongs.
    WriteFile(s.root / "README.txt", "drop the packages in <title id>/<type>/");
    // A directory whose name is not a title id at all.
    std::error_code ec;
    fs::create_directories(s.root / "rhythm" / "00000002", ec);
    // The .header level of the content root, which is not content.
    fs::create_directories(s.root / "45410914" / "Headers" / "00000002", ec);
    // Content that is not a package: what a MOGG or an ark looks like.
    WriteFile(s.root / "45410914" / "00000002" / "song.dat", "MOGG not a container");

    const DlcScanResult scan = ScanDlcRoot(s.root);
    CHECK_EQ(scan.packages.size(), 0);
    CHECK_EQ(scan.rejected.size(), 4);
    CHECK_TRUE(Mentions(FindRejected(scan, "README.txt"), "not a directory"));
    CHECK_TRUE(Mentions(FindRejected(scan, "rhythm"), "not a title_id"));
    CHECK_TRUE(Mentions(FindRejected(scan, "45410914/Headers"), "not a content_type"));
    CHECK_TRUE(Mentions(FindRejected(scan, "45410914/00000002/song.dat"), "content package"));
  }

  BeginCase("an absent DLC directory is the normal case");
  {
    Scratch s;
    const DlcScanResult missing = ScanDlcRoot(s.base / "nothing-here");
    CHECK_EQ(missing.packages.size(), 0);
    // No noise either: the folder being absent is not a user mistake.
    CHECK_EQ(missing.rejected.size(), 0);

    const DlcScanResult empty = ScanDlcRoot(s.root);
    CHECK_EQ(empty.packages.size(), 0);
    CHECK_EQ(empty.rejected.size(), 0);
  }

  return Finish();
}
