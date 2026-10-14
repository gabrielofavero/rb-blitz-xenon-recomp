// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/ui/dtb_author.{h,cpp} - producing a compiled-DTA definition of our own
// (P1 of docs/settings/plan.md).
//
// Two things are pinned here:
//   * the grammar is self-consistent - what Compile writes, Parse reads back as the same tree, and
//     re-Compiling that tree gives byte-identical bytes (so a definition we author is stable);
//   * the grammar is the *title's* - a real file from the local game data, given as argv[1], must
//     parse and re-compile byte for byte. That check needs content, so it is skipped when no path
//     is given (the CMake test runs without one; a maintainer runs it against a retail file).
//
// The synthetic fixture deliberately mirrors the shape measured in a retail panel
// (ui/pause/gen/pause.dtb): a `{new UIPanel <name> {file <milo>} {focus <list>.lst} {enter {...}}}`
// declaration, which is what an authored screen definition has to look like. See
// docs/settings/architecture.md (part 2, the panel/list API).
//
// SDK-free: no rex::, no window, no boot.

#include "check.h"

#include "ui/dtb_author.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::ui;
using namespace rb_blitz::test;

// One panel declaration, in the shape the retail pause panel uses:
//   {new UIPanel advanced_settings_panel
//      {file pause.milo}
//      {focus settings.lst}
//      {enter {do {settings.lst set_data (row_bool row_select row_submenu row_compound)}}}}
std::vector<dtb::Node> PanelFixture() {
  const dtb::Node rows = dtb::Node::Array({
      dtb::Node::Symbol("row_bool"),
      dtb::Node::Symbol("row_select"),
      dtb::Node::Symbol("row_submenu"),
      dtb::Node::Symbol("row_compound"),
  });
  const dtb::Node set_data =
      dtb::Node::Command({dtb::Node::Symbol("settings.lst"), dtb::Node::Symbol("set_data"), rows});
  return {
      dtb::Node::Command({
          dtb::Node::Symbol("new"),
          dtb::Node::Symbol("UIPanel"),
          dtb::Node::Symbol("advanced_settings_panel"),
          dtb::Node::Array({dtb::Node::Symbol("file"), dtb::Node::String("pause.milo")}),
          dtb::Node::Array({dtb::Node::Symbol("focus"), dtb::Node::Symbol("settings.lst")}),
          dtb::Node::Array({dtb::Node::Symbol("enter"), dtb::Node::Command({dtb::Node::Symbol("do"), set_data})}),
      }),
      dtb::Node::Array({dtb::Node::Symbol("max_push_depth"), dtb::Node::Int(2)}),
  };
}

void TestCompileParseRoundTrip() {
  BeginCase("an authored definition compiles, parses back, and recompiles identically");

  const std::vector<dtb::Node> authored = PanelFixture();
  const std::vector<uint8_t> file = dtb::Compile(authored, dtb::RootHeader{0x1234, 3, 0});
  CHECK_TRUE(file.size() > 11);

  std::vector<dtb::Node> parsed;
  dtb::RootHeader header;
  std::string error;
  CHECK_TRUE(dtb::Parse(file.data(), file.size(), &parsed, &header, &error));
  CHECK_EQ(header.seed, 0x1234);
  CHECK_EQ(header.line, 3);
  CHECK_EQ(parsed.size(), authored.size());

  // The structure survived: root command, its tag and the child count that carries the payload.
  CHECK_EQ(parsed[0].tag, dtb::kCommand);
  CHECK_EQ(parsed[0].kids.size(), authored[0].kids.size());
  CHECK_TRUE(parsed[1].tag == dtb::kArray);
  CHECK_EQ(parsed[1].kids[1].scalar, 2);

  // Recompiling what we parsed reproduces the file exactly - the property that makes an authored
  // definition safe to serve twice and compare.
  const std::vector<uint8_t> again = dtb::Compile(parsed, header);
  CHECK_EQ(again.size(), file.size());
  CHECK_MEM_EQ(again.data(), file.data(), file.size());

  // The seed is a real parameter: a different one changes the bytes but not the tree.
  const std::vector<uint8_t> other = dtb::Compile(authored, dtb::RootHeader{0x99, 3, 0});
  CHECK_EQ(other.size(), file.size());
  CHECK_FALSE(std::memcmp(other.data(), file.data(), file.size()) == 0);
  std::vector<dtb::Node> reparsed;
  CHECK_TRUE(dtb::Parse(other.data(), other.size(), &reparsed, &header, &error));
  CHECK_EQ(reparsed.size(), authored.size());
  CHECK_TRUE(reparsed[0].kids[2].text == "advanced_settings_panel");
}

