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
#include "ui/help_topics.h"
#include "ui/settings_screen.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
  // Retail's own cases for the rows of the inline list. `pause_controls` is the anchor the merge
  // uses to find that table (it is a row of `options.lst` and of nothing else).
  const Node tutorial_case =
      Arr({Sym("pause_tutorial"), Cmd({Sym("ui"), Sym("goto_screen"), Sym("tutorials_pause_screen")})});
  const Node controls_case = Arr(
      {Sym("pause_controls"), Cmd({Sym("ui"), Sym("goto_screen"), Sym("controller_config_pause_screen")})});
  const Node credits_case =
      Arr({Sym("credits"), Cmd({Sym("ui"), Sym("goto_screen"), Sym("pause_credits_screen")})});
  const Node audio_video_case =
      Arr({Sym("audio_video_options"), Cmd({Sym("transition_to"), Sym("av_options_pause_screen")})});
  const Node table =
      Cmd({Sym("switch"), value, resume_case, options_case, back_case, tutorial_case, controls_case,
           audio_video_case, credits_case});
  // `{enter {do {this populate_menu} {options.lst set_data (retail's five)} {this update_focus
  // {get_focus}}}}` - retail's own enter, transcribed from the shipped pause record.
  const Node fill = Cmd({Sym("options.lst"), Sym("set_data"),
                         Arr({Sym("pause_tutorial"), Sym("pause_controls"), Sym("calibration"),
                              Sym("audio_video_options"), Sym("credits")})});
  const Node enter = Arr({Sym("enter"), Cmd({Sym("do"), Cmd({Var("this"), Sym("populate_menu")}),
                                             fill,
                                             Cmd({Var("this"), Sym("update_focus"),
                                                  dtb::Node::Property({Sym("get_focus")})})})});
  return {
      Cmd({Sym("new"), Sym("UIPanel"), Sym("pause_panel"), Arr({Sym("file"), Str("pause.milo")}),
           Arr({Sym("focus"), Sym("pause.lst")}), enter, Arr({Sym("SELECT_MSG"), table})}),
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

// True when some leaf *ends with* `suffix`. The context twin's name is a suffix of a longer leaf, so
// an exact-match walk cannot see it.
bool HasTextSuffix(const dtb::Node& root, const std::string& suffix) {
  std::vector<std::string> texts;
  CollectText(root, &texts);
  for (const std::string& text : texts) {
    if (text.size() >= suffix.size() &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return true;
    }
  }
  return false;
}

// Every array node under a file, in document order.
void CollectArrays(const std::vector<dtb::Node>& nodes, std::vector<const dtb::Node*>* out) {
  for (const dtb::Node& root : nodes) {
    std::vector<const dtb::Node*> stack{&root};
    while (!stack.empty()) {
      const dtb::Node* node = stack.back();
      stack.pop_back();
      if (node->tag == dtb::kArray) {
        out->push_back(node);
      }
      for (const dtb::Node& kid : node->kids) {
        stack.push_back(&kid);
      }
    }
  }
}

// Every word inside the `[<row> <body...>]` case that dispatches `row`, so a test can assert what a
// *case* does without the rest of the record's vocabulary getting in the way.
std::vector<std::string> CaseWords(const std::vector<dtb::Node>& nodes, const std::string& row) {
  std::vector<std::string> out;
  std::vector<const dtb::Node*> found;
  CollectArrays(nodes, &found);
  for (const dtb::Node* node : found) {
    if (node->kids.size() < 2 || node->kids[0].tag != dtb::kSymbol ||
        node->kids[0].text != row) {
      continue;
    }
    if (node->kids[1].tag != dtb::kArray && node->kids[1].tag != dtb::kCommand &&
        node->kids[1].tag != dtb::kProperty) {
      continue;  // a row *list* holds bare symbols, not statements
    }
    CollectText(*node, &out);
  }
  return out;
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

// Every word inside the `[<case> ...]` array of one specific switch table - unlike CaseWords,
// which merges every array in a tree that happens to start with the row symbol (a row table and
// its direction tables share their case symbols).
std::vector<std::string> SwitchCaseWords(const dtb::Node& table, const std::string& row) {
  std::vector<std::string> out;
  for (const dtb::Node& kid : table.kids) {
    if (kid.tag == dtb::kArray && !kid.kids.empty() && kid.kids[0].tag == dtb::kSymbol &&
        kid.kids[0].text == row) {
      CollectText(kid, &out);
    }
  }
  return out;
}

// The `{switch <var action> ...}` table of a panel definition (nullptr on a screen or a table-less
// panel). Matches every context: the main-menu page and its in-song twin.
dtb::Node* FindButtonTable(dtb::Node* root) {
  if (root->tag == dtb::kCommand && root->kids.size() >= 3 && root->kids[0].tag == dtb::kSymbol &&
      root->kids[0].text == "switch" && root->kids[1].tag == dtb::kVar &&
      root->kids[1].text == "action") {
    return root;
  }
  for (dtb::Node& kid : root->kids) {
    if (kid.tag != dtb::kArray && kid.tag != dtb::kCommand && kid.tag != dtb::kProperty) continue;
    if (dtb::Node* hit = FindButtonTable(&kid)) return hit;
  }
  return nullptr;
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
  // Twenty panels and twenty screens per context - the main-menu one and the in-song one - so
  // 2 * 20 * 2 roots. The two contexts are a pair of declarations each, not one shared tree: a panel
  // whose bodies name a destination (a submenu must) cannot be hosted by two contexts, because the
  // destination differs between them (P5b-r2).
  CHECK_EQ(authored.size(), 80u);

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
  CHECK_EQ(panels.size(), 40u);  // twenty pages, in each of the two contexts
  CHECK_TRUE(panels.front() == kRootPanel);

  const std::vector<std::string> screens = ScreenNames();
  CHECK_EQ(screens.size(), 40u);
  CHECK_TRUE(screens.front() == kRootScreen);
  // The contexts pair up one for one, in declaration order: the i-th screen's in-song twin is the
  // (i + 20)-th. That is what makes `InSongDestination` total - every page a row can name has a twin
  // to send it to - and it is also the reason the pair sits adjacent in the record.
  CHECK_EQ(screens.size(), panels.size());
  for (size_t i = 0; i + panels.size() / 2 < screens.size(); ++i) {
    CHECK_TRUE(PauseScreen(screens[i]) == screens[i + panels.size() / 2]);
    CHECK_TRUE(PausePanel(panels[i]) == panels[i + panels.size() / 2]);
  }

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
  // surface has to name a project-owned scene instead.
  //
  // And *each context* has to name a different one. The engine caches a loaded dir by path, so two
  // distinct panels naming one scene are handed one dir and one list object; retail avoids the
  // question by hosting a single panel object in both contexts, which ours cannot do because a row
  // body names its destination and the destinations differ (measured: entering an in-song page whose
  // panel named the main-menu scene faulted the guest with `read of guest 0x00000024`, before the
  // panel's own `{enter}` ran - bridge findings §12.4).
  size_t main_menu = 0;
  size_t in_song = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    const std::vector<std::string> words = Words(root);
    if (std::find(words.begin(), words.end(), "UIPanel") == words.end()) {
      continue;  // a screen, not a panel
    }
    CHECK_TRUE(HasText(root, kListName));
    CHECK_FALSE(HasText(root, "options_menu.milo"));
    const bool pause = HasText(root, kPauseSceneName) || HasText(root, "../pause/settings_confirmation_pause.milo");
    CHECK_TRUE(pause || HasText(root, kSceneName) || HasText(root, "../pause/settings_confirmation.milo"));
    (pause ? in_song : main_menu) += 1;
  }
  CHECK_EQ(main_menu, 20);
  CHECK_EQ(in_song, 20);

  // The copies have to live **beside** the retail scene, because that scene refers to its neighbours
  // by relative path (`neon_lights.milo`, `../resource/lists/list_options_main.milo`), and those
  // resolve against the scene's own directory.
  const std::string scene = kSceneRecord;
  const std::string source = kSceneSourceRecord;
  CHECK_TRUE(scene.substr(0, scene.rfind('/')) == source.substr(0, source.rfind('/')));
  CHECK_TRUE(scene != source);
  const std::string pause_scene = kPauseSceneRecord;
  CHECK_TRUE(pause_scene.substr(0, pause_scene.rfind('/')) ==
             source.substr(0, source.rfind('/')));
  CHECK_TRUE(pause_scene != scene);
}

void TestScreenBackTargets() {
  BeginCase("each screen declares a back target and every page is hosted in both contexts");

  const std::vector<dtb::Node> roots = DefinitionRoots();
  for (const dtb::Node& root : roots) {
    const std::vector<std::string> words = Words(root);
    if (std::find(words.begin(), words.end(), "UIScreen") == words.end()) {
      continue;  // a panel
    }
    CHECK_TRUE(HasText(root, "back_screen"));
  }

  // Every in-song screen keeps the running game and the pause panel on screen - retail's own
  // `av_options_pause_screen` / `pause_credits_screen` shape. That is not cosmetic: the in-song
  // state machine reads a list on the pause panel, so a page that dropped it would leave that
  // machine with nothing to read. Only the *root's* twin returns to `game_screen`; a child's Back is
  // pointed at whoever sent it (`EnterScreen`), which is why its declaration's own back target is
  // the parent's twin.
  int in_song = 0;
  int root_twins = 0;
  for (const dtb::Node& root : roots) {
    const std::vector<std::string> words = Words(root);
    if (std::find(words.begin(), words.end(), "UIScreen") == words.end()) {
      continue;
    }
    if (!HasTextSuffix(root, "_pause_screen")) {
      continue;
    }
    ++in_song;
    CHECK_TRUE(HasText(root, "game_panel"));
    CHECK_TRUE(HasText(root, "pause_panel"));
    CHECK_TRUE(HasText(root, "background_night_panel"));
    // `{new UIScreen <name> ...}` - the name is the third child, so the root's twin is exact and a
    // screen that merely *names* it (as a back target) is not mistaken for it.
    if (root.kids.size() >= 3 && root.kids[2].tag == dtb::kSymbol &&
        root.kids[2].text == kRootPauseScreen) {
      ++root_twins;
      CHECK_TRUE(HasText(root, "game_screen"));
    }
  }
  CHECK_EQ(in_song, 20);  // one per page, the root included
  CHECK_EQ(root_twins, 1);
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
    // A commit carries no setting and no value; every other request addresses one - except the
    // caption event, whose setting is empty precisely when it says "no row" (the hide).
    if (request.op == Op::kCommit || request.op == Op::kRequestRestart ||
        request.op == Op::kCancelEdit || request.op == Op::kHelp) {
      CHECK_TRUE(request.value.kind == ValueKind::kNone);
      if (request.op != Op::kHelp) {
        CHECK_TRUE(request.setting.empty());
      }
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
  // The confirmed achievement action executes the provider's guest API directly.
  // It must not route to the host Invoke stub or shadow the Unlock All submenu.
  CHECK_FALSE(has_op(Op::kInvoke));
  std::vector<std::string> declared;
  for (const auto& root : DefinitionRoots()) {
    if (root.kids.size() < 3 || root.kids[0].text != "new") continue;
    CHECK_TRUE(std::find(declared.begin(), declared.end(), root.kids[2].text) == declared.end());
    declared.push_back(root.kids[2].text);
  }
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
  // The switch's *other* cases do jump (retail's own `pause_controls` / `credits` rows), which is
  // what the merge uses; this row is the one that must not.
  const std::vector<std::string> body = CaseWords(nodes, kPauseMenuRow);
  CHECK_TRUE(std::find(body.begin(), body.end(), "options.lst") != body.end());
  CHECK_TRUE(std::find(body.begin(), body.end(), "update_focus") != body.end());
  CHECK_TRUE(std::find(body.begin(), body.end(), "goto_screen") == body.end());
  CHECK_TRUE(std::find(body.begin(), body.end(), "transition_to") == body.end());
  CHECK_TRUE(std::find(body.begin(), body.end(), kRootPauseScreen) == body.end());
  // The panel's own Back row for the inline list is untouched, which is what makes the retail in-song
  // page still usable while the merge of that list's contents is pending.
  CHECK_TRUE(std::find(words.begin(), words.end(), "pause_options_back") != words.end());

  std::string error;
  CHECK_TRUE(Reparse(nodes, &error).size() == nodes.size());
}

void TestInSongContext() {
  BeginCase("every page has an in-song twin, and the in-song list is the merged root's own rows");

  // The name mapping is the whole mechanism: a page reached from inside a song is a different screen
  // (same panel *definition*, in-song name) because its bodies must point at the in-song twins below
  // it. Retail does the same thing by hand (`av_options_pause_screen`, `pause_credits_screen`).
  CHECK_TRUE(PauseScreen("settings_general_screen") == "settings_general_pause_screen");
  CHECK_TRUE(PauseScreen("settings_screen") == kRootPauseScreen);
  CHECK_TRUE(PausePanel("settings_panel") == "settings_pause_panel");
  CHECK_TRUE(PausePanel("settings_general_panel") == "settings_general_pause_panel");

  // Retail's own in-song twins are named by retail, not by the suffix rule.
  CHECK_TRUE(InSongDestination("controller_config_screen") == "controller_config_pause_screen");
  CHECK_TRUE(InSongDestination("av_options_screen") == "av_options_pause_screen");
  CHECK_TRUE(InSongDestination("credits_screen") == "pause_credits_screen");
  CHECK_TRUE(InSongDestination("tutorials_screen") == "tutorials_pause_screen");

  // The in-song rows: the *same* symbols the main-menu root uses, except the Controls row, which
  // retail's pause switch spells `pause_controls` (requesting that spelling is what reuses retail's
  // own case instead of adding a second body for the same page).
  const std::vector<std::string> six = PauseRootRows(true);
  const std::vector<std::string> five = PauseRootRows(false);
  CHECK_EQ(six.size(), 6u);
  CHECK_EQ(five.size(), 5u);
  CHECK_TRUE(six[0] == "settings_general");
  CHECK_TRUE(six[1] == "pause_controls");
  CHECK_TRUE(six[2] == "calibration");
  CHECK_TRUE(six[3] == "audio_video_options");
  CHECK_TRUE(six[4] == "settings_gameplay");
  CHECK_TRUE(six[5] == "settings_interface");
  // A base install must not be offered a dead row: only the provider row moves.
  CHECK_TRUE(std::find(five.begin(), five.end(), "settings_gameplay") == five.end());
  CHECK_TRUE(five[4] == "settings_interface");

  // A row of ours in song jumps to its twin and points that twin's Back at whoever sent us.
  const dtb::Node body = Cmd({InSongRowBody("settings_general_screen")});
  CHECK_TRUE(HasText(body, "settings_general_pause_screen"));
  CHECK_FALSE(HasText(body, "settings_general_screen"));  // never the main-menu screen
  CHECK_TRUE(HasText(body, "back_screen"));
  CHECK_TRUE(HasText(body, "current_screen"));

  // Two invariants that together say the in-song context is closed: every main-menu screen has a
  // twin to land on, and no in-song panel names a main-menu destination (which is what would send a
  // page in a song back out to the main-menu tree).
  const std::vector<std::string> screens = ScreenNames();
  std::vector<std::string> menu_screens;
  for (const std::string& screen : screens) {
    if (screen.find("_pause_screen") != std::string::npos) {
      continue;
    }
    menu_screens.push_back(screen);
    CHECK_TRUE(std::find(screens.begin(), screens.end(), PauseScreen(screen)) != screens.end());
  }
  CHECK_EQ(menu_screens.size(), 20u);
  int in_song_panels = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    const std::vector<std::string> words = Words(root);
    if (std::find(words.begin(), words.end(), "UIPanel") == words.end() ||
        !HasTextSuffix(root, "_pause_panel")) {
      continue;
    }
    ++in_song_panels;
    for (const std::string& screen : menu_screens) {
      CHECK_FALSE(HasText(root, screen));
    }
  }
  CHECK_EQ(in_song_panels, 20);

  // The two contexts' panel sets are retail's own, and the difference between them is
  // load-bearing - measured, not stylistic. Retail's in-song page screens that are entered with a
  // plain `{ui goto_screen ...}` (`controller_config_pause_screen`, `pause_credits_screen`) all
  // declare `game_fade_out_panel`, and the one in-song page without it is entered with
  // `{transition_to ...}` (`av_options_pause_screen`). A screen of ours without the fade panel,
  // reached by a plain goto from the pause menu's inline list, faults the guest with
  // `read of guest 0x00000024` during the switch (P5b-r2, `out/_swap/p5b/r2long.log`), and the same
  // screen with it enters cleanly. The main-menu twin must *not* carry it: that is the main-menu set.
  bool seen_in_song_screen = false;
  bool seen_menu_screen = false;
  for (const dtb::Node& root : DefinitionRoots()) {
    if (!HasText(root, "UIScreen")) {
      continue;
    }
    const bool in_song = HasText(root, "settings_general_pause_screen");
    const bool menu = HasText(root, "settings_general_screen");
    if (!in_song && !menu) {
      continue;
    }
    if (in_song) {
      seen_in_song_screen = true;
      CHECK_TRUE(HasText(root, "game_panel"));
      CHECK_TRUE(HasText(root, "pause_panel"));
      CHECK_TRUE(HasText(root, "game_fade_out_panel"));
      CHECK_TRUE(HasText(root, "background_night_panel"));
    } else {
      seen_menu_screen = true;
      CHECK_FALSE(HasText(root, "game_fade_out_panel"));
      CHECK_FALSE(HasText(root, "pause_panel"));
    }
  }
  CHECK_TRUE(seen_in_song_screen);
  CHECK_TRUE(seen_menu_screen);
}

void TestPauseMergeEdits() {
  BeginCase("the in-song list is merged, and only a record that still holds retail's five is");

  // A record without the fill (or without the switch) is refused rather than half-rewritten: the
  // shipped pause record is retail's, and a merge that guessed would break the one in-song page that
  // currently works.
  {
    std::vector<dtb::Node> bare = {Cmd({Sym("new"), Sym("UIPanel"), Sym("pause_panel"),
                                        Arr({Sym("file"), Str("pause.milo")})})};
    std::string reason;
    CHECK_FALSE(MergePauseListRows(&bare, &reason));
    CHECK_TRUE(!reason.empty());
    CHECK_FALSE(AddPauseSwitchCase(&bare, "settings_general", MainMenuRouteBody(), &reason));
    CHECK_TRUE(!reason.empty());
  }

  std::vector<dtb::Node> nodes = PauseFixture();
  std::string reason;

  // The fill becomes a gate-aware pair: the six-row list where the provider is active, the five-row
  // one where it is not.
  CHECK_TRUE(MergePauseListRows(&nodes, &reason));
  CHECK_TRUE(reason.empty());
  const std::vector<std::string> words = Words(nodes[0]);
  CHECK_TRUE(std::find(words.begin(), words.end(), "if_else") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "file_exists") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), kProviderQuery) != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "settings_general") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "settings_interface") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "settings_gameplay") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "pause_controls") != words.end());
  // Retail's five are still named (both branches carry the ones that survive), and the rows retail
  // had but the merge folds into General are gone from the *list*.
  CHECK_TRUE(std::find(words.begin(), words.end(), "pause_tutorial") != words.end());
  CHECK_TRUE(std::find(words.begin(), words.end(), "credits") != words.end());

  // Running it twice refuses: the record no longer holds retail's five-symbol fill, which is the
  // guard that keeps a second pass from eating the first one's work.
  CHECK_FALSE(MergePauseListRows(&nodes, &reason));
  CHECK_TRUE(!reason.empty());

  // The cases our three rows need. Each is appended to the switch that already dispatches retail's
  // rows, so it runs in the pause panel's own context - the context retail's `pause_controls` and
  // `credits` cases jump from, which is the one that works.
  const char* rows[] = {"settings_general", "settings_gameplay", "settings_interface"};
  const char* screens[] = {"settings_general_screen", "settings_gameplay_screen",
                           "settings_interface_screen"};
  for (size_t i = 0; i < 3; ++i) {
    CHECK_TRUE(AddPauseSwitchCase(&nodes, rows[i], InSongRowBody(screens[i]), &reason));
    CHECK_TRUE(reason.empty());
  }
  // A second case for the same row would be dead code a dump cannot tell from a live one, so it is
  // refused instead.
  CHECK_FALSE(AddPauseSwitchCase(&nodes, "settings_general", InSongRowBody("settings_general_screen"),
                                 &reason));
  CHECK_TRUE(!reason.empty());
  // Re-pointing a row retail already dispatches is RerouteRow's job, and that is how the Audio/Video
  // row moves from retail's page to the merged one.
  CHECK_TRUE(RerouteRow(&nodes, "audio_video_options",
                        InSongRowBody("settings_av_screen"), &reason));

  const std::vector<std::string> after = Words(nodes[0]);
  for (const char* screen : screens) {
    // A row of ours lands on the twin, never on the main-menu screen of the same page.
    CHECK_TRUE(std::find(after.begin(), after.end(), PauseScreen(screen)) != after.end());
    CHECK_FALSE(std::find(after.begin(), after.end(), screen) != after.end());
  }
  CHECK_TRUE(std::find(after.begin(), after.end(), "settings_av_pause_screen") != after.end());

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
                          statements[i + 1]->kids[2].tag == dtb::kVar &&
                          statements[i + 1]->kids[2].text == "rbbz_settings_index";
      CHECK_TRUE(placed);  // edits preserve the focused row when its label changes
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
  CHECK_EQ(fills, 40);                   // the twenty panels, in both contexts
  CHECK_EQ(placements, fills + moving);  // fills place it; the arrows are the only other movers

  // Every placement is on our own list, with an integer index.
  for (const dtb::Node& root : roots) {
    std::vector<const dtb::Node*> calls;
    FindCalls(root, "set_selected", &calls);
    for (const dtb::Node* call : calls) {
      CHECK_EQ(call->kids.size(), 3u);
      CHECK_TRUE(call->kids[0].text == "options.lst");
      CHECK_TRUE(call->kids[2].tag == dtb::kVar);
    }
  }
}

