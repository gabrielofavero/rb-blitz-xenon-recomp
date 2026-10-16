// SPDX-License-Identifier: GPL-2.0-only
#include "settings/menu_payload.h"

#include <algorithm>
#include <numeric>
#include "ui/settings_screen.h"
#include "ui/menu_options.h"

namespace rb_blitz::settings {
namespace {
using ui::dtb::Node;
void U32(std::vector<uint8_t>& out, uint32_t n) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(n >> (8 * i)));
}
void U64(std::vector<uint8_t>& out, uint64_t n) {
  U32(out, static_cast<uint32_t>(n)); U32(out, static_cast<uint32_t>(n >> 32));
}
}

bool SetDiscoveryCacheLabel(std::vector<ui::dtb::Node>* roots, bool cache_hit) {
  if (!cache_hit) return false;
  std::string why;
  return ui::screen::ReplaceLocaleEntry(roots, menu_options::kEnumeratingContentKey,
      menu_options::kDiscoveringContentLabel, menu_options::kLoadingCacheLabel, &why);
}

bool SetRefreshRowLabel(std::vector<ui::dtb::Node>* roots, bool refresh_row_active) {
  if (!refresh_row_active) return false;
  std::string why;
  // Only the locales that spell the row's shipped label are relabelled; a translation of "Download
  // Content" is left alone (the same scope the in-flight relabel always had).
  return ui::screen::ReplaceLocaleEntry(roots, menu_options::kDlcRowKey,
      menu_options::kDlcRowLabel, menu_options::kDlcRowRefreshLabel, &why);
}

// The refresh row's action as the payload's copy of the splash record carries it. The two
// statements and their order are menu_options.h's (`kRefreshRowDiscoverScreen`,
// `kRefreshSentinelPath`), shared with the byte-budgeted in-flight edit so the two can never
// disagree about what the press does.
std::vector<Node> RefreshRowBody() {
  return {Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                         Node::Symbol(menu_options::kRefreshRowDiscoverScreen)}),
          Node::Command({Node::Symbol("file_exists"),
                         Node::String(menu_options::kRefreshSentinelPath)})};
}

// The `splash_dlc` case wherever the switch that dispatches it lives, in these roots. The walk
// mirrors menu_options::FindSwitchCase, which owns the same idea for the byte-level twin: the
// case's first element is the row's name, which is what tells it apart from the option array
// that lists the same name somewhere else.
Node* FindRefreshRowCase(Node* node) {
  if (node->tag == ui::dtb::kArray && node->kids.size() >= 2 &&
      node->kids[0].tag == ui::dtb::kSymbol && node->kids[0].text == menu_options::kDlcRowKey) {
    return node;
  }
  for (Node& kid : node->kids) {
    if (Node* found = FindRefreshRowCase(&kid)) return found;
  }
  return nullptr;
}

bool ContainsString(const Node& node, const std::string_view text) {
  if (node.tag == ui::dtb::kString && node.text == text) return true;
  for (const Node& kid : node.kids) {
    if (ContainsString(kid, text)) return true;
  }
  return false;
}

bool SetRefreshRowAction(std::vector<ui::dtb::Node>* roots, bool refresh_row_active) {
  if (!refresh_row_active || roots == nullptr) return false;
  // The in-flight twin's guard, for the same reason: a case that already carries the call is left
  // alone rather than rewritten a second time.
  for (Node& root : *roots) {
    if (Node* row = FindRefreshRowCase(&root)) {
      for (size_t i = 1; i < row->kids.size(); ++i) {
        if (ContainsString(row->kids[i], menu_options::kRefreshSentinelPath)) return false;
      }
      break;
    }
  }
  std::string why;
  return ui::screen::RerouteRow(roots, menu_options::kDlcRowKey, RefreshRowBody(), &why);
}

