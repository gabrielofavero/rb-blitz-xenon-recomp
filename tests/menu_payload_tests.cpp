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

#include "settings/menu_payload.h"
#include "settings/ultimate_adapter.h"
#include "ui/menu_options.h"
#include "ui/input_font_scene.h"
#include "ui/settings_screen.h"

#include <filesystem>
#include <fstream>
#include <iterator>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
    "Controller Layouts (Adapted)|zacksly (Nintendo Switch 1/2)",
    "Controller Layouts (Adapted)|Valve Software (Steam Deck)",
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

// Optional integration check against extracted records; never boots or drives the game.
int CheckInstalledHeaderRecords(const char* asset_root, const char* output_root) {
  namespace fs = std::filesystem;
  namespace screen = rb_blitz::ui::screen;
  using namespace header_test;
  settings::RecordReader read = [&](const std::string& name, std::vector<uint8_t>* bytes, std::string* why) {
    std::ifstream file(fs::path(asset_root) / name, std::ios::binary);
    if (!file) { if (why) *why = "missing " + name; return false; }
    bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  };
  for (int boot_case = 0; boot_case < 16; ++boot_case) {
    const int mask = boot_case & 7;
    const bool fast = boot_case >= 8;
    const auto state = State(mask);
    screen::MenuConfiguration config;
    config.rhythmverse_row = state.store;
    config.show_messages = state.messages;
    config.show_recommendations = state.recommendations;
    screen::ConfigureMenu(config);
    settings::MenuRecords records;
    std::string why;
    std::vector<std::string> notes;
    if (!settings::BuildMenuRecords(read, {"ui/locale/eng/gen/locale_keep.dtb"}, &records,
                                    &why, false, false, &notes, {}, fast)) {
      std::printf("header records refused: %s\n", why.c_str()); return 1;
    }
    CHECK_TRUE(notes.empty());
    CHECK_TRUE(records.contains("ui/startup/gen/startup.dtb") == fast);
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
          if (root.kids[0].text == "splash_order") CHECK_EQ(root.kids.size(), 1u);
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
void TestGraphicsRecovery(const std::vector<rb_blitz::ui::dtb::Node>& roots) {
  using namespace rb_blitz::ui;
  using dtb::Node;
  struct Machine {
    const std::vector<Node>& roots;
    bool pending = true, failed = true;
    int retries = 0, ignores = 0, menus = 0, discoveries = 0;
    std::string action;
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
  CHECK_TRUE(retry.hints["continue.ihp"]=="rbbz_graphics_retry");
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
}

int main(int argc, char** argv) {
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
  if (argc == 3 && std::string(argv[1]) == "check-startup-record") {
    using namespace rb_blitz::ui::dtb;
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<Node> roots;
    RootHeader header;
    std::string why;
    CHECK_TRUE(Parse(bytes.data(), bytes.size(), &roots, &header, &why));
    CHECK_TRUE(Compile(roots, header) == bytes);
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
    return CheckInstalledHeaderRecords(argv[2], argv[3]);
  }
  TestHeaderAnimations();
  TestHeaderVisibility();
  TestHeaderNavigation();
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
    // Three spacers, the project's heading + row, a blank, the thanks heading + its ten rows, a
    // blank, then the official section untouched.
    CHECK_EQ(roots.size(), 20u);
    CheckStrEq(roots[3].kids[0].text, "heading");
    CheckStrEq(roots[3].kids[1].text, "Recompilation Project");
    CheckStrEq(roots[4].kids[0].text, "title_name");
    CheckStrEq(roots[4].kids[1].text, "Developer");
    CheckStrEq(roots[4].kids[2].text, "Gabriel Fávero");
    CHECK_EQ(roots[5].kids.size(), 0u);
    CheckStrEq(roots[6].kids[1].text, "Thanks To");
    CheckThanksRows(roots);
    CHECK_EQ(roots[17].kids.size(), 0u);
    CheckStrEq(roots[18].kids[1].text, "Core Development Team");
    CheckStrEq(roots[19].kids[2].text, "Matthew Nordhaus");

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
    // The one row became two plus the thanks block, so the file grew by thirteen roots.
    CHECK_EQ(roots.size(), 20u);
    CheckStrEq(roots[2].kids[1].text, "Recompilation Project & Ultimate 2.0 Mod");
    CheckStrEq(roots[3].kids[1].text, "Recompilation Developer");
    CheckStrEq(roots[3].kids[2].text, "Gabriel Fávero");
    CheckStrEq(roots[4].kids[1].text, "Ultimate 2.0 Developer");
    CheckStrEq(roots[4].kids[2].text, "Synchrony");
    CHECK_EQ(roots[5].kids.size(), 0u);
    CheckStrEq(roots[6].kids[1].text, "Thanks To");
    CheckThanksRows(roots);
    // The payload's own blank run is not lengthened by the insertion, and the official section
    // after it keeps its place.
    CHECK_EQ(roots[17].kids.size(), 0u);
    CHECK_EQ(roots[18].kids.size(), 0u);
    CheckStrEq(roots[19].kids[1].text, "Core Development Team");

    // A newer payload whose one row does not match is left untouched, including its heading.
    std::vector<Node> moved = fixture();
    moved[3].kids[1].text = "Someone Else";
    CHECK_FALSE(rb_blitz::settings::AddRecompilationCredits(&moved, true, &why));
    CheckStrEq(moved[2].kids[1].text, "Rock Band Blitz Ultimate 2.0 Development Team");
    CHECK_EQ(moved.size(), fixture().size());
  }
  TestExtendingKeepsOnePart();
  TestBaseArkGetsItsOwnPart();
  TestMountedPartOfTheWrongSizeIsNotCarried();
  return Finish();
}