// `{new UIScreen <name> {panels <panel>...} {focus <panel>} {back_screen <screen>}}` - the three
// facts a screen declares, read positionally rather than by searching the root for text, so a screen
// that merely *names* a symbol is never mistaken for the declaration that owns it.
struct ScreenFacts {
  std::string name;
  std::vector<std::string> panels;
  std::string focus;
  std::string back;
};

bool ReadScreenFacts(const dtb::Node& root, ScreenFacts* out) {
  if (root.tag != dtb::kCommand || root.kids.size() < 3 || root.kids[0].tag != dtb::kSymbol ||
      root.kids[0].text != "new" || root.kids[1].text != "UIScreen") {
    return false;
  }
  out->name = root.kids[2].text;
  for (size_t i = 3; i < root.kids.size(); ++i) {
    const dtb::Node& member = root.kids[i];
    if (member.tag != dtb::kArray || member.kids.size() < 2 || member.kids[0].tag != dtb::kSymbol) {
      continue;
    }
    if (member.kids[0].text == "panels") {
      out->panels.clear();
      for (size_t k = 1; k < member.kids.size(); ++k) {
        out->panels.push_back(member.kids[k].text);
      }
    } else if (member.kids[0].text == "focus") {
      out->focus = member.kids[1].text;
    } else if (member.kids[0].text == "back_screen") {
      out->back = member.kids[1].text;
    }
  }
  return true;
}

