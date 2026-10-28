// SPDX-License-Identifier: GPL-2.0-only
#include "settings/menu_payload.h"

#include <algorithm>
#include <array>
#include <numeric>
#include "store/store_metadata.h"
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

namespace {
Node* PanelMethod(std::vector<Node>* roots, std::string_view panel, std::string_view method) {
  if (!roots) return nullptr;
  for (auto& root : *roots) {
    if (root.tag != ui::dtb::kCommand || root.kids.size() < 3 ||
        root.kids[0].text != "new" || root.kids[2].text != panel) continue;
    for (auto& child : root.kids) {
      if (child.tag == ui::dtb::kArray && !child.kids.empty() &&
          child.kids[0].text == method) return &child;
    }
  }
  return nullptr;
}
void FindTriggerCases(Node& node, std::string_view action, std::vector<Node*>* cases) {
  if (node.tag == ui::dtb::kArray && !node.kids.empty()) {
    const auto& first = node.kids[0];
    // Song list / Inbox / Recommendations use switch cases; both store panels use cond.
    if ((first.tag == ui::dtb::kSymbol && first.text == action) ||
        (first.tag == ui::dtb::kCommand && first.kids.size() == 3 &&
         first.kids[0].text == "==" && first.kids[1].tag == ui::dtb::kVar &&
         first.kids[1].text == "action" && first.kids[2].text == action)) {
      cases->push_back(&node);
      return;
    }
  }
  for (auto& child : node.kids) FindTriggerCases(child, action, cases);
}
constexpr std::array<const char*, 4> kHeaderScreens = {
    "song_select_screen", "store_menu_screen", "inbox_screen", "recommendation_screen"};
LibraryHeader AdjacentHeader(LibraryHeader current, int direction, LibraryHeaderOptions options) {
  const std::array<bool, 4> enabled = {true, options.store, options.messages, options.recommendations};
  for (int step = 1; step <= 4; ++step) {
    const int index = (static_cast<int>(current) + direction * step + 8) % 4;
    if (enabled[index]) return static_cast<LibraryHeader>(index);
  }
  return LibraryHeader::kSongs;
}
Node HeaderDestination(LibraryHeader header) {
  if (header == LibraryHeader::kStore) {
    return Node::Command({Node::Symbol("store_menu_screen"), Node::Symbol("get"),
                          Node::Symbol("which_store_screen")});
  }
  return Node::Symbol(kHeaderScreens[static_cast<int>(header)]);
}
Node HeaderArrow(int direction) {
  return Node::Command({Node::Command({Node::Symbol("background_panel"), Node::Symbol("find"),
      Node::Symbol(direction > 0 ? "arrow_rt.flow" : "arrow_lt.flow")}), Node::Symbol("activate")});
}
// Rewrite only the transition in each authored case: retain preview cleanup, store
// highlight saves and the arrow Flow. Expand Recommendations' RT helper locally,
// since its other callers must still go to Your Songs.
bool RewriteHeaderTransition(Node& node, LibraryHeader destination, int direction, const Node* song_helper) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() == 2 &&
      node.kids[1].text == "go_to_song_select") {
    if (!song_helper) return false;
    std::vector<Node> body{Node::Symbol("do")};
    body.insert(body.end(), song_helper->kids.begin() + 1, song_helper->kids.end());
    node = Node::Command(std::move(body));
  }
  if (node.tag == ui::dtb::kCommand && node.kids.size() == 2 &&
      node.kids[0].text == "transition_to") {
    node.kids = {Node::Symbol(kLibraryHeaderTransition), HeaderDestination(destination),
                 Node::Int(static_cast<uint32_t>(direction))};
    return true;
  }
  if (destination != LibraryHeader::kStore) {
    std::erase_if(node.kids, [](const Node& child) {
      return child.tag == ui::dtb::kCommand && child.kids.size() >= 3 &&
             child.kids[0].text == "data_mine_mgr" && child.kids[1].text == "add_event" &&
             child.kids[2].text == "enter_store";
    });
  }
  bool found = false;
  for (auto& child : node.kids) found |= RewriteHeaderTransition(child, destination, direction, song_helper);
  return found;
}
}

namespace {
Node* FindNode(Node& node, uint32_t tag, std::string_view first, std::string_view second = {}) {
  if (node.tag == tag && !node.kids.empty() && node.kids[0].text == first &&
      (second.empty() || (node.kids.size() > 1 && node.kids[1].text == second))) return &node;
  for (auto& child : node.kids) if (auto* found = FindNode(child, tag, first, second)) return found;
  return nullptr;
}
// The cached transition object must receive the override before goto_screen starts it.
// Ordinary transitions explicitly reset it, including when they reuse a tab transition.
bool SetHeaderAnimationBeforeGoto(Node& function, Node direction) {
  auto* jump = FindNode(function, ui::dtb::kCommand, "ui", "goto_screen");
  if (!jump) return false;
  Node setter = Node::Command({Node::Command({Node::Symbol("object"), Node::Var("screen_name")}),
      Node::Symbol("set"), Node::Symbol("rbbz_header_animation"), std::move(direction)});
  *jump = Node::Command({Node::Symbol("do"), std::move(setter), *jump});
  return true;
}
}

