// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_payload.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "store/store_search.h"

namespace rb_blitz::store {
namespace {

using ui::dtb::Node;

bool IsSymbol(const Node& node, std::string_view name) {
  return node.tag == ui::dtb::kSymbol && node.text == name;
}

// An array whose first child is `name`: the shape a DTA definition has
// (`(servers (live ...) (local ...))`).
bool IsNamedArray(const Node& node, std::string_view name) {
  return node.tag == ui::dtb::kArray && !node.kids.empty() && IsSymbol(node.kids.front(), name);
}

Node* FindNetcacheInit(Node& node) {
  if (IsNamedArray(node, kNetcacheInitName)) {
    return &node;
  }
  for (Node& kid : node.kids) {
    if (Node* found = FindNetcacheInit(kid)) {
      return found;
    }
  }
  return nullptr;
}

// The shipped `local` entry is `(local (local TRUE) (root "<path>"))` — no `server`/`port`
// keys, because a local server has neither (out/_swap/config_store.tree.txt, verbatim).
Node LocalServerNode(std::string_view server_root) {
  return Node::Array({Node::Symbol("local"), Node::Array({Node::Symbol("local"),
                                                          Node::Symbol("TRUE")}),
                      Node::Array({Node::Symbol("root"), Node::String(std::string(server_root))})});
}

// `servers` / `default_server` occurrences, at any depth: the definition keeps one set per
// platform branch, and all of them are rewritten (see the header).
struct RewriteCounts {
  size_t servers = 0;
  size_t local_entries = 0;
  size_t default_servers = 0;
};

// The check pass: a refusal has to leave the record exactly as it was, so the decision is made
// before the first edit rather than in the middle of one.
size_t CountDefaults(const Node& node) {
  size_t count = IsNamedArray(node, "default_server") ? 1 : 0;
  for (const Node& kid : node.kids) {
    count += CountDefaults(kid);
  }
  return count;
}

void Rewrite(Node& node, std::string_view server_root, RewriteCounts* counts) {
  if (IsNamedArray(node, "servers")) {
    ++counts->servers;
    Node* local = nullptr;
    for (Node& kid : node.kids) {
      if (IsNamedArray(kid, "local")) {
        local = &kid;
        break;
      }
    }
    if (local != nullptr) {
      *local = LocalServerNode(server_root);
    } else {
      node.kids.push_back(LocalServerNode(server_root));
    }
    ++counts->local_entries;
  } else if (IsNamedArray(node, "default_server")) {
    // `(default_server live)`: the second child is the server's name, a symbol.
    ++counts->default_servers;
    if (node.kids.size() < 2) {
      node.kids.push_back(Node::Symbol("local"));
    } else {
      node.kids[1] = Node::Symbol("local");
    }
  }
  for (Node& kid : node.kids) {
    Rewrite(kid, server_root, counts);
  }
}

// `{net_cache_mgr init <arg>}`: a command whose first two children are the manager and `init`.
// The shipped argument is a command (`{find syscfg store netcache_init}`); the rewrite replaces it
// with an array, which is a value the interpreter passes through as-is.
//
// The owner vector is returned too, because the instrument inserts a `notify` line *before* the
// command: a boot log that carries that line is proof the guest loaded this record, which no path
// probe can show (records come out of the ark by offset).
struct InitCommandLocation {
  std::vector<Node>* owner = nullptr;
  size_t index = 0;
};

bool FindNetCacheInitCommand(std::vector<Node>& nodes, InitCommandLocation* out) {
  for (size_t i = 0; i < nodes.size(); ++i) {
    Node& node = nodes[i];
    if (node.tag == ui::dtb::kCommand && node.kids.size() >= 2 &&
        IsSymbol(node.kids[0], "net_cache_mgr") && IsSymbol(node.kids[1], "init")) {
      out->owner = &nodes;
      out->index = i;
      return true;
    }
    if (FindNetCacheInitCommand(node.kids, out)) {
      return true;
    }
  }
  return false;
}

// A method definition of a named object: `{new UIScreen store_menu_screen ... (enter ...) ...}`.
// The object name is the command's third child and each method is an array whose first child is
// the method name.
Node* FindNamedMethod(Node& node, std::string_view object, std::string_view method) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 4 && IsSymbol(node.kids[0], "new") &&
      IsSymbol(node.kids[2], object)) {
    for (size_t i = 3; i < node.kids.size(); ++i) {
      Node& kid = node.kids[i];
      if (kid.tag == ui::dtb::kArray && !kid.kids.empty() && IsSymbol(kid.kids.front(), method)) {
        return &kid;
      }
    }
  }
  for (Node& kid : node.kids) {
    if (Node* found = FindNamedMethod(kid, object, method)) {
      return found;
    }
  }
  return nullptr;
}

// `{new <class> <name> ...}` - the definition of one record object, matched by class as well as
// by name so a panel and a screen that share a name cannot be confused.
bool IsNewObject(const Node& node, std::string_view class_name, std::string_view object) {
  return node.tag == ui::dtb::kCommand && node.kids.size() >= 3 && IsSymbol(node.kids[0], "new") &&
         IsSymbol(node.kids[1], class_name) && IsSymbol(node.kids[2], object);
}

}  // namespace

bool PointNetcacheAtLocalServer(std::vector<ui::dtb::Node>* roots, std::string_view server_root,
                                std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (roots == nullptr) {
    return refuse("no record roots");
  }
  if (server_root.empty()) {
    return refuse("the local server root is empty");
  }
  Node* netcache = nullptr;
  for (Node& root : *roots) {
    netcache = FindNetcacheInit(root);
    if (netcache != nullptr) {
      break;
    }
  }
  if (netcache == nullptr) {
    return refuse(std::string("the record carries no ") + kNetcacheInitName + " definition");
  }
  RewriteCounts counts;
  if (CountDefaults(*netcache) == 0) {
    // A `servers` table with nothing selecting it measures nothing: say so rather than write a
    // record that looks rewritten.
    return refuse(std::string(kNetcacheInitName) + " has no default_server");
  }
  Rewrite(*netcache, server_root, &counts);
  return true;
}

bool PointUiNetcacheAtLocalServer(std::vector<ui::dtb::Node>* ui_roots,
                                  const ui::dtb::Node& definition, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (ui_roots == nullptr) {
    return refuse("no record roots");
  }
  if (!IsNamedArray(definition, kNetcacheInitName)) {
    return refuse(std::string("the definition is not a ") + kNetcacheInitName + " array");
  }
  InitCommandLocation location;
  bool found = false;
  for (size_t i = 0; i < ui_roots->size() && !found; ++i) {
    Node& root = (*ui_roots)[i];
    // The shipped record keeps the command at the top level, so the root itself is the first
    // candidate; the recursive search then covers a record that nests it.
    if (root.tag == ui::dtb::kCommand && root.kids.size() >= 2 &&
        IsSymbol(root.kids[0], "net_cache_mgr") && IsSymbol(root.kids[1], "init")) {
      location.owner = ui_roots;
      location.index = i;
      found = true;
    } else {
      found = FindNetCacheInitCommand(root.kids, &location);
    }
  }
  if (!found) {
    return refuse("the record carries no net_cache_mgr init command");
  }
  // The marker goes in front of the command, not into the command's own children (those are its
  // arguments). `{notify "..."}` is the retail scripts' own debug line: the DTA's Debug::Print
  // lands in this project's log through `DbgPrint`.
  location.owner->insert(location.owner->begin() + location.index,
                         Node::Command({Node::Symbol("notify"),
                                        Node::String("rbbz: netcache manager gets a local "
                                                     "definition from ui/gen/ui.dtb")}));
  Node& init = (*location.owner)[location.index + 1];
  if (init.kids.size() >= 3) {
    init.kids[2] = definition;
  } else {
    init.kids.push_back(definition);
  }
  return true;
}