// Every `{ui goto_screen <name>}` in an authored tree. The context-free form retail's own shared
// panels use - `{ui goto_screen {{ui current_screen} get back_screen}}` - carries a command in that
// position rather than a symbol, so it is deliberately not collected: it resolves at run time.
void CollectScreenTargets(const dtb::Node& node, std::vector<std::string>* out) {
  if (node.tag == dtb::kCommand && node.kids.size() >= 3 && node.kids[0].tag == dtb::kSymbol &&
      node.kids[0].text == "ui" && node.kids[1].text == "goto_screen" &&
      node.kids[2].tag == dtb::kSymbol) {
    out->push_back(node.kids[2].text);
  }
  for (const dtb::Node& kid : node.kids) {
    CollectScreenTargets(kid, out);
  }
}

void TestEveryDestinationExists() {
  BeginCase("every panel and destination an authored tree names is one this payload provides");

  // What the *installed* records own: retail's own pages (including the four in-song twins a merged
  // row can be sent to, and the screens the pause state machine drives) and the retail panels our
  // screens share with it. Nothing here is ours, so a name outside both lists is a name no
  // declaration provides - which is the shape of a screen or panel that dereferences as null.
  const std::vector<std::string> retail_screens = {
      "game_screen",
      "splash_screen",
      "options_screen",
      "credits_screen",
      "tutorials_screen",
      "av_options_screen",
      "controller_config_screen",
      "cal_welcome_screen",
      "pause_credits_screen",
      "tutorials_pause_screen",
      "av_options_pause_screen",
      "controller_config_pause_screen",
      "pause_confirm_quit_song_screen",
      "confirm_exit_game_screen"};
  const std::vector<std::string> retail_panels = {"game_panel",
                                                  "pause_panel",
                                                  "game_fade_out_panel",
                                                  "background_night_panel",
                                                  "shell_music_panel",
                                                  "menu_cache_panel",
                                                  "options_panel",
                                                  "av_options_panel",
                                                  "controller_config_panel",
                                                  "credits_panel"};

  auto known = [](const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
  };
  const std::vector<std::string> our_screens = ScreenNames();
  const std::vector<std::string> our_panels = PanelNames();
  auto screen_known = [&](const std::string& name) {
    return known(our_screens, name) || known(retail_screens, name);
  };
  auto panel_known = [&](const std::string& name) {
    return known(our_panels, name) || known(retail_panels, name);
  };

  int screens = 0;
  int targets = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    ScreenFacts facts;
    if (ReadScreenFacts(root, &facts)) {
      ++screens;
      // A screen's focus is one of the panels it hosts, and every panel it hosts has a declaration.
      if (!known(facts.panels, facts.focus)) {
        std::printf("         %s focuses %s, which it does not host\n", facts.name.c_str(),
                    facts.focus.c_str());
        CHECK_TRUE(false);
      }
      for (const std::string& panel : facts.panels) {
        if (!panel_known(panel)) {
          std::printf("         %s hosts unknown panel %s\n", facts.name.c_str(), panel.c_str());
          CHECK_TRUE(false);
        }
      }
      if (!screen_known(facts.back)) {
        std::printf("         %s returns to unknown screen %s\n", facts.name.c_str(),
                    facts.back.c_str());
        CHECK_TRUE(false);
      }
      continue;
    }
    std::vector<std::string> gotos;
    CollectScreenTargets(root, &gotos);
    for (const std::string& target : gotos) {
      ++targets;
      if (!screen_known(target)) {
        std::printf("         a row body jumps to unknown screen %s\n", target.c_str());
        CHECK_TRUE(false);
      }
    }
  }
  CHECK_EQ(screens, 40);
  // Every row that enters a screen of ours contributes one `{ui goto_screen <name>}`; the count is
  // reported rather than guessed at, because a change of row kind moves it.
  std::printf("         %d screen(s), %d goto_screen target(s), all known\n", screens, targets);
  CHECK_TRUE(targets > 20);
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
  CHECK_EQ(tables, 40);  // the twenty panels, in both contexts

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
  CHECK_EQ(row_tables, 40);  // one row table per panel, in both contexts
}

