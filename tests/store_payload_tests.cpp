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

#include "store/store_payload.h"
#include "ui/dtb_author.h"

#include <string>
#include <vector>

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

void TestRankLoopUnrolled() {
  BeginCase("the details panel reads the single-component rank keys");

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
  CHECK_TRUE(rb_blitz::store::UnrollOfferRanks(&record, &why));
  if (!why.empty()) Fail(__FILE__, __LINE__, why);
  const Node* method = FindNamedMethodNode(record[0], "store_details_panel", "refresh");
  CHECK_TRUE(method != nullptr);
  if (method == nullptr) return;
  // The `foreach` is gone; in its place one `{do ...}` with five blocks, each reading a single
  // `rank_<part>` key and passing the part itself to `rank_tier`/`sprintf`.
  const Node& ifelse = method->kids[2];
  const Node& then_branch = ifelse.kids[2];
  const Node& loop = then_branch.kids[1];
  CHECK_STR_EQ(loop.kids[0].text, "do");
  CHECK_EQ(loop.kids.size(), size_t{6});  // do + five blocks
  static const char* const kParts[] = {"guitar", "drum", "vocals", "bass", "keys"};
  for (size_t i = 0; i < 5; ++i) {
    const Node& one = loop.kids[i + 1];
    CHECK_STR_EQ(one.kids[0].text, "do");
    // (rank {offer get_data (rank_<part>)}) - a ONE-element path.
    const Node& path = one.kids[1].kids[1].kids[2];
    CHECK_EQ(path.kids.size(), size_t{1});
    CHECK_STR_EQ(path.kids[0].text, std::string("rank_") + kParts[i]);
    // (tier {song_mgr rank_tier rank <part>})
    CHECK_STR_EQ(one.kids[2].kids[1].kids[3].text, kParts[i]);
    // (idd {sprintf "diff_%s" <part>})
    CHECK_STR_EQ(one.kids[3].kids[1].kids[2].text, kParts[i]);
  }

  // No two-component `(rank <part>)` read is left anywhere in the method.
  const auto has_rank_pair = [](const Node& node, const auto& self) -> bool {
    if (node.tag == rb_blitz::ui::dtb::kCommand && node.kids.size() >= 3 &&
        node.kids[1].text == "get_data") {
      const Node& path = node.kids[2];
      if (path.tag == rb_blitz::ui::dtb::kArray && path.kids.size() == 2 &&
          path.kids[0].text == "rank") {
        return true;
      }
    }
    for (const Node& kid : node.kids) {
      if (self(kid, self)) return true;
    }
    return false;
  };
  CHECK_FALSE(has_rank_pair(*method, has_rank_pair));

  // Idempotent.
  why.clear();
  CHECK_TRUE(rb_blitz::store::UnrollOfferRanks(&record, &why));
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
  CHECK_FALSE(rb_blitz::store::UnrollOfferRanks(&other, &why));
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
  CHECK_FALSE(rb_blitz::store::UnrollOfferRanks(&not_ranks, &why));
  CHECK_TRUE(TreeUnchanged(not_ranks, not_ranks_untouched));
}

// `store-edit <file.dtb> store|info`: run the P3 store-record transforms against a real shipped
// record and print the refusal reason, plus the method the transform looked at. This is the
// measurement half of those edits - the payload applies them to whatever `ui/store/gen/*.dtb` the
// boot mounts, and a refusal there is a note in the log that names a reason but not the record's
// own shape.
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
  if (mode == "store") {
    ok = rb_blitz::store::UnrollOfferRanks(&roots, &why);
  } else {
    ok = rb_blitz::store::ShowOfferDetailsDirectly(&roots, &why);
    std::printf("store-edit: details route -> %s (%s)\n", ok ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    why.clear();
    const bool preview = rb_blitz::store::HidePreviewButton(&roots, &why);
    std::printf("store-edit: preview row -> %s (%s)\n", preview ? "edited" : "refused",
                why.empty() ? "no reason" : why.c_str());
    ok = ok && preview;
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

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "dump-menu") == 0) {
    return DumpMenu(argv[2]);
  }
  if (argc >= 3 && std::strcmp(argv[1], "dump-strings") == 0) {
    return DumpStrings(argv[2]);
  }
  if (argc >= 4 && std::strcmp(argv[1], "store-edit") == 0) {
    return StoreEdit(argv[2], argv[3]);
  }
  TestRewriteAddsLocalServer();
  TestRewriteReplacesExistingLocal();
  TestRefusalsLeaveTheTreeAlone();
  TestRewrittenRecordSurvivesCompileAndParse();
  TestUiInitGetsTheRewrittenDefinition();
  TestUiInitRefusals();
  TestUiInitSurvivesCompileAndParse();
  TestStoreEntryBypass();
  TestStoreEntryBypassRefusal();
  TestStoreEntryBypassSurvivesCompileAndParse();
  TestStorePriceFooter();
  TestStoreInfoPriceChrome();
  TestStoreHeading();
  TestOfferDetailsGuard();
  TestEveryUnavailableGuard();
  TestOfferDetailsDirect();
  TestPreviewButtonHidden();
  TestRankLoopUnrolled();
  return rb_blitz::test::Finish();
}
