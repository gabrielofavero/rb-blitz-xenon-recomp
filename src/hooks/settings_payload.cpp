// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/settings_payload.h"

#include <chrono>
#include <set>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/filesystem/file.h>
#include <rex/system/xmemory.h>

#include "fs/payload_overlay.h"
#include "hooks/menu_filter.h"
#include "hooks/ultimate.h"
#include "settings/boot.h"
#include "settings/menu_payload.h"
#include "settings/store.h"
#include "settings/schema.h"
#include "ui/settings_screen.h"
#include "platform/game_restart.h"
#include "ui/menu_options.h"

namespace rb_blitz::settings {
namespace {
using ultimate_adapter::ArkIndex;
using Bytes = std::vector<uint8_t>;

bool Read(rex::filesystem::VirtualFileSystem* vfs, const std::string& path,
          uint64_t offset, size_t length, Bytes* bytes, std::string* error) {
  auto* entry = vfs->ResolvePath("game:\\" + path);
  auto fail = [&] {
    if (error) *error = "cannot read " + path;
    return false;
  };
  if (!entry || offset > entry->size()) return fail();
  if (length == 0) length = entry->size();
  if (length > entry->size() - offset || length > 32 * 1024 * 1024) return fail();
  rex::filesystem::File* file = nullptr;
  if (entry->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 || !file) return fail();
  bytes->resize(length);
  size_t got = 0;
  const auto status = file->ReadSync(*bytes, offset, &got);
  file->Destroy();
  return status == 0 && got == length ? true : fail();
}

bool ReadRecord(rex::filesystem::VirtualFileSystem* vfs, const ArkIndex& index,
                const std::string& path, Bytes* bytes, std::string* error) {
  const auto* entry = index.Find(path);
  if (!entry || entry->compressed()) {
    if (error) *error = "missing or compressed UI record: " + path;
    return false;
  }
  uint64_t offset = entry->offset;
  for (size_t i = 0; i < index.parts.size(); ++i) {
    if (offset >= index.part_sizes[i]) { offset -= index.part_sizes[i]; continue; }
    if (entry->size > index.part_sizes[i] - offset) break;
    return Read(vfs, index.parts[i], offset, entry->size, bytes, error);
  }
  if (error) *error = "UI record outside its archive: " + path;
  return false;
}

bool WriteBytes(const std::filesystem::path& path, const Bytes& bytes, std::string* error) {
  return WriteFileAtomic(path, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                         error);
}
}

bool InstallMenuPayload(rex::Runtime* runtime) {
  const auto started = std::chrono::steady_clock::now();
  auto* vfs = runtime->file_system();
  ArkIndex main, patch;
  Bytes bytes;
  std::string error;
  auto fail = [&] {
    REXLOG_ERROR("settings_menu: installation refused: {}", error);
    return false;
  };
  if (!Read(vfs, "gen/main_xbox.hdr", 0, 0, &bytes, &error) ||
      !ultimate_adapter::ReadArkIndex(bytes.data(), bytes.size(), &main, &error)) return fail();
  if (vfs->ResolvePath("game:\\gen\\patch_xbox.hdr")) {
    if (!Read(vfs, "gen/patch_xbox.hdr", 0, 0, &bytes, &error) ||
        !ultimate_adapter::ReadArkIndex(bytes.data(), bytes.size(), &patch, &error)) return fail();
  }
  MenuRecords originals;
  RecordReader read = [&](const std::string& path, Bytes* out, std::string* why) {
    auto found = originals.find(path);
    if (found != originals.end()) { *out = found->second; return true; }
    if (!ReadRecord(vfs, patch.Find(path) ? patch : main, path, out, why)) return false;
    originals[path] = *out;
    return true;
  };
  std::set<std::string> locale_set;
  for (const auto* index : {&main, &patch}) {
    for (const auto& entry : index->entries) {
      if (entry.path.starts_with("ui/locale/") && entry.path.ends_with("/gen/locale_keep.dtb"))
        locale_set.insert(entry.path);
    }
  }
  if (locale_set.empty()) { error = "no installed UI locale"; return fail(); }
  if (read("ui/splash/gen/splash.dtb", &bytes, &error)) {
    const auto inventory = menu_options::MenuInventory(bytes.data(), bytes.size());
    if (auto* boot = boot::SettingsBoot::Current(); inventory.parsed && boot) {
      std::vector<Choice> rows;
      for (const auto& row : inventory.rows) rows.push_back({row.token, row.token});
      boot->SetMenuRows(std::move(rows));
    }
  }
  if (auto* boot = boot::SettingsBoot::Current(); boot && boot->service()) {
    ui::screen::MenuConfiguration configuration;
    const auto& provider = ultimate::Provider();
    for (const auto& descriptor : boot->service()->descriptors()) {
      const std::string token = std::string(CategoryToken(descriptor.category)) + "." +
                                std::string(SettingIdToken(descriptor.id));
      if (!descriptor.choices.empty()) {
        for (const auto& choice : descriptor.choices)
          configuration.choices[token].emplace_back(choice.token, choice.label);
      }
      if (boot->service()->IsOverridden(descriptor.id))
        configuration.disabled[token] = "Externally configured";
    }
    const auto renderer = boot->service()->EffectiveValue(SettingId::kRenderer).text;
    for (const auto& [key, tokens] : std::map<std::string, std::vector<std::string>>{
        {"graphics.anti_aliasing", AntiAliasingTokens(boot->capabilities(), renderer)},
        {"graphics.filtering", FilteringTokens(boot->capabilities(), renderer)}}) {
      auto& choices = configuration.choices[key];
      const auto* descriptor = boot->service()->DescriptorFor(schema::IdForToken(key));
      choices.clear();
      for (const auto& token : tokens)
        choices.emplace_back(token, schema::LabelForToken(*descriptor, token));
    }
    for (const auto& binding : ultimate_adapter::RowBindings()) {
      const auto found = provider.choices.find(std::string(binding.variable));
      if (found != provider.choices.end()) {
        auto& choices = configuration.choices[std::string(binding.token)];
        choices.clear();
        for (const auto& token : found->second) choices.emplace_back(token, token);
      }
    }
    // Trial still faults in the retail boot and is not a usable target in this build.
    configuration.choices["general.target"] = {{"common", "Rock Band Blitz"}};
    if (provider.payload_present)
      configuration.choices["general.target"].emplace_back("ultimate", "Rock Band Blitz Ultimate");
    if (provider.Active() && boot->service()->store().document().values.contains(SettingId::kUnlockAll))
      configuration.boot_booleans["unlockall"] = boot->service()->EffectiveValue(SettingId::kUnlockAll).boolean;
    for (const auto& row : schema::MenuRows()) {
      if (row.token != "mod_settings" && row.token != "advanced_settings")
        configuration.menu_rows.emplace_back(row.token, row.label);
    }
    ui::screen::ConfigureMenu(std::move(configuration));
  }
  MenuRecords records;
  if (!BuildMenuRecords(read, {locale_set.begin(), locale_set.end()}, &records, &error)) return fail();
  Bytes part;
  const Bytes header = ExtendMenuArchive(patch, records, &part);
  // Only generated records live here. Installed game and provider files remain read-only.
  const auto root = runtime->cache_root() / "settings-menu" /
                    std::to_string(platform::ThisProcessId());
  if (!WriteBytes(root / "gen/settings_xbox_0.ark", part, &error) ||
      !WriteBytes(root / "gen/patch_xbox.hdr", header, &error)) return fail();
  constexpr std::string_view mount = "\\Device\\BlitzSettingsMenu";
  auto* base = vfs->ResolvePath("game:\\");
  auto overlay = std::make_unique<fs::PayloadOverlayDevice>(mount, root, base);
  if (!base || !overlay->Initialize() || !vfs->RegisterDevice(std::move(overlay))) {
    error = "cannot mount generated UI records";
    return fail();
  }
  for (std::string_view alias : {"game:", "d:"}) {
    vfs->UnregisterSymbolicLink(alias);
    if (!vfs->RegisterSymbolicLink(alias, mount)) { error = "cannot route UI archive"; return fail(); }
  }
  ultimate::EnableSettingsArchive(runtime);
  for (const auto& [path, replacement] : records) {
    const auto original = originals.find(path);
    if (original == originals.end()) continue;
    uint8_t before[20], after[20];
    menu_options::Sha1Digest(original->second.data(), original->second.size(), before);
    menu_options::Sha1Digest(replacement.data(), replacement.size(), after);
    menu_filter::RetargetRecord(runtime->memory()->virtual_membase(), before, after);
  }
  REXLOG_INFO("settings_menu: mounted {} generated records over {} provider records in {} ms",
              records.size(), patch.entries.size(),
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started).count());
  return true;
}
}