void TestSelectAdvancesAndWraps() {
  BeginCase("a select row advances on Confirm and wraps at the last choice");

  // Owner decision, 2026-10-13: A on a horizontal selector steps to the next option and wraps at
  // the end, instead of staging the row and waiting for a second press. The Display row is a
  // three-choice select, so both the step and the wrap are visible in one page per context.
  const std::vector<dtb::Node> roots = DefinitionRoots();
  int pages = 0;
  for (const dtb::Node& root : roots) {
    if (root.tag != dtb::kCommand || root.kids.size() < 3 ||
        root.kids[2].text != "settings_display_panel" &&
            root.kids[2].text != "settings_display_pause_panel") {
      continue;
    }
    ++pages;
    std::vector<const dtb::Node*> members;
    CollectMembers(root, "select_row", &members);
    CHECK_EQ(members.size(), 1u);
    if (members.empty()) continue;
    std::vector<const dtb::Node*> tables;
    CollectRowTables(*members.front(), &tables);
    CHECK_EQ(tables.size(), 1u);
    if (tables.empty()) continue;
    const dtb::Node& table = *tables.front();

    const char* step[] = {
        "settings_display_windowed", "settings_display_borderless",
        "settings_display_fullscreen",
    };
    const char* next[] = {
        "rbbz-settings/set/graphics.display/t:borderless",
        "rbbz-settings/set/graphics.display/t:fullscreen",
        "rbbz-settings/set/graphics.display/t:windowed",  // wrap
    };
    for (size_t i = 0; i < 3; ++i) {
      const std::vector<std::string> words = SwitchCaseWords(table, step[i]);
      CHECK_TRUE(!words.empty());
      CHECK_TRUE(std::find(words.begin(), words.end(), next[i]) != words.end());
      CHECK_TRUE(std::find(words.begin(), words.end(), "rbbz-settings/commit/-/-") != words.end());
      // The staged-edit machine this replaces is gone.
      CHECK_TRUE(std::find(words.begin(), words.end(), "rbbz_settings_editing") == words.end());
      CHECK_TRUE(std::find(words.begin(), words.end(), "rbbz-settings/cancel/-/-") == words.end());
    }

    // The arrows make the same one-step change, and commit it the same way.
    dtb::Node& mutable_root = const_cast<dtb::Node&>(root);
    dtb::Node* buttons = FindButtonTable(&mutable_root);
    CHECK_TRUE(buttons != nullptr);
    if (buttons != nullptr) {
      for (const char* key : {"kAction_Left", "kAction_Right"}) {
        const std::vector<std::string> words = SwitchCaseWords(*buttons, key);
        CHECK_TRUE(!words.empty());
        CHECK_TRUE(std::find(words.begin(), words.end(), "rbbz-settings/set/graphics.display/") != words.end() ||
                   std::any_of(words.begin(), words.end(), [](const std::string& w) {
                     return w.rfind("rbbz-settings/set/graphics.display/", 0) == 0;
                   }));
        CHECK_TRUE(std::find(words.begin(), words.end(), "rbbz-settings/commit/-/-") != words.end());
      }
    }
  }
  CHECK_EQ(pages, 2);  // both contexts
}

void TestRestartPromptOnlyOnLeave() {
  BeginCase("the restart prompt is the merged root's Cancel, not each commit");

  // Owner decision, 2026-10-13: changing a row commits it silently; the prompt is asked once, when
  // the player leaves Help & Options. So the root panels' Cancel asks `restart.pending`, every
  // other panel just leaves, and no row case mentions the per-commit `restart_required` any more.
  const std::vector<dtb::Node> roots = DefinitionRoots();
  int panels = 0;
  int root_cancels = 0;
  int other_cancels = 0;
  for (const dtb::Node& root : roots) {
    std::vector<const dtb::Node*> members;
    CollectMembers(root, "select_row", &members);
    if (members.empty()) continue;  // a screen, not a panel
    ++panels;
    const bool is_root = root.kids[2].text == kRootPanel || root.kids[2].text == "settings_pause_panel";
    dtb::Node& mutable_root = const_cast<dtb::Node&>(root);
    dtb::Node* buttons = FindButtonTable(&mutable_root);
    CHECK_TRUE(buttons != nullptr);
    if (buttons != nullptr) {
      const std::vector<std::string> cancel = SwitchCaseWords(*buttons, "kAction_Cancel");
      CHECK_TRUE(!cancel.empty());
      const bool asks =
          std::find(cancel.begin(), cancel.end(), "rbbz-settings/snapshot/restart.pending/b:1") !=
          cancel.end();
      CHECK_EQ(asks, is_root);
      CHECK_TRUE(std::find(cancel.begin(), cancel.end(), "shell_sfx") != cancel.end());
      (is_root ? root_cancels : other_cancels) += 1;
    }

    // Nothing anywhere in the panel still reads the per-commit flag.
    CHECK_FALSE(HasText(root, "commit.restart_required"));
    CHECK_FALSE(HasText(root, "rbbz-settings/cancel/-/-"));
  }
  CHECK_EQ(panels, 40);
  CHECK_EQ(root_cancels, 2);
  CHECK_EQ(other_cancels, 38);
}

void TestInvertedBoolRow() {
  BeginCase("an inverted boolean reads On for a false setting and writes the flip");

  // `Depth of Field: On` is the provider's `disable_dof == false` (owner decision, 2026-10-13).
  // The symbol pair keeps its usual meaning, so the *query* and the *write* are the flipped ones.
  const std::vector<dtb::Node> roots = DefinitionRoots();
  int pages = 0;
  for (const dtb::Node& root : roots) {
    if (root.tag != dtb::kCommand || root.kids.size() < 3 ||
        root.kids[2].text != "settings_graphics_panel" &&
            root.kids[2].text != "settings_graphics_pause_panel") {
      continue;
    }
    ++pages;
    std::vector<const dtb::Node*> menus;
    CollectMembers(root, "populate_menu", &menus);
    CHECK_EQ(menus.size(), 1u);
    if (!menus.empty()) {
      const std::vector<std::string> words = Words(*menus.front());
      const auto has = [&words](const char* text) {
        return std::find(words.begin(), words.end(), text) != words.end();
      };
      CHECK_TRUE(has("rbbz-settings/snapshot/gameplay.disable_dof/b:0"));
      CHECK_FALSE(has("rbbz-settings/snapshot/gameplay.disable_dof/b:1"));
    }
    std::vector<const dtb::Node*> members;
    CollectMembers(root, "select_row", &members);
    if (members.empty()) continue;
    std::vector<const dtb::Node*> tables;
    CollectRowTables(*members.front(), &tables);
    if (tables.empty()) continue;
    const std::vector<std::string> on = SwitchCaseWords(*tables.front(), "settings_dof_on");
    const std::vector<std::string> off = SwitchCaseWords(*tables.front(), "settings_dof_off");
    CHECK_TRUE(std::find(on.begin(), on.end(), "rbbz-settings/set/gameplay.disable_dof/b:1") !=
               on.end());
    CHECK_TRUE(std::find(off.begin(), off.end(), "rbbz-settings/set/gameplay.disable_dof/b:0") !=
               off.end());
  }
  CHECK_EQ(pages, 2);
}

