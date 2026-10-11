// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/ui/settings_screen.{h,cpp} - P5b's **merged Help & Options** surface: the
// whole authored tree, the two retail rows it re-points, the labels it needs and the requests it
// sends (docs/plans/in-game-settings-plan.md §3 and §5.2, and the P5b package prompt).
//
// What is pinned here, none of which needs a boot:
//
//   * the definition compiles, parses back and recompiles byte for byte - the property that makes
//     it safe to serve twice and to compare;
//   * the tree's *shape*: one root panel hosted by two screens, the six root rows in the plan's
//     order with the retail three at their retail positions, the provider-only rows that a base
//     install must not show, and every screen's back target;
//   * the list-ownership property this package exists for: no panel names retail's
//     `options_menu.milo`, so no panel of ours can share retail's `options.lst` object;
//   * every request path the definition writes parses in the bridge's own text grammar
//     (src/hooks/settings_bridge.h) - the seam between the two modules, where a grammar change has
//     to break a *test* and not a boot;
//   * the retail-row edits: each replaces the case body it is aimed at, keeps the row symbol, and
//     refuses - without touching the tree - when the case is not there;
//   * the labels: unique, non-empty, and never the retired `advanced_settings` row's;
//   * every `set_data` is paired with a `set_selected`, and every panel's button table is a single
//     `{switch action ...}` with its cases as direct children (both bugs P1 measured).
//
// The fixtures are synthetic and built here, in the retail shapes the edits are aimed at - a
// `{switch {start.lst selected_sym} [splash_options ...] ...}` row table for the main menu, and a
// `{switch {get_focus selected_sym} [pause_options ...] ...}` one for the pause panel - so no
// retail content is needed to run this.
//
// SDK-free on purpose. Non-test argv modes write files a boot can be pointed at:
//
//   rb_blitz_settings_screen_tests emit-def    <out.dtb>
//   rb_blitz_settings_screen_tests emit-append <in.dtb> <out.dtb>
//   rb_blitz_settings_screen_tests emit-splash <in.dtb> <out.dtb>
//   rb_blitz_settings_screen_tests emit-pause  <in.dtb> <out.dtb>
//   rb_blitz_settings_screen_tests emit-locale <in.dtb> <out.dtb> [menu-row-text]
//
// They only ever *read* an input, and they write only the path the caller names.

#include "check.h"

#include "hooks/settings_bridge.h"
#include "ui/dtb_author.h"
#include "ui/settings_screen.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::ui;
using namespace rb_blitz::ui::screen;
using namespace rb_blitz::test;

using dtb::Node;

Node Sym(const char* name) { return dtb::Node::Symbol(name); }
Node Var(const char* name) { return dtb::Node::Var(name); }
Node Str(const char* text) { return dtb::Node::String(text); }
Node Arr(std::vector<dtb::Node> kids) { return dtb::Node::Array(std::move(kids)); }
Node Cmd(std::vector<dtb::Node> kids) { return dtb::Node::Command(std::move(kids)); }

// The retail init block's shape: an **array** whose first child is the `init` symbol and whose
// tail is `{set {var first_screen} <name>}`. Transcribed from ui/gen/ui.dtb, which is an array -
// and, like that file, it sits in a **later root** than the first, so a search that only looked at
// root 0 is caught here rather than by a boot.
std::vector<dtb::Node> InitFixture() {
  return {
      Arr({Sym("cancel_transition_notify"), Sym("TRUE")}),
      Arr({Sym("init"), Cmd({Sym("app_label_register")}), Cmd({Sym("pause_panel"), Sym("load")}),
           Cmd({Sym("set"), Var("first_screen"), Sym("splash_screen")}),
           Cmd({Sym("ui"), Sym("my_init")}),
           Cmd({Sym("ui"), Sym("goto_screen"), Var("first_screen")})}),
      Arr({Sym("max_push_depth"), dtb::Node::Int(2)}),
  };
}

// The retail main menu's panel, with `splash_options` routing to the retail options screen - the
// shipped shape P5b re-points, transcribed from ui/splash/gen/splash.dtb (the row table, its two
// neighbours and the option array). Note that the *rows* live in an array of bare symbols while the
// *cases* are arrays whose tail is made of commands: that difference is what RerouteRow's matcher
// has to tell apart, and this fixture is what catches it.
std::vector<dtb::Node> SplashFixture() {
  const std::vector<dtb::Node> rows = {
      Sym("splash_start"), Sym("splash_leaderboard"), Sym("splash_achievements"),
      Sym("splash_options"), Sym("splash_upgrade"), Sym("splash_dlc"), Sym("splash_exit")};
  const Node enter = Cmd({Sym("do"), Cmd({Var("options_array"), Cmd({Sym("array"), Arr(rows)})}),
                          Cmd({Sym("start.lst"), Sym("set_data"), Var("options_array")})});
  const Node start_case = Arr({Sym("splash_start"), Cmd({Var("this"), Sym("goto_main_hub")})});
  const Node options_case =
      Arr({Sym("splash_options"),
           Cmd({Sym("options_screen"), Sym("set"), Sym("back_screen"), Sym("splash_screen")}),
           Cmd({Sym("ui"), Sym("goto_screen"), Sym("options_screen")})});
  const Node exit_case = Arr(
      {Sym("splash_exit"), Cmd({Sym("ui"), Sym("goto_screen"), Sym("confirm_exit_game_screen")})});
  const Node table =
      Cmd({Sym("switch"), Cmd({Sym("start.lst"), Sym("selected_sym")}), start_case, options_case,
           exit_case});
  return {
      Cmd({Sym("new"), Sym("UIPanel"), Sym("splash_panel"), Arr({Sym("file"), Str("splash.milo")}),
           Arr({Sym("enter"), enter}), Arr({Sym("SELECT_MSG"), table})}),
  };
}