const ui::dtb::Node* FindNetcacheInit(const std::vector<ui::dtb::Node>& roots) {
  for (const Node& root : roots) {
    if (IsNamedArray(root, kNetcacheInitName)) {
      return &root;
    }
  }
  return nullptr;
}

bool AddStoreEntryBypass(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* enter = nullptr;
  for (Node& root : *store_ui_roots) {
    enter = FindNamedMethod(root, "store_menu_screen", "enter");
    if (enter != nullptr) {
      break;
    }
  }
  if (enter == nullptr) {
    return refuse("the record carries no store_menu_screen enter method");
  }
  for (const Node& kid : enter->kids) {
    if (kid.tag == ui::dtb::kCommand && kid.kids.size() >= 2 &&
        IsSymbol(kid.kids[0], "store_panel") &&
        IsSymbol(kid.kids[1], "on_init_commerce_success")) {
      return refuse("the enter method already drives the commerce callback");
    }
  }
  enter->kids.push_back(
      Node::Command({Node::Symbol("store_panel"), Node::Symbol("on_init_commerce_success")}));
  return true;
}

namespace {

// `{store_panel request <name> TRUE}` - the statement the Search row's gate wraps.
bool IsStoreRequestCommand(const Node& node) {
  return node.tag == ui::dtb::kCommand && node.kids.size() >= 2 &&
         IsSymbol(node.kids[0], "store_panel") && IsSymbol(node.kids[1], "request");
}

// The first request command at any depth, with the vector that owns it (so the caller can replace
// it in place).
bool FindStoreRequestCommand(std::vector<Node>& nodes, std::vector<Node>** owner, size_t* index) {
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (IsStoreRequestCommand(nodes[i])) {
      *owner = &nodes;
      *index = i;
      return true;
    }
    if (FindStoreRequestCommand(nodes[i].kids, owner, index)) {
      return true;
    }
  }
  return false;
}

// The gate's own query, anywhere in a method: a second pass has to refuse rather than nest one
// wrapper inside another.
bool MentionsSearchGate(const Node& node) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 2 && IsSymbol(node.kids[0], "file_exists") &&
      node.kids[1].tag == ui::dtb::kString && node.kids[1].text == kSearchShowQuery) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (MentionsSearchGate(kid)) {
      return true;
    }
  }
  return false;
}

// `{file_exists "<path>"}` - the one-bit read the P1 entry gate already uses.
Node FileExistsCommand(std::string_view path) {
  return Node::Command({Node::Symbol("file_exists"), Node::String(std::string(path))});
}

// `{write_file "<path>" 0}` - the channel's one-way write, the shape every store command uses.
Node WriteCommand(std::string_view path) {
  return Node::Command({Node::Symbol("write_file"), Node::String(std::string(path)), Node::Int(0)});
}

// The shipped Search row's own three statements (`store_menu_panel.SELECT_MSG`), speaking for the
// row: the source symbol the row builds (`menu_<id>`), the page it asks for, and the loading state
// its own footer reads. Written out because a screen method has none of the row's locals.
Node SearchPageRequest() {
  return Node::Command(
      {Node::Symbol("do"),
       Node::Command({Node::Symbol("store_panel"), Node::Symbol("set_source"),
                      Node::Symbol("menu_search"), Node::Symbol("TRUE")}),
       Node::Command({Node::Symbol("store_panel"), Node::Symbol("request"),
                      Node::String(kSearchPageRequest), Node::Symbol("TRUE")}),
       Node::Command({Node::Symbol("store_panel"), Node::Symbol("update_loading_status")})});
}

// `{{ui current_screen} <method>}` - a screen method called on whichever screen is current. The
// store's own record already calls methods on a computed receiver (`{{ui current_screen}
// set_focus_panel $this`), which is why the keyboard entry does not have to name the screen.
Node CurrentScreenMethod(std::string_view method) {
  return Node::Command(
      {Node::Command({Node::Symbol("ui"), Node::Symbol("current_screen")}),
       Node::Symbol(std::string(method))});
}

}  // namespace

bool RouteSearchRowToKeyboard(std::vector<ui::dtb::Node>* store_ui_roots, int search_row,
                              std::string_view keyboard_method, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  if (search_row < 0) {
    return refuse("the front menu carries no search row");
  }
  if (keyboard_method.empty()) {
    return refuse("no keyboard method to call");
  }
  Node* select = nullptr;
  for (Node& root : *store_ui_roots) {
    select = FindNamedMethod(root, "store_menu_panel", "SELECT_MSG");
    if (select != nullptr) {
      break;
    }
  }
  if (select == nullptr) {
    return refuse("the record carries no store_menu_panel SELECT_MSG method");
  }
  if (MentionsSearchGate(*select)) {
    return refuse("the Search row is already routed to the keyboard");
  }
  std::vector<Node>* owner = nullptr;
  size_t index = 0;
  if (!FindStoreRequestCommand(select->kids, &owner, &index)) {
    return refuse("SELECT_MSG carries no store_panel request command");
  }
  // The two branches share the shipped statement by value: the DTA has no callable, and a copy of
  // the measured body is exactly as retail wrote it.
  const Node request = (*owner)[index];
  Node gate = Node::Command(
      {Node::Symbol("if_else"),
       Node::Command({Node::Symbol("=="), Node::Var("ix"),
                      Node::Int(static_cast<uint32_t>(search_row))}),
       Node::Command({Node::Symbol("if_else"), FileExistsCommand(kSearchShowQuery), request,
                      CurrentScreenMethod(keyboard_method)}),
       request});
  (*owner)[index] = std::move(gate);
  return true;
}