// Adds the provider's row to a splash fixture: the case into the row switch, the symbol into the
// literal option array. Recursive, because the array command sits inside the `{do ...}` of `enter`.
void AddProviderRowToFixture(dtb::Node* node, bool* case_added, bool* row_added) {
  if (node->tag == dtb::kCommand && node->kids.size() >= 2 && node->kids[0].tag == dtb::kSymbol) {
    if (node->kids[0].text == "switch") {
      node->kids.push_back(Arr({Sym(kProviderMenuRow),
                                Cmd({Sym("ui"), Sym("goto_screen"), Sym("mod_settings_screen")})}));
      *case_added = true;
    } else if (node->kids[0].text == "array" && node->kids[1].tag == dtb::kArray) {
      node->kids[1].kids.push_back(Sym(kProviderMenuRow));
      *row_added = true;
    }
  }
  for (dtb::Node& kid : node->kids) {
    AddProviderRowToFixture(&kid, case_added, row_added);
  }
}

void TestProviderRowRemoval() {
  BeginCase("the provider's own menu row is removed from its case table and option list");

  // The fixture's switch and option array, given the row a provider's splash record carries.
  std::vector<dtb::Node> nodes = SplashFixture();
  bool case_added = false;
  bool row_added = false;
  AddProviderRowToFixture(&nodes[0], &case_added, &row_added);
  CHECK_TRUE(case_added);
  CHECK_TRUE(row_added);

  std::string reason;
  CHECK_TRUE(RemoveRow(&nodes, kProviderMenuRow, &reason));
  CHECK_TRUE(reason.empty());
  CHECK_FALSE(HasText(nodes[0], kProviderMenuRow));
  // The rows that were not removed are untouched.
  CHECK_TRUE(HasText(nodes[0], "splash_options"));
  CHECK_TRUE(HasText(nodes[0], "splash_exit"));

  std::string error;
  CHECK_TRUE(Reparse(nodes, &error).size() == nodes.size());

  // A second removal refuses: there is nothing left to remove.
  CHECK_FALSE(RemoveRow(&nodes, kProviderMenuRow, &reason));
  CHECK_TRUE(!reason.empty());
  CHECK_FALSE(RemoveRow(&nodes, "no_such_row", &reason));
}

void TestLocaleOverrides() {
  BeginCase("retail's AV-page strings are restyled, and a translated locale is left alone");

  const std::vector<LocaleOverride> overrides = LocaleOverrides();
  CHECK_EQ(overrides.size(), 4u);

  std::vector<dtb::Node> nodes = {Arr({Sym("av_overscan"), Str("Overscan | %s")}),
                                  Arr({Sym("enabled_token"), Str("ENABLED")}),
                                  Arr({Sym("other"), Str("Keep")})};
  std::string why;
  for (const LocaleOverride& entry : overrides) {
    CHECK_TRUE(ReplaceLocaleEntry(&nodes, entry.key, entry.from, entry.to, &why));
  }
  CHECK_TRUE(nodes[0].kids[1].text == "Overscan: %s");
  CHECK_TRUE(nodes[1].kids[1].text == "On");
  CHECK_TRUE(nodes[2].kids[1].text == "Keep");

  // Idempotent, and a key the locale does not carry is a skip.
  CHECK_TRUE(ReplaceLocaleEntry(&nodes, "av_overscan", "Overscan | %s", "Overscan: %s", &why));
  CHECK_TRUE(nodes[0].kids[1].text == "Overscan: %s");
  CHECK_TRUE(ReplaceLocaleEntry(&nodes, "av_bass_boost", "Bass Boost | %s", "Bass Boost: %s", &why));
  CHECK_EQ(nodes.size(), 3u);

  // A locale that carries a *different* label for the key (a translation) is untouched.
  std::vector<dtb::Node> translated = {Arr({Sym("av_overscan"), Str("Translated")})};
  CHECK_TRUE(ReplaceLocaleEntry(&translated, "av_overscan", "Overscan | %s", "Overscan: %s", &why));
  CHECK_TRUE(translated[0].kids[1].text == "Translated");
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

  // The boolean state suffix is the lowercase "On"/"Off" spelling (owner decision, 2026-10-13),
  // not the main menu's capitals - checked on the row the owner named as well as in general.
  for (const auto& label : labels) {
    CHECK_TRUE(label.second.rfind(": ON") == std::string::npos);
    CHECK_TRUE(label.second.rfind(": OFF") == std::string::npos);
  }
  bool dof_on = false;
  for (const auto& label : labels) {
    if (label.first == "settings_dof_on") {
      dof_on = true;
      CHECK_TRUE(label.second == "Depth of Field: On");
    }
    if (label.first == "settings_dof_off") {
      CHECK_TRUE(label.second == "Depth of Field: Off");
    }
  }
  CHECK_TRUE(dof_on);

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

// TEMPORARY INSTRUMENT (P5b-r2 diagnosis, removed once the fault is located): a guest-side print.
// The bridge answers a `snapshot` read by logging one line per query, so a statement this harmless -
// `{if_else {file_exists rbbz-settings/snapshot/<marker>/b:1} 0 0}` - reaches the host's log without
// touching the UI, the store, or any variable of the guest's. Set `RBBZ_PAUSE_PROBE=1` to arm the
// emit with markers around each step of the in-song row body and of the in-song panel's `{enter}`.
dtb::Node ProbeMark(const char* marker) {
  const std::string path = std::string("rbbz-settings/snapshot/") + marker + "/b:1";
  return Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(path.c_str())}), dtb::Node::Int(0),
              dtb::Node::Int(0)});
}

bool IsBlockNode(const dtb::Node& node) {
  return node.tag == dtb::kArray || node.tag == dtb::kCommand || node.tag == dtb::kProperty;
}

// TEMPORARY INSTRUMENT (P5b-r2, probe 6): a screen that differs from one of ours in exactly one
// dimension - the panel set, or which panel (and therefore scene) it hosts - so a single boot that
// enters both answers which dimension the `read of guest 0x00000024` lives in.
dtb::Node ProbeScreen(const char* name, const std::vector<const char*>& panels, const char* focus,
                      const char* back) {
  std::vector<dtb::Node> list = {Sym("panels")};
  for (const char* panel : panels) {
    list.push_back(Sym(panel));
  }
  return Cmd({Sym("new"), Sym("UIScreen"), Sym(name), Arr(std::move(list)),
              Arr({Sym("focus"), Sym(focus)}), Arr({Sym("back_screen"), Sym(back)})});
}

// `<panel>`'s `{enter ...}` array, wherever it sits in the tree - the block a screen entry runs.
dtb::Node* FindEnterBlock(dtb::Node* node, const char* panel) {
  if (node->tag == dtb::kCommand && node->kids.size() > 2 && node->kids[0].tag == dtb::kSymbol &&
      node->kids[0].text == "new" && node->kids[2].text == panel) {
    for (dtb::Node& kid : node->kids) {
      if (kid.tag == dtb::kArray && !kid.kids.empty() && kid.kids[0].tag == dtb::kSymbol &&
          kid.kids[0].text == "enter") {
        return &kid;
      }
    }
  }
  for (dtb::Node& kid : node->kids) {
    if (!IsBlockNode(kid)) continue;
    if (dtb::Node* hit = FindEnterBlock(&kid, panel)) return hit;
  }
  return nullptr;
}

// TEMPORARY INSTRUMENT (P5b-r2, `RBBZ_PAUSE_DEFS=1`): ride the surface's two contexts in the two
// retail records that belong to them - the main-menu pairs in the menu record, the in-song twins in
// the pause record - instead of appending all of them to the menu record. The probe exists because a
// screen's `{panels ...}` list looks like it resolves only the names its declaring record's context
// can see: from the menu record, `background_night_panel` / `shell_music_panel` / `menu_cache_panel`
// resolve and the in-song panels fault the guest with `read of guest 0x00000024`, while retail's own
// in-song screens - declared in their own in-song records - name those very panels and work.
bool SplitDefinitions() {
  const char* value = std::getenv("RBBZ_PAUSE_DEFS");
  return value != nullptr && std::strcmp(value, "1") == 0;
}

// `DefinitionRoots()` is the main-menu pairs first and the in-song twins after them, so the split is
// a half. Both halves are asserted by `TestEveryDestinationExists` and `TestInSongContext`.
std::vector<dtb::Node> MenuRootsOnly() {
  const std::vector<dtb::Node> roots = DefinitionRoots();
  return std::vector<dtb::Node>(roots.begin(), roots.begin() + roots.size() / 2);
}