// The in-song pause panel, with `pause_options` switching the *focus* to the panel's own inline
// options list instead of going anywhere - transcribed from ui/pause/gen/pause.dtb, whose row
// table is keyed on `{get_focus selected_sym}` rather than on a list. That is a second switch
// *value* shape RerouteRow has to accept.
std::vector<dtb::Node> PauseFixture() {
  const Node value = Cmd({dtb::Node::Property({Sym("get_focus")}), Sym("selected_sym")});
  const Node resume_case = Arr({Sym("pause_resume"), Cmd({Var("this"), Sym("resume")})});
  const Node options_case =
      Arr({Sym("pause_options"), Cmd({Var("this"), Sym("update_focus"), Sym("options.lst")})});
  const Node back_case = Arr(
      {Sym("pause_options_back"), Cmd({Var("this"), Sym("update_focus"), Sym("pause.lst")})});
  const Node table = Cmd({Sym("switch"), value, resume_case, options_case, back_case});
  return {
      Cmd({Sym("new"), Sym("UIPanel"), Sym("pause_panel"), Arr({Sym("file"), Str("pause.milo")}),
           Arr({Sym("focus"), Sym("pause.lst")}), Arr({Sym("SELECT_MSG"), table})}),
  };
}

// Every symbol anywhere under a node.
void CollectSymbols(const dtb::Node& node, std::vector<std::string>* out) {
  if (node.tag == dtb::kSymbol) {
    out->push_back(node.text);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectSymbols(kid, out);
  }
}

bool HasSymbol(const dtb::Node& root, const std::string& name) {
  std::vector<std::string> symbols;
  CollectSymbols(root, &symbols);
  for (const std::string& symbol : symbols) {
    if (symbol == name) {
      return true;
    }
  }
  return false;
}

// Every text leaf (a symbol and a string are both leaves a definition can name an asset with).
void CollectText(const dtb::Node& node, std::vector<std::string>* out) {
  if (node.tag == dtb::kSymbol || node.tag == dtb::kString) {
    out->push_back(node.text);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectText(kid, out);
  }
}

bool HasText(const dtb::Node& root, const std::string& name) {
  std::vector<std::string> texts;
  CollectText(root, &texts);
  return std::find(texts.begin(), texts.end(), name) != texts.end();
}

// Every `{<receiver> <method> <args...>}` call node under a tree - the method being the second
// child, which is the shape the definitions in this module write.
void FindCalls(const dtb::Node& node, const std::string& method,
               std::vector<const dtb::Node*>* out) {
  if (node.tag == dtb::kCommand && node.kids.size() >= 2 && node.kids[1].tag == dtb::kSymbol &&
      node.kids[1].text == method) {
    out->push_back(&node);
  }
  for (const dtb::Node& kid : node.kids) {
    FindCalls(kid, method, out);
  }
}

// Every `{switch <var X> ...}` command - a case table keyed on a variable.
void CollectSwitchesOn(const dtb::Node& node, const std::string& variable,
                       std::vector<const dtb::Node*>* out) {
  if (node.tag == dtb::kCommand && node.kids.size() >= 2 && node.kids[0].tag == dtb::kSymbol &&
      node.kids[0].text == "switch" && node.kids[1].tag == dtb::kVar &&
      node.kids[1].text == variable) {
    out->push_back(&node);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectSwitchesOn(kid, variable, out);
  }
}

std::vector<std::string> Words(const dtb::Node& node) {
  std::vector<std::string> out;
  CollectText(node, &out);
  return out;
}

// Every `{switch {<list> selected_sym} ...}` command - the row table.
void CollectRowTables(const dtb::Node& node, std::vector<const dtb::Node*>* out) {
  if (node.tag == dtb::kCommand && node.kids.size() >= 2 && node.kids[0].tag == dtb::kSymbol &&
      node.kids[0].text == "switch" && node.kids[1].tag == dtb::kCommand) {
    const std::vector<std::string> words = Words(node.kids[1]);
    if (std::find(words.begin(), words.end(), "selected_sym") != words.end()) {
      out->push_back(&node);
    }
  }
  for (const dtb::Node& kid : node.kids) {
    CollectRowTables(kid, out);
  }
}

// A `{<receiver> <method> ...}` call node - the shape this module's definitions write.
bool IsCall(const dtb::Node& node, const std::string_view method) {
  return node.tag == dtb::kCommand && node.kids.size() >= 2 && node.kids[1].tag == dtb::kSymbol &&
         node.kids[1].text == method;
}

// Every `{<name> ...}` member array: a panel's `populate_menu`, `select_row`, `move_up`, `move_down`.
void CollectMembers(const dtb::Node& node, const std::string_view name,
                    std::vector<const dtb::Node*>* out) {
  if (node.tag == dtb::kArray && !node.kids.empty() && node.kids[0].tag == dtb::kSymbol &&
      node.kids[0].text == name) {
    out->push_back(&node);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectMembers(kid, name, out);
  }
}

// The direct statements of a member's `{do <stmt> ...}` - a `populate_menu` body, whose statements
// have to be the `do`'s own children rather than one array of statements.
std::vector<const dtb::Node*> DoStatements(const dtb::Node& member) {
  std::vector<const dtb::Node*> out;
  for (const dtb::Node& kid : member.kids) {
    if (kid.tag != dtb::kCommand || kid.kids.empty() || kid.kids[0].tag != dtb::kSymbol ||
        kid.kids[0].text != "do") {
      continue;
    }
    for (size_t i = 1; i < kid.kids.size(); ++i) {
      out.push_back(&kid.kids[i]);
    }
  }
  return out;
}

bool HasLowercase(const std::string& text) {
  for (const char c : text) {
    if (c >= 'a' && c <= 'z') {
      return true;
    }
  }
  return false;
}

bool HasWord(const dtb::Node& node, const std::string_view word) {
  const std::vector<std::string> words = Words(node);
  return std::find(words.begin(), words.end(), word) != words.end();
}