// Retail interprets an index gap greater than one as a wrap. Skipping a hidden
// tab breaks that inference, and two-tab rings need opposite swipes for the same
// endpoints. Trigger and mouse routes override it; keep the retail expression intact
// for every other caller. See RhythmVerse plan P9-4's animation/layout correction.
bool ConfigureLibraryHeaderAnimations(std::vector<Node>* roots, std::string* why) {
  if (!roots) { if (why) *why = "no transition definitions"; return false; }
  auto staged = *roots;
  Node* between = nullptr;
  Node* transition = nullptr;
  for (auto& root : staged) {
    if (auto* found = FindNode(root, ui::dtb::kCommand, "func", "transition_between")) between = found;
    if (auto* found = FindNode(root, ui::dtb::kCommand, "new", "TransitionScreen")) transition = found;
  }
  Node* animation = transition ? FindNode(*transition, ui::dtb::kArray, "anim_type") : nullptr;
  if (!between || between->kids.size() != 4 || between->kids[2].tag != ui::dtb::kArray ||
      !animation || animation->kids.size() != 2) {
    if (why) *why = "transition definitions lack the header animation selector";
    return false;
  }
  Node directional = *between;
  directional.kids[1] = Node::Symbol("rbbz_header_transition_between");
  directional.kids[2].kids.push_back(Node::Var("rbbz_header_direction"));
  if (!SetHeaderAnimationBeforeGoto(directional, Node::Var("rbbz_header_direction")) ||
      !SetHeaderAnimationBeforeGoto(*between, Node::Int(0))) {
    if (why) *why = "transition_between lacks goto_screen";
    return false;
  }
  const auto property = [] { return Node::Property({Node::Symbol("rbbz_header_animation")}); };
  animation->kids[1] = Node::Command({Node::Symbol("if_else"),
      Node::Command({Node::Symbol("!="), property(), Node::Int(0)}),
      Node::Command({Node::Symbol("if_else"),
          Node::Command({Node::Symbol(">"), property(), Node::Int(0)}),
          Node::Symbol("shift_right"), Node::Symbol("shift_left")}), animation->kids[1]});
  transition->kids.push_back(Node::Array({Node::Symbol("rbbz_header_animation"), Node::Int(0)}));
  staged.push_back(std::move(directional));
  staged.push_back(Node::Command({Node::Symbol("func"), Node::Symbol(kLibraryHeaderTransition),
      Node::Array({Node::Var("screen_b"), Node::Var("rbbz_header_direction")}),
      Node::Command({Node::Symbol("rbbz_header_transition_between"),
          Node::Command({Node::Symbol("ui"), Node::Symbol("current_screen")}),
          Node::Var("screen_b"), Node::Var("rbbz_header_direction")})}));
  *roots = std::move(staged);
  return true;
}

bool ConfigureLibraryHeaderBar(std::vector<Node>* roots, LibraryHeaderOptions options, std::string* why) {
  if (!roots) { if (why) *why = "no background record"; return false; }
  auto staged = *roots;
  Node* mode = PanelMethod(&staged, "background_panel", "set_offline_mode_headers");
  Node* inbox = PanelMethod(&staged, "background_panel", "update_inbox_header");
  Node* enter = PanelMethod(&staged, "background_panel", "enter");
  Node* flow = nullptr;
  if (mode) {
    for (auto& command : mode->kids) {
      if (command.tag == ui::dtb::kCommand && command.kids.size() == 4 &&
          command.kids[0].text == "if_else") {
        if (flow) { if (why) *why = "ambiguous header Flow"; return false; }
        flow = &command;
      }
    }
  }
  if (!flow || !inbox || !enter) {
    if (why) *why = "background_panel lacks its header Flow, enter or inbox callback";
    return false;
  }
  // Full LT/RT appearance whenever there is another enabled tab, regardless of sign-in.
  flow->kids[1] = Node::Symbol(options.navigation_enabled() ? "FALSE" : "TRUE");
  const auto hide = [](const char* label) {
    return Node::Command({Node::Symbol(label), Node::Symbol("set_showing"), Node::Symbol("FALSE")});
  };
  std::vector<Node> hidden;
  if (!options.store) hidden.push_back(hide("main_store.lbl"));
  if (!options.recommendations) hidden.push_back(hide("main_Home.lbl"));
  if (!options.messages) {
    hidden.push_back(hide("main_inbox.lbl"));
    hidden.push_back(hide("main_new_msg.lbl"));
    inbox->kids.push_back(hide("main_new_msg.lbl"));
  }
  mode->kids.insert(mode->kids.end(), hidden.begin(), hidden.end());
  // Search results enter store_browser_screen without the front menu's header
  // setup. Scene/Flow restoration can re-show a disabled label at its old slot,
  // overlapping an enabled label whose animation keys were compacted below.
  // Enforce only the visibility mask at the shared panel's lifecycle boundaries;
  // never restart header Flows or move animated labels from poll.
  enter->kids.insert(enter->kids.end(), hidden.begin(), hidden.end());
  if (!hidden.empty()) {
    Node* poll = PanelMethod(&staged, "background_panel", "poll");
    if (poll) {
      poll->kids.insert(poll->kids.end(), hidden.begin(), hidden.end());
    } else {
      std::vector<Node> body{Node::Symbol("poll")};
      body.insert(body.end(), hidden.begin(), hidden.end());
      // Adding a method can move its siblings; finish using mode/inbox first and
      // reacquire enter before appending the position-key edits.
      for (auto& root : staged) {
        if (root.tag == ui::dtb::kCommand && root.kids.size() >= 3 &&
            root.kids[0].text == "new" && root.kids[2].text == "background_panel") {
          root.kids.push_back(Node::Array(std::move(body)));
          break;
        }
      }
      enter = PanelMethod(&staged, "background_panel", "enter");
    }
  }
  // TextShift.anim owns the label positions throughout a transition, so moving
  // transforms after a snap would be overwritten on the next frame. Reauthor its
  // existing X keys once the scene is loaded, before the screen's jump_to_header.
  // Its five endpoints are Recommendations/Songs/Store/Messages/Recommendations.
  // Keep the retail offscreen wrap keys, scale, colour, and all unrelated tracks.
  const std::array<bool, 4> visible = {options.recommendations, true, options.store, options.messages};
  constexpr std::array<const char*, 5> transforms = {"main_home.trans", "main_mySongs.trans",
      "main_store.trans", "main_inbox.trans", "main_newMessage.trans"};
  constexpr std::array<int, 4> slots = {-345, -43, 91, 226};
  if (!(options.store && options.messages && options.recommendations)) {
    for (int frame = 0; frame <= 4; ++frame) {
      const int current = frame % 4;
      int slot = 0;
      for (int offset = 0; offset < 4; ++offset) {
        const int header = (current + offset) % 4;
        if (!visible[header]) continue;
        const int x = header == 0 && slot == 3 ? 235 : slots[slot];
        const auto set_x = [&](int transform) {
          enter->kids.push_back(Node::Command({
              Node::Command({Node::Var("this"), Node::Symbol("find"), Node::Symbol("TextShift.anim")}),
              Node::Symbol("set_key_val"),
              Node::Command({Node::Var("this"), Node::Symbol("find"), Node::Symbol(transforms[transform])}),
              Node::Array({Node::Symbol("local_xfm"), Node::Symbol("x")}),
              Node::Int(frame * 10), Node::Int(static_cast<uint32_t>(x))}));
        };
        set_x(header);
        if (header == 3) set_x(4);  // Messages' badge follows its header.
        ++slot;
      }
    }
  }
  *roots = std::move(staged);
  return true;
}