void TestRejections() {
  BeginCase("a file that is not this grammar is refused, never half-read");

  const std::vector<uint8_t> file = dtb::Compile(PanelFixture());
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;

  // Short, empty and null inputs.
  CHECK_FALSE(dtb::Parse(nullptr, 0, &nodes, &header, &error));
  CHECK_FALSE(dtb::Parse(file.data(), 5, &nodes, &header, &error));

  // A tampered body: one byte inside the node stream no longer reserializes.
  std::vector<uint8_t> tampered = file;
  tampered[tampered.size() / 2] ^= 0x5A;
  CHECK_FALSE(dtb::Parse(tampered.data(), tampered.size(), &nodes, &header, &error));
  CHECK_FALSE(error.empty());

  // A body whose first byte is not the "an array is present" marker is refused before anything is
  // interpreted. Building it by compiling an empty root and then flipping that byte is enough,
  // because the marker is part of the encrypted body and the seed round-trips.
  const std::vector<uint8_t> empty = dtb::Compile({}, dtb::RootHeader{0, 1, 0});
  CHECK_EQ(empty.size(), 11u);  // seed + header, no nodes
  CHECK_FALSE(dtb::Parse(empty.data(), empty.size(), &nodes, &header, &error));

  // A truncated node stream: the file claims two roots but the second is cut off.
  CHECK_FALSE(dtb::Parse(file.data(), file.size() - 6, &nodes, &header, &error));
}

void TestKindTable() {
  BeginCase("the tag table is the grammar's, and an unknown tag is not a node");

  dtb::Kind kind = dtb::Kind::kScalar;
  CHECK_TRUE(dtb::KindOfTag(dtb::kInt, &kind));
  CHECK_TRUE(kind == dtb::Kind::kScalar);
  CHECK_TRUE(dtb::KindOfTag(dtb::kSymbol, &kind));
  CHECK_TRUE(kind == dtb::Kind::kText);
  CHECK_TRUE(dtb::KindOfTag(dtb::kCommand, &kind));
  CHECK_TRUE(kind == dtb::Kind::kBlock);
  CHECK_TRUE(dtb::KindOfTag(dtb::kProperty, &kind));
  CHECK_TRUE(kind == dtb::Kind::kBlock);
  CHECK_TRUE(dtb::KindOfTag(0x07, &kind));  // ifdef
  CHECK_TRUE(kind == dtb::Kind::kText);
  CHECK_TRUE(dtb::KindOfTag(0x09, &kind));  // endif
  CHECK_TRUE(kind == dtb::Kind::kScalar);
  // 0x0A is not in the grammar; a node carrying it is not one this title wrote.
  CHECK_FALSE(dtb::KindOfTag(0x0A, &kind));
  CHECK_FALSE(dtb::KindOfTag(0xFFFFFFFF, &kind));
}

