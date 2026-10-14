// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/settings_payload.h"

#include <chrono>
#include <set>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/filesystem/file.h>
#include <rex/system/xmemory.h>

#include "fs/payload_overlay.h"
#include "hooks/menu_filter.h"
#include "hooks/shell_music.h"
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

void CollectLocale(const ui::dtb::Node& node, std::map<std::string, std::string>* labels) {
  if (node.kids.size() >= 2 && node.kids[0].tag == ui::dtb::kSymbol &&
      node.kids[1].tag == ui::dtb::kString)
    (*labels)[node.kids[0].text] = node.kids[1].text;
  for (const auto& child : node.kids) CollectLocale(child, labels);
}

// How a render size reads in the Resolution row. The token alone for 16:9 - the shape every monitor
// in the catalogue shares - and the aspect spelled out for the sizes that are not, because that is
// exactly what the owner has to be able to recognise: a 21:9 mode beside a pile of 16:9 numbers is
// invisible without the caption, and a "more height than 16:9" mode is the same problem.
std::string ResolutionChoiceLabel(const DisplayMode& size) {
  const std::string token = DisplayModeToken(size);
  if (size.height <= 0 || size.width <= 0) {
    return token;
  }
  // 16:9 within 1%, in integers: real modes are not exact ratios (1176x664 is a 16:9 TV mode while
  // 1920x1080 is not 16:9 in integers), so the caption marks what is *not* the shape every monitor in
  // the catalogue shares - which is exactly the entry the owner has to be able to spot.
  const int64_t lhs = int64_t(size.width) * 9;
  const int64_t rhs = int64_t(size.height) * 16;
  const int64_t difference = lhs > rhs ? lhs - rhs : rhs - lhs;
  if (difference * 100 <= rhs) {
    return token;
  }
  return token + (lhs > rhs ? " (wide)" : " (tall)");
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
  std::map<std::string, std::string> menu_labels;
  auto locale = std::find_if(locale_set.begin(), locale_set.end(),
      [](const std::string& path) { return path.find("/eng/") != std::string::npos; });
  if (locale == locale_set.end()) locale = locale_set.begin();
  if (read(*locale, &bytes, &error)) {
    std::vector<ui::dtb::Node> roots;
    ui::dtb::RootHeader header;
    if (ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &error))
      for (const auto& root : roots) CollectLocale(root, &menu_labels);
  }
  menu_labels["splash_options"] = "HELP & OPTIONS";
  // With R7 on, the downloadable-content row performs the library refresh and is drawn as such
  // (`menu_filter`'s own relabel); the hide-list label mirrors what the menu really says.
  if (rex::cvar::GetFlagByName("enhancements_dlc_cache") == "true") {
    menu_labels["splash_dlc"] = menu_options::kDlcRowRefreshLabel;
  }

  // The menu-music state, settled before anything that lists the menu's rows (owner feature,
  // 2026-10-14). `shell_music::Configure` has already run - it is called earlier in
  // OnPostLoadXexImage - so these three answers are final for this boot:
  //   * the setting itself ("DLC Menu Songs"), which decides whether a pool was built at all;
  //   * whether that pool really holds a song (the state the settings screen reports as
  //     unavailable, and the reason the new menu entry is absent when it is false);
  //   * the pool's song keys, which the menu record bakes as the probes that name the song playing.
  const bool menu_music_on = rb_blitz::shell_music::MenuMusicEnabled();
  const bool menu_music_pool = rb_blitz::shell_music::HasPool();
  const std::vector<std::string> menu_music_songs = rb_blitz::shell_music::PoolSongNames();
  bool play_current_setting = true;
  if (auto* boot = boot::SettingsBoot::Current(); boot && boot->service()) {
    play_current_setting =
        boot->service()->EffectiveValue(SettingId::kPlayCurrentSong).boolean;
  }
  const bool play_current_row = play_current_setting && menu_music_pool;
  REXLOG_INFO("settings_menu: \"DLC Menu Songs\" is {}; {} menu-music song(s); the Play Current "
              "Song entry is {}",
              menu_music_on ? "on" : "off", menu_music_songs.size(),
              play_current_row ? "built as the menu's second row"
                               : (play_current_setting ? "not built (no DLC song is playing)"
                                                       : "off (its setting is off)"));

  if (read("ui/splash/gen/splash.dtb", &bytes, &error)) {
    const auto inventory = menu_options::MenuInventory(bytes.data(), bytes.size());
    if (auto* boot = boot::SettingsBoot::Current(); inventory.parsed && boot) {
      std::vector<Choice> rows;
      for (const auto& row : inventory.rows) {
        rows.push_back({row.token, menu_labels.contains(row.token) ? menu_labels.at(row.token) : row.token});
        // The row this payload adds is not in the installed menu, so the inventory cannot carry it:
        // it is inserted here, right after PLAY, both so the Hide Items mirror lists it where the
        // menu draws it and so the hide selection's own choices agree with the menu it mirrors.
        if (play_current_row && row.token == "splash_start") {
          rows.push_back({ui::screen::kPlayCurrentRow, ui::screen::kPlayCurrentLabel});
        }
      }
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
    // The Resolution row lists what this machine and this render path can actually make, and the
    // same planner the boot applies decides it - so the row cannot offer a size a boot would refuse.
    // A display's own mode is in that list when the render path can produce it, which is how a 21:9
    // primary gets its own 2560x1080 instead of only the 16:9 catalogue (docs/plans/
    // ultrawide-findings.md §5). The schema's static list is the fallback for a boot whose
    // capabilities never got compiled.
    {
      const auto& supported = boot->capabilities().supported_resolutions;
      if (!supported.empty()) {
        auto& choices = configuration.choices["graphics.resolution"];
        choices.clear();
        choices.emplace_back("auto", "Auto");
        std::string listed;
        for (const auto& size : supported) {
          const std::string token = DisplayModeToken(size);
          choices.emplace_back(token, ResolutionChoiceLabel(size));
          listed += (listed.empty() ? "" : ", ") + ResolutionChoiceLabel(size);
        }
        REXLOG_INFO("settings_menu: Resolution offers {} size(s): {}", choices.size(), listed);
      }
    }
    // The renderer row is the one choice a player can be misled by: what it offers is decided by the
    // build's backends (RBBLITZ_BACKENDS) plus the machine's Vulkan loader, so a single-choice row is
    // either a one-backend build or a machine with no loader - both worth saying out loud, in the
    // same words Controller Settings prints for the payload (`payload renderer backends:`).
    if (const auto renderers = configuration.choices.find("graphics.renderer");
        renderers != configuration.choices.end() && !renderers->second.empty()) {
      std::string listed;
      for (const auto& choice : renderers->second) {
        listed += (listed.empty() ? "" : ", ") + choice.second;
      }
      REXLOG_INFO("settings_menu: Renderer offers {}: {}", renderers->second.size(), listed);
    }
    for (const auto& binding : ultimate_adapter::RowBindings()) {
      const auto found = provider.choices.find(std::string(binding.variable));
      if (found != provider.choices.end()) {
        auto& choices = configuration.choices[std::string(binding.token)];
        choices.clear();
        for (const auto& token : found->second) choices.emplace_back(token, token);
      }
    }
    // The demo (trial) target is offered by name: the recorded trial-boot fault is a separate
    // matter and no longer a reason to omit the choice (owner decision, 2026-10-13). Trial still
    // routes through the same translation as always - `--license_mask=0` and no Ultimate mode.
    configuration.choices["general.target"] = {{"common", "Rock Band Blitz"}};
    if (provider.payload_present)
      configuration.choices["general.target"].emplace_back("ultimate", "Rock Band Blitz Ultimate");
    configuration.choices["general.target"].emplace_back("demo", "Rock Band Blitz Demo");
    if (provider.Active() && boot->service()->store().document().values.contains(SettingId::kUnlockAll))
      configuration.boot_booleans["unlockall"] = boot->service()->EffectiveValue(SettingId::kUnlockAll).boolean;
    // The two DLC rows the owner asked for (2026-10-14): R9's own setting is visible again, and the
    // Play Current Song row is wired to it - and to the pool, because a menu whose background music
    // is the title's own has no song to start. Both states are said on the row itself (the settings
    // screen draws a disabled row as `<label>: <reason>`), so nothing here is a silent no-op.
    if (!menu_music_on) {
      configuration.disabled["general.play_current_song"] = "Needs DLC Menu Songs";
    } else if (!menu_music_pool) {
      configuration.disabled["general.dlc_menu_songs"] = "No DLC songs found";
      configuration.disabled["general.play_current_song"] = "No DLC songs found";
    }
    configuration.play_current_song = play_current_row;
    configuration.menu_music_songs = menu_music_songs;
    for (const auto& row : schema::MenuRows()) {
      configuration.menu_rows.emplace_back(row.token, row.label);
    }
    ui::screen::ConfigureMenu(std::move(configuration));
  }
  MenuRecords records;
  if (!BuildMenuRecords(read, {locale_set.begin(), locale_set.end()}, &records, &error,
                        ultimate::Provider().Active())) return fail();
  Bytes part;
  std::string part_path;
  // The mounted part's own bytes, so the extended archive can stay a *one-part* archive: the title
  // only ever reads single-part archives, and a second part is what hung the Ultimate boot.
  Bytes mounted_part;
  if (!patch.parts.empty() && patch.parts.size() == patch.part_sizes.size()) {
    for (const std::string& name : patch.parts) {
      Bytes one;
      if (!Read(vfs, name, 0, 0, &one, &error)) return fail();
      mounted_part.insert(mounted_part.end(), one.begin(), one.end());
    }
  }
  const Bytes header = ExtendMenuArchive(patch, records, mounted_part, &part_path, &part);
  // Only generated records live in the cache root, and when a payload is installed the part also
  // carries a byte-for-byte copy of the mounted part those records were appended to. Installed game
  // and provider files on disk stay read-only either way: nothing here is ever written back.
  const auto root = runtime->cache_root() / "settings-menu" /
                    std::to_string(platform::ThisProcessId());
  if (!WriteBytes(root / part_path, part, &error) ||
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