std::vector<dtb::Node> InSongRootsOnly() {
  const std::vector<dtb::Node> roots = DefinitionRoots();
  return std::vector<dtb::Node>(roots.begin() + roots.size() / 2, roots.end());
}

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
  const std::vector<dtb::Node> appended = SplitDefinitions() ? MenuRootsOnly() : EarlyDefinitionRoots();
  for (const dtb::Node& root : appended) {
    nodes.push_back(root);
  }
  if (std::getenv("RBBZ_PAUSE_PROBE") != nullptr &&
      std::strcmp(std::getenv("RBBZ_PAUSE_PROBE"), "6") == 0) {
    // Variant A: our in-song root panel - so its scene is the in-song copy - hosted by the
    // **main-menu** panel set, which arm 2 measured to enter in song.
    nodes.push_back(ProbeScreen("probe_menuset_screen",
                                {"background_night_panel", "shell_music_panel", "menu_cache_panel",
                                 "settings_pause_panel"},
                                "settings_pause_panel", "game_screen"));
    // Variant B: our **main-menu** root panel - the scene that already works in song - hosted by the
    // in-song panel set.
    nodes.push_back(ProbeScreen("probe_samescene_screen",
                                {"game_panel", "pause_panel", "game_fade_out_panel",
                                 "background_night_panel", "settings_panel"},
                                "settings_panel", "game_screen"));
    std::printf("emit-append: PROBE6 armed (two variant screens appended)\n");
  }
  if (std::getenv("RBBZ_PAUSE_PROBE") != nullptr &&
      std::strcmp(std::getenv("RBBZ_PAUSE_PROBE"), "7") == 0) {
    // Probe 7, one panel at a time. `ui/gen/ui.dtb`'s own init loads `game_fade_out_panel`,
    // `pause_panel` and `background_night_panel` at boot - and **not** `game_panel`, which arrives
    // with `game.dtb` when a song starts. Each variant drops exactly one of them from the in-song
    // panel set, so one boot that survives names the panel our screen cannot host.
    nodes.push_back(ProbeScreen("probe_nogame_screen",
                                {"pause_panel", "game_fade_out_panel", "background_night_panel",
                                 "settings_pause_panel"},
                                "settings_pause_panel", "game_screen"));
    nodes.push_back(ProbeScreen("probe_nopause_screen",
                                {"game_panel", "game_fade_out_panel", "background_night_panel",
                                 "settings_pause_panel"},
                                "settings_pause_panel", "game_screen"));
    std::printf("emit-append: PROBE7 armed (two panel-set variants appended)\n");
  }
  if (std::getenv("RBBZ_PAUSE_PROBE") != nullptr) {
    // TEMPORARY INSTRUMENT (see `ProbeMark`): mark every step of the in-song panel's own `{enter}`
    // body, so a boot that dies on the way to it says whether the screen's construction reached our
    // panel at all, whether `{this populate_menu}` ran, and whether `{shell_sfx set_interceptor}`
    // did.
    dtb::Node* enter = nullptr;
    for (size_t i = before; i < nodes.size() && enter == nullptr; ++i) {
      enter = FindEnterBlock(&nodes[i], "settings_general_pause_panel");
    }
    if (enter == nullptr || enter->kids.size() != 3) {
      std::printf("emit-append: cannot instrument the in-song enter block\n");
      return 1;
    }
    std::vector<dtb::Node> marked = {ProbeMark("e1"), enter->kids[1], ProbeMark("e2"),
                                     enter->kids[2], ProbeMark("e3")};
    enter->kids = std::move(marked);
    std::printf("emit-append: PROBE armed (markers e1-e3 in the in-song enter block)\n");
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
  if (!AddDeferredScreens(&nodes, &error)) {
    std::printf("emit-ui: %s\n", error.c_str());
    return 1;
  }
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
  bool ok = false;
  const char* probe_env = std::getenv("RBBZ_PAUSE_PROBE");
  if (pause && SplitDefinitions()) {
    // The in-song twins ride in the record the in-song context loads (`RBBZ_PAUSE_DEFS=1`).
    const std::vector<dtb::Node> song = InSongRootsOnly();
    for (const dtb::Node& root : song) {
      nodes.push_back(root);
    }
    std::printf("emit-pause: appended %zu in-song root(s) to the pause record\n", song.size());
  }
  if (pause && probe_env != nullptr && std::strcmp(probe_env, "7") == 0) {
    // Probe 7: row 1 enters the variant that drops `game_panel`, row 5 the one that drops
    // `pause_panel`. The dispatch is the shipped merge, so only the destination differs.
    ok = RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &error) &&
         MergePauseListRows(&nodes, &error) &&
         AddPauseSwitchCase(&nodes, "settings_general",
                            {Cmd({Sym("ui"), Sym("goto_screen"), Sym("probe_nogame_screen")})},
                            &error) &&
         AddPauseSwitchCase(&nodes, "settings_interface",
                            {Cmd({Sym("ui"), Sym("goto_screen"), Sym("probe_nopause_screen")})},
                            &error);
    std::printf("emit-pause: PROBE7 armed (row 1 -> no game_panel, row 5 -> no pause_panel)\n");
  } else if (pause && probe_env != nullptr && std::strcmp(probe_env, "6") == 0) {
    // Probe 6, the twin laboratory. Retail's own row gesture is kept - the merge and the cases, which
    // is the same dispatch the shipped design uses - so only the destination differs. Row 1 of the
    // merged list enters **variant B** (`settings_panel`, the panel whose scene already works in
    // song, hosted by the in-song panel set) and row 5 enters **variant A** (our in-song panel
    // hosted by the main-menu panel set), the shape a laboratory boot already drew correctly.
    ok = RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &error) &&
         MergePauseListRows(&nodes, &error) &&
         AddPauseSwitchCase(&nodes, "settings_general",
                            {Cmd({Sym("ui"), Sym("goto_screen"), Sym("probe_samescene_screen")})},
                            &error) &&
         AddPauseSwitchCase(&nodes, "settings_interface",
                            {Cmd({Sym("ui"), Sym("goto_screen"), Sym("probe_menuset_screen")})},
                            &error);
    std::printf("emit-pause: PROBE6 armed (row 1 -> same-scene variant, row 5 -> menuset variant)\n");
  } else if (pause && probe_env != nullptr && std::strcmp(probe_env, "5") == 0) {
    // Arm 5, and the one P5b-r2 exists for: **re-point the pause menu's own Help & Options row at our
    // in-song root screen** - the design P5b tried first and abandoned, because every arm that jumped
    // faulted the guest with `read of guest 0x00000024`. That fault is now explained and fixed: the
    // in-song screens were missing `game_fade_out_panel` (see `PausePanels`), which is what a screen
    // entered with a plain `{ui goto_screen}` needs in song. With the panel there, this is one edit
    // instead of a list merge plus three switch cases, and it does not depend on the inline list's
    // dispatch at all.
    ok = RerouteRow(&nodes, kPauseMenuRow, InSongRowBody(kRootScreen), &error);
    std::printf("emit-pause: PROBE5 armed (the pause row jumps straight to our in-song root)\n");
  } else if (pause && probe_env != nullptr && std::strcmp(probe_env, "4") == 0) {
    // Arm 4, the decisive one: the case body's *shape* is held constant (a marker, a jump, a marker)
    // and only the destination changes. Row 1 jumps to a **retail** in-song page
    // (`controller_config_pause_screen` - the screen retail's own `pause_controls` row opens, so it is
    // known to work in song), row 5 jumps to one of ours. `q1` but not `q2` means the jump never
    // returned; both mean the case ran and the jump was taken; neither means the inline list is not
    // dispatching to this switch at all - which is a different problem from the destination's shape.
    std::vector<dtb::Node> retail_page = {ProbeMark("q1"),
                                          Cmd({Sym("ui"), Sym("goto_screen"),
                                               Sym("controller_config_pause_screen")}),
                                          ProbeMark("q2")};
    std::vector<dtb::Node> our_page = {ProbeMark("s1"),
                                       Cmd({Sym("ui"), Sym("goto_screen"),
                                            Sym("settings_interface_pause_screen")}),
                                       ProbeMark("s2")};
    ok = RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &error) &&
         MergePauseListRows(&nodes, &error) &&
         AddPauseSwitchCase(&nodes, "settings_general", retail_page, &error) &&
         AddPauseSwitchCase(&nodes, "settings_interface", our_page, &error);
    std::printf("emit-pause: PROBE4 armed (markers q1-q2 on a retail page, s1-s2 on ours)\n");
  } else if (pause && probe_env != nullptr) {
    // Arm 3. Arm 2 measured our **main-menu** screen to enter fine from the in-song pause case
    // (`p4b` logged with a plain `{ui goto_screen settings_general_screen}`), so the panel is
    // innocent in-song and the broken half is the `_pause_screen` twin's *definition* - or the
    // `set back_screen` statement that ran before the jump in arm 1. This arm jumps to the twin with
    // no statement before it: a logged `p4b` means the twin is enterable and the `set` was the fault,
    // a missing `p4b` means the twin's own `{panels ...}` / `{back_screen}` is.
    std::vector<dtb::Node> route = {ProbeMark("p4a"),
                                    Cmd({Sym("ui"), Sym("goto_screen"),
                                         Sym("settings_general_pause_screen")}),
                                    ProbeMark("p4b")};
    std::vector<dtb::Node> plain = {ProbeMark("p5a"),
                                    Cmd({Sym("ui"), Sym("goto_screen"), Sym("av_options_pause_screen")}),
                                    ProbeMark("p5b")};
    ok = RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &error) &&
         MergePauseListRows(&nodes, &error) &&
         AddPauseSwitchCase(&nodes, "settings_general", {ProbeMark("p1-reached")}, &error) &&
         AddPauseSwitchCase(&nodes, "settings_gameplay",
                            InSongRowBody("settings_gameplay_screen"), &error) &&
         AddPauseSwitchCase(&nodes, "settings_interface", plain, &error) &&
         RerouteRow(&nodes, "audio_video_options", route, &error);
    std::printf("emit-pause: PROBE3 armed (markers p1, p4a-b, p5a-b)\n");
  } else if (pause) {
    // The in-song merge (P5b-r2), in the order the record's own structure implies: the list's
    // contents, the cases our rows need, and the Audio/Video row re-pointed at the merged page's
    // in-song twin. `pause_options` itself is left as retail's own statement, which is why routing
    // it with `PauseRouteBody()` is a no-op rather than an edit.
    ok = RerouteRow(&nodes, kPauseMenuRow, PauseRouteBody(), &error) &&
         MergePauseListRows(&nodes, &error) &&
         AddPauseSwitchCase(&nodes, "settings_general",
                            InSongRowBody("settings_general_screen"), &error) &&
         AddPauseSwitchCase(&nodes, "settings_gameplay",
                            InSongRowBody("settings_gameplay_screen"), &error) &&
         AddPauseSwitchCase(&nodes, "settings_interface",
                            InSongRowBody("settings_interface_screen"), &error) &&
         RerouteRow(&nodes, "audio_video_options", InSongRowBody("settings_av_screen"), &error);
  } else {
    ok = RerouteRow(&nodes, kMainMenuRow, MainMenuRouteBody(), &error);
  }
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
  for (const LocaleOverride& entry : LocaleOverrides()) {
    std::string why;
    ReplaceLocaleEntry(&nodes, entry.key, entry.from, entry.to, &why);
  }
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