// Every `{do <stmt> ...}` command.
void CollectDoCommands(const dtb::Node& node, std::vector<const dtb::Node*>* out) {
  if (node.tag == dtb::kCommand && !node.kids.empty() && node.kids[0].tag == dtb::kSymbol &&
      node.kids[0].text == "do") {
    out->push_back(&node);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectDoCommands(kid, out);
  }
}

// Every `{var <name> <value>}` declaration: an *array* whose first child is the var.
void CollectDeclarations(const dtb::Node& node, std::vector<const dtb::Node*>* out) {
  if (node.tag == dtb::kArray && !node.kids.empty() && node.kids[0].tag == dtb::kVar) {
    out->push_back(&node);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectDeclarations(kid, out);
  }
}

std::vector<dtb::Node> Reparse(const std::vector<dtb::Node>& nodes, std::string* error) {
  const std::vector<uint8_t> file = dtb::Compile(nodes, dtb::RootHeader{0x52425A31, 1, 0});
  std::vector<dtb::Node> parsed;
  dtb::RootHeader header;
  if (!dtb::Parse(file.data(), file.size(), &parsed, &header, error)) {
    return {};
  }
  return parsed;
}

void TestDefinitionRoundTrip() {
  BeginCase("the authored definition compiles, parses back and recompiles identically");

  const std::vector<dtb::Node> authored = DefinitionRoots();
  // Fourteen panels, their fourteen screens, and the in-song root screen that hosts the same root
  // panel as the main-menu one.
  CHECK_EQ(authored.size(), 29u);

  std::string error;
  const std::vector<dtb::Node> parsed = Reparse(authored, &error);
  CHECK_TRUE(parsed.size() == authored.size());
  if (parsed.size() != authored.size()) {
    std::printf("         parse refused: %s\n", error.c_str());
    return;
  }

  const std::vector<uint8_t> first = dtb::Compile(authored, dtb::RootHeader{0x52425A31, 1, 0});
  const std::vector<uint8_t> again = dtb::Compile(parsed, dtb::RootHeader{0x52425A31, 1, 0});
  CHECK_EQ(again.size(), first.size());
  CHECK_MEM_EQ(again.data(), first.data(), first.size());
}

void TestMergedRootShape() {
  BeginCase("the merged root carries the plan's six rows, with the retail three in their slots");

  const std::vector<std::string> panels = PanelNames();
  CHECK_EQ(panels.size(), 14u);
  CHECK_TRUE(panels.front() == kRootPanel);

  const std::vector<std::string> screens = ScreenNames();
  CHECK_EQ(screens.size(), 15u);
  CHECK_TRUE(screens.front() == kRootScreen);
  CHECK_TRUE(screens.back() == kRootPauseScreen);

  // The root's rows, in the plan's §3 order. Retail's help list is How to Play, Controls,
  // Calibration, Audio/Video, Credits; the merge keeps Controls / Calibration / Audio-Video at
  // indices 1 / 2 / 3 so the route-driven acceptance scripts (`New-UiHelpRoute 1|2|3`) still land
  // on the same screens, and General / Gameplay take the two slots the retail rows vacate.
  const std::vector<std::string> words = Words(DefinitionRoots().front());
  auto at = [&words](const std::string& prefix) {
    for (size_t i = 0; i < words.size(); ++i) {
      if (words[i].rfind(prefix, 0) == 0) {
        return i;
      }
    }
    return words.size();
  };
  const char* order[] = {"settings_general", "controls", "calibration", "audio_video_options",
                         "settings_gameplay", "settings_interface"};
  for (size_t i = 0; i + 1 < 6; ++i) {
    CHECK_TRUE(at(order[i]) < words.size());
    CHECK_TRUE(at(order[i]) < at(order[i + 1]));
  }

  // Gameplay is the provider-only row: the gate query in this panel is what drops it, which is how
  // a base install shows five root rows and an Ultimate install six.
  CHECK_TRUE(HasText(DefinitionRoots().front(), kProviderQuery));
  CHECK_TRUE(HasText(DefinitionRoots().front(), "settings_gameplay"));
  CHECK_FALSE(HasText(DefinitionRoots().front(), "settings_gameplay_on"));
}

void TestListOwnership() {
  BeginCase("no panel names retail's options scene, so the merged list cannot be shared");

  // The defect this package fixes. P1's panel used `{file options_menu.milo}` - the same scene
  // retail's `options_panel` loads - and the engine's per-path dir loader therefore handed both
  // panels the *same* `options.lst` object, so they overwrote each other's rows and selection and
  // Back landed on the retail list's last row (bridge findings §12.3b). Every panel of the merged
  // surface has to name the project-owned scene instead.
  for (const dtb::Node& root : DefinitionRoots()) {
    const std::vector<std::string> words = Words(root);
    if (std::find(words.begin(), words.end(), "UIPanel") == words.end()) {
      continue;  // a screen, not a panel
    }
    CHECK_TRUE(HasText(root, kSceneName));
    CHECK_TRUE(HasText(root, kListName));
    CHECK_FALSE(HasText(root, "options_menu.milo"));
  }

  // The copy has to live **beside** the retail scene, because that scene refers to its neighbours
  // by relative path (`neon_lights.milo`, `../resource/lists/list_options_main.milo`), and those
  // resolve against the scene's own directory.
  const std::string scene = kSceneRecord;
  const std::string source = kSceneSourceRecord;
  CHECK_TRUE(scene.substr(0, scene.rfind('/')) == source.substr(0, source.rfind('/')));
  CHECK_TRUE(scene != source);
}

