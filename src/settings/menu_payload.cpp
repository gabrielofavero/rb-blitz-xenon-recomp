// SPDX-License-Identifier: GPL-2.0-only
#include "settings/menu_payload.h"

#include <algorithm>
#include <numeric>
#include "store/store_payload.h"
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

namespace {

// The credit sections the project prefixes, and the rows it puts there (owner request,
// 2026-10-19; the third-party thanks added 2026-10-21). The retail DTA and the Ultimate
// payload's own DTA are told apart by the heading of their first section; a file whose heading
// is neither is a different document and is left alone.
constexpr std::string_view kRetailCreditsHeading = "Core Development Team";
constexpr std::string_view kUltimateCreditsHeading =
    "Rock Band Blitz Ultimate 2.0 Development Team";
constexpr std::string_view kRecompilationHeading = "Recompilation Project";
constexpr std::string_view kRecompilationUltimateHeading =
    "Recompilation Project & Ultimate 2.0 Mod";
constexpr std::string_view kThanksHeading = "Thanks To";
constexpr std::string_view kDeveloperRole = "Developer";
constexpr std::string_view kRecompilationDeveloperRole = "Recompilation Developer";
constexpr std::string_view kUltimateDeveloperRole = "Ultimate 2.0 Developer";
constexpr std::string_view kRecompilationName = "Gabriel Fávero";
constexpr std::string_view kUltimateName = "Synchrony";

// Everything the build stands on, under its own heading (owner request, 2026-10-21): the
// recompiler it runs on and the research behind it, the Rock Band 3 recompilation whose hooks
// are ported here, and the catalogue and asset tools that carry the content it shows. One table,
// read by both credit files, so the two cannot drift. Both columns stay well inside what the
// retail screen already draws - its own longest role is 53 characters, its longest name 36.
struct CreditRow {
  std::string_view role;
  std::string_view name;
};
constexpr CreditRow kThanksRows[] = {
    {"Recompiler", "ReXGlue Team"},
    {"Static Recompiler", "XenonRecomp"},
    {"Emulation Research", "Xenia Project"},
    {"Rock Band 3 Recomp", "band3_recomp"},
    {"Customs Store", "RhythmVerse"},
    {"Asset Tooling", "MiloLib"},
};

bool IsArrayWithSymbol(const Node& node, std::string_view symbol) {
  return node.tag == ui::dtb::kArray && !node.kids.empty() &&
         node.kids[0].tag == ui::dtb::kSymbol &&
         std::string_view(node.kids[0].text) == symbol;
}

bool HeadingIs(const Node& node, std::string_view heading) {
  return IsArrayWithSymbol(node, "heading") && node.kids.size() >= 2 &&
         node.kids[1].tag == ui::dtb::kString &&
         std::string_view(node.kids[1].text) == heading;
}

bool TitleNameIs(const Node& node, std::string_view role, std::string_view name) {
  return IsArrayWithSymbol(node, "title_name") && node.kids.size() >= 3 &&
         node.kids[1].tag == ui::dtb::kString &&
         std::string_view(node.kids[1].text) == role && node.kids[2].tag == ui::dtb::kString &&
         std::string_view(node.kids[2].text) == name;
}

Node HeadingNode(std::string_view heading) {
  return Node::Array({Node::Symbol("heading"), Node::String(heading)});
}

Node TitleNameNode(std::string_view role, std::string_view name) {
  return Node::Array({Node::Symbol("title_name"), Node::String(role), Node::String(name)});
}

bool IsBlankRoot(const Node& node) { return node.tag == ui::dtb::kArray && node.kids.empty(); }

// The third-party thanks' heading and rows. The caller appends the one blank row retail puts
// between two sections - unless the section that follows already opens with blanks (the
// Ultimate payload's file carries sixteen).
std::vector<Node> ThanksSection() {
  std::vector<Node> nodes{HeadingNode(kThanksHeading)};
  for (const CreditRow& row : kThanksRows) nodes.push_back(TitleNameNode(row.role, row.name));
  return nodes;
}

// The first non-empty root, i.e. the first rendered section. The leading roots of both shipped
// files are blank spacer rows.
size_t FirstSection(const std::vector<Node>& roots) {
  for (size_t i = 0; i < roots.size(); ++i) {
    if (roots[i].tag == ui::dtb::kArray && !roots[i].kids.empty()) return i;
  }
  return roots.size();
}

}  // namespace