// ------------------------------------------------------------------------------------------------
// Reading a definition back as text (P5b-r2 diagnosis, host side)
// ------------------------------------------------------------------------------------------------
// The two contexts' pages differ only in four statements (the panel set, the focus, the back target
// and the row bodies), so the question "what does our twin declare that the main-menu page does not"
// is answerable without a boot - if the definition can be read. `Node::Symbol` keeps the name it was
// given (only `Compile` hashes it), so the authored trees print as readable source.
void PrintNode(const dtb::Node& node, std::string* out) {
  switch (node.tag) {
    case dtb::kCommand:
    case dtb::kArray:
    case dtb::kProperty: {
      const char open = node.tag == dtb::kCommand ? '{' : (node.tag == dtb::kArray ? '(' : '[');
      const char close = node.tag == dtb::kCommand ? '}' : (node.tag == dtb::kArray ? ')' : ']');
      *out += open;
      for (size_t i = 0; i < node.kids.size(); ++i) {
        if (i != 0) *out += ' ';
        PrintNode(node.kids[i], out);
      }
      *out += close;
      return;
    }
    case dtb::kString:
      *out += '"';
      *out += node.text;
      *out += '"';
      return;
    case dtb::kVar:
      *out += '$';
      *out += node.text;
      return;
    case dtb::kInt:
      *out += std::to_string(node.scalar);
      return;
    default:
      *out += node.text.empty() ? ("#" + std::to_string(node.tag)) : node.text;
      return;
  }
}

// Every root whose name holds `needle`, printed one top-level member per line so two contexts can be
// diffed by eye. `{new UIPanel <name> ...}` / `{new UIScreen <name> ...}` - the name is the third
// child, which is why this reads positionally instead of searching for text.
int DumpDef(const char* needle) {
  size_t printed = 0;
  for (const dtb::Node& root : DefinitionRoots()) {
    if (root.tag != dtb::kCommand || root.kids.size() < 3 || root.kids[0].text != "new" ||
        root.kids[2].text.find(needle) == std::string::npos) {
      continue;
    }
    ++printed;
    std::string head;
    for (size_t i = 0; i < 3; ++i) {
      if (i != 0) head += ' ';
      PrintNode(root.kids[i], &head);
    }
    std::printf("%s\n", head.c_str());
    for (size_t i = 3; i < root.kids.size(); ++i) {
      std::string member;
      PrintNode(root.kids[i], &member);
      std::printf("  %s\n", member.c_str());
    }
    std::printf("\n");
  }
  std::printf("dump-def: %zu root(s) matching \"%s\"\n", printed, needle);
  return printed == 0 ? 1 : 0;
}

// The same reading, on a **retail** record: `Parse` decrypts the stream and hands back the nodes, so
// an installed record is as readable as an authored one. One line per root, optionally only the
// roots whose text holds `needle` - which is how "who declares this panel, and in which record" is
// answerable without a boot or a byte search (the compiled stream is encrypted, so names are not
// greppable in the file itself).
int DumpRecord(const char* in_path, const char* needle) {
  std::vector<uint8_t> bytes;
  if (!ReadFile(in_path, &bytes)) return 1;
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error)) {
    std::printf("cannot parse %s: %s\n", in_path, error.c_str());
    return 1;
  }
  size_t printed = 0;
  for (const dtb::Node& root : nodes) {
    std::string text;
    PrintNode(root, &text);
    if (needle != nullptr && text.find(needle) == std::string::npos) {
      continue;
    }
    ++printed;
    std::printf("%s\n", text.c_str());
  }
  std::printf("dump-record: %s (%zu root(s), %zu printed)\n", in_path, nodes.size(), printed);
  return 0;
}

// -------------------------------------------------------------------------------------------------
// The help captions (owner request, 2026-10-13): every row carries a short description, the
// definition publishes the highlighted row's symbol through the settings channel on entry and on
// every move (and the hide on every way out), and the retail/screen table resolves the pages whose
// DTA is not ours.
// -------------------------------------------------------------------------------------------------

void TestHelpTextsCoverEveryRow() {
  BeginCase("every row of the surface carries a caption, and every caption fits the overlay");

  const std::vector<std::string> symbols = HelpSymbols();
  CHECK_TRUE(symbols.size() > 60u);
  std::vector<std::string> seen;
  for (const std::string& symbol : symbols) {
    CHECK_TRUE(std::find(seen.begin(), seen.end(), symbol) == seen.end());
    seen.push_back(symbol);
    const std::string text = HelpTextFor(symbol);
    CHECK_TRUE(!text.empty());
    if (text.empty()) {
      std::printf("         row %s has no caption\n", symbol.c_str());
      continue;
    }
    // The overlay wraps at a fixed width; "short, 1-2 lines" is a table contract, so it is
    // enforced here rather than discovered on screen.
    CHECK_TRUE(text.size() <= kMaxHelpTextChars);
    CHECK_TRUE(text.find('\n') == std::string::npos);
    CHECK_TRUE(text.back() == '.');
    for (const char c : text) {
      CHECK_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
    }
  }

  // An unknown symbol has no text - the overlay treats empty as "nothing to show", so a row that
  // somehow never got one degrades to no caption rather than a wrong one.
  CHECK_TRUE(HelpTextFor("no_such_row").empty());
  CHECK_TRUE(HelpTextFor("").empty());

  // Spot checks that a caption is the one authored for the row, including two retail rows the
  // merge adopts (their text is still ours; the locale only owns their labels) and a provider row.
  CHECK_TRUE(HelpTextFor("controls").find("controller") != std::string::npos);
  CHECK_TRUE(HelpTextFor("calibration").find("audio") != std::string::npos);
  CHECK_TRUE(HelpTextFor("settings_gameplay").find("Ultimate") != std::string::npos);
  CHECK_TRUE(HelpTextFor("settings_vsync").find("tearing") != std::string::npos);
}

// True when `text` is a caption event path, and the symbol it names ("-" for the hide).
bool IsHelpPath(const std::string& text, std::string* symbol) {
  constexpr char kPrefix[] = "rbbz-settings/help/";
  if (text.rfind(kPrefix, 0) != 0) {
    return false;
  }
  const std::string rest = text.substr(sizeof(kPrefix) - 1);
  const size_t slash = rest.find('/');
  if (symbol != nullptr) {
    *symbol = slash == std::string::npos ? std::string() : rest.substr(0, slash);
  }
  return true;
}