void TestScreenBackTargets() {
  BeginCase("each screen declares a back target and the root is hosted by two contexts");

  const std::vector<dtb::Node> roots = DefinitionRoots();
  for (const dtb::Node& root : roots) {
    const std::vector<std::string> words = Words(root);
    if (std::find(words.begin(), words.end(), "UIScreen") == words.end()) {
      continue;  // a panel
    }
    CHECK_TRUE(HasText(root, "back_screen"));
  }

  // The in-song root screen keeps the running game and the pause panel on screen - retail's own
  // `av_options_pause_screen` shape - and returns to `game_screen`. That is also why a panel of
  // ours leaves through `{ui current_screen get back_screen}` rather than naming a destination.
  const dtb::Node& pause = roots.back();
  CHECK_TRUE(HasText(pause, kRootPauseScreen));
  CHECK_TRUE(HasText(pause, kRootPanel));
  CHECK_TRUE(HasText(pause, "game_panel"));
  CHECK_TRUE(HasText(pause, "pause_panel"));
  CHECK_TRUE(HasText(pause, "game_screen"));
}

void TestRequestPathsParse() {
  BeginCase("every request the definition sends parses in the bridge's own grammar");

  using rb_blitz::settings::bridge::EncodeTextRequest;
  using rb_blitz::settings::bridge::Op;
  using rb_blitz::settings::bridge::ParseTextRequest;
  using rb_blitz::settings::bridge::Request;
  using rb_blitz::settings::bridge::ValueKind;

  const std::vector<std::string> paths = RequestPaths();
  CHECK_TRUE(paths.size() > 40u);

  for (const std::string& path : paths) {
    Request request;
    std::string error;
    const bool ok = ParseTextRequest(path, &request, &error);
    CHECK_TRUE(ok);
    if (!ok) {
      std::printf("         %s refused: %s\n", path.c_str(), error.c_str());
      continue;
    }
    // A commit carries no setting and no value; every other request addresses one.
    if (request.op == Op::kCommit) {
      CHECK_TRUE(request.setting.empty());
      CHECK_TRUE(request.value.kind == ValueKind::kNone);
    } else {
      CHECK_TRUE(!request.setting.empty());
    }
    // The round trip back to text is the same path, so what the boot log prints is what the guest
    // wrote and can be pasted back into a test.
    CHECK_TRUE(EncodeTextRequest(request) == path);
  }

  // Both halves of the channel are represented: the gate query the provider row is dropped on, the
  // commit that ends an edit, a snapshot read, a write and the one-shot action.
  auto has_op = [&paths](const Op op) {
    for (const std::string& path : paths) {
      Request request;
      std::string error;
      if (ParseTextRequest(path, &request, &error) && request.op == op) {
        return true;
      }
    }
    return false;
  };
  CHECK_TRUE(has_op(Op::kSnapshot));
  CHECK_TRUE(has_op(Op::kSet));
  CHECK_TRUE(has_op(Op::kCommit));
  CHECK_TRUE(has_op(Op::kInvoke));
  CHECK_TRUE(std::find(paths.begin(), paths.end(), std::string(kProviderQuery)) != paths.end());

  // And every `rbbz-settings/` literal the definition actually spells is in that table, so the
  // table cannot drift from the tree it describes.
  std::vector<std::string> literals;
  for (const dtb::Node& root : DefinitionRoots()) {
    CollectText(root, &literals);
  }
  int checked = 0;
  for (const std::string& literal : literals) {
    if (literal.rfind("rbbz-settings/", 0) != 0) {
      continue;
    }
    ++checked;
    CHECK_TRUE(std::find(paths.begin(), paths.end(), literal) != paths.end());
  }
  CHECK_TRUE(checked > 40);
}

void TestPanelLoadEdit() {
  BeginCase("the panel load lands inside {init ...} before first_screen, and is idempotent");

  // Retail creates the panels it declares this way (`{options_panel load}` and ~24 siblings in
  // ui/gen/ui.dtb). A panel that is only named inside a screen's `{panels ...}` list still draws
  // its rows, but its focus component is never established, so the list does not take the
  // direction keys - measured on the merged root, whose highlight stayed on row 0 through every
  // press until the load was added.
  std::vector<dtb::Node> nodes = InitFixture();
  std::string reason;
  CHECK_TRUE(AddPanelLoad(&nodes, kRootPanel, &reason));
  CHECK_TRUE(reason.empty());

  std::vector<std::string> symbols;
  CollectSymbols(nodes[1], &symbols);
  size_t load_at = symbols.size();
  size_t first_at = symbols.size();
  for (size_t i = 0; i < symbols.size(); ++i) {
    if (symbols[i] == kRootPanel && load_at == symbols.size()) load_at = i;
    if (symbols[i] == "first_screen" && first_at == symbols.size()) first_at = i;
  }
  CHECK_TRUE(load_at < first_at);

  // Idempotent: a second call adds nothing.
  const size_t before = symbols.size();
  CHECK_TRUE(AddPanelLoad(&nodes, kRootPanel, &reason));
  std::vector<std::string> again;
  CollectSymbols(nodes[1], &again);
  CHECK_EQ(again.size(), before);

  // And the edited tree still compiles and reparses.
  std::string error;
  CHECK_TRUE(Reparse(nodes, &error).size() == nodes.size());

  // Refuses, and changes nothing, without its anchor.
  std::vector<dtb::Node> bare = {Arr({Sym("max_push_depth"), dtb::Node::Int(2)})};
  reason.clear();
  CHECK_FALSE(AddPanelLoad(&bare, kRootPanel, &reason));
  CHECK_TRUE(!reason.empty());
  CHECK_EQ(bare.size(), 1u);
}

