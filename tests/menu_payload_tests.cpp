// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/settings/menu_payload.cpp, the archive half of the generated Help & Options
// menu (P5b/P11). SDK-free: the fixtures are an index and bytes, and the assertions go through the
// project's own v6 reader (`settings/ultimate_adapter.h`), which is also what the runtime uses.
//
// The property that matters, and the reason this file exists: **the extended archive keeps the
// shape the title can read.** Every archive Rock Band Blitz ships or reads declares exactly one
// part (measured 2026-10-12 with `scripts/assets/hmx_ark.py list`: `main_xbox.hdr` -> one part,
// `patch_xbox.hdr` -> one part). Extending the mounted patch by *adding* a second part - which is
// what this code used to do - produced an Ultimate boot that re-read the generated part forever and
// never presented (docs/settings/status.md (part 2, the black startup)). So: one part in, one part
// out, every record inside the part it belongs to, and the mounted bytes carried verbatim.

#include "check.h"
#include "windows_test_crash.h"

#include "settings/menu_payload.h"
#include "settings/ultimate_adapter.h"
#include "settings/ultimate_runtime.h"
#include "ui/menu_options.h"
#include "ui/input_font_scene.h"
#include "ui/milo_object_boundaries.h"
#include "ui/portrait_menu.h"
#include "ui/portrait_dialog_payload.h"
#include "input/calibration_text.h"
#include "input/inline_help.h"
#include <array>
#include "ui/settings_screen.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::test;
using rb_blitz::settings::ExtendMenuArchive;
using rb_blitz::settings::MenuRecords;
using rb_blitz::settings::ultimate_adapter::ArkEntry;
using rb_blitz::settings::ultimate_adapter::ArkIndex;
using rb_blitz::settings::ultimate_adapter::ReadArkIndex;

// check.h carries no string comparison, and a payload path is exactly the kind of value whose
// failure needs both sides printed.
void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

// The third-party thanks both credit files carry (owner request, 2026-10-21; the adapted layout
// art added 2026-10-29), as the credits screen renders them: the role column's text, then the
// name column's.
const std::vector<std::string> kThanksRows = {
    "Recompiler|ReXGlue Team",   "Static Recompiler|XenonRecomp",
    "Emulation Research|Xenia Project", "Engine References (RB3)|band3_recomp by ihatecompvir",
    "Customs Store|RhythmVerse", "Asset Tooling|MiloLib",
    "Button Layouts (Adapted)|Nicolae (XELU) Berbece, Paul Paun",
    "Controller Layouts (Adapted)|Nicolae (XELU) Berbece, Paul Paun",
    "Nintendo Switch 1/2 Layouts (Adapted)|zacksly",
    "Steam Deck Layouts (Adapted)|Official device artwork",
    "Quest 1/2 Layouts (Adapted)|Official device artwork",
};

// The rows under one `heading` of a credits document, up to the next blank root - the shape
// retail's own file uses to separate sections. A `title_name` row is "role|name".
std::vector<std::string> SectionRows(const std::vector<rb_blitz::ui::dtb::Node>& roots,
                                     const std::string& heading) {
  std::vector<std::string> rows;
  bool in_section = false;
  for (const auto& root : roots) {
    if (root.tag != rb_blitz::ui::dtb::kArray || root.kids.empty()) {
      in_section = false;
      continue;
    }
    if (root.kids[0].tag == rb_blitz::ui::dtb::kSymbol && root.kids[0].text == "heading") {
      in_section = root.kids.size() >= 2 && root.kids[1].text == heading;
      continue;
    }
    if (in_section && root.kids.size() >= 3 && root.kids[0].text == "title_name") {
      rows.push_back(root.kids[1].text + "|" + root.kids[2].text);
    }
  }
  return rows;
}

// Both credit files must name the same projects: one table in the implementation, one
// expectation here.
void CheckThanksRows(const std::vector<rb_blitz::ui::dtb::Node>& roots) {
  const std::vector<std::string> rows = SectionRows(roots, "Thanks To");
  CHECK_EQ(rows.size(), kThanksRows.size());
  for (size_t i = 0; i < rows.size() && i < kThanksRows.size(); ++i) {
    CheckStrEq(rows[i], kThanksRows[i]);
  }
}

// `mounted` describes the patch archive the boot already has: one part, one record that starts at 0.
ArkIndex MountedIndex(size_t part_size) {
  ArkIndex index;
  index.version = 6;
  index.extra.assign(20, 0xFF);
  index.parts = {"gen/patch_xbox_0.ark"};
  index.part_sizes = {static_cast<uint32_t>(part_size)};
  index.checksums = {0};
  index.entries.push_back({"ulti/gen/mod_settings.dtb", 0, 128, 0});
  return index;
}

// The v6 rule the reader implements: an entry's offset is absolute across the ark's parts, so it
// selects a part by accumulating the sizes. `scripts/assets/hmx_ark.py check_layout` is the same check.
bool FitsAnArkPart(const ArkIndex& index, const ArkEntry& entry, std::string* why) {
  uint64_t remaining = entry.offset;
  for (size_t i = 0; i < index.parts.size(); ++i) {
    if (remaining < index.part_sizes[i]) {
      if (remaining + entry.size > index.part_sizes[i]) {
        *why = entry.path + ": " + std::to_string(entry.size) + " bytes at 0x" +
               std::to_string(remaining) + " run past part " + std::to_string(i) + " (" +
               std::to_string(index.part_sizes[i]) + " bytes)";
        return false;
      }
      return true;
    }
    remaining -= index.part_sizes[i];
  }
  *why = entry.path + ": offset 0x" + std::to_string(entry.offset) + " lies past the last part";
  return false;
}

// Every index this code produces has to survive the project's own reader *and* keep every record
// inside a part - the reader refuses a header whose three counts disagree, so parsing at all is
// half the check.
void CheckLayout(const std::vector<uint8_t>& header, const std::vector<uint8_t>& part,
                 const char* what) {
  ArkIndex parsed;
  std::string error;
  if (!ReadArkIndex(header.data(), header.size(), &parsed, &error)) {
    Fail(__FILE__, __LINE__, std::string(what) + ": the generated header does not parse: " + error);
    return;
  }
  CHECK_EQ(parsed.parts.size(), 1u);
  CHECK_EQ(parsed.part_sizes.size(), 1u);
  CHECK_EQ(parsed.checksums.size(), 1u);
  CHECK_EQ(static_cast<size_t>(parsed.part_sizes.front()), part.size());
  for (const ArkEntry& entry : parsed.entries) {
    std::string why;
    CHECK_TRUE(FitsAnArkPart(parsed, entry, &why));
    if (!why.empty()) {
      Fail(__FILE__, __LINE__, std::string(what) + ": " + why);
    }
  }
}

void TestExtendingKeepsOnePart() {
  BeginCase("extending the mounted patch keeps it a one-part archive (the regression)");

  const size_t mounted_size = 4096;
  const ArkIndex mounted = MountedIndex(mounted_size);
  std::vector<uint8_t> mounted_bytes(mounted_size);
  for (size_t i = 0; i < mounted_bytes.size(); ++i) {
    mounted_bytes[i] = static_cast<uint8_t>(i * 7 + 3);  // distinguishable from our records
  }

  MenuRecords records;
  records["ui/gen/ui.dtb"] = std::vector<uint8_t>(97, 0x11);
  records["ui/options_menu/gen/settings_menu.milo_xbox"] = std::vector<uint8_t>(5000, 0x22);

  std::vector<uint8_t> part;
  std::string part_path;
  const std::vector<uint8_t> header =
      ExtendMenuArchive(mounted, records, mounted_bytes, &part_path, &part);

  // One part, and it is the mounted one: same record name, mounted bytes carried verbatim at the
  // head, only the appended records after them.
  CheckStrEq(part_path, "gen/patch_xbox_0.ark");
  // The mounted bytes, then the two records with the 16-byte alignment between them and nothing
  // else: 97 bytes of record, 15 bytes of padding, then 5000 bytes.
  CHECK_EQ(part.size(), mounted_size + 97 + 15 + 5000);
  CHECK_EQ(static_cast<size_t>(std::memcmp(part.data(), mounted_bytes.data(), mounted_size)), 0u);

  CheckLayout(header, part, "extended patch");

  // The record the payload already had keeps its offset and size: nothing that was readable before
  // moves, which is what makes carrying the bytes safe.
  ArkIndex parsed;
  std::string error;
  CHECK_TRUE(ReadArkIndex(header.data(), header.size(), &parsed, &error));
  const ArkEntry* kept = parsed.Find("ulti/gen/mod_settings.dtb");
  CHECK_TRUE(kept != nullptr);
  if (kept != nullptr) {
    CHECK_EQ(kept->offset, 0u);
    CHECK_EQ(kept->size, 128u);
  }
  const ArkEntry* added = parsed.Find("ui/options_menu/gen/settings_menu.milo_xbox");
  CHECK_TRUE(added != nullptr);
  if (added != nullptr) {
    CHECK_EQ(added->size, 5000u);
    // Past the mounted part's bytes - i.e. it really is in the appended region, not overwriting.
    CHECK_TRUE(added->offset >= mounted_size);
  }
}

void TestBaseArkGetsItsOwnPart() {
  BeginCase("with no mounted archive the records get a part of their own");

  ArkIndex empty;
  empty.version = 6;
  empty.extra.assign(20, 0xFF);

  MenuRecords records;
  records["ui/splash/gen/splash.dtb"] = std::vector<uint8_t>(64, 0x33);

  std::vector<uint8_t> part;
  std::string part_path;
  const std::vector<uint8_t> header = ExtendMenuArchive(empty, records, {}, &part_path, &part);

  CheckStrEq(part_path, "gen/settings_xbox_0.ark");
  CHECK_EQ(part.size(), 64u);
  CheckLayout(header, part, "base patch");

  ArkIndex parsed;
  std::string error;
  CHECK_TRUE(ReadArkIndex(header.data(), header.size(), &parsed, &error));
  const ArkEntry* added = parsed.Find("ui/splash/gen/splash.dtb");
  CHECK_TRUE(added != nullptr);
  if (added != nullptr) {
    CHECK_EQ(added->offset, 0u);  // the only part starts at the ark's own origin
  }
}

void TestMountedPartOfTheWrongSizeIsNotCarried() {
  BeginCase("a mounted part that does not match its recorded size is not carried");

  // Defensive: if the bytes on disk disagree with the index (a truncated download, a hand-edited
  // payload), carrying them would produce an archive whose kept entries point at the wrong data.
  // Appending a part of our own leaves the mounted archive's own entries untouched instead.
  ArkIndex mounted = MountedIndex(4096);
  const std::vector<uint8_t> short_bytes(1024, 0x44);

  MenuRecords records;
  records["ui/gen/ui.dtb"] = std::vector<uint8_t>(32, 0x55);

  std::vector<uint8_t> part;
  std::string part_path;
  const std::vector<uint8_t> header =
      ExtendMenuArchive(mounted, records, short_bytes, &part_path, &part);

  CheckStrEq(part_path, "gen/settings_xbox_0.ark");
  ArkIndex parsed;
  std::string error;
  CHECK_TRUE(ReadArkIndex(header.data(), header.size(), &parsed, &error));
  // Two parts, because the mounted one was left alone rather than half-replaced.
  CHECK_EQ(parsed.parts.size(), 2u);
  for (const ArkEntry& entry : parsed.entries) {
    std::string why;
    CHECK_TRUE(FitsAnArkPart(parsed, entry, &why));
  }
  const ArkEntry* kept = parsed.Find("ulti/gen/mod_settings.dtb");
  CHECK_TRUE(kept != nullptr);
  if (kept != nullptr) {
    CHECK_EQ(kept->offset, 0u);
    CHECK_EQ(kept->size, 128u);
  }
}

}  // namespace

namespace header_test {
using rb_blitz::ui::dtb::Node;
namespace dtb = rb_blitz::ui::dtb;
namespace settings = rb_blitz::settings;
using Tab = settings::LibraryHeader;
using Options = settings::LibraryHeaderOptions;
Node S(const char* text) { return Node::Symbol(text); }
Node C(std::vector<Node> nodes) { return Node::Command(std::move(nodes)); }
Node A(std::vector<Node> nodes) { return Node::Array(std::move(nodes)); }
constexpr const char* kScreens[] = {"song_select_screen", "store_menu_screen", "inbox_screen", "recommendation_screen"};
constexpr const char* kRecords[] = {"ui/song_select/gen/song_select.dtb", "ui/store/gen/store.dtb",
                                   "ui/inbox/gen/inbox.dtb", "ui/recommendations/gen/recommendations.dtb"};
const std::vector<std::vector<const char*>> kPanels = {{"song_select_panel"},
    {"store_menu_panel", "store_browser_panel"}, {"inbox_panel"}, {"recommendation_panel"}};
// Expected adjacency, including a disabled surface reached by some unrelated retail flow.
constexpr int kRight[8][4] = {{0,0,0,0}, {1,0,0,0}, {2,2,0,0}, {1,2,0,0},
                             {3,3,3,0}, {1,3,3,0}, {2,2,3,0}, {1,2,3,0}};
constexpr int kLeft[8][4] = {{0,0,0,0}, {1,0,1,1}, {2,0,0,2}, {2,0,1,2},
                            {3,0,0,0}, {3,0,1,1}, {3,0,0,2}, {3,0,1,2}};
Options State(int mask) { return {bool(mask & 1), bool(mask & 2), bool(mask & 4)}; }
Node Arrow(bool right) {
  return C({C({S("background_panel"), S("find"), S(right ? "arrow_rt.flow" : "arrow_lt.flow")}), S("activate")});
}
Node Transition(int destination) {
  return C({S("transition_to"), destination == 1 ?
      C({S("store_menu_screen"), S("get"), S("which_store_screen")}) : S(kScreens[destination])});
}
const Node* Method(const std::vector<Node>& roots, const char* panel, const char* name) {
  for (const auto& root : roots) {
    if (root.tag != dtb::kCommand || root.kids.size() < 3 || root.kids[2].text != panel) continue;
    for (const auto& child : root.kids) {
      if (child.tag == dtb::kArray && !child.kids.empty() && child.kids[0].text == name) return &child;
    }
  }
  return nullptr;
}
bool Has(const Node& node, const char* text) {
  if (node.text == text) return true;
  for (const auto& child : node.kids) if (Has(child, text)) return true;
  return false;
}
const Node* CompleteSplashArray(const Node& node) {
  if (node.tag == dtb::kArray) {
    bool start = false, exit = false;
    for (const auto& child : node.kids) {
      start |= child.tag == dtb::kSymbol && child.text == "splash_start";
      exit |= child.tag == dtb::kSymbol && child.text == "splash_exit";
    }
    if (start && exit) return &node;
  }
  for (const auto& child : node.kids)
    if (const Node* found = CompleteSplashArray(child)) return found;
  return nullptr;
}
const Node* Case(const Node& node, const char* action) {
  if (node.tag == dtb::kArray && !node.kids.empty()) {
    const auto& first = node.kids[0];
    if ((first.tag == dtb::kSymbol && first.text == action) ||
        (first.tag == dtb::kCommand && first.kids.size() == 3 && first.kids[0].text == "==" &&
         first.kids[2].text == action)) return &node;
  }
  for (const auto& child : node.kids) if (auto* found = Case(child, action)) return found;
  return nullptr;
}
void Destinations(const Node& node, std::vector<int>* found, bool right) {
  if (node.tag == dtb::kCommand && node.kids.size() == 3 && node.kids[0].text == settings::kLibraryHeaderTransition) {
    CHECK_EQ(static_cast<int32_t>(node.kids[2].scalar), right ? 1 : -1);
    const auto& destination = node.kids[1];
    if (destination.tag == dtb::kCommand) {
      CHECK_TRUE(Has(destination, "which_store_screen"));
      found->push_back(1);
    } else {
      for (int i = 0; i < 4; ++i) if (destination.text == kScreens[i]) found->push_back(i);
    }
  }
  for (const auto& child : node.kids) Destinations(child, found, right);
}
void CheckRoutes(const std::vector<Node>& roots, int current, int mask) {
  for (const char* panel : kPanels[current]) {
    const auto* buttons = Method(roots, panel, "BUTTON_DOWN_MSG");
    CHECK_TRUE(buttons != nullptr);
    if (!buttons) continue;
    const auto* poll = Method(roots, panel, "poll");
    int mouse_routes = 0;
    for (int target = 0; target < 4; ++target) {
      const bool enabled = target != current && (target == 0 || (mask & (1 << (target - 1))));
      const auto path = std::string("rbbz-mouse/header/") + panel + "/" + std::to_string(target);
      CHECK_EQ(poll && Has(*poll, path.c_str()), enabled);
      if (enabled) ++mouse_routes;
    }
    if (mouse_routes && poll) {
      const auto& routes = poll->kids.back();
      CHECK_EQ(routes.kids.size(), size_t(mouse_routes + 1));
      for (size_t i = 1; i < routes.kids.size(); ++i) {
        const auto& route = routes.kids[i];
        const int target = route.kids[0].kids[1].text.back() - '0';
        const bool right = (target - current + 4) % 4 <= 2;
        std::vector<int> destinations;
        Destinations(route, &destinations, right);
        CHECK_EQ(destinations.size(), 1u);
        if (!destinations.empty()) CHECK_EQ(destinations[0], target);
        CHECK_FALSE(Has(route, "online_gate"));
        CHECK_TRUE(Has(route, right ? "arrow_rt.flow" : "arrow_lt.flow"));
        if (current == 3) CHECK_TRUE(Has(route, "stop_preview_audio"));
        if (std::string(panel) == "store_browser_panel") CHECK_TRUE(Has(route, "turn_off_preview"));
      }
    }
    for (bool right : {true, false}) {
      const auto* kase = Case(*buttons, right ? "kAction_PageDown" : "kAction_PageUp");
      CHECK_TRUE(kase != nullptr);
      if (!kase) continue;
      std::vector<int> destinations;
      Destinations(*kase, &destinations, right);
      CHECK_FALSE(Has(*kase, "transition_to"));
      const int expected = right ? kRight[mask][current] : kLeft[mask][current];
      if (expected == current) CHECK_TRUE(destinations.empty());
      else { CHECK_EQ(destinations.size(), 1u); if (!destinations.empty()) CHECK_EQ(destinations[0], expected); }
      CHECK_TRUE(Has(*kase, right ? "arrow_rt.flow" : "arrow_lt.flow"));
      if (current == 3) CHECK_TRUE(Has(*kase, "stop_preview_audio"));
      if (std::string(panel) == "store_browser_panel") CHECK_TRUE(Has(*kase, "turn_off_preview"));
      CHECK_TRUE(!Has(*kase, "online_gate"));
    }
  }
}
std::vector<Node> Fixture(int current) {
  std::vector<Node> roots;
  for (const char* panel : kPanels[current]) {
    std::vector<Node> cases{S(current == 1 ? "cond" : "switch")};
    if (current != 1) cases.push_back(Node::Var("action"));
    for (bool right : {true, false}) {
      const int next = right ? (current + 1) % 4 : (current + 3) % 4;
      Node key = S(right ? "kAction_PageDown" : "kAction_PageUp");
      if (current == 1) key = C({S("=="), Node::Var("action"), key});
      std::vector<Node> body{key};
      if (current == 0) {
        body.push_back(C({S("if"), C({S("online_gate")}), Transition(next), Arrow(right)}));
      } else if (current == 3 && right) {
        body.push_back(C({Node::Var("this"), S("go_to_song_select")}));
      } else {
        const char* cleanup = current == 3 ? "stop_preview_audio" :
            (std::string(panel) == "store_browser_panel" ? "turn_off_preview" : "mark_current_highlight_idx");
        body.push_back(C({S("do"), C({Node::Var("this"), S(cleanup)}), Transition(next), Arrow(right)}));
      }
      cases.push_back(A(std::move(body)));
    }
    cases.push_back(A({S("kAction_Start"), C({S("retail_keyboard")})}));
    std::vector<Node> definition{S("new"), S("UIPanel"), S(panel), A({S("BUTTON_DOWN_MSG"), C(std::move(cases))})};
    if (current == 3) definition.push_back(A({S("go_to_song_select"),
        C({Node::Var("this"), S("stop_preview_audio")}), Arrow(true), Transition(0)}));
    roots.push_back(C(std::move(definition)));
  }
  return roots;
}
std::vector<Node> Background() {
  return {C({S("new"), S("UIPanel"), S("background_panel"),
      A({S("set_offline_mode_headers"), A({Node::Var("offline")}),
          C({S("if_else"), Node::Var("offline"), C({S("offline_flow")}), C({S("online_flow")})})}),
      A({S("update_inbox_header"), A({Node::Var("new_msgs")}), C({S("retail_badge")})}),
      A({S("enter"), C({S("retail_enter")})})})};
}
void CheckBar(const std::vector<Node>& roots, int mask) {
  const auto* mode = Method(roots, "background_panel", "set_offline_mode_headers");
  const auto* inbox = Method(roots, "background_panel", "update_inbox_header");
  CHECK_TRUE(mode && inbox);
  if (!mode || !inbox) return;
  CHECK_TRUE(mode->kids[2].kids[1].text == (mask ? "FALSE" : "TRUE"));
  const std::vector<std::pair<const char*, int>> labels = {
      {"main_store.lbl",1}, {"main_inbox.lbl",2}, {"main_new_msg.lbl",2}, {"main_Home.lbl",4}};
  for (const auto& [label, bit] : labels) {
    int hides = 0;
    for (const auto& command : mode->kids) {
      if (command.tag == dtb::kCommand && command.kids.size() == 3 && command.kids[0].text == label) {
        CHECK_TRUE(command.kids[0].tag == dtb::kSymbol);
        CHECK_TRUE(command.kids[1].text == "set_showing" && command.kids[2].text == "FALSE");
        ++hides;
      }
    }
    CHECK_EQ(hides, (mask & bit) ? 0 : 1);
  }
  CHECK_EQ(inbox->kids.size(), (mask & 2) ? 3u : 4u);
  const auto* enter = Method(roots, "background_panel", "enter");
  CHECK_TRUE(enter != nullptr);
  if (!enter) return;
  std::map<std::pair<std::string, int>, int> keys;
  for (const auto& command : enter->kids) {
    if (command.tag != dtb::kCommand || command.kids.size() != 6 || command.kids[1].text != "set_key_val") continue;
    CHECK_TRUE(Has(command.kids[0], "TextShift.anim"));
    CHECK_TRUE(Has(command.kids[3], "local_xfm") && Has(command.kids[3], "x"));
    const auto key = std::make_pair(command.kids[2].kids[2].text, int(command.kids[4].scalar));
    CHECK_FALSE(keys.contains(key));
    keys[key] = static_cast<int32_t>(command.kids[5].scalar);
  }
  // A scene refresh may restore all captions while Search opens its browser.
  // Apply the generated entry/poll statements to that state and ensure every
  // disabled caption is hidden again without touching the enabled captions.
  const auto* poll = Method(roots, "background_panel", "poll");
  CHECK_TRUE(mask == 7 || poll != nullptr);
  for (const auto* callback : {enter, poll}) {
    if (!callback) continue;
    std::map<std::string, bool> showing;
    for (const auto& [label, bit] : labels) showing[label] = true;
    for (const auto& command : callback->kids) {
      if (command.tag == dtb::kCommand && command.kids.size() == 3 &&
          command.kids[1].text == "set_showing" && showing.contains(command.kids[0].text)) {
        CHECK_TRUE(command.kids[2].text == "FALSE");
        showing[command.kids[0].text] = false;
      }
      CHECK_FALSE(Has(command, "online_headers.flow"));
      CHECK_FALSE(Has(command, "SnapToIndex.flow"));
    }
    for (const auto& [label, bit] : labels) CHECK_EQ(showing[label], bool(mask & bit));
  }
  if (mask == 7) { CHECK_TRUE(keys.empty()); return; }
  const char* names[] = {"main_mySongs.trans", "main_store.trans", "main_inbox.trans", "main_home.trans"};
  const int indices[] = {1, 2, 3, 0};
  const int slots[] = {-345, -43, 91, 226};
  for (int current = 0; current < 4; ++current) {
    if (current && !(mask & (1 << (current - 1)))) continue;
    int slot = 0;
    for (int offset = 0; offset < 4; ++offset) {
      const int tab = (current + offset) % 4;
      if (tab && !(mask & (1 << (tab - 1)))) continue;
      const int expected = tab == 3 && slot == 3 ? 235 : slots[slot];
      CHECK_EQ(keys[std::make_pair(names[tab], indices[current] * 10)], expected);
      if (tab == 2) CHECK_EQ(keys[std::make_pair("main_newMessage.trans", indices[current] * 10)], expected);
      ++slot;
    }
  }
  for (const auto& [key, value] : keys) {
    if (key.second == 0) CHECK_EQ(keys[std::make_pair(key.first, 40)], value);
  }
}

const Node* Find(const Node& node, uint32_t tag, const char* text) {
  if (node.tag == tag && !node.kids.empty() && node.kids[0].text == text) return &node;
  for (const auto& child : node.kids) if (const auto* result = Find(child, tag, text)) return result;
  return nullptr;
}
const Node* Function(const std::vector<Node>& roots, const char* name) {
  for (const auto& root : roots) {
    if (root.tag == dtb::kCommand && root.kids.size() == 4 && root.kids[0].text == "func" &&
        root.kids[1].text == name) return &root;
  }
  return nullptr;
}
std::vector<Node> AnimationFixture() {
  const Node fallback = C({S("if_else"), Node::Var("did_jump"),
      C({S("if_else"), C({S(">"), Node::Var("start_header_num"), Node::Var("target_header_num")}),
         S("shift_up"), S("shift_down")}),
      C({S("if_else"), C({S("^^"),
          C({S(">"), Node::Var("start_header_num"), Node::Var("target_header_num")}), Node::Var("did_loop")}),
          S("shift_left"), S("shift_right")})});
  return {C({S("func"), S("create_one_way_transition_screen"), A({}),
      C({S("new"), S("TransitionScreen"), S("test_transition"),
          A({S("TRANSITION_COMPLETE_MSG"), C({S("do"), A({Node::Var("anim_type"), fallback})})})})}),
      C({S("func"), S("transition_between"), A({Node::Var("screen_a"), Node::Var("screen_b")}),
          C({S("do"), C({S("create_one_way_transition_screen")}),
             C({S("ui"), S("goto_screen"), C({S("object"), Node::Var("screen_name")})})})})};
}
// Evaluate the finite boolean/selection grammar used by the actual retail anim_type
// expression. Exercise the generated expression, including its unmodified fallback.
Node EvalAnimation(const Node& node, int start, int target, int direction) {
  if (node.tag == dtb::kProperty) return Node::Int(static_cast<uint32_t>(direction));
  if (node.tag == dtb::kVar) {
    int value = 0;
    if (node.text == "start_header_num") value = start;
    else if (node.text == "target_header_num") value = target;
    else if (node.text == "did_jump") value = start / 10 != target / 10;
    else if (node.text == "did_loop") value = start / 10 == target / 10 && start / 10 == 0 && std::abs(start - target) != 1;
    else CHECK_TRUE(false);
    return Node::Int(static_cast<uint32_t>(value));
  }
  if (node.tag != dtb::kCommand) return node;
  const auto& op = node.kids[0].text;
  const auto number = [&](int child) { return static_cast<int32_t>(EvalAnimation(node.kids[child], start, target, direction).scalar); };
  if (op == "if_else") return EvalAnimation(node.kids[number(1) ? 2 : 3], start, target, direction);
  if (op == "!=") return Node::Int(number(1) != number(2));
  if (op == ">") return Node::Int(number(1) > number(2));
  if (op == "^^") return Node::Int(bool(number(1)) != bool(number(2)));
  CHECK_TRUE(false);
  return Node::Int(0);
}
void CheckAnimations(const std::vector<Node>& roots, int mask) {
  const auto* factory = Function(roots, "create_one_way_transition_screen");
  CHECK_TRUE(factory != nullptr);
  if (!factory) return;
  const auto* binding = Find(*factory, dtb::kArray, "anim_type");
  CHECK_TRUE(binding && binding->kids.size() == 2);
  if (!binding || binding->kids.size() != 2) return;
  const auto& expression = binding->kids[1];
  const int indices[] = {1, 2, 3, 0};
  for (int current = 0; current < 4; ++current) {
    for (int direction : {1, -1}) {
      const int next = direction > 0 ? kRight[mask][current] : kLeft[mask][current];
      if (next == current) continue;
      CheckStrEq(EvalAnimation(expression, indices[current], indices[next], direction).text,
                 direction > 0 ? "shift_right" : "shift_left");
    }
  }
  for (int start : {0, 1, 2, 3, 10, 21}) {
    for (int target : {0, 1, 2, 3, 10, 21}) {
      if (start == target) continue;
      const bool jump = start / 10 != target / 10;
      const bool loop = !jump && start / 10 == 0 && std::abs(start - target) != 1;
      const auto expected = jump ? (start > target ? "shift_up" : "shift_down") :
          ((bool(start > target) != loop) ? "shift_left" : "shift_right");
      CheckStrEq(EvalAnimation(expression, start, target, 0).text, expected);
    }
  }
  for (bool directional : {false, true}) {
    const auto* function = Function(roots, directional ? "rbbz_header_transition_between" : "transition_between");
    CHECK_TRUE(function != nullptr);
    if (!function) continue;
    CHECK_EQ(function->kids[2].kids.size(), directional ? 3u : 2u);
    const auto* go = Find(*function, dtb::kCommand, "do");
    CHECK_TRUE(go != nullptr);
    const Node* sequence = nullptr;
    if (go) {
      for (const auto& child : go->kids) {
        if (child.tag == dtb::kCommand && child.kids.size() == 3 && child.kids[0].text == "do" &&
            Has(child.kids[1], "rbbz_header_animation") && Has(child.kids[2], "goto_screen")) sequence = &child;
      }
    }
    CHECK_TRUE(sequence != nullptr);
    if (sequence) {
      const auto& value = sequence->kids[1].kids[3];
      if (directional) CHECK_TRUE(value.tag == dtb::kVar && value.text == "rbbz_header_direction");
      else CHECK_TRUE(value.tag == dtb::kInt && value.scalar == 0);
    }
  }
  const auto* wrapper = Function(roots, settings::kLibraryHeaderTransition);
  CHECK_TRUE(wrapper && Has(*wrapper, "rbbz_header_transition_between"));
}
}

void TestHeaderAnimations() {
  using namespace header_test;
  BeginCase("header swipes follow the pressed trigger, including two-tab rings; ordinary transitions reset the override");
  auto roots = AnimationFixture();
  const auto original = roots;
  std::string why;
  CHECK_TRUE(settings::ConfigureLibraryHeaderAnimations(&roots, &why));
  const auto* binding = Find(*Function(roots, "create_one_way_transition_screen"), dtb::kArray, "anim_type");
  const auto* old = Find(*Function(original, "create_one_way_transition_screen"), dtb::kArray, "anim_type");
  CHECK_TRUE(binding && old);
  if (binding && old) CHECK_TRUE(dtb::Compile({binding->kids[1].kids[3]}) == dtb::Compile({old->kids[1]}));
  for (int mask = 0; mask < 8; ++mask) CheckAnimations(roots, mask);
  auto invalid = AnimationFixture();
  invalid.pop_back();
  const auto before = dtb::Compile(invalid);
  CHECK_FALSE(settings::ConfigureLibraryHeaderAnimations(&invalid, &why));
  CHECK_TRUE(dtb::Compile(invalid) == before);
}

