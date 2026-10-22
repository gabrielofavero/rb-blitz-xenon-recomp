// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_payload.h"

#include <algorithm>
#include <cstddef>
#include <utility>

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

}  // namespace rb_blitz::store