bool BuildMenuRecords(const RecordReader& read, const std::vector<std::string>& locales,
                      MenuRecords* records, std::string* error, bool merge_provider,
                      bool cached_dlc_list, bool refresh_row_active) {
  namespace screen = ui::screen;
  MenuRecords built;
  auto edit = [&](const std::string& path, auto transform) {
    std::vector<uint8_t> bytes;
    std::vector<Node> roots;
    ui::dtb::RootHeader header;
    if (!read(path, &bytes, error) ||
        !ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, error) ||
        !transform(roots)) return false;
    built[path] = ui::dtb::Compile(roots, header);
    return true;
  };
  if (!edit(screen::kDefinitionRecord, [&](auto& roots) {
        const auto added = screen::EarlyDefinitionRoots();
        roots.insert(roots.end(), added.begin(), added.end());
        return true;
      }) || !edit("ui/gen/ui.dtb", [&](auto& roots) {
        if (!screen::AddDeferredScreens(&roots, error)) return false;
        for (const auto& panel : screen::PanelNames()) {
          if (!screen::AddPanelLoad(&roots, panel, error)) return false;
        }
        return true;
      }) || !edit("ui/splash/gen/splash.dtb", [&](auto& roots) {
        if (merge_provider) {
          // A compatible provider's own settings entry is *removed*: its pages are all reachable
          // from Help & Options now, so keeping a second button to the same things is one button
          // too many (owner decision, 2026-10-13). A payload that never had the row refuses, and
          // that refusal is deliberately not fatal - there is nothing to remove.
          std::string ignored;
          screen::RemoveRow(&roots, screen::kProviderMenuRow, &ignored);
        }
        // R7's own row (2026-10-15): this copy is the one the overlay prefers, so the action is
        // built here rather than left to menu_filter's in-flight edit, which cannot be relied on
        // for a mounted record (measured: the press ran the title's own store body while every
        // read that carried the row was rewritten). A record without that case is not an error -
        // the in-flight edit is still armed for the retail copy.
        SetRefreshRowAction(&roots, refresh_row_active);
        // The Play Current Song entry (owner feature, 2026-10-14), when the boot configured it:
        // the row right after PLAY, and the case that starts the song the menu is playing. A boot
        // with no pool refuses, and that refusal is *not* fatal - a menu without the row is exactly
        // what "no DLC song is playing" means. (No log here: this file is also compiled into the
        // SDK-free menu_payload test; settings_payload.cpp reports the configuration it passed in.)
        if (screen::CurrentMenuConfiguration().play_current_song) {
          std::string why;
          screen::AddPlayCurrentSongRow(&roots, &why);
        }
        return screen::RerouteRow(&roots, screen::kMainMenuRow, screen::MainMenuRouteBody(), error);
      }) || !edit("ui/pause/gen/pause.dtb", [&](auto& roots) {
        return screen::RerouteRow(&roots, screen::kPauseMenuRow,
            {Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                             Node::Symbol(screen::kRootPauseScreen)})}, error);
      })) return false;
  for (const auto& locale : locales) {
    if (!edit(locale, [&](auto& roots) {
      SetDiscoveryCacheLabel(&roots, cached_dlc_list);
      SetRefreshRowLabel(&roots, refresh_row_active);
      // Retail's own strings this surface restyles (the AV page's two checkboxes). A locale that
      // does not carry them, or carries a translation, is left alone by the same call.
      for (const auto& entry : screen::LocaleOverrides()) {
        std::string why;
        screen::ReplaceLocaleEntry(&roots, entry.key, entry.from, entry.to, &why);
      }
      for (const auto& [key, text] : screen::Labels()) {
        std::string why;
        if (!screen::AddLocaleEntry(&roots, key, text, &why) &&
            why != "the locale already has that key") {
          if (error) *error = why;
          return false;
        }
      }
      return true;
    })) return false;
  }
  if (!read(screen::kSceneSourceRecord, &built[screen::kSceneRecord], error)) return false;
  built[screen::kPauseSceneRecord] = built[screen::kSceneRecord];
  if (!read("ui/pause/gen/confirmation_message.milo_xbox",
            &built["ui/pause/gen/settings_confirmation.milo_xbox"], error)) return false;
  built["ui/pause/gen/settings_confirmation_pause.milo_xbox"] =
      built["ui/pause/gen/settings_confirmation.milo_xbox"];
  *records = std::move(built);
  return true;
}

