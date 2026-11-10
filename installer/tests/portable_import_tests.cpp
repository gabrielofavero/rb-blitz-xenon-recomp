// SPDX-License-Identifier: GPL-2.0-only
// SDK-free importer tests with synthetic content and a synthetic STFS package.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "game_import_core.h"
#include "util/sha256.h"

namespace fs = std::filesystem;
using namespace rb_blitz;
using namespace rb_blitz::installer;
using namespace rb_blitz::installer::portable;

namespace {
int failures = 0;
void Check(bool ok, std::string_view what) {
  if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
}
void Write(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  Check(static_cast<bool>(out), "write fixture");
}
std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

constexpr std::string_view kEntry = "synthetic xex";
constexpr std::string_view kHeader = "synthetic header";
constexpr std::string_view kArchive = "synthetic archive data";
const std::vector<std::string> kRoles{"entrypoint", "archive-header", "archive-data"};
const std::vector<std::string> kOptional{"ArcadeInfo.xml"};

fingerprint::GameFingerprint FixturePrint() {
  fingerprint::GameFingerprint print;
  print.schema_version = 1;
  print.name = "synthetic fixture";
  print.files = {{"entrypoint", "default.xex", kEntry.size(), util::HashHex(kEntry)},
                 {"archive-header", "gen/main_xbox.hdr", kHeader.size(), util::HashHex(kHeader)},
                 {"archive-data", "gen/main_xbox_0.ark", kArchive.size(), util::HashHex(kArchive)}};
  return print;
}
void MakeFolder(const fs::path& root) {
  Write(root / "default.xex", kEntry);
  Write(root / "gen/main_xbox.hdr", kHeader);
  Write(root / "gen/main_xbox_0.ark", kArchive);
  Write(root / "ArcadeInfo.xml", "optional-v1");
}

struct Member {
  std::string name;
  std::string bytes;
  int parent = -1;
  bool directory = false;
};

// A small read-only LIVE image with one file-table block. This exercises the
// production STFS reader without checking any retail bytes into the tree.
std::vector<std::uint8_t> Package(const std::vector<Member>& members) {
  constexpr std::size_t block = 0x1000;
  const auto physical = [](std::uint32_t logical) { return logical + (logical + 170) / 170; };
  std::vector<std::uint32_t> start(members.size()), count(members.size());
  std::uint32_t next = 1;
  for (std::size_t i = 0; i < members.size(); ++i) {
    count[i] = members[i].directory ? 0 : static_cast<std::uint32_t>((members[i].bytes.size()+block-1)/block);
    if (count[i]) { start[i] = next; next += count[i]; }
  }
  std::vector<std::uint8_t> image(block + static_cast<std::size_t>(physical(next - 1) + 1) * block, 0);
  const auto be32 = [&](std::size_t at, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) image[at + (3-i)] = static_cast<std::uint8_t>(value >> (i*8));
  };
  const auto le16 = [&](std::size_t at, std::uint16_t value) {
    image[at] = static_cast<std::uint8_t>(value); image[at+1] = static_cast<std::uint8_t>(value >> 8);
  };
  const auto le24 = [&](std::size_t at, std::uint32_t value) {
    for (int i=0; i<3; ++i) image[at+i] = static_cast<std::uint8_t>(value >> (i*8));
  };
  image[0]='L'; image[1]='I'; image[2]='V'; image[3]='E';
  be32(0x340, static_cast<std::uint32_t>(block));
  be32(0x344, 1); be32(0x354, 0x78492654); be32(0x360, 0x58412D2D);
  image[0x379 + 2] = 1; le16(0x379 + 3, 1); le24(0x379 + 5, 0);
  be32(0x379 + 0x1C, 170); be32(0x379 + 0x24, static_cast<std::uint32_t>(members.size()));
  std::size_t entry = block + static_cast<std::size_t>(physical(0)) * block;
  for (std::size_t i=0; i<members.size(); ++i,entry+=0x40) {
    Check(members[i].name.size() <= 0x28, "STFS fixture entry length");
    std::memcpy(image.data()+entry, members[i].name.data(), members[i].name.size());
    image[entry+0x28] = static_cast<std::uint8_t>(members[i].name.size()) |
                        static_cast<std::uint8_t>(members[i].directory ? 0x80 : 0x40);
    le24(entry+0x29,count[i]); le24(entry+0x2C,count[i]); le24(entry+0x2F,start[i]);
    image[entry+0x32] = members[i].parent < 0 ? 0xFF : 0;
    image[entry+0x33] = members[i].parent < 0 ? 0xFF : static_cast<std::uint8_t>(members[i].parent);
    be32(entry+0x34,static_cast<std::uint32_t>(members[i].bytes.size()));
    for (std::uint32_t j=0; j<count[i]; ++j) {
      const std::size_t offset = static_cast<std::size_t>(j)*block;
      const std::size_t take = std::min(block,members[i].bytes.size()-offset);
      std::memcpy(image.data()+block+static_cast<std::size_t>(physical(start[i]+j))*block,
                  members[i].bytes.data()+offset,take);
    }
  }
  return image;
}