void TestHeaderVisibility() {
  using namespace header_test;
  BeginCase("independent header visibility: eight states, badge follows Messages, arrows follow any destination");
  for (int mask = 0; mask < 8; ++mask) {
    auto roots = Background();
    std::string why;
    CHECK_TRUE(settings::ConfigureLibraryHeaderBar(&roots, State(mask), &why));
    CheckBar(roots, mask);
  }
  {
    auto roots = Background();
    roots[0].kids.push_back(A({S("poll"), C({S("retail_poll")})}));
    std::string why;
    CHECK_TRUE(settings::ConfigureLibraryHeaderBar(&roots, State(3), &why));
    const auto* poll = Method(roots, "background_panel", "poll");
    CHECK_TRUE(poll && poll->kids.size() == 3 && Has(*poll, "retail_poll"));
    CheckBar(roots, 3);
  }
  auto invalid = Background();
  invalid[0].kids.pop_back();
  const auto before = dtb::Compile(invalid);
  std::string why;
  CHECK_FALSE(settings::ConfigureLibraryHeaderBar(&invalid, State(0), &why));
  CHECK_TRUE(dtb::Compile(invalid) == before);
}
void TestHeaderRuntimeRefresh() {
  using namespace header_test;
  BeginCase("runtime header bar refreshes all eight masks and both trigger routes as one authored group");
  auto background = Background();
  std::string why;
  CHECK_TRUE(settings::ConfigureLibraryHeaderRuntimeBar(&background, &why));
  const auto* refresh = Method(background, "background_panel", "rbbz_refresh_headers");
  const auto* enter = Method(background, "background_panel", "enter");
  const auto* poll = Method(background, "background_panel", "poll");
  const auto* mode = Method(background, "background_panel", "set_offline_mode_headers");
  const auto* inbox = Method(background, "background_panel", "update_inbox_header");
  CHECK_TRUE(refresh && enter && poll && mode && inbox);
  if (refresh && enter && poll && mode && inbox) {
    CHECK_EQ(refresh->kids.size(), 13u); // method, mask, Flow, eight layouts, badge, acknowledgement
    CHECK_TRUE(refresh->kids[2].tag == dtb::kCommand &&
               refresh->kids[2].kids[0].text == "if_else" &&
               Has(refresh->kids[2], "rbbz-menu/rhythmverse") &&
               Has(refresh->kids[2], "rbbz-menu/messages") &&
               Has(refresh->kids[2], "rbbz-menu/recommendations"));
    for (int mask = 0; mask < 8; ++mask) {
      const auto& group = refresh->kids[mask + 3];
      CHECK_TRUE(group.tag == dtb::kCommand && group.kids.size() == 3 &&
                 group.kids[0].text == "if" && group.kids[1].kids[2].scalar == unsigned(mask) &&
                 group.kids[2].kids[0].text == "do");
      CHECK_TRUE(Has(group, "main_store.lbl") && Has(group, "main_inbox.lbl") &&
                 Has(group, "main_Home.lbl") && Has(group, "TextShift.anim"));
      for (const auto& [label, bit] : std::array<std::pair<const char*, int>, 3>{{
          {"main_store.lbl", 1}, {"main_inbox.lbl", 2}, {"main_Home.lbl", 4}}}) {
        const auto* setter = Find(group, dtb::kCommand, label);
        CHECK_TRUE(setter && setter->kids.size() == 3);
        if (setter) CHECK_TRUE(setter->kids[2].text == ((mask & bit) ? "TRUE" : "FALSE"));
      }
      // The badge is hidden with Messages; on restoration the retail callback below
      // decides whether the saved message count warrants displaying it.
      CHECK_EQ(Has(group, "main_new_msg.lbl"), !(mask & 2));
    }
    CHECK_TRUE(Has(refresh->kids[2], "offline_flow") &&
               Has(refresh->kids[2], "online_flow") &&
               Has(refresh->kids[2], "rbbz-menu/rhythmverse") &&
               Has(refresh->kids[2], "rbbz-menu/messages") &&
               Has(refresh->kids[2], "rbbz-menu/recommendations"));
    CHECK_TRUE(Has(*enter, "rbbz_refresh_headers"));
    CHECK_TRUE(Has(*poll, "rbbz_refresh_headers"));
    CHECK_TRUE(Has(*poll, "rbbz_header_mask") && Has(*poll, "!="));
    CHECK_FALSE(Has(*mode, "rbbz_refresh_headers"));
    CHECK_TRUE(Has(*inbox, "rbbz-menu/messages"));
    CHECK_TRUE(Has(*inbox, "rbbz_header_new_msgs") &&
               Has(refresh->kids[refresh->kids.size() - 2], "update_inbox_header"));
    CHECK_TRUE(refresh->kids.back().kids.size() == 2 &&
               refresh->kids.back().kids[1].text == "rbbz-menu/headers-applied");
  }
  for (int current = 0; current < 4; ++current) {
    auto roots = Fixture(current);
    CHECK_TRUE(settings::RouteLibraryHeaderRuntimeNavigation(&roots, static_cast<Tab>(current), &why));
    for (const char* panel : kPanels[current]) {
      const auto* buttons = Method(roots, panel, "BUTTON_DOWN_MSG");
      const auto* mouse = Method(roots, panel, "poll");
      CHECK_TRUE(buttons && Has(*buttons, "rbbz-menu/rhythmverse") &&
                 Has(*buttons, "rbbz-menu/messages") &&
                 Has(*buttons, "rbbz-menu/recommendations"));
      CHECK_TRUE(mouse != nullptr);
      if (mouse) {
        const auto& routes = mouse->kids.back();
        CHECK_EQ(routes.kids.size(), 4u);  // cond plus each other header
        for (int target = 0; target < 4; ++target) {
          if (target == current) continue;
          const std::string path = std::string("rbbz-mouse/header/") + panel +
                                   "/" + std::to_string(target);
          CHECK_TRUE(Has(*mouse, path.c_str()));
          const Node* route = nullptr;
          for (size_t i = 1; i < routes.kids.size(); ++i)
            if (routes.kids[i].kids[0].kids[1].text == path) route = &routes.kids[i];
          CHECK_TRUE(route != nullptr);
          if (route) {
            const bool right = (target - current + 4) % 4 <= 2;
            std::vector<int> destinations;
            Destinations(*route, &destinations, right);
            CHECK_EQ(destinations.size(), 1u);
            if (!destinations.empty()) CHECK_EQ(destinations[0], target);
          }
        }
      }
      if (!buttons) continue;
      for (bool right : {true, false}) {
        const auto* action = Case(*buttons, right ? "kAction_PageDown" : "kAction_PageUp");
        CHECK_TRUE(action && action->kids.size() == 2 &&
                   action->kids[1].tag == dtb::kCommand &&
                   action->kids[1].kids.size() == 9);
        if (!action || action->kids.size() != 2 || action->kids[1].kids.size() != 9) continue;
        for (int mask = 0; mask < 8; ++mask) {
          const auto& branch = action->kids[1].kids[mask + 1];
          CHECK_TRUE(branch.tag == dtb::kArray && branch.kids.size() == 2 &&
                     branch.kids[0].kids[0].text == "==" &&
                     branch.kids[0].kids[2].scalar == unsigned(mask));
          std::vector<int> destinations;
          Destinations(branch, &destinations, right);
          const int expected = right ? kRight[mask][current] : kLeft[mask][current];
          CHECK_EQ(destinations.size(), expected == current ? 0u : 1u);
          if (!destinations.empty()) CHECK_EQ(destinations[0], expected);
        }
      }
    }
    const auto bytes = dtb::Compile(roots);
    std::vector<Node> reparsed;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &reparsed, &header, &why));
  }
}
void TestMessagesUpdateInbox() {
  using namespace header_test;
  BeginCase("Messages START hint and action share the active-version gate; confirmation stays guest-authored");
  auto roots = std::vector<Node>{C({S("new"), S("InboxPanel"), S("inbox_panel"),
      A({S("enter"), C({S("retail_enter")})}),
      A({S("poll"), C({S("retail_poll")})}),
      A({S("update_inline_help"), C({S("do"),
          C({S("InlineHelp.ihp"), S("clear_action_token"), S("kAction_Start")}),
          C({S("InlineHelp.ihp"), S("set_action_token"), S("kAction_Start"),
             S("inline_help_options")})})}),
      A({S("BUTTON_DOWN_MSG"), C({S("switch"), Node::Var("action"),
          A({S("kAction_Start"),
             C({S("options_screen"), S("set"), S("back_screen"), S("inbox_screen")}),
             C({S("ui"), S("goto_screen"), S("options_screen")})})})})})};
  std::string why;
  CHECK_TRUE(settings::ConfigureMessagesUpdateInbox(&roots, &why));
  CHECK_EQ(roots.size(), 3u);
  const auto* help = Method(roots, "inbox_panel", "update_inline_help");
  const auto* buttons = Method(roots, "inbox_panel", "BUTTON_DOWN_MSG");
  const auto* inbox_poll = Method(roots, "inbox_panel", "poll");
  CHECK_TRUE(help && buttons);
  CHECK_TRUE(inbox_poll && Has(*inbox_poll, "retail_poll") &&
             Has(*inbox_poll, "rbbz_messages_update_available_last") &&
             Has(*inbox_poll, "update_inline_help"));
  if (help && buttons) {
    const auto* hint = Find(*help, dtb::kCommand, "if");
    const auto* start = Case(*buttons, "kAction_Start");
    CHECK_TRUE(hint && start);
    if (hint && start) {
      CHECK_TRUE(Has(*hint, "rbbz-messages/update/available"));
      CHECK_TRUE(Has(*hint, "inline_help_update_game"));
      CHECK_FALSE(Has(*hint, "inline_help_options"));
      CHECK_TRUE(Has(*start, "rbbz-messages/update/available"));
      CHECK_TRUE(Has(*start, "messages_update_screen"));
      CHECK_FALSE(Has(*start, "options_screen"));
      CHECK_EQ(start->kids.size(), 2u);  // false query has no action
    }
  }
  const auto* enter = Method(roots, "messages_update_panel", "enter");
  const auto* select = Method(roots, "messages_update_panel", "select_row");
  const auto* poll = Method(roots, "messages_update_panel", "poll");
  CHECK_TRUE(enter && select && poll);
  if (enter && select && poll) {
    CHECK_TRUE(Has(*enter, "rbbz-messages/update/message"));
    CHECK_TRUE(Has(*select, "rbbz-messages/update/open-releases/0"));
    CHECK_TRUE(Has(*select, "messages_update_later"));
    CHECK_TRUE(Has(*poll, "rbbz-messages/update/open-failed"));
    CHECK_TRUE(Has(*poll, "messages_update_open_failed"));
  }
  CHECK_TRUE(Has(roots.back(), "inbox_screen"));
  const auto bytes = dtb::Compile(roots);
  std::vector<Node> reparsed;
  dtb::RootHeader header;
  CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &reparsed, &header, &why));
  CHECK_EQ(reparsed.size(), roots.size());
  auto invalid = std::vector<Node>{C({S("new"), S("InboxPanel"), S("inbox_panel")})};
  const auto before = dtb::Compile(invalid);
  CHECK_FALSE(settings::ConfigureMessagesUpdateInbox(&invalid, &why));
  CHECK_TRUE(dtb::Compile(invalid) == before);
}
void TestCurrencyVisibility() {
  using namespace header_test;
  BeginCase("currency chrome is hidden after retail setup without changing purchase or reward data");
  auto powerups = std::vector<Node>{C({S("new"), S("PowerupSelectPanel"), S("powerup_select_panel"),
      A({S("enter"), C({S("your_monies_text.lbl"), S("set_token_fmt"), S("current_monies")}),
                      C({S("your_cred.lbl"), S("set_token_fmt"), S("current_cred")}),
                      C({S("your_cred.lbl"), S("set"), S("showing"), S("TRUE")})})})};
  auto results = std::vector<Node>{C({S("new"), S("UIPanel"), S("song_results_panel"),
      A({S("enter"), C({S("retail_enter")})}), A({S("poll"), C({S("retail_poll")})})})};
  std::string why;
  CHECK_TRUE(settings::HidePowerupCurrency(&powerups, &why));
  CHECK_TRUE(settings::HideResultsCurrency(&results, &why));
  const auto* enter = Method(powerups, "powerup_select_panel", "enter");
  CHECK_TRUE(enter && enter->kids.size() == 6 && enter->kids[3].kids[0].text == "your_cred.lbl" &&
             enter->kids[4].kids[0].text == "your_monies_text.lbl" &&
             enter->kids[5].kids[0].text == "your_cred.lbl" &&
             enter->kids[4].kids[2].text == "FALSE" && enter->kids[5].kids[2].text == "FALSE");
  for (const char* method : {"enter", "poll"}) {
    const auto* body = Method(results, "song_results_panel", method);
    CHECK_TRUE(body && body->kids.size() == 4 && body->kids[1].kids[0].text ==
               std::string(method == std::string("enter") ? "retail_enter" : "retail_poll") &&
               body->kids[2].kids[0].text == "Coins.grp" &&
               body->kids[3].kids[0].text == "cred.grp" &&
               body->kids[2].kids[2].text == "FALSE" && body->kids[3].kids[2].text == "FALSE");
  }
  auto invalid = powerups;
  invalid[0].kids[3].kids.erase(invalid[0].kids[3].kids.begin() + 1);
  const auto before = dtb::Compile(invalid);
  CHECK_FALSE(settings::HidePowerupCurrency(&invalid, &why));
  CHECK_TRUE(dtb::Compile(invalid) == before);
}
void TestHeaderNavigation() {
  using namespace header_test;
  BeginCase("all eight header rings: both triggers skip disabled destinations on all five panels");
  for (int mask = 0; mask < 8; ++mask) {
    for (int current = 0; current < 4; ++current) {
      auto roots = Fixture(current);
      const auto original = roots;
      std::string why;
      CHECK_TRUE(settings::RouteLibraryHeaderNavigation(&roots, static_cast<Tab>(current), State(mask), &why));
      CheckRoutes(roots, current, mask);
      for (const char* panel : kPanels[current]) {
        const auto* old_start = Case(*Method(original, panel, "BUTTON_DOWN_MSG"), "kAction_Start");
        const auto* new_start = Case(*Method(roots, panel, "BUTTON_DOWN_MSG"), "kAction_Start");
        CHECK_TRUE(dtb::Compile({*old_start}) == dtb::Compile({*new_start}));
      }
    }
  }
  auto invalid = Fixture(1);
  invalid.pop_back();
  const auto before = dtb::Compile(invalid);
  std::string why;
  CHECK_FALSE(settings::RouteLibraryHeaderNavigation(&invalid, Tab::kStore, State(7), &why));
  CHECK_TRUE(dtb::Compile(invalid) == before);
}

void CheckIconScene(const std::vector<uint8_t>& icon_scene) {
  CHECK_TRUE(icon_scene.size() > 0x810 + 0x867 + 31);
  if (icon_scene.size() <= 0x810 + 0x867 + 31) return;
  const std::string resources(reinterpret_cast<const char*>(icon_scene.data() + 0x810), 0x867);
  CHECK_TRUE(resources.find("Iconatonic_Regular_(8_89)4x.tex") != std::string::npos);
  CHECK_TRUE(resources.find("../image/iconatonic_regular_(8_89)4x.bmp") != std::string::npos);
  const auto retail = rb_blitz::ui::input_font::OriginalBc3();
  CHECK_TRUE(retail != nullptr);
  const auto private_bc3 = rb_blitz::ui::input_font::PrivateBc3();
  CHECK_TRUE(private_bc3 != nullptr);
  if (!retail || !private_bc3) return;
  constexpr size_t payload = 0x810 + 0x867 + 31;
  constexpr size_t dimensions = 0x810 + 0x867 + 10;
  for (size_t field = 0; field < 3; ++field) {
    CHECK_EQ(icon_scene[dimensions + field * 2], uint8_t{4});
    CHECK_EQ(icon_scene[dimensions + field * 2 + 1], uint8_t{0});
  }
  CHECK_TRUE(icon_scene.size() >= payload + private_bc3->size());
  CHECK_TRUE(*private_bc3 != *retail);
  if (icon_scene.size() < payload + private_bc3->size()) return;
  constexpr size_t key = 0x810 + 0x867;
  const uint32_t bitmap_key = uint32_t(icon_scene[key]) << 24 |
      uint32_t(icon_scene[key + 1]) << 16 | uint32_t(icon_scene[key + 2]) << 8 |
      icon_scene[key + 3];
  CHECK_TRUE(bitmap_key != 0x6F93E325);
  bool texture_identical = true;
  for (size_t i = 0; i < private_bc3->size(); i += 2)
    texture_identical &= icon_scene[payload + i] == (*private_bc3)[i + 1] &&
                         icon_scene[payload + i + 1] == (*private_bc3)[i];
  CHECK_TRUE(texture_identical);
}

void CheckPrivateFontGeometry(const std::vector<uint8_t>& scene, bool hint) {
  namespace font = rb_blitz::ui::input_font;
  auto be32 = [&](size_t at) {
    return uint32_t(scene[at]) << 24 | uint32_t(scene[at + 1]) << 16 |
           uint32_t(scene[at + 2]) << 8 | scene[at + 3];
  };
  const float cell = font::kPrivateFontCellWidth;
  const uint32_t index = hint ? font::kHintFirst + font::kSplashStartHint : 0;
  const float glyph_width = float(font::kPrivateIconSize) / cell;
  const float glyph_x = float(font::PrivateIconX(index)) / font::kAtlasSize;
  const float glyph_y = float(font::PrivateIconY(index) - font::kPrivateInset) / font::kAtlasSize;
  size_t tables = 0;
  for (size_t at = 0x810 + 580; at + (256 - 32) * 18 <= scene.size(); ++at) {
    if (scene[at] != 0 || scene[at + 1] != 0x20 || be32(at - 580) != 256) continue;
    bool ascii = true;
    for (uint32_t c = 0; c < 8; ++c)
      ascii &= scene[at + c * 18] == 0 && scene[at + c * 18 + 1] == 0x20 + c;
    if (!ascii) continue;
    ++tables;
    const size_t table = at - 576;
    CHECK_EQ(be32(table - 12), std::bit_cast<uint32_t>(cell / font::kAtlasSize));
    CHECK_EQ(be32(table - 8), std::bit_cast<uint32_t>(cell / font::kAtlasSize));
    const size_t glyph = table + (hint ? font::kSplashGlyphCode : font::GlyphCode(0)) * 18;
    CHECK_EQ(be32(glyph + 2), std::bit_cast<uint32_t>(glyph_x));
    CHECK_EQ(be32(glyph + 6), std::bit_cast<uint32_t>(glyph_y));
    CHECK_EQ(be32(glyph + 10), std::bit_cast<uint32_t>(glyph_width));
    CHECK_EQ(be32(glyph + 14), std::bit_cast<uint32_t>(glyph_width));
  }
  CHECK_EQ(tables, size_t{4});
}

// Optional integration check against extracted records; never boots or drives the game.
int CheckInstalledRuntimeHeaderRecords(const char* asset_root, bool offline_only = false) {
  namespace fs = std::filesystem;
  namespace screen = rb_blitz::ui::screen;
  using namespace header_test;
  settings::RecordReader read = [&](const std::string& name, std::vector<uint8_t>* bytes, std::string* why) {
    std::ifstream file(fs::path(asset_root) / name, std::ios::binary);
    if (!file) { if (why) *why = "missing " + name; return false; }
    bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  };
  screen::MenuConfiguration config;
  config.menu_refresh_trial = true;
  config.rhythmverse_row = true;
  config.show_messages = true;
  config.show_recommendations = true;
  config.controller_layout_enabled = true;
  screen::ConfigureMenu(config);
  settings::MenuRecords records;
  settings::MenuRefreshRoutes routes;
  std::string why;
  std::vector<std::string> notes;
  if (!settings::BuildMenuRecords(read, {"ui/locale/eng/gen/locale_keep.dtb"},
                                  &records, &why, false, false, &notes, {}, false, &routes)) {
    std::printf("runtime header records refused: %s\n", why.c_str()); return 1;
  }
  for (const auto& note : notes) std::printf("record note: %s\n", note.c_str());
  CHECK_TRUE(notes.empty());
  CHECK_TRUE(routes.rows && routes.headers && routes.ready());
  BeginCase("generated connection records carry the offline policy before archive reads");
  for (unsigned mode = 0; mode < 4; ++mode) {
    const bool portrait = (mode & 2) != 0;
    const bool skip_offline = (mode & 1) != 0;
    settings::MenuRecords configured;
    if (skip_offline || portrait) {
      CHECK_TRUE(settings::BuildMenuRecords(read, {"ui/locale/eng/gen/locale_keep.dtb"},
          &configured, &why, false, false, nullptr, {}, false, nullptr, {}, portrait, skip_offline));
    } else {
      configured = records;
    }
    const auto found = configured.find("ui/net/gen/server_connect.dtb");
    CHECK_TRUE(found != configured.end());
    if (found == configured.end()) continue;
    std::vector<Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(found->second.data(), found->second.size(), &roots, &header, &why));
    const auto* update = Method(roots, "server_connect_panel", "update_state");
    CHECK_TRUE(update != nullptr);
    if (update) {
      CHECK_TRUE(Has(*update, "set_state") == skip_offline);
      CHECK_TRUE(Has(*update, "rbbz_begin_graphics") == skip_offline);
      CHECK_FALSE(Has(*update, "goto_screen"));
    }
    // The packed bytes already contain the transitions; read alignment can no
    // longer decide whether the skip reaches the guest. A later filter is a no-op.
    auto copy = found->second;
    const auto skipped = rb_blitz::menu_options::SkipOfflinePrompts(copy.data(), copy.size());
    CHECK_TRUE(skipped.applied != skip_offline);
    if (skip_offline) CHECK_TRUE(copy == found->second);
  }
  if (offline_only) return Finish();
  const auto splash = records.find("ui/splash/gen/splash.dtb");
  CHECK_TRUE(splash != records.end());
  if (splash != records.end()) {
    std::vector<Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(splash->second.data(), splash->second.size(), &roots, &header, &why));
    const Node* refresh = Method(roots, "splash_panel", "rbbz_refresh_menu");
    CHECK_TRUE(refresh != nullptr);
    if (refresh) {
      const Node* inventory = CompleteSplashArray(*refresh);
      CHECK_TRUE(inventory != nullptr);
      if (inventory) {
        CHECK_TRUE(std::any_of(inventory->kids.begin(), inventory->kids.end(),
            [](const Node& row) { return row.tag == dtb::kSymbol &&
                row.text == "splash_leaderboard"; }));
      }
    }
  }
  for (int current = 0; current < 4; ++current) {
    const auto found = records.find(kRecords[current]);
    CHECK_TRUE(found != records.end());
    if (found == records.end()) continue;
    std::vector<Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(found->second.data(), found->second.size(), &roots, &header, &why));
    for (const char* panel : kPanels[current]) {
      const auto* buttons = Method(roots, panel, "BUTTON_DOWN_MSG");
      CHECK_TRUE(buttons && Has(*buttons, "rbbz-menu/rhythmverse") &&
                 Has(*buttons, "rbbz-menu/messages") &&
                 Has(*buttons, "rbbz-menu/recommendations"));
    }
  }
  {
    const auto found = records.find("ui/song_select/gen/song_select.dtb");
    CHECK_TRUE(found != records.end());
    if (found != records.end()) {
      std::vector<uint8_t> selection_trial;
      CHECK_TRUE(settings::ultimate_runtime::PatchSongSelectionRecord(
          found->second, &selection_trial, &why));
      CHECK_FALSE(selection_trial.empty());
    }
  }
  const auto background = records.find("ui/background/gen/background.dtb");
  CHECK_TRUE(background != records.end());
  if (background != records.end()) {
    std::vector<Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(background->second.data(), background->second.size(), &roots, &header, &why));
    CHECK_TRUE(Method(roots, "background_panel", "rbbz_refresh_headers") != nullptr);
  }
  BeginCase("a refused splash refresh leaves the menu-return route unavailable");
  settings::RecordReader without_inventory = [&](const std::string& name,
                                                   std::vector<uint8_t>* bytes,
                                                   std::string* reason) {
    if (!read(name, bytes, reason)) return false;
    if (name != "ui/splash/gen/splash.dtb") return true;
    std::vector<Node> roots;
    dtb::RootHeader header;
    if (!dtb::Parse(bytes->data(), bytes->size(), &roots, &header, reason)) return false;
    for (Node& root : roots) {
      if (root.tag != dtb::kCommand || root.kids.size() < 3 ||
          root.kids[2].text != "splash_panel") continue;
      root.kids.erase(std::remove_if(root.kids.begin(), root.kids.end(), [](const Node& member) {
        return member.tag == dtb::kArray && !member.kids.empty() &&
               member.kids[0].text == "check_trial";
      }), root.kids.end());
    }
    *bytes = dtb::Compile(roots, header);
    return true;
  };
  settings::MenuRecords degraded_records;
  settings::MenuRefreshRoutes degraded_routes;
  notes.clear();
  CHECK_TRUE(settings::BuildMenuRecords(without_inventory,
      {"ui/locale/eng/gen/locale_keep.dtb"}, &degraded_records, &why,
      false, false, &notes, {}, false, &degraded_routes));
  CHECK_FALSE(degraded_routes.rows);
  CHECK_TRUE(degraded_routes.headers);
  CHECK_FALSE(degraded_routes.ready());
  return Finish();
}
int CheckInstalledHeaderRecords(const char* asset_root, const char* output_root,
                                bool single = false) {
  namespace fs = std::filesystem;
  namespace screen = rb_blitz::ui::screen;
  using namespace header_test;
  settings::RecordReader read = [&](const std::string& name, std::vector<uint8_t>* bytes, std::string* why) {
    std::ifstream file(fs::path(asset_root) / name, std::ios::binary);
    if (!file) { if (why) *why = "missing " + name; return false; }
    bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  };
  for (int boot_case = 0; boot_case < (single ? 1 : 16); ++boot_case) {
    const int mask = boot_case & 7;
    const bool fast = boot_case >= 8;
    const auto state = State(mask);
    screen::MenuConfiguration config;
    config.rhythmverse_row = state.store;
    config.show_messages = state.messages;
    config.show_recommendations = state.recommendations;
    config.show_currency = (boot_case & 1) != 0;
    config.controller_layout_enabled = single;
    screen::ConfigureMenu(config);
    settings::MenuRecords records;
    std::string why;
    std::vector<std::string> notes;
    const std::vector<std::string> locales = single ? std::vector<std::string>{
        "ui/locale/deu/gen/locale_keep.dtb", "ui/locale/eng/gen/locale_keep.dtb",
        "ui/locale/esl/gen/locale_keep.dtb", "ui/locale/fre/gen/locale_keep.dtb",
        "ui/locale/ita/gen/locale_keep.dtb", "ui/locale/ptb/gen/locale_keep.dtb"} :
        std::vector<std::string>{"ui/locale/eng/gen/locale_keep.dtb"};
    if (!settings::BuildMenuRecords(read, locales, &records,
                                    &why, false, false, &notes, {}, fast)) {
      std::printf("header records refused: %s\n", why.c_str()); return 1;
    }
    for (const auto& note : notes) std::printf("record note: %s\n", note.c_str());
    CHECK_TRUE(notes.empty());
    const auto error_flow = records.find("ui/net/gen/error_flow.dtb");
    CHECK_TRUE((error_flow != records.end()) == fast);
    if (error_flow != records.end()) {
      std::vector<dtb::Node> roots;
      dtb::RootHeader header;
      CHECK_TRUE(dtb::Parse(error_flow->second.data(), error_flow->second.size(),
                            &roots, &header, &why));
      const auto* enter = Method(roots, "error_screen", "enter");
      CHECK_TRUE(enter && enter->kids.size() == 3 &&
                 enter->kids[2].kids[1].kids[2].text ==
                     "error_signin_unlocked_full_game");
    }
    for (const auto& [path, panel, names] :
         std::array<std::tuple<const char*, const char*, std::array<const char*, 2>>, 2>{{
             {"ui/powerup_select/gen/powerup_select.dtb", "powerup_select_panel",
              {"your_monies_text.lbl", "your_cred.lbl"}},
             {"ui/results_hub/gen/song_results.dtb", "song_results_panel",
              {"Coins.grp", "cred.grp"}}}}) {
      const auto found = records.find(path);
      CHECK_TRUE((found != records.end()) == !config.show_currency);
      if (found == records.end()) continue;
      std::vector<dtb::Node> roots;
      dtb::RootHeader header;
      CHECK_TRUE(dtb::Parse(found->second.data(), found->second.size(), &roots, &header, &why));
      const auto* enter = Method(roots, panel, "enter");
      CHECK_TRUE(enter && enter->kids.size() >= 3);
      if (!enter || enter->kids.size() < 3) continue;
      for (size_t i = 0; i < 2; ++i) {
        const auto& hide = enter->kids[enter->kids.size() - 2 + i];
        CHECK_TRUE(hide.tag == dtb::kCommand && hide.kids.size() == 3 &&
                   hide.kids[0].text == names[i] && hide.kids[1].text == "set_showing" &&
                   hide.kids[2].text == "FALSE");
      }
    }
    if (boot_case == 0) {
      // The crash remedy: the Xbox guest heap is sized from this record at boot, and the
      // retail 137,000,000-byte definition cannot satisfy a fragmented 1 MiB texture
      // request. The generated record must carry the raised value or the transform is a
      // no-op that only looks applied.
      {
        namespace dtb = rb_blitz::ui::dtb;
        const auto mem = records.find("config/gen/mem.dtb");
        CHECK_TRUE(mem != records.end());
        if (mem != records.end()) {
          std::vector<dtb::Node> mem_roots;
          dtb::RootHeader mem_header;
          std::string mem_why;
          CHECK_TRUE(dtb::Parse(mem->second.data(), mem->second.size(), &mem_roots,
                                &mem_header, &mem_why));
          uint32_t xbox_main = 0;
          for (const auto& root : mem_roots) {
            if (root.tag != dtb::kArray || root.kids.size() < 3 ||
                root.kids[0].text != "heaps") continue;
            for (size_t i = 1; i + 1 < root.kids.size(); ++i) {
              if (root.kids[i].tag != dtb::kIfdef || root.kids[i].text != "HX_XBOX") continue;
              for (size_t j = i + 1; j < root.kids.size() && root.kids[j].tag == dtb::kArray; ++j) {
                const auto& heap = root.kids[j];
                if (heap.kids.size() < 2 || heap.kids[0].text != "main") continue;
                const auto& size = heap.kids[1];
                if (size.tag == dtb::kArray && size.kids.size() >= 2 &&
                    size.kids[0].text == "size" && size.kids[1].tag == dtb::kInt)
                  xbox_main = size.kids[1].scalar;
              }
            }
          }
          CHECK_EQ(xbox_main, uint32_t{160000000});
        }
      }
      const auto& row_font = records.at(rb_blitz::ui::input_font::kIconSceneRecord);
      CheckIconScene(row_font);
      {
        // Project the resting speaker's visible alpha bounds through its mesh
        // and parent transforms, independently of the font configuration.
        const auto& scene = records.at(rb_blitz::ui::input_font::kCalibrationRecord);
        const auto f = [&](size_t at) {
          const auto* p = scene.data() + 0x810 + at;
          return std::bit_cast<float>(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
                                      uint32_t(p[2]) << 8 | p[3]);
        };
        constexpr size_t mesh = 0x327362, group = 0x325DFF;
        const float u = 71.5f / 256, v = 135.f / 256;
        const float left = f(mesh + 0xE8 + 3 * 36);
        const float x = left + u * (f(mesh + 0xE8 + 36) - left) +
                               v * (f(mesh + 0xE8 + 2 * 36) - left);
        const float world_x = f(group + 0x75) + f(group + 0x21) * (f(mesh + 0x39) + x);
        constexpr std::string_view token = "calibration_countdown_go";
        const auto at = std::search(scene.begin(), scene.end(), token.begin(), token.end());
        CHECK_TRUE(at != scene.end());
        if (at != scene.end()) {
          const size_t label = size_t(at - scene.begin()) - 0x810 - 0xB9;
          CHECK_TRUE(std::abs(world_x - f(label + 0x6D)) < .001f);
        }
        CHECK_TRUE(std::abs(f(group + 0x75) - f(group + 0x45) -
                            (f(0x325CB3 + 0x45))) < .001f);
      }
      const auto original_buttons = rb_blitz::ui::input_font::OriginalButtonsBc3();
      CHECK_TRUE(original_buttons != nullptr);
      if (original_buttons) {
        CHECK_EQ(original_buttons->size(), size_t{512 * 512});
        CHECK_TRUE(std::any_of(original_buttons->begin(), original_buttons->end(),
                               [](uint8_t byte) { return byte != 0; }));
      }
      if (single) {
        const auto calibration = rb_blitz::ui::input_font::OriginalCalibrationButtons();
        CHECK_TRUE(calibration != nullptr);
        if (calibration) {
          for (const auto& frames : *calibration) {
            CHECK_EQ(frames.size(), size_t{5});
            for (size_t mip = 0; mip < frames.size(); ++mip)
              CHECK_EQ(frames[mip].size(), size_t{(256u >> mip) * (256u >> mip)});
          }
          CHECK_TRUE((*calibration)[0][0] != (*calibration)[1][0]);
          CHECK_TRUE((*calibration)[2][0] != (*calibration)[3][0]);
        }
      }
      CheckPrivateFontGeometry(row_font, false);
      const auto& hint_font = records.at(rb_blitz::ui::input_font::kHintSceneRecord);
      CheckIconScene(hint_font);
      CheckPrivateFontGeometry(hint_font, true);
      CHECK_TRUE(hint_font != records.at(rb_blitz::ui::input_font::kIconSceneRecord));
      CHECK_TRUE(std::equal(row_font.begin() + 0x810 + 0x867,
                            row_font.begin() + 0x810 + 0x867 + 4,
                            hint_font.begin() + 0x810 + 0x867));
      if (single) {
        const auto& loading = records.at(rb_blitz::ui::input_font::kLoadingRecord);
        auto has_resource = [](const std::vector<uint8_t>& bytes,
                               std::string_view name) {
          return std::search(bytes.begin(), bytes.end(), name.begin(), name.end()) !=
                 bytes.end();
        };
        const auto& row_font = records.at(rb_blitz::ui::input_font::kIconSceneRecord);
        CHECK_TRUE(has_resource(row_font, "Iconatonic_Regular_(8_89)4x.tex"));
        CHECK_TRUE(has_resource(hint_font, "Iconatonic_Regular_(8_89)4x.tex"));
        CHECK_FALSE(has_resource(hint_font, "Hintatonic_Regular_"));
        CHECK_TRUE(has_resource(hint_font, "../image/iconatonic_regular_(8_89)4x.bmp"));
        constexpr std::string_view loading_font =
            "../resource/fonts/inline_help_icons.milo";
        size_t loading_font_count = 0;
        for (auto at = std::search(loading.begin(), loading.end(),
                                  loading_font.begin(), loading_font.end());
             at != loading.end();
             at = std::search(at + loading_font.size(), loading.end(),
                              loading_font.begin(), loading_font.end()))
          ++loading_font_count;
        CHECK_EQ(loading_font_count, size_t{5});
        constexpr std::string_view retail_button_font = "../resource/fonts/buttons.milo";
        size_t retail_button_font_count = 0;
        for (auto at = std::search(loading.begin(), loading.end(),
                                  retail_button_font.begin(), retail_button_font.end());
             at != loading.end();
             at = std::search(at + retail_button_font.size(), loading.end(),
                              retail_button_font.begin(), retail_button_font.end()))
          ++retail_button_font_count;
        CHECK_EQ(retail_button_font_count, size_t{0});
        const auto& splash = records.at(rb_blitz::ui::input_font::kSplashRecord);
        size_t splash_hint_fonts = 0;
        for (auto at = std::search(splash.begin(), splash.end(),
                                  loading_font.begin(), loading_font.end());
             at != splash.end();
             at = std::search(at + loading_font.size(), splash.end(),
                              loading_font.begin(), loading_font.end()))
          ++splash_hint_fonts;
        CHECK_EQ(splash_hint_fonts, size_t{1});
        CHECK_FALSE(has_resource(splash, "../resource/fonts/buttons.milo"));
        const auto& song_select = records.at(rb_blitz::ui::input_font::kSongSelectRecord);
        CHECK_TRUE(has_resource(song_select, loading_font));
        CHECK_FALSE(has_resource(song_select, "../resource/fonts/buttons.milo"));
        auto count_resource = [](const std::vector<uint8_t>& bytes,
                                 std::string_view name) {
          size_t count = 0;
          for (auto at = std::search(bytes.begin(), bytes.end(), name.begin(), name.end());
               at != bytes.end();
               at = std::search(at + name.size(), bytes.end(), name.begin(), name.end()))
            ++count;
          return count;
        };
        CHECK_EQ(count_resource(records.at(rb_blitz::ui::input_font::kCalibrationRecord),
                                loading_font), size_t{4});
        CHECK_EQ(count_resource(records.at(rb_blitz::ui::input_font::kHudRecord),
                                "../ui/resource/fonts/inline_help_icons.milo"), size_t{4});
        CHECK_EQ(count_resource(records.at(rb_blitz::ui::input_font::kPowerupSelectRecord),
                                loading_font), size_t{1});
        CHECK_EQ(count_resource(records.at(rb_blitz::ui::input_font::kErrorReturnRecord),
                                loading_font), size_t{1});
        CHECK_EQ(count_resource(records.at(rb_blitz::ui::input_font::kAccountLinkRecord),
                                loading_font), size_t{1});
        {
          namespace dtb = rb_blitz::ui::dtb;
          const auto& config = records.at("config/gen/beatmatch_controller.dtb");
          std::vector<dtb::Node> roots;
          dtb::RootHeader header;
          CHECK_TRUE(dtb::Parse(config.data(), config.size(), &roots, &header, &why));
          size_t tables = 0;
          auto check_names = [&](auto&& self, const dtb::Node& node) -> void {
            if (node.tag == dtb::kArray && node.kids.size() == 2 &&
                node.kids[0].text == "button_names") {
              ++tables;
              CHECK_EQ(node.kids[1].kids.size(), size_t{5});
              for (const auto& row : node.kids[1].kids)
                CHECK_TRUE(row.kids.size() == 2 &&
                           row.kids[1].text.starts_with("rbbz_hint_"));
            }
            for (const auto& child : node.kids) self(self, child);
          };
          for (const auto& root : roots) check_names(check_names, root);
          CHECK_EQ(tables, size_t{4});
        }
        CHECK_FALSE(records.contains("ui/resource/fonts/gen/hintbtn.milo_xbox"));
      } else {
        CHECK_FALSE(records.contains(rb_blitz::ui::input_font::kLoadingRecord));
        CHECK_FALSE(records.contains(rb_blitz::ui::input_font::kSplashRecord));
        CHECK_FALSE(records.contains(rb_blitz::ui::input_font::kSongSelectRecord));
        CHECK_TRUE(records.contains(rb_blitz::ui::input_font::kCalibrationRecord));
        CHECK_FALSE(records.contains(rb_blitz::ui::input_font::kHudRecord));
        CHECK_FALSE(records.contains(rb_blitz::ui::input_font::kAccountLinkRecord));
        CHECK_FALSE(records.contains("ui/resource/fonts/gen/hintbtn.milo_xbox"));
      }
      const auto& help = records.at(rb_blitz::ui::input_font::kInlineHelpRecord);
      constexpr std::string_view hint_path = "../fonts/inline_help_icons.milo";
      CHECK_TRUE(std::search(help.begin(), help.end(),
          hint_path.begin(), hint_path.end())
          != help.end());
      std::vector<rb_blitz::ui::dtb::Node> locale_roots;
      rb_blitz::ui::dtb::RootHeader locale_header;
      CHECK_TRUE(rb_blitz::ui::dtb::Parse(
          records.at("ui/locale/eng/gen/locale_keep.dtb").data(),
          records.at("ui/locale/eng/gen/locale_keep.dtb").size(),
          &locale_roots, &locale_header, &why));
      bool numbered_alt = false;
      bool loading_confirm = false;
      bool loading_powerup = false;
      bool splash_start = false;
      bool song_sort = false;
      for (const auto& root : locale_roots)
        if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2) {
          if (root.kids[0].text == "inline_help_fmt" &&
              root.kids[1].text == "<alt1>%s</alt1> %s") numbered_alt = true;
          if (root.kids[0].text == "loading_press_button" &&
              root.kids[1].text.find("<alt1>\xC2\x81</alt1>") != std::string::npos)
            loading_confirm = true;
          if (root.kids[0].text == "loading_tip_03" &&
              root.kids[1].text.find("<alt1>\xC2\x86</alt1>") != std::string::npos)
            loading_powerup = true;
          if (root.kids[0].text == "splash_press_start" &&
              root.kids[1].text.find("<alt1>\xC2\x80</alt1>") != std::string::npos)
            splash_start = true;
          if (root.kids[0].text == "song_sort_str" &&
              root.kids[1].text.find("<alt1>\xC2\x85</alt1>") != std::string::npos)
            song_sort = true;
        }
      CHECK_TRUE(numbered_alt);
      CHECK_EQ(loading_confirm, single);
      CHECK_EQ(loading_powerup, single);
      CHECK_EQ(splash_start, single);
      CHECK_EQ(song_sort, single);
      for (const auto& locale : locales) {
        std::vector<rb_blitz::ui::dtb::Node> localized;
        rb_blitz::ui::dtb::RootHeader localized_header;
        CHECK_TRUE(rb_blitz::ui::dtb::Parse(records.at(locale).data(),
            records.at(locale).size(), &localized, &localized_header, &why));
        size_t prompts = 0, powerup_tips = 0, splash_starts = 0, sort_labels = 0;
        for (const auto& root : localized)
          if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2 &&
              root.kids[0].text == "loading_press_button" &&
              root.kids[1].text.find("<alt1>\xC2\x81</alt1>") != std::string::npos)
            ++prompts;
        for (const auto& root : localized)
          if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2 &&
              root.kids[0].text == "loading_tip_03" &&
              root.kids[1].text.find("<alt1>\xC2\x86</alt1>") != std::string::npos)
            ++powerup_tips;
        for (const auto& root : localized)
          if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2 &&
              root.kids[0].text == "splash_press_start" &&
              root.kids[1].text.find("<alt1>\xC2\x80</alt1>") != std::string::npos)
            ++splash_starts;
        for (const auto& root : localized)
          if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2 &&
              root.kids[0].text == "song_sort_str" &&
              root.kids[1].text.find("<alt1>\xC2\x85</alt1>") != std::string::npos)
            ++sort_labels;
        CHECK_EQ(prompts, single ? size_t{1} : size_t{0});
        CHECK_EQ(powerup_tips, single ? size_t{1} : size_t{0});
        CHECK_EQ(splash_starts, single ? size_t{1} : size_t{0});
        CHECK_EQ(sort_labels, single ? size_t{1} : size_t{0});
        if (single) {
          size_t account_links = 0;
          for (const auto& root : localized)
            if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2 &&
                root.kids[0].text == "link_facebook_email_entry" &&
                root.kids[1].text.find("<alt1>\xC2\x81</alt1>") != std::string::npos)
              ++account_links;
          CHECK_EQ(account_links, size_t{3});
          for (const auto& [key, glyph] :
               std::array<std::pair<std::string_view, std::string_view>, 17>{{
                   {"cal_audio_desc_pad", "<alt1>\xC2\x81</alt1>"},
                   {"cal_video_desc_pad", "<alt1>\xC2\x81</alt1>"},
                   {"error_return_button", "<alt1>\xC2\x81</alt1>"},
                   {"remedial_help_powerup", "<alt1>\xC2\x86</alt1>"},
                   {"active_description", "<alt1>\xC2\x86</alt1>"},
                   {"autoplay_desc", "<alt1>\xC2\x86</alt1>"},
                   {"grenade_desc", "<alt1>\xC2\x86</alt1>"},
                   {"jackpot_desc", "<alt1>\xC2\x86</alt1>"},
                   {"overdrive_desc", "<alt1>\xC2\x86</alt1>"},
                   {"plasma_desc", "<alt1>\xC2\x86</alt1>"},
                   {"ripple_desc", "<alt1>\xC2\x86</alt1>"},
                   {"press_start_to_continue", "<alt1>\xC2\x8D</alt1>"},
                   {"rbbz_hint_first_note", "<alt1>\xC2\x87</alt1>"},
                   {"rbbz_hint_second_note", "<alt1>\xC2\x88</alt1>"},
                   {"rbbz_hint_move_right", "<alt1>\xC2\x89</alt1>"},
                   {"rbbz_hint_move_left", "<alt1>\xC2\x8B</alt1>"},
                   {"rbbz_hint_powerup", "<alt1>\xC2\x86</alt1>"}}}) {
            size_t found = 0;
            for (const auto& root : localized)
              if (root.tag == rb_blitz::ui::dtb::kArray && root.kids.size() >= 2 &&
                  root.kids[0].text == key &&
                  root.kids[1].text.find(glyph) != std::string::npos) ++found;
            CHECK_EQ(found, size_t{1});
          }
        }
      }
    }
    CHECK_TRUE(records.contains("ui/startup/gen/startup.dtb"));
    const auto dir = fs::path(output_root) / (std::string(fast ? "fast-" : "normal-") + std::to_string(mask));
    for (const auto& [name, bytes] : records) {
      if (boot_case == 0 && (name == rb_blitz::ui::input_font::kIconSceneRecord ||
                            name == rb_blitz::ui::input_font::kListRecord)) {
        fs::create_directories((dir / name).parent_path());
        std::ofstream file(dir / name, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        CHECK_TRUE(bool(file));
      }
      if (!name.ends_with(".dtb")) continue;
      std::vector<Node> roots;
      dtb::RootHeader header;
      if (!dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why) || dtb::Compile(roots, header) != bytes) {
        std::printf("header records invalid: %s: %s\n", name.c_str(), why.c_str()); return 1;
      }
      if (name == "ui/background/gen/background.dtb") CheckBar(roots, mask);
      if (name == "ui/startup/gen/startup.dtb") {
        for (const auto& root : roots) {
          if (root.kids.empty()) continue;
          if (root.kids[0].text == "splash_order") CHECK_TRUE(fast ? root.kids.size() == 1u : root.kids.size() > 1u);
          if (root.kids[0].text == "wait_for_splash") CHECK_TRUE(root.kids[1].text == "FALSE");
        }
      }
      if (name == "ui/gen/transition_screens.dtb") CheckAnimations(roots, mask);
      if (name == "ui/locale/eng/gen/locale_keep.dtb") {
        // P9-7/P9-10's store entries ride in every locale the payload edits while the store is on:
        // the Start caption and the failed-press sentence. Off leaves the locale alone.
        bool filters = false;
        bool unreachable = false;
        for (const auto& root : roots) {
          if (root.tag != dtb::kArray || root.kids.size() < 2) continue;
          if (root.kids[0].text == "inline_help_search_filters") filters = true;
          if (root.kids[0].text == screen::kStoreUnreachableKey) unreachable = true;
        }
        CHECK_TRUE(filters == state.store);
        CHECK_TRUE(unreachable == state.store);
      }
      for (int current = 0; current < 4; ++current) if (name == kRecords[current]) CheckRoutes(roots, current, mask);
      fs::create_directories((dir / name).parent_path());
      std::ofstream file(dir / name, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!file) return 1;
    }
    for (const char* path : kRecords) CHECK_TRUE(records.contains(path));
    CHECK_TRUE(records.contains("ui/background/gen/background.dtb"));
    CHECK_TRUE(records.contains("ui/gen/transition_screens.dtb"));
    std::printf("store=%d messages=%d recommendations=%d: %zu records round-tripped, %zu refusal(s)\n",
                state.store, state.messages, state.recommendations, records.size(), notes.size());
  }
  screen::ConfigureMenu({});
  return Finish();
}