void TestSplashReroute() {
  BeginCase("the main-menu row is re-pointed at the merged root, keeping its symbol");

  std::vector<dtb::Node> nodes = SplashFixture();
  std::string reason;
  CHECK_TRUE(RerouteRow(&nodes, kMainMenuRow, MainMenuRouteBody(), &reason));
  CHECK_TRUE(reason.empty());

  const std::vector<std::string> words = Words(nodes[0]);
  CHECK_TRUE(std::find(words.begin(), words.end(), kMainMenuRow) != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), kRootScreen) != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "splash_screen") != words.end());
  // The retail destination is gone, and the rows that were not re-pointed are untouched.
  CHECK_TRUE(std::find(words.begin(), words.end(), "options_screen") == words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "splash_start") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "splash_exit") != words.end());

  // The row *list* still holds the symbol, because the merge keeps the retail row and only changes
  // where the row goes. That is the whole difference from P1/P5's added Advanced Settings row,
  // which needed a new array element, a new case and a new locale key.
  std::vector<std::string> symbols;
  CollectSymbols(nodes[0], &symbols);
  CHECK_TRUE(std::find(symbols.begin(), symbols.end(), std::string(kMainMenuRow)) !=
             symbols.end());

  std::string error;
  CHECK_TRUE(Reparse(nodes, &error).size() == nodes.size());
}

void TestPauseReroute() {
  BeginCase("the in-song pause row keeps retail's own focus switch, and the record is left alone");

  std::vector<dtb::Node> nodes = PauseFixture();
  std::string reason;
  CHECK_TRUE(RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &reason));
  CHECK_TRUE(reason.empty());

  const std::vector<std::string> words = Words(nodes[0]);
  CHECK_TRUE(std::find(words.begin(), words.end(), kPauseMenuRow) != words.end());
  // Retail's own in-song behaviour, restored verbatim: hand the focus to the pause panel's inline
  // list. Measured: every body that leaves the pause list instead - `{transition_to ...}`, on the
  // panel's own primitive, and `{ui goto_screen ...}`, the main menu's - faults the guest with
  // `read of guest 0x00000024` on Confirm, and so does retail's switch plus the jump in one frame.
  CHECK_TRUE(std::find(words.begin(), words.end(), "options.lst") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "update_focus") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "goto_screen") == words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "transition_to") == words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), kRootPauseScreen) == words.end());
  // The panel's own Back row for the inline list is untouched, which is what makes the retail in-song
  // page still usable while the merge of that list's contents is pending.
  CHECK_TRUE(std::find(words.begin(), words.end(), "pause_options_back") != words.end());

  std::string error;
  CHECK_TRUE(Reparse(nodes, &error).size() == nodes.size());
}

void TestRerouteRefusesWithoutTheCase() {
  BeginCase("a re-route with no such case refuses, and changes nothing");

  std::vector<dtb::Node> nodes = SplashFixture();
  const size_t before = nodes.size();
  std::string reason;
  CHECK_FALSE(RerouteRow(&nodes, "no_such_row", MainMenuRouteBody(), &reason));
  CHECK_TRUE(!reason.empty());
  CHECK_EQ(nodes.size(), before);

  // A row *list* is not a case table: the option array holds bare symbols, so a symbol that appears
  // only there must not be matched. `splash_achievements` is in the array and has no case, and a
  // matcher that keyed on "first child of an array" would take it.
  CHECK_FALSE(RerouteRow(&nodes, "splash_achievements", MainMenuRouteBody(), &reason));
  CHECK_TRUE(!reason.empty());
}

void TestDoTakesStatementsDirectly() {
  BeginCase("every {do ...} takes runnable statements, and declarations use the array tag");

  // Two bugs of the same family, both measured in a real boot, both invisible to a structure-only
  // test because they *compile, parse and recompile byte for byte*:
  //
  //   1. `{do (stmt stmt ...)}` - a `do` whose single argument is an array of statements - does
  //      nothing at all. The merged root drew its title and no rows, because `populate_menu`'s
  //      `set_data` never ran.
  //   2. `{var rows <value>}` emitted as a *command* crashes the guest on entry with
  //      `read of guest 0x00000008`. This compiler encodes a declaration as an **array** whose
  //      first child is the var; a command whose first child is a var is an expression that *reads*
  //      one (that is what `{this populate_menu}` is).
  int dos = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    std::vector<const dtb::Node*> found;
    CollectDoCommands(root, &found);
    for (const dtb::Node* node : found) {
      ++dos;
      CHECK_TRUE(node->kids.size() >= 2);  // `{do}` with nothing to run is always a mistake
      for (size_t i = 1; i < node->kids.size(); ++i) {
        const dtb::Node& kid = node->kids[i];
        const bool declaration =
            kid.tag == dtb::kArray && !kid.kids.empty() && kid.kids[0].tag == dtb::kVar;
        CHECK_TRUE(kid.tag == dtb::kCommand || declaration);
      }
    }
  }
  CHECK_TRUE(dos > 20);  // one per panel's populate_menu, plus the navigation statements

  // And the declaration that `populate_menu` builds its row list with is that array form: one
  // declaration (`rows`) per panel. The count is printed before it is indexed: an unguarded
  // `front()` on a vector that stays empty is how this check used to crash the harness instead of
  // failing it.
  const std::vector<dtb::Node> roots = DefinitionRoots();
  std::vector<const dtb::Node*> declarations;
  if (!roots.empty()) {
    CollectDeclarations(roots.front(), &declarations);
  }
  std::printf("[ INFO ] first root: %zu root(s), %zu declaration(s)\n", roots.size(),
              declarations.size());
  CHECK_EQ(declarations.size(), 1u);
  if (!declarations.empty()) {
    CHECK_TRUE(declarations.front()->kids[0].text == "rows");
  }
}