bool RouteLibraryHeaderNavigation(std::vector<Node>* roots, LibraryHeader current,
                                   LibraryHeaderOptions options, std::string* why) {
  if (!roots) { if (why) *why = "no navigation record"; return false; }
  auto staged = *roots;
  const std::array<std::vector<const char*>, 4> panels = {{
      {"song_select_panel"}, {"store_menu_panel", "store_browser_panel"},
      {"inbox_panel"}, {"recommendation_panel"}}};
  for (const char* panel : panels[static_cast<int>(current)]) {
    Node* buttons = PanelMethod(&staged, panel, "BUTTON_DOWN_MSG");
    const Node* helper = PanelMethod(&staged, panel, "go_to_song_select");
    // Keep the authored departure work (preview shutdown, highlight saves, etc.)
    // for direct mouse destinations too, before the trigger cases are rewritten.
    std::array<Node, 2> click_templates;
    for (int side = 0; side < 2; ++side) {
      std::vector<Node*> cases;
      if (buttons) FindTriggerCases(*buttons, side == 0 ? "kAction_PageDown" : "kAction_PageUp", &cases);
      if (cases.size() != 1) {
        if (why) *why = std::string(panel) + " lacks its mouse departure template";
        return false;
      }
      click_templates[side] = *cases[0];
    }
    std::vector<Node> click_routes{Node::Symbol("cond")};
    const std::array<bool, 4> enabled = {true, options.store, options.messages, options.recommendations};
    for (int target = 0; target < 4; ++target) {
      if (!enabled[target] || target == static_cast<int>(current)) continue;
      const int distance = (target - static_cast<int>(current) + 4) % 4;
      const int direction = distance <= 2 ? 1 : -1;
      Node route = click_templates[direction > 0 ? 0 : 1];
      route.kids[0] = Node::Command({Node::Symbol("file_exists"),
          Node::String(std::string("rbbz-mouse/header/") + panel + "/" + std::to_string(target))});
      if (current == LibraryHeader::kSongs && route.kids.size() == 2 &&
          route.kids[1].kids.size() >= 4 && route.kids[1].kids[0].text == "if")
        route.kids[1].kids[1] = Node::Symbol("TRUE");
      if (!RewriteHeaderTransition(route, static_cast<LibraryHeader>(target),
                                   direction, helper)) {
        if (why) *why = std::string(panel) + " lacks its mouse header transition";
        return false;
      }
      click_routes.push_back(std::move(route));
    }
    for (int direction : {1, -1}) {
      std::vector<Node*> cases;
      if (buttons) FindTriggerCases(*buttons,
          direction > 0 ? "kAction_PageDown" : "kAction_PageUp", &cases);
      if (cases.size() != 1) {
        if (why) *why = std::string(panel) + " lacks its unique trigger case";
        return false;
      }
      Node& kase = *cases[0];
      const LibraryHeader next = AdjacentHeader(current, direction, options);
      if (next == current) {
        kase.kids = {kase.kids[0], HeaderArrow(direction)};
        continue;
      }
      if (current == LibraryHeader::kSongs) {
        if (kase.kids.size() != 2 || kase.kids[1].tag != ui::dtb::kCommand ||
            kase.kids[1].kids.size() < 4 || kase.kids[1].kids[0].text != "if") {
          if (why) *why = "song list lacks its offline navigation gate";
          return false;
        }
        kase.kids[1].kids[1] = Node::Symbol("TRUE");
      }
      if (!RewriteHeaderTransition(kase, next, direction, helper)) {
        if (why) *why = std::string(panel) + " lacks its header transition";
        return false;
      }
    }
    if (click_routes.size() > 1) {
      Node command = Node::Command(std::move(click_routes));
      if (auto* poll = PanelMethod(&staged, panel, "poll")) {
        poll->kids.push_back(std::move(command));
      } else {
        for (auto& root : staged) {
          if (root.tag == ui::dtb::kCommand && root.kids.size() >= 3 &&
              root.kids[0].text == "new" && root.kids[2].text == panel) {
            root.kids.push_back(Node::Array({Node::Symbol("poll"), std::move(command)}));
            break;
          }
        }
      }
    }
  }
  *roots = std::move(staged);
  return true;
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
    {"Engine References (RB3)", "band3_recomp by ihatecompvir"},
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

// The payload copy takes the same native discovery route as the in-flight edit.
std::vector<Node> RefreshRowBody() {
  return {Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                         Node::Symbol(menu_options::kRefreshRowDiscoverScreen)})};
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