bool AddSearchKeyboard(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  // Both targets are located before either is written, so a refusal leaves the record as shipped.
  Node* screen = nullptr;
  Node* panel_exit = nullptr;
  for (Node& root : *store_ui_roots) {
    if (screen == nullptr && IsNewObject(root, "UIScreen", "store_menu_screen")) {
      screen = &root;
    }
    if (panel_exit == nullptr) {
      panel_exit = FindNamedMethod(root, "store_menu_panel", "exit");
    }
  }
  if (screen == nullptr) {
    return refuse("the record carries no store_menu_screen definition");
  }
  if (panel_exit == nullptr) {
    return refuse("the record carries no store_menu_panel exit method");
  }
  for (const Node& kid : screen->kids) {
    if (kid.tag == ui::dtb::kArray && !kid.kids.empty() && IsSymbol(kid.kids[0], "open_store_search")) {
      return refuse("the store screen already carries the keyboard entry");
    }
  }

  // The keyboard entry: the strip's guidance first (so the one-at-a-time rule is on screen while
  // the player types), then either the seeded submit - the measurement switch - or the title's own
  // keyboard, with `$this` (the screen) as the receiver of its result message.
  //
  // The first argument is a **User object**, not the bare symbol: the native show
  // (`sub_8237B008`) asks it for the player's name through its first virtual slot before it calls
  // the platform keyboard, and a symbol names no object - the measured result was
  // `read of guest 0x00000000` right after the press (2026-10-23). The store is reachable with the
  // participating user (retail's front-menu gate) or, offline, with pad 0's user, so the body binds
  // the first that answers and dismisses the surface when neither does.
  const Node keyboard = Node::Command(
      {Node::Symbol("virtual_keyboard"), Node::Symbol("show_keyboard"), Node::Var("user"),
       Node::Int(100),
       Node::Command({Node::Symbol("localize"), Node::Symbol("search_for_song")}),
       Node::Command({Node::Symbol("localize"), Node::Symbol("search_for_song_desc")}),
       Node::String(""), Node::Var("this")});
  const Node bind_participating_user = Node::Array(
      {Node::Var("user"),
       Node::Command({Node::Symbol("user_mgr"), Node::Symbol("get_participating_user")})});
  const Node bind_pad_zero_user =
      Node::Array({Node::Var("user"), Node::Command({Node::Symbol("user_mgr"),
                                                     Node::Symbol("get_user_from_pad_num"),
                                                     Node::Int(0)})});
  const Node show_with_user = Node::Command(
      {Node::Symbol("if_else"), Node::Var("user"), keyboard,
       Node::Command({Node::Symbol("do"), bind_pad_zero_user,
                      Node::Command({Node::Symbol("if_else"), Node::Var("user"), keyboard,
                                     WriteCommand(kSearchLeaveWrite)})})});
  const Node open = Node::Command(
      {Node::Symbol("do"), WriteCommand("rbbz-store/search/open"),
       Node::Command({Node::Symbol("if_else"), FileExistsCommand(kSearchSeedQuery),
                      Node::Command({Node::Symbol("do"),
                                     WriteCommand("rbbz-store/search/done"),
                                     SearchPageRequest()}),
                      Node::Command({Node::Symbol("do"), bind_participating_user,
                                     show_with_user})})});
  screen->kids.push_back(Node::Array({Node::Symbol("open_store_search"), open}));

  // The result: `$ok` true writes the typed text and then the submit, in that order, so the session
  // receives them as one query. `$ok` false is the keyboard's Cancel, which dismisses the surface
  // (the same `leave` a store exit writes) and touches nothing else - the field and any earlier
  // answer stay, so the Search row still lists what it had.
  //
  // The two values arrive positionally, so the method carries a parameter list and a body - the
  // shape retail's own engine-sent handlers use (`store_info.dtb`'s
  // `(preview_download_complete_msg ($bSuccess) …)`).
  const Node write_text = Node::Command(
      {Node::Symbol("write_file"),
       Node::Command({Node::Symbol("sprintf"), Node::String("rbbz-store/search/text/%s"),
                      Node::Var("text")}),
       Node::Int(0)});
  const Node result = Node::Command(
      {Node::Symbol("if_else"), Node::Var("ok"),
       Node::Command({Node::Symbol("do"), write_text, WriteCommand("rbbz-store/search/done"),
                      SearchPageRequest()}),
       WriteCommand(kSearchLeaveWrite)});
  const Node result_params = Node::Array({Node::Var("ok"), Node::Var("text")});
  screen->kids.push_back(
      Node::Array({Node::Symbol(kSearchKeyboardResultMethod), result_params, result}));

  // Leaving the store menu ends the search surface: the strip stops, the field and the answer stay.
  panel_exit->kids.push_back(WriteCommand(kSearchLeaveWrite));
  return true;
}

namespace {

// `{<label> set_showing TRUE}` / `{<label> set_showing 1}` on one of the store's cost labels.
bool IsCostShow(const Node& node) {
  if (node.tag != ui::dtb::kCommand || node.kids.size() < 3) {
    return false;
  }
  if (!IsSymbol(node.kids[0], "cost.lbl") && !IsSymbol(node.kids[0], "cost_ps3.lbl")) {
    return false;
  }
  if (!IsSymbol(node.kids[1], "set_showing")) {
    return false;
  }
  const Node& value = node.kids[2];
  return (value.tag == ui::dtb::kSymbol && value.text == "TRUE") ||
         (value.tag == ui::dtb::kInt && value.scalar != 0);
}

// Removes every cost-showing statement at any depth of the method, then hides both labels once more
// so a body that re-shows them through a branch this pass did not recognise still ends hidden.
void StripCostShows(Node& node) {
  for (Node& kid : node.kids) {
    StripCostShows(kid);
  }
  node.kids.erase(std::remove_if(node.kids.begin(), node.kids.end(), IsCostShow),
                  node.kids.end());
}

}  // namespace

bool HideStorePriceChrome(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* enter = nullptr;
  for (Node& root : *store_ui_roots) {
    enter = FindNamedMethod(root, "store_panel", "enter");
    if (enter != nullptr) {
      break;
    }
  }
  if (enter == nullptr) {
    return refuse("the record carries no store_panel enter method");
  }
  // The shipped `enter` carries `#ifdef HX_PS3 {microsoft.lbl set_showing FALSE} #else {microsoft.lbl
  // set_showing TRUE} #endif`, so a scan for the command anywhere in the method would find the
  // PS3 branch's own hide and conclude it was already rewritten. Only a statement **after** the
  // last `#endif` is ours.
  size_t start = 1;  // the method's own name is kid 0
  for (size_t i = 0; i < enter->kids.size(); ++i) {
    if (enter->kids[i].tag == ui::dtb::kEndif) {
      start = i + 1;
    }
  }
  for (size_t i = start; i < enter->kids.size(); ++i) {
    const Node& kid = enter->kids[i];
    if (kid.tag == ui::dtb::kCommand && kid.kids.size() >= 3 &&
        IsSymbol(kid.kids[0], "microsoft.lbl") && IsSymbol(kid.kids[1], "set_showing") &&
        IsSymbol(kid.kids[2], "FALSE")) {
      if (why != nullptr) why->clear();  // already rewritten; an idempotent second pass
      return true;
    }
  }
  // Appended after the shipped `#ifdef HX_PS3` block, which is what shows the label on this build.
  enter->kids.push_back(Node::Command({Node::Symbol("microsoft.lbl"),
                                       Node::Symbol("set_showing"), Node::Symbol("FALSE")}));
  if (why != nullptr) why->clear();
  return true;
}

bool HideStoreInfoPriceChrome(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* panel = nullptr;
  for (Node& root : *store_info_roots) {
    panel = FindNamedMethod(root, "store_info_panel", "refresh_panel");
    if (panel != nullptr) {
      break;
    }
  }
  if (panel == nullptr) {
    return refuse("the record carries no store_info_panel refresh_panel method");
  }
  StripCostShows(*panel);
  panel->kids.push_back(Node::Command(
      {Node::Symbol("cost.lbl"), Node::Symbol("set_showing"), Node::Symbol("FALSE")}));
  panel->kids.push_back(Node::Command(
      {Node::Symbol("cost_ps3.lbl"), Node::Symbol("set_showing"), Node::Symbol("FALSE")}));
  if (why != nullptr) why->clear();
  return true;
}