void TestDiscoveryWaitingLabel() {
  using namespace rb_blitz;
  using ui::dtb::Node;
  namespace dtb = ui::dtb;
  BeginCase("pending host scans display content loading and retain real connection labels");
  const auto make = [](Node key) {
    return std::vector<Node>{Node::Command({Node::Symbol("new"),
      Node::Symbol("ServerConnectPanel"), Node::Symbol("server_connect_panel"),
      Node::Array({Node::Symbol("update_state"), Node::Array({Node::Var("state")}),
        Node::Command({Node::Symbol("status.lbl"), Node::Symbol("set"), Node::Symbol("text_token"),
          Node::Command({Node::Symbol("switch"), Node::Var("state"),
            Node::Array({std::move(key), Node::Symbol(menu_options::kEnumeratingContentKey)}),
            Node::Symbol("server_connect_connecting")})})})})};
  };
  for (Node key : {Node::Int(29), Node::Symbol("kServerConnectPanel_EnumeratingContent")}) {
    auto roots = make(key);
    CHECK_TRUE(settings::SetDiscoveryStartLabel(&roots));
    const Node& choices = roots[0].kids[3].kids[2].kids[3];
    CHECK_EQ(choices.kids.size(), 5);
    CHECK_EQ(choices.kids[3].kids[0].tag, dtb::kInt);
    CHECK_EQ(choices.kids[3].kids[0].scalar, 28);
    CHECK_TRUE(choices.kids[3].kids[1].text == menu_options::kEnumeratingContentKey);
    CHECK_TRUE(choices.kids.back().text == "server_connect_connecting");
    const auto bytes = dtb::Compile(roots);
    CHECK_TRUE(settings::SetDiscoveryStartLabel(&roots));
    CHECK_TRUE(dtb::Compile(roots) == bytes);
    std::vector<Node> parsed;
    dtb::RootHeader header;
    std::string why;
    CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  }
  auto unknown = make(Node::Int(99));
  const auto bytes = dtb::Compile(unknown);
  CHECK_FALSE(settings::SetDiscoveryStartLabel(&unknown));
  CHECK_TRUE(dtb::Compile(unknown) == bytes);
}

// Evaluate the authored recovery branch with a failed host preparation. The
// important boundary is navigation: retry must never reenter content discovery,
// while Ignore must execute the saved menu continuation exactly once.
void TestGraphicsRecovery(const std::vector<rb_blitz::ui::dtb::Node>& roots, bool portrait = false) {
  using namespace rb_blitz::ui;
  using dtb::Node;
  struct Machine {
    const std::vector<Node>& roots;
    bool pending = true, failed = true;
    int retries = 0, ignores = 0, skips = 0, menus = 0, discoveries = 0, cancels = 0;
    std::string action, splash_state;
    std::map<std::string,std::string> hints;
    std::map<std::string,bool> showing;
    bool Truth(const Node& n) { auto v = Eval(n); return v.tag == dtb::kInt ? v.scalar != 0 : v.text == "TRUE"; }
    Node Method(const std::string& name) {
      const auto* method = header_test::Method(roots,"server_connect_panel",name.c_str());
      CHECK_TRUE(method != nullptr);
      Node result = Node::Int(0);
      if (method) for (size_t i=1;i<method->kids.size();++i) result = Eval(method->kids[i]);
      return result;
    }
    Node Eval(const Node& n) {
      if (n.tag == dtb::kProperty) return Node::Int(pending);
      if (n.tag == dtb::kVar && n.text == "action") return Node::Symbol(action);
      if (n.tag != dtb::kCommand || n.kids.empty()) return n;
      const auto& op = n.kids[0].text;
      if (op == "file_exists") {
        const auto& path = n.kids[1].text;
        if (path == "rbbz-graphics/failed") return Node::Int(failed);
        if (path == "rbbz-graphics/retry") { ++retries; failed=false; return Node::Int(1); }
        if (path == "rbbz-graphics/ignore") { ++ignores; return Node::Int(1); }
        if (path == "rbbz-graphics/skip") { ++skips; return Node::Int(1); }
        if (path == "rbbz-graphics/cancel") { ++cancels; return Node::Int(0); }
        if (path == "rbbz-graphics/checking") return Node::Int(1);
        return Node::Int(0);
      }
      if (op == "if_else") return Eval(n.kids[Truth(n.kids[1])?2:3]);
      if (op == "if" || op == "unless") {
        if (Truth(n.kids[1]) == (op=="if"))
          for (size_t i=2;i<n.kids.size();++i) Eval(n.kids[i]);
        return Node::Int(0);
      }
      if (op == "&&") return Node::Int(Truth(n.kids[1]) && Truth(n.kids[2]));
      if (op == "||") return Node::Int(Truth(n.kids[1]) || Truth(n.kids[2]));
      if (op == "!") return Node::Int(!Truth(n.kids[1]));
      if (op == "==") { auto a=Eval(n.kids[1]), b=Eval(n.kids[2]); return Node::Int(a.tag==b.tag && a.text==b.text && a.scalar==b.scalar); }
      if (op == "do") { for (size_t i=1;i<n.kids.size();++i) Eval(n.kids[i]); return Node::Int(0); }
      if (op == "switch") {
        const auto value=Eval(n.kids[1]);
        for (size_t i=2;i<n.kids.size();++i)
          if (!n.kids[i].kids.empty() && n.kids[i].kids[0].text==value.text)
            for (size_t j=1;j<n.kids[i].kids.size();++j) Eval(n.kids[i].kids[j]);
        return Node::Int(0);
      }
      if (op == "set" && n.kids[1].tag==dtb::kProperty) { pending=Truth(n.kids[2]); return Node::Int(0); }
      if (n.kids[0].tag == dtb::kCommand && n.kids.size() == 4 &&
          n.kids[1].text == "set" && n.kids[2].text == "state") splash_state = n.kids[3].text;
      if (n.kids[0].tag==dtb::kVar && op=="this") {
        if (n.kids[1].text=="start_connect_process") { ++discoveries; return Node::Int(0); }
        return Method(n.kids[1].text);
      }
      if (op=="ui" && n.kids[1].text=="goto_screen") ++menus;
      if (op=="content_mgr" || op=="saveload_mgr") ++discoveries;
      if (n.kids.size()>=4 && n.kids[1].text=="set_action_token") hints[op]=Eval(n.kids[3]).text;
      if (n.kids.size()>=3 && n.kids[1].text=="set_showing") showing[op]=Truth(n.kids[2]);
      return Node::Int(0);
    }
  };
  BeginCase("failed preparation exposes native A retry and B ignore; retry preserves DLC loading");
  Machine retry{roots};
  retry.Method("rbbz_graphics_poll");
  CHECK_TRUE(retry.showing["continue.ihp"] && retry.showing["back.ihp"]);
  CHECK_TRUE(retry.hints["continue.ihp"]==(portrait ? "rbbz_loading_retry" : "rbbz_graphics_retry"));
  CHECK_TRUE(retry.hints["back.ihp"]=="rbbz_graphics_ignore");
  retry.action="kAction_Confirm";
  retry.Method("BUTTON_DOWN_MSG");
  CHECK_EQ(retry.retries,1); CHECK_EQ(retry.discoveries,0); CHECK_EQ(retry.menus,0);
  CHECK_TRUE(retry.pending);
  retry.Method("BUTTON_DOWN_MSG");
  CHECK_EQ(retry.retries,1);
  BeginCase("Ignore executes the menu continuation and retires the guest graphics gate");
  Machine ignore{roots}; ignore.action="kAction_Cancel";
  ignore.Method("BUTTON_DOWN_MSG");
  CHECK_EQ(ignore.ignores,1); CHECK_EQ(ignore.discoveries,0); CHECK_EQ(ignore.menus,1);
  CHECK_FALSE(ignore.pending);
  ignore.Method("rbbz_graphics_poll");
  CHECK_EQ(ignore.menus,1);
  BeginCase("Skip during preparation continues to the main menu exactly once");
  Machine skip{roots}; skip.failed = false; skip.action = portrait ? "kAction_Confirm" : "kAction_Cancel";
  skip.Method("rbbz_graphics_poll");
  CHECK_TRUE(skip.hints[portrait ? "continue.ihp" : "back.ihp"] == "rbbz_graphics_back");
  if (portrait) {
    CHECK_TRUE(skip.showing["continue.ihp"] && skip.showing["back.ihp"]);
    CHECK_TRUE(skip.hints["back.ihp"] == "inline_help_back");
  }
  skip.Method("BUTTON_DOWN_MSG");
  CHECK_EQ(skip.skips,1); CHECK_EQ(skip.ignores,0); CHECK_EQ(skip.discoveries,0);
  CHECK_EQ(skip.menus,1); CHECK_FALSE(skip.pending);
  skip.Method("rbbz_graphics_poll");
  CHECK_EQ(skip.menus,1);
  Machine preparing{roots}; preparing.failed = false;
  if (portrait) {
    BeginCase("portrait Back cancels preparation and returns to the Start prompt");
    preparing.action = "kAction_Cancel";
    preparing.Method("BUTTON_DOWN_MSG");
    CHECK_FALSE(preparing.pending); CHECK_EQ(preparing.cancels, 1);
    CHECK_EQ(preparing.skips, 0); CHECK_EQ(preparing.discoveries, 0);
    CHECK_TRUE(preparing.splash_state == "start");
  } else {
    BeginCase("Confirm while preparing neither skips nor restarts discovery");
    preparing.action = "kAction_Confirm";
    preparing.Method("BUTTON_DOWN_MSG");
    CHECK_TRUE(preparing.pending); CHECK_EQ(preparing.skips,0);
    CHECK_EQ(preparing.discoveries,0); CHECK_EQ(preparing.menus,0);
  }
}

void TestLoadedMenuRefresh() {
  using rb_blitz::ui::dtb::Node;
  using rb_blitz::ui::dtb::kArray;
  using rb_blitz::ui::dtb::kCommand;
  BeginCase("loaded splash refresh rebuilds from the pre-hide inventory and keeps selection by symbol");
  const auto sym = [](const char* s) { return Node::Symbol(s); };
  std::vector<Node> roots{Node::Command({sym("new"), sym("UIPanel"), sym("splash_panel"),
      Node::Array({sym("enter"), Node::Command({sym("this"), sym("check_trial")})}),
      Node::Array({sym("check_trial"), Node::Array({Node::Var("options_array"),
          Node::Command({sym("array"), Node::Array({sym("splash_start"),
              sym("splash_play_current"), sym("splash_dlc"), sym("splash_options"),
              sym("splash_exit")})})})})}),
      Node::Command({sym("new"), sym("UIScreen"), sym("splash_screen"),
          Node::Array({sym("enter"), Node::Command({sym("splash_panel"),
                                                    sym("check_trial")})})})};
  const auto source = roots;
  std::string why;
  Node inventory;
  CHECK_TRUE(rb_blitz::settings::CaptureMainMenuInventory(roots, &inventory, &why));
  CHECK_EQ(inventory.kids.size(), 5u);
  CHECK_TRUE(rb_blitz::ui::screen::HideMenuRows(&roots,
      {"splash_exit", "splash_play_current", "splash_dlc"}, &why));
  CHECK_TRUE(rb_blitz::settings::AddLoadedMainMenuRefresh(&roots, inventory, false, &why));
  CHECK_EQ(roots[0].kids.size(), 6u);
  const Node& enter = roots[1].kids[3];
  CHECK_TRUE(enter.kids.size() == 4 && enter.kids[1].kids[0].text == "set" &&
             enter.kids[2].kids[1].text == "check_trial" &&
             enter.kids[3].kids[1].text == "rbbz_refresh_menu");
  const Node& refresh = roots[0].kids.back();
  CHECK_TRUE(refresh.tag == kArray && refresh.kids[0].text == "rbbz_refresh_menu");
  CHECK_TRUE(refresh.kids.size() == 2 && refresh.kids[1].tag == kCommand &&
             refresh.kids[1].kids[0].text == "do");
  const Node& statements = refresh.kids[1];
  // Exit visibility must be queried on every return, despite being absent at boot.
  bool filters_exit = false;
  for (const auto& statement : statements.kids) {
    if (statement.tag != kCommand || statement.kids.size() != 3 ||
        statement.kids[0].text != "if") continue;
    const auto& query = statement.kids[1];
    const auto& remove = statement.kids[2];
    if (query.kids.size() == 2 && query.kids[1].text == "rbbz-menu/hidden/splash_exit" &&
        remove.kids.size() == 3 && remove.kids[0].text == "remove_elem" &&
        remove.kids[2].text == "splash_exit") filters_exit = true;
  }
  CHECK_TRUE(filters_exit);
  CHECK_TRUE(statements.kids[1].tag == kArray &&
             statements.kids[1].kids.size() == 2 &&
             statements.kids[1].kids[0].tag == rb_blitz::ui::dtb::kVar &&
             statements.kids[1].kids[0].text == "options_array" &&
             statements.kids[1].kids[1].kids[1].tag == kArray &&
             statements.kids[1].kids[1].kids[1].kids.size() == 5);
  const Node& selection = statements.kids[statements.kids.size() - 2];
  CHECK_TRUE(selection.tag == kCommand && selection.kids[0].text == "switch");
  CHECK_TRUE(statements.kids.back().kids.size() == 2 &&
             statements.kids.back().kids[1].text == "rbbz-menu/rows-applied");
  {
    const auto evaluate = [&](auto&& self, const Node& node, bool play, bool refresh_on) -> int {
      if (node.tag == rb_blitz::ui::dtb::kInt) return int(node.scalar);
      if (node.tag != kCommand || node.kids.empty()) return -100;
      if (node.kids[0].text == "+")
        return self(self, node.kids[1], play, refresh_on) +
               self(self, node.kids[2], play, refresh_on);
      if (node.kids[0].text == "if_else") {
        const std::string& path = node.kids[1].kids[1].text;
        const bool visible = path == "rbbz-menu/visible/splash_play_current" ? play :
                             path == "rbbz-menu/visible/splash_dlc" ? refresh_on : false;
        return self(self, node.kids[visible ? 2 : 3], play, refresh_on);
      }
      return -100;
    };
    const std::array<const char*, 5> previous = {"splash_start", "splash_play_current",
        "splash_dlc", "splash_options", "splash_exit"};
    for (int mask = 0; mask < 4; ++mask) {
      const bool play = (mask & 1) != 0, dlc = (mask & 2) != 0;
      const std::array<int, 5> expected = {0, 1, 1 + int(play),
          1 + int(play) + int(dlc), 2 + int(play) + int(dlc)};
      for (size_t row = 0; row < previous.size(); ++row) {
        const Node* selected = nullptr;
        for (const auto& kid : selection.kids)
          if (kid.tag == kArray && !kid.kids.empty() && kid.kids[0].text == previous[row])
            selected = &kid;
        CHECK_TRUE(selected && selected->kids.size() == 2 &&
                   selected->kids[1].kids.size() == 3 &&
                   selected->kids[1].kids[1].text == "set_selected");
        if (selected) CHECK_EQ(evaluate(evaluate, selected->kids[1].kids[2], play, dlc),
                               expected[row]);
      }
    }
    // An unknown previous symbol falls through the switch and keeps the initial first-row choice.
    CHECK_TRUE(statements.kids[statements.kids.size() - 3].kids.size() == 3 &&
               statements.kids[statements.kids.size() - 3].kids[1].text == "set_selected" &&
               statements.kids[statements.kids.size() - 3].kids[2].scalar == 0);
  }
  const auto bytes = rb_blitz::ui::dtb::Compile(roots);
  std::vector<Node> decoded;
  rb_blitz::ui::dtb::RootHeader header;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &decoded, &header, &why));
  const auto before = rb_blitz::ui::dtb::Compile(roots);
  CHECK_FALSE(rb_blitz::settings::AddLoadedMainMenuRefresh(&roots, inventory, false, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::Compile(roots) == before);
  for (bool duplicate : {false, true}) {
    auto incompatible = source;
    auto& screen_enter = incompatible[1].kids[3];
    if (duplicate) screen_enter.kids.push_back(screen_enter.kids[1]);
    else screen_enter.kids.erase(screen_enter.kids.begin() + 1);
    const auto original = rb_blitz::ui::dtb::Compile(incompatible);
    CHECK_FALSE(rb_blitz::settings::AddLoadedMainMenuRefresh(
        &incompatible, inventory, false, &why));
    CHECK_TRUE(rb_blitz::ui::dtb::Compile(incompatible) == original);
  }
}

