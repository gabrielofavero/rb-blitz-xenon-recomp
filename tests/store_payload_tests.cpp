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

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "dump-menu") == 0) {
    return DumpMenu(argv[2]);
  }
  if (argc >= 3 && std::strcmp(argv[1], "dump-strings") == 0) {
    return DumpStrings(argv[2]);
  }
  TestRewriteAddsLocalServer();
  TestRewriteReplacesExistingLocal();
  TestRefusalsLeaveTheTreeAlone();
  TestRewrittenRecordSurvivesCompileAndParse();
  return rb_blitz::test::Finish();
}