namespace {

// The `if_else` whose test is `{<receiver> is_completely_unavailable}`, collected from every panel
// the record defines (both `store_info_enum_panel` and `store_offer_fetch_panel` carry one).
void CollectUnavailableGuards(Node& node, std::vector<Node*>* guards) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 2 && IsSymbol(node.kids[0], "if_else")) {
    const Node& test = node.kids[1];
    if (test.tag == ui::dtb::kCommand && test.kids.size() >= 2 &&
        IsSymbol(test.kids[1], "is_completely_unavailable")) {
      guards->push_back(&node);
    }
  }
  for (Node& kid : node.kids) {
    CollectUnavailableGuards(kid, guards);
  }
}

// An `if_else` whose test is the literal `FALSE` - the shape this edit leaves behind.
bool HasFalseGuard(const Node& node) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 2 && IsSymbol(node.kids[0], "if_else") &&
      IsSymbol(node.kids[1], "FALSE")) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasFalseGuard(kid)) return true;
  }
  return false;
}

// `{<receiver> <method> <args...>}` anywhere in the method. A receiver is spelled `this`, an object
// name or a variable, so only the names are compared.
bool HasCommand(const Node& node, std::string_view receiver, std::string_view method) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 2 && node.kids[0].text == receiver &&
      node.kids[1].text == method) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasCommand(kid, receiver, method)) return true;
  }
  return false;
}

// A local variable's name. The compiled stream spells parameters and locals as `kVar` nodes ($name),
// not symbols - measured on the shipped records, where `offer`, `part`, `rank`, `tier`, `idd` and
// `this` all parse as 0x02 - so a scan that only understood symbols would never see them.
bool IsVariable(const Node& node, std::string_view name) {
  return (node.tag == ui::dtb::kVar || node.tag == ui::dtb::kSymbol) && node.text == name;
}

}  // namespace

bool AlwaysShowOfferDetails(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  std::vector<Node*> guards;
  for (Node& root : *store_info_roots) {
    CollectUnavailableGuards(root, &guards);
  }
  if (guards.empty()) {
    // Already rewritten? Then the guards test the literal `FALSE`, which is the state this edit
    // leaves behind - an idempotent second pass, not a refusal.
    for (const Node& root : *store_info_roots) {
      if (HasFalseGuard(root)) {
        if (why != nullptr) why->clear();
        return true;
      }
    }
    return refuse("the record carries no is_completely_unavailable guard");
  }
  // `{if_else FALSE <then> <else>}`: the else branch (the details screen) always runs.
  for (Node* guard : guards) {
    guard->kids[1] = Node::Symbol("FALSE");
  }
  if (why != nullptr) why->clear();
  return true;
}

bool ShowOfferDetailsDirectly(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* method = nullptr;
  for (Node& root : *store_info_roots) {
    method = FindNamedMethod(root, "store_info_enum_panel", "enum_and_show_offer");
    if (method != nullptr) {
      break;
    }
  }
  if (method == nullptr) {
    return refuse("the record carries no store_info_enum_panel enum_and_show_offer method");
  }
  // The rewritten body is exactly the retail `on_enum_finished` else branch; its marker is the
  // direct jump to the details screen plus the populate commands.
  if (HasCommand(*method, "ui", "goto_screen") &&
      HasCommand(*method, "store_info_panel", "clear_data")) {
    if (why != nullptr) why->clear();
    return true;
  }
  if (method->kids.size() < 2 || method->kids[1].tag != ui::dtb::kArray ||
      method->kids[1].kids.size() != 1 || !IsVariable(method->kids[1].kids[0], "offer")) {
    return refuse("enum_and_show_offer does not take a single `offer` argument");
  }
  // Keep the name and the parameter list; drop the retail body (which went through
  // `store_info_enum_screen` and the native enumeration).
  method->kids.resize(2);
  method->kids.push_back(
      Node::Command({Node::Symbol("do"),
                     Node::Command({Node::Symbol("store_info_panel"), Node::Symbol("clear_data")}),
                     Node::Command({Node::Symbol("store_info_panel"), Node::Symbol("set"),
                                    Node::Symbol("cur_offer"), Node::Var("offer")}),
                     Node::Command({Node::Symbol("store_details_panel"), Node::Symbol("refresh"),
                                    Node::Var("offer")}),
                     Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                                    Node::Symbol("store_info_screen")})}));
  if (why != nullptr) why->clear();
  return true;
}

namespace {

// `{insert_elem <list> <pos> <row>}` for the preview row, anywhere in the method.
bool IsPreviewRowInsert(const Node& node) {
  if (node.tag != ui::dtb::kCommand || node.kids.size() < 3 || !IsSymbol(node.kids[0], "insert_elem")) {
    return false;
  }
  return node.kids.back().text == "store_preview_audio";
}

// Removes every preview-row insertion, at any depth.
void StripPreviewInsertions(Node& node) {
  for (Node& kid : node.kids) {
    StripPreviewInsertions(kid);
  }
  node.kids.erase(std::remove_if(node.kids.begin(), node.kids.end(), IsPreviewRowInsert),
                  node.kids.end());
}

}  // namespace

bool HidePreviewButton(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* method = nullptr;
  for (Node& root : *store_info_roots) {
    method = FindNamedMethod(root, "store_info_panel", "refresh_buttons");
    if (method != nullptr) {
      break;
    }
  }
  if (method == nullptr) {
    return refuse("the record carries no store_info_panel refresh_buttons method");
  }
  StripPreviewInsertions(*method);
  if (why != nullptr) why->clear();
  return true;
}

namespace {

// `{offer is_purchased}` - the **native** read. The offer object's own message handler answers
// `is_purchased` from its commerce state, so a data key of that name is never consulted: measured
// 2026-10-22 with the record carrying `(is_purchased TRUE)` (the trace line counted 2 of 99 marked)
// the details page drew Exit alone (`out/_swap/store-probe/20261022-091435`). The handler is
// `sub_82453928`, whose literals are the offer properties it answers itself (`get_data`,
// `has_data`, `offer_type`, `year_released`, `genre`, `is_purchased`, ...).
bool IsNativePurchasedRead(const Node& node) {
  return node.tag == ui::dtb::kCommand && node.kids.size() == 2 &&
         node.kids[0].tag == ui::dtb::kVar && node.kids[0].text == "offer" &&
         IsSymbol(node.kids[1], "is_purchased");
}

// The replacement is the **data** read retail's own method already uses for `preview_audio`
// (`{offer get_data (preview_audio)}`): a one-component path, which the offer's walk resolves
// (unlike the two-component `(rank <part>)` that `UnrollOfferDifficulty` had to replace).
bool IsDataPurchasedRead(const Node& node) {
  if (node.tag != ui::dtb::kCommand || node.kids.size() != 3) return false;
  if (node.kids[0].tag != ui::dtb::kVar || node.kids[0].text != "offer") return false;
  if (!IsSymbol(node.kids[1], "get_data")) return false;
  const Node& path = node.kids[2];
  return path.tag == ui::dtb::kArray && path.kids.size() == 1 &&
         IsSymbol(path.kids[0], "is_purchased");
}

Node DataPurchasedRead() {
  return Node::Command(
      {Node::Var("offer"), Node::Symbol("get_data"), Node::Array({Node::Symbol("is_purchased")})});
}

size_t RewritePurchasedReads(Node& node) {
  size_t rewritten = 0;
  for (Node& kid : node.kids) {
    if (IsNativePurchasedRead(kid)) {
      kid = DataPurchasedRead();
      ++rewritten;
      continue;
    }
    rewritten += RewritePurchasedReads(kid);
  }
  return rewritten;
}

bool HasDataPurchasedRead(const Node& node) {
  if (IsDataPurchasedRead(node)) return true;
  for (const Node& kid : node.kids) {
    if (HasDataPurchasedRead(kid)) return true;
  }
  return false;
}

}  // namespace

