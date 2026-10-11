// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Authoring side of the compiled-DTA (.dtb) grammar (P1 of
// docs/plans/in-game-settings-plan.md; the format is documented in src/ui/menu_options.h and
// docs/assets.md "dtb").
//
// Why this exists: src/ui/menu_options.cpp *edits* files the title already has - byte-neutrally,
// because the ark index fixes every entry's size. P1's screen step needs the other direction:
// **produce** a definition of our own ("Load one authored minimal definition", parent plan §5.2
// item 2). A definition that is authored has no size constraint at all, so this module simply
// builds well-formed bytes.
//
// It reuses menu_options::EncodeFile for the seed and the Park-Miller keystream, so there is one
// implementation of the cipher. The node grammar is the same table menu_options uses; it is
// reproduced here rather than shared because the two modules live in different lanes (this file is
// P1's, menu_options.* is P10's) and a lane must not have to edit another's file to be built. If
// the two ever drift, tests/dtb_author_tests.cpp is what fails first.
//
// SDK-free on purpose: this is pure bytes, so it is testable without a boot, and the same code can
// run in the game, in a test, or in a tool.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::ui::dtb {

// The three payload shapes a tag can have, which is what the grammar is really made of.
enum class Kind { kScalar, kText, kBlock };

// The tag table, exactly as menu_options.cpp spells it. False for a tag this title never writes.
bool KindOfTag(uint32_t tag, Kind* kind);

// Tag constants, named for what they mean in the source DTA.
inline constexpr uint32_t kInt = 0x00;
inline constexpr uint32_t kFloat = 0x01;
inline constexpr uint32_t kVar = 0x02;      // $name
inline constexpr uint32_t kFunc = 0x03;
inline constexpr uint32_t kObject = 0x04;
inline constexpr uint32_t kSymbol = 0x05;   // name
inline constexpr uint32_t kIfdef = 0x07;
inline constexpr uint32_t kEndif = 0x09;
inline constexpr uint32_t kArray = 0x10;    // ( ... )
inline constexpr uint32_t kCommand = 0x11;  // { ... }
inline constexpr uint32_t kString = 0x12;
inline constexpr uint32_t kProperty = 0x13; // [ ... ]
inline constexpr uint32_t kAutorun = 0x24;

// One node of the grammar. A text node carries `text`; a block node carries `kids`; a scalar node
// carries `scalar`. `line` is the source line the compiler recorded and is zero for anything this
// module authors, which is what the shipped files show for generated statements too.
struct Node {
  uint32_t tag = 0;
  uint32_t scalar = 0;
  uint16_t line = 0;
  uint16_t deprecated = 0;
  std::string text;
  std::vector<Node> kids;

  // Builders for the tags a definition actually uses, so callers do not spell tag numbers.
  static Node Int(uint32_t value);
  static Node Symbol(std::string_view name);
  static Node Var(std::string_view name);
  static Node String(std::string_view text);
  static Node Array(std::vector<Node> kids, uint16_t line = 0);
  static Node Command(std::vector<Node> kids, uint16_t line = 0);
  static Node Property(std::vector<Node> kids, uint16_t line = 0);
  static Node Ifdef(std::string_view macro);
  static Node Endif();
};

// The root header a compiled file carries, kept so a parse/recompile is byte-exact.
struct RootHeader {
  uint32_t seed = 0;
  uint16_t line = 1;
  uint16_t deprecated = 0;
};

// The node stream of a root array's children, without the seven-byte root header.
std::vector<uint8_t> SerializeNodes(const std::vector<Node>& nodes);

// A whole compiled file: `[u32 seed][0x01][u16 arity][u16 line][u16 dep]` then the node stream,
// encrypted with the seed's keystream (menu_options::EncodeFile).
std::vector<uint8_t> Compile(const std::vector<Node>& nodes, const RootHeader& header = {});

// Decodes and parses a file. Returns false and fills `reason` - never partially fills `out` - when
// the header is not a compiled array, the stream does not parse, or it does not reserialize to the
// bytes it came from. That last check is the same one menu_options::LoadFile makes: a file this
// module cannot reproduce is a file it does not understand.
bool Parse(const uint8_t* file, size_t available, std::vector<Node>* out, RootHeader* header,
           std::string* reason);

}  // namespace rb_blitz::ui::dtb
