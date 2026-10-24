// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/store/store_payload.cpp — P2's `netcache_init` rewrite (RhythmVerse store
// plan §5/P2). SDK-free: the fixtures are node trees shaped like the shipped record
// (out/_swap/config_store.tree.txt), and the round-trip case compiles them with the project's own
// DTA writer and parses the result back, which is the same pair the boot uses.
//
// What these pin down:
//   * a rewrite adds `local` to every `servers` table, points every `default_server` at it, and
//     keeps everything else in the definition byte-for-byte in meaning (the `xlsp_filter`, the
//     shipped servers' own fields, the platform `#ifdef` nesting);
//   * an existing `local` is *rewritten*, not duplicated;
//   * a record that is not `netcache_init`, one with no `default_server`, and an empty root are
//     all refused with a reason — and a refusal leaves the tree untouched, because a partial
//     rewrite would look successful in the boot log;
//   * the rewritten record survives compile -> parse, which is what delivery does.

#include "check.h"

#include "store/store_metadata.h"
#include "store/store_payload.h"
#include "store/store_search.h"
#include "ui/dtb_author.h"
#include "ui/menu_options.h"

#include <string>
#include <vector>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {

using namespace rb_blitz::test;
using rb_blitz::store::PointNetcacheAtLocalServer;
using rb_blitz::ui::dtb::Node;

// check.h has no string comparison, and a server name is exactly the value whose failure needs
// both sides printed.
void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq((actual), (expected))

Node Symbol(std::string_view name) { return Node::Symbol(name); }
Node Text(std::string_view text) { return Node::String(text); }

// One shipped-shaped `servers` table: `live` plus the two fields every server carries.
Node ServersTable() {
  return Node::Array({
      Symbol("servers"),
      Node::Array({Symbol("live"),
                   Node::Array({Symbol("server"), Text("")}),
                   Node::Array({Symbol("port"), Node::Int(1100)}),
                   Node::Array({Symbol("root"), Text("/rbb/xbox/live/")})}),
  });
}

// `(netcache_init (xlsp_filter "Harmonix") (servers ...) (default_server live))`, with the platform
// conditionals the shipped record keeps around them.
std::vector<Node> NetcacheRecord() {
  return {Node::Array({
      Symbol("netcache_init"),
      Node::Array({Symbol("xlsp_filter"), Text("Harmonix")}),
      Node::Array({Symbol("ms_between_reconn_store"), Node::Int(5000)}),
      Node::Ifdef("HX_XBOX"),
      ServersTable(),
      Node::Ifdef("_SHIP"),
      Node::Array({Symbol("default_server"), Symbol("live")}),
      Node::Endif(),
      Node::Endif(),
      // A second platform branch, so the "every table" rule is exercised rather than assumed.
      Node::Ifdef("HX_PS3"),
      ServersTable(),
      Node::Array({Symbol("default_server"), Symbol("live")}),
      Node::Endif(),
  })};
}

// `(local (local TRUE) (root "<root>"))` is the shipped `local` entry's exact shape.
bool IsLocalServer(const Node& node, std::string_view root) {
  if (node.tag != rb_blitz::ui::dtb::kArray || node.kids.size() != 3) return false;
  if (!(node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "local")) return false;
  const Node& flag = node.kids[1];
  if (flag.kids.size() != 2 || flag.kids[0].text != "local" || flag.kids[1].text != "TRUE")
    return false;
  const Node& root_node = node.kids[2];
  return root_node.kids.size() == 2 && root_node.kids[0].text == "root" &&
         root_node.kids[1].text == root;
}

// Collects the names every `default_server` in the tree picks.
void CollectDefaults(const Node& node, std::vector<std::string>* names) {
  if (node.tag == rb_blitz::ui::dtb::kArray && !node.kids.empty() &&
      node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "default_server") {
    names->push_back(node.kids.size() >= 2 ? node.kids[1].text : std::string());
  }
  for (const Node& kid : node.kids) CollectDefaults(kid, names);
}

// Collects the number of `local` entries every `servers` table carries.
void CollectLocalCounts(const Node& node, std::vector<size_t>* counts) {
  if (node.tag == rb_blitz::ui::dtb::kArray && !node.kids.empty() &&
      node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "servers") {
    size_t locals = 0;
    for (const Node& kid : node.kids) {
      if (kid.kids.size() >= 1 && kid.kids[0].text == "local") ++locals;
    }
    counts->push_back(locals);
  }
  for (const Node& kid : node.kids) CollectLocalCounts(kid, counts);
}

bool TreeUnchanged(const std::vector<Node>& lhs, const std::vector<Node>& rhs) {
  std::vector<uint8_t> a = rb_blitz::ui::dtb::SerializeNodes(lhs);
  std::vector<uint8_t> b = rb_blitz::ui::dtb::SerializeNodes(rhs);
  return a == b;
}

void TestRewriteAddsLocalServer() {
  BeginCase("the rewrite adds a local server to every table and points every default at it");

  std::vector<Node> record = NetcacheRecord();
  std::string why;
  CHECK_TRUE(PointNetcacheAtLocalServer(&record, "ui/store/remote_metadata/", &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  std::vector<size_t> locals;
  CollectLocalCounts(record[0], &locals);
  CHECK_EQ(locals.size(), 2u);  // the Xbox table and the PS3 table
  for (size_t count : locals) CHECK_EQ(count, 1u);

  std::vector<std::string> defaults;
  CollectDefaults(record[0], &defaults);
  CHECK_EQ(defaults.size(), 2u);
  for (const std::string& name : defaults) CHECK_STR_EQ(name, "local");

  // The rest of the definition is untouched: the filter survives, and the shipped `live` server
  // keeps its own fields.
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(record).size() > 0);
  bool saw_filter = false;
  bool saw_live_root = false;
  std::vector<const Node*> stack = {&record[0]};
  while (!stack.empty()) {
    const Node* node = stack.back();
    stack.pop_back();
    if (node->tag == rb_blitz::ui::dtb::kArray && node->kids.size() == 2 &&
        node->kids[0].text == "xlsp_filter" && node->kids[1].text == "Harmonix") {
      saw_filter = true;
    }
    if (node->tag == rb_blitz::ui::dtb::kArray && node->kids.size() == 2 &&
        node->kids[0].text == "root" && node->kids[1].text == "/rbb/xbox/live/") {
      saw_live_root = true;
    }
    for (const Node& kid : node->kids) stack.push_back(&kid);
  }
  CHECK_TRUE(saw_filter);
  CHECK_TRUE(saw_live_root);
}

void TestRewriteReplacesExistingLocal() {
  BeginCase("an existing local entry is rewritten in place, never duplicated");

  std::vector<Node> record = NetcacheRecord();
  // A `local` entry already present, with the shipped root.
  Node& servers = record[0].kids[4];
  servers.kids.push_back(Node::Array({Symbol("local"),
                                      Node::Array({Symbol("local"), Symbol("TRUE")}),
                                      Node::Array({Symbol("root"), Text("ui/store/remote_metadata/")})}));
  std::string why;
  CHECK_TRUE(PointNetcacheAtLocalServer(&record, "rbbz-store/", &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  std::vector<size_t> locals;
  CollectLocalCounts(record[0], &locals);
  CHECK_EQ(locals.size(), 2u);
  for (size_t count : locals) CHECK_EQ(count, 1u);

  // The new root is the one that is there, in both tables.
  size_t matches = 0;
  std::vector<const Node*> stack = {&record[0]};
  while (!stack.empty()) {
    const Node* node = stack.back();
    stack.pop_back();
    if (IsLocalServer(*node, "rbbz-store/")) ++matches;
    for (const Node& kid : node->kids) stack.push_back(&kid);
  }
  CHECK_EQ(matches, 2u);
}

void TestRefusalsLeaveTheTreeAlone() {
  BeginCase("a refused rewrite leaves the record exactly as it was");

  // No netcache_init: the record is not the store's config.
  std::vector<Node> other = {Node::Array({Symbol("groupings"), Node::Array({Symbol("by_artist")})})};
  const std::vector<Node> before = other;
  std::string why;
  CHECK_FALSE(PointNetcacheAtLocalServer(&other, "ui/store/remote_metadata/", &why));
  CHECK_TRUE(!why.empty());
  CHECK_TRUE(TreeUnchanged(other, before));

  // A netcache_init with no default_server selects nothing, so it is refused *before* any edit.
  std::vector<Node> no_default = {Node::Array({Symbol("netcache_init"), ServersTable()})};
  const std::vector<Node> no_default_before = no_default;
  why.clear();
  CHECK_FALSE(PointNetcacheAtLocalServer(&no_default, "ui/store/remote_metadata/", &why));
  CHECK_TRUE(!why.empty());
  CHECK_TRUE(TreeUnchanged(no_default, no_default_before));

  // An empty root would measure nothing.
  std::vector<Node> record = NetcacheRecord();
  const std::vector<Node> record_before = record;
  why.clear();
  CHECK_FALSE(PointNetcacheAtLocalServer(&record, "", &why));
  CHECK_TRUE(!why.empty());
  CHECK_TRUE(TreeUnchanged(record, record_before));
}

void TestRewrittenRecordSurvivesCompileAndParse() {
  BeginCase("the rewritten record round-trips through the DTA writer and the reader");

  std::vector<Node> record = NetcacheRecord();
  std::string why;
  CHECK_TRUE(PointNetcacheAtLocalServer(&record, "ui/store/remote_metadata/", &why));

  const std::vector<uint8_t> compiled = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  std::string error;
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(compiled.data(), compiled.size(), &parsed, &header, &error));
  if (!error.empty()) Fail(__FILE__, __LINE__, error);
  CHECK_EQ(parsed.size(), 1u);

  std::vector<std::string> defaults;
  CollectDefaults(parsed[0], &defaults);
  CHECK_EQ(defaults.size(), 2u);
  for (const std::string& name : defaults) CHECK_STR_EQ(name, "local");

  std::vector<size_t> locals;
  CollectLocalCounts(parsed[0], &locals);
  for (size_t count : locals) CHECK_EQ(count, 1u);
}

}  // namespace

// The boot-time shape `ui/gen/ui.dtb` carries around the netcache manager: a `set`, the manager's
// `init` with the syscfg lookup, and its `load` (decoded in out/_swap/ui.tree.txt).
std::vector<Node> UiInitRecord() {
  return {
      Node::Command({Symbol("set"), Node::Var("first_screen"), Symbol("splash_screen")}),
      Node::Command({Symbol("net_cache_mgr"), Symbol("init"),
                     Node::Command({Symbol("find"), Node::Var("syscfg"), Symbol("store"),
                                    Symbol("netcache_init")})}),
      Node::Command({Symbol("net_cache_mgr"), Symbol("load"),
                     Symbol("kNetCacheMgrCacheSizeLarge")}),
  };
}

const Node* FindNetCacheInitCommand(const Node& node) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
      node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "net_cache_mgr" &&
      node.kids[1].text == "init") {
    return &node;
  }
  for (const Node& kid : node.kids) {
    if (const Node* found = FindNetCacheInitCommand(kid)) return found;
  }
  return nullptr;
}

// True when the tree still carries a symbol `name` anywhere.
bool HasSymbol(const Node& node, std::string_view name) {
  if (node.tag == rb_blitz::ui::dtb::kSymbol && node.text == name) return true;
  for (const Node& kid : node.kids) {
    if (HasSymbol(kid, name)) return true;
  }
  return false;
}

void TestUiInitGetsTheRewrittenDefinition() {
  BeginCase("the ui init is handed the record's own rewritten definition");

  // The definition comes from the record the boot already rewrote, so the manager's argument has
  // the shipped shape by construction (a hand-built literal crashed the guest on 2026-10-20).
  std::vector<Node> store = NetcacheRecord();
  std::string why;
  CHECK_TRUE(PointNetcacheAtLocalServer(&store, "rbbz-store/", &why));
  const Node* definition = rb_blitz::store::FindNetcacheInit(store);
  CHECK_TRUE(definition != nullptr);
  if (definition == nullptr) return;

  std::vector<Node> record = UiInitRecord();
  why.clear();
  CHECK_TRUE(rb_blitz::store::PointUiNetcacheAtLocalServer(&record, *definition, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* init = nullptr;
  for (const Node& root : record) {
    init = FindNetCacheInitCommand(root);
    if (init != nullptr) break;
  }
  CHECK_TRUE(init != nullptr);
  if (init == nullptr || init->kids.size() < 3) return;

  const Node& argument = init->kids[2];
  CHECK_TRUE(argument.tag == rb_blitz::ui::dtb::kArray);
  if (argument.kids.empty()) return;
  CHECK_STR_EQ(argument.kids[0].text, "netcache_init");

  // The whole shipped definition comes through, not a two-field skeleton: the filter and the
  // reconnect delay the record carries are part of what the native manager reads.
  CHECK_TRUE(HasSymbol(argument, "xlsp_filter"));
  CHECK_TRUE(HasSymbol(argument, "ms_between_reconn_store"));
  std::vector<size_t> locals;
  CollectLocalCounts(argument, &locals);
  CHECK_EQ(locals.size(), 2u);  // both platform branches, as the record carries them
  for (size_t count : locals) CHECK_EQ(count, 1u);
  std::vector<std::string> defaults;
  CollectDefaults(argument, &defaults);
  CHECK_EQ(defaults.size(), 2u);
  for (const std::string& name : defaults) CHECK_STR_EQ(name, "local");

  size_t root_matches = 0;
  std::vector<const Node*> stack = {&argument};
  while (!stack.empty()) {
    const Node* node = stack.back();
    stack.pop_back();
    if (IsLocalServer(*node, "rbbz-store/")) ++root_matches;
    for (const Node& kid : node->kids) stack.push_back(&kid);
  }
  CHECK_EQ(root_matches, 2u);

  // The shipped lookup is gone, and the rest of the record - the `set`, the marker and the `load` -
  // survives. The marker is what proves a boot loaded this record: records come out of the ark by
  // offset, so no path probe can see the read.
  bool saw_set = false;
  bool saw_load = false;
  bool saw_find = false;
  size_t markers = 0;
  std::vector<const Node*> nodes;
  for (const Node& root : record) nodes.push_back(&root);
  while (!nodes.empty()) {
    const Node* node = nodes.back();
    nodes.pop_back();
    if (node->tag == rb_blitz::ui::dtb::kSymbol) {
      if (node->text == "set") saw_set = true;
      if (node->text == "kNetCacheMgrCacheSizeLarge") saw_load = true;
      if (node->text == "find") saw_find = true;
      if (node->text == "notify") ++markers;
    }
    for (const Node& kid : node->kids) nodes.push_back(&kid);
  }
  CHECK_TRUE(saw_set);
  CHECK_TRUE(saw_load);
  CHECK_FALSE(saw_find);
  CHECK_EQ(markers, size_t{1});
  CHECK_EQ(record.size(), size_t{4});  // set, notify, init, load
}

void TestUiInitRefusals() {
  BeginCase("a refused ui rewrite leaves the record exactly as it was");

  std::vector<Node> store = NetcacheRecord();
  std::string why;
  CHECK_TRUE(PointNetcacheAtLocalServer(&store, "rbbz-store/", &why));
  const Node* definition = rb_blitz::store::FindNetcacheInit(store);
  CHECK_TRUE(definition != nullptr);
  if (definition == nullptr) return;

  // No `net_cache_mgr init`: the record is not the UI init.
  std::vector<Node> other = {Node::Command({Symbol("set"), Node::Var("first_screen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> before = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::PointUiNetcacheAtLocalServer(&other, *definition, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(other, before));

  // A definition that is not `netcache_init` is refused before any edit.
  std::vector<Node> record = UiInitRecord();
  const std::vector<Node> untouched = record;
  const Node not_a_definition = Node::Array({Symbol("servers"), Node::Array({Symbol("live")})});
  why.clear();
  CHECK_FALSE(rb_blitz::store::PointUiNetcacheAtLocalServer(&record, not_a_definition, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(record, untouched));
  CHECK_EQ(record.size(), size_t{3});  // no marker was inserted by the refusal
}

void TestUiInitSurvivesCompileAndParse() {
  BeginCase("the rewritten ui record survives compile and parse");

  std::vector<Node> store = NetcacheRecord();
  std::string why;
  CHECK_TRUE(PointNetcacheAtLocalServer(&store, "rbbz-store/", &why));
  const Node* definition = rb_blitz::store::FindNetcacheInit(store);
  CHECK_TRUE(definition != nullptr);
  if (definition == nullptr) return;
  std::vector<Node> record = UiInitRecord();
  why.clear();
  CHECK_TRUE(rb_blitz::store::PointUiNetcacheAtLocalServer(&record, *definition, &why));
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  CHECK_FALSE(bytes.empty());

  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  const Node* init = nullptr;
  for (const Node& root : parsed) {
    init = FindNetCacheInitCommand(root);
    if (init != nullptr) break;
  }
  CHECK_TRUE(init != nullptr);
  if (init == nullptr || init->kids.size() < 3) return;
  CHECK_STR_EQ(init->kids[2].kids.empty() ? std::string() : init->kids[2].kids[0].text,
               "netcache_init");
}

// The store screen's own shape (out/_swap/store.tree.txt): a `new UIScreen` command with its
// method definitions as arrays, the `enter` one holding the retail commands.
std::vector<Node> StoreUiRecord() {
  return {
      Node::Command({
          Symbol("new"), Symbol("UIScreen"), Symbol("store_menu_screen"),
          Node::Array({Symbol("panels"), Symbol("background_panel"), Symbol("store_panel")}),
          Node::Array({Symbol("enter"),
                       Node::Command({Symbol("background_panel"), Symbol("jump_to_header"),
                                      Node::Property({Symbol("header_index")})}),
                       Node::Command({Symbol("update_participating_user_presence"),
                                      Symbol("CONTEXT_PRESENCE_RP_BROWSING_STORE")})}),
          Node::Array({Symbol("hide_upsell"),
                       Node::Command({Symbol("upsell_panel"), Symbol("set_showing"),
                                      Symbol("FALSE")})}),
      }),
  };
}

const Node* FindEnterMethod(const Node& node) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 4 &&
      node.kids[0].text == "new" && node.kids[2].text == "store_menu_screen") {
    for (const Node& kid : node.kids) {
      if (kid.tag == rb_blitz::ui::dtb::kArray && !kid.kids.empty() && kid.kids[0].text == "enter") {
        return &kid;
      }
    }
  }
  for (const Node& kid : node.kids) {
    if (const Node* found = FindEnterMethod(kid)) return found;
  }
  return nullptr;
}

bool DrivesCommerceCallback(const Node& node) {
  return node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 2 &&
         node.kids[0].text == "store_panel" && node.kids[1].text == "on_init_commerce_success";
}

void TestStoreEntryBypass() {
  BeginCase("the store screen drives the commerce callback on enter");

  std::vector<Node> record = StoreUiRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddStoreEntryBypass(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* enter = FindEnterMethod(record[0]);
  CHECK_TRUE(enter != nullptr);
  if (enter == nullptr) return;
  CHECK_EQ(enter->kids.size(), size_t{4});  // the method name, the two retail commands, ours
  CHECK_STR_EQ(enter->kids[0].text, "enter");
  CHECK_TRUE(DrivesCommerceCallback(enter->kids[3]));

  // The retail commands are untouched and the screen's other methods survive.
  CHECK_EQ(record[0].kids.size(), size_t{6});
  CHECK_STR_EQ(record[0].kids[5].kids[0].text, "hide_upsell");

  // A second pass is refused, not doubled.
  why.clear();
  CHECK_FALSE(rb_blitz::store::AddStoreEntryBypass(&record, &why));
  CHECK_FALSE(why.empty());
}

void TestStoreEntryBypassRefusal() {
  BeginCase("a record without the store screen is refused untouched");

  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"), Symbol("splash_screen"),
                                            Node::Array({Symbol("panels"), Symbol("background_panel")})})};
  const std::vector<Node> before = other;
  std::string why;
  CHECK_FALSE(rb_blitz::store::AddStoreEntryBypass(&other, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(other, before));
}

void TestStoreEntryBypassSurvivesCompileAndParse() {
  BeginCase("the bypassed store record survives compile and parse");

  std::vector<Node> record = StoreUiRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddStoreEntryBypass(&record, &why));
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  const Node* enter = parsed.empty() ? nullptr : FindEnterMethod(parsed[0]);
  CHECK_TRUE(enter != nullptr);
  if (enter == nullptr || enter->kids.size() < 3) return;
  CHECK_TRUE(DrivesCommerceCallback(enter->kids.back()));
}

// `dump-menu <file.dtb>`: prints the row symbols of the main menu's option array and the case
// names of its dispatch switch. This is the *measurement* half of the row edits: a boot's payload
// can be extracted from its generated ark (scripts/hmx_ark.py extract) and checked here, which is
// what tells "the row was authored" apart from "the row is in the record the guest loads".
int DumpMenu(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::printf("dump-menu: cannot read %s\n", path);
    return 1;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  std::string error;
  if (!rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &error)) {
    std::printf("dump-menu: %s does not parse: %s\n", path, error.c_str());
    return 1;
  }
  auto is_row_array = [](const Node& node) {
    if (node.tag != rb_blitz::ui::dtb::kArray) return false;
    bool first = false;
    bool last = false;
    for (const Node& kid : node.kids) {
      first = first || (kid.tag == rb_blitz::ui::dtb::kSymbol && kid.text == "splash_start");
      last = last || (kid.tag == rb_blitz::ui::dtb::kSymbol && kid.text == "splash_exit");
    }
    return first && last;
  };
  std::vector<const Node*> rows;
  std::vector<const Node*> stack = {&roots[0]};
  for (const Node& root : roots) stack.push_back(&root);
  while (!stack.empty()) {
    const Node* node = stack.back();
    stack.pop_back();
    if (is_row_array(*node)) rows.push_back(node);
    for (const Node& kid : node->kids) stack.push_back(&kid);
  }
  std::printf("dump-menu: %s (%zu bytes, %zu option array(s))\n", path, bytes.size(), rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    std::printf("  option array %zu:", i);
    for (const Node& kid : rows[i]->kids) {
      if (kid.tag == rb_blitz::ui::dtb::kSymbol) {
        std::printf(" %s", kid.text.c_str());
      } else {
        std::printf(" [tag %u]", kid.tag);
      }
    }
    std::printf("\n");
  }
  return rows.empty() ? 1 : 0;
}

// `dump-strings <file.dtb>`: every symbol/string the record carries, in tree order. Used to check
// that a generated locale carries the label a generated row names.
int DumpStrings(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::printf("dump-strings: cannot read %s\n", path);
    return 1;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  std::string error;
  if (!rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &error)) {
    std::printf("dump-strings: %s does not parse: %s\n", path, error.c_str());
    return 1;
  }
  std::vector<const Node*> stack;
  for (const Node& root : roots) stack.push_back(&root);
  while (!stack.empty()) {
    const Node* node = stack.back();
    stack.pop_back();
    if (node->tag == rb_blitz::ui::dtb::kSymbol || node->tag == rb_blitz::ui::dtb::kString) {
      std::printf("%c %s\n", node->tag == rb_blitz::ui::dtb::kString ? '"' : '\'', node->text.c_str());
    }
    for (const Node& kid : node->kids) stack.push_back(&kid);
  }
  return 0;
}

// The panel's own shape (`out/_swap/store.typed.txt`): `(enter ... #ifdef HX_PS3 ... #else ...
// #endif)`, where the Xbox branch is what shows the "ALL PRICES ARE IN MICROSOFT(R) POINTS" footer.
std::vector<Node> StorePanelRecord() {
  return {
      Node::Command({
          Symbol("new"), Symbol("BandStorePanel"), Symbol("store_panel"),
          Node::Array({Symbol("file"), Node::String("store.milo")}),
          Node::Array({Symbol("enter"),
                       Node::Command({Symbol("this"), Symbol("show_loading_thinger"),
                                      Symbol("FALSE")}),
                       Node::Ifdef("HX_PS3"),
                       Node::Command({Symbol("microsoft.lbl"), Symbol("set_showing"),
                                      Symbol("FALSE")}),
                       Node::Endif()}),
      }),
  };
}

const Node* FindNamedMethodNode(const Node& node, const char* object, const char* method) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 4 &&
      node.kids[0].text == "new" && node.kids[2].text == object) {
    for (const Node& kid : node.kids) {
      if (kid.tag == rb_blitz::ui::dtb::kArray && !kid.kids.empty() && kid.kids[0].text == method) {
        return &kid;
      }
    }
  }
  for (const Node& kid : node.kids) {
    if (const Node* found = FindNamedMethodNode(kid, object, method)) return found;
  }
  return nullptr;
}