bool UseDataForDownloadedRow(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* method = nullptr;
  for (Node& root : *store_info_roots) {
    method = FindNamedMethod(root, "store_info_panel", "refresh_buttons");
    if (method != nullptr) {
      break;
    }
  }
  if (method == nullptr) {
    return refuse("the record carries no store_info_panel refresh_buttons method");
  }
  if (HasDataPurchasedRead(*method)) {
    if (why != nullptr) why->clear();  // already rewritten
    return true;
  }
  if (RewritePurchasedReads(*method) == 0) {
    return refuse("refresh_buttons has no `{offer is_purchased}` gate to redirect");
  }
  if (why != nullptr) why->clear();
  return true;
}

namespace {

// The two button symbols this build adds or rewrites (store_payload.h).
constexpr char kDownloadNowButton[] = "store_download_now";
constexpr char kDownloadAgainButton[] = "store_download_again";

// `{insert_elem buttons 0 <symbol>}` for one of those rows. The list itself is a **local** in the
// shipped record (`[var] buttons`) and a symbol in a hand-built one, so only the spelling is pinned.
bool IsInsertButton(const Node& node, std::string_view symbol) {
  return node.tag == ui::dtb::kCommand && node.kids.size() >= 4 &&
         IsSymbol(node.kids[0], "insert_elem") && node.kids[1].text == "buttons" &&
         IsSymbol(node.kids[3], symbol);
}

bool HasInsertButton(const Node& node, std::string_view symbol) {
  if (IsInsertButton(node, symbol)) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasInsertButton(kid, symbol)) {
      return true;
    }
  }
  return false;
}