void TestHelpEventsInTheDefinition() {
  BeginCase("the definition publishes a caption on entry and every move, and the hide on leave");

  const std::vector<dtb::Node> roots = DefinitionRoots();
  int panels = 0;
  int enters = 0;
  int moves = 0;
  int clears = 0;
  for (const dtb::Node& root : roots) {
    std::vector<const dtb::Node*> rows;
    CollectMembers(root, "select_row", &rows);
    if (rows.empty()) {
      continue;  // a screen, not a panel
    }
    ++panels;

    // Enter: the first visible row's caption, emitted as an `enter` statement so a commit that
    // re-runs `populate_menu` cannot drag the overlay back to row 0 (the mirror has shown that
    // bug class is real). A page whose first row set depends on the provider gate writes both
    // gate worlds; the test accepts 1 or 2 writes and requires at least one.
    std::vector<const dtb::Node*> enter_members;
    CollectMembers(root, "enter", &enter_members);
    CHECK_EQ(enter_members.size(), 1u);
    int written = 0;
    for (const dtb::Node* member : enter_members) {
      std::vector<std::string> texts;
      CollectText(*member, &texts);
      for (const std::string& text : texts) {
        std::string symbol;
        if (!IsHelpPath(text, &symbol)) {
          continue;
        }
        ++written;
        CHECK_TRUE(symbol == "-" || !HelpTextFor(symbol).empty());
      }
    }
    CHECK_TRUE(written >= 1 && written <= 2);
    if (written > 0) {
      ++enters;
    }

    // Move tables: every case that moves the selection also publishes the caption of the row it
    // moves to, in the same statement - so the overlay names the row the highlight just reached.
    for (const char* name : {"move_up", "move_down"}) {
      std::vector<const dtb::Node*> move_members;
      CollectMembers(root, name, &move_members);
      CHECK_EQ(move_members.size(), 1u);
      for (const dtb::Node* member : move_members) {
        std::vector<const dtb::Node*> tables;
        CollectRowTables(*member, &tables);
        for (const dtb::Node* table : tables) {
          for (size_t i = 2; i < table->kids.size(); ++i) {
            const dtb::Node& kase = table->kids[i];
            if (kase.tag != dtb::kArray) {
              continue;
            }
            std::vector<const dtb::Node*> placements;
            FindCalls(kase, "set_selected", &placements);
            if (placements.empty()) {
              continue;  // clamped at an end: the selection does not move, so nothing is said
            }
            ++moves;
            int writes = 0;
            std::vector<std::string> texts;
            CollectText(kase, &texts);
            for (const std::string& text : texts) {
              std::string symbol;
              if (!IsHelpPath(text, &symbol)) {
                continue;
              }
              ++writes;
              CHECK_TRUE(symbol != "-");  // a move always names a row
              CHECK_TRUE(!HelpTextFor(symbol).empty());
            }
            CHECK_EQ(writes, 1);
          }
        }
      }
    }

    // Ways out (Cancel, Close, a one-shot choice) publish the hide before they navigate.
    std::vector<std::string> texts;
    CollectText(root, &texts);
    for (const std::string& text : texts) {
      std::string symbol;
      if (IsHelpPath(text, &symbol) && symbol == "-") {
        ++clears;
      }
    }
  }
  CHECK_EQ(panels, 40);      // twenty pages, in both contexts
  CHECK_EQ(enters, panels);  // every page tells the overlay where the highlight starts
  CHECK_TRUE(moves > 100);   // the unclamped direction cases, across the surface
  CHECK_TRUE(clears >= panels);
}

void TestHelpTopicsResolve() {
  BeginCase("the retail pages and the in-song pause list resolve to captions");

  using help::CaptionFor;
  using help::IsGuestScreen;
  using help::KnowsScreen;
  using help::TextForSymbol;

  // The retail rows this table owns are short, distinct and grounded in the retail labels.
  for (const char* symbol : {"av_overscan", "av_active_track_volume", "av_sound_volume",
                             "av_bass_boost", "tutorialsong", "tutorialsong3"}) {
    const std::string text = TextForSymbol(symbol);
    CHECK_TRUE(!text.empty());
    CHECK_TRUE(text.size() <= kMaxHelpTextChars);
  }
  CHECK_TRUE(TextForSymbol("no_such_row").empty());

  // The in-song pause list is the merged root's own rows, five or six by the provider gate.
  const std::vector<std::string> six = PauseRootRows(true);
  const std::vector<std::string> five = PauseRootRows(false);
  CHECK_EQ(six.size(), 6u);
  CHECK_EQ(five.size(), 5u);
  for (size_t i = 0; i < six.size(); ++i) {
    CHECK_TRUE(CaptionFor({"game_screen", "options.lst", static_cast<int>(i), true}) ==
               HelpTextFor(six[i]));
  }
  for (size_t i = 0; i < five.size(); ++i) {
    CHECK_TRUE(CaptionFor({"game_screen", "options.lst", static_cast<int>(i), false}) ==
               HelpTextFor(five[i]));
  }
  CHECK_TRUE(CaptionFor({"game_screen", "options.lst", 5, false}).empty());  // five-row world
  CHECK_TRUE(CaptionFor({"game_screen", "pause.lst", 0, true}).empty());   // not a settings list
  CHECK_TRUE(CaptionFor({"game_screen", "", -1, true}).empty());           // nothing focused

  // The retail audio page: its four checkbox rows, in the order the record fills the list, plus
  // the two sliders the volume rows open.
  const char* av_rows[] = {"av_overscan", "av_active_track_volume", "av_sound_volume",
                           "av_bass_boost"};
  for (int i = 0; i < 4; ++i) {
    CHECK_TRUE(CaptionFor({"av_options_screen", "checkbox.lst", i, true}) ==
               TextForSymbol(av_rows[i]));
    CHECK_TRUE(CaptionFor({"av_options_pause_screen", "checkbox.lst", i, true}) ==
               TextForSymbol(av_rows[i]));
  }
  CHECK_TRUE(CaptionFor({"av_options_screen", "checkbox.lst", 4, false}).empty());
  CHECK_TRUE(CaptionFor({"av_options_screen", "track_sound_slider.sld", -1, false}) ==
             TextForSymbol("av_active_track_volume"));
  CHECK_TRUE(CaptionFor({"av_options_screen", "sound_effects_slider.sld", -1, false}) ==
             TextForSymbol("av_sound_volume"));

  // How to Play's two rows, and the two pages that are pickers rather than lists.
  CHECK_TRUE(CaptionFor({"tutorials_screen", "options.lst", 0, false}) ==
             TextForSymbol("tutorialsong"));
  CHECK_TRUE(CaptionFor({"tutorials_screen", "options.lst", 1, false}) ==
             TextForSymbol("tutorialsong3"));
  CHECK_TRUE(CaptionFor({"tutorials_screen", "options.lst", 2, false}).empty());
  CHECK_TRUE(!CaptionFor({"cal_welcome_screen", "", -1, false}).empty());
  CHECK_TRUE(!CaptionFor({"controller_config_screen", "", -1, false}).empty());
  CHECK_TRUE(!CaptionFor({"controller_config_pause_screen", "", -1, false}).empty());

  // Ownership: the observer manages only its own screens, and the guest's screens are off limits
  // to its clear (the guest's entry event has already published the destination's caption).
  CHECK_TRUE(KnowsScreen("game_screen"));
  CHECK_TRUE(KnowsScreen("av_options_screen"));
  CHECK_TRUE(KnowsScreen("tutorials_pause_screen"));
  CHECK_FALSE(KnowsScreen(kRootScreen));
  CHECK_FALSE(KnowsScreen("splash_screen"));
  CHECK_TRUE(IsGuestScreen(kRootScreen));
  CHECK_TRUE(IsGuestScreen("settings_hud_pause_screen"));
  CHECK_TRUE(IsGuestScreen("settings_restart_screen"));
  CHECK_FALSE(IsGuestScreen("av_options_screen"));
  CHECK_FALSE(IsGuestScreen("splash_screen"));
  CHECK_FALSE(IsGuestScreen(""));
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
  if (argc >= 3 && std::strcmp(argv[1], "dump-def") == 0) {
    return DumpDef(argv[2]);
  }
  if (argc >= 3 && std::strcmp(argv[1], "dump-record") == 0) {
    return DumpRecord(argv[2], argc >= 4 ? argv[3] : nullptr);
  }

  TestDefinitionRoundTrip();
  TestMergedRootShape();
  TestListOwnership();
  TestScreenBackTargets();
  TestRequestPathsParse();
  TestPanelLoadEdit();
  TestSplashReroute();
  TestPauseReroute();
  TestInSongContext();
  TestPauseMergeEdits();
  TestRerouteRefusesWithoutTheCase();
  TestDoTakesStatementsDirectly();
  TestLocaleEntry();
  TestEveryFillPlacesTheSelection();
  TestEveryDestinationExists();
  TestButtonTableIsOneSwitch();
  TestSelectAdvancesAndWraps();
  TestRestartPromptOnlyOnLeave();
  TestInvertedBoolRow();
  TestProviderRowRemoval();
  TestLocaleOverrides();
  TestLabels();
  TestHelpTextsCoverEveryRow();
  TestHelpEventsInTheDefinition();
  TestHelpTopicsResolve();
  return Finish();
}