bool ContainsNativeDiscovery(const Node& node) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() == 3 &&
      node.kids[0].text == "ui" && node.kids[1].text == "goto_screen" &&
      node.kids[2].text == menu_options::kRefreshRowDiscoverScreen) return true;
  for (const Node& kid : node.kids) {
    if (ContainsNativeDiscovery(kid)) return true;
  }
  return false;
}

bool SetRefreshRowAction(std::vector<ui::dtb::Node>* roots, bool refresh_row_active) {
  if (!refresh_row_active || roots == nullptr) return false;
  // The in-flight twin's guard, for the same reason: a case already entering discovery is left
  // alone rather than rewritten a second time.
  for (Node& root : *roots) {
    if (Node* row = FindRefreshRowCase(&root)) {
      for (size_t i = 1; i < row->kids.size(); ++i) {
        if (ContainsNativeDiscovery(row->kids[i])) return false;
      }
      break;
    }
  }
  std::string why;
  return ui::screen::RerouteRow(roots, menu_options::kDlcRowKey, RefreshRowBody(), &why);
}

bool SetDiscoveryStartLabel(std::vector<Node>* roots) {
  Node* method = PanelMethod(roots, "server_connect_panel", "update_state");
  if (!method) return false;
  const auto state_case = [](const Node& node, uint32_t number, const char* name) {
    return node.tag == ui::dtb::kArray && !node.kids.empty() &&
        ((node.kids[0].tag == ui::dtb::kInt && node.kids[0].scalar == number) ||
         (node.kids[0].tag == ui::dtb::kSymbol && node.kids[0].text == name));
  };
  const auto visit = [&](auto&& self, Node& node) -> bool {
    if (node.tag == ui::dtb::kCommand && node.kids.size() >= 3 &&
        node.kids[0].text == "switch" && node.kids[1].tag == ui::dtb::kVar &&
        node.kids[1].text == "state") {
      for (const auto& branch : node.kids) {
        if (state_case(branch, 28, "kServerConnectPanel_StartEnumeratingContent")) return true;
      }
      for (const auto& branch : node.kids) {
        if (state_case(branch, 29, "kServerConnectPanel_EnumeratingContent") &&
            branch.kids.size() == 2 && branch.kids[1].text == menu_options::kEnumeratingContentKey) {
          Node waiting = branch;
          // Numeric on purpose: the byte-neutral offline-prompt edit removes the
          // unused StartEnumeratingContent definition to pay for its transitions.
          waiting.kids[0] = Node::Int(28);
          node.kids.insert(node.kids.end() - 1, std::move(waiting));
          return true;
        }
      }
    }
    for (auto& child : node.kids) if (self(self, child)) return true;
    return false;
  };
  return visit(visit, *method);
}