// The `{if_else <test> <then> <else>}` whose then-branch is retail's Download Again row. The
// shipped record puts the insert straight in the branch; a branch wrapped in a `do` counts too, so
// the match is on the insert being *inside* it. `kids` is the vector the caller edits in place;
// `else_index` is 3 when the branch exists and the vector's size when the record gave none (the
// statement is then appended instead of replaced).
bool FindDownloadedGate(std::vector<Node>& nodes, std::vector<Node>** gate, size_t* else_index) {
  for (Node& node : nodes) {
    if (node.tag == ui::dtb::kCommand && IsSymbol(node.kids[0], "if_else") &&
        node.kids.size() >= 3 && HasInsertButton(node.kids[2], kDownloadAgainButton)) {
      *gate = &node.kids;
      *else_index = node.kids.size() >= 4 ? 3 : node.kids.size();
      return true;
    }
    if (FindDownloadedGate(node.kids, gate, else_index)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool AddDownloadButton(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* method = nullptr;
  for (Node& root : *store_info_roots) {
    method = FindNamedMethod(root, "store_info_panel", "refresh_buttons");
    if (method != nullptr) {
      break;
    }
  }
  if (method == nullptr) {
    return refuse("the record carries no store_info_panel refresh_buttons method");
  }
  if (HasInsertButton(*method, kDownloadNowButton)) {
    if (why != nullptr) why->clear();  // already added
    return true;
  }
  std::vector<Node>* gate = nullptr;
  size_t else_index = 0;
  if (!FindDownloadedGate(method->kids, &gate, &else_index)) {
    return refuse("refresh_buttons inserts no store_download_again row");
  }
  // The new row's own insert, then retail's else branch **by value**: the DTA has no callable, and
  // keeping the shipped statement inside a `do` is exactly what the record meant. `{do ...}` takes
  // its statements as its own children (`<command> unary do {…} {…}` in the record's own spelling),
  // not as one array - a `do` around an array is a single statement the guest would not dispatch.
  Node insert_now = Node::Command({Node::Symbol("insert_elem"), Node::Var("buttons"), Node::Int(0),
                                   Node::Symbol(kDownloadNowButton)});
  if (else_index >= gate->size()) {
    gate->push_back(std::move(insert_now));
  } else {
    Node shipped = std::move((*gate)[else_index]);
    (*gate)[else_index] = Node::Command(
        {Node::Symbol("do"), std::move(insert_now), std::move(shipped)});
  }
  if (why != nullptr) why->clear();
  return true;
}

namespace {

// One button's press, as store_payload.h describes it. Every statement is a shape the shipped
// `store_info_panel.refresh_buttons` itself uses, which is the only reason this can be written
// without a boot per statement:
//
//   * `{var $offer {property cur_offer}}` - the binding it makes before reading an offer's data.
//     A `get_data` sent straight at the property receiver is *not* that shape, and the first cut
//     died on it: `read of guest 0x00000000` from `store_info_screen` the frame the row was pressed
//     (`out/_swap/store-probe/20261022-115913`, log `rb_blitz_212.log`, 12:04:03).
//   * `{offer has_data job}` - its own guard before the read, the one that keeps a missing key from
//     being walked (`has_data preview_art`, same method). A record that carries no job therefore
//     answers with a message instead of a null read.
//   * `{sprintf "%s%s" … {offer get_data (preview_audio)}}` - a built path, and the same nested
//     command as its argument.
//   * `{write_file $path 0}` - the request channel's write side, the one builtin every settings row
//     is already pressed through.
Node DownloadRequest(std::string_view command) {
  // `{do ($offer {property cur_offer}) ...}` - a **binding statement**, which is an array in the
  // compiled record (`(Var value...)`), not a `{var $offer …}` command: the text form's `{var}` and
  // the compiled form's binding are the same thing to the guest, and a `var` **command** inside a
  // `do` is not. Measured 2026-10-22, the boot that first pressed the row: the literal write before
  // this bound (`refusing a malformed write: rbbz-store/download/start/--`) reached the host, and
  // the next statement - this binding as a command - died on `read of 0x00000000`.
  Node bind_offer =
      Node::Array({Node::Var("offer"), Node::Property({Node::Symbol("cur_offer")})});
  Node read = Node::Command({Node::Symbol("sprintf"),
                             Node::String("rbbz-store/download/" + std::string(command) + "/%s"),
                             Node::Command({Node::Var("offer"), Node::Symbol("get_data"),
                                            Node::Array({Node::Symbol("job")})})});
  Node bind_path = Node::Array({Node::Var("path"), std::move(read)});
  Node press = Node::Command(
      {Node::Symbol("do"), std::move(bind_path),
       Node::Command({Node::Symbol("write_file"), Node::Var("path"), Node::Int(0)})});
  Node no_job = Node::Command(
      {Node::Symbol("notify"), Node::String("This offer carries no download job")});
  return Node::Command(
      {Node::Symbol("do"), std::move(bind_offer),
       Node::Command({Node::Symbol("if_else"),
                      Node::Command({Node::Var("offer"), Node::Symbol("has_data"),
                                     Node::Symbol("job")}),
                      std::move(press), std::move(no_job)})});
}

// The `{switch {component selected_sym} ...}` a method dispatches its buttons with, plus the vector
// its case arrays live in.
bool FindButtonSwitch(std::vector<Node>& nodes, std::vector<Node>** cases) {
  for (Node& node : nodes) {
    if (node.tag == ui::dtb::kCommand && IsSymbol(node.kids[0], "switch") &&
        node.kids.size() >= 3) {
      *cases = &node.kids;
      return true;
    }
    if (FindButtonSwitch(node.kids, cases)) {
      return true;
    }
  }
  return false;
}

size_t FindButtonCase(const std::vector<Node>& cases, std::string_view symbol) {
  for (size_t i = 2; i < cases.size(); ++i) {  // 0 = `switch`, 1 = the test, 2.. = one array per case
    const Node& node = cases[i];
    if (node.tag == ui::dtb::kArray && !node.kids.empty() && IsSymbol(node.kids[0], symbol)) {
      return i;
    }
  }
  return cases.size();
}

}  // namespace

bool RouteDownloadButtons(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* select = nullptr;
  for (Node& root : *store_info_roots) {
    select = FindNamedMethod(root, "store_info_panel", "SELECT_MSG");
    if (select != nullptr) {
      break;
    }
  }
  if (select == nullptr) {
    return refuse("the record carries no store_info_panel SELECT_MSG method");
  }
  std::vector<Node>* cases = nullptr;
  if (!FindButtonSwitch(select->kids, &cases)) {
    return refuse("SELECT_MSG dispatches no button switch");
  }
  if (FindButtonCase(*cases, kDownloadNowButton) != cases->size()) {
    if (why != nullptr) why->clear();  // already routed
    return true;
  }
  const size_t again = FindButtonCase(*cases, kDownloadAgainButton);
  if (again == cases->size()) {
    return refuse("the button switch carries no store_download_again case");
  }
  // The case keeps its symbol and takes this build's body: retail's own body calls the commerce
  // object's `re_download`, which a data-only custom never has.
  Node& retargeted = (*cases)[again];
  retargeted.kids.erase(retargeted.kids.begin() + 1, retargeted.kids.end());
  retargeted.kids.push_back(DownloadRequest("again"));
  (*cases).push_back(Node::Array({Node::Symbol(kDownloadNowButton), DownloadRequest("start")}));
  if (why != nullptr) why->clear();
  return true;
}

namespace {

// Where a `{<receiver> <method> ...}` command sits inside its parent's child list. The append below
// wants the command's *sibling* position, not a copy of it.
struct CommandLocation {
  std::vector<Node>* owner = nullptr;
  size_t index = 0;
};

bool FindReceiverCall(Node& node, std::string_view receiver, std::string_view method,
                      CommandLocation* out) {
  for (size_t i = 0; i < node.kids.size(); ++i) {
    Node& kid = node.kids[i];
    if (kid.tag == ui::dtb::kCommand && kid.kids.size() >= 2 && kid.kids[0].text == receiver &&
        kid.kids[1].text == method) {
      out->owner = &node.kids;
      out->index = i;
      return true;
    }
    if (FindReceiverCall(kid, receiver, method, out)) {
      return true;
    }
  }
  return false;
}

// This edit's own marker: `{album.mat set diffuse_tex {store_panel album_tex}}`, at any depth.
bool HasAlbumLink(const Node& node) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 4 && node.kids[0].text == "album.mat" &&
      node.kids[1].text == "set" && node.kids[2].text == "diffuse_tex") {
    const Node& value = node.kids[3];
    if (value.tag == ui::dtb::kCommand && value.kids.size() >= 2 &&
        value.kids[0].text == "store_panel" && value.kids[1].text == "album_tex") {
      return true;
    }
  }
  for (const Node& kid : node.kids) {
    if (HasAlbumLink(kid)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool LinkAlbumArt(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* method = nullptr;
  for (Node& root : *store_ui_roots) {
    method = FindNamedMethod(root, "store_details_panel", "refresh");
    if (method != nullptr) {
      break;
    }
  }
  if (method == nullptr) {
    return refuse("the record carries no store_details_panel refresh method");
  }
  if (HasAlbumLink(*method)) {
    if (why != nullptr) why->clear();
    return true;  // an idempotent second pass
  }
  CommandLocation call;
  if (!FindReceiverCall(*method, "store_panel", "load_art", &call)) {
    return refuse("store_details_panel refresh carries no store_panel load_art call");
  }
  Node link = Node::Command({Node::Symbol("album.mat"), Node::Symbol("set"),
                             Node::Symbol("diffuse_tex"),
                             Node::Command({Node::Symbol("store_panel"), Node::Symbol("album_tex")})});
  call.owner->insert(call.owner->begin() + static_cast<std::ptrdiff_t>(call.index) + 1,
                     std::move(link));
  if (why != nullptr) why->clear();
  return true;
}

namespace {
// and the body. `owner` is the vector that holds it, which is what the rewrite swaps.
struct ForeachLocation {
  std::vector<Node>* owner = nullptr;
  size_t index = 0;
};

bool FindForeach(Node& node, ForeachLocation* out) {
  for (size_t i = 0; i < node.kids.size(); ++i) {
    Node& kid = node.kids[i];
    if (kid.tag == ui::dtb::kCommand && kid.kids.size() >= 4 && IsSymbol(kid.kids[0], "foreach")) {
      out->owner = &node.kids;
      out->index = i;
      return true;
    }
    if (FindForeach(kid, out)) {
      return true;
    }
  }
  return false;
}

bool IsRankPartList(const Node& node) {
  if (node.tag != ui::dtb::kArray || node.kids.size() != 5) {
    return false;
  }
  static const char* const kParts[] = {"guitar", "drum", "vocals", "bass", "keys"};
  for (size_t i = 0; i < 5; ++i) {
    if (!IsSymbol(node.kids[i], kParts[i])) {
      return false;
    }
  }
  return true;
}

// `{<receiver> get_data (rank <var>)}` - the two-component read this rewrite replaces.
bool ReadsRankPath(const Node& node, std::string_view var) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 3 &&
      IsSymbol(node.kids[1], "get_data")) {
    const Node& path = node.kids[2];
    if (path.tag == ui::dtb::kArray && path.kids.size() == 2 && IsSymbol(path.kids[0], "rank") &&
        IsVariable(path.kids[1], var)) {
      return true;
    }
  }
  for (const Node& kid : node.kids) {
    if (ReadsRankPath(kid, var)) {
      return true;
    }
  }
  return false;
}

// `{<receiver> get_data (<diff_...>)}` - the single-component tier read this rewrite leaves behind.
bool ReadsDifficultyTier(const Node& node) {
  if (node.tag == ui::dtb::kCommand && node.kids.size() >= 3 &&
      IsSymbol(node.kids[1], "get_data")) {
    const Node& path = node.kids[2];
    if (path.tag == ui::dtb::kArray && path.kids.size() == 1 &&
        path.kids[0].tag == ui::dtb::kSymbol && path.kids[0].text.rfind("diff_", 0) == 0) {
      return true;
    }
  }
  for (const Node& kid : node.kids) {
    if (ReadsDifficultyTier(kid)) {
      return true;
    }
  }
  return false;
}

// One unrolled loop body, in the statement shape the retail record carries (`store.dtb`'s
// `store_details_panel.refresh`, dumped 2026-10-21): a `{do}` binding `$tier`, then the panel call.
// The receiver (`offer`/`this`) and the binding name are **variables**; the keys and the tokens are
// symbols, exactly as the shipped record spells them.
//
// The retail body reads `(rank <part>)`, converts with `{song_mgr rank_tier rank <part>}` and passes
// the result to `set_inst_difficulty`; this one reads the offer's own `(diff_<part>)` - the tier the
// catalogue publishes - and passes it straight through, because that call takes a tier. Measured
// 2026-10-21: the meter asset has five pips and a devil icon
// (`ui/difficulty_meter/gen/diff_meter_all_inst.milo`: `diff_1.mesh` ... `diff_5.mesh`,
// `icon_devil.tex`), the 0..6 scale of the API's `diff_*`, and 0 draws `no_part`. The `rank_tier`
// thresholds this build would need to keep the rank path are not in the image.
Node DifficultyBlock(std::string_view part) {
  const std::string key = "diff_" + std::string(part);
  return Node::Command(
      {Node::Symbol("do"),
       Node::Array({Node::Var("tier"),
                    Node::Command({Node::Var("offer"), Node::Symbol("get_data"),
                                   Node::Array({Node::Symbol(key)})})}),
       Node::Command({Node::Var("this"), Node::Symbol("set_inst_difficulty"),
                      Node::String(key), Node::Var("tier"), Node::Symbol("TRUE")})});
}

}  // namespace

bool UnrollOfferDifficulty(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* method = nullptr;
  for (Node& root : *store_ui_roots) {
    method = FindNamedMethod(root, "store_details_panel", "refresh");
    if (method != nullptr) {
      break;
    }
  }
  if (method == nullptr) {
    return refuse("the record carries no store_details_panel refresh method");
  }
  ForeachLocation loop;
  if (!FindForeach(*method, &loop)) {
    // Already unrolled? The tier reads are the state this edit leaves behind.
    if (ReadsDifficultyTier(*method)) {
      if (why != nullptr) why->clear();
      return true;
    }
    return refuse("store_details_panel refresh carries no foreach loop");
  }
  Node& command = (*loop.owner)[loop.index];
  const std::string var = (command.kids[1].tag == ui::dtb::kVar ||
                           command.kids[1].tag == ui::dtb::kSymbol)
                              ? command.kids[1].text
                              : std::string();
  if (var.empty() || !IsRankPartList(command.kids[2]) || !ReadsRankPath(command.kids[3], var)) {
    return refuse("the foreach is not the difficulty loop (no `(rank <part>)` read)");
  }
  std::vector<Node> blocks;
  for (const char* part : {"guitar", "drum", "vocals", "bass", "keys"}) {
    blocks.push_back(DifficultyBlock(part));
  }
  Node replacement = Node::Command({Node::Symbol("do")});
  for (Node& block : blocks) {
    replacement.kids.push_back(std::move(block));
  }
  (*loop.owner)[loop.index] = std::move(replacement);
  if (why != nullptr) why->clear();
  return true;
}

bool SetStoreHeading(std::vector<ui::dtb::Node>* locale_roots, std::string_view heading,
                     std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (locale_roots == nullptr) {
    return refuse("no record roots");
  }
  if (heading.empty()) {
    return refuse("the heading is empty");
  }
  for (Node& root : *locale_roots) {
    if (root.tag == ui::dtb::kArray && root.kids.size() >= 2 && IsSymbol(root.kids[0], "store") &&
        root.kids[1].tag == ui::dtb::kString) {
      root.kids[1].text = std::string(heading);
      if (why != nullptr) why->clear();
      return true;
    }
  }
  return refuse("the locale has no `store` entry");
}

namespace {

// `{== $action kAction_Start}` - the test a button case array begins with. `$action` parses as a
// variable (`store_payload.h`'s note on `kVar`), so only the name is compared.
bool IsActionCaseTest(const Node& node, std::string_view action) {
  return node.tag == ui::dtb::kCommand && node.kids.size() == 3 && IsSymbol(node.kids[0], "==") &&
         node.kids[1].text == "action" && IsSymbol(node.kids[2], action);
}

// The `{== $action <action>} ...` case array inside a method, at any depth.
Node* FindActionCase(Node& node, std::string_view action) {
  if (node.tag == ui::dtb::kArray && !node.kids.empty() &&
      IsActionCaseTest(node.kids.front(), action)) {
    return &node;
  }
  for (Node& kid : node.kids) {
    if (Node* found = FindActionCase(kid, action)) {
      return found;
    }
  }
  return nullptr;
}

// `{ui goto_screen <screen>}` - the jump this edit rewrites, and the marker of one already routed.
bool IsGotoScreenStatement(const Node& node, std::string_view screen) {
  return node.tag == ui::dtb::kCommand && node.kids.size() == 3 && IsSymbol(node.kids[0], "ui") &&
         IsSymbol(node.kids[1], "goto_screen") && IsSymbol(node.kids[2], screen);
}

bool HasGotoScreenStatement(const Node& node, std::string_view screen) {
  if (IsGotoScreenStatement(node, screen)) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasGotoScreenStatement(kid, screen)) {
      return true;
    }
  }
  return false;
}

// `{options_screen set back_screen <target>}` - the other half of the shipped Start case.
bool IsOptionsBackStatement(const Node& node) {
  return node.tag == ui::dtb::kCommand && node.kids.size() == 4 &&
         IsSymbol(node.kids[0], "options_screen") && IsSymbol(node.kids[1], "set") &&
         IsSymbol(node.kids[2], "back_screen");
}

bool HasOptionsGotoStatement(const Node& node) {
  if (IsGotoScreenStatement(node, "options_screen")) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasOptionsGotoStatement(kid)) {
      return true;
    }
  }
  return false;
}