void TestLocaleEntry() {
  BeginCase("a label is appended once, a duplicate key is refused, and the retired row is absent");

  std::vector<dtb::Node> nodes = {Arr({Sym("splash_start"), Str("Start")})};
  std::string reason;
  CHECK_TRUE(AddLocaleEntry(&nodes, kRootPanel, "HELP & OPTIONS", &reason));
  CHECK_TRUE(reason.empty());
  CHECK_EQ(nodes.size(), 2u);
  CHECK_TRUE(nodes[1].kids[0].text == kRootPanel);
  CHECK_TRUE(nodes[1].kids[1].text == "HELP & OPTIONS");
  // The shape every locale file in this title uses: exactly two elements.
  CHECK_EQ(nodes[1].kids.size(), 2u);

  CHECK_FALSE(AddLocaleEntry(&nodes, "splash_start", "Other", &reason));
  CHECK_TRUE(!reason.empty());
  CHECK_EQ(nodes.size(), 2u);

  // P1/P5's added row is retired by the merge, and retiring it means its locale entry is not
  // written either: the payload is generated from the retail records, which never carried it.
  for (const auto& label : Labels()) {
    CHECK_TRUE(label.first != kRetiredRow);
  }

  std::string error;
  CHECK_TRUE(Reparse(nodes, &error).size() == nodes.size());
}

void TestEveryFillPlacesTheSelection() {
  BeginCase("every fill places the selection, and only the arrows move it afterwards");

  // The bug this pins, measured in a real boot: the first row was highlighted and Confirm ran the
  // *second* row's action, because a list keeps its selected symbol across a `set_data`. Retail
  // pairs the two everywhere it fills a list, and so does this definition: the placement is the
  // statement *directly after* the fill, in the same `populate_menu` body.
  //
  // The number of `set_selected` calls is deliberately not pinned to one per panel. The navigation
  // tables move the selection as well - one table per direction, one per provider state - and those
  // are the *only* other place a placement may appear. A row action or an arrow that wants a
  // different row re-runs `populate_menu` instead of placing an index itself, which is what keeps
  // the drawing and the selection from drifting apart again.
  const std::vector<dtb::Node> roots = DefinitionRoots();
  int fills = 0;
  int placements = 0;
  int moving = 0;
  for (const dtb::Node& root : roots) {
    std::vector<const dtb::Node*> menus;
    CollectMembers(root, "populate_menu", &menus);
    if (menus.empty()) {
      continue;  // a screen, not a panel
    }
    CHECK_EQ(menus.size(), 1u);  // one fill per panel
    const std::vector<const dtb::Node*> statements = DoStatements(*menus.front());
    for (size_t i = 0; i < statements.size(); ++i) {
      if (!IsCall(*statements[i], "set_data")) {
        continue;
      }
      ++fills;
      const bool placed = i + 1 < statements.size() &&
                          IsCall(*statements[i + 1], "set_selected") &&
                          statements[i + 1]->kids.size() == 3u &&
                          statements[i + 1]->kids[2].tag == dtb::kInt &&
                          statements[i + 1]->kids[2].scalar == 0u;
      CHECK_TRUE(placed);  // the fill is followed by `set_selected 0`
    }

    std::vector<const dtb::Node*> calls;
    FindCalls(root, "set_selected", &calls);
    placements += static_cast<int>(calls.size());
    for (const char* name : {"move_up", "move_down"}) {
      std::vector<const dtb::Node*> members;
      CollectMembers(root, name, &members);
      CHECK_EQ(members.size(), 1u);
      for (const dtb::Node* member : members) {
        std::vector<const dtb::Node*> inside;
        FindCalls(*member, "set_selected", &inside);
        moving += static_cast<int>(inside.size());
      }
    }
  }
  CHECK_EQ(fills, 14);                   // the fourteen panels
  CHECK_EQ(placements, fills + moving);  // fills place it; the arrows are the only other movers

  // Every placement is on our own list, with an integer index.
  for (const dtb::Node& root : roots) {
    std::vector<const dtb::Node*> calls;
    FindCalls(root, "set_selected", &calls);
    for (const dtb::Node* call : calls) {
      CHECK_EQ(call->kids.size(), 3u);
      CHECK_TRUE(call->kids[0].text == "options.lst");
      CHECK_TRUE(call->kids[2].tag == dtb::kInt);
    }
  }
}

void TestButtonTableIsOneSwitch() {
  BeginCase("each button table is a single switch with its cases as direct children");

  // The bug this pins, measured in a real boot: a helper that returned a whole `{switch action ...}`
  // was spliced in as a *child* of the panel's switch, nesting one inside the other so its cases
  // were never evaluated - Start and Cancel did nothing at all, and a dump still looked plausible
  // because the outer switch was well formed.
  int tables = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    std::vector<const dtb::Node*> switches;
    CollectSwitchesOn(root, "action", &switches);
    if (switches.empty()) {
      continue;  // a screen, not a panel
    }
    CHECK_EQ(switches.size(), 1u);  // exactly one table per panel, never a nested second one
    ++tables;
    const dtb::Node& table = *switches.front();
    int starts = 0;
    int cancels = 0;
    int confirms = 0;
    for (const dtb::Node& kid : table.kids) {
      if (kid.tag != dtb::kArray || kid.kids.empty() || kid.kids[0].tag != dtb::kSymbol) {
        continue;
      }
      if (kid.kids[0].text == "kAction_Start") ++starts;
      if (kid.kids[0].text == "kAction_Cancel") ++cancels;
      if (kid.kids[0].text == "kAction_Confirm") ++confirms;
    }
    CHECK_EQ(starts, 1);     // a direct child of the table, so it can actually be selected
    CHECK_EQ(cancels, 1);
    // Measured (armS): the pad's A arrives as `kAction_Confirm`, so a table without this case drops
    // every press on a row - the failure that made A look inert. Both it and Start must reach the
    // row table, and they must reach it *directly*: forwarding to `{handle {this button_down ...}}`
    // the way retail forwards Start crashed the boot with `read of guest 0x0000000F`, because the
    // message variables that form passes are not in scope here.
    CHECK_EQ(confirms, 1);
    int selects = 0;
    for (const std::string& word : Words(*switches.front())) {
      if (word == "select_row") ++selects;
    }
    CHECK_TRUE(selects >= 2);  // Confirm and Start both call it
  }
  CHECK_EQ(tables, 14);  // the fourteen panels

  // And the row table itself exists once per panel, keyed on the list's selected symbol.
  int row_tables = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    std::vector<const dtb::Node*> members;
    CollectMembers(root, "select_row", &members);
    if (members.empty()) {
      continue;  // a screen, not a panel
    }
    CHECK_EQ(members.size(), 1u);  // exactly one row table per panel
    ++row_tables;

    std::vector<const dtb::Node*> own;
    CollectRowTables(*members.front(), &own);
    CHECK_EQ(own.size(), 1u);  // the table itself, keyed on the selected symbol

    // Every other switch on the same symbol is a navigation table. They may only live in
    // `move_up` / `move_down` - one per direction, one per provider state - or in the panel's own
    // Left/Right cases; a panel whose rows have no direction must carry none at all, or the arrows
    // would act on a row with nothing to move through.
    std::vector<const dtb::Node*> all;
    CollectRowTables(root, &all);
    int in_moves = 0;
    for (const char* name : {"move_up", "move_down"}) {
      std::vector<const dtb::Node*> moves;
      CollectMembers(root, name, &moves);
      for (const dtb::Node* move : moves) {
        std::vector<const dtb::Node*> inside;
        CollectRowTables(*move, &inside);
        in_moves += static_cast<int>(inside.size());
      }
    }
    const size_t extra = all.size() - 1u - static_cast<size_t>(in_moves);
    CHECK_EQ(extra, HasWord(root, "kAction_Left") ? 2u : 0u);  // the two arrow tables, or none
  }
  CHECK_EQ(row_tables, 14);  // one row table per panel
}