void TestIOSAutoPause() {
  using rb_blitz::settings::AddIOSAutoPause;
  using rb_blitz::ui::dtb::Node;
  BeginCase("iOS lifecycle preserves guest callbacks and uses the guarded native pause action");
  const auto sym = [](const char* name) { return Node::Symbol(name); };
  const auto original_poll = Node::Command({sym("original_poll")});
  std::vector<Node> roots{
      Node::Command({sym("new"), sym("GamePanel"), sym("game_panel"),
          Node::Array({sym("poll"), original_poll})}),
      Node::Command({sym("new"), sym("GameScreen"), sym("game_screen"),
          Node::Array({sym("enter"), Node::Command({sym("original_enter")})}),
          Node::Array({sym("exit"), Node::Command({sym("original_exit")})}),
          Node::Array({sym("check_pause"), Node::Command({sym("native_pause_guard")})})})};
  std::string why;
  const auto before = roots;
  CHECK_TRUE(AddIOSAutoPause(&roots, &why));
  const auto* enter = header_test::Method(roots, "game_screen", "enter");
  const auto* exit = header_test::Method(roots, "game_screen", "exit");
  const auto* poll = header_test::Method(roots, "game_panel", "poll");
  CHECK_EQ(enter->kids.size(), size_t(3));
  CHECK_EQ(exit->kids.size(), size_t(3));
  CheckStrEq(enter->kids[1].kids[0].text, "original_enter");
  CheckStrEq(exit->kids[1].kids[0].text, "original_exit");
  CheckStrEq(poll->kids[1].kids[0].text, "original_poll");
  CheckStrEq(enter->kids.back().kids[1].text, "rbbz-ios/game/enter");
  CheckStrEq(exit->kids.back().kids[1].text, "rbbz-ios/game/exit");
  const auto& request = poll->kids.back();
  CheckStrEq(request.kids[1].kids[1].kids[1].text, "can_pause");
  CheckStrEq(request.kids[1].kids[2].kids[1].text, "rbbz-ios/pause-request");
  CheckStrEq(request.kids[2].kids[1].text, "check_pause");
  CheckStrEq(header_test::Method(roots, "game_screen", "check_pause")->kids[1].kids[0].text,
             "native_pause_guard");
  roots = before;
  roots[1].kids.pop_back(); // Unknown guest pause contract must leave the record intact.
  const auto incompatible = rb_blitz::ui::dtb::Compile(roots);
  CHECK_FALSE(AddIOSAutoPause(&roots, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::Compile(roots) == incompatible);
}

void TestPortraitSplash() {
  namespace dtb = rb_blitz::ui::dtb;
  using dtb::Node;
  using namespace header_test;
  BeginCase("portrait calibration instructions fit their measured width and reserved height");
  for (float aspect : {.3f, 402.f / 874, .75f, .9f}) {
    const float available = rb_blitz::ui::PortraitSplash(aspect).width * .84f;
    for (const auto& [width, height] : {std::pair{100.f, 20.f}, {200.f, 85.f}, {600.f, 150.f}}) {
      const float scale = rb_blitz::input::CalibrationTextFit(width, height, available,
          rb_blitz::ui::kPortraitCalibrationInstructionsHeight);
      CHECK_TRUE(scale > 0 && scale <= 1);
      CHECK_TRUE(width * scale <= available + .001f);
      CHECK_TRUE(height * scale <= rb_blitz::ui::kPortraitCalibrationInstructionsHeight + .001f);
    }
  }
  BeginCase("portrait Start, loading and main menu share a logo pose while all six rows remain clear");
  for (float aspect : {.3f, .35f, 402.f / 874, 9.f / 16, .625f, .75f, .9f}) {
    const auto l = rb_blitz::ui::PortraitSplash(aspect);
    CHECK_EQ(l.logo_x, 0);
    CHECK_TRUE(l.logo_scale > .85f * std::min(l.width * .755f / 499.4504f, .45f));
    CHECK_TRUE(l.start_scale * 256 < l.width * .65f);
    CHECK_TRUE(l.city_z - 131.94f * l.city_scale < -240);
    CHECK_TRUE(l.city_z + 548.6727f * l.city_scale < -70);
    CHECK_TRUE(499.4504f * l.logo_scale <= l.width * .90f);
    CHECK_TRUE(l.logo_z + rb_blitz::ui::kPortraitLogoTop * l.logo_scale < 240);
    const float menu_logo_bottom = l.logo_z + rb_blitz::ui::kPortraitLogoBottom * l.logo_scale;
    const float first_row_top = l.list_z + 26.375f * l.menu_list_scale;
    CHECK_TRUE(menu_logo_bottom >= first_row_top + 7.99f);
    CHECK_TRUE(l.menu_list_scale > 0 && l.menu_list_scale <= l.list_scale);
    CHECK_TRUE(l.list_z - (5 * rb_blitz::ui::kPortraitMenuRowStep + 26.375f) * l.menu_list_scale > -165);
    CHECK_TRUE((rb_blitz::ui::kPortraitMenuRowStep - 52.75f) * l.menu_list_scale > 2.5f);
    const auto loading = rb_blitz::ui::PortraitLoading(aspect);
    CHECK_EQ(loading.logo_scale, l.logo_scale);
    CHECK_EQ(loading.logo_z, l.title_logo_z);
    CHECK_EQ(loading.logo_z, l.logo_z);
    CHECK_TRUE(std::abs(loading.text_bottom_z + 150) < .001f);
    CHECK_TRUE(loading.bar_width <= l.width * .5f);
    CHECK_TRUE(std::abs(loading.bar_width / loading.bar_height - 8) < .001f);
    CHECK_TRUE(loading.bar_height > 0);
    CHECK_TRUE(loading.bar_gap > 0 && loading.bar_gap < 10);
    CHECK_TRUE(loading.text_bottom_z > loading.percent_z);
    CHECK_TRUE(loading.text_size > 0 && loading.text_size <= 18.001f);
    CHECK_TRUE(loading.text_size / l.list_scale > 32 && loading.text_size / l.list_scale < 33);
  }
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("splash_screen", "splash_panel"));
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("autosave_warning_screen", "autosave_warning_panel"));
  CHECK_FALSE(rb_blitz::ui::PortraitSplashScreen("autosave_warning_screen", "splash_panel"));
  CHECK_FALSE(rb_blitz::ui::PortraitSplashScreen("splash_screen", "upsell_panel"));
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("confirm_exit_game_screen", "pause_exit_game_panel"));
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("settings_restart_screen", "settings_restart_panel",
                                              "settings_confirmation"));
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("settings_restart_pause_screen", "settings_restart_pause_panel",
                                              "confirmation_message"));
  CHECK_FALSE(rb_blitz::ui::PortraitSplashScreen("options_screen", "options_panel"));
  for (const char* panel : {"cal_welcome_panel", "cal_audio_panel", "cal_visuals_panel",
                           "cal_hw_audio_panel", "cal_hw_visuals_panel", "cal_manual_audio_panel",
                           "cal_manual_video_panel", "cal_quick_panel", "cal_complete_panel",
                           "cal_failed_panel", "controller_config_panel"})
    CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("", panel));
  for (const char* panel : {"settings_panel", "settings_av_panel", "settings_controls_pause_panel",
                            "settings_scale_panel", "settings_scale_pause_panel"}) {
    const auto scene = rb_blitz::ui::screen::PanelScene(panel);
    CHECK_TRUE(rb_blitz::ui::PortraitSettingsScene(scene));
    CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("settings_screen", panel, scene));
  }
  for (const char* panel : {"av_options_panel", "credits_panel"})
    CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("", panel));
  for (float aspect : {402.f / 874, 9.f / 16, .75f}) {
    const auto layout = rb_blitz::ui::PortraitSettings(aspect);
    const auto confirm = rb_blitz::ui::PortraitCalibrationFooter(aspect, 0);
    const auto back = rb_blitz::ui::PortraitCalibrationFooter(aspect, 1);
    const auto retry = rb_blitz::ui::PortraitCalibrationFooter(aspect, 2);
    CHECK_TRUE(confirm.x + confirm.width / 2 < back.x - back.width / 2);
    CHECK_TRUE(retry.z - retry.height / 2 > back.z + back.height / 2);
    const auto live_retry = rb_blitz::ui::PortraitFooterAction(aspect, 2, 3);
    const auto live_back = rb_blitz::ui::PortraitFooterAction(aspect, 0, 2);
    CHECK_EQ(live_retry.x, 0.f);
    CHECK_TRUE(live_retry.z - live_retry.height / 2 > live_back.z + live_back.height / 2);
    CHECK_TRUE(std::string_view(rb_blitz::ui::PortraitFooterSurface(2, 3)) == "portrait_retry");
    CHECK_TRUE(std::string_view(rb_blitz::ui::PortraitFooterSurface(1, 3)) == "portrait_back");
    CHECK_TRUE(confirm.height >= 30 && retry.height >= 30);
    CHECK_EQ(layout.content_scale, rb_blitz::ui::PortraitSplash(aspect).list_scale);
    CHECK_TRUE(layout.back_width < 364.5f * layout.content_scale * .6f);
    CHECK_TRUE(300 * layout.content_scale <= rb_blitz::ui::PortraitSplash(aspect).width * .93f);
    CHECK_TRUE(layout.back_width <= rb_blitz::ui::PortraitSplash(aspect).width * .93f);
    CHECK_TRUE(layout.back_top < 240 - 480 * .865f); // below the caption strip
    CHECK_TRUE(layout.back_top - layout.back_bottom >= 30);
    for (size_t count = 1; count <= 3; ++count) {
      for (size_t i = 0; i < count; ++i) {
        const auto slot = rb_blitz::ui::PortraitFooterAction(aspect, i, count);
        CHECK_TRUE(std::abs(slot.x) + slot.width / 2 < rb_blitz::ui::PortraitSplash(aspect).width / 2);
        CHECK_TRUE(slot.z - slot.height / 2 >= -240);
        if (count == 1) { CHECK_EQ(slot.x, 0.f); CHECK_EQ(slot.width, layout.back_width); }
        if (count == 2) CHECK_EQ(slot.width, rb_blitz::ui::PortraitLoading(aspect).button_width);
      }
    }
    CHECK_TRUE(layout.back_bottom > -240);
  }
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("server_connect_screen", "server_connect_panel"));
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("loading_screen", "loading_panel"));
  CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("server_connect_screen", "error_popup_panel"));
  for (const char* panel : {"rbbz_download_panel", "rbbz_search_loading_panel", "rbbz_library_panel"})
    CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("store_info_screen", panel));
  for (const char* panel : {"settings_restart_panel", "settings_restore_recommended_panel",
                            "settings_restore_vanilla_panel", "settings_restore_recommended_pause_panel",
                            "settings_restore_vanilla_pause_panel", "settings_restart_pause_panel",
                            "messages_update_panel"}) {
    const auto scene = rb_blitz::ui::screen::PanelScene(panel);
    CHECK_TRUE(rb_blitz::ui::PortraitPromptScene(scene));
    CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("settings_prompt_screen", panel, scene));
  }
  CHECK_FALSE(rb_blitz::ui::PortraitPromptScene(rb_blitz::ui::screen::PanelScene("settings_av_panel")));
  CHECK_FALSE(rb_blitz::ui::PortraitPromptScene(rb_blitz::ui::screen::PanelScene("settings_restore_defaults_panel")));
  CHECK_TRUE(rb_blitz::ui::PortraitSettingsScene(rb_blitz::ui::screen::PanelScene("settings_restore_defaults_panel")));
  for (float aspect : {402.f / 874, 9.f / 16, .75f}) {
    CHECK_TRUE(rb_blitz::ui::PortraitPromptScale(aspect) * 350 <= rb_blitz::ui::PortraitSplash(aspect).width * .85f);
    CHECK_TRUE(rb_blitz::ui::PortraitPromptScale(aspect, true) * 640 <= rb_blitz::ui::PortraitSplash(aspect).width * .85f);
  }

  BeginCase("portrait Start and main menu preserve logo pose across state changes without repeated writes");
  std::vector<Node> roots{C({S("new"), S("UIPanel"), S("splash_panel"),
      A({S("enter"), C({S("native_enter")})}),
      A({S("poll"), C({S("native_poll")})}),
      A({S("BUTTON_DOWN_MSG"), C({S("native_buttons")})})})};
  const auto before = roots;
  std::string why;
  CHECK_TRUE(rb_blitz::settings::AddPortraitSplashLayout(&roots, 402.f / 874, &why));
  CHECK_TRUE(dtb::SerializeNodes(Method(roots, "splash_panel", "BUTTON_DOWN_MSG")->kids) ==
             dtb::SerializeNodes(Method(before, "splash_panel", "BUTTON_DOWN_MSG")->kids));
  struct Machine {
    const std::vector<Node>& roots;
    std::string state = "start";
    Node cached = Node::Int(uint32_t(-1));
    std::map<std::string, std::array<float, 3>> scales, positions;
    int writes = 0, native_enters = 0, native_polls = 0;
    Node Method(const std::string& name) {
      const auto* method = header_test::Method(roots, "splash_panel", name.c_str());
      CHECK_TRUE(method != nullptr);
      if (method) for (size_t i = 1; i < method->kids.size(); ++i) Eval(method->kids[i]);
      return Node::Int(0);
    }
    Node Eval(const Node& n) {
      if (n.tag == dtb::kProperty) return cached;
      if (n.tag != dtb::kCommand || n.kids.empty()) return n;
      const auto& op = n.kids[0].text;
      if (op == "==") {
        const auto a = Eval(n.kids[1]), b = Eval(n.kids[2]);
        return Node::Int(a.tag == b.tag && a.scalar == b.scalar && a.text == b.text);
      }
      if (op == "unless") { if (!Eval(n.kids[1]).scalar) Eval(n.kids[2]); return Node::Int(0); }
      if (op == "if_else") return Eval(n.kids[Eval(n.kids[1]).scalar ? 2 : 3]);
      if (op == "do") { for (size_t i = 1; i < n.kids.size(); ++i) Eval(n.kids[i]); return Node::Int(0); }
      if (op == "native_enter") ++native_enters;
      if (op == "native_poll") ++native_polls;
      if (n.kids.size() >= 2) {
        const auto& action = n.kids[1].text;
        if (action == "get") return Node::Symbol(state);
        if (action == "set") { cached = Eval(n.kids[3]); return cached; }
        if (action == "rbbz_portrait_layout") return Method(action);
        if (action == "set_local_scale" || action == "set_local_pos") {
          auto& vector = action == "set_local_scale" ? scales[op] : positions[op];
          for (int i = 0; i < 3; ++i) vector[i] = std::bit_cast<float>(n.kids[i + 2].scalar);
          ++writes;
        }
      }
      return Node::Int(0);
    }
  } machine{roots};
  machine.Method("enter");
  const auto title_scale = machine.scales.at("anim_logo.grp");
  const auto title_position = machine.positions.at("anim_logo.grp");
  const int title_writes = machine.writes;
  machine.Method("poll");
  CHECK_EQ(machine.writes, title_writes);
  machine.state = "main_menu";
  machine.Method("poll");
  CHECK_TRUE(machine.scales.at("anim_logo.grp") == title_scale);
  CHECK_TRUE(machine.positions.at("anim_logo.grp") == title_position);
  const auto menu_positions = machine.positions;
  const int menu_writes = machine.writes;
  for (int i = 0; i < 30; ++i) machine.Method("poll");
  CHECK_EQ(machine.writes, menu_writes);
  CHECK_TRUE(machine.positions == menu_positions);
  machine.state = "waiting";
  machine.Method("poll");
  CHECK_TRUE(machine.scales.at("anim_logo.grp") == title_scale);
  CHECK_TRUE(machine.positions.at("anim_logo.grp") == title_position);
  machine.state = "start";
  machine.Method("poll");
  CHECK_TRUE(machine.scales.at("anim_logo.grp") == title_scale);
  CHECK_EQ(machine.native_enters, 1);
  CHECK_EQ(machine.native_polls, 34);
  const auto bytes = dtb::Compile(roots);
  std::vector<Node> roundtrip;
  dtb::RootHeader header;
  CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &roundtrip, &header, &why));
  CHECK_FALSE(rb_blitz::settings::AddPortraitSplashLayout(&roots, .5f, &why));
  CHECK_TRUE(dtb::Compile(roots) == bytes);
  roots = before;
  roots[0].kids.pop_back();
  const auto unknown = dtb::Compile(roots);
  CHECK_FALSE(rb_blitz::settings::AddPortraitSplashLayout(&roots, .5f, &why));
  CHECK_TRUE(dtb::Compile(roots) == unknown);

  BeginCase("portrait autosave centers the combined icon and measured localized text");
  std::vector<Node> autosave{C({S("new"), S("UIPanel"), S("autosave_warning_panel"),
      A({S("enter"), C({S("native_enter")})}),
      A({S("poll"), C({S("native_poll")})})})};
  const auto autosave_before = autosave;
  CHECK_TRUE(rb_blitz::settings::AddPortraitAutosaveLayout(&autosave, &why));
  CHECK_TRUE(dtb::SerializeNodes(Method(autosave, "autosave_warning_panel", "poll")->kids) ==
             dtb::SerializeNodes(Method(autosave_before, "autosave_warning_panel", "poll")->kids));
  for (float text_height : {40.f, 95.5f, 150.f, 240.f}) {
    bool measured = false;
    int native_enters = 0;
    std::map<std::string, float> z;
    std::map<std::string, std::array<float, 3>> scales;
    const auto eval = [&](auto&& self, const Node& n) -> float {
      if (n.tag == dtb::kInt) return float(int32_t(n.scalar));
      if (n.tag == dtb::kFloat) return std::bit_cast<float>(n.scalar);
      if (n.tag != dtb::kCommand || n.kids.empty()) return 0;
      const auto& op = n.kids[0].text;
      if (op == "native_enter") { ++native_enters; return 0; }
      if (op == "+") return self(self, n.kids[1]) + self(self, n.kids[2]);
      if (op == "-") return self(self, n.kids[1]) - self(self, n.kids[2]);
      if (op == "/") return self(self, n.kids[1]) / self(self, n.kids[2]);
      const auto& action = n.kids[1].text;
      if (action == "set" && n.kids[2].text == "width") {
        CHECK_FALSE(measured);
        CHECK_TRUE(self(self, n.kids[3]) < 480 * 9.f / 16);
      }
      if (action == "set_height_from_text") { measured = true; return 0; }
      if (action == "get") { CHECK_TRUE(measured); return text_height; }
      if (action == "set_local_pos") z[op] = self(self, n.kids[4]);
      if (action == "set_local_scale")
        for (size_t axis = 0; axis < 3; ++axis)
          scales[op][axis] = self(self, n.kids[axis + 2]);
      return 0;
    };
    const auto* enter = Method(autosave, "autosave_warning_panel", "enter");
    for (size_t i = 1; i < enter->kids.size(); ++i) eval(eval, enter->kids[i]);
    CHECK_EQ(native_enters, 1);
    const auto& icon_scale = scales.at("wedges.grp");
    CHECK_TRUE(icon_scale[0] > 0 && icon_scale[0] < 1);
    CHECK_TRUE(icon_scale[0] == icon_scale[1] && icon_scale[1] == icon_scale[2]);
    CHECK_TRUE(icon_scale == scales.at("spinner.trans"));
    const float radius = 28 * icon_scale[0];
    const float top = z.at("wedges.grp") + radius;
    const float bottom = z.at("autosave.lbl") - text_height;
    CHECK_TRUE(std::abs(top + bottom) < .001f);
    CHECK_TRUE(top < 240 && bottom > -240);
    CHECK_TRUE(std::abs(z.at("wedges.grp") - radius - z.at("autosave.lbl") - 27) < .001f);
    CHECK_TRUE(z.at("wedges.grp") == z.at("spinner.trans"));
  }

  BeginCase("portrait store layout refills widgets after changing the visible row count");
  for (const bool info : {false, true}) {
    const auto method = [](const char* name) {
      return A({S(name), C({S("native_bind")})});
    };
    std::vector<Node> store = info ? std::vector<Node>{
        C({S("new"), S("UIPanel"), S("store_info_panel"), method("refresh_panel"),
           A({S("refresh_buttons"), A({Node::Var("init")}),
              C({S("do"), A({Node::Var("buttons"), A({})}),
                 C({S("resize"), Node::Var("buttons"), Node::Int(0)}),
                 C({S("insert_elem"), Node::Var("buttons"), Node::Int(0), S("exit")}),
                 C({S("buttons.lst"), S("set_data"), Node::Var("buttons")})})})})}
      : std::vector<Node>{
        C({S("new"), S("StoreMenuPanel"), S("store_menu_panel"),
            method("enter"), method("new_provider"), method("exit")}),
        C({S("new"), S("HeldButtonPanel"), S("store_browser_panel"),
            method("enter"), method("refresh_list"), method("exit")}),
        C({S("new"), S("HeldButtonPanel"), S("store_details_panel"), method("refresh")})};
    CHECK_TRUE(rb_blitz::settings::AddPortraitStoreLayout(&store, .5f, info, &why));
    if (info) {
      const auto* refresh = Method(store, "store_info_panel", "refresh_buttons");
      CHECK_TRUE(refresh != nullptr);
      if (refresh) {
        const auto& block = refresh->kids[2];
        CHECK_EQ(block.kids.size(), 4u);
        CHECK_EQ(block.kids[2].tag, dtb::kCommand);
        CHECK_TRUE(block.kids[2].kids[0].text == "resize");
        CHECK_EQ(block.kids[3].tag, dtb::kCommand);
        CHECK_TRUE(block.kids[3].kids[1].text == "set_data");
      }
    }
    for (const auto& [panel, callback] : {
        std::pair{"store_menu_panel", "enter"}, {"store_menu_panel", "new_provider"},
        {"store_browser_panel", "refresh_list"}, {"store_info_panel", "refresh_panel"}}) {
      if (info != (std::string_view(panel) == "store_info_panel")) continue;
      bool populated = true;
      int native_binds = 0, rebuilt = 0;
      const auto run = [&](const Node& command) {
        const auto& kids = command.kids;
        if (command.tag != dtb::kCommand || kids.empty()) return;
        if (kids[0].text == "native_bind") { populated = true; ++native_binds; }
        // UIList::SetNumDisplay recreates empty label elements. Its provider
        // remains attached, but only a subsequent refresh fills those elements.
        if (kids.size() == 4 && kids[1].text == "set" && kids[2].text == "display_num") {
          populated = false; ++rebuilt;
        }
        if (kids.size() == 2 && kids[1].text == "refresh") populated = true;
      };
      const auto* edited = Method(store, panel, callback);
      CHECK_TRUE(edited != nullptr);
      if (edited) for (const auto& command : edited->kids) run(command);
      CHECK_EQ(native_binds, 1);
      CHECK_EQ(rebuilt, 1);
      CHECK_TRUE(populated);
    }
  }

  BeginCase("portrait message feed reserves separate icon, subject, reading and footer areas");
  for (float aspect : {.3f, 402.f / 874, .75f, .9f}) {
    const auto layout = rb_blitz::ui::PortraitMessages(aspect);
    CHECK_TRUE(layout.width <= rb_blitz::ui::PortraitSplash(aspect).width);
    CHECK_TRUE(layout.list_z + layout.row_height / 2 < layout.tabs_z - 20);
    CHECK_TRUE(layout.list_z - (layout.rows - 1) * layout.row_step - layout.row_height / 2 > layout.detail_top);
    CHECK_TRUE(layout.list_z - layout.row_height / 2 - layout.detail_top <= 10);
    CHECK_TRUE(layout.icon_inset - layout.icon_size / 2 > 0);
    CHECK_TRUE(layout.icon_inset + layout.icon_size / 2 < layout.text_inset);
    CHECK_TRUE(layout.width - layout.text_inset - 16 > 60);
    CHECK_TRUE(layout.subject_top < layout.detail_top);
    CHECK_TRUE(layout.subject_top - layout.subject_height > layout.body_top);
    CHECK_TRUE(layout.body_top - layout.body_height > layout.detail_bottom);
    CHECK_TRUE(layout.detail_bottom > rb_blitz::ui::PortraitSongsFooter(aspect, 0, 2).z + 15);
  }
  BeginCase("portrait Messages keeps Back in the footer and measures text after provider callbacks");
  {
    std::vector<Node> inbox{C({S("new"), S("InboxPanel"), S("inbox_panel"),
        A({S("enter"), C({S("native_enter")})}),
        A({S("on_messages_updated"), C({S("native_update")})}),
        A({S("SCROLL_MSG"), C({S("native_scroll")})}),
        A({S("exit"), C({S("native_exit")})}),
        A({S("update_inline_help"), C({S("native_actions")})}),
        A({S("BUTTON_DOWN_MSG"), C({S("native_buttons")})})})};
    const auto before = inbox;
    CHECK_TRUE(rb_blitz::settings::AddPortraitMessagesLayout(&inbox, .5f, &why));
    for (const char* action : {"BUTTON_DOWN_MSG"})
      CHECK_TRUE(dtb::SerializeNodes(Method(inbox, "inbox_panel", action)->kids) ==
                 dtb::SerializeNodes(Method(before, "inbox_panel", action)->kids));
    const auto* help = Method(inbox, "inbox_panel", "update_inline_help");
    CHECK_TRUE(help && Has(*help, "native_actions"));
    for (const char* action : {"kAction_Confirm", "kAction_Start", "kAction_Option", "kAction_ShellOption"})
      CHECK_TRUE(help && Has(*help, action));
    CHECK_TRUE(help && !Has(*help, "kAction_Cancel"));
    for (const char* callback : {"enter", "on_messages_updated", "SCROLL_MSG"}) {
      bool native_ran = false, subject_measured = false, body_measured = false, filled = true;
      int resized = 0;
      for (const auto& command : Method(inbox, "inbox_panel", callback)->kids) {
        if (command.tag != dtb::kCommand || command.kids.empty()) continue;
        const auto& kids = command.kids;
        if (kids[0].text.starts_with("native_")) { native_ran = true; filled = true; }
        if (kids.size() == 4 && kids[1].text == "set" && kids[2].text == "display_num") {
          CHECK_FALSE(native_ran); filled = false; ++resized;
        }
        if (kids.size() == 2 && kids[1].text == "refresh") filled = true;
        if (kids.size() == 2 && kids[1].text == "set_height_from_text") {
          CHECK_TRUE(native_ran);
          subject_measured |= kids[0].text == "subject.lbl";
          body_measured |= kids[0].text == "message.lbl";
        }
        CHECK_FALSE(kids.size() > 1 && kids[0].text == "songs.lst" && kids[1].text == "set_selected");
      }
      CHECK_EQ(resized, std::string_view(callback) == "enter" ? 1 : 0);
      CHECK_TRUE(native_ran && subject_measured && body_measured && filled);
    }
    auto incompatible = before;
    incompatible[0].kids.erase(incompatible[0].kids.begin() + 5);
    const auto original = dtb::Compile(incompatible);
    CHECK_FALSE(rb_blitz::settings::AddPortraitMessagesLayout(&incompatible, .5f, &why));
    CHECK_TRUE(dtb::Compile(incompatible) == original);
  }

  BeginCase("portrait recommendation artwork, navigation, text and two footer actions stay separated");
  for (float aspect : {.3f, 402.f / 874, .75f, .9f}) {
    const auto layout = rb_blitz::ui::PortraitRecommendations(aspect);
    CHECK_TRUE(layout.carousel_z + layout.art_size / 2 < layout.tabs_z - 20);
    CHECK_TRUE(layout.NavigationX() - layout.navigation_size / 2 > layout.art_size / 2);
    CHECK_TRUE(layout.TypeZ() + 6 < layout.carousel_z - layout.art_size / 2);
    CHECK_TRUE(layout.TypeZ() - 8 > layout.title_top);
    CHECK_TRUE(layout.title_top - layout.title_height > layout.artist_top);
    CHECK_TRUE(layout.artist_top - layout.artist_height > layout.info_top);
    CHECK_TRUE(layout.info_top - layout.info_height > layout.reason_top);
    CHECK_TRUE(layout.reason_top - layout.reason_height > layout.leaderboard_title_top);
    for (size_t count = 1; count <= 2; ++count) {
      for (size_t i = 0; i < count; ++i) {
        const auto a = rb_blitz::ui::PortraitRecommendationsFooter(aspect, i, count);
        CHECK_TRUE(a.width > 32 && a.height >= 25);
        CHECK_TRUE(std::abs(a.x) + a.width / 2 <= layout.width / 2 + .001f);
        CHECK_TRUE(a.z - a.height / 2 > -240);
        CHECK_TRUE(a.z + a.height / 2 < layout.leaderboard_z - layout.leaderboard_step - 7);
        for (size_t j = 0; j < i; ++j) {
          const auto b = rb_blitz::ui::PortraitRecommendationsFooter(aspect, j, count);
          CHECK_TRUE(std::abs(a.x - b.x) >= (a.width + b.width) / 2 ||
                     std::abs(a.z - b.z) >= (a.height + b.height) / 2);
          CHECK_TRUE(std::string_view(rb_blitz::ui::PortraitRecommendationFooterSurface(i)) !=
                     rb_blitz::ui::PortraitRecommendationFooterSurface(j));
        }
      }
    }
  }
  BeginCase("portrait recommendation state follows the native provider without replacing actions or selection");
  {
    const auto method = [](const char* name) { return A({S(name), C({S("native_update")})}); };
    std::vector<Node> roots{C({S("new"), S("RecommendationsPanel"), S("recommendation_panel"),
        method("enter"), method("exit"), method("update_recommendation_info"),
        method("update_lb_ui_for_focus_rec"), method("on_mini_leaderboards_updated"),
        method("on_leaderboard_failed"), method("SELECT_MSG"), method("BUTTON_DOWN_MSG"), method("SCROLL_MSG")})};
    const auto before = roots;
    CHECK_TRUE(rb_blitz::settings::AddPortraitRecommendationsLayout(&roots, .5f, &why));
    for (const char* action : {"SELECT_MSG", "BUTTON_DOWN_MSG", "SCROLL_MSG"})
      CHECK_TRUE(dtb::SerializeNodes(Method(roots, "recommendation_panel", action)->kids) ==
                 dtb::SerializeNodes(Method(before, "recommendation_panel", action)->kids));
    for (const auto& [count, selected] : {std::pair{0, 0}, {1, 0}, {5, 0}, {5, 2}, {5, 4}})
      for (int board_state = 1; board_state <= 5; ++board_state) {
      std::map<std::string, bool> showing;
      int native_updates = 0, measured = 0, cleared = 0;
      std::array<int, 2> position{};
      const auto eval = [&](auto&& self, const Node& n) -> float {
        if (n.tag == dtb::kInt) return float(int32_t(n.scalar));
        if (n.text == "TRUE") return 1;
        if (n.text == "hidden") return 1;
        if (n.text == "loading") return 2;
        if (n.tag != dtb::kCommand || n.kids.empty()) return 0;
        const auto& k = n.kids; const auto& op = k[0].text;
        if (op == "native_update") { ++native_updates; return 0; }
        if (op == "!") return !self(self, k[1]);
        if (op == "&&") return std::all_of(k.begin() + 1, k.end(), [&](const auto& v) { return self(self, v) != 0; });
        if (op == "!=") return self(self, k[1]) != self(self, k[2]);
        if (op == ">") return self(self, k[1]) > self(self, k[2]);
        if (op == "<") return self(self, k[1]) < self(self, k[2]);
        if (op == "+") return self(self, k[1]) + self(self, k[2]);
        if (op == "-") return self(self, k[1]) - self(self, k[2]);
        if (op == "unless") {
          if (!self(self, k[1])) for (size_t i = 2; i < k.size(); ++i) self(self, k[i]);
          return 0;
        }
        if (op == "if") { if (self(self, k[1])) self(self, k[2]); return 0; }
        if (op == "leaderboard_mode.ep") return float(board_state);
        if (k[1].text == "num_data") return float(count);
        if (k[1].text == "selected_pos") return float(selected);
        if (k[1].text == "set_showing") showing[op] = self(self, k[2]);
        if (k[1].text == "set_token_fmt") position = {int(self(self, k[3])), int(self(self, k[4]))};
        if (k[1].text == "set_height_from_text") { CHECK_EQ(native_updates, 1); ++measured; }
        if (k[1].text == "clear_action_token") ++cleared;
        CHECK_FALSE(k[1].text == "set_selected" || (k[1].text == "set" && k[2].text == "display_num"));
        return 0;
      };
      const auto* update = Method(roots, "recommendation_panel", "update_recommendation_info");
      for (const auto& cmd : update->kids) eval(eval, cmd);
      CHECK_EQ(native_updates, 1); CHECK_EQ(measured, 4);
      CHECK_EQ(cleared, count == 0 ? 3 : 0);
      CHECK_EQ(showing.at("portrait_rec_empty.lbl"), count == 0);
      CHECK_EQ(showing.at("recommendations.lst"), count != 0);
      CHECK_FALSE(showing.at("rec_icon"));
      CHECK_EQ(showing.at("portrait_rec_board_state.grp"), count != 0 && board_state > 2);
      CHECK_EQ(showing.at("portrait_rec_previous.lbl"), count > 0 && selected > 0);
      CHECK_EQ(showing.at("portrait_rec_next.lbl"), count > 0 && selected < count - 1);
      CHECK_EQ(position[0], selected + 1); CHECK_EQ(position[1], count);
      for (const char* callback : {"update_lb_ui_for_focus_rec", "on_mini_leaderboards_updated", "on_leaderboard_failed"}) {
        for (const auto& cmd : Method(roots, "recommendation_panel", callback)->kids) eval(eval, cmd);
        CHECK_EQ(showing.at("portrait_rec_board_state.grp"), count != 0 && board_state > 2);
      }
    }
    auto incompatible = before;
    incompatible[0].kids.erase(incompatible[0].kids.begin() + 8);
    const auto unchanged = dtb::Compile(incompatible);
    CHECK_FALSE(rb_blitz::settings::AddPortraitRecommendationsLayout(&incompatible, .5f, &why));
    CHECK_TRUE(dtb::Compile(incompatible) == unchanged);
  }

  BeginCase("portrait connection and song loading preserve native entry work");
  for (const bool song : {false, true}) {
    const char* name = song ? "loading_screen" : "server_connect_panel";
    std::vector<Node> loading{C({S("new"), S(song ? "LoadingScreen" : "ServerConnectPanel"), S(name),
        A({S("enter"), C({S("native_loading")})})})};
    CHECK_TRUE(rb_blitz::settings::AddPortraitLoadingLayout(&loading, song, .5625f, &why));
    const auto* enter = Method(loading, name, "enter");
    CHECK_TRUE(enter && enter->kids[1].kids[0].text == "native_loading");
    CheckStrEq(enter->kids[2].kids[1].text, "set_local_scale");
    const float scale = std::bit_cast<float>(enter->kids[2].kids[2].scalar);
    CHECK_TRUE(scale > 0 && scale <= 1);
    CHECK_EQ(scale == 1, !song);
    CHECK_EQ(enter->kids.size(), song ? size_t(18) : size_t(21));
    if (!song) {
      std::map<std::string, std::array<float, 3>> positions, scales;
      int fit_type = 2; // Retail status.lbl uses ellipsis, which cannot measure its height.
      for (size_t i = 3; i < enter->kids.size(); ++i) {
        const auto& command = enter->kids[i].kids;
        if (command.size() == 4 && command[0].text == "status.lbl" &&
            command[1].text == "set" && command[2].text == "fit_type")
          fit_type = int(command[3].scalar);
        if (command[1].text != "set_local_pos" && command[1].text != "set_local_scale") continue;
        auto& values = command[1].text == "set_local_pos" ? positions[command[0].text] : scales[command[0].text];
        for (size_t axis = 0; axis < 3; ++axis)
          values[axis] = std::bit_cast<float>(command[axis + 2].scalar) * scale;
      }
      const auto& bar = scales.at("progress_bar.grp");
      CHECK_EQ(fit_type, 0);
      CHECK_TRUE(bar[0] * 256 <= 270 * .5f);
      CHECK_TRUE(std::abs(bar[0] - bar[2]) < .001f); // circular caps retain their shape
      CHECK_TRUE(bar[2] * 32 > 14);
      CHECK_TRUE(std::abs(positions.at("status.lbl")[2] - rb_blitz::ui::PortraitLoading(.5625f).text_bottom_z) < .001f);
      CHECK_TRUE(positions.at("status.lbl")[2] > positions.at("content_percent.lbl")[2]);
      for (float height : {26.f, 52.f, 78.f, 130.f}) {
        bool measured = false;
        float measured_height = 180; // Initial box height, retained by non-wrap native labels.
        float bar_z = 0;
        const auto eval = [&](auto&& self, const Node& n) -> float {
          if (n.tag == dtb::kFloat) return std::bit_cast<float>(n.scalar);
          if (n.tag != dtb::kCommand || n.kids.empty()) return 0;
          const auto& op = n.kids[0].text;
          if (op == "do") { for (size_t i = 1; i < n.kids.size(); ++i) self(self, n.kids[i]); return 0; }
          if (op == "+") return self(self, n.kids[1]) + self(self, n.kids[2]);
          if (op == "/") return self(self, n.kids[1]) / self(self, n.kids[2]);
          if (op == "status.lbl" && n.kids[1].text == "set_height_from_text" && fit_type == 0) {
            measured = true;
            measured_height = height;
          }
          if (op == "status.lbl" && n.kids[1].text == "get") { CHECK_TRUE(measured); return measured_height; }
          if (op == "progress_bar.grp" && n.kids[1].text == "set_local_pos") bar_z = self(self, n.kids[4]) * scale;
          return 0;
        };
        const auto* update = Method(loading, name, "rbbz_portrait_loading");
        CHECK_TRUE(update != nullptr);
        if (update) eval(eval, update->kids[1]);
        CHECK_TRUE(measured);
        const float gap = bar_z - bar[2] * 16 - (positions.at("status.lbl")[2] + height);
        CHECK_TRUE(gap > 0 && gap < 10);
      }
    }
  }
}

void TestMiloFlowBoundaries() {
  using rb_blitz::ui::input_font::detail::MiloObjectBoundaries;
  BeginCase("Milo object boundaries retain inline Flow directories and nested children");
  std::vector<uint8_t> bytes;
  const auto word = [&](uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(value >> shift));
  };
  const auto marker = [&] {
    const auto at = bytes.size(); word(0xADDEADDE); return at;
  };
  const auto flow_directory = [&](const std::string& child_type) {
    word(32); word(4);
    bytes.insert(bytes.end(), {'F', 'l', 'o', 'w'});
    word(1); bytes.push_back('n');
    word(4); word(2); bytes.push_back(0); word(1);
    word(uint32_t(child_type.size()));
    bytes.insert(bytes.end(), child_type.begin(), child_type.end());
    word(1); bytes.push_back('n');
  };
  const auto root = marker();
  word(7); marker(); flow_directory("Flow"); marker();
  word(7); marker(); flow_directory("FlowAnimate"); marker();
  word(3); const auto flow_end = marker();
  word(11); const auto texture_end = marker();
  const auto boundaries = MiloObjectBoundaries(bytes, 0, 2);
  CHECK_TRUE((boundaries == std::vector<size_t>{root, flow_end, texture_end}));
  bytes.resize(flow_end);
  CHECK_TRUE(MiloObjectBoundaries(bytes, 0, 2).empty());
}