bool AddGraphicsLoadingGate(std::vector<Node>* roots, std::string* why) {
  using namespace ui::dtb;
  if (!roots) return false;
  auto staged = *roots;
  Node* panel = nullptr;
  for (auto& root : staged)
    if (root.tag == kCommand && root.kids.size() >= 3 && root.kids[0].text == "new" &&
        root.kids[1].text == "ServerConnectPanel" && root.kids[2].text == "server_connect_panel") panel = &root;
  if (!panel || PanelMethod(&staged,"server_connect_panel","rbbz_begin_graphics")) {
    if (why) *why = "missing connect panel or graphics gate already installed";
    return false;
  }
  const auto sym = [](const char* s) { return Node::Symbol(s); };
  const auto cmd = [](std::vector<Node> n) { return Node::Command(std::move(n)); };
  const auto query = [&](const char* name) {
    return cmd({sym("file_exists"),Node::String(std::string("rbbz-graphics/")+name)});
  };
  const auto call = [&](const char* method) { return cmd({Node::Var("this"),sym(method)}); };
  const auto pending = Node::Property({sym("rbbz_graphics_pending")});
  const auto set_pending = [&](bool v) { return cmd({sym("set"),pending,sym(v?"TRUE":"FALSE")}); };
  Node continuation;
  size_t replaced = 0;
  const auto rewrite = [&](auto&& self, Node& n) -> void {
    for (size_t i = 0; i+1 < n.kids.size(); ++i) {
      const auto& a = n.kids[i]; const auto& b = n.kids[i+1];
      const bool state = a.tag == kCommand && a.kids.size() == 4 &&
          a.kids[0].tag == kCommand && a.kids[0].kids.size() == 2 &&
          a.kids[0].kids[0].text == "splash_panel" && a.kids[0].kids[1].text == "loaded_dir" &&
          a.kids[1].text == "set" && a.kids[2].text == "state" && a.kids[3].text == "main_menu";
      const bool jump = b.tag == kCommand && b.kids.size() == 3 && b.kids[0].text == "ui" &&
          b.kids[1].text == "goto_screen" && b.kids[2].text == "splash_screen";
      if (!state || !jump) continue;
      continuation = cmd({sym("do"),set_pending(false),a,b});
      n.kids[i] = call("rbbz_begin_graphics");
      n.kids.erase(n.kids.begin()+i+1);
      ++replaced;
    }
    for (auto& child : n.kids) self(self,child);
  };
  rewrite(rewrite,*panel);
  if (replaced != 2) {
    if (why) *why = "expected exactly Confirm and Option menu continuations";
    return false;
  }
  // A graphics failure is recoverable within this same loaded screen: A retries
  // only graphics; B explicitly accepts ordinary demand compilation/caching.
  auto* buttons = PanelMethod(&staged,"server_connect_panel","BUTTON_DOWN_MSG");
  if (!buttons) { if (why) *why = "missing connect input handler"; return false; }
  std::vector<Node> original{sym("do")};
  original.insert(original.end(),buttons->kids.begin()+1,buttons->kids.end());
  const auto recovery = cmd({sym("switch"),Node::Var("action"),
      Node::Array({sym("kAction_Confirm"),cmd({sym("if"),query("retry"),call("rbbz_graphics_poll")})}),
      Node::Array({sym("kAction_Cancel"),cmd({sym("if"),query("ignore"),call("rbbz_finish_graphics")})})});
  buttons->kids = {sym("BUTTON_DOWN_MSG"),cmd({sym("if_else"),
      cmd({sym("&&"),pending,query("failed")}),recovery,
      cmd({sym("if"),cmd({sym("||"),cmd({sym("!"),pending}),
          cmd({sym("=="),Node::Var("action"),sym("kAction_Cancel")})}),cmd(std::move(original))})})};
  const auto set_label = [&](const char* token) { return cmd({sym("status.lbl"),sym("set"),sym("text_token"),sym(token)}); };
  const auto preparing_label = cmd({sym("status.lbl"),sym("set_token_fmt"),sym("rbbz_graphics_preparing"),
      query("current"),query("total")});
  const auto showing = [&](const char* object, bool value) { return cmd({sym(object),sym("set_showing"),sym(value?"TRUE":"FALSE")}); };
  const auto directory_set = [&](const char* property, Node value) {
    return cmd({cmd({Node::Var("this"),sym("loaded_dir")}),sym("set"),sym(property),std::move(value)});
  };
  panel->kids.push_back(Node::Array({sym("rbbz_graphics_pending"),sym("FALSE")}));
  panel->kids.push_back(Node::Array({sym("rbbz_finish_graphics"),continuation}));
  panel->kids.push_back(Node::Array({sym("rbbz_begin_graphics"),cmd({sym("unless"),pending,
      set_pending(true),query("request"),call("rbbz_graphics_poll")})}));
  panel->kids.push_back(Node::Array({sym("rbbz_graphics_poll"),cmd({sym("if"),pending,
      cmd({sym("if_else"),query("ready"),call("rbbz_finish_graphics"),
          cmd({sym("do"),
              cmd({sym("if_else"),query("failed"),
                  cmd({sym("if_else"),query("failed_data"),set_label("rbbz_graphics_failed_data"),
                      cmd({sym("if_else"),query("failed_cache"),set_label("rbbz_graphics_failed_cache"),
                          set_label("rbbz_graphics_failed")})}),
                  cmd({sym("if_else"),query("checking"),set_label("rbbz_graphics_checking"),preparing_label})}),
              cmd({sym("continue.ihp"),sym("set_action_token"),sym("kAction_Confirm"),sym("rbbz_graphics_retry")}),
              cmd({sym("back.ihp"),sym("set_action_token"),sym("kAction_Cancel"),
                  cmd({sym("if_else"),query("failed"),sym("rbbz_graphics_ignore"),sym("rbbz_graphics_back")})}),
              cmd({sym("continue.ihp"),sym("set_showing"),query("failed")}),
              showing("skip.ihp",false),showing("back.ihp",true),
              showing("enter_name.grp",false),showing("enter_password.grp",false),
              directory_set("current_content",query("current")),directory_set("total_content",query("total"))})})})}));
  panel->kids.push_back(Node::Array({sym("poll"),call("rbbz_graphics_poll")}));
  if (auto* exit = PanelMethod(&staged,"server_connect_panel","exit")) {
    exit->kids.push_back(query("cancel")); exit->kids.push_back(set_pending(false));
  } else panel->kids.push_back(Node::Array({sym("exit"),query("cancel"),set_pending(false)}));
  *roots = std::move(staged);
  return true;
}

bool AddCrashRecoveryHooks(std::vector<Node>* roots, std::string* why) {
  if (!roots) return false;
  auto staged = *roots;
  auto* enter = PanelMethod(&staged, "splash_panel", "enter");
  auto* screen_enter = PanelMethod(&staged, "splash_screen", "enter");
  if (!enter || !screen_enter || PanelMethod(&staged, "splash_panel", "rbbz_recovery_boot")) {
    if (why) *why = "unsupported splash recovery lifecycle";
    return false;
  }
  const auto sym = [](const char* s) { return Node::Symbol(s); };
  const auto cmd = [](std::vector<Node> kids) { return Node::Command(std::move(kids)); };
  const auto query = [&](const char* leaf) {
    return cmd({sym("file_exists"), Node::String(std::string("rbbz-load/recovery/") + leaf)});
  };
  const auto set = [&](Node value) {
    return cmd({Node::Var("this"), sym("set"), sym("rbbz_recovery_boot"), std::move(value)});
  };
  enter->kids.push_back(set(query("active")));
  const auto start_action = cmd({sym("do"),
      cmd({sym("user_mgr"), sym("set_participating_user"), Node::Var("rbbz_recovery_user")}),
      query("starting"), cmd({sym("ui"), sym("goto_screen"), sym("server_connect_screen")})});
  const auto user = Node::Array({Node::Var("rbbz_recovery_user"),
      cmd({sym("user_mgr"), sym("get_user_from_pad_num"), Node::Int(0)})});
  const auto start = cmd({sym("if"), query("start"), cmd({sym("do"), user,
      cmd({sym("if"), Node::Var("rbbz_recovery_user"), start_action})})});
  const auto panel_state = cmd({cmd({sym("splash_panel"), sym("loaded_dir")}), sym("get"), sym("state")});
  const auto popup_condition = cmd({sym("&&"), cmd({sym("=="), panel_state, sym("main_menu")}), query("popup")});
  const auto popup_action = cmd({sym("do"), set(sym("FALSE")),
      cmd({sym("error_popup_panel"), sym("show_popup"),
           cmd({sym("if_else"), query("timeout30"), sym("rbbz_load_timeout_30"),
                cmd({sym("if_else"), query("timeout60"), sym("rbbz_load_timeout_60"), sym("rbbz_load_crashed")})}),
           sym("splash_panel")})});
  const auto popup = cmd({sym("if"), popup_condition, popup_action});
  const auto poll = cmd({sym("if"), Node::Property({sym("rbbz_recovery_boot")}),
                         cmd({sym("do"), start, popup})});
  if (auto* existing = PanelMethod(&staged, "splash_panel", "poll")) existing->kids.push_back(poll);
  else for (auto& root : staged) {
    if (root.tag == ui::dtb::kCommand && root.kids.size() >= 3 && root.kids[2].text == "splash_panel")
      root.kids.push_back(Node::Array({sym("poll"), poll}));
  }
  // Add the property after appending to enter: vector growth can invalidate it.
  for (auto& root : staged) {
    if (root.tag == ui::dtb::kCommand && root.kids.size() >= 3 && root.kids[2].text == "splash_panel")
      root.kids.push_back(Node::Array({sym("rbbz_recovery_boot"), sym("FALSE")}));
  }
  *roots = std::move(staged);
  return true;
}