void TestLabels() {
  BeginCase("the surface's labels are complete, unique and printable");

  const std::vector<std::pair<std::string, std::string>> labels = Labels();
  CHECK_TRUE(labels.size() >= 70u);
  for (size_t i = 0; i < labels.size(); ++i) {
    CHECK_TRUE(!labels[i].first.empty());
    CHECK_TRUE(!labels[i].second.empty());
    // A locale key is a symbol the list can draw: no spaces, and none of the retired row's.
    CHECK_TRUE(labels[i].first.find(' ') == std::string::npos);
    CHECK_TRUE(labels[i].first != kRetiredRow);
    for (size_t j = i + 1; j < labels.size(); ++j) {
      CHECK_FALSE(labels[i].first == labels[j].first);
    }
  }

  std::vector<std::string> symbols;
  for (const dtb::Node& root : DefinitionRoots()) {
    CollectSymbols(root, &symbols);
  }

  // The spelling, measured in a capture: our own rows drew "GENERAL" and "GAMEPLAY" beside retail's
  // "Controls" and "Calibration", because a label of ours was typed in the main menu's capitals
  // while the retail rows beside it come from the options *page* locale strings. Every row label of
  // ours must therefore carry lowercase - with two deliberate exceptions: the Main Menu mirror, which
  // lists the menu's own rows and so mirrors the menu's own spelling ("PLAY", "EXIT GAME"), and the
  // acronyms, which are the game's own words for these things ("HUD", "FXAA").
  int checked = 0;
  for (const auto& label : labels) {
    if (label.first.rfind("settings_mm_", 0) == 0 || label.second == "HUD" ||
        label.second == "FXAA") {
      continue;
    }
    ++checked;
    if (!HasLowercase(label.second)) {
      std::printf("         label in the menu's capitals: %s = %s\n", label.first.c_str(),
                  label.second.c_str());
    }
    CHECK_TRUE(HasLowercase(label.second));
  }
  CHECK_TRUE(checked > 60);

  // No orphan label: everything the table produces is drawn by some row.
  for (const auto& label : labels) {
    CHECK_TRUE(std::find(symbols.begin(), symbols.end(), label.first) != symbols.end());
  }

  // And no unlabelled row: every `settings_*` symbol that is not a panel or a screen name is a row
  // symbol, and a row symbol the locale has no text for draws as the bare symbol.
  const std::vector<std::string> panels = PanelNames();
  const std::vector<std::string> screens = ScreenNames();
  for (const std::string& symbol : symbols) {
    if (symbol.rfind("settings_", 0) != 0) {
      continue;
    }
    if (std::find(panels.begin(), panels.end(), symbol) != panels.end() ||
        std::find(screens.begin(), screens.end(), symbol) != screens.end()) {
      continue;
    }
    bool labelled = false;
    for (const auto& label : labels) {
      if (label.first == symbol) {
        labelled = true;
        break;
      }
    }
    if (!labelled) {
      std::printf("         row symbol with no label: %s\n", symbol.c_str());
    }
    CHECK_TRUE(labelled);
  }
}

// ------------------------------------------------------------------------------------------------
// The write modes a boot is pointed at
// ------------------------------------------------------------------------------------------------

bool WriteFile(const char* path, const std::vector<uint8_t>& bytes) {
  std::ofstream handle(path, std::ios::binary);
  if (!handle) {
    std::printf("cannot write %s\n", path);
    return false;
  }
  handle.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return true;
}

bool ReadFile(const char* path, std::vector<uint8_t>* bytes) {
  std::ifstream handle(path, std::ios::binary);
  if (!handle) {
    std::printf("cannot read %s\n", path);
    return false;
  }
  bytes->assign(std::istreambuf_iterator<char>(handle), std::istreambuf_iterator<char>());
  return true;
}

// The P1-c bisection's experiment fixtures (`BareRoots`, `ScreenShareRoots`, `OtherSceneRoots`,
// `ActionProbeRoots`, `BareFileRoots`, `RetailRowsRoots`, `ScreenRetailRoots`) are gone: P5
// answered the questions they were built to ask, and nothing in the merged surface serves them.
// The evidence they produced lives in docs/plans/in-game-settings-bridge-findings.md §12.