// The real-file check: only runs when a path is given, and only ever *reads* it.
int TestRealFile(const char* path) {
  BeginCase("a real compiled DTA parses and recompiles byte for byte");
  if (path == nullptr || path[0] == '\0') {
    std::printf("[ SKIP ] no file given; pass a .dtb path to run this case\n");
    return 0;
  }
  std::ifstream handle(path, std::ios::binary);
  if (!handle) {
    std::printf("[ SKIP ] cannot open %s\n", path);
    return 0;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(handle)),
                             std::istreambuf_iterator<char>());
  std::printf("         file: %s (%zu bytes)\n", path, bytes.size());

  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  CHECK_TRUE(dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error));
  if (!error.empty()) {
    std::printf("         parse refused: %s\n", error.c_str());
  }
  const std::vector<uint8_t> again = dtb::Compile(nodes, header);
  CHECK_EQ(again.size(), bytes.size());
  CHECK_MEM_EQ(again.data(), bytes.data(), bytes.size());
  return 0;
}

// Authoring helpers for the P1-b reachability probe (docs/settings/architecture.md (part 2, the panel/list API) §8).
// They turn the verified encoder into files a boot can be pointed at, which is what the round trip
// above cannot do on its own. They write only what the caller names, and only ever *read* an input.
int EmitPanel(const char* out_path) {
  const std::vector<uint8_t> file = dtb::Compile(PanelFixture(), dtb::RootHeader{0x52425A31, 1, 0});
  std::ofstream handle(out_path, std::ios::binary);
  if (!handle) {
    std::printf("cannot write %s\n", out_path);
    return 1;
  }
  handle.write(reinterpret_cast<const char*>(file.data()),
               static_cast<std::streamsize>(file.size()));
  std::printf("author-panel: %s (%zu bytes)\n", out_path, file.size());
  return 0;
}

// The smallest compiled file this grammar can hold: one root that defines a macro. It exists so a
// boot can say "an authored compiled DTA was loaded" without also depending on whether a panel's
// scene and list assets that do not exist yet are tolerated at `new` time.
int EmitMin(const char* out_path) {
  dtb::Node define;
  define.tag = 0x20;  // define
  define.text = "RBBZ_ADVANCED_SETTINGS";
  const std::vector<uint8_t> file = dtb::Compile({define}, dtb::RootHeader{0x52425A31, 1, 0});
  std::ofstream handle(out_path, std::ios::binary);
  if (!handle) {
    std::printf("cannot write %s\n", out_path);
    return 1;
  }
  handle.write(reinterpret_cast<const char*>(file.data()),
               static_cast<std::streamsize>(file.size()));
  std::printf("authored-min: %s (%zu bytes)\n", out_path, file.size());
  return 0;
}

int EmitInit(const char* in_path, const char* out_path, const char* include_name) {
  std::ifstream handle(in_path, std::ios::binary);
  if (!handle) {
    std::printf("cannot read %s\n", in_path);
    return 1;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(handle)),
                                   std::istreambuf_iterator<char>());
  std::vector<dtb::Node> nodes;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &nodes, &header, &error)) {
    std::printf("cannot parse %s: %s\n", in_path, error.c_str());
    return 1;
  }
  // An include is one text node; appended last it lands outside the file's own
  // `#ifdef UI_INIT_DTA` guard, which is fine - the guard only stops a second include.
  dtb::Node include;
  include.tag = 0x21;
  include.text = include_name;
  nodes.push_back(include);

  const std::vector<uint8_t> file = dtb::Compile(nodes, header);
  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::printf("cannot write %s\n", out_path);
    return 1;
  }
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  std::printf("emit-init: %s -> %s (%zu -> %zu bytes, %zu -> %zu roots)\n", in_path, out_path,
              bytes.size(), file.size(), nodes.size() - 1, nodes.size());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "emit-panel") == 0) return EmitPanel(argv[2]);
  if (argc >= 3 && std::strcmp(argv[1], "emit-min") == 0) return EmitMin(argv[2]);
  if (argc >= 5 && std::strcmp(argv[1], "emit-init") == 0) {
    return EmitInit(argv[2], argv[3], argv[4]);
  }
  TestKindTable();
  TestCompileParseRoundTrip();
  TestRejections();
  TestRealFile(argc > 1 ? argv[1] : nullptr);
  return Finish();
}