// Append `records` to the mounted patch archive, and return the header to write.
//
// **One part, always - this is the whole point of `mounted_part`.** Every archive this title
// ships or reads declares exactly one part (`game/gen/main_xbox.hdr`: `gen/main_xbox_0.ark`;
// `game/ultimate/gen/patch_xbox.hdr`: `gen/patch_xbox_0.ark`, measured 2026-10-12 with
// `scripts/hmx_ark.py list`). Growing the ark by *adding* a second part - which is what this
// function used to do - produced the title's only two-part ark, and an Ultimate boot that read the
// generated part forever and never presented (docs/settings/status.md (part 2, the black startup)).
// So when the mounted index has a single part, its bytes are carried into the new part unchanged
// and our records are appended after them: the existing entries keep their offsets, the part list
// keeps its one entry (same name, new size), and the guest sees the shape it has always seen.
//
// `part_path` receives the record path the caller must write `part` to. It is the mounted part's
// own name in that case, which is also what makes the union prefer it: the settings overlay's
// payload side then serves the extended ark where the base used to serve the provider's copy.
std::vector<uint8_t> ExtendMenuArchive(ultimate_adapter::ArkIndex index, const MenuRecords& records,
                                       const std::vector<uint8_t>& mounted_part,
                                       std::string* part_path, std::vector<uint8_t>* part) {
  part->clear();
  // Extending in place is only well-defined for the single-part mounted archive; anything else
  // falls back to appending a part of our own (the previous behaviour).
  const bool extend_in_place = index.parts.size() == 1 &&
                               mounted_part.size() == index.part_sizes.front();
  if (extend_in_place) {
    *part = mounted_part;
  }
  // The ark offset of the first byte our part owns: the whole mounted part when extending, and the
  // whole mounted ark when a new part is appended.
  const uint64_t ark_prefix =
      std::accumulate(index.part_sizes.begin(), index.part_sizes.end(), uint64_t{});
  const size_t preloaded = part->size();
  for (const auto& [path, bytes] : records) {
    part->resize((part->size() + 15) & ~size_t(15));
    const ultimate_adapter::ArkEntry added{path, ark_prefix + (part->size() - preloaded),
                                          bytes.size(), 0};
    auto old = std::find_if(index.entries.begin(), index.entries.end(),
                           [&](const auto& entry) { return entry.path == path; });
    if (old == index.entries.end()) index.entries.push_back(added);
    else *old = added;
    part->insert(part->end(), bytes.begin(), bytes.end());
  }
  if (extend_in_place) {
    index.part_sizes.front() = static_cast<uint32_t>(part->size());
    *part_path = index.parts.front();
  } else {
    index.parts.push_back("gen/settings_xbox_0.ark");
    index.part_sizes.push_back(static_cast<uint32_t>(part->size()));
    index.checksums.push_back(0);
    *part_path = "gen/settings_xbox_0.ark";
  }
  std::vector<uint8_t> body;
  U32(body, 6);
  if (index.extra.size() != 20) index.extra.assign(20, 0xFF);
  body.insert(body.end(), index.extra.begin(), index.extra.end());
  const uint32_t count = static_cast<uint32_t>(index.parts.size());
  U32(body, count); U32(body, count);
  for (uint32_t size : index.part_sizes) U32(body, size);
  U32(body, count);
  for (const auto& path : index.parts) {
    U32(body, static_cast<uint32_t>(path.size()));
    body.insert(body.end(), path.begin(), path.end());
  }
  U32(body, count);
  for (uint32_t checksum : index.checksums) U32(body, checksum);
  std::vector<uint8_t> strings{0};
  std::vector<uint32_t> offsets;
  std::map<std::string, uint32_t> interned;
  auto intern = [&](const std::string& text) {
    auto found = interned.find(text);
    if (found != interned.end()) return found->second;
    const auto id = static_cast<uint32_t>(offsets.size());
    offsets.push_back(static_cast<uint32_t>(strings.size()));
    strings.insert(strings.end(), text.begin(), text.end()); strings.push_back(0);
    interned[text] = id;
    return id;
  };
  std::vector<uint8_t> table;
  U32(table, static_cast<uint32_t>(index.entries.size()));
  for (const auto& entry : index.entries) {
    const auto slash = entry.path.rfind('/');
    U64(table, entry.offset);
    U32(table, intern(slash == std::string::npos ? entry.path : entry.path.substr(slash + 1)));
    U32(table, slash == std::string::npos ? uint32_t(-1) : intern(entry.path.substr(0, slash)));
    U32(table, static_cast<uint32_t>(entry.size)); U32(table, static_cast<uint32_t>(entry.inflate));
  }
  U32(body, static_cast<uint32_t>(strings.size()));
  body.insert(body.end(), strings.begin(), strings.end());
  U32(body, static_cast<uint32_t>(offsets.size()));
  for (uint32_t at : offsets) U32(body, at);
  body.insert(body.end(), table.begin(), table.end());
  std::vector<uint8_t> encrypted;
  uint32_t key = 0x5A5A5A5A;
  U32(encrypted, key);
  for (uint8_t byte : body) {
    key = static_cast<uint32_t>((uint64_t(key) * 16807) % 2147483647);
    encrypted.push_back(byte ^ static_cast<uint8_t>(key));
  }
  return encrypted;
}
}