// Appends the definition's roots to a **retail** record. This is the measured delivery site
// (docs/plans/in-game-settings-bridge-findings.md §12.3): the same panel declarations open when
// they ride inside a retail record and crash the guest with `write of guest 0x00000200` when they
// live in a record of our own reached by `[include]`.
int EmitAppend(const char* in_path, const char* out_path) {
  std::vector<uint8_t> bytes;
  if (!ReadFile(in_path, &bytes)) return 1;
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error)) {
    std::printf("cannot parse %s: %s\n", in_path, error.c_str());
    return 1;
  }
  const size_t before = nodes.size();
  for (const dtb::Node& root : DefinitionRoots()) {
    nodes.push_back(root);
  }
  const std::vector<uint8_t> file = dtb::Compile(nodes, header);
  if (!WriteFile(out_path, file)) return 1;
  std::printf("emit-append: %s -> %s (%zu -> %zu bytes, +%zu roots)\n", in_path, out_path,
              bytes.size(), file.size(), nodes.size() - before);
  return 0;
}

int EmitDef(const char* out_path) {
  const std::vector<dtb::Node> roots = DefinitionRoots();
  const std::vector<uint8_t> file = dtb::Compile(roots, dtb::RootHeader{0x52425A31, 1, 0});
  if (!WriteFile(out_path, file)) return 1;
  std::printf("emit-def: %s (%zu bytes, %zu roots)\n", out_path, file.size(), roots.size());
  return 0;
}

// `{<panel> load}` for every panel of the surface, into a retail ui/gen/ui.dtb - the way retail
// creates the panels it declares, before the first screen is shown.
int EmitUi(const char* in_path, const char* out_path) {
  std::vector<uint8_t> bytes;
  if (!ReadFile(in_path, &bytes)) return 1;
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error)) {
    std::printf("cannot parse %s: %s\n", in_path, error.c_str());
    return 1;
  }
  size_t added = 0;
  for (const std::string& panel : PanelNames()) {
    std::string why;
    if (AddPanelLoad(&nodes, panel, &why)) {
      ++added;
    } else {
      std::printf("emit-ui refused for %s: %s\n", panel.c_str(), why.c_str());
      return 1;
    }
  }
  const std::vector<uint8_t> file = dtb::Compile(nodes, header);
  if (!WriteFile(out_path, file)) return 1;
  std::printf("emit-ui: %s -> %s (%zu -> %zu bytes, %zu load(s))\n", in_path, out_path,
              bytes.size(), file.size(), added);
  return 0;
}

// The two re-points, on whichever retail record the caller passes: `emit-splash` for
// ui/splash/gen/splash.dtb, `emit-pause` for ui/pause/gen/pause.dtb.
int EmitReroute(const char* command, const char* in_path, const char* out_path) {
  std::vector<uint8_t> bytes;
  if (!ReadFile(in_path, &bytes)) return 1;
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error)) {
    std::printf("cannot parse %s: %s\n", in_path, error.c_str());
    return 1;
  }
  const bool pause = std::strcmp(command, "emit-pause") == 0;
  const bool ok = pause ? RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &error)
                        : RerouteRow(&nodes, kMainMenuRow, MainMenuRouteBody(), &error);
  if (!ok) {
    std::printf("%s refused: %s\n", command, error.c_str());
    return 1;
  }
  const std::vector<uint8_t> file = dtb::Compile(nodes, header);
  if (!WriteFile(out_path, file)) return 1;
  std::printf("%s: %s -> %s (%zu -> %zu bytes)\n", command, in_path, out_path, bytes.size(),
              file.size());
  return 0;
}

// Appends every label to a locale file. A key the installed locale already carries is skipped, not
// refused, so the mode is safe to run twice.
int EmitLocale(const char* in_path, const char* out_path) {
  std::vector<uint8_t> bytes;
  if (!ReadFile(in_path, &bytes)) return 1;
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error)) {
    std::printf("cannot parse %s: %s\n", in_path, error.c_str());
    return 1;
  }
  size_t added = 0;
  for (const auto& label : Labels()) {
    std::string why;
    if (AddLocaleEntry(&nodes, label.first, label.second, &why)) {
      ++added;
    } else if (why != "the locale already has that key") {
      std::printf("locale add refused for %s: %s\n", label.first.c_str(), why.c_str());
      return 1;
    }
  }
  const std::vector<uint8_t> file = dtb::Compile(nodes, header);
  if (!WriteFile(out_path, file)) return 1;
  std::printf("emit-locale: %s -> %s (%zu -> %zu bytes, %zu added)\n", in_path, out_path,
              bytes.size(), file.size(), added);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "emit-def") == 0) {
    return EmitDef(argv[2]);
  }
  if (argc >= 4 && std::strcmp(argv[1], "emit-append") == 0) {
    return EmitAppend(argv[2], argv[3]);
  }
  if (argc >= 4 && std::strcmp(argv[1], "emit-ui") == 0) {
    return EmitUi(argv[2], argv[3]);
  }
  if (argc >= 4 &&
      (std::strcmp(argv[1], "emit-splash") == 0 || std::strcmp(argv[1], "emit-pause") == 0)) {
    return EmitReroute(argv[1], argv[2], argv[3]);
  }
  if (argc >= 4 && std::strcmp(argv[1], "emit-locale") == 0) {
    return EmitLocale(argv[2], argv[3]);
  }

  TestDefinitionRoundTrip();
  TestMergedRootShape();
  TestListOwnership();
  TestScreenBackTargets();
  TestRequestPathsParse();
  TestPanelLoadEdit();
  TestSplashReroute();
  TestPauseReroute();
  TestRerouteRefusesWithoutTheCase();
  TestDoTakesStatementsDirectly();
  TestLocaleEntry();
  TestEveryFillPlacesTheSelection();
  TestButtonTableIsOneSwitch();
  TestLabels();
  return Finish();
}
