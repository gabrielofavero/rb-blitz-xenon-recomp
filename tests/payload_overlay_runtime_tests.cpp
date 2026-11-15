// SPDX-License-Identifier: GPL-2.0-only
// Real VFS/host-file operations, without a guest, renderer or UI.
#include "check.h"
#include "fs/payload_overlay.h"
#include <rex/filesystem/vfs.h>
#include <array>
#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
namespace vfs = rex::filesystem;
using namespace rb_blitz::test;

std::unique_ptr<vfs::VirtualFileSystem> Mount(const fs::path& root) {
  auto system = std::make_unique<vfs::VirtualFileSystem>();
  auto overlay = std::make_unique<rb_blitz::fs::PayloadOverlayDevice>(
      "\\Device\\TestOverlay", root / "payload", root / "game", true,
      std::vector<std::string>{});
  CHECK_TRUE(overlay->AddSettingsSide(root / "user", {"ulti_settings.dta", "scores"}));
  CHECK_TRUE(overlay->Initialize());
  CHECK_TRUE(system->RegisterDevice(std::move(overlay)));
  CHECK_TRUE(system->RegisterSymbolicLink("game:", "\\Device\\TestOverlay"));
  CHECK_TRUE(system->RegisterSymbolicLink("d:", "\\Device\\TestOverlay"));
  return system;
}

std::string Read(vfs::VirtualFileSystem& system, const char* path) {
  vfs::File* file = nullptr;
  vfs::FileAction action{};
  const auto status = system.OpenFile(nullptr, path, vfs::FileDisposition::kOpen,
      vfs::FileAccess::kGenericRead, false, true, &file, &action);
  CHECK_EQ(status, 0);
  if (!file) return {};
  std::array<uint8_t, 256> bytes{};
  size_t count = 0;
  CHECK_EQ(file->ReadSync(bytes, 0, &count), 0);
  file->Destroy();
  return std::string(reinterpret_cast<const char*>(bytes.data()), count);
}

bool Write(vfs::VirtualFileSystem& system, const char* path, const std::string& text) {
  vfs::File* file = nullptr;
  vfs::FileAction action{};
  const auto status = system.OpenFile(nullptr, path, vfs::FileDisposition::kOverwriteIf,
      vfs::FileAccess::kGenericWrite, false, true, &file, &action);
  if (status || !file) return false;
  size_t count = 0;
  const auto result = file->WriteSync(
      {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, 0, &count);
  file->Destroy();
  return result == 0 && count == text.size();
}

int main() {
  const auto root = fs::temp_directory_path() / ("rbbz-overlay-runtime-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK_TRUE(fs::create_directory(root));
  fs::create_directory(root / "payload");
  fs::create_directory(root / "game");
  fs::create_directory(root / "user");
  { std::ofstream(root / "game" / "asset.bin") << "read-only game data"; }
  const std::string score = "(123 456789 5)\n";
  BeginCase("scores survive a new VFS instance and both game aliases");
  {
    auto system = Mount(root);
    CHECK_TRUE(system->ResolvePath("game:\\scores") == nullptr);
    CHECK_TRUE(Write(*system, "game:\\SCORES", score));
    CHECK_TRUE(Read(*system, "d:\\scores") == score);
    CHECK_FALSE(Write(*system, "game:\\asset.bin", "must not change"));
    CHECK_FALSE(Write(*system, "game:\\unrelated.bin", "must not create"));
  }
  {
    auto system = Mount(root);
    CHECK_TRUE(Read(*system, "game:\\scores") == score);
    CHECK_TRUE(Read(*system, "d:\\SCORES") == score);
    CHECK_TRUE(Write(*system, "game:\\SCORES", "updated"));
    CHECK_TRUE(Read(*system, "d:\\scores") == "updated");
    CHECK_TRUE(Write(*system, "d:\\scores", score));
    CHECK_TRUE(Read(*system, "game:\\asset.bin") == "read-only game data");
    CHECK_FALSE(fs::exists(root / "game" / "scores"));
    CHECK_TRUE(fs::exists(root / "user" / "scores"));
  }
  BeginCase("writable records override legacy root entries without duplicate children");
  { std::ofstream(root / "game" / "scores") << "legacy score";
    std::ofstream(root / "game" / "ulti_settings.dta") << "legacy settings";
    std::ofstream(root / "user" / "ulti_settings.dta") << "user settings"; }
  {
    auto system = Mount(root);
    CHECK_TRUE(Read(*system, "game:\\scores") == score);
    CHECK_TRUE(Read(*system, "game:\\ulti_settings.dta") == "user settings");
    size_t scores = 0;
    for (const auto& child : system->ResolvePath("game:")->children())
      if (rb_blitz::fs::FoldName(child->name()) == "scores") ++scores;
    CHECK_EQ(scores, 1);
  }
  fs::remove_all(root);
  return Finish();
}