// Rewrites the shipped Start case: the `options_screen` receiver of its back-target statement and
// its jump both become `filter_screen`.
void RetargetStartStatements(std::vector<Node>& nodes, std::string_view filter_screen) {
  for (Node& node : nodes) {
    if (IsOptionsBackStatement(node)) {
      node.kids[0] = Node::Symbol(filter_screen);
      continue;
    }
    if (IsGotoScreenStatement(node, "options_screen")) {
      node.kids[2] = Node::Symbol(filter_screen);
      continue;
    }
    RetargetStartStatements(node.kids, filter_screen);
  }
}
// `{<receiver> <method> <action> [<token>]}` - the inline-help command this module writes, and the
// marker an idempotent second pass looks for.
bool IsHelpCommand(const Node& node, std::string_view receiver, std::string_view method,
                   std::string_view action) {
  return node.tag == ui::dtb::kCommand && node.kids.size() >= 3 &&
         IsSymbol(node.kids[0], receiver) && IsSymbol(node.kids[1], method) &&
         IsSymbol(node.kids[2], action);
}

Node HelpCommand(std::string_view receiver, std::string_view method, std::string_view action,
                 std::string_view token) {
  std::vector<Node> kids{Node::Symbol(receiver), Node::Symbol(method), Node::Symbol(action)};
  if (!token.empty()) {
    kids.push_back(Node::Symbol(token));
  }
  return Node::Command(std::move(kids));
}