bool AddRecompilationCredits(std::vector<Node>* roots, bool ultimate, std::string* why) {
  if (roots == nullptr) {
    if (why) *why = "no roots";
    return false;
  }
  const size_t first = FirstSection(*roots);
  if (first == roots->size()) {
    if (why) *why = "the credits DTA carries no sections";
    return false;
  }
  if (!ultimate) {
    if (!HeadingIs((*roots)[first], kRetailCreditsHeading)) {
      if (why) *why = "the first credits section is not the retail game's";
      return false;
    }
    // The section goes where the official credits begin, after the file's leading blank rows,
    // and one blank row separates each section from the next: the project's own row, then the
    // third-party thanks, then "Core Development Team".
    std::vector<Node> added = {
        HeadingNode(kRecompilationHeading),
        TitleNameNode(kDeveloperRole, kRecompilationName),
        Node::Array({}),
    };
    const std::vector<Node> thanks = ThanksSection();
    added.insert(added.end(), thanks.begin(), thanks.end());
    added.push_back(Node::Array({}));
    roots->insert(roots->begin() + static_cast<std::ptrdiff_t>(first), added.begin(),
                  added.end());
    return true;
  }

  if (!HeadingIs((*roots)[first], kUltimateCreditsHeading)) {
    if (why) *why = "the first credits section is not the Ultimate payload's";
    return false;
  }
  // The Ultimate payload's file has exactly one row after that heading: "Only Dev"/"Synchrony".
  // Check it before changing anything, so a newer payload whose row moved is left untouched
  // rather than half-rewritten.
  size_t row = first + 1;
  while (row < roots->size() && !IsArrayWithSymbol((*roots)[row], "title_name")) ++row;
  if (row == roots->size() || !TitleNameIs((*roots)[row], "Only Dev", kUltimateName)) {
    if (why) *why = "the Ultimate credits section does not carry its one 'Only Dev' row";
    return false;
  }
  (*roots)[first].kids[1].text = std::string(kRecompilationUltimateHeading);
  // The two owner rows, then the same third-party thanks the retail file gets.
  std::vector<Node> rows = {
      TitleNameNode(kRecompilationDeveloperRole, kRecompilationName),
      TitleNameNode(kUltimateDeveloperRole, kUltimateName),
      Node::Array({}),
  };
  const std::vector<Node> thanks = ThanksSection();
  rows.insert(rows.end(), thanks.begin(), thanks.end());
  if (row + 1 >= roots->size() || !IsBlankRoot((*roots)[row + 1])) rows.push_back(Node::Array({}));
  roots->erase(roots->begin() + static_cast<std::ptrdiff_t>(row));
  roots->insert(roots->begin() + static_cast<std::ptrdiff_t>(row), rows.begin(), rows.end());
  return true;
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
                      bool cached_dlc_list, bool refresh_row_active,
                      std::vector<std::string>* notes,
                      const std::vector<std::string>& hidden_menu_rows) {
  namespace screen = ui::screen;
  MenuRecords built;
  auto note = [notes](std::string line) {
    if (notes != nullptr) notes->push_back(std::move(line));
  };
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
        //
        // The restored Download Content row (owner request, 2026-10-20) must be cloned *before*
        // that call: it copies the shipped `splash_dlc` action verbatim, which R7 is about to
        // replace with the refresh body.
        if (screen::CurrentMenuConfiguration().download_content_row) {
          std::string why;
          if (!screen::AddDownloadContentRow(&roots, &why)) {
            note("the Download Content row was refused: " + why);
          }
        }
        SetRefreshRowAction(&roots, refresh_row_active);
        // The Play Current Song entry (owner feature, 2026-10-14), when the boot configured it:
        // the row right after PLAY, and the case that starts the song the menu is playing. A boot
        // with no pool refuses, and that refusal is *not* fatal - a menu without the row is exactly
        // what "no DLC song is playing" means. (No log here: this file is also compiled into the
        // SDK-free menu_payload test; settings_payload.cpp reports the configuration it passed in.)
        if (screen::CurrentMenuConfiguration().play_current_song) {
          std::string why;
          if (!screen::AddPlayCurrentSongRow(&roots, &why)) {
            note("the Play Current Song row was refused: " + why);
          }
        }
        // The RhythmVerse Customs entry (plan P1): built only when the store's master switch was on
        // for this boot, so a store the player turned off never draws a row that would refuse the
        // press. A refusal here is not fatal - the row simply is not there.
        if (screen::CurrentMenuConfiguration().rhythmverse_row) {
          std::string why;
          if (!screen::AddRhythmVerseRow(&roots, &why)) {
            note("the RhythmVerse Customs row was refused: " + why);
          }
        }
        // The R5 hide decision, baked into the record this payload generates. The in-flight twin
        // (menu_filter::HideRows) cannot deliver it for this file when it spans two ark reads
        // (measured 2026-10-20), and the rows it was meant to hide are what pushed the store entry
        // off the visible page. Applied last, so the anchors above still find the shipped rows.
        if (!hidden_menu_rows.empty()) {
          std::string why;
          if (!screen::HideMenuRows(&roots, hidden_menu_rows, &why)) {
            note("hiding the main-menu rows was refused: " + why);
          }
        }
        return screen::RerouteRow(&roots, screen::kMainMenuRow, screen::MainMenuRouteBody(), error);
      }) || !edit("ui/pause/gen/pause.dtb", [&](auto& roots) {
        if (!screen::RerouteRow(&roots, screen::kPauseMenuRow,
            {Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                             Node::Symbol(screen::kRootPauseScreen)})}, error)) return false;
        // P7: the in-song list's Controls row is retail's own `pause_controls` case, which opened
        // retail's controller page. It now opens our Controls page (the same jump shape retail's
        // case already used, so the measured in-song transition is unchanged); Default Presets
        // inside it is the row that reaches retail's page in song.
        return screen::RerouteRow(&roots, "pause_controls",
            {Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                             Node::Symbol("settings_controls_pause_screen")})}, error);
      }) || !edit("ui/av_options/gen/av_options.dtb", [&](auto& roots) {
        // The retail Audio page is reachable from our A/V page as "Audio", but its own scene
        // draws the AUDIO/VIDEO title and places its four rows one row lower than the pages above
        // it. Both are corrected on entry, by the time the panel's `{enter}` runs and its scene is
        // loaded (a statement in our own panel runs before `{ui goto_screen ...}`, when the retail
        // scene is not loaded yet - measured 2026-10-19).
        return screen::AppendPanelEnterStatements(&roots, "av_options_panel",
                                                  screen::AudioPageEnterStatements(), error);
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
      // The store's heading (RhythmVerse plan P3 §1.8): with the store on, its title names the
      // catalogue it shows instead of retail's "MUSIC STORE". Replaced whatever the locale says,
      // because a translated heading is still retail's, not ours.
      if (screen::CurrentMenuConfiguration().rhythmverse_row) {
        std::string why;
        if (!store::SetStoreHeading(&roots, store::kStoreHeadingText, &why)) {
          note("the store heading was refused: " + why);
        }
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
  // The recompilation project's credit line, added to the credits DTA the title loads. The
  // record is replaced under its own path in the generated archive, which the overlay prefers,
  // so neither the retail archive nor the Ultimate payload is touched. Which file is the
  // effective one is decided by what is installed: the Ultimate payload ships its own credits
  // DTA, while the base game's `config/gen/sync_keep.dtb` names the locale record.
  {
    std::vector<uint8_t> credits_bytes;
    std::vector<Node> credits_roots;
    ui::dtb::RootHeader credits_header;
    std::string probe;
    const bool ultimate_credits = read("ulti/gen/credits.dtb", &credits_bytes, &probe);
    const std::string credits_path =
        ultimate_credits ? "ulti/gen/credits.dtb" : "ui/locale/gen/locale_credits_keep.dtb";
    std::string why;
    if (ultimate_credits || read(credits_path, &credits_bytes, &why)) {
      if (!ui::dtb::Parse(credits_bytes.data(), credits_bytes.size(), &credits_roots,
                          &credits_header, &why)) {
        // Not fatal: a boot that cannot parse the credits DTA has bigger problems than the
        // project's credit line, and this payload is not the thing that should refuse it.
        credits_roots.clear();
      } else if (AddRecompilationCredits(&credits_roots, ultimate_credits, &why)) {
        built[credits_path] = ui::dtb::Compile(credits_roots, credits_header);
      }
    }
  }
  // P2's local-server spike (RhythmVerse plan §5/P2): with a root configured, the retail
  // store's netcache definition is rewritten to fetch its metadata from a local (host-file)
  // server (src/store/store_payload.h). Not fatal in either direction: a boot that cannot read or
  // rewrite the record keeps the retail server, which is exactly the behaviour every ordinary
  // boot has, and the reason is reported by the caller's log.
  const std::string& local_store_root = screen::CurrentMenuConfiguration().store_local_server_root;
  if (!local_store_root.empty()) {
    std::vector<uint8_t> store_bytes;
    std::vector<Node> store_roots;
    ui::dtb::RootHeader store_header;
    std::string why;
    if (read("config/gen/store.dtb", &store_bytes, &why) &&
        ui::dtb::Parse(store_bytes.data(), store_bytes.size(), &store_roots, &store_header,
                       &why) &&
        store::PointNetcacheAtLocalServer(&store_roots, local_store_root, &why)) {
      built["config/gen/store.dtb"] = ui::dtb::Compile(store_roots, store_header);
    } else {
      note("the store's local-server rewrite was refused: " + why);
    }
    // The manager is created once, at boot, from a definition it looks up in the engine's syscfg
    // (`{net_cache_mgr init {find syscfg store netcache_init}}`). That lookup is unmeasured, so the
    // same switch also hands the manager the record's own rewritten definition — the shipped
    // shape by construction, not a literal that guesses what the native manager reads.
    const ui::dtb::Node* definition = store::FindNetcacheInit(store_roots);
    std::vector<uint8_t> ui_bytes;
    std::vector<Node> ui_roots;
    ui::dtb::RootHeader ui_header;
    std::string ui_why;
    if (definition != nullptr && read("ui/gen/ui.dtb", &ui_bytes, &ui_why) &&
        ui::dtb::Parse(ui_bytes.data(), ui_bytes.size(), &ui_roots, &ui_header, &ui_why) &&
        store::PointUiNetcacheAtLocalServer(&ui_roots, *definition, &ui_why)) {
      built["ui/gen/ui.dtb"] = ui::dtb::Compile(ui_roots, ui_header);
    } else {
      if (definition == nullptr) ui_why = "the store record carries no netcache_init";
      note("the store's netcache-init rewrite was refused: " + ui_why);
    }
  }
  // P3's data lane (RhythmVerse plan §5/P3). With the store's master switch on, the retail store's
  // own DTA records are edited so the retail screens can show our catalogue:
  //
  //   * `store.dtb` - the commerce bypass (the panel starts its first request only from its own
  //     `on_init_commerce_success`, which offline never fires) and the price footer, which retail
  //     shows on the Xbox branch of the panel's `enter` (§2.12).
  //   * `store_info.dtb` - the details page's two cost labels.
  //
  // Both are refusals-not-errors: a boot that cannot read a record keeps retail's behaviour and
  // the reason is reported. The P2 spike's netcache rewrite above stays gated on its own cvar and
  // is independent of these.
  if (screen::CurrentMenuConfiguration().rhythmverse_row) {
    std::vector<uint8_t> store_ui_bytes;
    std::vector<Node> store_ui_roots;
    ui::dtb::RootHeader store_ui_header;
    std::string store_ui_why;
    if (read("ui/store/gen/store.dtb", &store_ui_bytes, &store_ui_why) &&
        ui::dtb::Parse(store_ui_bytes.data(), store_ui_bytes.size(), &store_ui_roots,
                       &store_ui_header, &store_ui_why)) {
      std::string why;
      if (!store::AddStoreEntryBypass(&store_ui_roots, &why)) {
        note("the store's commerce-bypass edit was refused: " + why);
      }
      why.clear();
      if (!store::HideStorePriceChrome(&store_ui_roots, &why)) {
        note("hiding the store's price footer was refused: " + why);
      }
      why.clear();
      // The details panel's `get_data (rank <part>)` is a two-component path the offer walk cannot
      // resolve (store_payload.h), so its loop reads the record's single-component `rank_<part>`
      // keys instead.
      if (!store::UnrollOfferRanks(&store_ui_roots, &why)) {
        note("unrolling the details panel's difficulty read was refused: " + why);
      }
      why.clear();
      // The details panel's own `art_loaded` message never fires for our synchronous delivery, so
      // the material is linked right after `load_art` returns (store_payload.h).
      if (!store::LinkAlbumArt(&store_ui_roots, &why)) {
        note("linking the details page's album art was refused: " + why);
      }
      built["ui/store/gen/store.dtb"] = ui::dtb::Compile(store_ui_roots, store_ui_header);
    } else {
      note("the store's screen record could not be read: " + store_ui_why);
    }
    std::vector<uint8_t> info_bytes;
    std::vector<Node> info_roots;
    ui::dtb::RootHeader info_header;
    std::string info_why;
    if (read("ui/store/gen/store_info.dtb", &info_bytes, &info_why) &&
        ui::dtb::Parse(info_bytes.data(), info_bytes.size(), &info_roots, &info_header,
                       &info_why)) {
      std::string why;
      if (!store::HideStoreInfoPriceChrome(&info_roots, &why)) {
        note("hiding the details page's price was refused: " + why);
      }
      why.clear();
      if (!store::AlwaysShowOfferDetails(&info_roots, &why)) {
        note("opening the details page for a free offer was refused: " + why);
      }
      why.clear();
      if (!store::HidePreviewButton(&info_roots, &why)) {
        note("hiding the details page's preview row was refused: " + why);
      }
      why.clear();
      // The native `set_offer` enumeration reports an error for a served offer, so the browser's A
      // press populates the details screen directly (store_payload.h).
      if (!store::ShowOfferDetailsDirectly(&info_roots, &why)) {
        note("the details page's direct route was refused: " + why);
      }
      built["ui/store/gen/store_info.dtb"] = ui::dtb::Compile(info_roots, info_header);
    } else {
      note("the store's details record could not be read: " + info_why);
    }
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