bool ShowsCost(const Node& node) {
  return node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
         (node.kids[0].text == "cost.lbl" || node.kids[0].text == "cost_ps3.lbl") &&
         node.kids[1].text == "set_showing" && node.kids[2].text == "TRUE";
}

size_t CountShowsCost(const Node& node) {
  size_t count = ShowsCost(node) ? 1 : 0;
  for (const Node& kid : node.kids) count += CountShowsCost(kid);
  return count;
}

void TestStorePriceFooter() {
  BeginCase("the store footer's price text is hidden on every store screen");

  std::vector<Node> record = StorePanelRecord();
  const size_t before = record[0].kids.size();
  std::string why;
  CHECK_TRUE(rb_blitz::store::HideStorePriceChrome(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* enter = FindNamedMethodNode(record[0], "store_panel", "enter");
  CHECK_TRUE(enter != nullptr);
  if (enter == nullptr) return;
  CHECK_EQ(enter->kids.size(), size_t{6});  // name, show_loading, ifdef, ps3 branch, endif, ours
  CHECK_STR_EQ(enter->kids.back().kids[0].text, "microsoft.lbl");
  CHECK_STR_EQ(enter->kids.back().kids[1].text, "set_showing");
  CHECK_STR_EQ(enter->kids.back().kids[2].text, "FALSE");
  // The `#ifdef` block is untouched: the label is hidden after it, not by rewriting the branch.
  CHECK_EQ(enter->kids[2].tag, rb_blitz::ui::dtb::kIfdef);
  CHECK_STR_EQ(enter->kids[3].kids[2].text, "FALSE");
  CHECK_EQ(record[0].kids.size(), before);  // only the method grew

  // Idempotent: a second pass finds its own statement and adds nothing.
  why.clear();
  CHECK_TRUE(rb_blitz::store::HideStorePriceChrome(&record, &why));
  const Node* again = FindNamedMethodNode(record[0], "store_panel", "enter");
  CHECK_TRUE(again != nullptr && again->kids.size() == size_t{6});

  // A record without the panel is refused before any edit.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::HideStorePriceChrome(&other, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

void TestStoreInfoPriceChrome() {
  BeginCase("the details page's cost labels are never shown");

  // `refresh_panel`'s measured shape: hide both, then re-show one under `purchase_allowed`.
  std::vector<Node> record = {
      Node::Command({
          Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
          Node::Array({Symbol("file"), Node::String("store_info.milo")}),
          Node::Array({Symbol("refresh_panel"),
                       Node::Command({Symbol("cost_ps3.lbl"), Symbol("set_showing"),
                                      Symbol("FALSE")}),
                       Node::Command({Symbol("cost.lbl"), Symbol("set_showing"),
                                      Symbol("FALSE")}),
                       Node::Command({Symbol("if"),
                                      Node::Command({Symbol("this"), Symbol("purchase_allowed"),
                                                     Symbol("offer")}),
                                      Node::Command({Symbol("cost.lbl"), Symbol("set_offer_cost"),
                                                     Symbol("offer")}),
                                      Node::Command({Symbol("cost.lbl"), Symbol("set_showing"),
                                                     Symbol("TRUE")})})}),
      }),
  };
  CHECK_EQ(CountShowsCost(record[0]), size_t{1});
  std::string why;
  CHECK_TRUE(rb_blitz::store::HideStoreInfoPriceChrome(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  CHECK_EQ(CountShowsCost(record[0]), size_t{0});
  const Node* panel = FindNamedMethodNode(record[0], "store_info_panel", "refresh_panel");
  CHECK_TRUE(panel != nullptr);
  if (panel == nullptr) return;
  // The two closing hides are the method's last statements.
  CHECK_EQ(panel->kids.size(), size_t{6});  // name, the two retail hides, the `if`, two ours
  CHECK_STR_EQ(panel->kids[4].kids[0].text, "cost.lbl");
  CHECK_STR_EQ(panel->kids[5].kids[0].text, "cost_ps3.lbl");
  CHECK_STR_EQ(panel->kids[5].kids[2].text, "FALSE");
  // `set_offer_cost` itself stays (it writes a label nobody shows); the `if` keeps its test.
  CHECK_STR_EQ(panel->kids[3].kids[0].text, "if");

  // Compile and parse round trip: the edited record is still a record the guest can read.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());
  if (!parsed.empty()) CHECK_EQ(CountShowsCost(parsed[0]), size_t{0});

  // A record without the details panel is refused before any edit.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::HideStoreInfoPriceChrome(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

void TestStoreHeading() {
  BeginCase("the store heading names the catalogue it shows");

  std::vector<Node> locale = {
      Node::Array({Symbol("store"), Node::String("MUSIC STORE")}),
      Node::Array({Symbol("store_browse"), Node::String("BROWSE")}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::SetStoreHeading(&locale, rb_blitz::store::kStoreHeadingText, &why));
  CHECK_STR_EQ(locale[0].kids[1].text, rb_blitz::store::kStoreHeadingText);
  // Every other entry is untouched.
  CHECK_STR_EQ(locale[1].kids[1].text, "BROWSE");
  // Idempotent.
  why.clear();
  CHECK_TRUE(rb_blitz::store::SetStoreHeading(&locale, rb_blitz::store::kStoreHeadingText, &why));
  CHECK_STR_EQ(locale[0].kids[1].text, rb_blitz::store::kStoreHeadingText);

  // A translated heading is replaced too: it is retail's text, not ours.
  std::vector<Node> translated = {
      Node::Array({Symbol("store"), Node::String("MUSIKLADEN")}),
  };
  why.clear();
  CHECK_TRUE(
      rb_blitz::store::SetStoreHeading(&translated, rb_blitz::store::kStoreHeadingText, &why));
  CHECK_STR_EQ(translated[0].kids[1].text, rb_blitz::store::kStoreHeadingText);

  // A locale without the key is refused.
  std::vector<Node> missing = {Node::Array({Symbol("other"), Node::String("X")})};
  const std::vector<Node> untouched = missing;
  why.clear();
  CHECK_FALSE(rb_blitz::store::SetStoreHeading(&missing, "X", &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(missing, untouched));
}

void TestOfferDetailsGuard() {
  BeginCase("a free offer always opens the details screen");

  std::vector<Node> record = {
      Node::Command({
          Symbol("new"), Symbol("StoreInfoEnumPanel"), Symbol("store_info_enum_panel"),
          Node::Array({Symbol("file"), Node::String("store_info_enum.milo")}),
          Node::Array({Symbol("on_enum_finished"),
                       Node::Command({Symbol("do"),
                                      Node::Array({Symbol("offer"),
                                                   Node::Command({Symbol("this"),
                                                                  Symbol("get_single_offer")})}),
                                      Node::Command({Symbol("if_else"),
                                                     Node::Command({Symbol("offer"),
                                                                    Symbol("is_completely_unavailable")}),
                                                     Node::Command({Symbol("do"),
                                                                    Node::Command({
                                                                        Symbol("ui"), Symbol("goto_screen"),
                                                                        Symbol("store_info_unavailable_screen")})}),
                                                     Node::Command({Symbol("do"),
                                                                    Node::Command({Symbol("ui"),
                                                                                   Symbol("goto_screen"),
                                                                                   Symbol("store_info_screen")})})})})}),
      }),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::AlwaysShowOfferDetails(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  // The guard's test is now the literal FALSE, and the details branch is untouched.
  const Node* method = FindNamedMethodNode(record[0], "store_info_enum_panel", "on_enum_finished");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;
  bool found = false;
  for (const Node& kid : method->kids) {
    if (kid.tag != rb_blitz::ui::dtb::kCommand || kid.kids.size() < 3) continue;
    for (const Node& statement : kid.kids) {
      if (statement.tag != rb_blitz::ui::dtb::kCommand || statement.kids.size() < 4) continue;
      if (statement.kids[0].text != "if_else") continue;
      found = true;
      CHECK_STR_EQ(statement.kids[1].text, "FALSE");
      CHECK_STR_EQ(statement.kids[3].kids[1].kids[2].text, "store_info_screen");
    }
  }
  CHECK_TRUE(found);

  // Idempotent, and a record without the panel is refused untouched.
  why.clear();
  CHECK_TRUE(rb_blitz::store::AlwaysShowOfferDetails(&record, &why));
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::AlwaysShowOfferDetails(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

void TestEveryUnavailableGuard() {
  BeginCase("every is_completely_unavailable guard answers false");

  // Both panels the shipped record carries; `store_offer_fetch_panel` comes first in document
  // order, which is why a first-match-only edit left the browser's own guard in place. The enum
  // panel wraps its guard in a `{do}` (its `on_enum_finished` binds `offer` first).
  const auto guarded_panel = [](const char* type, const char* name, bool wrapped) {
    Node guard = Node::Command({Symbol("if_else"),
                                Node::Command({Node::Var("offer"),
                                               Symbol("is_completely_unavailable")}),
                                Node::Command({Symbol("ui"), Symbol("goto_screen"),
                                               Symbol("store_info_unavailable_screen")}),
                                Node::Command({Symbol("ui"), Symbol("goto_screen"),
                                               Symbol("store_info_screen")})});
    std::vector<Node> method{Symbol("on_enum_finished")};
    if (wrapped) {
      method.push_back(Node::Command({Symbol("do"), std::move(guard)}));
    } else {
      method.push_back(std::move(guard));
    }
    return Node::Command(
        {Symbol("new"), Symbol(type), Symbol(name), Node::Array(std::move(method))});
  };
  std::vector<Node> record = {
      guarded_panel("StoreOfferFetchPanel", "store_offer_fetch_panel", false),
      guarded_panel("StoreInfoEnumPanel", "store_info_enum_panel", true),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::AlwaysShowOfferDetails(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* fetch = FindNamedMethodNode(record[0], "store_offer_fetch_panel", "on_enum_finished");
  const Node* method =
      FindNamedMethodNode(record[1], "store_info_enum_panel", "on_enum_finished");
  CHECK_TRUE(fetch != nullptr && method != nullptr);
  if (fetch == nullptr || method == nullptr) return;
  // Both guards test the literal FALSE; the else branch (the details screen) is untouched.
  CHECK_STR_EQ(fetch->kids[1].kids[1].text, "FALSE");
  CHECK_STR_EQ(fetch->kids[1].kids[3].kids[2].text, "store_info_screen");
  CHECK_STR_EQ(method->kids[1].kids[1].kids[1].text, "FALSE");
  CHECK_STR_EQ(method->kids[1].kids[1].kids[3].kids[2].text, "store_info_screen");
}

void TestOfferDetailsDirect() {
  BeginCase("the browser's A press populates the details screen directly");

  std::vector<Node> record = {
      Node::Command(
          {Symbol("new"), Symbol("StoreInfoEnumPanel"), Symbol("store_info_enum_panel"),
           Node::Array(
               {Symbol("enum_and_show_offer"), Node::Array({Node::Var("offer")}),
                Node::Command({Symbol("ui"), Symbol("goto_screen"),
                               Symbol("store_info_enum_screen")}),
                Node::Command({Symbol("this"), Symbol("set"), Symbol("in_offer"),
                               Node::Var("offer")}),
                Node::Command({Symbol("this"), Symbol("set"), Symbol("b_enum_only"),
                               Symbol("TRUE")})})}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::ShowOfferDetailsDirectly(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method =
      FindNamedMethodNode(record[0], "store_info_enum_panel", "enum_and_show_offer");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;
  // Name + the parameter list survive; the body is exactly the retail success branch.
  CHECK_EQ(method->kids.size(), size_t{3});
  const Node& body = method->kids[2];
  CHECK_STR_EQ(body.kids[0].text, "do");
  CHECK_EQ(body.kids.size(), size_t{5});
  CHECK_STR_EQ(body.kids[1].kids[0].text, "store_info_panel");
  CHECK_STR_EQ(body.kids[1].kids[1].text, "clear_data");
  CHECK_STR_EQ(body.kids[2].kids[1].text, "set");
  CHECK_STR_EQ(body.kids[2].kids[2].text, "cur_offer");
  CHECK_STR_EQ(body.kids[2].kids[3].text, "offer");
  CHECK_STR_EQ(body.kids[3].kids[0].text, "store_details_panel");
  CHECK_STR_EQ(body.kids[3].kids[1].text, "refresh");
  CHECK_STR_EQ(body.kids[4].kids[0].text, "ui");
  CHECK_STR_EQ(body.kids[4].kids[1].text, "goto_screen");
  CHECK_STR_EQ(body.kids[4].kids[2].text, "store_info_screen");

  // Idempotent.
  why.clear();
  CHECK_TRUE(rb_blitz::store::ShowOfferDetailsDirectly(&record, &why));
  const Node* again =
      FindNamedMethodNode(record[0], "store_info_enum_panel", "enum_and_show_offer");
  CHECK_TRUE(again != nullptr && again->kids.size() == size_t{3});

  // Compile and parse round trip.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());
  if (!parsed.empty()) {
    const Node* back =
        FindNamedMethodNode(parsed[0], "store_info_enum_panel", "enum_and_show_offer");
    CHECK_TRUE(back != nullptr && back->kids.size() == size_t{3});
  }

  // A record without the panel is refused before any edit.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::ShowOfferDetailsDirectly(&other, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

void TestDifficultyLoopUnrolled() {
  BeginCase("the details panel reads the catalogue's tier keys");

  // The measured shape of `store_details_panel.refresh`'s difficulty block (`store.dtb`, dumped
  // 2026-10-21): `{if_else {== {offer offer_type} song} {do {foreach part (guitar drum vocals bass
  // keys) {do (rank {offer get_data (rank part)}) ...}}} ...}`.
  const auto block = []() {
    return Node::Command(
        {Symbol("do"),
         Node::Array({Node::Var("rank"),
                      Node::Command({Node::Var("offer"), Symbol("get_data"),
                                     Node::Array({Symbol("rank"), Node::Var("part")})})}),
         Node::Array({Node::Var("tier"),
                      Node::Command({Symbol("song_mgr"), Symbol("rank_tier"),
                                     Node::Var("rank"), Node::Var("part")})}),
         Node::Array({Node::Var("idd"),
                      Node::Command({Symbol("sprintf"), Node::String("diff_%s"),
                                     Node::Var("part")})}),
         Node::Command({Node::Var("this"), Symbol("set_inst_difficulty"),
                        Node::Var("idd"), Node::Var("tier"), Symbol("TRUE")})});
  };
  std::vector<Node> record = {
      Node::Command(
          {Symbol("new"), Symbol("HeldButtonPanel"), Symbol("store_details_panel"),
           Node::Array(
               {Symbol("refresh"), Node::Array({Node::Var("offer")}),
                Node::Command(
                    {Symbol("if_else"),
                     Node::Command({Symbol("=="),
                                    Node::Command({Node::Var("offer"), Symbol("offer_type")}),
                                    Symbol("song")}),
                     Node::Command({Symbol("do"),
                                    Node::Command({Symbol("foreach"), Node::Var("part"),
                                                   Node::Array({Symbol("guitar"), Symbol("drum"),
                                                                Symbol("vocals"), Symbol("bass"),
                                                                Symbol("keys")}),
                                                   block()})}),
                     Node::Command({Node::Var("this"), Symbol("set_inst_difficulty"),
                                    Node::String(""), Node::Int(-1), Symbol("FALSE")})})})}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::UnrollOfferDifficulty(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method = FindNamedMethodNode(record[0], "store_details_panel", "refresh");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;
  // The `foreach` is gone; in its place one `{do ...}` with five blocks, each reading a single
  // `diff_<part>` key and passing that tier straight to `set_inst_difficulty` - no `rank_tier`
  // conversion, no `sprintf` label.
  const Node& ifelse = method->kids[2];
  const Node& then_branch = ifelse.kids[2];
  const Node& loop = then_branch.kids[1];
  CHECK_STR_EQ(loop.kids[0].text, "do");
  CHECK_EQ(loop.kids.size(), size_t{6});  // do + five blocks
  static const char* const kParts[] = {"guitar", "drum", "vocals", "bass", "keys"};
  for (size_t i = 0; i < 5; ++i) {
    const Node& one = loop.kids[i + 1];
    CHECK_STR_EQ(one.kids[0].text, "do");
    CHECK_EQ(one.kids.size(), size_t{3});  // do + (tier ...) + the panel call
    // (tier {offer get_data (diff_<part>)}) - a ONE-element path.
    const Node& path = one.kids[1].kids[1].kids[2];
    CHECK_EQ(path.kids.size(), size_t{1});
    CHECK_STR_EQ(path.kids[0].text, std::string("diff_") + kParts[i]);
    // {this set_inst_difficulty "diff_<part>" $tier TRUE}
    const Node& call = one.kids[2];
    CHECK_STR_EQ(call.kids[1].text, "set_inst_difficulty");
    CHECK_STR_EQ(call.kids[2].text, std::string("diff_") + kParts[i]);
    CHECK_TRUE(call.kids[2].tag == rb_blitz::ui::dtb::kString);
    CHECK_STR_EQ(call.kids[3].text, "tier");
  }

  // No two-component `(rank <part>)` read and no `rank_tier` call is left anywhere in the method.
  const auto has_rank_pair = [](const Node& node, const auto& self) -> bool {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
        node.kids[1].text == "get_data") {
      const Node& path = node.kids[2];
      if (path.tag == rb_blitz::ui::dtb::kArray && path.kids.size() == 2 &&
          path.kids[0].text == "rank") {
        return true;
      }
    }
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
        node.kids[1].text == "rank_tier") {
      return true;
    }
    for (const Node& kid : node.kids) {
      if (self(kid, self)) return true;
    }
    return false;
  };
  CHECK_FALSE(has_rank_pair(*method, has_rank_pair));

  // Idempotent.
  why.clear();
  CHECK_TRUE(rb_blitz::store::UnrollOfferDifficulty(&record, &why));
  const Node* again = FindNamedMethodNode(record[0], "store_details_panel", "refresh");
  CHECK_TRUE(again != nullptr && again->kids[2].kids[2].kids[1].kids.size() == size_t{6});

  // Compile and parse round trip.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());

  // A record without the details panel is refused untouched.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::UnrollOfferDifficulty(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));

  // A foreach that is not the difficulty loop is refused too (nothing is rewritten by accident).
  std::vector<Node> not_ranks = {
      Node::Command(
          {Symbol("new"), Symbol("HeldButtonPanel"), Symbol("store_details_panel"),
           Node::Array({Symbol("refresh"),
                        Node::Command({Symbol("foreach"), Symbol("row"),
                                       Node::Array({Symbol("a"), Symbol("b")}),
                                       Node::Command({Symbol("this"), Symbol("draw"),
                                                      Symbol("row")})})})}),
  };
  const std::vector<Node> not_ranks_untouched = not_ranks;
  why.clear();
  CHECK_FALSE(rb_blitz::store::UnrollOfferDifficulty(&not_ranks, &why));
  CHECK_TRUE(TreeUnchanged(not_ranks, not_ranks_untouched));
}

// The store menu's own panel, as the shipped record defines it (`out/_swap/store.tree.txt`):
// `SELECT_MSG` carries the measured request body, and `exit` is retail's own listener teardown.
std::vector<Node> StoreMenuPanelRecord() {
  const Node request = Node::Command(
      {Symbol("store_panel"), Symbol("request"),
       Node::Command({Symbol("provider"), Symbol("get_string"), Node::Var("ix")}), Symbol("TRUE")});
  const Node body = Node::Command({
      Symbol("do"),
      Node::Array({Node::Var("provider"),
                   Node::Command({Symbol("this"), Symbol("get_menu_provider")})}),
      Node::Array({Node::Var("ix"),
                   Node::Command({Symbol("component"), Symbol("selected_pos")})}),
      Node::Command(
          {Symbol("store_panel"), Symbol("set_source"),
           Node::Command({Symbol("symbol"),
                          Node::Command({Symbol("sprintf"), Text("menu_%s"),
                                         Node::Command({Symbol("provider"), Symbol("get_string"),
                                                        Node::Var("ix")})})}),
           Symbol("TRUE")}),
      request,
      Node::Command({Symbol("store_panel"), Symbol("update_loading_status")}),
  });
  const Node exit_member =
      Node::Array({Symbol("exit"),
                   Node::Command({Symbol("store_panel"), Symbol("set"),
                                  Symbol("offer_status_listener"), Node::Int(0)}),
                   Node::Command({Symbol("store_panel"), Symbol("remove_sink"),
                                  Node::Var("this")})});
  const Node condition = Node::Command(
      {Symbol("&&"),
       Node::Command({Symbol("store_panel"), Symbol("user_can_do_input")}),
       Node::Command({Symbol("this"), Symbol("has_menus")})});
  const Node select =
      Node::Array({Symbol("SELECT_MSG"), Node::Command({Symbol("if"), condition, body})});
  Node panel = Node::Command({Symbol("new"), Symbol("BandStorePanel"), Symbol("store_menu_panel"),
                              Node::Array({Symbol("file"), Text("store.milo")}), exit_member,
                              select});
  return {std::move(panel)};
}

bool IsStoreRequest(const Node& node) {
  return node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
         node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "store_panel" &&
         node.kids[1].text == "request";
}

// `{if_else {== $ix <row>} ... ...}` - the gate `RouteSearchRowToKeyboard` writes.
bool IsSearchGate(const Node& node, int row) {
  if (node.tag != rb_blitz::ui::dtb::kCommand || node.kids.size() != 4 ||
      node.kids[0].tag != rb_blitz::ui::dtb::kSymbol || node.kids[0].text != "if_else") {
    return false;
  }
  const Node& condition = node.kids[1];
  return condition.tag == rb_blitz::ui::dtb::kCommand && condition.kids.size() == 3 &&
         condition.kids[0].text == "==" && condition.kids[1].tag == rb_blitz::ui::dtb::kVar &&
         condition.kids[1].text == "ix" &&
         condition.kids[2].tag == rb_blitz::ui::dtb::kInt &&
         static_cast<int>(condition.kids[2].scalar) == row;
}

const Node* FindSearchGate(const Node& node, int row) {
  if (IsSearchGate(node, row)) return &node;
  for (const Node& kid : node.kids) {
    if (const Node* found = FindSearchGate(kid, row)) return found;
  }
  return nullptr;
}

// `{file_exists "<path>"}`
bool IsFileExists(const Node& node, std::string_view path) {
  return node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 2 &&
         node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "file_exists" &&
         node.kids[1].tag == rb_blitz::ui::dtb::kString && node.kids[1].text == path;
}

// `{ {ui current_screen} <method> }` - the screen method the Search row calls (P9-11).
bool IsCurrentScreenCall(const Node& node, std::string_view method) {
  if (node.tag != rb_blitz::ui::dtb::kCommand || node.kids.size() != 2) return false;
  const Node& receiver = node.kids[0];
  return receiver.tag == rb_blitz::ui::dtb::kCommand && receiver.kids.size() == 2 &&
         receiver.kids[0].tag == rb_blitz::ui::dtb::kSymbol && receiver.kids[0].text == "ui" &&
         receiver.kids[1].tag == rb_blitz::ui::dtb::kSymbol &&
         receiver.kids[1].text == "current_screen" && node.kids[1].tag == rb_blitz::ui::dtb::kSymbol &&
         node.kids[1].text == method;
}

// `{do {store_panel set_source menu_search TRUE} {store_panel request "/eng/search/" TRUE}
// {store_panel update_loading_status}}` - the shipped Search row's own three statements, which the
// keyboard's result handler runs after the submit so the answer opens in the browser with no second
// press (`store_menu_panel.SELECT_MSG`).
bool IsSearchPageRequest(const Node& node) {
  if (node.tag != rb_blitz::ui::dtb::kCommand || node.kids.size() != 4 ||
      node.kids[0].tag != rb_blitz::ui::dtb::kSymbol || node.kids[0].text != "do") {
    return false;
  }
  const Node& source = node.kids[1];
  const Node& request = node.kids[2];
  const Node& loading = node.kids[3];
  return source.tag == rb_blitz::ui::dtb::kCommand && source.kids.size() == 4 &&
         source.kids[0].tag == rb_blitz::ui::dtb::kSymbol && source.kids[0].text == "store_panel" &&
         source.kids[1].tag == rb_blitz::ui::dtb::kSymbol && source.kids[1].text == "set_source" &&
         source.kids[2].tag == rb_blitz::ui::dtb::kSymbol && source.kids[2].text == "menu_search" &&
         request.tag == rb_blitz::ui::dtb::kCommand && request.kids.size() == 4 &&
         request.kids[0].tag == rb_blitz::ui::dtb::kSymbol && request.kids[0].text == "store_panel" &&
         request.kids[1].tag == rb_blitz::ui::dtb::kSymbol && request.kids[1].text == "request" &&
         request.kids[2].tag == rb_blitz::ui::dtb::kString &&
         request.kids[2].text == rb_blitz::store::kSearchPageRequest &&
         loading.tag == rb_blitz::ui::dtb::kCommand && loading.kids.size() == 2 &&
         loading.kids[0].tag == rb_blitz::ui::dtb::kSymbol &&
         loading.kids[0].text == "store_panel" && loading.kids[1].text == "update_loading_status";
}

bool IsWriteFile(const Node& node, std::string_view path) {
  return node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
         node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "write_file" &&
         node.kids[1].tag == rb_blitz::ui::dtb::kString && node.kids[1].text == path;
}

// `{$user {user_mgr <getter> …}}` - the bind the keyboard's first argument needs.
bool IsUserBinding(const Node& node, std::string_view getter) {
  if (node.tag != rb_blitz::ui::dtb::kArray || node.kids.size() < 2) return false;
  const Node& target = node.kids[0];
  const Node& call = node.kids[1];
  return target.tag == rb_blitz::ui::dtb::kVar && target.text == "user" &&
         call.tag == rb_blitz::ui::dtb::kCommand && call.kids.size() >= 2 &&
         call.kids[0].tag == rb_blitz::ui::dtb::kSymbol && call.kids[0].text == "user_mgr" &&
         call.kids[1].tag == rb_blitz::ui::dtb::kSymbol && call.kids[1].text == getter;
}

// `{virtual_keyboard show_keyboard $user 100 (localize …) (localize …) "" $this}` - the call the
// Ultimate payload's own song search makes, with `$this` as the result's receiver. `$user` is a
// **variable holding a User object**: the native show reads the player's name from it, so a symbol
// there is a null read (measured 2026-10-23).
bool IsShowKeyboard(const Node& node) {
  if (node.tag != rb_blitz::ui::dtb::kCommand || node.kids.size() != 8) return false;
  if (node.kids[0].tag != rb_blitz::ui::dtb::kSymbol || node.kids[0].text != "virtual_keyboard" ||
      (node.kids[1].tag != rb_blitz::ui::dtb::kSymbol && node.kids[1].tag != rb_blitz::ui::dtb::kVar) ||
      node.kids[1].text != "show_keyboard" || node.kids[2].tag != rb_blitz::ui::dtb::kVar ||
      node.kids[2].text != "user" || node.kids[3].tag != rb_blitz::ui::dtb::kInt ||
      node.kids[3].scalar != rb_blitz::store::kMaxSearchChars) {
    return false;
  }
  const Node& title = node.kids[4];
  const Node& desc = node.kids[5];
  const Node& initial = node.kids[6];
  const Node& receiver = node.kids[7];
  return title.tag == rb_blitz::ui::dtb::kCommand && title.kids.size() == 2 &&
         title.kids[0].text == "localize" && title.kids[1].text == "search_for_song" &&
         desc.tag == rb_blitz::ui::dtb::kCommand && desc.kids.size() == 2 &&
         desc.kids[0].text == "localize" && desc.kids[1].text == "search_for_song_desc" &&
         initial.tag == rb_blitz::ui::dtb::kString && initial.text.empty() &&
         receiver.tag == rb_blitz::ui::dtb::kVar && receiver.text == "this";
}

const Node* FindMethod(const Node& root, std::string_view name) {
  if (root.tag == rb_blitz::ui::dtb::kCommand && root.kids.size() >= 4 &&
      root.kids[0].text == "new") {
    for (size_t i = 3; i < root.kids.size(); ++i) {
      const Node& kid = root.kids[i];
      if (kid.tag == rb_blitz::ui::dtb::kArray && !kid.kids.empty() &&
          kid.kids[0].tag == rb_blitz::ui::dtb::kSymbol && kid.kids[0].text == name) {
        return &kid;
      }
    }
  }
  return nullptr;
}

void TestSearchRowRoutesToKeyboard() {
  BeginCase("the front menu's Search row opens the title's keyboard until an answer is waiting");

  std::vector<Node> record = StoreMenuPanelRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::RouteSearchRowToKeyboard(&record, 1, "open_store_search", &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* select = FindNamedMethodNode(record[0], "store_menu_panel", "SELECT_MSG");
  CHECK_TRUE(select != nullptr);
  if (select == nullptr) return;
  const Node* gate = FindSearchGate(*select, 1);
  CHECK_TRUE(gate != nullptr);
  if (gate == nullptr) return;

  // The search branch: request when the host says an answer is waiting, else the keyboard entry.
  const Node& search_branch = gate->kids[2];
  CHECK_TRUE(search_branch.tag == rb_blitz::ui::dtb::kCommand && search_branch.kids.size() == 4 &&
             search_branch.kids[0].text == "if_else");
  if (search_branch.kids.size() == 4) {
    CHECK_TRUE(IsFileExists(search_branch.kids[1], rb_blitz::store::kSearchShowQuery));
    CHECK_TRUE(IsStoreRequest(search_branch.kids[2]));
    CHECK_TRUE(IsCurrentScreenCall(search_branch.kids[3], "open_store_search"));
  }
  // Every other row keeps retail's own statement, verbatim.
  CHECK_TRUE(IsStoreRequest(gate->kids[3]));
  CHECK_TRUE(gate->kids[3].kids.size() == 4 && gate->kids[3].kids[3].tag == rb_blitz::ui::dtb::kSymbol);
  // The statements retail wrote around the request are untouched, and the source is still set for
  // the Search row too (the row's provider name is what the palette shows).
  bool set_source = false;
  const auto find_set_source = [&set_source](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() > 1 &&
        node.kids[0].text == "store_panel" && node.kids[1].text == "set_source") {
      set_source = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  find_set_source(*select, find_set_source);
  CHECK_TRUE(set_source);

  // A second pass refuses rather than nesting a second gate.
  why.clear();
  CHECK_FALSE(rb_blitz::store::RouteSearchRowToKeyboard(&record, 1, "open_store_search", &why));
  CHECK_TRUE(why.find("already routed") != std::string::npos);

  // Refusals leave the tree exactly as it was.
  {
    std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"), Symbol("splash_screen"),
                                              Node::Array({Symbol("enter")})})};
    const std::vector<Node> before = other;
    why.clear();
    CHECK_FALSE(rb_blitz::store::RouteSearchRowToKeyboard(&other, 1, "open_store_search", &why));
    CHECK_TRUE(TreeUnchanged(other, before));
  }
  {
    std::vector<Node> fresh = StoreMenuPanelRecord();
    const std::vector<Node> before = fresh;
    why.clear();
    CHECK_FALSE(rb_blitz::store::RouteSearchRowToKeyboard(&fresh, -1, "open_store_search", &why));
    CHECK_TRUE(TreeUnchanged(fresh, before));
    why.clear();
    CHECK_FALSE(rb_blitz::store::RouteSearchRowToKeyboard(&fresh, 1, "", &why));
    CHECK_TRUE(TreeUnchanged(fresh, before));
    why.clear();
    CHECK_FALSE(rb_blitz::store::RouteSearchRowToKeyboard(nullptr, 1, "open_store_search", &why));
  }
}

// The record shape `AddSearchKeyboard` edits: the store's own shipped screen plus a menu panel with
// an `exit` the transform appends the dismissal to.
std::vector<Node> StoreMenuScreenRecord() {
  std::vector<Node> record = StoreMenuPanelRecord();
  Node screen = Node::Command({Symbol("new"), Symbol("UIScreen"), Symbol("store_menu_screen"),
                               Node::Array({Symbol("panels"), Symbol("store_menu_panel")}),
                               Node::Array({Symbol("enter"), Node::Command({Symbol("do")})})});
  record.push_back(std::move(screen));
  return record;
}

void TestSearchKeyboardScreenMethods() {
  BeginCase("the store screen carries the keyboard entry, its result, and the dismissal");

  std::vector<Node> record = StoreMenuScreenRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddSearchKeyboard(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* screen = nullptr;
  const Node* panel = nullptr;
  for (const Node& root : record) {
    if (root.kids.size() >= 3 && root.kids[2].tag == rb_blitz::ui::dtb::kSymbol &&
        root.kids[2].text == "store_menu_screen") {
      screen = &root;
    }
    if (root.kids.size() >= 3 && root.kids[2].text == "store_menu_panel") {
      panel = &root;
    }
  }
  CHECK_TRUE(screen != nullptr);
  CHECK_TRUE(panel != nullptr);
  if (screen == nullptr || panel == nullptr) return;

  // Loading owns focus, consumes every action, and only B can cancel. A page
  // request is deferred to poll, so slow requests cannot freeze the guest/UI.
  const Node& loading = record.front();
  CHECK_TRUE(loading.kids.size() >= 3 &&
             loading.kids[2].text == rb_blitz::store::kSearchLoadingPanel);
  const auto contains = [](auto&& self, const Node& node, std::string_view text) -> bool {
    if (node.text == text) return true;
    for (const auto& kid : node.kids) if (self(self, kid, text)) return true;
    return false;
  };
  const Node* poll = FindMethod(loading, "poll");
  const Node* buttons = FindMethod(loading, "BUTTON_DOWN_MSG");
  const Node* show = FindMethod(loading, "show_popup");
  CHECK_TRUE(poll != nullptr && buttons != nullptr && show != nullptr);
  if (poll && buttons && show) {
    CHECK_TRUE(contains(contains, *poll, rb_blitz::store::kSearchBusyQuery));
    CHECK_TRUE(contains(contains, *poll, rb_blitz::store::kSearchPageRequest));
    CHECK_TRUE(contains(contains, *poll, rb_blitz::store::kSearchEmptyToken));
    CHECK_TRUE(contains(contains, *buttons, "kAction_Cancel"));
    CHECK_FALSE(contains(contains, *buttons, "kAction_Confirm"));
    CHECK_TRUE(contains(contains, *buttons, "rbbz-store/search/cancel"));
    CHECK_TRUE(buttons->kids.back().tag == rb_blitz::ui::dtb::kInt &&
               buttons->kids.back().scalar == 1);  // no action falls through to the store
    CHECK_TRUE(contains(contains, *show, "set_focus_panel"));
    CHECK_TRUE(contains(contains, *show, rb_blitz::store::kSearchCancelToken));
  }

  // 1. The entry: guidance first, then the seeded submit or the keyboard itself.
  const Node* open = FindMethod(*screen, "open_store_search");
  CHECK_TRUE(open != nullptr);
  if (open != nullptr) {
    const Node& body = open->kids[1];
    CHECK_TRUE(body.tag == rb_blitz::ui::dtb::kCommand && body.kids[0].text == "do" &&
               body.kids.size() == 3);
    if (body.kids.size() == 3) {
      CHECK_TRUE(IsWriteFile(body.kids[1], "rbbz-store/search/open"));
      const Node& gate = body.kids[2];
      CHECK_TRUE(gate.kids.size() == 4 && gate.kids[0].text == "if_else");
      if (gate.kids.size() == 4) {
        CHECK_TRUE(IsFileExists(gate.kids[1], rb_blitz::store::kSearchSeedQuery));
        // The seeded submit uses the same loading modal as a keyboard submission.
        const Node& seeded = gate.kids[2];
        CHECK_TRUE(seeded.kids.size() == 3 && seeded.kids[0].text == "do");
        if (seeded.kids.size() == 3) {
          CHECK_TRUE(IsWriteFile(seeded.kids[1], "rbbz-store/search/done"));
          CHECK_TRUE(seeded.kids[2].kids.size() == 2 &&
                     seeded.kids[2].kids[0].text == rb_blitz::store::kSearchLoadingPanel &&
                     seeded.kids[2].kids[1].text == "show_popup");
        }
        // The keyboard arm binds a User object first: the native show reads the player's name
        // from it, and a bare symbol there is a null read (measured 2026-10-23).
        const Node& with_user = gate.kids[3];
        CHECK_TRUE(with_user.kids.size() == 3 && with_user.kids[0].text == "do");
        if (with_user.kids.size() == 3) {
          CHECK_TRUE(IsUserBinding(with_user.kids[1], "get_participating_user"));
          const Node& pick = with_user.kids[2];
          CHECK_TRUE(pick.kids.size() == 4 && pick.kids[0].text == "if_else" &&
                     pick.kids[1].tag == rb_blitz::ui::dtb::kVar && pick.kids[1].text == "user");
          CHECK_TRUE(pick.kids.size() == 4 && IsShowKeyboard(pick.kids[2]));
          const Node& fallback = pick.kids.size() == 4 ? pick.kids[3] : pick;
          CHECK_TRUE(fallback.kids.size() == 3 && fallback.kids[0].text == "do");
          if (fallback.kids.size() == 3) {
            CHECK_TRUE(IsUserBinding(fallback.kids[1], "get_user_from_pad_num"));
            const Node& second = fallback.kids[2];
            CHECK_TRUE(second.kids.size() == 4 && second.kids[0].text == "if_else" &&
                       second.kids[1].tag == rb_blitz::ui::dtb::kVar &&
                       second.kids[1].text == "user");
            CHECK_TRUE(second.kids.size() == 4 && IsShowKeyboard(second.kids[2]));
            if (second.kids.size() == 4) {
              CHECK_TRUE(IsWriteFile(second.kids[3], rb_blitz::store::kSearchLeaveWrite));
            }
          }
        }
      }
    }
  }

  // 2. The result: `$ok` gates the text write and the submit; its else arm dismisses the strip.
  const Node* result = FindMethod(*screen, rb_blitz::store::kSearchKeyboardResultMethod);
  CHECK_TRUE(result != nullptr);
  if (result != nullptr) {
    // Two positional values arrive from the engine, so the method carries a parameter list.
    CHECK_TRUE(result->kids.size() == 3);
    if (result->kids.size() == 3) {
      const Node& params = result->kids[1];
      CHECK_TRUE(params.tag == rb_blitz::ui::dtb::kArray && params.kids.size() == 2 &&
                 params.kids[0].tag == rb_blitz::ui::dtb::kVar && params.kids[0].text == "ok" &&
                 params.kids[1].tag == rb_blitz::ui::dtb::kVar && params.kids[1].text == "text");
    }
    const Node& body = result->kids.size() == 3 ? result->kids[2] : result->kids[1];
    CHECK_TRUE(body.kids.size() == 4 && body.kids[0].text == "if_else" &&
               body.kids[1].tag == rb_blitz::ui::dtb::kVar && body.kids[1].text == "ok");
    CHECK_TRUE(body.kids.size() == 4 &&
               IsWriteFile(body.kids[3], rb_blitz::store::kSearchLeaveWrite));
    const Node& inner = body.kids[2];
    CHECK_TRUE(inner.kids.size() == 4 && inner.kids[0].text == "do");
    if (inner.kids.size() == 4) {
      const Node& write = inner.kids[1];
      CHECK_TRUE(write.kids.size() == 3 && write.kids[0].text == "write_file" &&
                 write.kids[1].tag == rb_blitz::ui::dtb::kCommand &&
                 write.kids[1].kids[0].text == "sprintf" &&
                 write.kids[1].kids[1].tag == rb_blitz::ui::dtb::kString &&
                 write.kids[1].kids[1].text == "rbbz-store/search/text/%s" &&
                 write.kids[1].kids[2].tag == rb_blitz::ui::dtb::kVar &&
                 write.kids[1].kids[2].text == "text");
      CHECK_TRUE(IsWriteFile(inner.kids[2], "rbbz-store/search/done"));
      // Open the modal, whose poll requests the page once the answer is complete.
      CHECK_TRUE(inner.kids[3].kids.size() == 2 &&
                 inner.kids[3].kids[0].text == rb_blitz::store::kSearchLoadingPanel &&
                 inner.kids[3].kids[1].text == "show_popup");
    }
  }

  // 3. The dismissal: leaving the store ends the strip, and the field and answer stay.
  const Node* exit = FindMethod(*panel, "exit");
  CHECK_TRUE(exit != nullptr);
  if (exit != nullptr) {
    CHECK_TRUE(!exit->kids.empty() &&
               IsWriteFile(exit->kids.back(), rb_blitz::store::kSearchLeaveWrite));
  }

  // A second pass refuses, and refusals leave the record byte-identical.
  why.clear();
  CHECK_FALSE(rb_blitz::store::AddSearchKeyboard(&record, &why));
  CHECK_TRUE(why.find("already carries") != std::string::npos);
  {
    std::vector<Node> panel_only = StoreMenuPanelRecord();
    const std::vector<Node> before = panel_only;
    why.clear();
    CHECK_FALSE(rb_blitz::store::AddSearchKeyboard(&panel_only, &why));
    CHECK_TRUE(TreeUnchanged(panel_only, before));
  }
  {
    std::vector<Node> nothing;
    why.clear();
    CHECK_FALSE(rb_blitz::store::AddSearchKeyboard(nullptr, &why));
    CHECK_TRUE(nothing.empty());
  }
}

void TestSearchKeyboardSurvivesCompileAndParse() {
  BeginCase("the keyboard methods survive compile and parse");

  std::vector<Node> record = StoreMenuScreenRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddSearchKeyboard(&record, &why));
  CHECK_TRUE(rb_blitz::store::RouteSearchRowToKeyboard(&record, 1, "open_store_search", &why));
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  const Node* screen = nullptr;
  for (const Node& root : parsed) {
    if (root.kids.size() >= 3 && root.kids[2].tag == rb_blitz::ui::dtb::kSymbol &&
        root.kids[2].text == "store_menu_screen") {
      screen = &root;
    }
  }
  CHECK_TRUE(screen != nullptr);
  if (screen == nullptr) return;
  CHECK_TRUE(FindMethod(*screen, "open_store_search") != nullptr);
  CHECK_TRUE(FindMethod(*screen, rb_blitz::store::kSearchKeyboardResultMethod) != nullptr);
}

void TestSearchRowGateSurvivesCompileAndParse() {
  BeginCase("the routed store record survives compile and parse");

  std::vector<Node> record = StoreMenuPanelRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::RouteSearchRowToKeyboard(&record, 1, "open_store_search", &why));
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  const Node* select =
      parsed.empty() ? nullptr : FindNamedMethodNode(parsed[0], "store_menu_panel", "SELECT_MSG");
  CHECK_TRUE(select != nullptr);
  if (select == nullptr) return;
  CHECK_TRUE(FindSearchGate(*select, 1) != nullptr);
}

// `store-edit <file.dtb> store|info|search|buttons|start`: run the store-record transforms against
// a real shipped record and print the refusal reason, plus the method the transform looked at. This
// is the measurement half of those edits - the payload applies them to whatever `ui/store/gen/*.dtb`
// the boot mounts, and a refusal there is a note in the log that names a reason but not the record's
// own shape. `start` covers P9-7/P9-8: the two Start cases and the inline-help caption commands.
void PrintNode(const Node& node, int depth) {
  std::printf("%*s[0x%02x] %s\n", depth * 2, "", node.tag, node.text.c_str());
  if (depth >= 10) return;
  for (const Node& kid : node.kids) PrintNode(kid, depth + 1);
}

int StoreEdit(const char* path, const char* which) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::printf("store-edit: cannot read %s\n", path);
    return 1;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
  std::vector<Node> roots;
  rb_blitz::ui::dtb::RootHeader header;
  std::string error;
  if (!rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &error)) {
    std::printf("store-edit: %s does not parse: %s\n", path, error.c_str());
    return 1;
  }
  const std::string mode = which;
  std::string why;
  bool ok = false;
  if (mode == "buttons") {
    // Read-only: the details page's own button logic, printed before anything edits it. This is
    // how the Downloaded affordance was read out of the shipped record rather than guessed at
    // (2026-10-22: the marked offer's page drew only Exit).
    const Node* buttons = nullptr;
    for (const Node& root : roots) {
      buttons = FindNamedMethodNode(root, "store_info_panel", "refresh_buttons");
      if (buttons != nullptr) break;
    }
    if (buttons == nullptr) {
      std::printf("store-edit: refresh_buttons not found in %zu root(s)\n", roots.size());
      return 1;
    }
    std::printf("store-edit: refresh_buttons shape:\n");
    PrintNode(*buttons, 0);
    return 0;
  }
  if (mode == "download") {
    // P5's two transforms against the shipped record, and both methods they rewrote, printed in
    // full: the row has to sit in the not-downloaded branch of the Downloaded gate, and its press
    // has to be a `write_file` on a path built from the offer's own token - after P9-6's popup
    // call, which is why the modal's definition is printed too.
    ok = rb_blitz::store::AddDownloadButton(&roots, &why);
    std::printf("store-edit: download row -> %s (%s)\n", ok ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool popup = rb_blitz::store::AddDownloadPopup(&roots, &why);
    std::printf("store-edit: download popup -> %s (%s)\n", popup ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool routed = rb_blitz::store::RouteDownloadButtons(&roots, &why);
    std::printf("store-edit: download press -> %s (%s)\n", routed ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    ok = ok && popup && routed;
    const Node* buttons = nullptr;
    const Node* select = nullptr;
    for (const Node& root : roots) {
      if (buttons == nullptr) buttons = FindNamedMethodNode(root, "store_info_panel", "refresh_buttons");
      if (select == nullptr) select = FindNamedMethodNode(root, "store_info_panel", "SELECT_MSG");
      if (buttons != nullptr && select != nullptr) break;
    }
    if (buttons != nullptr) {
      std::printf("store-edit: refresh_buttons after:\n");
      PrintNode(*buttons, 0);
    }
    if (select != nullptr) {
      std::printf("store-edit: SELECT_MSG after:\n");
      PrintNode(*select, 0);
    }
    for (const Node& root : roots) {
      if (root.tag == rb_blitz::ui::dtb::kCommand && root.kids.size() >= 3 &&
          root.kids[0].text == "new" &&
          root.kids[1].text == "UIPanel" &&
          root.kids[2].text == rb_blitz::store::kDownloadPanelName) {
        std::printf("store-edit: %s definition:\n", rb_blitz::store::kDownloadPanelName);
        PrintNode(root, 0);
        break;
      }
    }
    return ok ? 0 : 1;
  }
  if (mode == "search") {
    // P9-11's transforms against the shipped record, and the methods they wrote, printed in full:
    // the gate has to sit inside the same `{do}` that defined `$provider`/`$ix`, and only the
    // document itself can show that. `AddSearchKeyboard` runs first because the gate calls the
    // screen method it defines.
    why.clear();
    const bool keyboard = rb_blitz::store::AddSearchKeyboard(&roots, &why);
    std::printf("store-edit: search keyboard -> %s (%s)\n", keyboard ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool routed = rb_blitz::store::RouteSearchRowToKeyboard(
        &roots, rb_blitz::store::kSearchMenuRow, rb_blitz::store::kSearchKeyboardMethod, &why);
    std::printf("store-edit: search row -> %s (%s)\n", routed ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    const Node* select = nullptr;
    for (const Node& root : roots) {
      select = FindNamedMethodNode(root, "store_menu_panel", "SELECT_MSG");
      if (select != nullptr) break;
    }
    if (select != nullptr) {
      std::printf("store-edit: SELECT_MSG shape:\n");
      PrintNode(*select, 0);
    } else {
      std::printf("store-edit: SELECT_MSG not found in %zu root(s)\n", roots.size());
    }
    const Node* open = nullptr;
    const Node* result = nullptr;
    for (const Node& root : roots) {
      if (open == nullptr) open = FindMethod(root, "open_store_search");
      if (result == nullptr) result = FindMethod(root, rb_blitz::store::kSearchKeyboardResultMethod);
    }
    if (open != nullptr) {
      std::printf("store-edit: open_store_search:\n");
      PrintNode(*open, 0);
    } else {
      std::printf("store-edit: open_store_search not found\n");
    }
    if (result != nullptr) {
      std::printf("store-edit: %s:\n", rb_blitz::store::kSearchKeyboardResultMethod);
      PrintNode(*result, 0);
    } else {
      std::printf("store-edit: %s not found\n", rb_blitz::store::kSearchKeyboardResultMethod);
    }
    const Node* exit = nullptr;
    for (const Node& root : roots) {
      exit = FindNamedMethodNode(root, "store_menu_panel", "exit");
      if (exit != nullptr) break;
    }
    if (exit != nullptr) {
      std::printf("store-edit: store_menu_panel exit:\n");
      PrintNode(*exit, 0);
    } else {
      std::printf("store-edit: store_menu_panel exit not found\n");
    }
    return routed ? 0 : 1;
  }
  if (mode == "store") {
    ok = rb_blitz::store::UnrollOfferDifficulty(&roots, &why);
    std::printf("store-edit: difficulty loop -> %s (%s)\n", ok ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool art = rb_blitz::store::LinkAlbumArt(&roots, &why);
    std::printf("store-edit: album art -> %s (%s)\n", art ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool keyboard = rb_blitz::store::AddSearchKeyboard(&roots, &why);
    std::printf("store-edit: search keyboard -> %s (%s)\n", keyboard ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool search = rb_blitz::store::RouteSearchRowToKeyboard(
        &roots, rb_blitz::store::kSearchMenuRow, rb_blitz::store::kSearchKeyboardMethod, &why);
    std::printf("store-edit: search row -> %s (%s)\n", search ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    ok = ok && art && keyboard && search;
  } else if (mode == "start") {
    // P9-7/P9-8 against the shipped store record, then every method those edits touch, printed in
    // full: whether the caption commands sit on the scenes' own `InlineHelp.ihp`/`store.ihp`
    // objects, and whether both Start cases keep their shipped surrounds, is what the record itself
    // has to show (`docs/engine/diagnostics.md`, the store-edit modes).
    const bool routed = rb_blitz::store::RouteStoreStartToFilter(
        &roots, "settings_rhythmverse_filter_screen", &why);
    std::printf("store-edit: start route -> %s (%s)\n", routed ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool caption = rb_blitz::store::SetStoreFooterStart(
        &roots, rb_blitz::store::kFooterSearchFiltersToken, &why);
    std::printf("store-edit: start caption -> %s (%s)\n", caption ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool option = rb_blitz::store::ClearStoreFooterOption(&roots, &why);
    std::printf("store-edit: view item caption -> %s (%s)\n", option ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    ok = routed && caption && option;
    const Node* front = nullptr;
    const Node* browser = nullptr;
    const Node* enter = nullptr;
    const Node* refresh = nullptr;
    for (const Node& root : roots) {
      if (front == nullptr) front = FindNamedMethodNode(root, "store_menu_panel", "BUTTON_DOWN_MSG");
      if (browser == nullptr) {
        browser = FindNamedMethodNode(root, "store_browser_panel", "BUTTON_DOWN_MSG");
      }
      if (enter == nullptr) enter = FindNamedMethodNode(root, "store_menu_panel", "enter");
      if (refresh == nullptr) {
        refresh = FindNamedMethodNode(root, "store_browser_panel", "refresh_list");
      }
    }
    if (front != nullptr) {
      std::printf("store-edit: store_menu_panel BUTTON_DOWN_MSG after:\n");
      PrintNode(*front, 0);
    }
    if (browser != nullptr) {
      std::printf("store-edit: store_browser_panel BUTTON_DOWN_MSG after:\n");
      PrintNode(*browser, 0);
    }
    if (enter != nullptr) {
      std::printf("store-edit: store_menu_panel enter after:\n");
      PrintNode(*enter, 0);
    }
    if (refresh != nullptr) {
      std::printf("store-edit: store_browser_panel refresh_list after:\n");
      PrintNode(*refresh, 0);
    }
  } else {
    ok = rb_blitz::store::ShowOfferDetailsDirectly(&roots, &why);
    std::printf("store-edit: details route -> %s (%s)\n", ok ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool preview = rb_blitz::store::HidePreviewButton(&roots, &why);
    std::printf("store-edit: preview row -> %s (%s)\n", preview ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool purchased = rb_blitz::store::UseDataForDownloadedRow(&roots, &why);
    std::printf("store-edit: downloaded row -> %s (%s)\n", purchased ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool option = rb_blitz::store::ClearDetailsFooterOption(&roots, &why);
    std::printf("store-edit: view item caption -> %s (%s)\n", option ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    // P9-6: the popup the Download press opens, on this record's own details screen.
    why.clear();
    const bool popup = rb_blitz::store::AddDownloadPopup(&roots, &why);
    std::printf("store-edit: download popup -> %s (%s)\n", popup ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    // P9-13: the credit block is written into `refresh_panel`, whose whole method the dump below
    // prints so a boot's record can be compared with this one.
    why.clear();
    const bool credits = rb_blitz::store::ShowOfferCredits(&roots, &why);
    std::printf("store-edit: credit block -> %s (%s)\n", credits ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    ok = ok && preview && purchased && option && popup && credits;
  }
  std::printf("store-edit: %s -> %s\n", mode.c_str(), ok ? "edited" : "refused");
  const Node* method = nullptr;
  for (const Node& root : roots) {
    method = mode == "store"
                 ? FindNamedMethodNode(root, "store_details_panel", "refresh")
                 : FindNamedMethodNode(root, "store_info_enum_panel", "enum_and_show_offer");
    if (method != nullptr) break;
  }
  if (method != nullptr) {
    std::printf("store-edit: method shape:\n");
    PrintNode(*method, 0);
  } else {
    std::printf("store-edit: method not found in %zu root(s)\n", roots.size());
  }
  if (mode != "store") {
    const Node* panel = nullptr;
    for (const Node& root : roots) {
      panel = FindNamedMethodNode(root, "store_info_panel", "refresh_panel");
      if (panel != nullptr) break;
    }
    if (panel != nullptr) {
      std::printf("store-edit: store_info_panel refresh_panel after:\n");
      PrintNode(*panel, 0);
    } else {
      std::printf("store-edit: refresh_panel not found in %zu root(s)\n", roots.size());
    }
  }
  return ok ? 0 : 1;
}

void TestPreviewButtonHidden() {
  BeginCase("the details page never offers a preview it cannot play");

  // `refresh_buttons`' measured shape: build `buttons`, insert Exit, then the preview row inside
  // the `has_data preview_audio` block (which an empty string satisfies).
  const auto insert = [](const char* row) {
    return Node::Command({Symbol("insert_elem"), Symbol("buttons"), Node::Int(0), Symbol(row)});
  };
  std::vector<Node> record = {
      Node::Command(
          {Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
           Node::Array(
               {Symbol("refresh_buttons"), Node::Array({Node::Var("init")}),
                Node::Command({Symbol("do"), insert("exit"),
                               Node::Command({Symbol("if"),
                                              Node::Command({Symbol("offer"), Symbol("has_data"),
                                                             Symbol("preview_audio")}),
                                              Node::Command({Symbol("do"),
                                                             insert("store_preview_audio")})})})})}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::HidePreviewButton(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method =
      FindNamedMethodNode(record[0], "store_info_panel", "refresh_buttons");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;
  // The Exit row stays; the preview row is gone.
  bool exit_row = false;
  bool preview_row = false;
  const auto scan = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
        node.kids[0].text == "insert_elem") {
      if (node.kids.back().text == "exit") exit_row = true;
      if (node.kids.back().text == "store_preview_audio") preview_row = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan(*method, scan);
  CHECK_TRUE(exit_row);
  CHECK_FALSE(preview_row);

  // Idempotent, and a record without the panel is refused untouched.
  why.clear();
  CHECK_TRUE(rb_blitz::store::HidePreviewButton(&record, &why));
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::HidePreviewButton(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

void TestDownloadedRowGate() {
  BeginCase("the Downloaded row reads the record instead of the offer's native commerce state");

  // `refresh_buttons`' measured shape, from the shipped record (printed 2026-10-22): the purchased
  // branch inserts `store_download_again`, and its gate is the *native* `{offer is_purchased}`.
  const auto insert = [](const char* row) {
    return Node::Command({Symbol("insert_elem"), Symbol("buttons"), Node::Int(0), Symbol(row)});
  };
  const auto native_gate = [] {
    // The record spells the receiver as a **variable** (`[0x02] offer` in the dump), not a symbol.
    return Node::Command({Node::Var("offer"), Symbol("is_purchased")});
  };
  std::vector<Node> record = {
      Node::Command(
          {Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
           Node::Array(
               {Symbol("refresh_buttons"), Node::Array({Node::Var("init")}),
                Node::Command({Symbol("do"), insert("exit"),
                               Node::Command({Symbol("if_else"), native_gate(),
                                              Node::Command({Symbol("do"),
                                                             insert("store_download_again")}),
                                              Node::Command({Symbol("if"),
                                                             Node::Command({Symbol("this"),
                                                                            Symbol("purchase_allowed"),
                                                                            Node::Var("offer")}),
                                                             Node::Command({Symbol("do"),
                                                                            insert("store_buy_now")})})})})})}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::UseDataForDownloadedRow(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method = FindNamedMethodNode(record[0], "store_info_panel", "refresh_buttons");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;

  // The gate is now `{offer get_data (is_purchased)}`; the native read is gone and both rows stay.
  bool native_read = false;
  bool data_read = false;
  bool again_row = false;
  bool buy_row = false;
  const auto scan = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 2 &&
        node.kids[0].text == "offer" && node.kids[1].text == "is_purchased") {
      native_read = true;
    }
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
        node.kids[0].text == "offer" && node.kids[1].text == "get_data") {
      const Node& path = node.kids[2];
      if (path.tag == rb_blitz::ui::dtb::kArray && path.kids.size() == 1 &&
          path.kids[0].text == "is_purchased") {
        data_read = true;
      }
    }
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
        node.kids[0].text == "insert_elem") {
      if (node.kids.back().text == "store_download_again") again_row = true;
      if (node.kids.back().text == "store_buy_now") buy_row = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan(*method, scan);
  CHECK_FALSE(native_read);
  CHECK_TRUE(data_read);
  CHECK_TRUE(again_row);
  CHECK_TRUE(buy_row);

  // Idempotent: a second run changes nothing and still reports success.
  const std::vector<Node> after_first = record;
  why.clear();
  CHECK_TRUE(rb_blitz::store::UseDataForDownloadedRow(&record, &why));
  CHECK_TRUE(TreeUnchanged(record, after_first));

  // A record without the panel, or a method with no such gate, is refused untouched.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::UseDataForDownloadedRow(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));

  std::vector<Node> no_gate = {
      Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                     Node::Array({Symbol("refresh_buttons"), Node::Array({Node::Var("init")}),
                                  Node::Command({Symbol("do"), insert("exit")})})}),
  };
  const std::vector<Node> no_gate_untouched = no_gate;
  why.clear();
  CHECK_FALSE(rb_blitz::store::UseDataForDownloadedRow(&no_gate, &why));
  CHECK_TRUE(TreeUnchanged(no_gate, no_gate_untouched));
}

// P5's row: a not-downloaded offer's page gains Download, and retail's own branch survives inside
// it. The shape is `refresh_buttons`' own (printed from the shipped record 2026-10-22) - the gate
// first, then the else branch with the buy path.
void TestDownloadButtonAdded() {
  BeginCase("the details page's Download row goes into the not-downloaded branch");

  const auto insert = [](const char* row) {
    return Node::Command({Symbol("insert_elem"), Node::Var("buttons"), Node::Int(0), Symbol(row)});
  };
  const auto buy_branch = [&insert] {
    return Node::Command({Symbol("if"),
                          Node::Command({Symbol("this"), Symbol("purchase_allowed"),
                                         Node::Var("offer")}),
                          Node::Command({Symbol("do"), insert("store_buy_now")})});
  };
  const auto record_with_gate = [&insert, &buy_branch] {
    return std::vector<Node>{
        Node::Command(
            {Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
             Node::Array(
                 {Symbol("refresh_buttons"), Node::Array({Node::Var("init")}),
                  Node::Command(
                      {Symbol("do"), insert("exit"),
                       Node::Command({Symbol("if_else"),
                                      Node::Command({Node::Var("offer"), Symbol("get_data"),
                                                     Node::Array({Symbol("is_purchased")})}),
                                      Node::Command({Symbol("do"), insert("store_download_again")}),
                                      buy_branch()})})})}),
    };
  };

  std::vector<Node> record = record_with_gate();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddDownloadButton(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method = FindNamedMethodNode(record[0], "store_info_panel", "refresh_buttons");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;

  // The new row exists exactly once, and it is a sibling of retail's buy path inside the else
  // branch - not inside the downloaded branch (where the page already offers "Download Again").
  bool now_row = false;
  bool buy_row = false;
  const auto scan = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
        node.kids[0].text == "insert_elem") {
      if (node.kids.back().text == "store_download_now") now_row = true;
      if (node.kids.back().text == "store_buy_now") buy_row = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan(*method, scan);
  CHECK_TRUE(now_row);
  CHECK_TRUE(buy_row);

  // The row sits in the `do` that *is* the else branch: the gate's kid 3, whose first statement
  // inserts it.
  const Node* gate = nullptr;
  const auto find_gate = [&](const Node& node, const auto& self) -> void {
    if (gate != nullptr) return;
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
        node.kids[0].text == "if_else") {
      for (const Node& kid : node.kids) {
        if (kid.tag == rb_blitz::ui::dtb::kCommand && kid.kids.size() >= 2 &&
            kid.kids[0].text == "do" && kid.kids[1].tag == rb_blitz::ui::dtb::kCommand &&
            kid.kids[1].kids.size() >= 4 && kid.kids[1].kids[3].text == "store_download_now") {
          gate = &node;
          return;
        }
      }
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  find_gate(*method, find_gate);
  CHECK_TRUE(gate != nullptr);
  if (gate != nullptr) {
    CHECK_TRUE(gate->kids.size() == 4);  // if_else, test, then, else
    CHECK_TRUE(gate->kids[3].tag == rb_blitz::ui::dtb::kCommand);
    CHECK_TRUE(gate->kids[3].kids[0].text == "do");
    CHECK_TRUE(gate->kids[3].kids.size() == 3);  // the insert, then retail's own branch by value
  }

  // Idempotent: a second pass is refused by name and changes nothing.
  const std::vector<Node> after_first = record;
  why.clear();
  CHECK_TRUE(rb_blitz::store::AddDownloadButton(&record, &why));
  CHECK_TRUE(TreeUnchanged(record, after_first));

  // Refusals, each before any edit: a record with no such panel, and a panel whose button method
  // carries no Downloaded gate.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::AddDownloadButton(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));

  std::vector<Node> no_gate = {
      Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                     Node::Array({Symbol("refresh_buttons"), Node::Array({Node::Var("init")}),
                                  Node::Command({Symbol("do"), insert("exit")})})}),
  };
  const std::vector<Node> no_gate_untouched = no_gate;
  why.clear();
  CHECK_FALSE(rb_blitz::store::AddDownloadButton(&no_gate, &why));
  CHECK_TRUE(TreeUnchanged(no_gate, no_gate_untouched));
}

// P5's press: both rows reach the request channel, and neither calls the commerce object.
void TestDownloadButtonsRouted() {
  BeginCase("the Download rows write a request instead of calling the commerce object");

  // `SELECT_MSG`' measured shape: `{switch {component selected_sym} [case body...] ...}`.
  const auto record_with_switch = [] {
    return std::vector<Node>{
        Node::Command(
            {Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
             Node::Array(
                 {Symbol("SELECT_MSG"),
                  Node::Command(
                      {Symbol("switch"),
                       Node::Command({Node::Var("component"), Symbol("selected_sym")}),
                       Node::Array({Symbol("store_buy_now"),
                                    Node::Command({Symbol("store_panel"), Symbol("check_out"),
                                                   Node::Var("offer")})}),
                       Node::Array({Symbol("store_download_again"),
                                    Node::Command({Symbol("store_panel"), Symbol("re_download"),
                                                   Node::Var("offer")}),
                                    Node::Command({Node::Command({Symbol("ui"),
                                                                  Symbol("get_recommendation_mgr")}),
                                                   Symbol("expire_store_rec"),
                                                   Node::Var("offer")})})})})}),
    };
  };

  std::vector<Node> record = record_with_switch();
  std::string why;
  CHECK_TRUE(rb_blitz::store::RouteDownloadButtons(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method = FindNamedMethodNode(record[0], "store_info_panel", "SELECT_MSG");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;

  // The native `re_download` call is gone, and both cases carry a `write_file` whose argument is a
  // variable - a builtin's argument is not evaluated by the guest, so the path is built first.
  struct CaseShape {
    bool found = false;
    bool native = false;
    bool writes = false;
    bool literal_path = false;
    bool var_path = false;
    bool popup = false;
    bool popup_before_write = false;
  };
  const auto scan_case = [&](const char* symbol, const char* command) {
    CaseShape shape;
    const auto scan = [&](const Node& node, const auto& self) -> void {
      if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
          node.kids[0].text == "store_panel" && node.kids[1].text == "re_download") {
        shape.native = true;
      }
      if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
          node.kids[0].text == "write_file") {
        shape.writes = true;
        if (node.kids[1].tag == rb_blitz::ui::dtb::kVar && node.kids[1].text == "path") {
          shape.var_path = true;
        } else if (node.kids[1].tag == rb_blitz::ui::dtb::kString) {
          shape.literal_path = node.kids[1].text.find(command) != std::string::npos;
        }
      }
      // P9-6: the press shows the popup, and it does so *before* the request is written - the
      // `do` that writes the path has the popup call as an earlier statement.
      if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
          node.kids[0].text == "do") {
        size_t popup_at = node.kids.size();
        size_t write_at = node.kids.size();
        for (size_t i = 1; i < node.kids.size(); ++i) {
          const Node& kid = node.kids[i];
          if (kid.tag == rb_blitz::ui::dtb::kCommand && kid.kids.size() >= 2 &&
              kid.kids[0].text == rb_blitz::store::kDownloadPanelName &&
              kid.kids[1].text == "show_popup") {
            shape.popup = true;
            popup_at = i;
          }
          if (kid.tag == rb_blitz::ui::dtb::kCommand && !kid.kids.empty() &&
              kid.kids[0].text == "write_file") {
            write_at = i;
          }
        }
        if (popup_at != node.kids.size() && write_at != node.kids.size() && popup_at < write_at) {
          shape.popup_before_write = true;
        }
      }
      for (const Node& kid : node.kids) self(kid, self);
    };
    for (const Node& node : method->kids) {
      if (node.tag != rb_blitz::ui::dtb::kCommand) continue;
      for (const Node& kid : node.kids) {
        if (kid.tag != rb_blitz::ui::dtb::kArray || kid.kids.empty()) continue;
        if (kid.kids[0].text != symbol) continue;
        shape.found = true;
        scan(kid, scan);
      }
    }
    return shape;
  };

  {
    const CaseShape again = scan_case("store_download_again", "again");
    CHECK_TRUE(again.found);
    CHECK_FALSE(again.native);
    CHECK_TRUE(again.writes);
    CHECK_FALSE(again.literal_path);  // never a literal: the token is the offer's own key
    CHECK_TRUE(again.var_path);
    CHECK_TRUE(again.popup);
    CHECK_TRUE(again.popup_before_write);
  }
  {
    const CaseShape now = scan_case("store_download_now", "start");
    CHECK_TRUE(now.found);
    CHECK_FALSE(now.native);
    CHECK_TRUE(now.writes);
    CHECK_FALSE(now.literal_path);
    CHECK_TRUE(now.var_path);
    CHECK_TRUE(now.popup);
    CHECK_TRUE(now.popup_before_write);
  }
  // The path's own spelling, in the `{var $path {sprintf …}}` statement both cases carry.
  bool start_path = false;
  bool again_path = false;
  const auto scan_paths = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
        node.kids[0].text == "sprintf" && node.kids[1].tag == rb_blitz::ui::dtb::kString) {
      if (node.kids[1].text.find("rbbz-store/download/start/%s") != std::string::npos) {
        start_path = true;
      }
      if (node.kids[1].text.find("rbbz-store/download/again/%s") != std::string::npos) {
        again_path = true;
      }
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan_paths(*method, scan_paths);
  CHECK_TRUE(start_path);
  CHECK_TRUE(again_path);
  // Both cases guard the read: a record without a job answers with a message instead of walking a
  // null (`{offer has_data job}` - the same guard `refresh_buttons` puts before `preview_art`).
  int guarded = 0;
  const auto scan_guard = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
        node.kids[0].text == "offer" && node.kids[1].text == "has_data" &&
        node.kids[2].text == "job") {
      ++guarded;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan_guard(*method, scan_guard);
  CHECK_TRUE(guarded == 2);

  // Idempotent: a second run is refused by name and changes nothing.
  const std::vector<Node> after_first = record;
  why.clear();
  CHECK_TRUE(rb_blitz::store::RouteDownloadButtons(&record, &why));
  CHECK_TRUE(TreeUnchanged(record, after_first));

  // A switch with no Download Again case, and a record with no switch at all: both refused
  // untouched (nothing else's press is ever rewritten).
  std::vector<Node> buy_only = {
      Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                     Node::Array({Symbol("SELECT_MSG"),
                                  Node::Command({Symbol("switch"),
                                                 Node::Command({Node::Var("component"),
                                                                Symbol("selected_sym")}),
                                                 Node::Array({Symbol("store_buy_now"),
                                                              Node::Command({Symbol("store_panel"),
                                                                             Symbol("check_out")})})})})}),
  };
  const std::vector<Node> buy_only_untouched = buy_only;
  why.clear();
  CHECK_FALSE(rb_blitz::store::RouteDownloadButtons(&buy_only, &why));
  CHECK_TRUE(TreeUnchanged(buy_only, buy_only_untouched));

  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::RouteDownloadButtons(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

// P9-6: the modal the Download press opens on the details screen.
void TestDownloadPopup() {
  BeginCase("the Download press opens a modal popup on the details screen");

  const auto record_with_screen = [] {
    return std::vector<Node>{
        Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                       Node::Array({Symbol("focus"), Symbol("buttons.lst")})}),
        Node::Command({Symbol("new"), Symbol("UIScreen"), Symbol("store_info_screen"),
                       Node::Array({Symbol("panels"), Symbol("background_panel"),
                                    Symbol("store_info_panel"), Symbol("error_popup_panel")}),
                       Node::Array({Symbol("focus"), Symbol("store_info_panel")})}),
    };
  };

  std::vector<Node> record = record_with_screen();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddDownloadPopup(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  CHECK_EQ(record.size(), size_t{3});

  // The definition is first (`new UIPanel rbbz_download_panel ...`), before the screen naming it,
  // and it carries the search modal's own resource.
  const Node& panel = record[0];
  CHECK_TRUE(panel.tag == rb_blitz::ui::dtb::kCommand && panel.kids.size() >= 3);
  if (panel.kids.size() < 3) return;
  CHECK_STR_EQ(panel.kids[1].text, "UIPanel");
  CHECK_STR_EQ(panel.kids[2].text, rb_blitz::store::kDownloadPanelName);
  bool resource = false;
  for (const Node& kid : panel.kids) {
    if (kid.tag == rb_blitz::ui::dtb::kArray && kid.kids.size() == 2 &&
        kid.kids[0].text == "file" && kid.kids[1].tag == rb_blitz::ui::dtb::kString &&
        kid.kids[1].text == "../error/error_general.milo") {
      resource = true;
    }
  }
  CHECK_TRUE(resource);

  // Every method the popup's life runs through, and every sentence / bridge path it needs: the
  // three terminal predicates, the cancel command, both outcome methods and the three tokens.
  const auto method = [&](std::string_view name) -> const Node* {
    for (const Node& kid : panel.kids) {
      if (kid.tag == rb_blitz::ui::dtb::kArray && !kid.kids.empty() && kid.kids[0].text == name) {
        return &kid;
      }
    }
    return nullptr;
  };
  for (const char* name : {"enter", "exit", "show_popup", "hide_popup", "finish_installed",
                           "finish_failed", "poll", "BUTTON_DOWN_MSG"}) {
    CHECK_TRUE(method(name) != nullptr);
  }
  std::vector<std::string> texts;
  const auto collect = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kString || node.tag == rb_blitz::ui::dtb::kSymbol) {
      texts.push_back(node.text);
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  collect(panel, collect);
  const auto has = [&](std::string_view value) {
    return std::find(texts.begin(), texts.end(), value) != texts.end();
  };
  CHECK_TRUE(has("rbbz-store/state/%s/installed"));
  CHECK_TRUE(has("rbbz-store/state/%s/failed"));
  CHECK_TRUE(has("rbbz-store/state/%s/cancelled"));
  CHECK_TRUE(has("rbbz-store/download/cancel/%s"));
  CHECK_TRUE(has("finish_installed"));
  CHECK_TRUE(has("finish_failed"));
  CHECK_TRUE(has(rb_blitz::store::kDownloadLoadingToken));
  CHECK_TRUE(has(rb_blitz::store::kDownloadCancelToken));
  CHECK_TRUE(has(rb_blitz::store::kDownloadCloseToken));
  CHECK_TRUE(has(rb_blitz::store::kDownloadRefreshToken));
  CHECK_TRUE(has(rb_blitz::store::kDownloadFailedToken));
  // The installed line is composed from the offer with the writer the credit block already draws
  // with, and both the job and the song's name are the offer's own keys behind its `has_data` guard
  // (and behind the object-truthiness test, so a property read that answered nothing is not walked).
  CHECK_TRUE(has("Downloaded '%s'."));
  CHECK_TRUE(has("set_display_text_string"));
  CHECK_TRUE(has("cur_offer"));
  CHECK_TRUE(has("has_data"));
  CHECK_TRUE(has("job"));
  CHECK_TRUE(has("name"));
  CHECK_TRUE(has("store_info_panel"));

  // The screen lists the popup last, so its `all.grp` draws over the screen's own panels.
  const Node* screen = nullptr;
  for (const Node& root : record) {
    if (root.tag == rb_blitz::ui::dtb::kCommand && root.kids.size() >= 3 &&
        root.kids[0].text == "new" && root.kids[1].text == "UIScreen" &&
        root.kids[2].text == "store_info_screen") {
      screen = &root;
    }
  }
  CHECK_TRUE(screen != nullptr);
  bool listed = false;
  if (screen != nullptr) {
    for (const Node& kid : screen->kids) {
      if (kid.tag != rb_blitz::ui::dtb::kArray || kid.kids.empty() || kid.kids[0].text != "panels") {
        continue;
      }
      listed = !kid.kids.empty() && kid.kids.back().text == rb_blitz::store::kDownloadPanelName;
    }
  }
  CHECK_TRUE(listed);

  // Idempotent: a second pass finds its own definition and changes nothing.
  const std::vector<Node> after_first = record;
  why.clear();
  CHECK_TRUE(rb_blitz::store::AddDownloadPopup(&record, &why));
  CHECK_TRUE(TreeUnchanged(record, after_first));

  // The record survives compile -> parse, which is what delivery does.
  {
    const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(after_first);
    std::vector<Node> parsed;
    rb_blitz::ui::dtb::RootHeader header;
    why.clear();
    CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
    bool parsed_panel = false;
    for (const Node& root : parsed) {
      if (root.tag == rb_blitz::ui::dtb::kCommand && root.kids.size() >= 3 &&
          root.kids[1].text == "UIPanel" &&
          root.kids[2].text == rb_blitz::store::kDownloadPanelName) {
        parsed_panel = true;
      }
    }
    CHECK_TRUE(parsed_panel);
  }

  // Refusals: a record with no details screen, and one whose screen lists no panels. Both leave
  // the tree byte-identical - a popup nobody draws is worse than none.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> other_untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::AddDownloadPopup(&other, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(other, other_untouched));

  std::vector<Node> no_panels = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                                Symbol("store_info_screen")})};
  const std::vector<Node> no_panels_untouched = no_panels;
  why.clear();
  CHECK_FALSE(rb_blitz::store::AddDownloadPopup(&no_panels, &why));
  CHECK_TRUE(TreeUnchanged(no_panels, no_panels_untouched));
}

// P9-14: the installed toast's A Refresh.
void TestDownloadRefreshAction() {
  BeginCase("the installed toast offers A Refresh and runs the menu's own refresh");

  const auto record_with_screen = [] {
    return std::vector<Node>{
        Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                       Node::Array({Symbol("focus"), Symbol("buttons.lst")})}),
        Node::Command({Symbol("new"), Symbol("UIScreen"), Symbol("store_info_screen"),
                       Node::Array({Symbol("panels"), Symbol("background_panel"),
                                    Symbol("store_info_panel"), Symbol("error_popup_panel")})}),
    };
  };
  std::vector<Node> record = record_with_screen();
  std::string why;
  CHECK_TRUE(rb_blitz::store::AddDownloadPopup(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node& panel = record[0];

  const auto method = [&](std::string_view name) -> const Node* {
    for (const Node& kid : panel.kids) {
      if (kid.tag == rb_blitz::ui::dtb::kArray && !kid.kids.empty() && kid.kids[0].text == name) {
        return &kid;
      }
    }
    return nullptr;
  };
  CHECK_TRUE(method("refresh_songs") == nullptr);  // no second method: the two statements are inline

  // `installed` is declared beside `active`/`terminal` and starts FALSE - the caption and the
  // action both hang off it, so it must exist before any press can reach the panel.
  bool declared = false;
  for (const Node& kid : panel.kids) {
    if (kid.tag == rb_blitz::ui::dtb::kArray && kid.kids.size() == 2 &&
        kid.kids[0].text == "installed" && kid.kids[1].text == "FALSE") {
      declared = true;
    }
  }
  CHECK_TRUE(declared);

  // The three methods that write it: a new popup starts with it clear, an install sets it, and a
  // failure clears it again - so neither the A caption nor the A action can outlive a job that
  // installed nothing.
  const auto writes = [&](std::string_view name, const char* value) {
    const Node* body = method(name);
    if (body == nullptr) return false;
    bool found = false;
    const auto scan = [&](const Node& node, const auto& self) -> void {
      if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
          node.kids[0].text == "set" && node.kids[1].tag == rb_blitz::ui::dtb::kProperty &&
          !node.kids[1].kids.empty() && node.kids[1].kids[0].text == "installed" &&
          node.kids[2].text == value) {
        found = true;
      }
      for (const Node& kid : node.kids) self(kid, self);
    };
    scan(*body, scan);
    return found;
  };
  CHECK_TRUE(writes("show_popup", "FALSE"));
  CHECK_TRUE(writes("finish_installed", "TRUE"));
  CHECK_TRUE(writes("finish_failed", "FALSE"));

  // The installed state's prompt is the new token: A Refresh   B Close.
  {
    const Node* body = method("finish_installed");
    CHECK_TRUE(body != nullptr);
    bool prompt = false;
    if (body != nullptr) {
      const auto scan = [&](const Node& node, const auto& self) -> void {
        if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
            node.kids[0].text == "continue.lbl" && node.kids[1].text == "set_token_fmt" &&
            node.kids[2].text == rb_blitz::store::kDownloadRefreshToken) {
          prompt = true;
        }
        for (const Node& kid : node.kids) self(kid, self);
      };
      scan(*body, scan);
    }
    CHECK_TRUE(prompt);
  }

  // The A case lives in the same switch B does, and it is one guarded expression: `{if
  // [installed] {do <close> {file_exists <sentinel>} {ui goto_screen <discovery screen>}}}`. The
  // navigation is the LAST statement on purpose (store_payload.h: a `do` does not survive the
  // screen change it would perform mid-way), and the sentinel/discovery spellings are the menu
  // row's own constants rather than copies.
  const Node* buttons = method("BUTTON_DOWN_MSG");
  CHECK_TRUE(buttons != nullptr);
  if (buttons == nullptr) return;
  const Node* confirm = nullptr;
  const Node* cancel = nullptr;
  const auto find_case = [&](const Node& node, std::string_view symbol, const auto& self)
      -> const Node* {
    if (node.tag == rb_blitz::ui::dtb::kArray && !node.kids.empty() &&
        node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == symbol) {
      return &node;
    }
    for (const Node& kid : node.kids) {
      if (const Node* found = self(kid, symbol, self)) return found;
    }
    return nullptr;
  };
  confirm = find_case(*buttons, "kAction_Confirm", find_case);
  cancel = find_case(*buttons, "kAction_Cancel", find_case);
  CHECK_TRUE(confirm != nullptr);
  CHECK_TRUE(cancel != nullptr);

  // B's case stays what P9-6 shipped: the cancel write and the close, never the refresh.
  if (cancel != nullptr) {
    bool routed = false;
    const auto scan = [&](const Node& node, const auto& self) -> void {
      if (node.tag == rb_blitz::ui::dtb::kString &&
          (node.text == rb_blitz::menu_options::kRefreshSentinelPath ||
           node.text == rb_blitz::menu_options::kRefreshRowDiscoverScreen)) {
        routed = true;
      }
      for (const Node& kid : node.kids) self(kid, self);
    };
    scan(*cancel, scan);
    CHECK_FALSE(routed);
  }

  if (confirm != nullptr) {
    // Three siblings, the row's own order - close, navigate, ask - each guarded by `installed`, so
    // a cancelled or failed job has no action behind the caption it does not draw.
    CHECK_TRUE(confirm->kids.size() == 4);
    for (size_t i = 1; i < 4 && i < confirm->kids.size(); ++i) {
      const Node& step = confirm->kids[i];
      CHECK_TRUE(step.tag == rb_blitz::ui::dtb::kCommand && step.kids.size() == 3 &&
                 step.kids[0].text == "if");
      if (step.tag != rb_blitz::ui::dtb::kCommand || step.kids.size() != 3) continue;
      CHECK_TRUE(step.kids[1].tag == rb_blitz::ui::dtb::kProperty &&
                 !step.kids[1].kids.empty() && step.kids[1].kids[0].text == "installed");
      const Node& body = step.kids[2];
      if (i == 1) {
        // 1. the popup closes (the details page is left behind by the navigation below)
        CHECK_TRUE(body.tag == rb_blitz::ui::dtb::kCommand && body.kids.size() == 2 &&
                   body.kids[0].text == rb_blitz::store::kDownloadPanelName &&
                   body.kids[1].text == "hide_popup");
      } else if (i == 2) {
        // 2. the discovery page the title's own start button enters
        CHECK_TRUE(body.tag == rb_blitz::ui::dtb::kCommand && body.kids.size() == 3 &&
                   body.kids[0].text == "ui" && body.kids[1].text == "goto_screen" &&
                   body.kids[2].text == rb_blitz::menu_options::kRefreshRowDiscoverScreen);
      } else {
        // 3. the menu row's own sentinel, the call the host answers by re-scanning
        CHECK_TRUE(body.tag == rb_blitz::ui::dtb::kCommand && body.kids.size() == 2 &&
                   body.kids[0].text == "file_exists" &&
                   body.kids[1].tag == rb_blitz::ui::dtb::kString &&
                   body.kids[1].text == rb_blitz::menu_options::kRefreshSentinelPath);
      }
    }
  }
}

void TestAlbumArtLinked() {
  BeginCase("the details page waits for its selected cover and links it after loading");
  // `store_details_panel.refresh`' measured shape: the art block is the `unless` under
  // `{if_else {&& {offer has_data preview_art} {is_allowed_to_preview_offer offer}} ...}`, and its
  // last statement is `{store_panel load_art cur_preview_art this}`.
  const auto art_block = [] {
    return Node::Command(
        {Symbol("unless"),
         Node::Command({Symbol("=="), Node::Var("cur_preview_art"),
                        Node::Command({Symbol("offer"), Symbol("get_data"),
                                       Node::Array({Symbol("preview_art")})})}),
         Node::Command({Symbol("set"), Node::Var("cur_preview_art"),
                        Node::Command({Symbol("offer"), Symbol("get_data"),
                                       Node::Array({Symbol("preview_art")})})}),
         Node::Command(
             {Symbol("album.mat"), Symbol("set"), Symbol("diffuse_tex"),
              Symbol("cover_art_none.tex")}),
         Node::Command({Symbol("store_panel"), Symbol("load_art"),
                        Node::Var("cur_preview_art"), Symbol("this")})});
  };
  std::vector<Node> record = {
      Node::Command(
          {Symbol("new"), Symbol("HeldButtonPanel"), Symbol("store_details_panel"),
           Node::Array({Symbol("refresh"), Node::Array({Node::Var("offer")}),
                        Node::Command({Symbol("do"), art_block()})})}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::LinkAlbumArt(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* refresh = FindNamedMethodNode(record[0], "store_details_panel", "refresh");
  const Node* method = FindNamedMethodNode(record[0], "store_details_panel", "poll_store_art");
  CHECK_TRUE(method != nullptr && refresh != nullptr);
  if (method == nullptr || refresh == nullptr) return;

  // The material link follows load_art inside the ready guard, not the initial
  // selection refresh. Cold images must not trigger the offline native fetch.
  const auto find_load_art = [](const Node& node, const auto& self, const Node** parent,
                                size_t* index) -> bool {
    for (size_t i = 0; i < node.kids.size(); ++i) {
      const Node& kid = node.kids[i];
      if (kid.tag == rb_blitz::ui::dtb::kCommand && kid.kids.size() >= 2 &&
          kid.kids[0].text == "store_panel" && kid.kids[1].text == "load_art") {
        *parent = &node;
        *index = i;
        return true;
      }
      if (self(kid, self, parent, index)) return true;
    }
    return false;
  };
  const Node* parent = nullptr;
  size_t index = 0;
  CHECK_TRUE(find_load_art(*method, find_load_art, &parent, &index));
  if (parent == nullptr) return;
  CHECK_TRUE(parent->kids.size() >= index + 2);
  CHECK_TRUE(parent->kids[0].text == "if" && parent->kids[1].kids[0].text == "file_exists");
  if (parent->kids.size() < index + 2) return;
  const Node& link = parent->kids[index + 1];
  CHECK_TRUE(link.tag == rb_blitz::ui::dtb::kCommand);
  CHECK_TRUE(link.kids.size() == 4);
  if (link.kids.size() != 4) return;
  CHECK_STR_EQ(link.kids[0].text, "album.mat");
  CHECK_STR_EQ(link.kids[1].text, "set");
  CHECK_STR_EQ(link.kids[2].text, "diffuse_tex");
  CHECK_TRUE(link.kids[3].tag == rb_blitz::ui::dtb::kCommand);
  CHECK_TRUE(link.kids[3].kids.size() == 2);
  if (link.kids[3].kids.size() == 2) {
    CHECK_STR_EQ(link.kids[3].kids[0].text, "store_panel");
    CHECK_STR_EQ(link.kids[3].kids[1].text, "album_tex");
  }

  // The placeholder assignment retail wrote is still there - this append does not replace it.
  bool placeholder = false;
  const auto scan = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 4 &&
        node.kids[0].text == "album.mat" && node.kids[3].text == "cover_art_none.tex") {
      placeholder = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan(*refresh, scan);
  CHECK_TRUE(placeholder);

  // Idempotent: a second pass adds nothing.
  why.clear();
  CHECK_TRUE(rb_blitz::store::LinkAlbumArt(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  size_t links = 0;
  const auto count = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 4 &&
        node.kids[0].text == "album.mat" && node.kids[1].text == "set" &&
        node.kids[3].tag == rb_blitz::ui::dtb::kCommand && node.kids[3].kids.size() == 2 &&
        node.kids[3].kids[1].text == "album_tex") {
      ++links;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  count(*method, count);
  CHECK_TRUE(links == 1);

  // Compile and parse round trip: the appended command is a shape the compiler accepts.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());

  // A record without the panel is refused untouched.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("splash_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::LinkAlbumArt(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));

  // A `refresh` with no `load_art` call is refused untouched too - never a link to nothing.
  std::vector<Node> no_art = {
      Node::Command({Symbol("new"), Symbol("HeldButtonPanel"), Symbol("store_details_panel"),
                     Node::Array({Symbol("refresh"), Node::Array({Node::Var("offer")}),
                                  Node::Command({Symbol("do"), Symbol("NIL")})})}),
  };
  const std::vector<Node> no_art_untouched = no_art;
  why.clear();
  CHECK_FALSE(rb_blitz::store::LinkAlbumArt(&no_art, &why));
  CHECK_TRUE(why.find("load_art") != std::string::npos);
  CHECK_TRUE(TreeUnchanged(no_art, no_art_untouched));
}

// ---------------------------------------------------------------------------------------------
// P9-7/P9-8 - the store's Start button and its own footer
// ---------------------------------------------------------------------------------------------
//
// The fixtures are the shipped shapes (`out/_swap/store.tree.txt`): the front menu's Start case is
// two statements, the browser's is one `{do ...}`, and each screen's inline help is a scene object
// the record addresses as `InlineHelp.ihp` / `store.ihp` / `store_info.ihp`.

// `{== $action <action>}` - the test a case array begins with, in the shipped spelling.
Node ActionCaseTest(const char* action) {
  return Node::Command({Symbol("=="), Node::Var("action"), Symbol(action)});
}

// The case array for one action, at any depth (null when the method has none).
const Node* FindActionCaseNode(const Node& node, const char* action) {
  if (node.tag == rb_blitz::ui::dtb::kArray && !node.kids.empty() &&
      node.kids[0].tag == rb_blitz::ui::dtb::kCommand && node.kids[0].kids.size() == 3 &&
      node.kids[0].kids[0].text == "==" && node.kids[0].kids[1].text == "action" &&
      node.kids[0].kids[2].text == action) {
    return &node;
  }
  for (const Node& kid : node.kids) {
    if (const Node* found = FindActionCaseNode(kid, action)) return found;
  }
  return nullptr;
}

bool HasBackStatement(const Node& node, std::string_view receiver, std::string_view target) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 4 &&
      node.kids[0].text == receiver && node.kids[1].text == "set" &&
      node.kids[2].text == "back_screen" && node.kids[3].text == target) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasBackStatement(kid, receiver, target)) return true;
  }
  return false;
}

bool HasGotoStatement(const Node& node, std::string_view screen) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
      node.kids[0].text == "ui" && node.kids[1].text == "goto_screen" &&
      node.kids[2].text == screen) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasGotoStatement(kid, screen)) return true;
  }
  return false;
}

// `{<receiver> <method> <action> [<token>]}`.
bool HasHelpStatement(const Node& node, std::string_view receiver, std::string_view method,
                      std::string_view action, std::string_view token = {}) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
      node.kids[0].text == receiver && node.kids[1].text == method &&
      node.kids[2].text == action &&
      (token.empty() || (node.kids.size() >= 4 && node.kids[3].text == token))) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasHelpStatement(kid, receiver, method, action, token)) return true;
  }
  return false;
}

// The record both store screens live in: the front menu panel and the browser panel, each with the
// shipped Start case, and the front menu's `enter` carrying the shipped trailing `{cond …}` whose
// default arm is a `{fail …}` - the reason the footer commands are inserted before it.
std::vector<Node> StoreScreensRecord() {
  const Node front_case = Node::Array(
      {ActionCaseTest("kAction_Start"),
       Node::Command({Symbol("options_screen"), Symbol("set"), Symbol("back_screen"),
                      Symbol("store_menu_screen")}),
       Node::Command({Symbol("ui"), Symbol("goto_screen"), Symbol("options_screen")})});
  const Node browser_case = Node::Array(
      {ActionCaseTest("kAction_Start"),
       Node::Command({Symbol("do"),
                      Node::Command({Symbol("this"), Symbol("turn_off_preview")}),
                      Node::Command({Symbol("options_screen"), Symbol("set"),
                                     Symbol("back_screen"), Symbol("store_menu_screen")}),
                      Node::Command({Symbol("ui"), Symbol("goto_screen"),
                                     Symbol("options_screen")})})});
  const Node fail_arm =
      Node::Command({Symbol("fail"), Text("StorePanel Commerce Session in an illegal state "
                                          "(likely kSessionNone).")});
  const Node loading_arm =
      Node::Command({Symbol("this"), Symbol("set_loading_visual_state"), Symbol("loading")});
  const Node cond =
      Node::Command({Symbol("cond"),
                     Node::Command({Symbol("store_panel"),
                                    Symbol("is_creating_commerce_session")}),
                     loading_arm, fail_arm});
  const Node menu_enter = Node::Array(
      {Symbol("enter"),
       Node::Command({Symbol("store_panel"), Symbol("add_sink"), Node::Var("this")}), cond});
  const Node menu_panel =
      Node::Command({Symbol("new"), Symbol("StoreMenuPanel"), Symbol("store_menu_panel"),
                     Node::Array({Symbol("file"), Text("store_menu.milo")}), menu_enter,
                     Node::Array({Symbol("BUTTON_DOWN_MSG"),
                                  Node::Command({Symbol("cond"), front_case})})});
  const Node refresh_do = Node::Command(
      {Symbol("do"), Node::Command({Symbol("store_panel"), Symbol("update_crumb_text")}),
       Node::Command({Symbol("if_else"),
                      Node::Command({Symbol("store_panel"), Symbol("has_shortcuts")}),
                      Node::Command({Symbol("store.ihp"), Symbol("set_action_token"),
                                     Symbol("kAction_Option"), Symbol("shortcut_header")}),
                      Node::Command({Symbol("store.ihp"), Symbol("clear_action_token"),
                                     Symbol("kAction_Option")})})});
  const Node browser_panel = Node::Command(
      {Symbol("new"), Symbol("HeldButtonPanel"), Symbol("store_browser_panel"),
       Node::Array({Symbol("file"), Text("store_browser.milo")}),
       Node::Array({Symbol("refresh_list"), refresh_do}),
       Node::Array({Symbol("BUTTON_DOWN_MSG"),
                    Node::Command({Symbol("cond"), browser_case})})});
  return {menu_panel, browser_panel};
}

void TestStoreStartRoutesToFilter() {
  BeginCase("the store's Start button opens the Filters page");

  std::vector<Node> record = StoreScreensRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::RouteStoreStartToFilter(
      &record, "settings_rhythmverse_filter_screen", &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* front = FindNamedMethodNode(record[0], "store_menu_panel", "BUTTON_DOWN_MSG");
  const Node* browser = FindNamedMethodNode(record[1], "store_browser_panel", "BUTTON_DOWN_MSG");
  CHECK_TRUE(front != nullptr && browser != nullptr);
  if (front == nullptr || browser == nullptr) return;
  const Node* front_start = FindActionCaseNode(*front, "kAction_Start");
  const Node* browser_start = FindActionCaseNode(*browser, "kAction_Start");
  CHECK_TRUE(front_start != nullptr && browser_start != nullptr);
  if (front_start == nullptr || browser_start == nullptr) return;
  // The destination and its back target are both ours; nothing jumps to retail's options screen.
  for (const Node* start : {front_start, browser_start}) {
    CHECK_TRUE(HasGotoStatement(*start, "settings_rhythmverse_filter_screen"));
    CHECK_FALSE(HasGotoStatement(*start, "options_screen"));
    CHECK_TRUE(HasBackStatement(*start, "settings_rhythmverse_filter_screen", "store_menu_screen"));
    CHECK_FALSE(HasBackStatement(*start, "options_screen", "store_menu_screen"));
  }
  // The browser's own statement around the jump is untouched.
  CHECK_TRUE(HasGotoStatement(*browser_start, "settings_rhythmverse_filter_screen"));
  bool preview_kept = false;
  const auto scan = [&preview_kept](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
        node.kids[0].text == "this" && node.kids[1].text == "turn_off_preview") {
      preview_kept = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan(*browser_start, scan);
  CHECK_TRUE(preview_kept);

  // Idempotent: a second pass finds its own destination and changes nothing.
  const std::vector<uint8_t> once = rb_blitz::ui::dtb::SerializeNodes(record);
  why.clear();
  CHECK_TRUE(rb_blitz::store::RouteStoreStartToFilter(
      &record, "settings_rhythmverse_filter_screen", &why));
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(record) == once);

  // Compile and parse round trip.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());
  if (!parsed.empty()) {
    const Node* again = FindNamedMethodNode(parsed[0], "store_menu_panel", "BUTTON_DOWN_MSG");
    CHECK_TRUE(again != nullptr);
    if (again != nullptr) {
      const Node* start = FindActionCaseNode(*again, "kAction_Start");
      CHECK_TRUE(start != nullptr &&
                 HasGotoStatement(*start, "settings_rhythmverse_filter_screen"));
    }
  }

  // Refusals leave the record exactly as it was: no browser panel, and a Start case that jumps
  // somewhere else entirely.
  {
    std::vector<Node> only_front = StoreScreensRecord();
    only_front.pop_back();
    const std::vector<Node> untouched = only_front;
    why.clear();
    CHECK_FALSE(rb_blitz::store::RouteStoreStartToFilter(
        &only_front, "settings_rhythmverse_filter_screen", &why));
    CHECK_TRUE(why.find("store_browser_panel") != std::string::npos);
    CHECK_TRUE(TreeUnchanged(only_front, untouched));
  }
  {
    const Node other_case = Node::Array(
        {ActionCaseTest("kAction_Start"),
         Node::Command({Symbol("ui"), Symbol("goto_screen"), Symbol("splash_screen")})});
    std::vector<Node> other = {
        Node::Command({Symbol("new"), Symbol("StoreMenuPanel"), Symbol("store_menu_panel"),
                       Node::Array({Symbol("BUTTON_DOWN_MSG"),
                                    Node::Command({Symbol("cond"), other_case})})}),
        Node::Command({Symbol("new"), Symbol("HeldButtonPanel"), Symbol("store_browser_panel"),
                       Node::Array({Symbol("BUTTON_DOWN_MSG"),
                                    Node::Command({Symbol("cond"), other_case})})}),
    };
    const std::vector<Node> untouched = other;
    why.clear();
    CHECK_FALSE(rb_blitz::store::RouteStoreStartToFilter(
        &other, "settings_rhythmverse_filter_screen", &why));
    CHECK_TRUE(why.find("options_screen") != std::string::npos);
    CHECK_TRUE(TreeUnchanged(other, untouched));
  }
  // An empty destination is refused too.
  std::vector<Node> record2 = StoreScreensRecord();
  why.clear();
  CHECK_FALSE(rb_blitz::store::RouteStoreStartToFilter(&record2, "", &why));
}

void TestStoreFooterStart() {
  BeginCase("the store's footer names the Filters button");

  std::vector<Node> record = StoreScreensRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::SetStoreFooterStart(
      &record, rb_blitz::store::kFooterSearchFiltersToken, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* menu_enter = FindNamedMethodNode(record[0], "store_menu_panel", "enter");
  const Node* refresh = FindNamedMethodNode(record[1], "store_browser_panel", "refresh_list");
  CHECK_TRUE(menu_enter != nullptr && refresh != nullptr);
  if (menu_enter == nullptr || refresh == nullptr) return;
  CHECK_TRUE(HasHelpStatement(*menu_enter, "InlineHelp.ihp", "set_action_token", "kAction_Start",
                              rb_blitz::store::kFooterSearchFiltersToken));
  CHECK_TRUE(HasHelpStatement(*refresh, "store.ihp", "set_action_token", "kAction_Start",
                              rb_blitz::store::kFooterSearchFiltersToken));
  // The front menu's command is the method's **first** statement, before the shipped `{cond …}`
  // whose default arm is `{fail …}` (P9-7 record: the owner's boot showed the screen-level
  // placement does nothing, and a statement after that arm is one this build cannot rely on).
  CHECK_TRUE(menu_enter->kids.size() >= 2 && menu_enter->kids[1].tag == rb_blitz::ui::dtb::kCommand &&
             menu_enter->kids[1].kids.size() >= 3 &&
             menu_enter->kids[1].kids[0].text == "InlineHelp.ihp" &&
             menu_enter->kids[1].kids[1].text == "set_action_token");
  bool fail_kept = false;
  for (const Node& kid : menu_enter->kids) {
    if (kid.tag == rb_blitz::ui::dtb::kCommand && !kid.kids.empty() &&
        kid.kids[0].text == "cond") {
      for (const Node& arm : kid.kids) {
        if (arm.tag == rb_blitz::ui::dtb::kCommand && !arm.kids.empty() &&
            arm.kids[0].text == "fail") {
          fail_kept = true;
        }
      }
    }
  }
  CHECK_TRUE(fail_kept);
  // The token is a symbol, not a quoted string: `set_action_token` interns its argument.
  const auto token_is_symbol = [](const Node& node, const auto& self) -> bool {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 4 &&
        node.kids[1].text == "set_action_token" && node.kids[2].text == "kAction_Start") {
      return node.kids[3].tag == rb_blitz::ui::dtb::kSymbol;
    }
    for (const Node& kid : node.kids) {
      if (self(kid, self)) return true;
    }
    return false;
  };
  CHECK_TRUE(token_is_symbol(*menu_enter, token_is_symbol));
  CHECK_TRUE(token_is_symbol(*refresh, token_is_symbol));

  // Idempotent.
  const std::vector<uint8_t> once = rb_blitz::ui::dtb::SerializeNodes(record);
  why.clear();
  CHECK_TRUE(rb_blitz::store::SetStoreFooterStart(
      &record, rb_blitz::store::kFooterSearchFiltersToken, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(record) == once);

  // Compile and parse round trip.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());

  // Refusals: no browser panel, and an empty token.
  {
    std::vector<Node> only_front = StoreScreensRecord();
    only_front.pop_back();
    const std::vector<Node> untouched = only_front;
    why.clear();
    CHECK_FALSE(rb_blitz::store::SetStoreFooterStart(
        &only_front, rb_blitz::store::kFooterSearchFiltersToken, &why));
    CHECK_TRUE(TreeUnchanged(only_front, untouched));
  }
  std::vector<Node> record2 = StoreScreensRecord();
  why.clear();
  CHECK_FALSE(rb_blitz::store::SetStoreFooterStart(&record2, "", &why));
}

void TestStoreFooterOptionCleared() {
  BeginCase("the store's footer draws no View Item caption");

  std::vector<Node> record = StoreScreensRecord();
  std::string why;
  CHECK_TRUE(rb_blitz::store::ClearStoreFooterOption(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);

  const Node* menu_enter = FindNamedMethodNode(record[0], "store_menu_panel", "enter");
  const Node* refresh = FindNamedMethodNode(record[1], "store_browser_panel", "refresh_list");
  CHECK_TRUE(menu_enter != nullptr && refresh != nullptr);
  if (menu_enter == nullptr || refresh == nullptr) return;
  CHECK_TRUE(HasHelpStatement(*menu_enter, "InlineHelp.ihp", "clear_action_token",
                              "kAction_Option"));
  CHECK_TRUE(HasHelpStatement(*refresh, "store.ihp", "clear_action_token", "kAction_Option"));
  // The front menu's clear is the method's first statement (see `TestStoreFooterStart`).
  CHECK_TRUE(menu_enter->kids.size() >= 2 && menu_enter->kids[1].tag == rb_blitz::ui::dtb::kCommand &&
             menu_enter->kids[1].kids.size() >= 3 &&
             menu_enter->kids[1].kids[0].text == "InlineHelp.ihp" &&
             menu_enter->kids[1].kids[1].text == "clear_action_token");
  // The browser's unconditional clear sits **after** its own shortcut-header gate: the statement
  // order inside the method is `do(... if_else ...) ...`, so the gate is still there and the clear
  // is a later sibling.
  bool gate_first = false;
  size_t gate_index = 0;
  size_t clear_index = 0;
  for (size_t i = 0; i < refresh->kids.size(); ++i) {
    if (refresh->kids[i].tag == rb_blitz::ui::dtb::kCommand &&
        refresh->kids[i].kids.size() >= 1 && refresh->kids[i].kids[0].text == "do") {
      gate_index = i;
      gate_first = true;
    }
    if (refresh->kids[i].tag == rb_blitz::ui::dtb::kCommand &&
        refresh->kids[i].kids.size() >= 3 && refresh->kids[i].kids[0].text == "store.ihp" &&
        refresh->kids[i].kids[1].text == "clear_action_token") {
      clear_index = i;
    }
  }
  CHECK_TRUE(gate_first && clear_index > gate_index);

  // Idempotent.
  const std::vector<uint8_t> once = rb_blitz::ui::dtb::SerializeNodes(record);
  why.clear();
  CHECK_TRUE(rb_blitz::store::ClearStoreFooterOption(&record, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(record) == once);

  // Compile and parse round trip.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());

  // A record without the front-menu panel is refused untouched, and the reason names it.
  std::vector<Node> no_panel = StoreScreensRecord();
  no_panel.erase(no_panel.begin());
  const std::vector<Node> untouched = no_panel;
  why.clear();
  CHECK_FALSE(rb_blitz::store::ClearStoreFooterOption(&no_panel, &why));
  CHECK_TRUE(why.find("store_menu_panel") != std::string::npos);
  CHECK_TRUE(TreeUnchanged(no_panel, untouched));
}

void TestDetailsFooterOptionCleared() {
  BeginCase("the details page draws no View Item caption");

  std::vector<Node> record = {
      Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                     Node::Array({Symbol("file"), Text("store_info.milo")}),
                     Node::Array({Symbol("enter"),
                                  Node::Command({Symbol("this"), Symbol("refresh_panel")}),
                                  Node::Command({Symbol("store_preview_mgr"), Symbol("add_sink"),
                                                 Node::Var("this")})})}),
  };
  std::string why;
  CHECK_TRUE(rb_blitz::store::ClearDetailsFooterOption(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* enter = FindNamedMethodNode(record[0], "store_info_panel", "enter");
  CHECK_TRUE(enter != nullptr);
  if (enter == nullptr) return;
  // Both inline-help objects the details scene declares are cleared; the panel's own enter work is
  // untouched.
  CHECK_TRUE(HasHelpStatement(*enter, "store_info.ihp", "clear_action_token", "kAction_Option"));
  CHECK_TRUE(HasHelpStatement(*enter, "view_offer.ihp", "clear_action_token", "kAction_Option"));
  CHECK_TRUE(HasHelpStatement(*enter, "store_preview_mgr", "add_sink", "this"));

  // Idempotent.
  const std::vector<uint8_t> once = rb_blitz::ui::dtb::SerializeNodes(record);
  why.clear();
  CHECK_TRUE(rb_blitz::store::ClearDetailsFooterOption(&record, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(record) == once);

  // Compile and parse round trip.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());

  // A record without the panel is refused untouched.
  std::vector<Node> other = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                            Symbol("store_info_screen")})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::ClearDetailsFooterOption(&other, &why));
  CHECK_TRUE(TreeUnchanged(other, untouched));
}

// P9-13's fixture: `store_info_panel.refresh_panel`'s measured shape
// (`out/_swap/store_info_panel.txt`, 2026-10-23): a single `{do ...}` body that binds `$offer`,
// writes the name, drives the rating group's two labels and the record's description, and hides
// the two cost labels. Every receiver the transform anchors on appears exactly as the shipped
// record spells it.
std::vector<Node> StoreInfoCreditsRecord() {
  std::vector<Node> body;
  body.push_back(Node::Array({Node::Var("offer"), Node::Property({Symbol("cur_offer")})}));
  body.push_back(Node::Command({Symbol("song.lbl"), Symbol("set_offer_name"), Node::Var("offer")}));
  body.push_back(Node::Command({Symbol("rating.grp"), Symbol("set_showing"), Symbol("FALSE")}));
  body.push_back(Node::Command({Symbol("rating.lbl"), Symbol("set_showing"), Symbol("FALSE")}));
  body.push_back(Node::Command({Symbol("rating_description.lbl"), Symbol("set"),
                                Symbol("text_token"), Text("rating_0")}));
  body.push_back(Node::Command({Symbol("drm.lbl"), Symbol("set_offer_description"),
                                Node::Var("offer")}));
  body.push_back(Node::Command({Symbol("cost.lbl"), Symbol("set_showing"), Symbol("FALSE")}));
  body.push_back(Node::Command({Symbol("cost_ps3.lbl"), Symbol("set_showing"), Symbol("FALSE")}));
  Node do_body = Node::Command({Symbol("do")});
  do_body.kids = std::move(body);
  return {Node::Command({Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"),
                         Node::Array({Symbol("file"), Text("store_info.milo")}),
                         Node::Array({Symbol("refresh_buttons"),
                             Node::Command({Symbol("buttons.lst"), Symbol("set_data"),
                                            Node::Var("buttons")})}),
                         Node::Array({Symbol("refresh_panel"), std::move(do_body)})})};
}

// `{<receiver> set_display_text_string <value>}` at any depth.
const Node* FindDisplayText(const Node& node, std::string_view receiver) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
      node.kids[0].text == receiver && node.kids[1].text == "set_display_text_string") {
    return &node;
  }
  for (const Node& kid : node.kids) {
    if (const Node* found = FindDisplayText(kid, receiver)) return found;
  }
  return nullptr;
}

// `{offer get_data (<key>)}` at any depth.
bool HasCreditDataRead(const Node& node, std::string_view key) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 3 &&
      node.kids[0].tag == rb_blitz::ui::dtb::kVar && node.kids[0].text == "offer" &&
      node.kids[1].text == "get_data" && node.kids[2].tag == rb_blitz::ui::dtb::kArray &&
      node.kids[2].kids.size() == 1 && node.kids[2].kids[0].text == key) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasCreditDataRead(kid, key)) return true;
  }
  return false;
}

// The format string of a `sprintf` anywhere in the subtree, or empty.
std::string FindSprintfFormat(const Node& node) {
  if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
      node.kids[0].tag == rb_blitz::ui::dtb::kSymbol && node.kids[0].text == "sprintf" &&
      node.kids[1].tag == rb_blitz::ui::dtb::kString) {
    return node.kids[1].text;
  }
  for (const Node& kid : node.kids) {
    std::string found = FindSprintfFormat(kid);
    if (!found.empty()) return found;
  }
  return {};
}

void TestOfferCredits() {
  BeginCase("the details page's box carries the custom's own credit block");

  std::vector<Node> record = StoreInfoCreditsRecord();
  const Node* before = FindNamedMethodNode(record[0], "store_info_panel", "refresh_panel");
  CHECK_TRUE(before != nullptr);
  if (before == nullptr) return;
  const size_t before_count = before->kids.size();

  std::string why;
  CHECK_TRUE(rb_blitz::store::ShowOfferCredits(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* panel = FindNamedMethodNode(record[0], "store_info_panel", "refresh_panel");
  CHECK_TRUE(panel != nullptr);
  if (panel == nullptr) return;
  // The method's own body is untouched, and one `{do ...}` block follows it: a method-level
  // `do` is retail's own shape for a body that binds a local (`refresh_buttons`), and the binding
  // inside it is the compiled record's array statement, not a `{var}` command.
  CHECK_EQ(panel->kids.size(), before_count + 1);
  const std::vector<uint8_t> once = rb_blitz::ui::dtb::SerializeNodes(record);

  bool binding_ok = false;
  if (panel->kids.size() == before_count + 1) {
    const Node& block = panel->kids.back();
    binding_ok = block.tag == rb_blitz::ui::dtb::kCommand && block.kids.size() >= 2 &&
                 block.kids[0].tag == rb_blitz::ui::dtb::kSymbol && block.kids[0].text == "do" &&
                 block.kids[1].tag == rb_blitz::ui::dtb::kArray && block.kids[1].kids.size() == 2 &&
                 block.kids[1].kids[0].tag == rb_blitz::ui::dtb::kVar &&
                 block.kids[1].kids[0].text == "offer" &&
                 block.kids[1].kids[1].tag == rb_blitz::ui::dtb::kProperty;
  }
  CHECK_TRUE(binding_ok);
  // The rating group is shown again (retail hid it because a served offer carries no `rating`),
  // and the cost row - which the price transform hides - is shown before its line is written.
  CHECK_TRUE(HasHelpStatement(*panel, "rating.grp", "set_showing", "TRUE"));
  CHECK_TRUE(HasHelpStatement(*panel, "cost.lbl", "set_showing", "TRUE"));

  // Song Artist on `drm.lbl`, from the record's own key.
  const Node* artist = FindDisplayText(*panel, "drm.lbl");
  CHECK_TRUE(artist != nullptr);
  if (artist != nullptr) CHECK_TRUE(HasCreditDataRead(artist->kids[2], "artist"));

  // The audio type is drawn through its own guard: a value shows `rating.lbl` and writes it, an
  // empty one hides the label (the plan's "nothing at all when the catalogue has none").
  CHECK_TRUE(HasCreditDataRead(*panel, "audio_type"));
  CHECK_TRUE(HasHelpStatement(*panel, "rating.lbl", "set_showing", "FALSE"));

  // Every text label has bounded, left-aligned ellipsis, including the title and the
  // formerly hidden PS3 cost label, now a separate download-count row.
  for (const char* label : {"song.lbl", "drm.lbl", "rating.lbl", "rating_description.lbl",
                            "cost.lbl", "cost_ps3.lbl"}) {
    const Node* fit = nullptr;
    const Node* align = nullptr;
    const Node* text_size = nullptr;
    bool uses_rnd_text_properties = false;
    const auto find_layout = [&](const Node& node, const auto& self) -> void {
      if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() == 4 &&
          node.kids[0].text == label && node.kids[1].text == "set") {
        if (node.kids[2].text == "fit_type") fit = &node;
        if (node.kids[2].text == "alignment") align = &node;
        if (node.kids[2].text == "align") uses_rnd_text_properties = true;
        const Node& path = node.kids[2];
        if (path.tag == rb_blitz::ui::dtb::kArray && path.kids.size() == 3 &&
            path.kids[0].text == "styles" && path.kids[1].scalar == 0) {
          if (path.kids[2].text == "text_size") text_size = &node;
          if (path.kids[2].text == "size") uses_rnd_text_properties = true;
        }
      }
      for (const Node& kid : node.kids) self(kid, self);
    };
    find_layout(*panel, find_layout);
    CHECK_TRUE(fit != nullptr);
    CHECK_TRUE(align != nullptr);
    // These names come from the shipped UILabel schema / song_select.dtb,
    // not RndText's similarly named properties. Invalid paths can crash the guest.
    CHECK_TRUE(text_size != nullptr);
    CHECK_FALSE(uses_rnd_text_properties);
    if (fit) CHECK_EQ(fit->kids[3].scalar, 2u);
    if (align) CHECK_EQ(align->kids[3].scalar, 0x11u);
    if (text_size) CHECK_EQ(text_size->kids[3].scalar,
                            std::string_view(label) == "song.lbl" ? 24u :
                            std::string_view(label) == "drm.lbl" ? 18u : 14u);
  }
  CHECK_TRUE(FindDisplayText(*panel, "cost_ps3.lbl") != nullptr);
  const Node* release = FindDisplayText(*panel, "cost.lbl");
  CHECK_TRUE(release != nullptr);
  if (release) CHECK_STR_EQ(FindSprintfFormat(*release), "Released at %s");

  // Charted by <name>, Released at <date>, and <N> Downloads - separate lines, with the
  // record's own keys inside the format calls.
  CHECK_STR_EQ(FindSprintfFormat(*panel), "Charted by %s");
  CHECK_TRUE(HasCreditDataRead(*panel, "charter"));
  CHECK_TRUE(HasCreditDataRead(*panel, "rbbz_release_date"));
  CHECK_FALSE(HasCreditDataRead(*panel, "release_date"));
  CHECK_TRUE(HasCreditDataRead(*panel, "downloads"));
  bool downloads_format = false;
  const auto scan_formats = [&](const Node& node, const auto& self) -> void {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 2 &&
        node.kids[0].text == "sprintf" && node.kids[1].tag == rb_blitz::ui::dtb::kString &&
        node.kids[1].text.find("Downloads") != std::string::npos) {
      downloads_format = true;
    }
    for (const Node& kid : node.kids) self(kid, self);
  };
  scan_formats(*panel, scan_formats);
  CHECK_TRUE(downloads_format);

  // Idempotent: a second pass adds nothing.
  why.clear();
  CHECK_TRUE(rb_blitz::store::ShowOfferCredits(&record, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(record) == once);

  // Compile and parse round trip, and the parsed record is still recognised as already written.
  const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(record);
  std::vector<Node> parsed;
  rb_blitz::ui::dtb::RootHeader header;
  why.clear();
  CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &parsed, &header, &why));
  CHECK_TRUE(!parsed.empty());
  const std::vector<uint8_t> reparsed_once = rb_blitz::ui::dtb::SerializeNodes(parsed);
  why.clear();
  CHECK_TRUE(rb_blitz::store::ShowOfferCredits(&parsed, &why));
  CHECK_TRUE(rb_blitz::ui::dtb::SerializeNodes(parsed) == reparsed_once);

  // A record whose `refresh_panel` does not drive one of the labels is refused, untouched: the
  // anchor is what keeps four object names out of a scene this edit was not measured against.
  std::vector<Node> other = {Node::Command(
      {Symbol("new"), Symbol("StoreInfoPanel"), Symbol("store_info_panel"), Text("elsewhere.milo"),
       Node::Array({Symbol("refresh_panel"),
                    Node::Command({Symbol("do"),
                                   Node::Command({Symbol("song.lbl"), Symbol("set_offer_name"),
                                                  Node::Var("offer")}),
                                   Node::Command({Symbol("drm.lbl"),
                                                  Symbol("set_offer_description"),
                                                  Node::Var("offer")})})})})};
  const std::vector<Node> untouched = other;
  why.clear();
  CHECK_FALSE(rb_blitz::store::ShowOfferCredits(&other, &why));
  CHECK_FALSE(why.empty());
  CHECK_TRUE(TreeUnchanged(other, untouched));

  // A record without the details panel is refused too.
  std::vector<Node> missing = {Node::Command({Symbol("new"), Symbol("UIScreen"),
                                              Symbol("store_info_screen")})};
  const std::vector<Node> missing_untouched = missing;
  why.clear();
  CHECK_FALSE(rb_blitz::store::ShowOfferCredits(&missing, &why));
  CHECK_TRUE(TreeUnchanged(missing, missing_untouched));
}

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "dump-menu") == 0) {
    return DumpMenu(argv[2]);
  }
  if (argc >= 3 && std::strcmp(argv[1], "dump-strings") == 0) {
    return DumpStrings(argv[2]);
  }
  if (argc >= 4 && std::strcmp(argv[1], "store-edit") == 0) {
    return StoreEdit(argv[2], argv[3]);
  }  TestRewriteAddsLocalServer();
  TestRewriteReplacesExistingLocal();
  TestRefusalsLeaveTheTreeAlone();
  TestRewrittenRecordSurvivesCompileAndParse();
  TestUiInitGetsTheRewrittenDefinition();
  TestUiInitRefusals();
  TestUiInitSurvivesCompileAndParse();
  TestStoreEntryBypass();
  TestStoreEntryBypassRefusal();
  TestStoreEntryBypassSurvivesCompileAndParse();
  TestSearchRowRoutesToKeyboard();
  TestSearchKeyboardScreenMethods();
  TestSearchKeyboardSurvivesCompileAndParse();
  TestSearchRowGateSurvivesCompileAndParse();
  TestStorePriceFooter();
  TestStoreInfoPriceChrome();
  TestStoreHeading();
  TestOfferDetailsGuard();
  TestEveryUnavailableGuard();
  TestOfferDetailsDirect();
  TestPreviewButtonHidden();
  TestDownloadedRowGate();
  TestDownloadButtonAdded();
  TestDownloadButtonsRouted();
  TestDownloadPopup();
  TestDownloadRefreshAction();
  TestDifficultyLoopUnrolled();
  TestAlbumArtLinked();
  TestStoreStartRoutesToFilter();
  TestStoreFooterStart();
  TestStoreFooterOptionCleared();
  TestDetailsFooterOptionCleared();
  TestOfferCredits();
  return rb_blitz::test::Finish();
}