// Rewrites every `{<receiver> set_action_token <action> ...}` in `nodes` to `token` and counts the
// ones it found; the caller appends one when the count is zero. A method that carries no such
// command is what an idempotent second pass looks like too, because the appended command is one.
size_t ClaimHelpToken(std::vector<Node>& nodes, std::string_view receiver,
                      std::string_view action, std::string_view token) {
  size_t claimed = 0;
  for (Node& node : nodes) {
    if (IsHelpCommand(node, receiver, "set_action_token", action)) {
      if (node.kids.size() >= 4) {
        node.kids[3] = Node::Symbol(token);
      } else {
        node.kids.push_back(Node::Symbol(token));
      }
      ++claimed;
      continue;
    }
    claimed += ClaimHelpToken(node.kids, receiver, action, token);
  }
  return claimed;
}

// The same for `clear_action_token`, but **direct children only**: the browser's `refresh_list`
// already carries a clear inside its `has_shortcuts` else branch, and that one is exactly what this
// edit must not accept as done - the caption it guards against can be set by the true branch.
// Counting only a statement this edit appended keeps the second pass idempotent.
size_t ClaimHelpClear(std::vector<Node>& nodes, std::string_view receiver,
                      std::string_view action) {
  size_t claimed = 0;
  for (Node& node : nodes) {
    if (IsHelpCommand(node, receiver, "clear_action_token", action)) {
      ++claimed;
    }
  }
  return claimed;
}

// One named object's method, searched over every root. Null when the record does not define it.
Node* FindMethodIn(std::vector<Node>& roots, std::string_view object, std::string_view method) {
  for (Node& root : roots) {
    if (Node* found = FindNamedMethod(root, object, method)) {
      return found;
    }
  }
  return nullptr;
}

// Inserts `command` right after the method's own name. The front menu's footer commands go in
// **first**, because `store_menu_panel.enter` ends in the shipped `{cond …}` whose default arm is
// `{fail "StorePanel Commerce Session in an illegal state…"}`: that is a ship-build log-and-return
// in retail, but a statement this build must not depend on running after it is one it puts before.
void PrependToMethod(Node* method, Node command) {
  method->kids.insert(method->kids.begin() + 1, std::move(command));
}

}  // namespace

bool RouteStoreStartToFilter(std::vector<ui::dtb::Node>* store_ui_roots,
                             std::string_view filter_screen, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  if (filter_screen.empty()) {
    return refuse("no filter page to open");
  }
  // Both cases are located before either is written, so a refusal leaves the record as it was.
  Node* cases[2] = {nullptr, nullptr};
  static const char* const kPanels[] = {"store_menu_panel", "store_browser_panel"};
  for (size_t i = 0; i < 2; ++i) {
    Node* method = FindMethodIn(*store_ui_roots, kPanels[i], "BUTTON_DOWN_MSG");
    if (method == nullptr) {
      return refuse(std::string("the record carries no ") + kPanels[i] + " BUTTON_DOWN_MSG method");
    }
    Node* start = FindActionCase(*method, "kAction_Start");
    if (start == nullptr) {
      return refuse(std::string(kPanels[i]) + "'s button table carries no kAction_Start case");
    }
    if (!HasGotoScreenStatement(*start, filter_screen) && !HasOptionsGotoStatement(*start)) {
      return refuse(std::string(kPanels[i]) +
                    "'s Start case jumps to neither options_screen nor the filter page");
    }
    cases[i] = start;
  }
  for (Node* start : cases) {
    // A case that already goes to the filter page is the idempotent second pass - its back
    // statement may or may not have been retargeted with it, and both are harmless.
    if (HasGotoScreenStatement(*start, filter_screen)) {
      continue;
    }
    RetargetStartStatements(start->kids, filter_screen);
  }
  if (why != nullptr) why->clear();
  return true;
}

bool SetStoreFooterStart(std::vector<ui::dtb::Node>* store_ui_roots, std::string_view start_token,
                         std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  if (start_token.empty()) {
    return refuse("the Start caption token is empty");
  }
  Node* menu_enter = FindMethodIn(*store_ui_roots, "store_menu_panel", "enter");
  if (menu_enter == nullptr) {
    return refuse("the record carries no store_menu_panel enter method");
  }
  Node* browser_refresh = FindMethodIn(*store_ui_roots, "store_browser_panel", "refresh_list");
  if (browser_refresh == nullptr) {
    return refuse("the record carries no store_browser_panel refresh_list method");
  }
  if (ClaimHelpToken(menu_enter->kids, "InlineHelp.ihp", "kAction_Start", start_token) == 0) {
    PrependToMethod(menu_enter,
                    HelpCommand("InlineHelp.ihp", "set_action_token", "kAction_Start", start_token));
  }
  if (ClaimHelpToken(browser_refresh->kids, "store.ihp", "kAction_Start", start_token) == 0) {
    browser_refresh->kids.push_back(
        HelpCommand("store.ihp", "set_action_token", "kAction_Start", start_token));
  }
  if (why != nullptr) why->clear();
  return true;
}

bool ClearStoreFooterOption(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_ui_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* menu_enter = FindMethodIn(*store_ui_roots, "store_menu_panel", "enter");
  if (menu_enter == nullptr) {
    return refuse("the record carries no store_menu_panel enter method");
  }
  Node* browser_refresh = FindMethodIn(*store_ui_roots, "store_browser_panel", "refresh_list");
  if (browser_refresh == nullptr) {
    return refuse("the record carries no store_browser_panel refresh_list method");
  }
  if (ClaimHelpClear(menu_enter->kids, "InlineHelp.ihp", "kAction_Option") == 0) {
    PrependToMethod(menu_enter,
                    HelpCommand("InlineHelp.ihp", "clear_action_token", "kAction_Option", ""));
  }
  // Appended **after** `refresh_list`'s own `has_shortcuts` gate, so a provider that ever reports
  // shortcuts still cannot bring the caption back.
  if (ClaimHelpClear(browser_refresh->kids, "store.ihp", "kAction_Option") == 0) {
    browser_refresh->kids.push_back(
        HelpCommand("store.ihp", "clear_action_token", "kAction_Option", ""));
  }
  if (why != nullptr) why->clear();
  return true;
}

bool ClearDetailsFooterOption(std::vector<ui::dtb::Node>* store_info_roots, std::string* why) {
  auto refuse = [&](std::string reason) {
    if (why != nullptr) {
      *why = std::move(reason);
    }
    return false;
  };
  if (store_info_roots == nullptr) {
    return refuse("no record roots");
  }
  Node* enter = FindMethodIn(*store_info_roots, "store_info_panel", "enter");
  if (enter == nullptr) {
    return refuse("the record carries no store_info_panel enter method");
  }
  // Both inline-help objects the details scene declares (`python scripts/hmx_milo.py list
  // assets/game/360/ui/store/gen/store_info.milo_xbox`): the page draws `store_info.ihp`, and
  // `view_offer.ihp` sits in the recommendation area, so both are cleared rather than guessing
  // which one the caption comes from.
  for (const char* receiver : {"store_info.ihp", "view_offer.ihp"}) {
    if (ClaimHelpClear(enter->kids, receiver, "kAction_Option") == 0) {
      enter->kids.push_back(HelpCommand(receiver, "clear_action_token", "kAction_Option", ""));
    }
  }
  if (why != nullptr) why->clear();
  return true;
}

}  // namespace rb_blitz::store