void CheckTree(const fs::path& root, const fingerprint::GameFingerprint& print) {
  std::string error;
  Check(VerifyTree(root, print, kRoles, nullptr, &error), error);
}
}  // namespace

int main() {
  const fs::path scratch = fs::current_path() / "portable-import-scratch";
  std::error_code ec;
  fs::remove_all(scratch, ec);
  fs::create_directories(scratch, ec);
  Check(!ec, "create scratch directory");
  const auto print = FixturePrint();
  const fs::path source = scratch / fs::path(u8"source é 音楽");
  const fs::path root = scratch / fs::path(u8"saved game é 音楽");
  MakeFolder(source);
  std::string error;
  GameSourcePlan plan;
  Check(ProbeFolder(source.parent_path(), print, kRoles, kOptional, &plan, &error), error);
  Check(plan.Complete() && plan.kind == GameSourceKind::kFolder, "folder probe");
  Check(ProbePickedSource([&] { return source; }, print, kRoles, kOptional, &plan, &error),
        "native picker contract");
  int last_percent = -1;
  ImportCallbacks callbacks;
  callbacks.progress = [&](int percent, std::string_view) { last_percent = percent; };
  Check(Import(plan, root, print, kRoles, kOptional, callbacks, &error), error);
  Check(last_percent == 100, "progress reaches 100");
  CheckTree(root, print);
  Check(Read(root / "ArcadeInfo.xml") == "optional-v1", "optional file imported");

  Write(root / "ultimate/keep.txt", "owned mod data");
  Write(root / "saves/profile.dat", "owned save");
  Write(source / "ArcadeInfo.xml", "optional-v2");
  Check(ProbeFolder(source, print, kRoles, kOptional, &plan, &error), error);
  for (ImportPhase phase : {ImportPhase::kStaged, ImportPhase::kOldMoved,
                            ImportPhase::kNewActivated}) {
    callbacks.continue_at = [phase](ImportPhase current) { return current != phase; };
    Check(!Import(plan, root, print, kRoles, kOptional, callbacks, &error), "fault injection stops import");
    CheckTree(root, print);
    Check(Read(root / "ArcadeInfo.xml") == "optional-v1", "canceled import keeps old optional file");
    Check(Read(root / "ultimate/keep.txt") == "owned mod data", "canceled import keeps mod");
    Check(Read(root / "saves/profile.dat") == "owned save", "canceled import keeps save");
  }
  callbacks.continue_at = {};
  bool cancel_copy = false;
  callbacks.progress = [&](int percent, std::string_view) {
    if (percent > 0 && percent < 75) cancel_copy = true;
  };
  callbacks.cancelled = [&] { return cancel_copy; };
  Check(!Import(plan, root, print, kRoles, kOptional, callbacks, &error),
        "progress cancellation stops an active copy");
  Check(error == "import canceled", "copy cancellation has a clear reason");
  CheckTree(root, print);
  Check(Read(root / "ArcadeInfo.xml") == "optional-v1", "copy cancellation keeps active import");
  callbacks.cancelled = {};
  callbacks.progress = {};
  Check(Import(plan, root, print, kRoles, kOptional, callbacks, &error), error);
  Check(Read(root / "ArcadeInfo.xml") == "optional-v2", "update replaces optional file");
  Check(Read(root / "ultimate/keep.txt") == "owned mod data", "update preserves mod");
  Check(Read(root / "saves/profile.dat") == "owned save", "update preserves save");

  GameSourcePlan duplicate = plan;
  duplicate.files.push_back(duplicate.files.front());
  Check(!Import(duplicate, root, print, kRoles, kOptional, callbacks, &error),
        "contradictory duplicate plan is rejected");
  CheckTree(root, print);

  Write(source / "default.xex", "wrong content");
  Check(ProbeFolder(source, print, kRoles, kOptional, &plan, &error), "probe sees named files");
  Check(!Import(plan, root, print, kRoles, kOptional, callbacks, &error), "wrong hash fails staging");
  CheckTree(root, print);
  Check(Read(root / "ArcadeInfo.xml") == "optional-v2", "bad source keeps previous import");

  fs::path backup_name = root.filename();
  backup_name += fs::path(".import-backup").native();
  const fs::path backup = root.parent_path() / backup_name;
  fs::path stage_name = root.filename();
  stage_name += fs::path(".import-stage").native();
  const fs::path stage = root.parent_path() / stage_name;
  fs::rename(root, backup, ec);
  Check(!ec, "simulate interrupted old-root move");
  Write(stage / "junk.txt", "interrupted stage");
  Check(RecoverImport(root, print, kRoles, &error), error);
  CheckTree(root, print);
  Check(!fs::exists(backup) && !fs::exists(stage), "recovery removes transaction artifacts");

  fs::copy(root, backup, fs::copy_options::recursive, ec);
  Check(!ec, "simulate interrupted backup cleanup");
  Check(RecoverImport(root, print, kRoles, &error), error);
  Check(Read(root / "ArcadeInfo.xml") == "optional-v2", "recovery keeps verified active tree");
  Check(!fs::exists(backup), "recovery removes obsolete backup");

  Check(!ProbePickedSource([] { return fs::path{}; }, print, kRoles, kOptional, &plan, &error),
        "picker cancellation");

  // The checked-in production fingerprint is read directly; no game bytes are
  // present in the repository. A synthetic source with the right names must
  // still fail the real size/hash gate and leave the previous import intact.
#ifdef RBBLITZ_ROOT_DIR
  const fs::path repository = fs::path(RBBLITZ_ROOT_DIR);
#else
  const fs::path repository = fs::current_path();
#endif
  fingerprint::GameFingerprint supported;
  Check(fingerprint::LoadGameFingerprint(repository / "config/game_fingerprints.toml",
                                         &supported, &error), "load supported fingerprint");
  MakeFolder(source);
  GameSourcePlan unsupported;
  Check(ProbeFolder(source, supported, kRoles, kOptional, &unsupported, &error),
        "named synthetic files can be probed against supported fingerprint");
  Check(!Import(unsupported, root, supported, kRoles, kOptional, callbacks, &error),
        "unsupported source fails real fingerprint validation");
  CheckTree(root, print);

  const fs::path package_path = scratch / "synthetic.stfs";
  const std::vector<Member> members{{"gen", "", -1, true},
                                    {"default.xex", std::string(kEntry)},
                                    {"main_xbox.hdr", std::string(kHeader), 0},
                                    {"main_xbox_0.ark", std::string(kArchive), 0}};
  const auto image = Package(members);
  fs::create_directories(package_path.parent_path());
  std::ofstream out(package_path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
  out.close();
  error.clear();
  Check(ProbePackage(package_path, print, kRoles, kOptional, &plan, &error), error);
  const fs::path package_root = scratch / "package-import";
  Check(Import(plan, package_root, print, kRoles, kOptional, callbacks, &error), error);
  CheckTree(package_root, print);

  fs::remove_all(scratch, ec);
  std::cout << (failures == 0 ? "portable import tests passed\n" : "portable import tests failed\n");
  return failures == 0 ? 0 : 1;
}