void TestPortraitCanvas() {
  BeginCase("detached portrait objects inherit the fitted panel without changing animation parents");
  std::vector<uint8_t> bytes;
  const auto word = [&](uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(value >> shift));
  };
  const auto string = [&](std::string_view value) {
    word(uint32_t(value.size())); bytes.insert(bytes.end(), value.begin(), value.end());
  };
  word(32); string("PanelDir"); string("panel");
  word(0); word(0); bytes.push_back(0); word(3);
  string("UILabel"); string("detached.lbl");
  string("UILabel"); string("animated.lbl");
  string("Trans"); string("Parent.trans");
  word(0xADDEADDE);
  for (const auto [matrix, parent] : {std::pair{size_t(0x19), ""},
                                     {size_t(0x19), "Parent.trans"}, {size_t(0x11), ""}}) {
    bytes.insert(bytes.end(), matrix + 105, 0);
    string(parent); word(0xADDEADDE);
  }
  std::vector<uint8_t> packed(0x810, 0);
  const auto little = [&](size_t at, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) packed[at + i] = uint8_t(value >> (8 * i));
  };
  little(0, 0xCABEDEAF); little(4, 0x810); little(8, 1);
  little(12, uint32_t(bytes.size())); little(16, uint32_t(bytes.size()) | 0x01000000);
  packed.insert(packed.end(), bytes.begin(), bytes.end());
  std::vector<uint8_t> fitted;
  std::string why;
  CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitCanvas(packed, &fitted, &why));
  const auto markers = rb_blitz::ui::input_font::detail::MiloObjectBoundaries(
      std::span<const uint8_t>(fitted).subspan(0x810), 0, 3);
  CHECK_EQ(markers.size(), size_t(4));
  if (markers.size() != 4) return;
  for (size_t i = 0; i < 3; ++i) {
    const auto at = 0x810 + markers[i] + 4 + (i == 2 ? 0x11 : 0x19) + 105;
    const std::string expected = i == 1 ? "Parent.trans" : "panel";
    CHECK_EQ(fitted[at + 3], uint8_t(expected.size()));
    CHECK_TRUE(std::memcmp(fitted.data() + at + 4, expected.data(), expected.size()) == 0);
  }
  std::vector<uint8_t> again;
  CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitCanvas(fitted, &again, &why));
  CHECK_TRUE(again == fitted);
}

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "check-render-scale-source") {
    namespace font = rb_blitz::ui::input_font;
    const auto u32 = [](const uint8_t* p) {
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    const auto read = [&](const char* path) {
      std::ifstream file(std::filesystem::path(argv[2]) / path, std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    const auto source = read("ui/av_options/gen/av_options.milo_xbox");
    const auto buttons = read(font::kListRecord);
    BeginCase("native render scale scene retains Audio buttons and a ten-marker bottom wheel");
    CHECK_FALSE(source.empty()); CHECK_FALSE(buttons.empty());
    if (source.empty() || buttons.empty()) return Finish();
    for (const bool portrait : {false, true}) {
      for (const float aspect : {.3f, 402.f / 874, .75f, .9f}) {
        std::vector<uint8_t> scene;
        std::string why;
        CHECK_TRUE(font::PrepareRenderScaleScene(source, buttons, aspect, portrait, &scene, &why));
        if (scene.size() <= 0x810) { std::printf("scene refused: %s\n", why.c_str()); continue; }
        const auto* bytes = scene.data() + 0x810;
        size_t at = 4;
        const auto string = [&] {
          const auto length = u32(bytes + at); at += 4;
          std::string text(reinterpret_cast<const char*>(bytes + at), length);
          at += length; return text;
        };
        string(); string(); at += 9;
        const auto count = u32(bytes + at); at += 4;
        std::vector<std::string> names;
        for (uint32_t i = 0; i < count; ++i) { string(); names.push_back(string()); }
        const auto markers = font::detail::MiloObjectBoundaries(
            std::span<const uint8_t>(bytes, scene.size() - 0x810), at, count);
        CHECK_EQ(markers.size(), size_t(count) + 1);
        if (markers.size() != size_t(count) + 1) continue;
        std::map<std::string, std::vector<uint8_t>> objects;
        for (size_t i = 0; i < count; ++i)
          objects[names[i]] = {bytes + markers[i] + 4, bytes + markers[i + 1] + 4};
        for (const char* name : {"checkbox.lst", "track_sound_slider.sld", "scale_value.lbl", "nav.ihp"})
          CHECK_TRUE(objects.contains(name));
        if (!objects.contains("track_sound_slider.sld") || !objects.contains("scale_value.lbl")) continue;
        const auto& wheel = objects.at("track_sound_slider.sld");
        constexpr std::string_view resource = "../resource/sliders/slider_options.milo";
        CHECK_TRUE(std::search(wheel.begin(), wheel.end(), resource.begin(), resource.end()) != wheel.end());
        CHECK_TRUE(std::abs(std::bit_cast<float>(u32(wheel.data() + 0x20 + 44)) + 100) < .001f);
        const float scale = std::bit_cast<float>(u32(wheel.data() + 0x20));
        const float width = portrait ? rb_blitz::ui::PortraitSplash(aspect).width : 640.f;
        CHECK_TRUE(scale > 0 && 190 * scale <= width * .85f);
        const auto& label = objects.at("scale_value.lbl");
        CHECK_TRUE(std::abs(std::bit_cast<float>(u32(label.data() + 0x19 + 44)) + 70) < .001f);
        CHECK_EQ(u32(objects.at("nav.ihp").data() + 9), uint32_t(2));
      }
    }
    return Finish();
  }
  if (argc == 4 && std::string(argv[1]) == "check-calibration-source") {
    namespace settings = rb_blitz::settings;
    namespace font = rb_blitz::ui::input_font;
    namespace dtb = rb_blitz::ui::dtb;
    const auto read = [&](const char* path) {
      std::ifstream file(std::filesystem::path(argv[2]) / path, std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    std::string why;
    const auto source = read(font::kCalibrationRecord);
    std::vector<uint8_t> layout, hinted;
    CHECK_TRUE(font::PrepareCalibrationLayout(source, &layout, &why));
    CHECK_TRUE(font::PrepareCalibration(layout, &hinted, &why));
    std::vector<uint8_t> refused;
    CHECK_FALSE(font::PrepareCalibrationLayout(layout, &refused, &why));
    const auto record = read("ui/calibration/gen/calibration.dtb");
    std::vector<dtb::Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(record.data(), record.size(), &roots, &header, &why));
    CHECK_TRUE(settings::FadeCalibrationButtonAfterCountdown(&roots, &why));
    const auto compiled = dtb::Compile(roots, header);
    CHECK_TRUE(settings::FadeCalibrationButtonAfterCountdown(&roots, &why));
    CHECK_TRUE(dtb::Compile(roots, header) == compiled);
    const auto check = [&](auto&& self, const dtb::Node& n) -> size_t {
      size_t found = 0;
      if (n.tag == dtb::kArray && !n.kids.empty() && n.kids[0].text == "script") {
        for (size_t i = 1; i + 1 < n.kids.size(); ++i) {
          const auto& wait = n.kids[i];
          const auto& fade = n.kids[i + 1];
          if (wait.kids.size() != 3 || wait.kids[0].text != "task" ||
              wait.kids[1].text != "wait") continue;
          CHECK_TRUE(fade.kids.size() == 4 && fade.kids[0].text == "if" &&
              fade.kids[1].kids[0].text == "is_video");
          CHECK_TRUE(fade.kids[2].kids[0].text == "button_press_xbox_fade_out.mnm");
          CHECK_TRUE(fade.kids[3].kids[0].text == "button_press_ps3_fade_out.mnm");
          CHECK_TRUE(n.kids[i + 2].kids[0].text == "if_else");
          ++found;
        }
      }
      for (const auto& child : n.kids) found += self(self, child);
      return found;
    };
    size_t handoffs = 0;
    for (const auto& root : roots) handoffs += check(check, root);
    CHECK_EQ(handoffs, size_t{1});
    CHECK_TRUE(settings::PreparePortraitCalibrationFlow(&roots, &why));
    const auto portrait_flow = dtb::Compile(roots, header);
    CHECK_TRUE(settings::PreparePortraitCalibrationFlow(&roots, &why));
    CHECK_TRUE(dtb::Compile(roots, header) == portrait_flow);
    const auto output = std::filesystem::path(argv[3]);
    std::filesystem::create_directories(output);
    for (const auto& [name, bytes] : std::array<std::pair<const char*, const std::vector<uint8_t>*>, 3>{{
        {"cal_auto-layout.milo_xbox", &layout}, {"cal_auto-hints.milo_xbox", &hinted},
        {"calibration.dtb", &compiled}}}) {
      std::ofstream file(output / name, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes->data()), bytes->size());
      CHECK_TRUE(bool(file));
    }
    return Finish();
  }
  if (argc == 6 && std::string(argv[1]) == "check-portrait-menu") {
    namespace dtb = rb_blitz::ui::dtb;
    namespace font = rb_blitz::ui::input_font;
    auto read = [](const char* path) {
      std::ifstream file(path, std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    const auto source = read(argv[2]), list = read(argv[3]), buttons = read(argv[4]);
    std::vector<dtb::Node> roots;
    dtb::RootHeader header;
    std::string why;
    CHECK_TRUE(dtb::Parse(source.data(), source.size(), &roots, &header, &why));
    const auto before = roots;
    CHECK_TRUE(rb_blitz::settings::AddPortraitSplashLayout(&roots, 402.f / 874, &why));
    for (const auto* name : {"BUTTON_DOWN_MSG", "SELECT_MSG", "check_trial", "exit"})
      CHECK_TRUE(dtb::SerializeNodes(header_test::Method(roots, "splash_panel", name)->kids) ==
                 dtb::SerializeNodes(header_test::Method(before, "splash_panel", name)->kids));
    std::vector<uint8_t> scene;
    CHECK_TRUE(font::PreparePortraitSplashList(list, buttons, &scene, &why));
    if (scene.size() <= 0x810 + 4423) return 1;
    const auto u32 = [](const uint8_t* p) {
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    const auto* data = scene.data() + 0x810;
    CHECK_EQ(u32(data + 0x25), uint32_t(31));
    size_t at = 0x29;
    std::vector<std::pair<std::string, std::string>> declarations;
    for (int i = 0; i < 31; ++i) {
      const auto n = u32(data + at); at += 4;
      const std::string type(reinterpret_cast<const char*>(data + at), n); at += n;
      const auto m = u32(data + at); at += 4;
      const std::string name(reinterpret_cast<const char*>(data + at), m); at += m;
      declarations.emplace_back(type, name);
    }
    CHECK_TRUE(declarations[30] == std::make_pair(std::string("UIListMesh"), std::string("portrait_selected")));
    CHECK_TRUE(std::search(scene.begin(), scene.end(), declarations[24].second.begin(),
                           declarations[24].second.end()) != scene.end());
    // Native Mesh slots draw behind labels. The selected surface uses the
    // engine's highlight-only slot, rather than its reshaping underline widget.
    std::vector<size_t> markers;
    for (size_t i = 0x810; i + 4 <= scene.size(); ++i)
      if (u32(scene.data() + i) == 0xADDEADDE) markers.push_back(i);
    CHECK_EQ(markers.size(), size_t(32));
    const size_t widget = markers[markers.size() - 2] + 4;
    CHECK_TRUE(std::bit_cast<float>(u32(scene.data() + widget + 25)) < 1.f);
    CHECK_EQ(u32(scene.data() + widget + 101), uint32_t(1));
    CHECK_EQ(u32(scene.data() + widget + 105), uint32_t(std::string("portrait_selected.mesh").size()));
    CHECK_TRUE(std::none_of(declarations.begin(), declarations.end(), [](const auto& d) {
      return d.first == "UIListHighlight";
    }));
    std::vector<uint8_t> rejected;
    CHECK_FALSE(font::PreparePortraitSplashList(scene, buttons, &rejected, &why));
    const auto output = std::filesystem::path(argv[5]);
    std::filesystem::create_directories(output);
    const auto record = dtb::Compile(roots, header);
    std::ofstream splash_file(output / "splash.dtb", std::ios::binary);
    splash_file.write(reinterpret_cast<const char*>(record.data()), record.size());
    std::ofstream list_file(output / "list_splash.milo_xbox", std::ios::binary);
    list_file.write(reinterpret_cast<const char*>(scene.data()), scene.size());
    TestPortraitSplash();
    return Finish();
  }
  if (argc == 4 && std::string(argv[1]) == "check-touch-hud") {
    namespace font = rb_blitz::ui::input_font;
    namespace dtb = rb_blitz::ui::dtb;
    auto read = [](const char* path) {
      std::ifstream file(path, std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    const auto source = read(argv[2]);
    const auto before = source;
    std::vector<uint8_t> scene;
    std::string why;
    CHECK_TRUE(font::PrepareTouchHud(source, &scene, &why));
    CHECK_TRUE(source == before);
    if (scene.size() <= 0x810) return Finish();
    const auto* data = scene.data() + 0x810;
    auto u32 = [](const uint8_t* p) {
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    CHECK_EQ(u32(data + 0x25), uint32_t(121));
    size_t at = 0x29;
    std::vector<std::string> names;
    for (unsigned i = 0; i < 121; ++i) {
      const auto type_size = u32(data + at); at += 4 + type_size;
      const auto name_size = u32(data + at); at += 4;
      names.emplace_back(reinterpret_cast<const char*>(data + at), name_size);
      at += name_size;
    }
    std::vector<std::string> added{"touch_pause_bg.mat", "touch_pause_ink.mat",
        "touch_pause.grp", "touch_pause_icon.grp", "touch_pause_bg.mesh",
        "touch_pause_l.mesh", "touch_pause_r.mesh", "touch_pause.anim", "portrait_top_hud.grp"};
    CHECK_TRUE(std::find(names.begin(), names.end(), "touch_powerup.grp") == names.end());
    for (size_t i = 0; i < added.size(); ++i) CheckStrEq(names[112 + i], added[i]);
    std::vector<size_t> markers;
    for (size_t i = 0x810; i + 4 <= scene.size(); ++i)
      if (u32(scene.data() + i) == 0xADDEADDE) markers.push_back(i);
    CHECK_TRUE(markers.size() >= 10);
    if (markers.size() < 10) return Finish();
    auto object = [&](size_t i) {
      const size_t start = markers[markers.size() - 10 + i] + 4;
      const size_t end = markers[markers.size() - 9 + i] + 4;
      return std::vector<uint8_t>(scene.begin() + start, scene.begin() + end);
    };
    auto contains = [](const std::vector<uint8_t>& bytes, std::string_view text) {
      return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
    };
    CHECK_TRUE(contains(object(0), "help_bg_powerup.tex"));
    const auto ink = object(1);
    CHECK_EQ(u32(ink.data() + 0x19), std::bit_cast<uint32_t>(.92f));
    CHECK_EQ(u32(ink.data() + 0x1D), std::bit_cast<uint32_t>(.96f));
    CHECK_EQ(u32(ink.data() + 0x21), std::bit_cast<uint32_t>(1.f));
    CHECK_EQ(u32(ink.data() + 0x25), uint32_t(0x3F800000));
    CHECK_FALSE(contains(ink, ".tex"));
    CHECK_EQ(object(2)[0x9A], uint8_t(1));
    CHECK_TRUE(contains(object(2), "touch_pause_icon.grp"));
    CHECK_TRUE(contains(object(3), "touch_pause.grp"));
    CHECK_TRUE(contains(object(4), "touch_pause_bg.mat"));
    for (size_t i : {5, 6}) {
      CHECK_TRUE(contains(object(i), "touch_pause_ink.mat"));
      CHECK_TRUE(contains(object(i), "touch_pause_icon.grp"));
    }
    CHECK_TRUE(contains(object(7), "touch_pause_icon.grp"));
    CHECK_FALSE(contains(object(7), "remedial_help.lbl"));
    CHECK_TRUE(contains(object(8), "blitz_meter.grp"));
    CHECK_TRUE(contains(object(8), "blitz_text_flat.mesh"));
    std::vector<uint8_t> hints;
    CHECK_TRUE(font::PrepareHudHints(scene, &hints, &why));
    CHECK_FALSE(font::PrepareTouchHud(scene, &hints, &why)); // never double-apply offsets

    const auto game = read(argv[3]);
    std::vector<dtb::Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(game.data(), game.size(), &roots, &header, &why));
    const auto* original_enter = header_test::Method(roots, "game_screen", "enter");
    const size_t statements = original_enter ? original_enter->kids.size() : 0;
    CHECK_TRUE(rb_blitz::settings::AddTouchHudAnimation(&roots, &why));
    const auto* enter = header_test::Method(roots, "game_screen", "enter");
    CHECK_TRUE(enter && enter->kids.size() == statements + 1);
    if (enter && !enter->kids.empty()) {
      const auto& animate = enter->kids[statements];
      CheckStrEq(animate.kids[0].kids[2].text, "touch_pause.anim");
      CheckStrEq(animate.kids[1].text, "animate");
      CheckStrEq(animate.kids[2].kids[0].text, "loop");
      CHECK_EQ(animate.kids[2].kids[2].scalar, uint32_t(480));
    }
    CHECK_TRUE(rb_blitz::settings::AddIOSAutoPause(&roots, &why));
    CHECK_TRUE(header_test::Method(roots, "game_panel", "poll") != nullptr);
    const auto* exit = header_test::Method(roots, "game_screen", "exit");
    CHECK_TRUE(exit && exit->kids.back().kids[1].text == "rbbz-ios/game/exit");
    const auto compiled = dtb::Compile(roots, header);
    CHECK_TRUE(dtb::Parse(compiled.data(), compiled.size(), &roots, &header, &why));
    return Finish();
  }
  rb_blitz::test::SuppressWindowsTestCrashDialogs();
  if (argc == 3 && std::string(argv[1]) == "check-unlock-error-source") {
    namespace dtb = rb_blitz::ui::dtb;
    std::ifstream input(argv[2], std::ios::binary);
    CHECK_TRUE(bool(input));
    if (!input) return Finish();
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    std::vector<dtb::Node> roots;
    dtb::RootHeader header;
    std::string why;
    CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    CHECK_TRUE(rb_blitz::settings::BypassAutomaticUnlockError(&roots, &why));
    const auto compiled = dtb::Compile(roots, header);
    std::vector<dtb::Node> reparsed;
    CHECK_TRUE(dtb::Parse(compiled.data(), compiled.size(), &reparsed, &header, &why));
    return Finish();
  }
  if (argc == 3 && std::string(argv[1]) == "check-inline-help") {
    std::ifstream input(argv[2], std::ios::binary);
    CHECK_TRUE(bool(input));
    if (!input) return Finish();
    std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
    std::vector<uint8_t> scene;
    std::string why;
    const bool prepared = rb_blitz::ui::input_font::PrepareInlineHelp(original, &scene, &why);
    CHECK_TRUE(prepared);
    if (!prepared) std::printf("inline help font refused: %s\n", why.c_str());
    return Finish();
  }
  {
    BeginCase("inline help uses the private input font without changing its other records");
    constexpr size_t header = 0x810, size = 1002, at = 0x3AD;
    std::vector<uint8_t> source(header + size, 0);
    auto little = [&](size_t pos, uint32_t value) {
      for (unsigned i = 0; i < 4; ++i) source[pos + i] = uint8_t(value >> (i * 8));
    };
    auto big = [&](size_t pos, uint32_t value) {
      for (unsigned i = 0; i < 4; ++i) source[header + pos + i] = uint8_t(value >> (24 - i * 8));
    };
    constexpr std::string_view old_font = "../fonts/buttons.milo";
    constexpr std::string_view new_font = "../fonts/inline_help_icons.milo";
    little(0, 0xCABEDEAF); little(4, header); little(8, 1);
    little(12, size); little(16, 0x01000000 | size);
    big(at, static_cast<uint32_t>(old_font.size()));
    std::copy(old_font.begin(), old_font.end(), source.begin() + header + at + 4);
    big(0x3CA, 0x4198B240);
    big(0x3E6, 0xADDEADDE);
    std::vector<uint8_t> output;
    std::string why;
    CHECK_TRUE(rb_blitz::ui::input_font::PrepareInlineHelp(source, &output, &why));
    if (!output.empty()) {
      CHECK_EQ(output.size(), source.size() + new_font.size() - old_font.size());
      CHECK_TRUE(std::equal(new_font.begin(), new_font.end(), output.begin() + header + at + 4));
      CHECK_EQ(uint32_t(output[header + 0x3CA + new_font.size() - old_font.size()]) << 24 |
               uint32_t(output[header + 0x3CB + new_font.size() - old_font.size()]) << 16 |
               uint32_t(output[header + 0x3CC + new_font.size() - old_font.size()]) << 8 |
               output[header + 0x3CD + new_font.size() - old_font.size()],
               std::bit_cast<uint32_t>(20.0f));
      CHECK_TRUE(std::equal(source.begin() + header + 0x3E6, source.end(),
                            output.begin() + header + 0x3E6 + new_font.size() - old_font.size()));
    }
    source[header + at + 4] = 'x';
    CHECK_FALSE(rb_blitz::ui::input_font::PrepareInlineHelp(source, &output, &why));
  }
  if (argc == 3 && std::string(argv[1]) == "check-icon-font") {
    std::ifstream input(argv[2], std::ios::binary);
    CHECK_TRUE(bool(input));
    if (!input) return Finish();
    std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
    std::vector<uint8_t> scene, bc3;
    std::string why;
    CHECK_TRUE(rb_blitz::ui::input_font::PrepareScene(original, &scene, &bc3, &why));
    if (!why.empty()) std::printf("icon font refused: %s\n", why.c_str());
    if (!scene.empty()) CheckIconScene(scene);
    return Finish();
  }
  if (argc == 3 && std::string(argv[1]) == "check-retail-ps-pads") {
    std::ifstream input(argv[2], std::ios::binary);
    CHECK_TRUE(bool(input));
    if (!input) return Finish();
    std::vector<uint8_t> scene{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    std::string why;
    CHECK_TRUE(rb_blitz::ui::input_font::PrepareRetailPsPads(scene, &why));
    if (!why.empty()) std::printf("retail PS pads refused: %s\n", why.c_str());
    const auto pads = rb_blitz::ui::input_font::OriginalPsPadsBc3();
    CHECK_TRUE(pads != nullptr);
    if (pads) {
      for (const auto& pad : *pads) {
        CHECK_EQ(pad.size(), size_t{7});
        for (size_t mip = 0; mip < pad.size(); ++mip)
          CHECK_EQ(pad[mip].size(), size_t{1024u >> mip} * (1024u >> mip));
      }
      CHECK_TRUE((*pads)[0][0] != (*pads)[1][0]);
    }
    return Finish();
  }
  {
    BeginCase("input label styles carry both PreLoad records and PostLoad variation strings");
    constexpr size_t header = 0x810;
    std::vector<uint8_t> source(header + 91806, 0);
    auto little = [&](size_t at, uint32_t value) {
      for (unsigned i = 0; i < 4; ++i) source[at + i] = uint8_t(value >> (i * 8));
    };
    auto big = [&](size_t at, uint32_t value) {
      for (unsigned i = 0; i < 4; ++i) source[header + at + i] = uint8_t(value >> (24 - i * 8));
    };
    auto reference = [&](size_t at, const std::string& text) {
      big(at, uint32_t(text.size()));
      for (size_t i = 0; i < text.size(); ++i) source[header + at + 4 + i] = text[i];
    };
    little(0, 0xCABEDEAF); little(4, header); little(8, 1);
    little(12, 91806); little(16, 0x01000000 | 91806);
    big(0x1625F, 1);
    reference(0x16263, "../fonts/pentatonic.milo");
    reference(0x1627F, "active_normal.color");
    source[header + 0x16252] = 1;
    big(0x16296, 0x41A23D60);
    big(0x162A6, 0x3F800000);
    big(0x162AE, 0xADDEADDE);
    std::vector<uint8_t> output;
    std::string why;
    const bool prepared = rb_blitz::ui::input_font::PrepareList(source, &output, &why);
    CHECK_TRUE(prepared);
    if (prepared) {
      auto read = [&](size_t at) {
        const uint8_t* p = output.data() + header + at;
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
      };
      const uint32_t styles = read(0x1625F);
      CHECK_EQ(styles, 2u);
      CHECK_EQ(read(0x16296), 0x41A23D60u);
      size_t cursor = 0x16263;
      std::vector<std::string> fonts;
      // Mirror the two native loading phases, rather than just round-tripping
      // the generated container: PostLoad must stop at this object's marker.
      for (uint32_t i = 0; i < styles; ++i) {
        for (int ref = 0; ref < 2; ++ref) {
          const uint32_t length = read(cursor);
          if (ref == 0) fonts.emplace_back(
              reinterpret_cast<const char*>(output.data() + header + cursor + 4), length);
          cursor += 4 + length;
        }
        CHECK_EQ(read(cursor), i == 0 ? 0x41A23D60u : std::bit_cast<uint32_t>(22.3f));
        cursor += 5 * 4;
      }
      CHECK_TRUE(fonts == std::vector<std::string>({
          "../fonts/pentatonic.milo", "../fonts/input_icons.milo"}));
      for (uint32_t i = 0; i < styles; ++i) {
        CHECK_EQ(read(cursor), 0u);
        cursor += 4;
      }
      CHECK_EQ(read(cursor), 0xADDEADDEu);
    }
  }
  {
    using namespace rb_blitz::ui::dtb;
    BeginCase("the autosave warning is selected only on normal boots");
    for (const bool ultimate : {false, true}) {
      const char* source = ultimate ? "splash_screen" : "autosave_warning_screen";
      for (const bool fast : {false, true}) {
        std::vector<Node> roots{Node::Array({Node::Symbol("init"),
            Node::Symbol("init.dta"), Node::Command({Node::Symbol("app_label_register")}),
            Node::Command({Node::Symbol("set"), Node::Var("first_screen"), Node::Symbol(source)}),
            Node::Command({Node::Symbol("ui"), Node::Symbol("my_init")})})};
        std::string why;
        CHECK_TRUE(rb_blitz::settings::SetFirstBootScreen(&roots, fast, &why));
        const size_t assignment_index = fast ? 4 : 3;
        CHECK_TRUE(roots[0].kids[assignment_index].kids[2].text ==
                   (fast ? "splash_screen" : "autosave_warning_screen"));
        CHECK_EQ(roots[0].kids.size(), fast ? size_t{6} : size_t{5});
        if (fast) {
          CHECK_TRUE(roots[0].kids[3].kids[0].text == "splash_panel");
          CHECK_TRUE(roots[0].kids[3].kids[1].text == "load");
        }
        const auto selected = Compile(roots);
        CHECK_TRUE(rb_blitz::settings::SetFirstBootScreen(&roots, fast, &why));
        CHECK_TRUE(Compile(roots) == selected);
      }
    }
  }
  {
    using namespace rb_blitz::ui::dtb;
    BeginCase("normal boot preserves the authored splash wait flag and playlist");
    std::vector<Node> roots{
        Node::Array({Node::Symbol("wait_for_splash"), Node::Symbol("FALSE")}),
        Node::Array({Node::Symbol("splash_order"),
            Node::Array({Node::Symbol("ui/startup/hmx_logo_screen.milo"), Node::Int(5)}),
            Node::Array({Node::Symbol("ui/startup/esrb.milo"), Node::Int(5)})})};
    const auto playlist = Compile({roots[1]});
    std::string why;
    CHECK_TRUE(rb_blitz::settings::KeepStartupLogos(&roots, &why));
    CHECK_TRUE(roots[0].kids[1].text == "FALSE");
    CHECK_TRUE(Compile({roots[1]}) == playlist);
    roots[0].kids[1] = Node::Symbol("TRUE");
    CHECK_TRUE(rb_blitz::settings::KeepStartupLogos(&roots, &why));
    CHECK_TRUE(roots[0].kids[1].text == "TRUE");
    roots[1].kids.resize(1);
    CHECK_FALSE(rb_blitz::settings::KeepStartupLogos(&roots, &why));
  }
  {
    using namespace rb_blitz::ui::dtb;
    BeginCase("Ultimate startup selects Harmonix then the distinct rating scene in both regions");
    Node other_region;
    other_region.tag = 0x08; // #else
    const Node hmx = Node::Array({Node::Symbol("ui/startup/hmx_logo_screen.milo"), Node::Int(5)});
    const Node ultimate = Node::Array({Node::Symbol("ulti/ultimate_mods.milo"), Node::Int(5)});
    const Node esrb = Node::Array({Node::Symbol("ui/startup/esrb.milo"), Node::Int(5)});
    std::vector<Node> roots{
        Node::Array({Node::Symbol("wait_for_splash"), Node::Symbol("FALSE")}),
        Node::Array({Node::Symbol("splash_order"), ultimate, Node::Ifdef("REGION_NA"),
                     hmx, esrb, other_region, hmx, Node::Endif()})};
    std::string why;
    if (!rb_blitz::settings::SelectUltimateStartupLogo(&roots, &why)) {
      std::printf("synthetic Ultimate startup selection refused: %s\n", why.c_str());
      return 1;
    }
    CHECK_TRUE(rb_blitz::settings::KeepStartupLogos(&roots, &why));
    const auto& order = roots[1].kids;
    CHECK_EQ(order.size(), size_t{3});
    CHECK_TRUE(SerializeNodes({order[1]}) == SerializeNodes({hmx}));
    CHECK_TRUE(SerializeNodes({order[2]}) == SerializeNodes({esrb}));
    CHECK_TRUE(roots[0].kids[1].text == "TRUE");
    const auto after = Compile(roots);
    CHECK_FALSE(rb_blitz::settings::SelectUltimateStartupLogo(&roots, &why));
    CHECK_TRUE(Compile(roots) == after);
  }
  {
    using namespace rb_blitz::ui::dtb;
    BeginCase("short boots clear regional and provider logos without changing other startup settings");
    Node region;
    region.tag = kIfdef;
    region.text = "REGION_NA";
    std::vector<Node> roots{
        Node::Array({Node::Symbol("wait_for_splash"), Node::Symbol("TRUE")}),
        Node::Array({Node::Symbol("splash_order"),
            Node::Array({Node::Symbol("ulti/ultimate_mods.milo"), Node::Int(5)}), region,
            Node::Array({Node::Symbol("ui/startup/hmx_logo_screen.milo"), Node::Int(5)})}),
        Node::Array({Node::Symbol("provider_setting"), Node::Int(42)})};
    const auto extra = Compile({roots[2]});
    std::string why;
    CHECK_TRUE(rb_blitz::settings::SkipStartupLogos(&roots, &why));
    CHECK_EQ(roots[1].kids.size(), 1u);
    CHECK_TRUE(roots[0].kids[1].text == "FALSE");
    CHECK_TRUE(Compile({roots[2]}) == extra);
    CHECK_TRUE(rb_blitz::settings::SkipStartupLogos(&roots, &why));
    roots.erase(roots.begin());
    const auto before = Compile(roots);
    CHECK_FALSE(rb_blitz::settings::SkipStartupLogos(&roots, &why));
    CHECK_TRUE(Compile(roots) == before);
  }
  if (argc == 3 && std::string(argv[1]) == "check-first-screen-source") {
    using namespace rb_blitz::ui::dtb;
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<Node> roots;
    RootHeader header;
    std::string why;
    CHECK_TRUE(Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    for (const bool fast : {false, true}) {
      auto selected = roots;
      CHECK_TRUE(rb_blitz::settings::SetFirstBootScreen(&selected, fast, &why));
      const auto output = Compile(selected, header);
      CHECK_TRUE(Parse(output.data(), output.size(), &selected, &header, &why));
      bool found = false;
      for (const auto& root : selected) {
        if (root.kids.empty() || root.kids[0].text != "init") continue;
        for (const auto& statement : root.kids) {
          if (statement.tag != kCommand || statement.kids.size() != 3 ||
              statement.kids[1].text != "first_screen") continue;
          found = true;
          CHECK_TRUE(statement.kids[2].text ==
                     (fast ? "splash_screen" : "autosave_warning_screen"));
        }
      }
      CHECK_TRUE(found);
    }
    return Finish();
  }
  if (argc == 3 && std::string(argv[1]) == "check-ultimate-startup-source") {
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<rb_blitz::ui::dtb::Node> roots;
    rb_blitz::ui::dtb::RootHeader header;
    std::string why;
    if (!rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why)) {
      std::printf("parse failed: %s\n", why.c_str()); return 1;
    }
    if (!rb_blitz::settings::SelectUltimateStartupLogo(&roots, &why)) {
      std::printf("installed Ultimate startup selection refused: %s\n", why.c_str());
      return 1;
    }
    CHECK_TRUE(rb_blitz::settings::KeepStartupLogos(&roots, &why));
    for (const auto& root : roots) {
      if (root.kids.empty() || root.kids[0].text != "splash_order") continue;
      CHECK_EQ(root.kids.size(), size_t{3});
      CHECK_TRUE(root.kids[1].kids[0].text == "ui/startup/hmx_logo_screen.milo");
      CHECK_TRUE(root.kids[2].kids[0].text == "ui/startup/esrb.milo");
    }
    const auto selected = rb_blitz::ui::dtb::Compile(roots, header);
    CHECK_TRUE(rb_blitz::ui::dtb::Parse(selected.data(), selected.size(), &roots, &header, &why));
    return Finish();
  }
  if (argc == 3 && std::string(argv[1]) == "check-ultimate-rating-source") {
    namespace fs = std::filesystem;
    const fs::path root = argv[2];
    rb_blitz::settings::RecordReader read = [&](const std::string& name,
        std::vector<uint8_t>* bytes, std::string* why) {
      std::ifstream file(root / name, std::ios::binary);
      if (!file) { if (why) *why = "missing " + name; return false; }
      bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      return true;
    };
    std::vector<uint8_t> scene, original, source;
    std::string why;
    CHECK_TRUE(rb_blitz::settings::BuildUltimateRatingScene(read, &scene, &why));
    CHECK_TRUE(read("ui/startup/gen/esrb.milo_xbox", &original, &why));
    CHECK_TRUE(read("ulti/gen/ultimate_mods.milo_xbox", &source, &why));
    constexpr size_t target_start = 0x810 + 829;
    constexpr size_t source_start = 0x810 + 768;
    constexpr size_t image_size = 349455;
    CHECK_TRUE(scene.size() == original.size());
    CHECK_TRUE(std::equal(scene.begin(), scene.begin() + target_start, original.begin()));
    CHECK_TRUE(std::equal(scene.begin() + target_start + image_size, scene.end(),
                          original.begin() + target_start + image_size));
    CHECK_TRUE(std::equal(scene.begin() + target_start,
                          scene.begin() + target_start + image_size,
                          source.begin() + source_start));
    CHECK_TRUE(scene != original);
    BeginCase("portrait Ultimate zoom preserves every bitmap byte and rejects changed geometry");
    std::vector<uint8_t> portrait;
    CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitStartupLogo(scene, &portrait, &why));
    CHECK_TRUE(portrait.size() == 0x810 + 351617);
    CHECK_TRUE(std::equal(portrait.begin() + target_start,
                          portrait.begin() + target_start + image_size,
                          source.begin() + source_start));
    const auto* scale = portrait.data() + 0x810 + 0x55A90;
    const auto zoom = std::bit_cast<float>(uint32_t(scale[0]) << 24 | uint32_t(scale[1]) << 16 |
                                         uint32_t(scale[2]) << 8 | scale[3]);
    CHECK_TRUE(zoom > 1 && zoom * .44f < .9f);
    portrait.back() ^= 1;
    std::vector<uint8_t> untouched{1, 2, 3};
    CHECK_FALSE(rb_blitz::ui::input_font::PreparePortraitStartupLogo(portrait, &untouched, &why));
    CHECK_TRUE(untouched == std::vector<uint8_t>({1, 2, 3}));
    return Finish();
  }
  if (argc == 3 && std::string(argv[1]) == "check-startup-record") {
    using namespace rb_blitz::ui::dtb;
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<Node> roots;
    RootHeader header;
    std::string why;
    CHECK_TRUE(Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    CHECK_TRUE(Compile(roots, header) == bytes);
    bool has_harmonix = false;
    bool has_ultimate = false;
    std::vector<uint8_t> retail_playlist;
    for (const auto& root : roots) {
      if (root.kids.empty() || root.kids[0].text != "splash_order") continue;
      retail_playlist = Compile({root}, header);
      for (const auto& entry : root.kids) {
        if (entry.kids.empty()) continue;
        has_harmonix |= entry.kids[0].text == "ui/startup/hmx_logo_screen.milo";
        has_ultimate |= entry.kids[0].text == "ulti/ultimate_mods.milo";
      }
    }
    CHECK_TRUE(has_harmonix);
    CHECK_FALSE(has_ultimate);
    CHECK_TRUE(rb_blitz::settings::KeepStartupLogos(&roots, &why));
    for (const auto& root : roots) {
      if (root.kids.empty()) continue;
      if (root.kids[0].text == "splash_order") CHECK_TRUE(Compile({root}, header) == retail_playlist);
      if (root.kids[0].text == "wait_for_splash") CHECK_TRUE(root.kids[1].text == "TRUE");
    }
    CHECK_TRUE(rb_blitz::settings::SkipStartupLogos(&roots, &why));
    for (const auto& root : roots) {
      if (root.kids.empty()) continue;
      if (root.kids[0].text == "splash_order") CHECK_EQ(root.kids.size(), 1u);
      if (root.kids[0].text == "wait_for_splash") CHECK_TRUE(root.kids[1].text == "FALSE");
    }
    const auto skipped = Compile(roots, header);
    CHECK_TRUE(Parse(skipped.data(), skipped.size(), &roots, &header, &why));
    CHECK_TRUE(Compile(roots, header) == skipped);
    return Finish();
  }
  if (argc == 4 && std::string(argv[1]) == "check-startup-output") {
    using namespace rb_blitz::ui::dtb;
    const bool ultimate_route = std::string(argv[3]) == "ultimate";
    if (!ultimate_route && std::string(argv[3]) != "retail") return 2;
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<Node> roots;
    RootHeader header;
    std::string why;
    CHECK_TRUE(Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    CHECK_TRUE(Compile(roots, header) == bytes);
    size_t playlists = 0;
    size_t waits = 0;
    for (const auto& root : roots) {
      if (root.kids.empty()) continue;
      if (root.kids[0].text == "wait_for_splash") {
        ++waits;
        CHECK_TRUE(root.kids.size() == 2 && root.kids[1].text == "TRUE");
      }
      if (root.kids[0].text != "splash_order") continue;
      ++playlists;
      bool has_harmonix = false;
      bool has_ultimate = false;
      for (const auto& entry : root.kids) {
        if (entry.kids.empty()) continue;
        has_harmonix |= entry.kids[0].text == "ui/startup/hmx_logo_screen.milo";
        has_ultimate |= entry.kids[0].text == "ulti/ultimate_mods.milo";
      }
      if (ultimate_route) {
        CHECK_EQ(root.kids.size(), size_t{3});
        CHECK_TRUE(root.kids[1].kids[0].text == "ui/startup/hmx_logo_screen.milo");
        CHECK_TRUE(root.kids[2].kids[0].text == "ui/startup/esrb.milo");
        CHECK_FALSE(has_ultimate);
        CHECK_TRUE(has_harmonix);
      } else {
        CHECK_TRUE(root.kids.size() > 1);
        CHECK_TRUE(has_harmonix);
        CHECK_FALSE(has_ultimate);
      }
    }
    CHECK_EQ(playlists, size_t{1});
    CHECK_EQ(waits, size_t{1});
    return Finish();
  }
  {
    namespace dtb = rb_blitz::ui::dtb;
    using dtb::Node;
    BeginCase("short purchased boot reroutes only the automatic full-game error");
    std::vector<Node> roots{Node::Command({Node::Symbol("new"), Node::Symbol("UIPanel"),
        Node::Symbol("error_panel"), Node::Array({Node::Symbol("file"),
        Node::String("error_screen.milo")})}),
        Node::Command({Node::Symbol("new"), Node::Symbol("UIScreen"),
        Node::Symbol("error_screen"), Node::Array({Node::Symbol("panels"),
        Node::Symbol("shell_music_panel"), Node::Symbol("error_panel")}),
        Node::Array({Node::Symbol("enter"),
        Node::Command({Node::Var("this"), Node::Symbol("reset_audio")}),
        Node::Command({Node::Symbol("status.lbl"), Node::Symbol("set"),
                       Node::Symbol("text_token"),
                       Node::Property({Node::Symbol("error_token")})})})})};
    std::string why;
    CHECK_TRUE(rb_blitz::settings::BypassAutomaticUnlockError(&roots, &why));
    const auto* panel_load = header_test::Method(roots, "error_panel", "load");
    CHECK_TRUE(panel_load != nullptr);
    if (panel_load) CHECK_TRUE(panel_load->kids[1].kids[2].text == "FALSE");
    const auto* screen_panels = header_test::Method(roots, "error_screen", "panels");
    CHECK_TRUE(screen_panels != nullptr);
    if (screen_panels) {
      CHECK_EQ(screen_panels->kids.size(), size_t{4});
      CHECK_TRUE(screen_panels->kids[2].text == "splash_panel");
    }
    const auto* method = header_test::Method(roots, "error_screen", "enter");
    CHECK_TRUE(method != nullptr);
    if (method) {
      CHECK_EQ(method->kids.size(), size_t{3});
      const auto& route = method->kids[2];
      CHECK_TRUE(route.kids[0].text == "if_else");
      CHECK_TRUE(route.kids[1].kids[2].text == "error_signin_unlocked_full_game");
      CHECK_TRUE(route.kids[2].kids[3].kids[1].text == "goto_screen");
      CHECK_TRUE(route.kids[2].kids[3].kids[2].text == "server_connect_screen");
      CHECK_TRUE(route.kids[3].kids[1].kids[0].text == "error_panel");
      CHECK_TRUE(route.kids[3].kids[1].kids[2].text == "TRUE");
      CHECK_TRUE(route.kids[3].kids[2].kids[1].text == "reset_audio");
    }
    const auto compiled = dtb::Compile(roots);
    std::vector<Node> reparsed;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(compiled.data(), compiled.size(), &reparsed, &header, &why));
    std::vector<Node> invalid{Node::Command({Node::Symbol("new"), Node::Symbol("UIScreen"),
        Node::Symbol("error_screen")})};
    const auto before = dtb::Compile(invalid);
    CHECK_FALSE(rb_blitz::settings::BypassAutomaticUnlockError(&invalid, &why));
    CHECK_TRUE(dtb::Compile(invalid) == before);
  }
  {
    namespace dtb = rb_blitz::ui::dtb;
    using dtb::Node;
    BeginCase("crash recovery preserves splash methods and refuses incompatible records atomically");
    std::vector<Node> roots{
        Node::Command({Node::Symbol("new"), Node::Symbol("UIPanel"), Node::Symbol("splash_panel"),
            Node::Array({Node::Symbol("enter"), Node::Command({Node::Symbol("original_enter")})}),
            Node::Array({Node::Symbol("poll"), Node::Command({Node::Symbol("provider_poll")})})}),
        Node::Command({Node::Symbol("new"), Node::Symbol("UIScreen"), Node::Symbol("splash_screen"),
            Node::Array({Node::Symbol("enter"), Node::Command({Node::Symbol("screen_enter")})})})};
    std::string why;
    CHECK_TRUE(rb_blitz::settings::AddCrashRecoveryHooks(&roots, &why));
    const auto bytes = dtb::Compile(roots);
    const auto authored = dtb::SerializeNodes(roots);
    const std::string authored_text(authored.begin(), authored.end());
    CHECK_TRUE(authored_text.find("rbbz-load/recovery/graphics") != std::string::npos);
    CHECK_TRUE(authored_text.find("rbbz_graphics_recovered") != std::string::npos);
    const auto before = bytes;
    CHECK_FALSE(rb_blitz::settings::AddCrashRecoveryHooks(&roots, &why));
    CHECK_TRUE(dtb::Compile(roots) == before);
    CHECK_TRUE(roots[0].kids[3].kids[1].kids[0].text == "original_enter");
    CHECK_TRUE(roots[0].kids[4].kids[1].kids[0].text == "provider_poll");
    std::vector<Node> invalid{Node::Command({Node::Symbol("new"), Node::Symbol("UIPanel"), Node::Symbol("splash_panel")})};
    const auto original = dtb::Compile(invalid);
    CHECK_FALSE(rb_blitz::settings::AddCrashRecoveryHooks(&invalid, &why));
    CHECK_TRUE(dtb::Compile(invalid) == original);
  }
  if (argc == 3 && std::string(argv[1]) == "check-discovery-record") {
    namespace dtb = rb_blitz::ui::dtb;
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    std::vector<dtb::Node> roots;
    dtb::RootHeader header;
    std::string why;
    CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    CHECK_TRUE(rb_blitz::settings::SetDiscoveryStartLabel(&roots));
    CHECK_TRUE(rb_blitz::settings::AddGraphicsLoadingGate(&roots,&why));
    const auto count = [](auto&& self, const dtb::Node& n, const std::string& text) -> int {
      int total = n.text == text;
      for (const auto& child : n.kids) total += self(self,child,text);
      return total;
    };
    const auto count_all = [&](const std::string& text) {
      int total = 0; for (const auto& n : roots) total += count(count,n,text); return total;
    };
    // Confirm and Option both call the same gate; main_menu exists only inside
    // the ready continuation. Applying R3 must preserve that invariant.
    CHECK_EQ(count_all("main_menu"),1);
    CHECK_EQ(count_all("rbbz_begin_graphics"),3);
    bytes = dtb::Compile(roots, header);
    // The generated record must remain compatible with the later read-time
    // offline transform, which removes the unused named state-28 constant.
    CHECK_TRUE(rb_blitz::menu_options::SkipOfflinePrompts(bytes.data(), bytes.size()).applied);
    CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    CHECK_EQ(count_all("main_menu"),1);
    CHECK_EQ(count_all("rbbz_begin_graphics"),4);
    CHECK_EQ(count_all("rbbz-graphics/cancel"),1);
    TestGraphicsRecovery(roots);
    const auto before = dtb::Compile(roots, header);
    CHECK_TRUE(rb_blitz::settings::SetDiscoveryStartLabel(&roots));
    CHECK_TRUE(dtb::Compile(roots, header) == before);
    return Finish();
  }
  if (argc == 4 && std::string(argv[1]) == "check-header-records") {
    try { return CheckInstalledHeaderRecords(argv[2], argv[3]); }
    catch (const std::exception& error) {
      std::printf("header record check threw: %s\n", error.what());
      return 1;
    }
  }
  if (argc == 3 && (std::string(argv[1]) == "check-runtime-header-records" ||
                    std::string(argv[1]) == "check-offline-records")) {
    return CheckInstalledRuntimeHeaderRecords(argv[2], std::string(argv[1]) == "check-offline-records");
  }
  if (argc == 4 && std::string(argv[1]) == "check-p10-records") {
    return CheckInstalledHeaderRecords(argv[2], argv[3], true);
  }
  if (argc == 4 && std::string(argv[1]) == "check-portrait-records") {
    namespace settings = rb_blitz::settings;
    namespace dtb = rb_blitz::ui::dtb;
    namespace screen = rb_blitz::ui::screen;
    const auto input = std::filesystem::path(argv[2]);
    settings::RecordReader read = [&](const std::string& name, std::vector<uint8_t>* bytes, std::string* why) {
      std::ifstream file(input / name, std::ios::binary);
      if (!file) { if (why) *why = "missing " + name; return false; }
      bytes->assign(std::istreambuf_iterator<char>(file), {});
      return true;
    };
    screen::MenuConfiguration config;
    config.controller_layout_enabled = true;
    screen::ConfigureMenu(config);
    settings::MenuRecords landscape, portrait;
    std::string why;
    for (const bool enabled : {false, true}) {
      std::vector<std::string> notes;
      if (!settings::BuildMenuRecords(read, {"ui/locale/eng/gen/locale_keep.dtb"},
          enabled ? &portrait : &landscape, &why, false, false, &notes, {}, false,
          nullptr, {}, enabled)) {
        std::printf("portrait records refused: %s\n", why.c_str()); return 1;
      }
      for (const auto& note : notes) std::printf("record note: %s\n", note.c_str());
      CHECK_TRUE(notes.empty());
    }
    BeginCase("mixed orientations author interface scenes and gameplay HUD independently");
    for (const bool interface_portrait : {false, true}) {
      settings::MenuRecords mixed;
      const bool gameplay_portrait = !interface_portrait;
      CHECK_TRUE(settings::BuildMenuRecords(read, {"ui/locale/eng/gen/locale_keep.dtb"},
          &mixed, &why, false, false, nullptr, {}, false, nullptr, {},
          interface_portrait, false, gameplay_portrait));
      const auto& locale = mixed.at("ui/locale/eng/gen/locale_keep.dtb");
      std::vector<dtb::Node> localized;
      dtb::RootHeader locale_header;
      CHECK_TRUE(dtb::Parse(locale.data(), locale.size(), &localized, &locale_header, &why));
      const auto locale_text = [&](std::string_view key) {
        for (const auto& root : localized)
          if (root.kids.size() == 2 && root.kids[0].text == key) return root.kids[1].text;
        return std::string();
      };
      CHECK_TRUE(locale_text("settings_achievements") ==
                 (interface_portrait ? "Unlocks" : "Achievements & Unlocks"));
      CHECK_TRUE(locale_text("settings_title_settings_controller_layout_panel") ==
                 (interface_portrait ? "LAYOUT" : "CONTROLLER LAYOUT"));
      CHECK_TRUE(locale_text("settings_mm_rhythmverse_off") ==
                 (interface_portrait ? "Shortcut: Visible" : "Main Menu Shortcut: Visible"));
      CHECK_TRUE(locale_text("settings_mm_achievements_off") == "Achievements: Visible");
      const auto& interface_records = interface_portrait ? portrait : landscape;
      const auto& gameplay_records = gameplay_portrait ? portrait : landscape;
      for (const auto& [path, bytes] : portrait) {
        const auto& expected = path == "ui/gen/game.dtb" ? gameplay_records : interface_records;
        CHECK_EQ(mixed.contains(path), expected.contains(path));
        if (mixed.contains(path) && expected.contains(path))
          CHECK_TRUE(mixed.at(path) == expected.at(path));
      }
    }
    BeginCase("touch audio and video instructions preserve all six installed languages and restore physical input");
    for (const char* language : {"deu", "eng", "esl", "fre", "ita", "ptb"}) {
      std::vector<uint8_t> bytes;
      CHECK_TRUE(read(std::string("ui/locale/") + language + "/gen/locale_keep.dtb", &bytes, &why));
      std::vector<dtb::Node> localized;
      dtb::RootHeader locale_header;
      CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &localized, &locale_header, &why));
      size_t instructions = 0;
      for (const auto& root : localized) {
        if (root.tag != dtb::kArray || root.kids.size() < 2 ||
            (root.kids[0].text != "cal_audio_desc_pad" && root.kids[0].text != "cal_video_desc_pad")) continue;
        ++instructions;
        rb_blitz::input::CalibrationPromptText prompt;
        const auto& original = root.kids[1].text;
        const auto touch = prompt.Update(original, true, false);
        CHECK_TRUE(touch != original);
        CHECK_TRUE(touch.find("<alt1>") == std::string::npos);
        CHECK_TRUE(prompt.Update(touch, false, false) == original);
      }
      CHECK_EQ(instructions, size_t{2});
    }
    BeginCase("portrait payload changes splash buttons, loading layouts and the existing gameplay HUD route");
    CHECK_EQ(portrait.size(), landscape.size() + 30 + !landscape.contains("ui/gen/game.dtb") +
                                      !landscape.contains("ui/loading/gen/loading.dtb"));
    for (const auto& [path, original] : landscape) {
      CHECK_TRUE(portrait.contains(path));
      if (!path.starts_with("ui/locale/") && path != "ui/splash/gen/splash.dtb" && path != "ui/gen/game.dtb" &&
          path != "ui/net/gen/server_connect.dtb" && path != "ui/av_options/gen/av_options.dtb" &&
          path != "ui/av_options/gen/settings_scale.milo_xbox" &&
          path != "ui/av_options/gen/settings_scale_pause.milo_xbox" &&
          path != "ui/calibration/gen/calibration.dtb" &&
          path != "ui/song_select/gen/song_select.dtb" &&
          path != "ui/background/gen/background.dtb" &&
          path != "ui/inbox/gen/inbox.dtb" &&
          path != "ui/recommendations/gen/recommendations.dtb" &&
          path != "ui/powerup_select/gen/powerup_select.dtb" &&
          path != rb_blitz::ui::input_font::kPowerupSelectRecord &&
          path != rb_blitz::ui::input_font::kSongSelectRecord &&
          path != rb_blitz::ui::input_font::kSplashRecord &&
          path != rb_blitz::ui::input_font::kErrorReturnRecord &&
          path != rb_blitz::ui::input_font::kCalibrationRecord &&
          path != rb_blitz::ui::input_font::kLoadingRecord &&
          path != screen::kSceneRecord && path != screen::kPauseSceneRecord &&
          path != "ui/pause/gen/confirmation_message.milo_xbox" &&
          path != "ui/pause/gen/settings_confirmation.milo_xbox" &&
          path != "ui/pause/gen/settings_confirmation_pause.milo_xbox")
      {
        auto expected = original;
        // Scenes without a dedicated layout still inherit the shared canvas fit.
        if (path.starts_with("ui/") && path.ends_with(".milo_xbox"))
          CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitCanvas(original, &expected, &why));
        if (portrait.at(path) != expected) std::printf("unexpected portrait record change: %s\n", path.c_str());
        CHECK_TRUE(portrait.at(path) == expected);
      }
    }
    CHECK_TRUE(portrait.contains(rb_blitz::ui::input_font::kSplashListRecord));
    std::vector<uint8_t> portrait_buttons, portrait_welcome;
    CHECK_TRUE(read(rb_blitz::ui::input_font::kListRecord, &portrait_buttons, &why));
    CHECK_TRUE(read("ui/calibration/gen/cal_welcome.milo_xbox", &portrait_welcome, &why));
    BeginCase("portrait calibration and presets preserve validated native scenes at narrow aspects");
    for (const char* name : {"cal_welcome", "cal_auto", "cal_manual", "cal_quick", "cal_complete", "cal_failed", "controller_config"}) {
      const std::string path = std::string("ui/") +
          (std::string_view(name) == "controller_config" ? "controller_config" : "calibration") +
          "/gen/" + name + ".milo_xbox";
      CHECK_TRUE(portrait.contains(path));
      CHECK_TRUE(rb_blitz::ui::PortraitControlsScene(name));
      CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("", std::string(name) + "_panel"));
      std::vector<uint8_t> source;
      CHECK_TRUE(read(path, &source, &why));
      for (float aspect : {402.f / 874, 9.f / 16, .75f}) {
        std::vector<uint8_t> fitted;
        CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitControls(source, portrait_buttons, portrait_welcome, aspect, &fitted, &why));
        CHECK_TRUE(fitted != source);
      }
      source[0] ^= 1;
      std::vector<uint8_t> refused;
      CHECK_FALSE(rb_blitz::ui::input_font::PreparePortraitControls(source, portrait_buttons, portrait_welcome, config.portrait_aspect, &refused, &why));
    }
    const auto& button_font = portrait.at(rb_blitz::ui::input_font::kPortraitSplashFontRecord);
    CHECK_TRUE(button_font.size() > 0x810 + 0x156102 + 0x29);
    for (size_t material : {size_t(0x155D8A), size_t(0x155EB2), size_t(0x155FDA), size_t(0x156102)})
      CHECK_EQ(button_font[0x810 + material + 0x29], uint8_t(0)); // unlit ink in every variation
    BeginCase("portrait vignette has visible, translucent native geometry with valid faces");
    const auto& art = portrait.at(rb_blitz::ui::input_font::kSplashRecord);
    const auto u32 = [](const uint8_t* p) {
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    BeginCase("portrait Start preserves the landscape prompt's text/icon metrics and hidden panel");
    std::vector<uint8_t> original_splash, landscape_splash;
    CHECK_TRUE(read(rb_blitz::ui::input_font::kSplashRecord, &original_splash, &why));
    CHECK_TRUE(rb_blitz::ui::input_font::PrepareSplash(original_splash, &landscape_splash, &why));
    // PrepareSplash returns a stored stream. Its only variable-length edit
    // lives inside start.but, after the measured beginning of that object.
    const auto button_begin = landscape_splash.begin() + 0x810 + 0x9F656C;
    const std::array<uint8_t, 4> marker{0xAD, 0xDE, 0xAD, 0xDE};
    const auto button_end = std::search(button_begin, landscape_splash.end(), marker.begin(), marker.end());
    CHECK_TRUE(button_end != landscape_splash.end());
    if (button_end == landscape_splash.end()) return Finish();
    CHECK_TRUE(std::search(art.begin(), art.end(), button_begin, button_end + 4) != art.end());
    constexpr size_t font_growth = std::string_view("inline_help_icons").size() -
                                   std::string_view("buttons").size();
    const auto panel_begin = landscape_splash.begin() + 0x810 + 0x9F6B5F + font_growth;
    const auto panel_end = landscape_splash.begin() + 0x810 + 0x9F6CF9 + font_growth;
    CHECK_EQ(panel_begin[149], uint8_t(0));
    CHECK_TRUE(std::search(art.begin(), art.end(), panel_begin, panel_end) != art.end());
    BeginCase("portrait settings share a native Back button while landscape keeps its original scene");
    CHECK_TRUE(portrait.at(screen::kSceneRecord) == portrait.at(screen::kPauseSceneRecord));
    std::vector<uint8_t> original_settings;
    CHECK_TRUE(read(screen::kSceneSourceRecord, &original_settings, &why));
    CHECK_TRUE(landscape.at(screen::kSceneRecord) == original_settings);
    const auto& settings_scene = portrait.at(screen::kSceneRecord);
    CHECK_TRUE(settings_scene.size() > 0x810 + 17455);
    const auto stored_objects = [&](const std::vector<uint8_t>& packed) {
      const auto* bytes = packed.data() + 0x810;
      const auto size = packed.size() - 0x810;
      size_t at = 4;
      auto string = [&] {
        const auto length = u32(bytes + at); at += 4;
        std::string value(reinterpret_cast<const char*>(bytes + at), length);
        at += length; return value;
      };
      string(); string(); at += 9;
      const auto count = u32(bytes + at); at += 4;
      std::vector<std::string> names;
      for (uint32_t i = 0; i < count; ++i) { string(); names.push_back(string()); }
      const auto markers = rb_blitz::ui::input_font::detail::MiloObjectBoundaries(
          std::span<const uint8_t>(bytes, size), at, count);
      std::map<std::string, std::vector<uint8_t>> objects;
      for (size_t i = 0; i < count; ++i)
        objects[names[i]] = {bytes + markers[i] + 4, bytes + markers[i + 1] + 4};
      return objects;
    };
    const auto scene_root = [&](const std::vector<uint8_t>& packed) {
      const auto* bytes = packed.data() + 0x810;
      const size_t at = 8 + u32(bytes + 4);
      return std::string(reinterpret_cast<const char*>(bytes + at + 4), u32(bytes + at));
    };
    const auto object_parent = [&](const std::vector<uint8_t>& data, size_t matrix) {
      const size_t at = matrix + 105;
      const auto length = u32(data.data() + at);
      CHECK_TRUE(at + 4 + length <= data.size());
      return std::string(reinterpret_cast<const char*>(data.data() + at + 4), length);
    };
    const auto object_showing = [&](const std::vector<uint8_t>& data, size_t matrix) {
      return data.at(matrix + 113 + object_parent(data, matrix).size());
    };
    const auto group_members = [&](const std::vector<uint8_t>& data) {
      return u32(data.data() + 0xAB + object_parent(data, 0x21).size());
    };
    namespace font = rb_blitz::ui::input_font;
    BeginCase("portrait pause uses native button providers and preserves routing across orientations");
    CHECK_FALSE(landscape.contains(font::kPauseRecord));
    CHECK_TRUE(portrait.contains(font::kPauseRecord));
    CHECK_TRUE(portrait.at("ui/pause/gen/pause.dtb") == landscape.at("ui/pause/gen/pause.dtb"));
    std::vector<uint8_t> pause_source;
    CHECK_TRUE(read(font::kPauseRecord, &pause_source, &why));
    for (float aspect : {.3f, 402.f / 874, .75f, .9f}) {
      std::vector<uint8_t> scene;
      const auto source_copy = pause_source;
      CHECK_TRUE(font::PreparePortraitPause(pause_source, portrait_buttons, aspect, &scene, &why));
      CHECK_TRUE(pause_source == source_copy);
      const auto objects = stored_objects(scene);
      for (const char* name : {"pause.lst", "options.lst", "pause.lbl", "select.ihp",
                               "Group.grp", "enter.flow", "on.anim", "popup_neon",
                               "portrait_pause_panel.mesh", "portrait_confirm.mesh", "portrait_back.mesh"})
        CHECK_TRUE(objects.contains(name));
      const auto contains = [](const auto& bytes, std::string_view text) {
        return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
      };
      for (const char* name : {"pause.lst", "options.lst"}) {
        const auto& list = objects.at(name);
        CHECK_TRUE(object_parent(list, 0x19) == "Group.grp");
        CHECK_TRUE(contains(list, "../resource/lists/portrait_prompt.milo"));
      }
      CHECK_TRUE(object_parent(objects.at("portrait_pause_panel.mesh"), 0x15) == "Group.grp");
      CHECK_TRUE(contains(objects.at("select.ihp"), "portrait_actions.milo"));
      CHECK_TRUE(contains(objects.at("pause.lbl"), "portrait_splash.milo"));
      CHECK_FALSE(contains(objects.at("Group.grp"), "popup_neon"));
      CHECK_FALSE(contains(objects.at("Group.grp"), "main_bg1.mesh"));
      CHECK_TRUE(contains(objects.at("Group.grp"), "portrait_pause_panel.mesh"));
    }
    auto changed_pause = pause_source;
    changed_pause.back() ^= 1;
    std::vector<uint8_t> refused{1, 2, 3};
    CHECK_FALSE(font::PreparePortraitPause(changed_pause, portrait_buttons, .5f, &refused, &why));
    CHECK_TRUE(refused == std::vector<uint8_t>({1, 2, 3}));
    BeginCase("portrait Messages retains native row icons, wrapped text and independent detail fonts");
    for (float aspect : {.3f, 402.f / 874, .75f, .9f}) {
      std::vector<uint8_t> source, scene;
      CHECK_TRUE(read(font::kMessagesRecord, &source, &why));
      const auto original = source;
      CHECK_TRUE(font::PreparePortraitMessages(source, portrait_buttons, aspect, &scene, &why));
      CHECK_TRUE(source == original);
      const auto panel = stored_objects(scene);
      for (const char* name : {"songs.lst", "subject.lbl", "message.lbl", "portrait_tab_messages.lbl",
                               "portrait_messages_reading.mesh", "portrait_messages_reading_border.mesh",
                               "portrait_header_previous.mesh", "portrait_header_next.mesh",
                               "portrait_confirm.mesh", "portrait_back.mesh", "InlineHelp.ihp"})
        CHECK_TRUE(panel.contains(name));
      const auto contains = [](const auto& bytes, std::string_view text) {
        return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
      };
      CHECK_TRUE(contains(panel.at("songs.lst"), "portrait_inbox.milo"));
      CHECK_TRUE(contains(panel.at("InlineHelp.ihp"), "portrait_actions.milo"));
      CHECK_EQ(u32(panel.at("InlineHelp.ihp").data() + 9), 1u);
      CHECK_EQ(u32(panel.at("InlineHelp.ihp").data() + 13), 2u);
      CHECK_TRUE(panel.contains("portrait_messages_list.mesh"));
      CHECK_TRUE(contains(panel.at("message.lbl"), "portrait_splash.milo"));
      CHECK_TRUE(contains(panel.at("message.lbl"), "pentatonic_bold.milo"));
      const auto layout = rb_blitz::ui::PortraitMessages(aspect);
      const auto& slide = panel.at("TransAnim.tnm");
      const float distance = 1022 * rb_blitz::ui::PortraitSplash(aspect).width / (480 * 16.f / 9);
      CHECK_TRUE(std::abs(std::bit_cast<float>(u32(slide.data() + 53)) + distance) < .001f);
      CHECK_TRUE(std::abs(std::bit_cast<float>(u32(slide.data() + 85)) - distance) < .001f);
      CHECK_EQ(u32(slide.data() + 65), std::bit_cast<uint32_t>(-10.f));
      CHECK_EQ(u32(slide.data() + 97), std::bit_cast<uint32_t>(10.f));
      for (const char* name : {"songs.lst", "subject.lbl", "message.lbl", "portrait_tab_messages.lbl"})
        CHECK_EQ(u32(panel.at(name).data() + 0x19 + 105), 12u);
      CHECK_EQ(u32(panel.at("songs.lst").data() + 0x19 + 44), std::bit_cast<uint32_t>(layout.list_z));
      CHECK_TRUE(contains(panel.at("all.grp"), "portrait_tab_messages.lbl"));
      CHECK_TRUE(read("ui/resource/lists/gen/list_inbox.milo_xbox", &source, &why));
      const auto list_source = source;
      CHECK_TRUE(font::PreparePortraitMessagesList(source, portrait_buttons, aspect, &scene, &why));
      CHECK_TRUE(source == list_source);
      const auto list = stored_objects(scene);
      for (const char* name : {"icon", "icon.mesh", "icon_misc.mat", "icon_more_stars.mat",
                               "icon_misc.tex", "subject_line", "subject_line.lbl", "new",
                               "portrait_message_header.lbl", "portrait_row.mesh", "portrait_selected.mesh"})
        CHECK_TRUE(list.contains(name));
      CHECK_FALSE(list.contains("slot"));
      CHECK_FALSE(list.contains("highlight"));
      CHECK_FALSE(list.contains("time")); // The feed's published date is already in the subject.
      CHECK_FALSE(list.contains("header.lbl")); // Avoid treating a row heading as the page title.
      CHECK_TRUE(contains(list.at("icon"), "icon.mesh"));
      CHECK_EQ(u32(list.at("icon").data() + 25), std::bit_cast<uint32_t>(2.f));
      CHECK_EQ(u32(list.at("icon.mesh").data() + 0x15), std::bit_cast<uint32_t>(layout.icon_size / 50));
      CHECK_EQ(u32(list.at("icon.mesh").data() + 0x15 + 36),
               std::bit_cast<uint32_t>(-layout.width / 2 + layout.icon_inset));
      CHECK_FALSE(contains(list.at("portrait_old_rows.grp"), "icon.mesh"));
      std::vector<uint8_t> refused{1,2,3};
      source.back() ^= 1;
      CHECK_FALSE(font::PreparePortraitMessagesList(source, portrait_buttons, aspect, &refused, &why));
      CHECK_TRUE((refused == std::vector<uint8_t>{1,2,3}));
    }
    CHECK_FALSE(landscape.contains(font::kMessagesRecord));
    CHECK_FALSE(landscape.contains(font::kPortraitMessagesListRecord));
    BeginCase("portrait Recommended uses static header arrows, bounded text and two actions without loading or empty panels");
    for (float aspect : {.3f, 402.f / 874, .75f, .9f}) {
      const auto layout = rb_blitz::ui::PortraitRecommendations(aspect);
      const auto contains = [](const auto& bytes, std::string_view text) {
        return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
      };
      std::vector<uint8_t> source, scene, header_arrows;
      CHECK_TRUE(read("ui/background/gen/background.milo_xbox", &header_arrows, &why));
      CHECK_TRUE(read(font::kRecommendationsRecord, &source, &why));
      const auto original = source;
      CHECK_TRUE(font::PreparePortraitRecommendations(source, portrait_buttons, header_arrows, aspect, &scene, &why));
      CHECK_TRUE(source == original);
      const auto panel = stored_objects(scene);
      for (const char* name : {"recommendations.lst", "mini_leaders.lst", "rec_icon", "description.lbl",
                               "song_title.lbl", "song_artist.lbl", "song_info.lbl", "portrait_tab_recs.lbl",
                               "portrait_rec_previous.lbl", "portrait_rec_next.lbl", "portrait_rec_empty.lbl",
                               "portrait_rec_position.lbl", "portrait_rec_board_state.grp", "InlineHelp.ihp"})
        CHECK_TRUE(panel.contains(name));
      CHECK_TRUE(contains(panel.at("recommendations.lst"), "portrait_recommendations.milo"));
      CHECK_TRUE(contains(panel.at("mini_leaders.lst"), "portrait_rec_leaders.milo"));
      CHECK_TRUE(contains(panel.at("description.lbl"), "portrait_splash.milo"));
      CHECK_TRUE(contains(panel.at("description.lbl"), "pentatonic_bold.milo"));
      CHECK_TRUE(contains(panel.at("InlineHelp.ihp"), "portrait_actions.milo"));
      CHECK_TRUE(contains(panel.at("portrait_rec_board_state.grp"), "no_board.grp"));
      CHECK_FALSE(contains(panel.at("portrait_rec_board_state.grp"), "loading.grp"));
      CHECK_TRUE(contains(panel.at("portrait_rec_board_state.grp"), "mini_leaders.grp"));
      CHECK_TRUE(contains(panel.at("portrait_rec_unused.grp"), "loading.grp"));
      CHECK_TRUE(contains(panel.at("portrait_rec_unused.grp"), "rec_icon"));
      const auto& unused = panel.at("portrait_rec_unused.grp");
      CHECK_EQ(unused[0x21 + 113 + u32(unused.data() + 0x21 + 105)], 0);
      CHECK_FALSE(contains(panel.at("home.grp"), "loading.grp"));
      CHECK_FALSE(contains(panel.at("home.grp"), "rec_icon"));
      CHECK_FALSE(panel.contains("portrait_rec_board.mesh"));
      for (const char* stem : {"portrait_rec_previous", "portrait_rec_next"}) {
        CHECK_TRUE(panel.contains(std::string(stem) + "_arrow.mesh"));
        CHECK_TRUE(contains(panel.at(std::string(stem) + "_arrow.mat"), "shell_nav_arrow.tex"));
        CHECK_FALSE(contains(panel.at(std::string(stem) + "_arrow.mesh"), ".flow"));
      }
      CHECK_TRUE(panel.contains("spinner_hoz") && panel.contains("spinner_hoz1") && panel.contains("spinner_vert"));
      CHECK_TRUE(std::bit_cast<float>(u32(panel.at("portrait_rec_details.mat").data() + 0x25)) >= .9f);
      for (const char* name : {"song_title.lbl", "song_artist.lbl", "song_info.lbl", "description.lbl", "recommendations.lst"})
        CHECK_EQ(u32(panel.at(name).data() + 0x19 + 105), 12u);
      CHECK_EQ(u32(panel.at("recommendations.lst").data() + 0x19 + 44), std::bit_cast<uint32_t>(layout.carousel_z));
      for (size_t i = 0; i < 2; ++i)
        CHECK_TRUE(panel.contains(std::string(rb_blitz::ui::PortraitRecommendationFooterSurface(i)) + ".mesh"));
      CHECK_FALSE(panel.contains("portrait_rec_action_2.mesh"));
      CHECK_TRUE(read("ui/resource/lists/gen/list_recommendations_albums.milo_xbox", &source, &why));
      const auto original_list = source;
      CHECK_TRUE(font::PreparePortraitRecommendationList(source, aspect, &scene, &why));
      CHECK_TRUE(source == original_list);
      const auto cards = stored_objects(scene);
      CHECK_TRUE(cards.contains("album_mesh") && cards.contains("album0.mat") && cards.contains("rec_type"));
      CHECK_FALSE(cards.contains("shade") || cards.contains("shade_highlight") || cards.contains("UIListHighlight.arw"));
      CHECK_TRUE(contains(cards.at("album_mesh"), "album_slot.mesh"));
      CHECK_EQ(u32(cards.at("album_slot.mesh").data() + 0x15), std::bit_cast<uint32_t>(layout.art_size / 125));
      CHECK_EQ(u32(cards.at("album_slot.mesh").data() + 0x15 + 36), std::bit_cast<uint32_t>(0.f));
      CHECK_TRUE(contains(cards.at("rec_type.lbl"), "portrait_splash.milo"));
      CHECK_TRUE(read("ui/resource/lists/gen/list_home_lb.milo_xbox", &source, &why));
      const auto original_leaders = source;
      CHECK_TRUE(font::PreparePortraitRecommendationLeaderboard(source, aspect, &scene, &why));
      CHECK_TRUE(source == original_leaders);
      const auto leaders = stored_objects(scene);
      CHECK_TRUE(leaders.contains("user_name") && leaders.contains("rank") && leaders.contains("mini_score"));
      CHECK_FALSE(leaders.contains("slot") || leaders.contains("UIListHighlight.arw"));
      CHECK_TRUE(contains(leaders.at("name.lbl"), "portrait_splash.milo"));
      std::vector<uint8_t> refused{1,2,3};
      source.back() ^= 1;
      CHECK_FALSE(font::PreparePortraitRecommendationLeaderboard(source, aspect, &refused, &why));
      CHECK_TRUE((refused == std::vector<uint8_t>{1,2,3}));
      source = original_list; source.back() ^= 1;
      CHECK_FALSE(font::PreparePortraitRecommendationList(source, aspect, &refused, &why));
      CHECK_TRUE((refused == std::vector<uint8_t>{1,2,3}));
      source = original; source.back() ^= 1;
      CHECK_FALSE(font::PreparePortraitRecommendations(source, portrait_buttons, header_arrows, aspect, &refused, &why));
      CHECK_TRUE((refused == std::vector<uint8_t>{1,2,3}));
    }
    CHECK_FALSE(landscape.contains(font::kRecommendationsRecord));
    CHECK_FALSE(landscape.contains(font::kPortraitRecommendationListRecord));
    CHECK_FALSE(landscape.contains(font::kPortraitRecommendationLeaderboardRecord));
    BeginCase("RhythmVerse portrait authors every panel and preserves native download states");
    config.rhythmverse_row = true;
    for (const float aspect : {.3f, 201.f / 437, .75f, .9f}) {
      config.portrait_aspect = aspect;
      screen::ConfigureMenu(config);
      settings::MenuRecords records;
      std::vector<std::string> notes;
      CHECK_TRUE(settings::BuildMenuRecords(read, {"ui/locale/eng/gen/locale_keep.dtb"},
          &records, &why, false, false, &notes, {}, false, nullptr, {}, true));
      for (const auto& note : notes) std::printf("store record note: %s\n", note.c_str());
      CHECK_TRUE(notes.empty());
      for (const char* kind : {"menu", "browser", "details", "info"}) {
        const auto path = std::string("ui/store/gen/store_") + kind + ".milo_xbox";
        CHECK_TRUE(records.contains(path));
        const auto objects = stored_objects(records.at(path));
        CHECK_TRUE(objects.contains("portrait_store.color"));
        if (std::string_view(kind) != "details") {
          CHECK_TRUE(objects.contains("portrait_tab_store.lbl"));
          const auto& heading = objects.at("portrait_tab_store.lbl");
          constexpr std::string_view font_path = "../resource/fonts/portrait_splash.milo";
          const auto font_at = std::search(heading.begin(), heading.end(), font_path.begin(), font_path.end());
          CHECK_TRUE(font_at != heading.end());
          // Hub titles share the slide parent; the standalone info page keeps
          // its own layout. Both center their ink on the marquee's anchor.
          CHECK_TRUE(object_parent(heading, 0x19) == (std::string_view(kind) == "info"
              ? scene_root(records.at(path)) : std::string("Parent.trans")));
          CHECK_EQ(u32(heading.data() + 0x19 + 36), std::bit_cast<uint32_t>(0.f));
          CHECK_EQ(u32(heading.data() + 0x19 + 44),
                   std::bit_cast<uint32_t>(rb_blitz::ui::PortraitSongs(aspect).tabs_z));
          if (std::string_view(kind) == "menu") {
            constexpr std::string_view title = "portrait_tab_store.lbl";
            const auto has_title = [&](const char* group) {
              const auto& bytes = objects.at(group);
              return std::search(bytes.begin(), bytes.end(), title.begin(), title.end()) != bytes.end();
            };
            CHECK_TRUE(has_title("all.grp"));
            CHECK_FALSE(has_title("main.grp"));
          }
        }
      }
      const auto dialog = stored_objects(records.at(font::kPortraitStoreDialogRecord));
      CHECK_TRUE(dialog.contains("error.lbl") && dialog.contains("continue.lbl"));
      CHECK_TRUE(dialog.contains("portrait_dialog_close.lbl"));
      CHECK_TRUE(dialog.contains("portrait_dialog_panel.mesh"));
      CHECK_TRUE(dialog.contains("portrait_dialog_action.mesh"));
      CHECK_TRUE(dialog.contains("portrait_dialog_close.mesh"));
      const auto modal = rb_blitz::ui::PortraitStoreDialog(aspect);
      CHECK_TRUE(modal.text_top < modal.close_z - modal.close_size / 2);
      CHECK_TRUE(modal.text_top - modal.text_height > modal.action_z + modal.action_height / 2);
      CHECK_TRUE(modal.action_z - modal.action_height / 2 > -modal.height / 2);
      CHECK_TRUE(modal.CloseX() + modal.close_size / 2 < modal.width / 2);

      // Text inherits the owning canvas fit, independently of the hidden old art.
      for (const char* name : {"error.lbl", "continue.lbl"})
        CHECK_TRUE(object_parent(dialog.at(name), 0x19) ==
                   scene_root(records.at(font::kPortraitStoreDialogRecord)));
      for (const auto& [kind, field] : {std::pair{"menu", "filter"}, {"browser", "song"}, {"info", "list"}}) {
        const auto objects = stored_objects(records.at(std::string("ui/resource/lists/gen/portrait_store_") + kind + ".milo_xbox"));
        CHECK_TRUE(objects.contains(field));
        CHECK_TRUE(objects.contains("portrait_row.mesh") && objects.contains("portrait_selected.mesh"));
        if (std::string_view(kind) == "browser") {
          // The measured native Flow includes its own directory and FlowAnimate.
          // Losing those boundaries makes the guest read texture pixels as names.
          const auto& flow = objects.at("Flow.flow");
          CHECK_EQ(flow.size(), size_t{1006});
          CHECK_EQ(u32(flow.data()), 7u);
          CHECK_EQ(u32(flow.data() + 0x193), 32u);
        }
      }
      const auto details = stored_objects(records.at("ui/store/gen/store_details.milo_xbox"));
      const auto& album = details.at("album.mesh");
      const auto layout = rb_blitz::ui::PortraitSongs(aspect);
      for (size_t i = 0; i < 4; ++i) {
        const auto parent_length = u32(album.data() + 0x15 + 105);
        const auto x = std::bit_cast<float>(u32(album.data() + 208 + parent_length + i * 36));
        const auto z = std::bit_cast<float>(u32(album.data() + 216 + parent_length + i * 36));
        CHECK_TRUE(std::abs(x - (-layout.width / 2 + 27)) <= 20.001f);
        CHECK_TRUE(std::abs(z - layout.card_z) <= 20.001f);
      }
      for (const bool info : {false, true}) {
        const auto& record = records.at(info ? "ui/store/gen/store_info.dtb" : "ui/store/gen/store.dtb");
        std::vector<dtb::Node> roots; dtb::RootHeader header;
        CHECK_TRUE(dtb::Parse(record.data(), record.size(), &roots, &header, &why));
        if (info) {
          CHECK_TRUE(header_test::Method(roots, "store_info_panel", "refresh_panel") != nullptr);
          CHECK_TRUE(header_test::Method(roots, "rbbz_download_panel", "show_popup") != nullptr);
          CHECK_TRUE(header_test::Method(roots, "rbbz_download_panel", "finish_installed") != nullptr);
          const auto* buttons = header_test::Method(roots, "store_info_panel", "refresh_buttons");
          CHECK_TRUE(buttons != nullptr);
          bool exit_row = false, selected_action = false, back_footer = false, action_footer = false;
          const auto inspect = [&](const auto& self, const dtb::Node& node) -> void {
            if (node.tag == dtb::kCommand && node.kids.size() >= 3) {
              const auto& k = node.kids;
              if (k.size() == 4 && k[0].text == "insert_elem" && k[3].text == "exit") exit_row = true;
              if (k[0].text == "buttons.lst" && k[1].text == "set_selected" && k[2].scalar == 0) selected_action = true;
              if (k.size() == 4 && k[0].text == "store_info.ihp" && k[1].text == "set_action_token") {
                if (k[2].text == "kAction_Cancel" && k[3].text == "back") back_footer = true;
                if (k[2].text == "kAction_Confirm" && k[3].kids.size() == 2 &&
                    k[3].kids[0].text == "buttons.lst" && k[3].kids[1].text == "selected_sym") action_footer = true;
              }
            }
            for (const auto& kid : node.kids) self(self, kid);
          };
          inspect(inspect, *buttons);
          CHECK_FALSE(exit_row);
          CHECK_TRUE(selected_action && back_footer && action_footer);
          const auto* deleting = header_test::Method(roots, "rbbz_library_panel", "confirm_delete");
          CHECK_TRUE(deleting != nullptr);
          const auto visibility = [&](const auto& self, const dtb::Node& node, const char* label) -> int {
            int result = -1;
            if (node.tag == dtb::kCommand && node.kids.size() == 3 &&
                node.kids[0].text == label && node.kids[1].text == "set_showing")
              result = node.kids[2].text == "TRUE" ? 1 : 0;
            for (const auto& kid : node.kids) {
              const auto next = self(self, kid, label);
              if (next >= 0) result = next;
            }
            return result;
          };
          CHECK_EQ(visibility(visibility, *deleting, "continue.lbl"), 0);
          CHECK_EQ(visibility(visibility, *deleting, "portrait_dialog_close.lbl"), 0);
          const auto* installed = header_test::Method(roots, "rbbz_download_panel", "finish_installed");
          CHECK_EQ(visibility(visibility, *installed, "continue.lbl"), 1);
          CHECK_EQ(visibility(visibility, *installed, "portrait_dialog_close.lbl"), 1);

          for (const char* panel : {"rbbz_download_panel", "rbbz_library_panel"}) {
            bool found = false;
            for (const auto& root : roots) if (root.kids.size() > 2 && root.kids[2].text == panel)
              for (const auto& member : root.kids)
                if (member.kids.size() == 2 && member.kids[0].text == "file") {
                  CHECK_TRUE(member.kids[1].text == "../error/portrait_store_dialog.milo"); found = true;
                }
            CHECK_TRUE(found);
          }
        } else {
          const auto* refresh = header_test::Method(roots, "store_browser_panel", "refresh_list");
          CHECK_TRUE(refresh != nullptr);
          CHECK_TRUE(header_test::Method(roots, "store_details_panel", "refresh") != nullptr);
        }
      }
      std::vector<uint8_t> source, refused{1,2,3};
      CHECK_TRUE(read("ui/store/gen/store_browser.milo_xbox", &source, &why));
      source.back() ^= 1;
      CHECK_FALSE(font::PreparePortraitStore(source, portrait_buttons, "browser", aspect, &refused, &why));
      CHECK_TRUE((refused == std::vector<uint8_t>{1,2,3}));
    }
    config.rhythmverse_row = false;
    config.portrait_aspect = 9.f / 16;
    screen::ConfigureMenu(config);
    const auto settings_objects = stored_objects(settings_scene);
    BeginCase("portrait powerups keep native equip and launch handlers with three visible choices");
    {
      namespace font = rb_blitz::ui::input_font;
      std::vector<dtb::Node> before, after;
      const auto& original = landscape.at("ui/powerup_select/gen/powerup_select.dtb");
      const auto& changed = portrait.at("ui/powerup_select/gen/powerup_select.dtb");
      CHECK_TRUE(dtb::Parse(original.data(), original.size(), &before, nullptr, &why));
      CHECK_TRUE(dtb::Parse(changed.data(), changed.size(), &after, nullptr, &why));
      for (const char* method : {"go_to_powerup_list", "update_powerup_desc", "powerups_chosen"}) {
        const auto* a = header_test::Method(before, "powerup_select_panel", method);
        const auto* b = header_test::Method(after, "powerup_select_panel", method);
        CHECK_TRUE(a && b);
        if (a && b) CHECK_TRUE(dtb::Compile({*a}) == dtb::Compile({*b}));
      }
      for (const char* method : {"SELECT_MSG", "SCROLL_MSG"}) {
        const auto* a = header_test::Method(before, "powerup_select_panel", method);
        const auto* b = header_test::Method(after, "powerup_select_panel", method);
        CHECK_TRUE(a && b && b->kids.size() == 2 && b->kids[1].kids.size() == 4);
        if (a && b && b->kids.size() == 2 && b->kids[1].kids.size() == 4) {
          const auto& guard = b->kids[1];
          CHECK_TRUE(guard.kids[0].text == "if_else");
          CHECK_EQ(guard.kids[2].scalar, 0u); // Reading blocks equip and scrolling.
          auto restored = *b;
          restored.kids.resize(1);
          restored.kids.insert(restored.kids.end(), guard.kids[3].kids.begin() + 1, guard.kids[3].kids.end());
          CHECK_TRUE(dtb::Compile({*a}) == dtb::Compile({restored}));
        }
      }
      const auto* open = header_test::Method(after, "powerup_select_panel", "rbbz_open_powerup_info");
      const auto* close = header_test::Method(after, "powerup_select_panel", "rbbz_close_powerup_info");
      CHECK_TRUE(open && close && header_test::Has(*open, "update_powerup_desc"));
      CHECK_TRUE(open && !header_test::Has(*open, "SELECT_MSG") && !header_test::Has(*open, "update_equipped_powerups"));
      const auto* buttons = header_test::Method(after, "powerup_select_panel", "BUTTON_DOWN_MSG");
      CHECK_TRUE(buttons && header_test::Has(*buttons, "kAction_Start"));
      if (buttons) {
        const auto* start = header_test::Case(*buttons, "kAction_Start");
        CHECK_TRUE(start && header_test::Has(*start, "song_preview") && header_test::Has(*start, "powerups_chosen"));
        const auto* select = header_test::Method(before, "powerup_select_panel", "SELECT_MSG");
        const auto* launch = select ? header_test::Case(*select, "play_song") : nullptr;
        CHECK_TRUE(start && launch);
        if (start && launch) {
          auto expected = *launch; expected.kids[0] = dtb::Node::Symbol("kAction_Start");
          CHECK_TRUE(dtb::Compile({*start}) == dtb::Compile({expected}));
        }
      }
      const auto* enter = header_test::Method(after, "powerup_select_panel", "enter");
      CHECK_TRUE(enter != nullptr);
      size_t counts = 0;
      if (enter) for (const auto& cmd : enter->kids)
        if (cmd.kids.size() == 4 && cmd.kids[2].text == "display_num") {
          ++counts; CHECK_EQ(cmd.kids[3].scalar, 3u);
        }
      CHECK_EQ(counts, size_t{3});
      const auto objects = stored_objects(portrait.at(font::kPowerupSelectRecord));
      for (const char* name : {"portrait_song_card.mesh", "portrait_powerup_details.mesh",
                               "portrait_powerups.color", "help.ihp", "difficulty", "header.lbl",
                               "portrait_powerup_info.grp", "portrait_info_help.ihp",
                               "portrait_powerup_info_0.lbl", "portrait_powerup_info_2.mesh"})
        CHECK_TRUE(objects.contains(name));
      const auto contains = [](const auto& bytes, std::string_view text) {
        return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
      };
      CHECK_TRUE(contains(objects.at("help.ihp"), "portrait_actions.milo"));
      CHECK_TRUE(contains(objects.at("help.ihp"), "powerup_sel__head_play"));
      CHECK_EQ(u32(objects.at("portrait_info_help.ihp").data() + 9), 1u);
      CHECK_EQ(u32(objects.at("portrait_info_help.ihp").data() + 13), 2u);
      CHECK_TRUE(contains(objects.at("portrait_info_help.ihp"), "settings_close"));
      CHECK_EQ(object_showing(objects.at("portrait_powerup_info.grp"), 0x21), uint8_t{0});
      CHECK_TRUE(contains(objects.at("active_powerups.lst"), "portrait_powerup_choices.milo"));
      CHECK_TRUE(contains(objects.at("powerup_select.lst"), "portrait_powerup_categories.milo"));
      CHECK_EQ(u32(objects.at("help.ihp").data() + 9), 2u);
      CHECK_EQ(u32(objects.at("help.ihp").data() + 13), 4u); // native Start, localized Play Song
      CHECK_TRUE(contains(objects.at("header.lbl"), "powerup_selection_title"));
      CHECK_TRUE(contains(objects.at("header.lbl"), "portrait_powerup_ink.color"));
      CHECK_TRUE(contains(objects.at("selection_title.lbl"), "portrait_powerup_ink.color"));
      CHECK_TRUE(contains(objects.at("selection_artist.lbl"), "portrait_powerup_ink.color"));
      for (size_t channel = 0; channel < 4; ++channel)
        CHECK_EQ(u32(objects.at("portrait_powerup_ink.color").data() + 17 + channel * 4),
                 std::bit_cast<uint32_t>(1.f));
      const auto choices = stored_objects(portrait.at(font::kPortraitPowerupChoicesRecord));
      CHECK_TRUE(choices.contains("back_arrow") && choices.contains("new_arrow"));
      CHECK_TRUE(choices.contains("arrow_lt.mesh") && choices.contains("arrow_rt.mesh"));
      CHECK_FALSE(landscape.contains(font::kPortraitPowerupCategoriesRecord));
      CHECK_FALSE(landscape.contains(font::kPortraitPowerupChoicesRecord));
      // UIListDir's spacing precedes embedded flow directories. Changing a
      // nested flow's tail instead corrupts the resource despite valid names.
      for (const auto& [path, spacing] : {
          std::pair{font::kPortraitPowerupCategoriesRecord, 120.f},
          std::pair{font::kPortraitPowerupChoicesRecord, 64.f}}) {
        const auto& packed = portrait.at(path);
        const auto* bytes = packed.data() + 0x810;
        size_t at = 4;
        const auto skip_string = [&] { at += 4 + u32(bytes + at); };
        skip_string(); skip_string(); at += 9;
        const auto count = u32(bytes + at); at += 4;
        for (uint32_t i = 0; i < count; ++i) { skip_string(); skip_string(); }
        while (u32(bytes + at) != 0xADDEADDE) ++at;
        CHECK_EQ(u32(bytes + at + 4 - 29), std::bit_cast<uint32_t>(spacing));
      }
      std::vector<uint8_t> source, scene;
      CHECK_TRUE(read(font::kPowerupSelectRecord, &source, &why));
      for (const float aspect : {201.f / 437, 9.f / 16, .75f}) {
        CHECK_TRUE(font::PreparePortraitPowerups(source, portrait_buttons, aspect, &scene, &why));
        const auto fitted = stored_objects(scene);
        const auto& name = fitted.at("name.lbl");
        CHECK_EQ(u32(name.data() + 25 + 44), std::bit_cast<uint32_t>(rb_blitz::ui::PortraitPowerups(aspect).name_z));
      }
      source[0] ^= 1;
      CHECK_FALSE(font::PreparePortraitPowerups(source, portrait_buttons, config.portrait_aspect, &scene, &why));
    }
    BeginCase("portrait songs retain native providers, a modal shortcut list and separate search rows");
    {
      namespace font = rb_blitz::ui::input_font;
      const auto objects = stored_objects(portrait.at(font::kSongSelectRecord));
      for (const char* name : {"songs.lst", "shortcuts.lst", "portrait_sort.mesh",
                               "portrait_search.mesh", "portrait_song_title.lbl",
                               "portrait_header_next.mesh",
                               "portrait_song_card.mesh", "shortcuts.grp"})
        CHECK_TRUE(objects.contains(name));
      CHECK_FALSE(objects.contains("scroll_arrow.tex"));
      CHECK_TRUE(stored_objects(portrait.at(font::kPortraitShortcutsRecord)).contains("list.lbl"));
      CHECK_FALSE(landscape.contains(font::kPortraitShortcutsRecord));
      const auto rows = stored_objects(portrait.at(font::kPortraitSongListRecord));
      for (const char* name : {"song", "filter", "star_1", "star_5", "portrait_row.mesh", "portrait_selected.mesh"})
        CHECK_TRUE(rows.contains(name));
      CHECK_FALSE(rows.contains("highlight.arw"));
      CHECK_TRUE(std::bit_cast<float>(u32(rows.at("star_1").data() + 25)) >
                 std::bit_cast<float>(u32(rows.at("song").data() + 25)));
      {
        const auto& label = rows.at("song.lbl");
        constexpr std::string_view prefix = "../fonts/";
        const auto font = std::search(label.begin(), label.end(), prefix.begin(), prefix.end());
        CHECK_TRUE(font != label.end());
        const auto at = size_t(font - label.begin());
        // Leading is a multiplier, independent of the taller row's height.
        CHECK_EQ(u32(label.data() + at - 29), std::bit_cast<uint32_t>(1.f));
        CHECK_EQ(u32(label.data() + at - 41), 0u);
      }
      for (const char* name : {"portrait_row.mesh", "portrait_selected.mesh", "portrait_header.mesh"}) {
        const auto& mesh = rows.at(name);
        const auto value = [&](size_t offset) { return std::bit_cast<float>(u32(mesh.data() + 0x15 + offset)); };
        // The button's XY quad must face the list's XZ camera plane.
        CHECK_TRUE(std::abs(300 * value(0) - 420) < .001f);
        CHECK_TRUE(std::abs(30 * value(20) - (std::string_view(name) == "portrait_header.mesh" ? 30 : rb_blitz::ui::kPortraitSongRowHeight)) < .001f);
        CHECK_TRUE(std::abs(value(16)) < .001f && std::abs(value(32)) < .001f);
      }
      CHECK_TRUE(stored_objects(portrait.at(font::kPortraitSongSearchRecord)).contains("list"));
      CHECK_FALSE(landscape.contains(font::kPortraitSongListRecord));
      CHECK_FALSE(landscape.contains(font::kPortraitSongSearchRecord));
      std::vector<dtb::Node> song_roots;
      dtb::RootHeader song_header;
      const auto& record = portrait.at("ui/song_select/gen/song_select.dtb");
      CHECK_TRUE(dtb::Parse(record.data(), record.size(), &song_roots, &song_header, &why));
      for (const char* name : {"popup_neon"}) {
        bool found = false;
        for (const auto& root : song_roots) found |= header_test::Has(root, name);
        CHECK_TRUE(found);
      }
      std::vector<dtb::Node> landscape_song_roots;
      const auto& landscape_record = landscape.at("ui/song_select/gen/song_select.dtb");
      CHECK_TRUE(dtb::Parse(landscape_record.data(), landscape_record.size(), &landscape_song_roots, nullptr, &why));
      for (const char* method : {"BUTTON_DOWN_MSG", "poll"}) {
        const auto* before = header_test::Find(landscape_song_roots.front(), dtb::kArray, method);
        const auto* after = header_test::Find(song_roots.front(), dtb::kArray, method);
        CHECK_TRUE(before && after);
        CHECK_TRUE(dtb::Compile({*before}) == dtb::Compile({*after}));
      }
      for (const char* method : {"show_shortcuts", "rbbz_portrait_layout"}) {
        bool found = false;
        for (const auto& root : song_roots) found |= header_test::Find(root, dtb::kArray, method) != nullptr;
        CHECK_TRUE(found);
      }
      CHECK_TRUE(settings::AddPortraitSongsLayout(&song_roots, 9.f / 16, &why));
      CHECK_TRUE(dtb::Compile(song_roots, song_header) == record);
      std::vector<dtb::Node> unknown{dtb::Node::Array({dtb::Node::Symbol("other")})};
      const auto original = dtb::Compile(unknown);
      CHECK_FALSE(settings::AddPortraitSongsLayout(&unknown, .5f, &why));
      CHECK_TRUE(dtb::Compile(unknown) == original);
    }
    BeginCase("the persistent portrait background owns arrows across page exits and native press Flows");
    {
      std::vector<dtb::Node> roots;
      const auto& background = portrait.at("ui/background/gen/background.dtb");
      CHECK_TRUE(dtb::Parse(background.data(), background.size(), &roots, nullptr, &why));
      const auto* enter = header_test::Method(roots, "background_panel", "enter");
      CHECK_TRUE(enter != nullptr);
      for (const char* arrow : {"Left_arrow.grp", "Rt_arrow.grp"}) {
        bool placed = false, scaled = false;
        if (enter) for (const auto& command : enter->kids) {
          if (command.kids.size() != 5 || !header_test::Has(command.kids[0], arrow)) continue;
          if (command.kids[1].text == "set_local_pos") {
            placed = true;
            CHECK_EQ(command.kids[4].scalar, std::bit_cast<uint32_t>(rb_blitz::ui::kPortraitHubTitleCenter));
          }
          if (command.kids[1].text == "set_local_scale") scaled = true;
        }
        CHECK_TRUE(placed && scaled);
      }
      for (const auto& [path, panels] : std::array<std::pair<const char*, std::vector<const char*>>, 4>{{
          {"ui/song_select/gen/song_select.dtb", {"song_select_panel"}},
          {"ui/store/gen/store.dtb", {"store_menu_panel", "store_browser_panel"}},
          {"ui/inbox/gen/inbox.dtb", {"inbox_panel"}},
          {"ui/recommendations/gen/recommendations.dtb", {"recommendation_panel"}}}}) {
        const auto& record = portrait.at(path);
        CHECK_TRUE(dtb::Parse(record.data(), record.size(), &roots, nullptr, &why));
        for (const auto* panel : panels) {
          const auto* exit = header_test::Method(roots, panel, "exit");
          if (exit) {
            CHECK_FALSE(header_test::Has(*exit, "Left_arrow.grp"));
            CHECK_FALSE(header_test::Has(*exit, "Rt_arrow.grp"));
          }
          const auto* buttons = header_test::Method(roots, panel, "BUTTON_DOWN_MSG");
          const auto* helper = header_test::Method(roots, panel, "go_to_song_select");
          CHECK_TRUE(buttons && header_test::Has(*buttons, "arrow_lt.flow"));
          CHECK_TRUE(buttons && (header_test::Has(*buttons, "arrow_rt.flow") ||
              (header_test::Has(*buttons, "go_to_song_select") && helper &&
               header_test::Has(*helper, "arrow_rt.flow"))));
        }
      }
    }
    BeginCase("portrait native scenes prepare independently at phone and tablet aspects");
    for (float aspect : {.3f, 402.f / 874, 9.f / 16, .75f, .9f}) {
      std::vector<uint8_t> source, fitted;
      CHECK_TRUE(read("ui/av_options/gen/av_options.milo_xbox", &source, &why));
      CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitAudio(source, portrait_buttons, aspect, &fitted, &why));
      auto objects = stored_objects(fitted);
      const auto& wheel = objects.at("sound_effects_slider.sld");
      const float scale = std::bit_cast<float>(u32(wheel.data() + 0x20));
      CHECK_TRUE(scale > 0 && 190 * scale <= rb_blitz::ui::PortraitSplash(aspect).width * .85f);
      CHECK_TRUE(rb_blitz::ui::input_font::PrepareRenderScaleScene(source, portrait_buttons,
                                                                 aspect, true, &fitted, &why));
      objects = stored_objects(fitted);
      CHECK_TRUE(objects.contains("scale_value.lbl"));
      CHECK_TRUE(objects.contains("checkbox.lst"));
      CHECK_EQ(u32(objects.at("nav.ihp").data() + 9), uint32_t(2));
      for (const auto& [slider, z] : {std::pair{"sound_effects_slider.sld", -100.f},
                                     {"track_sound_slider.sld", -100.f}}) {
        const auto& object = objects.at(slider);
        CHECK_TRUE(std::abs(std::bit_cast<float>(u32(object.data() + 0x20 + 44)) - z) < .001f);
      }
      CHECK_TRUE(read("ui/credits/gen/credits.milo_xbox", &source, &why));
      CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitCredits(source, portrait_buttons, aspect, &fitted, &why));
      CHECK_TRUE(read("ui/resource/lists/gen/list_credits.milo_xbox", &source, &why));
      CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitCreditsList(source, aspect, &fitted, &why));
      CHECK_TRUE(read(rb_blitz::ui::input_font::kConnectionRecord, &source, &why));
      CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitConnection(source, portrait_buttons, aspect, &fitted, &why));
      objects = stored_objects(fitted);
      const auto& status = objects.at("status.lbl");
      constexpr std::string_view font = "../resource/fonts/portrait_splash.milo";
      const auto style = std::search(status.begin(), status.end(), font.begin(), font.end());
      CHECK_TRUE(style != status.end());
      if (style != status.end()) {
        const auto color = size_t(style - status.begin()) + font.size();
        const float size = std::bit_cast<float>(u32(status.data() + color + 4 + u32(status.data() + color)));
        CHECK_EQ(size, rb_blitz::ui::PortraitLoading(aspect).text_size);
      }
    }
    BeginCase("portrait audio, render wheels and credits retain their native widgets and shared footer");
    for (const char* path : {"ui/av_options/gen/av_options.milo_xbox",
                             "ui/av_options/gen/settings_scale.milo_xbox",
                             "ui/av_options/gen/settings_scale_pause.milo_xbox",
                             "ui/credits/gen/credits.milo_xbox"}) {
      CHECK_TRUE(portrait.contains(path));
      const auto objects = stored_objects(portrait.at(path));
      CHECK_TRUE(objects.contains("portrait_back.mesh"));
      CHECK_TRUE(objects.contains("portrait_confirm.mesh"));
      if (std::string_view(path).find("credits") == std::string_view::npos) {
        CHECK_TRUE(objects.contains("sound_effects_slider.sld"));
        CHECK_TRUE(objects.contains("track_sound_slider.sld"));
        CHECK_TRUE(object_parent(objects.at("sound_effects_slider.sld"), 0x20) == scene_root(portrait.at(path)));
      } else CHECK_TRUE(objects.contains("credits.lst"));
    }
    CHECK_TRUE(settings_objects.contains("portrait_back.mesh"));
    CHECK_TRUE(settings_objects.contains("portrait_back_border.mesh"));
    CHECK_EQ(settings_objects.at("portrait_back.mat")[0x29], uint8_t(0));
    CHECK_EQ(u32(settings_objects.at("portrait_back.mat").data() + 0x15), uint32_t(3));
    CHECK_EQ(u32(settings_objects.at("portrait_back_border.mat").data() + 0x15), uint32_t(3));
    CHECK_EQ(object_showing(settings_objects.at("portrait_back_surface.grp"), 0x21), uint8_t(0));
    CHECK_EQ(group_members(settings_objects.at("portrait_back_surface.grp")), uint32_t(2));
    const auto opacity = std::bit_cast<float>(u32(settings_objects.at("portrait_back.mat").data() + 0x25));
    CHECK_TRUE(std::abs(opacity - .67f * .8f) < .0001f);
    CHECK_TRUE(std::abs(std::bit_cast<float>(u32(settings_objects.at("portrait_back_border.mat").data() + 0x25)) - .19f * .8f) < .0001f);
    CHECK_TRUE(std::abs(std::bit_cast<float>(u32(settings_objects.at("portrait_back.mat").data() + 0x19)) - .063f) < .0001f);
    CHECK_TRUE(std::abs(std::bit_cast<float>(u32(settings_objects.at("portrait_back.mat").data() + 0x1D)) - .051f) < .0001f);
    CHECK_TRUE(std::abs(std::bit_cast<float>(u32(settings_objects.at("portrait_back.mat").data() + 0x21)) - .11f) < .0001f);
    const auto menu_buttons = stored_objects(portrait.at(rb_blitz::ui::input_font::kSplashListRecord));
    for (const auto& [footer, menu] : {std::pair{"portrait_back.mat", "portrait_button.mat"},
                                      {"portrait_back_border.mat", "portrait_border.mat"}}) {
      for (size_t channel = 0; channel < 4; ++channel) {
        const auto read_color = [&](const auto& object) {
          return std::bit_cast<float>(u32(object.data() + 0x19 + channel * 4));
        };
        CHECK_TRUE(std::abs(read_color(settings_objects.at(footer)) -
            read_color(menu_buttons.at(menu)) * (channel == 3 ? .8f : 1.f)) < .0001f);
      }
    }
    CHECK_EQ(u32(settings_objects.at("InlineHelp.ihp").data() + 9), uint32_t(1));
    CHECK_EQ(u32(settings_objects.at("InlineHelp.ihp").data() + 13), uint32_t(2));
    CHECK_EQ(std::bit_cast<float>(u32(settings_objects.at("header.lbl").data() + 0x19)), 1.f);
    CHECK_EQ(std::bit_cast<float>(u32(settings_objects.at("header.lbl").data() + 0x19 + 44)), rb_blitz::ui::kPortraitTitleCenter);
    for (const char* path : {"ui/calibration/gen/cal_welcome.milo_xbox",
                             "ui/calibration/gen/cal_auto.milo_xbox",
                             "ui/calibration/gen/cal_manual.milo_xbox",
                             "ui/calibration/gen/cal_complete.milo_xbox",
                             "ui/controller_config/gen/controller_config.milo_xbox"}) {
      auto objects = stored_objects(portrait.at(path));
      CHECK_TRUE(objects.contains("portrait_back.mesh"));
      CHECK_TRUE(objects.contains("portrait_confirm.mesh"));
      CHECK_EQ(objects.contains("portrait_calibration_footer.color"),
               std::string_view(path).find("/calibration/") != std::string_view::npos);
      CHECK_EQ(objects.at("portrait_back.mat")[0x29], uint8_t(0));
      CHECK_EQ(objects.at("portrait_confirm.mat")[0x29], uint8_t(0));
      CHECK_EQ(std::bit_cast<float>(u32(objects.at("header.lbl").data() + 0x19)), 1.f);
    }
    const auto welcome = stored_objects(portrait.at("ui/calibration/gen/cal_welcome.milo_xbox"));
    CHECK_EQ(std::bit_cast<float>(u32(welcome.at("header.lbl").data() + 0x19 + 44)), rb_blitz::ui::kPortraitTitleCenter);
    CHECK_EQ(std::bit_cast<float>(u32(welcome.at("cal_mode.lst").data() + 0x19 + 44)), 77.f);
    CHECK_TRUE(std::bit_cast<float>(u32(welcome.at("cal_mode.lst").data() + 0x19)) ==
               rb_blitz::ui::PortraitSplash(config.portrait_aspect).list_scale);
    CHECK_EQ(object_showing(welcome.at("portrait_back_surface.grp"), 0x21), uint8_t(0));
    CHECK_FALSE(welcome.contains("portrait_back.ihp"));
    const auto credits = stored_objects(portrait.at("ui/credits/gen/credits.milo_xbox"));
    CHECK_EQ(u32(credits.at("nav.ihp").data() + 9), uint32_t(2));
    CHECK_FALSE(credits.contains("portrait_back.ihp"));
    BeginCase("calibration list keeps the provider widget and wizard keeps its changing native footer");
    const auto calibration_list = stored_objects(portrait.at(rb_blitz::ui::input_font::kPortraitCalibrationListRecord));
    CHECK_TRUE(calibration_list.contains("name"));
    CHECK_FALSE(calibration_list.contains("song"));
    const auto wizard = stored_objects(portrait.at("ui/calibration/gen/cal_auto.milo_xbox"));
    CHECK_TRUE(std::abs(std::bit_cast<float>(u32(wizard.at("cal_metronome.grp").data() + 0x21)) * 599.163f -
                        rb_blitz::ui::PortraitSplash(config.portrait_aspect).width * .64f) < .001f);
    const auto z = [&](const char* name) {
      return std::bit_cast<float>(u32(wizard.at(name).data() + 0x19 + 44));
    };
    CHECK_TRUE(z("cal_reps.lbl") > z("cal_instructions.lbl") + 20);
    CHECK_TRUE(z("cal_instructions.lbl") > z("cal_setting.lbl") + 20);
    CHECK_TRUE(z("cal_video_instructions.lbl") == rb_blitz::ui::kPortraitCalibrationInstructionsZ);
    BeginCase("portrait calibration hides its meter and progress on success and restores retry defaults");
    {
      const auto& record = portrait.at("ui/calibration/gen/calibration.dtb");
      CHECK_TRUE(record != landscape.at("ui/calibration/gen/calibration.dtb"));
      std::vector<dtb::Node> roots;
      dtb::RootHeader header;
      CHECK_TRUE(dtb::Parse(record.data(), record.size(), &roots, &header, &why));
      int checked_states = 0;
      const auto check_states = [&](const auto& self, const dtb::Node& node) -> void {
        if (node.tag == dtb::kArray && !node.kids.empty() && node.kids[0].text == "set_state") {
          for (const char* state : {"init", "success"}) {
            const auto* branch = header_test::Find(node, dtb::kArray, state);
            CHECK_TRUE(branch != nullptr);
            if (!branch) continue;
            for (const char* graphic : rb_blitz::ui::kPortraitCalibrationGraphics) {
              const bool show = std::string_view(state) == "init" &&
                  std::string_view(graphic) != "audio_anim.grp" && std::string_view(graphic) != "visuals_anim.grp";
              CHECK_TRUE(std::any_of(branch->kids.begin(), branch->kids.end(), [&](const auto& command) {
                return command.tag == dtb::kCommand && command.kids.size() == 3 &&
                    command.kids[0].text == graphic && command.kids[1].text == "set_showing" &&
                    command.kids[2].text == (show ? "TRUE" : "FALSE");
              }));
            }
          }
          ++checked_states;
        }
        for (const auto& child : node.kids) self(self, child);
      };
      for (const auto& root : roots) check_states(check_states, root);
      CHECK_EQ(checked_states, 1);
      CHECK_TRUE(settings::PreparePortraitCalibrationFlow(&roots, &why));
      CHECK_TRUE(dtb::Compile(roots, header) == record);
      std::vector<dtb::Node> invalid{dtb::Node::Array({dtb::Node::Symbol("different_state")})};
      const auto original = dtb::Compile(invalid);
      CHECK_FALSE(settings::PreparePortraitCalibrationFlow(&invalid, &why));
      CHECK_TRUE(dtb::Compile(invalid) == original);
    }
    CHECK_EQ(u32(wizard.at("nav.ihp").data() + 9), uint32_t(2));
    CHECK_FALSE(wizard.contains("portrait_back.ihp"));
    CHECK_TRUE(wizard.contains("portrait_retry.mesh"));
    for (const auto& [name, minimum] : {std::pair{"audio_anim.grp", .5f}, {"prog_bar.grp", 1.f},
                                      {"visuals_anim.grp", .3f}, {"cal_metronome.grp", .2f}}) {
      const auto& data = wizard.at(name);
      CHECK_TRUE(std::bit_cast<float>(u32(data.data() + 0x21)) >= minimum);
      CHECK_TRUE(object_parent(data, 0x21) == scene_root(portrait.at("ui/calibration/gen/cal_auto.milo_xbox"))); // owning canvas, native draw group retained
    }
    CHECK_TRUE(std::abs(std::bit_cast<float>(u32(wizard.at("cal_metronome.grp").data() + 0x21 + 20))) > .2f);
    for (const char* name : {"button_press_xbox.mesh", "button_press_ps3.mesh"}) {
      const auto& button = wizard.at(name);
      for (size_t matrix : {size_t{0x15}, size_t{0x45}}) {
        const auto field = [&](size_t offset) {
          return std::bit_cast<float>(u32(button.data() + matrix + offset));
        };
        CHECK_TRUE(std::abs(field(36) - rb_blitz::ui::PortraitSplash(config.portrait_aspect).width * .42f) < .001f);
        CHECK_TRUE(std::abs(field(44) - rb_blitz::ui::kPortraitCalibrationMeterZ) < .001f);
        const float previous = 1.5573348f * rb_blitz::ui::PortraitSplash(config.portrait_aspect).width * .65f / 511.003f;
        CHECK_TRUE(std::abs(field(0) / previous - .5f) < .001f);
        CHECK_TRUE(std::abs(field(20) - field(0)) < .001f); // retain the quad's XY-to-XZ rotation
      }
      CHECK_TRUE(object_parent(button, 0x15) == scene_root(portrait.at("ui/calibration/gen/cal_auto.milo_xbox")));
    }
    const auto failed_scene = stored_objects(portrait.at("ui/calibration/gen/cal_failed.milo_xbox"));
    CHECK_EQ(std::bit_cast<float>(u32(failed_scene.at("failed.lbl").data() + 0x19 + 36)), 0.f);
    CHECK_TRUE(std::bit_cast<float>(u32(failed_scene.at("failed.lbl").data() + 0x19 + 44)) == -112.f);
    const auto& failed_footer = failed_scene.at("InlineHelp.ihp");
    CHECK_EQ(u32(failed_footer.data() + 9), uint32_t{1});
    const std::string_view retry_caption(reinterpret_cast<const char*>(failed_footer.data() + 21),
                                          u32(failed_footer.data() + 17));
    CHECK_TRUE(rb_blitz::input::CalibrationRetryCaption(retry_caption));
    const auto retry_placement = rb_blitz::input::CalibrationFooterPlacement(true, retry_caption, 0, 1);
    const auto retry_slot = rb_blitz::ui::PortraitFooterAction(config.portrait_aspect,
        retry_placement.index, retry_placement.count);
    const auto lower_slot = rb_blitz::ui::PortraitFooterAction(config.portrait_aspect, 0, 2);
    CHECK_TRUE(retry_slot.z - retry_slot.height / 2 > lower_slot.z + lower_slot.height / 2);
    const auto& speaker = wizard.at("audio_anim.grp");
    const auto speaker_field = [&](size_t field) {
      return std::bit_cast<float>(u32(speaker.data() + 0x21 + field));
    };
    CHECK_TRUE(std::abs(speaker_field(36) + speaker_field(0) * 223.3656f) < .001f);
    CHECK_TRUE(std::abs(speaker_field(44) + speaker_field(32) * 39.78075f - rb_blitz::ui::kPortraitCalibrationMeterZ) < .001f);
    const auto presets = stored_objects(portrait.at("ui/controller_config/gen/controller_config.milo_xbox"));
    CHECK_TRUE(presets.contains("portrait_previous.mesh"));
    CHECK_TRUE(presets.contains("portrait_next.mesh"));
    CHECK_EQ(object_showing(presets.at("portrait_preset_arrows.grp"), 0x21), uint8_t(0));
    CHECK_EQ(group_members(presets.at("portrait_preset_arrows.grp")), uint32_t(4));
    CHECK_EQ(object_showing(presets.at("portrait_previous_surface.grp"), 0x21), uint8_t(0));
    CHECK_EQ(object_showing(presets.at("portrait_next_surface.grp"), 0x21), uint8_t(0));
    for (const char* name : {"arrow_lt.mesh", "arrow_rt.mesh", "arrow_lt_glow.mesh", "arrow_rt_glow.mesh"}) {
      const auto& group = presets.at("all.grp");
      CHECK_TRUE(std::search(group.begin(), group.end(), name, name + std::strlen(name)) == group.end());
      const float scale = std::bit_cast<float>(u32(presets.at(name).data() + 0x15));
      CHECK_TRUE(scale * 64 <= 38 - 8); // Complete quad plus padding inside the touch target.
    }
    CHECK_TRUE(rb_blitz::ui::PortraitSplashScreen("settings_screen", "settings_panel",
                                                screen::PanelScene("settings_panel")));
    CHECK_TRUE(art.size() > 0x810);
    BeginCase("shared portrait prompts preserve native shade and animation timing across aliases");
    const auto& confirmation = portrait.at("ui/pause/gen/confirmation_message.milo_xbox");
    CHECK_TRUE(portrait.at("ui/pause/gen/settings_confirmation.milo_xbox") ==
               portrait.at("ui/pause/gen/settings_confirmation_pause.milo_xbox"));
    const auto generic_prompt = stored_objects(confirmation);
    const auto& prompt_animation = generic_prompt.at("on.anim");
    const float prompt_scale = rb_blitz::ui::PortraitPromptScale(config.portrait_aspect);
    CHECK_EQ(std::bit_cast<float>(u32(generic_prompt.at("Group.grp").data() + 0x21)), prompt_scale);
    CHECK_EQ(std::bit_cast<float>(u32(prompt_animation.data() + 0xAF)), .5f * prompt_scale);
    CHECK_EQ(std::bit_cast<float>(u32(prompt_animation.data() + 0xCF)), prompt_scale);
    CHECK_EQ(std::bit_cast<float>(u32(prompt_animation.data() + 0xBB)), 0.f);
    CHECK_EQ(std::bit_cast<float>(u32(prompt_animation.data() + 0xDB)), 10.f);
    CHECK_EQ(std::bit_cast<float>(u32(generic_prompt.at("Shade.mesh").data() + 0x15)), 1.f); // unscaled dimmer
    BeginCase("portrait error dialogs retain the native scene and dispatch its exact Confirm branch");
    {
      namespace modal = rb_blitz::ui::portrait_dialog;
      CHECK_TRUE(portrait.at(font::kErrorReturnRecord) == landscape.at(font::kErrorReturnRecord));
      std::vector<uint8_t> source;
      CHECK_TRUE(read("ui/error/gen/error_general.dtb", &source, &why));
      std::vector<dtb::Node> original, bridged;
      CHECK_TRUE(dtb::Parse(source.data(), source.size(), &original, nullptr, &why));
      const auto& record = portrait.at("ui/error/gen/error_general.dtb");
      CHECK_TRUE(dtb::Parse(record.data(), record.size(), &bridged, nullptr, &why));
      const auto* before = header_test::Method(original, "error_popup_panel", "BUTTON_DOWN_MSG");
      const auto* after = header_test::Method(bridged, "error_popup_panel", "BUTTON_DOWN_MSG");
      CHECK_TRUE(before && after);
      CHECK_TRUE(dtb::SerializeNodes({*before}) == dtb::SerializeNodes({*after}));
      const auto* poll = header_test::Method(bridged, "error_popup_panel", "poll");
      CHECK_TRUE(poll && poll->kids.size() == 2);
      const auto& dispatch = poll->kids.at(1);
      CHECK_TRUE(dispatch.kids.at(1).kids.at(1).text == "rbbz-portrait-dialog/confirm");
      const auto* native = header_test::Find(*before, dtb::kArray, "kAction_Confirm");
      CHECK_TRUE(native != nullptr);
      CHECK_TRUE(dtb::SerializeNodes(dispatch.kids.at(2).kids).size() > 0);
      std::vector<dtb::Node> commands(native->kids.begin() + 1, native->kids.end());
      std::vector<dtb::Node> copied(dispatch.kids.at(2).kids.begin() + 1, dispatch.kids.at(2).kids.end());
      CHECK_TRUE(dtb::SerializeNodes(commands) == dtb::SerializeNodes(copied));
      for (const auto& [method, suffix] : {std::pair{"show_popup", "open"}, {"hide_popup", "close"}}) {
        const auto* native_method = header_test::Method(original, "error_popup_panel", method);
        const auto* host_method = header_test::Method(bridged, "error_popup_panel", method);
        CHECK_EQ(host_method->kids.size(), native_method->kids.size() + 1);
        auto prefix = host_method->kids; prefix.pop_back();
        CHECK_TRUE(dtb::SerializeNodes(prefix) == dtb::SerializeNodes(native_method->kids));
        CHECK_TRUE(host_method->kids.back().kids.at(1).text == std::string("rbbz-portrait-dialog/") + suffix);
      }
      auto incompatible = original;
      for (auto& root : incompatible) if (root.tag == dtb::kCommand)
        std::erase_if(root.kids, [](const auto& member) {
          return member.tag == dtb::kArray && !member.kids.empty() && member.kids.front().text == "BUTTON_DOWN_MSG";
        });
      const auto untouched = dtb::SerializeNodes(incompatible);
      CHECK_FALSE(modal::Install(incompatible));
      CHECK_TRUE(dtb::SerializeNodes(incompatible) == untouched);
    }
    BeginCase("settings portrait prompts separate readable messages from full-width native touch buttons");
    const auto settings_prompt = stored_objects(portrait.at("ui/pause/gen/settings_confirmation.milo_xbox"));
    CHECK_TRUE(settings_prompt.at("Shade.mesh") == generic_prompt.at("Shade.mesh"));
    const auto& prompt_list = portrait.at(rb_blitz::ui::input_font::kPortraitPromptListRecord);
    const auto prompt_buttons = stored_objects(prompt_list);
    CHECK_TRUE(prompt_buttons.contains("list")); // Popup provider field, not the splash field.
    CHECK_FALSE(prompt_buttons.contains("song"));
    for (const char* state : {"portrait_button", "portrait_selected", "portrait_border"})
      CHECK_TRUE(prompt_buttons.contains(std::string(state) + ".mesh"));
    std::vector<uint8_t> prompt_source;
    CHECK_TRUE(read("ui/pause/gen/confirmation_message.milo_xbox", &prompt_source, &why));
    for (const float aspect : {402.f / 874, 447.f / 982, 9.f / 16, .75f}) {
      std::vector<uint8_t> fitted;
      CHECK_TRUE(rb_blitz::ui::input_font::PreparePortraitSettingsPrompt(prompt_source, aspect, &fitted, &why));
      const auto objects = stored_objects(fitted);
      const auto& list = objects.at("options.lst");
      const float scale = std::bit_cast<float>(u32(list.data() + 0x19));
      const float width = rb_blitz::ui::PortraitSplash(aspect).width;
      CHECK_TRUE(std::abs(364.5f * scale - width * .84f) < .001f);
      CHECK_TRUE(52.75f * scale / width * 402 >= 48); // Phone-sized touch height.
      CHECK_TRUE((62 - 52.75f) * scale > 3); // Clear gap between actions.
      constexpr std::string_view resource = "../resource/lists/portrait_prompt.milo";
      CHECK_TRUE(std::search(list.begin(), list.end(), resource.begin(), resource.end()) != list.end());
      const auto& message = objects.at("pause.lbl");
      CHECK_EQ(std::bit_cast<float>(u32(message.data() + 0x19)), 1.f);
      const auto& animation = objects.at("on.anim");
      CHECK_EQ(std::bit_cast<float>(u32(animation.data() + 0xAF)), .5f);
      CHECK_EQ(std::bit_cast<float>(u32(animation.data() + 0xCF)), 1.f);
    }
    const auto splash_objects = stored_objects(art);
    CHECK_TRUE(splash_objects.contains("portrait_continue.mesh"));
    CHECK_TRUE(splash_objects.contains("portrait_back.mesh"));
    const auto* material = splash_objects.at("portrait_shade.mat").data();
    const auto* mesh = splash_objects.at("portrait_shade.mesh").data();
    CHECK_EQ(material[0x2A], uint8_t(1)); // pre-lit: honor packed vertex alpha
    CHECK_EQ(mesh[140], uint8_t(1)); // visible
    CHECK_EQ(u32(mesh + 219), uint32_t(81));
    CHECK_EQ(u32(mesh + 224), uint32_t(36));
    const auto alpha = [&](size_t vertex) { return u32(mesh + 232 + vertex * 36 + 12) >> 24; };
    CHECK_EQ(alpha(40), uint32_t(0)); // leave the centre completely untouched
    CHECK_TRUE(alpha(0) >= 60 && alpha(80) >= 60); // a light shade at the edges
    CHECK_TRUE(alpha(0) < 90 && alpha(80) < 90);
    const auto* faces = mesh + 232 + 81 * 36;
    CHECK_EQ(u32(faces), uint32_t(128));
    bool valid_faces = true;
    for (size_t i = 0; i < 128 * 3; ++i)
      valid_faces &= (uint32_t(faces[4 + i * 2]) << 8 | faces[5 + i * 2]) < 81;
    CHECK_TRUE(valid_faces);
    const auto& splash = portrait.at("ui/splash/gen/splash.dtb");
    std::vector<dtb::Node> roots;
    dtb::RootHeader header;
    CHECK_TRUE(dtb::Parse(splash.data(), splash.size(), &roots, &header, &why));
    CHECK_TRUE(header_test::Method(roots, "splash_panel", "rbbz_portrait_layout") != nullptr);
    const auto& connection = portrait.at("ui/net/gen/server_connect.dtb");
    CHECK_TRUE(dtb::Parse(connection.data(), connection.size(), &roots, &header, &why));
    TestGraphicsRecovery(roots, true);
    const auto& connection_scene = portrait.at(rb_blitz::ui::input_font::kConnectionRecord);
    const auto connection_objects = stored_objects(connection_scene);
    for (const auto* objects : {&settings_objects, &splash_objects, &connection_objects}) {
      const auto& black = objects->at("portrait_caption_black.color");
      for (size_t channel = 0; channel < 3; ++channel)
        CHECK_EQ(u32(black.data() + 17 + channel * 4), uint32_t(0));
    }
    const auto& status = connection_objects.at("status.lbl");
    constexpr std::string_view status_font = "../resource/fonts/portrait_splash.milo";
    CHECK_TRUE(std::search(status.begin(), status.end(), status_font.begin(), status_font.end()) != status.end());
    constexpr std::string_view status_color = "UIColor.color";
    CHECK_TRUE(std::search(status.begin(), status.end(), status_color.begin(), status_color.end()) != status.end());
    const auto& pulse = connection_objects.at("status_pulse.anim");
    for (size_t i = 0; i + 4 <= pulse.size(); ++i)
      CHECK_TRUE(u32(pulse.data() + i) != std::bit_cast<uint32_t>(1.25f));
    for (const char* action : {"continue", "back", "skip"}) {
      const auto name = std::string("portrait_") + action;
      CHECK_TRUE(connection_objects.contains(name + "_border.mesh"));
      CHECK_EQ(object_showing(connection_objects.at(name + "_surface.grp"), 0x21), uint8_t(0));
      const auto& hint = connection_objects.at(std::string(action) + ".ihp");
      constexpr std::string_view resource = "../resource/inline_help/portrait_actions.milo";
      CHECK_TRUE(std::search(hint.begin(), hint.end(), resource.begin(), resource.end()) != hint.end());
    }
    for (const auto& [path, bytes] : portrait) {
      const auto output = std::filesystem::path(argv[3]) / path;
      std::filesystem::create_directories(output.parent_path());
      std::ofstream file(output, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      CHECK_TRUE(bool(file));
    }
    return Finish();
  }
  TestHeaderAnimations();
  TestIOSAutoPause();
  TestPortraitSplash();
  TestHeaderVisibility();
  TestHeaderRuntimeRefresh();
  TestCurrencyVisibility();
  TestHeaderNavigation();
  TestMessagesUpdateInbox();
  TestDiscoveryWaitingLabel();
  BeginCase("the generated locale renames the downloadable-content row when R7 owns it");
  {
    using rb_blitz::ui::dtb::Node;
    namespace options = rb_blitz::menu_options;
    std::vector<Node> locale{Node::Array(
        {Node::Symbol(options::kDlcRowKey), Node::String(options::kDlcRowLabel)})};
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowLabel(&locale, false));
    CheckStrEq(locale[0].kids[1].text, options::kDlcRowLabel);
    CHECK_TRUE(rb_blitz::settings::SetRefreshRowLabel(&locale, true));
    CheckStrEq(locale[0].kids[1].text, options::kDlcRowRefreshLabel);
    // A translation of the shipped label, or a locale that never carried it, is left
    // alone: the replacement needs the exact text (the same scope the in-flight
    // menu_filter relabel has always had). ReplaceLocaleEntry reports done either way -
    // "left as it ships" is the outcome, not a refusal.
    std::vector<Node> translated{Node::Array(
        {Node::Symbol(options::kDlcRowKey), Node::String("Inhalt herunterladen")})};
    CHECK_TRUE(rb_blitz::settings::SetRefreshRowLabel(&translated, true));
    CheckStrEq(translated[0].kids[1].text, "Inhalt herunterladen");
  }
  BeginCase("the payload's own splash copy carries the refresh action when R7 owns the row");
  {
    using rb_blitz::ui::dtb::Node;
    namespace options = rb_blitz::menu_options;
    // The switch the row is dispatched in, with a sibling case so the walk has to find the right
    // one. The shipped body is retail's own online-store branch, which is what the press must no
    // longer run with R7 on.
    const auto fixture = [] {
      return std::vector<Node>{Node::Command(
          {Node::Symbol("switch"), Node::Symbol("selected_sym"),
           Node::Array({Node::Symbol("splash_start"),
                        Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                                       Node::Symbol("demo_screen")})}),
           Node::Array({Node::Symbol(options::kDlcRowKey),
                        Node::Command({Node::Symbol("if_else"), Node::Symbol("online"),
                                       Node::Symbol("store"), Node::Symbol("popup")})})})};
    };

    std::vector<Node> splash = fixture();
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowAction(&splash, false));
    CHECK_EQ(splash[0].kids.size(), 4u);  // the switch is untouched while the flag is off
    CHECK_EQ(splash[0].kids[3].kids.size(), 2u);  // ...and so is the shipped body

    CHECK_TRUE(rb_blitz::settings::SetRefreshRowAction(&splash, true));
    // kids: the row symbol, then exactly two statements - no padding, because this record is the
    // payload's own and may grow, which is the whole reason the action is built here.
    const Node& kase = splash[0].kids[3];
    CHECK_EQ(kase.kids.size(), 2u);
    CheckStrEq(kase.kids[0].text, options::kDlcRowKey);
    const Node& discover = kase.kids[1];
    CHECK_EQ(discover.kids.size(), 3u);
    CheckStrEq(discover.kids[0].text, "ui");
    CheckStrEq(discover.kids[1].text, "goto_screen");
    CheckStrEq(discover.kids[2].text, options::kRefreshRowDiscoverScreen);
    // The sibling case is untouched: the walk found the dlc case by name.
    CHECK_EQ(splash[0].kids[2].kids.size(), 2u);

    // Idempotent the same way the in-flight edit is: a case that already carries the sentinel is
    // left alone, so the payload and the hook can both be armed without rewriting twice.
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowAction(&splash, true));
    CHECK_EQ(splash[0].kids[3].kids.size(), 2u);

    // A record that ships no such case is a refusal, not a failure: the in-flight edit still owns
    // the retail copy.
    std::vector<Node> other{Node::Command(
        {Node::Symbol("switch"), Node::Symbol("selected_sym"),
         Node::Array({Node::Symbol("splash_start"),
                      Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                                     Node::Symbol("demo_screen")})})})};
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowAction(&other, true));
  }
  BeginCase("the recompilation credits are inserted before the retail credits");
  {
    using rb_blitz::ui::dtb::Node;
    std::vector<Node> roots;
    for (int i = 0; i < 3; ++i) roots.push_back(Node::Array({}));
    roots.push_back(
        Node::Array({Node::Symbol("heading"), Node::String("Core Development Team")}));
    roots.push_back(Node::Array({Node::Symbol("title_name"), Node::String("Project Lead"),
                                 Node::String("Matthew Nordhaus")}));

    std::string why;
    CHECK_TRUE(rb_blitz::settings::AddRecompilationCredits(&roots, false, &why));
    // Three spacers, the project's heading + row, a blank, the thanks heading + its eleven rows, a
    // blank, then the official section untouched.
    CHECK_EQ(roots.size(), 21u);
    if (roots.size() == 21u) {
      CheckStrEq(roots[3].kids[0].text, "heading");
      CheckStrEq(roots[3].kids[1].text, "Recompilation Project");
      CheckStrEq(roots[4].kids[0].text, "title_name");
      CheckStrEq(roots[4].kids[1].text, "Developer");
      CheckStrEq(roots[4].kids[2].text, "Gabriel Fávero");
      CHECK_EQ(roots[5].kids.size(), 0u);
      CheckStrEq(roots[6].kids[1].text, "Thanks To");
    }
    CheckThanksRows(roots);
    if (roots.size() == 21u) {
      CHECK_EQ(roots[18].kids.size(), 0u);
      CheckStrEq(roots[19].kids[1].text, "Core Development Team");
      CheckStrEq(roots[20].kids[2].text, "Matthew Nordhaus");
    }

    // The edited roots compile to a DTA the project's own parser reads back.
    const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(roots);
    std::vector<Node> reparsed;
    rb_blitz::ui::dtb::RootHeader header;
    CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &reparsed, &header, &why));
    CHECK_EQ(reparsed.size(), roots.size());

    // A different document is left alone rather than rewritten.
    std::vector<Node> other{
        Node::Array({Node::Symbol("heading"), Node::String("Some Other Team")})};
    CHECK_FALSE(rb_blitz::settings::AddRecompilationCredits(&other, false, &why));
    CHECK_EQ(other.size(), 1u);
  }
  BeginCase("the recompilation credits rename the Ultimate section and split its row");
  {
    using rb_blitz::ui::dtb::Node;
    const auto fixture = [] {
      std::vector<Node> roots;
      for (int i = 0; i < 2; ++i) roots.push_back(Node::Array({}));
      roots.push_back(Node::Array(
          {Node::Symbol("heading"), Node::String("Rock Band Blitz Ultimate 2.0 Development Team")}));
      roots.push_back(Node::Array(
          {Node::Symbol("title_name"), Node::String("Only Dev"), Node::String("Synchrony")}));
      for (int i = 0; i < 2; ++i) roots.push_back(Node::Array({}));
      roots.push_back(
          Node::Array({Node::Symbol("heading"), Node::String("Core Development Team")}));
      return roots;
    };

    std::vector<Node> roots = fixture();
    std::string why;
    CHECK_TRUE(rb_blitz::settings::AddRecompilationCredits(&roots, true, &why));
    // The one row became two plus the thanks block, so the file grew by fourteen roots.
    CHECK_EQ(roots.size(), 21u);
    if (roots.size() == 21u) {
      CheckStrEq(roots[2].kids[1].text, "Recompilation Project & Ultimate 2.0 Mod");
      CheckStrEq(roots[3].kids[1].text, "Recompilation Developer");
      CheckStrEq(roots[3].kids[2].text, "Gabriel Fávero");
      CheckStrEq(roots[4].kids[1].text, "Ultimate 2.0 Developer");
      CheckStrEq(roots[4].kids[2].text, "Synchrony");
      CHECK_EQ(roots[5].kids.size(), 0u);
      CheckStrEq(roots[6].kids[1].text, "Thanks To");
    }
    CheckThanksRows(roots);
    // The payload's own blank run is not lengthened by the insertion, and the official section
    // after it keeps its place.
    if (roots.size() == 21u) {
      CHECK_EQ(roots[18].kids.size(), 0u);
      CHECK_EQ(roots[19].kids.size(), 0u);
      CheckStrEq(roots[20].kids[1].text, "Core Development Team");
    }

    // A newer payload whose one row does not match is left untouched, including its heading.
    std::vector<Node> moved = fixture();
    moved[3].kids[1].text = "Someone Else";
    CHECK_FALSE(rb_blitz::settings::AddRecompilationCredits(&moved, true, &why));
    CheckStrEq(moved[2].kids[1].text, "Rock Band Blitz Ultimate 2.0 Development Team");
    CHECK_EQ(moved.size(), fixture().size());
  }
  TestMiloFlowBoundaries();
  TestPortraitCanvas();
  TestExtendingKeepsOnePart();
  TestBaseArkGetsItsOwnPart();
  TestMountedPartOfTheWrongSizeIsNotCarried();
  TestLoadedMenuRefresh();
  return Finish();
}
