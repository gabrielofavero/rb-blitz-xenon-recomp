// SPDX-License-Identifier: GPL-2.0-only
// Real VFS/host-file operations, without a guest, renderer or UI.
#include "check.h"
#include "fs/payload_overlay.h"
#include <rex/filesystem/file_system_factory.h>
#include <rex/app/app.h>
#include <rex/cli.h>
#include <rex/settings_registry.h>
#include <array>
#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
namespace vfs = rex::filesystem;
using namespace rb_blitz::test;


class FileApp final : public rex::app::App {
 public:
  FileApp():App("payload_overlay_probe"){}
  std::unique_ptr<vfs::IFileSystem> CreateFileSystem() {
    auto* factory=GetControlFactory().Get<vfs::IFileSystemFactory>();
    CHECK_TRUE(factory!=nullptr);
    return factory ? factory->Create() : nullptr;
  }
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    for (const auto* key:{"ui.driver","hid.drivers","apu.driver","gpu.driver"})
      REX_TRY_VOID(registry.SetText(rex::SettingLayer::kTitle,key,"none"));
    return registry.SetText(rex::SettingLayer::kTitle,"log.file","false");
  }
};

std::unique_ptr<vfs::IFileSystem> Mount(FileApp& app,const fs::path& root) {
  auto system=app.CreateFileSystem();
  if(!system)return {};
  CHECK_TRUE(system->Mount({"\\Device\\Base",vfs::ProviderType::kHostDirectory,
      root/"game",vfs::MountFlags::kNone,{}}).has_value());
  std::string error;
  CHECK_TRUE(rb_blitz::fs::MountPayloadOverlay(*system,"\\Device\\TestOverlay",
      root/"payload","\\Device\\Base",{},root/"user",{"ulti_settings.dta","scores"},&error));
  return system;
}

std::string Read(vfs::IFileSystem& system,const char* path) {
  auto opened=system.Open(path,{});
  CHECK_TRUE(opened.has_value());
  if(!opened)return {};
  opened->file->Refresh();
  std::vector<uint8_t> bytes(static_cast<size_t>(opened->file->GetInfo().size));
  if(bytes.empty())return {};
  auto read=opened->file->Read(bytes,0);
  CHECK_TRUE(read.has_value());
  return read ? std::string(bytes.begin(),bytes.begin()+*read) : std::string{};
}

bool Write(vfs::IFileSystem& system,const char* path,const std::string& text) {
  auto opened=system.Open(path,{.disposition=vfs::Disposition::kOverwriteIf,
      .access=vfs::Access::kWriteAccess,.require_file=true});
  if(!opened)return false;
  auto written=opened->file->Write({reinterpret_cast<const uint8_t*>(text.data()),text.size()},0);
  auto flushed=opened->file->Flush();
  return written && *written==text.size() && flushed;
}

int main() {
  FileApp app;
  auto initialized=app.Init(rex::CLIArgList::Parse(std::vector<std::string>{}));
  CHECK_TRUE(initialized.has_value());
  if(!initialized)return Finish();
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
    auto system = Mount(app,root);
    CHECK_TRUE(!system->Stat("game:\\scores"));
    CHECK_TRUE(Write(*system, "game:\\SCORES", score));
    CHECK_TRUE(Read(*system, "d:\\scores") == score);
    CHECK_FALSE(Write(*system, "game:\\asset.bin", "must not change"));
    CHECK_FALSE(Write(*system, "game:\\unrelated.bin", "must not create"));
  }
  {
    auto system = Mount(app,root);
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
    auto system = Mount(app,root);
    CHECK_TRUE(Read(*system, "game:\\scores") == score);
    CHECK_TRUE(Read(*system, "game:\\ulti_settings.dta") == "user settings");
    size_t scores = 0;
    auto listing=system->List("game:","");
    CHECK_TRUE(listing.has_value());
    if(listing)while(auto child=(*listing)->Next())
      if(rb_blitz::fs::FoldName(child->name)=="scores")++scores;
    CHECK_EQ(scores, 1);
  }
  fs::remove_all(root);
  CHECK_EQ(app.Shutdown(),0);
  return Finish();
}