bool SkipStartupLogos(std::vector<Node>* roots, std::string* why) {
  if (!roots) return false;
  Node* order = nullptr;
  Node* wait = nullptr;
  for (auto& root : *roots) {
    if (root.tag != ui::dtb::kArray || root.kids.empty() ||
        root.kids[0].tag != ui::dtb::kSymbol) continue;
    if (root.kids[0].text == "splash_order") order = &root;
    else if (root.kids[0].text == "wait_for_splash") wait = &root;
  }
  if (!order || !wait || wait->kids.size() != 2) {
    if (why) *why = "unsupported startup splash playlist";
    return false;
  }
  // Clear the complete list, including regional directives and provider logos.
  // startup_splasher.cpp guards the native scene operations on this empty list;
  // retail otherwise dereferences its sentinel as the first scene.
  order->kids.resize(1);
  wait->kids[1] = Node::Symbol("FALSE");
  return true;
}

bool BuildMenuRecords(const RecordReader& read, const std::vector<std::string>& locales,
                      MenuRecords* records, std::string* error, bool merge_provider,
                      bool refresh_row_active,
                      std::vector<std::string>* notes,
                      const std::vector<std::string>& hidden_menu_rows,
                      bool skip_startup_logos) {
  namespace screen = ui::screen;
  MenuRecords built;
  auto note = [notes](std::string line) {
    if (notes != nullptr) notes->push_back(std::move(line));
  };
  // Validate the effective provider's native popup contract before installing
  // a recovery route that depends on it. Never ship half of that route.
  {
    std::vector<uint8_t> popup_bytes;
    std::vector<Node> popup_roots;
    ui::dtb::RootHeader popup_header;
    if (!read("ui/error/gen/error_general.dtb", &popup_bytes, error) ||
        !ui::dtb::Parse(popup_bytes.data(), popup_bytes.size(), &popup_roots, &popup_header, error)) return false;
    const auto* popup = PanelMethod(&popup_roots, "error_popup_panel", "show_popup");
    if (!popup || popup->kids.size() < 3 || popup->kids[1].tag != ui::dtb::kArray ||
        popup->kids[1].kids.size() != 2 || popup->kids[1].kids[0].tag != ui::dtb::kVar ||
        popup->kids[1].kids[0].text != "error_token" || popup->kids[1].kids[1].tag != ui::dtb::kVar ||
        popup->kids[1].kids[1].text != "ret_panel") {
      if (error) *error = "unsupported native crash-popup arguments";
      return false;
    }
  }
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
  if (skip_startup_logos && !edit("ui/startup/gen/startup.dtb", [&](auto& roots) {
        return SkipStartupLogos(&roots, error);
      })) return false;
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
        if (!AddCrashRecoveryHooks(&roots, error)) return false;
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
        // The RhythmVerse entry (plan P1): built only when the store's master switch was on
        // for this boot, so a store the player turned off never draws a row that would refuse the
        // press. A refusal here is not fatal - the row simply is not there.
        if (screen::CurrentMenuConfiguration().rhythmverse_row) {
          std::string why;
          if (!screen::AddRhythmVerseRow(&roots, &why)) {
            note("the RhythmVerse row was refused: " + why);
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
        // The owner's row order (2026-10-23): Play, Play Current Song, RhythmVerse, Refresh Songs,
        // retail's own rows, then Exit Game last. After every insertion and after the hide
        // decision, so the rows it orders are all present; the rows it removes are already gone.
        {
          std::string why;
          if (!screen::OrderMainMenuRows(&roots, &why)) {
            note("ordering the main-menu rows was refused: " + why);
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
        if (!screen::ReorganizeAudioPage(&roots, error)) return false;
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
      for (const auto& [key,text] : std::initializer_list<std::pair<const char*,const char*>>{
          {"rbbz_graphics_checking","Checking graphics cache..."},
          {"rbbz_graphics_preparing","Preparing graphics... %i / %i"},
          {"rbbz_graphics_failed","Graphics preparation failed."},
          {"rbbz_graphics_failed_data","Game shader data is missing or unreadable."},
          {"rbbz_graphics_failed_cache","The graphics cache folder is not writable."},
          {"rbbz_graphics_retry","Try Again"},
          {"rbbz_graphics_ignore","Ignore"},
          {"rbbz_graphics_back","Skip"},
          {"rbbz_load_crashed","The game crashed while loading a song and was restarted. Returned to the main menu. Try another song."},
          {"rbbz_load_timeout_30","The song did not finish loading within 30 seconds. The game was restarted to the main menu. Try another song or check this song's files."},
          {"rbbz_load_timeout_60","The song did not finish loading within 60 seconds. The game was restarted to the main menu. Try another song or check this song's files."}}) {
        std::string why;
        if (!screen::AddLocaleEntry(&roots,key,text,&why) && why != "the locale already has that key") {
          if (error) *error = why;
          return false;
        }
      }
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
        // P5's button label. The button list is built from symbols (`insert_elem buttons 0
        // store_download_now`), and `component` resolves that symbol to a locale key of the same
        // name - retail's `store_download_again` row is spelled exactly like its own label key. So
        // the row this build inserts needs its key here, or it draws with no text at all.
        why.clear();
        if (!screen::AddLocaleEntry(&roots, "store_download_now", "Download", &why) &&
            why != "the locale already has that key") {
          note("the Download label was refused: " + why);
        }
        screen::AddLocaleEntry(&roots, "store_delete_song", "Delete", &why);
        screen::AddLocaleEntry(&roots, "store_checking_library", "Checking installed songs...", &why);
        // P9-7's footer caption: the store's Start button now opens the Search Song Filter page, so
        // its inline help names that page instead of the scene's `inline_help_options`
        // (store_payload.h's `SetStoreFooterStart` writes the token on both store screens).
        why.clear();
        if (!screen::AddLocaleEntry(&roots, store::kFooterSearchFiltersToken,
                                    store::kFooterSearchFiltersText, &why) &&
            why != "the locale already has that key") {
          note("the Search Filters footer label was refused: " + why);
        }
        // P9-10's failed-press sentence: the store row's case shows this in the main menu's own
        // error popup when the catalogue could not be reached (the same panel the store's download
        // modal borrows its resource from; this is that screen's own sentence).
        why.clear();
        if (!screen::AddLocaleEntry(&roots, screen::kStoreUnreachableKey,
                                    screen::kStoreUnreachableText, &why) &&
            why != "the locale already has that key") {
          note("the store-unreachable popup sentence was refused: " + why);
        }
        // P9-11: the title's own keyboard titles itself with `search_for_song` and describes
        // itself with `search_for_song_desc` (the two tokens the Ultimate payload's song search
        // passes, measured in its `song_select.dtb`). Retail's locale carries neither, so the
        // store screen's `open_store_search` would draw bare token names without them. The
        // description is the plan's own one-at-a-time sentence, so the rule is read while typing.
        why.clear();
        if (!screen::AddLocaleEntry(&roots, store::kSearchKeyboardTitleToken,
                                    store::kSearchKeyboardTitleText, &why) &&
            why != "the locale already has that key") {
          note("the search keyboard's title was refused: " + why);
        }
        why.clear();
        if (!screen::AddLocaleEntry(&roots, store::kSearchKeyboardDescToken,
                                    store::kSearchKeyboardDescText, &why) &&
            why != "the locale already has that key") {
          note("the search keyboard's description was refused: " + why);
        }
        for (const auto& [key, text] : {
                 std::pair{store::kSearchLoadingToken, store::kSearchLoadingText},
                 std::pair{store::kSearchCancelToken, store::kSearchCancelText},
                 std::pair{store::kSearchEmptyToken, store::kSearchEmptyText},
                 std::pair{store::kSearchFailedToken, store::kSearchFailedText},
                 std::pair{store::kSearchShortToken, store::kSearchShortText},
                 // P9-6's modal: the download popup's own sentences. The installed state composes
                 // its line from the offer, so it needs no entry here; P9-14 adds the prompt that
                 // state draws (A Refresh   B Close).
                 std::pair{store::kDownloadLoadingToken, store::kDownloadLoadingText},
                 std::pair{store::kDownloadCancelToken, store::kDownloadCancelText},
                 std::pair{store::kDownloadCloseToken, store::kDownloadCloseText},
                 std::pair{store::kDownloadRefreshToken, store::kDownloadRefreshText},
                 std::pair{store::kDownloadFailedToken, store::kDownloadFailedText}}) {
          why.clear();
          if (!screen::AddLocaleEntry(&roots, key, text, &why) &&
              why != "the locale already has that key") {
            note("the search popup label was refused: " + why);
          }
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
  // Independently enabled tabs share one cyclic route and one header appearance.
  const auto& menu_config = screen::CurrentMenuConfiguration();
  const LibraryHeaderOptions headers{menu_config.rhythmverse_row, menu_config.show_messages,
                                     menu_config.show_recommendations};
  if (!edit("ui/background/gen/background.dtb", [&](auto& roots) {
        return ConfigureLibraryHeaderBar(&roots, headers, error);
      })) return false;
  if (!edit("ui/gen/transition_screens.dtb", [&](auto& roots) {
        return ConfigureLibraryHeaderAnimations(&roots, error);
      })) return false;
  for (const auto& [path, tab] : std::array<std::pair<const char*, LibraryHeader>, 4>{{
      {"ui/song_select/gen/song_select.dtb", LibraryHeader::kSongs},
      {"ui/store/gen/store.dtb", LibraryHeader::kStore},
      {"ui/inbox/gen/inbox.dtb", LibraryHeader::kMessages},
      {"ui/recommendations/gen/recommendations.dtb", LibraryHeader::kRecommendations}}}) {
    if (!edit(path, [&](auto& roots) {
          return RouteLibraryHeaderNavigation(&roots, tab, headers, error);
        })) return false;
  }
  // P3's data lane (RhythmVerse plan §5/P3). With the store's master switch on, the retail store's
  // own DTA records are edited so the retail screens can show our catalogue:
  //
  //   * `store.dtb` - the commerce bypass (the panel starts its first request only from its own
  //     `on_init_commerce_success`, which offline never fires), the price footer retail shows on the
  //     Xbox branch of the panel's `enter` (§2.12), and P4's Search row (the gate that opens the
  //     authored keys while no answer is waiting).
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
    // Carry the hidden-header navigation edit into the RhythmVerse edits below.
    const auto header_record = built.find("ui/store/gen/store.dtb");
    const bool have_store = header_record != built.end();
    if (have_store) store_ui_bytes = header_record->second;
    if ((have_store || read("ui/store/gen/store.dtb", &store_ui_bytes, &store_ui_why)) &&
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
      // resolve (store_payload.h), so its loop reads the record's own `diff_<part>` tiers.
      if (!store::UnrollOfferDifficulty(&store_ui_roots, &why)) {
        note("unrolling the details panel's difficulty read was refused: " + why);
      }
      why.clear();
      // The details panel's own `art_loaded` message never fires for our synchronous delivery, so
      // the material is linked right after `load_art` returns (store_payload.h).
      if (!store::LinkAlbumArt(&store_ui_roots, &why)) {
        note("linking the details page's album art was refused: " + why);
      }
      // P4's entry from the store, re-pointed by P9-11 at the title's own keyboard: the front
      // menu's Search row opens the keyboard while no answer is waiting, and asks for the search
      // page (which the serving policy answers with the accepted answer) once one is. The row's
      // index is the one the front menu writer puts it at, so the DTA gate and the served record
      // cannot disagree. Both halves are screen methods the same edit adds (store_payload.h).
      why.clear();
      if (!store::AddSearchKeyboard(&store_ui_roots, &why)) {
        note("adding the store's search keyboard was refused: " + why);
      }
      why.clear();
      if (!store::RouteSearchRowToKeyboard(&store_ui_roots, store::kSearchMenuRow,
                                           store::kSearchKeyboardMethod, &why)) {
        note("routing the store's Search row to the keyboard was refused: " + why);
      }
      // P9-7: the store's Start button opens the Search Song Filter page, and Back from there
      // returns to the store front menu - the shipped case's own back target, re-pointed at a page
      // whose Back reads its current screen (store_payload.h).
      why.clear();
      if (!store::RouteStoreStartToFilter(&store_ui_roots, screen::kRhythmVerseFilterScreen, &why)) {
        note("routing the store's Start button to the filter page was refused: " + why);
      }
      // P9-7/P9-8: the store's own footer. Start reads SEARCH FILTERS (it now opens that page), and
      // Y's caption goes - the front menu's option case opens a trial upsell or a marquee offer we
      // never serve, and the browser's opens a shortcut list our provider never supplies.
      why.clear();
      if (!store::SetStoreFooterStart(&store_ui_roots, store::kFooterSearchFiltersToken, &why)) {
        note("the store's Start footer caption was refused: " + why);
      }
      why.clear();
      if (!store::ClearStoreFooterOption(&store_ui_roots, &why)) {
        note("hiding the store's View Item caption was refused: " + why);
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
      // P9-13: the box that carries the Download / Cancel rows carries the custom's own credit
      // block. It runs **after** the price-chrome transform, because it re-shows `cost.lbl` -
      // the scene's last free row - and the price transform's hides have to be the earlier
      // statements (store_payload.h).
      why.clear();
      if (!store::ShowOfferCredits(&info_roots, &why)) {
        note("writing the details page's credit block was refused: " + why);
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
      // `refresh_buttons` gates the Downloaded row on the offer's *native* `is_purchased`, which a
      // data-only custom never has, so the gate is redirected to the record's own key
      // (store_payload.h).
      if (!store::UseDataForDownloadedRow(&info_roots, &why)) {
        note("redirecting the details page's Downloaded row was refused: " + why);
      }
      why.clear();
      // The native `set_offer` enumeration reports an error for a served offer, so the browser's A
      // press populates the details screen directly (store_payload.h).
      if (!store::ShowOfferDetailsDirectly(&info_roots, &why)) {
        note("the details page's direct route was refused: " + why);
      }
      why.clear();
      // P5's row (store_payload.h): a free custom is never purchasable, so the page a served offer
      // opens would carry Exit alone - this inserts the Download row, and then rewrites its press
      // to reach the download worker through the request channel instead of the commerce object.
      if (!store::AddDownloadButton(&info_roots, &why)) {
        note("adding the details page's Download row was refused: " + why);
      }
      // P9-6: the modal the press opens. Defined on the details screen - the record that carries
      // the press - before the routes below write the press that shows it.
      why.clear();
      if (!store::AddDownloadPopup(&info_roots, &why)) {
        note("adding the details page's download popup was refused: " + why);
      }
      why.clear();
      if (!store::RouteDownloadButtons(&info_roots, &why)) {
        note("routing the details page's Download rows was refused: " + why);
      }
      why.clear();
      if (!store::AddLibraryManagement(&info_roots, &why)) {
        note("adding the details page's library management was refused: " + why);
      }
      // P9-8: the details page's Y caption goes too - its option case only opens a recommendation,
      // and this build never shows the recommendation band (store_payload.h).
      why.clear();
      if (!store::ClearDetailsFooterOption(&info_roots, &why)) {
        note("hiding the details page's View Item caption was refused: " + why);
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
  // This record may grow in the generated archive. Retain all original constants
  // so menu_filter's independent, byte-neutral offline transitions still apply.
  {
    constexpr char path[] = "ui/net/gen/server_connect.dtb";
    std::vector<uint8_t> bytes;
    std::vector<Node> roots;
    ui::dtb::RootHeader header;
    std::string why;
    if (read(path, &bytes, &why) &&
        ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &why) &&
        SetDiscoveryStartLabel(&roots) && AddGraphicsLoadingGate(&roots,&why)) {
      built[path] = ui::dtb::Compile(roots, header);
    } else {
      if (error) *error = "the required graphics loading gate could not be authored: " + why;
      return false;
    }
  }
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
