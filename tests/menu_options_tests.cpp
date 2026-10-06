// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the menu filter's edit (src/ui/menu_options.{h,cpp}), against a
// .dtb built here rather than taken from the game: what has to be right is the
// shape of the rewrite, and the shape is decidable without a boot. No retail
// content is used or needed (customization-plan.md D14).
//
// What is pinned is the contract the read hook relies on. The file's length is
// the ark index's, so an edit that changes it is not an edit but a damaged
// archive: the length is checked byte for byte, together with the bytes that
// precede the option list and whatever follows the file in the block it was read
// in. The rows leave the list the guest navigates. The bytes they take with them
// come back as extra characters in the list's own `#ifdef` macro name, which
// makes that macro undefined, and the title's loader *skips* everything up to
// the matching `#endif` when that happens - so the block between the two has to
// end up empty, or a row the player asked to keep would vanish with it.
//
// The cases are the ones a user's own row list can produce: the compiled
// default, a single row, a row inside the conditional's block, a row that is not
// there, an empty list, a name the file only uses as structure, and a list with
// no conditional to pay the bytes back with.

#include "check.h"

#include "ui/menu_options.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::menu_options;
using namespace rb_blitz::test;

// --- the fixture ----------------------------------------------------------
//
// A compiled DTA carrying the menu's option array, shaped like the one the title
// ships: a root array, the command that defines the panel, and the option list
// as a `{array (...)}` whose elements include an `#ifdef` block. The row names
// are the title's own, because they are what the toggle's default list matches.

constexpr uint32_t kInt = 0x00;
constexpr uint32_t kSymbol = 0x05;
constexpr uint32_t kIfdef = 0x07;
constexpr uint32_t kEndif = 0x09;
constexpr uint32_t kArray = 0x10;
constexpr uint32_t kCommand = 0x11;
constexpr uint32_t kSeed = 0x1234ABCDu;

void PushU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>(value >> 8));
}

void PushU32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

std::vector<uint8_t> Cat(std::initializer_list<std::vector<uint8_t>> parts) {
  std::vector<uint8_t> out;
  for (const std::vector<uint8_t>& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

std::vector<uint8_t> Name(uint32_t tag, const std::string& text) {
  std::vector<uint8_t> out;
  PushU32(&out, tag);
  PushU32(&out, static_cast<uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return out;
}

std::vector<uint8_t> Scalar(uint32_t tag, uint32_t value) {
  std::vector<uint8_t> out;
  PushU32(&out, tag);
  PushU32(&out, value);
  return out;
}

std::vector<uint8_t> Block(uint32_t tag, uint16_t line,
                           std::initializer_list<std::vector<uint8_t>> kids) {
  std::vector<uint8_t> out;
  PushU32(&out, tag);
  PushU16(&out, static_cast<uint16_t>(kids.size()));
  PushU16(&out, line);
  PushU16(&out, 0);
  for (const std::vector<uint8_t>& kid : kids) {
    out.insert(out.end(), kid.begin(), kid.end());
  }
  return out;
}

// The option array: the rows the menu opens and closes with, the achievement
// list behind an `#ifdef` block, and the rest. `second_ifdef` adds the second
// conditional the retail file carries around its exit row.
std::vector<uint8_t> OptionArray(bool second_ifdef) {
  std::vector<std::vector<uint8_t>> kids{
      Name(kSymbol, "splash_start"),
      Name(kSymbol, "splash_leaderboard"),
      Name(kIfdef, "HX_XBOX"),
      Name(kSymbol, "splash_achievements"),
      Scalar(kEndif, 0),
      Name(kSymbol, "splash_options"),
      Name(kSymbol, "splash_upgrade"),
      Name(kSymbol, "splash_dlc"),
  };
  if (second_ifdef) {
    kids.push_back(Name(kIfdef, "HX_XBOX"));
  }
  kids.push_back(Name(kSymbol, "splash_exit"));
  if (second_ifdef) {
    kids.push_back(Scalar(kEndif, 0));
  }
  std::vector<uint8_t> out;
  PushU32(&out, kArray);
  PushU16(&out, static_cast<uint16_t>(kids.size()));
  PushU16(&out, 44);
  PushU16(&out, 0);
  for (const std::vector<uint8_t>& kid : kids) {
    out.insert(out.end(), kid.begin(), kid.end());
  }
  return out;
}

std::vector<uint8_t> FixtureBody(bool second_ifdef) {
  const std::vector<uint8_t> rows = OptionArray(second_ifdef);
  const std::vector<uint8_t> assignment = Block(
      kArray, 42,
      {Name(kSymbol, "options_array"), Block(kCommand, 43, {Name(kSymbol, "array"), rows})});
  const std::vector<uint8_t> panel =
      Block(kCommand, 2,
            {Name(kSymbol, "new"), Name(kSymbol, "splash_panel"),
             Block(kArray, 18,
                   {Name(kSymbol, "check_trial"),
                    Block(kCommand, 19, {Name(kSymbol, "do"), assignment})})});
  const std::vector<uint8_t> body =
      Cat({std::vector<uint8_t>{0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00}, panel,
           Name(kSymbol, "game_exit_screen"),
           Block(kCommand, 3, {Name(kSymbol, "set"), Scalar(kInt, 1)}), Scalar(kEndif, 0)});
  return body;
}

std::vector<uint8_t> FixtureFile(bool second_ifdef) {
  const std::vector<uint8_t> body = FixtureBody(second_ifdef);
  std::vector<uint8_t> file(body.size() + 4);
  EncodeFile(kSeed, body.data(), body.size(), file.data());
  return file;
}

// --- reading the fixture back ---------------------------------------------
//
// The test parses the file itself, with its own reading of the grammar, so a
// mistake in the module's encoder cannot hide behind a matching mistake in the
// reader: what is asserted on is the option list the guest would navigate.

struct Node {
  uint32_t tag = 0;
  std::string text;
  uint32_t scalar = 0;
  std::vector<Node> kids;
};

bool ParseNode(const std::vector<uint8_t>& body, size_t* pos, Node* out) {
  auto take32 = [&](uint32_t* value) {
    if (*pos + 4 > body.size()) {
      return false;
    }
    *value = static_cast<uint32_t>(body[*pos]) | (static_cast<uint32_t>(body[*pos + 1]) << 8) |
             (static_cast<uint32_t>(body[*pos + 2]) << 16) |
             (static_cast<uint32_t>(body[*pos + 3]) << 24);
    *pos += 4;
    return true;
  };
  auto take16 = [&](uint16_t* value) {
    if (*pos + 2 > body.size()) {
      return false;
    }
    *value = static_cast<uint16_t>(body[*pos] | (body[*pos + 1] << 8));
    *pos += 2;
    return true;
  };
  if (!take32(&out->tag)) {
    return false;
  }
  if (out->tag == kArray || out->tag == kCommand || out->tag == 0x13) {
    uint16_t arity = 0;
    uint16_t line = 0;
    uint16_t deprecated = 0;
    if (!take16(&arity) || !take16(&line) || !take16(&deprecated)) {
      return false;
    }
    for (uint16_t i = 0; i < arity; ++i) {
      Node kid;
      if (!ParseNode(body, pos, &kid)) {
        return false;
      }
      out->kids.push_back(kid);
    }
    return true;
  }
  if (out->tag == kInt || out->tag == 0x06 || out->tag == kEndif) {
    return take32(&out->scalar);
  }
  uint32_t length = 0;
  if (!take32(&length) || *pos + length > body.size()) {
    return false;
  }
  out->text.assign(reinterpret_cast<const char*>(body.data() + *pos), length);
  *pos += length;
  return true;
}

std::vector<Node> ReadRoot(const std::vector<uint8_t>& file) {
  // The keystream is its own inverse, so encoding the stored body hands back the
  // decoded one.
  std::vector<uint8_t> scratch(file.size());
  EncodeFile(kSeed, file.data() + 4, file.size() - 4, scratch.data());
  const std::vector<uint8_t> body(scratch.begin() + 4, scratch.end());
  std::vector<Node> root;
  size_t pos = 7;  // past the root array's own header
  const uint16_t arity = static_cast<uint16_t>(body[1] | (body[2] << 8));
  for (uint16_t i = 0; i < arity; ++i) {
    Node node;
    if (!ParseNode(body, &pos, &node)) {
      return {};
    }
    root.push_back(node);
  }
  return root;
}

Node* FindOptionArray(Node* node) {
  if (node->tag == kArray) {
    bool first = false;
    bool last = false;
    for (const Node& kid : node->kids) {
      first = first || kid.text == "splash_start";
      last = last || kid.text == "splash_exit";
    }
    if (first && last) {
      return node;
    }
  }
  for (Node& kid : node->kids) {
    if (Node* found = FindOptionArray(&kid)) {
      return found;
    }
  }
  return nullptr;
}

Node* OptionArrayOf(std::vector<Node>* root) {
  for (Node& node : *root) {
    if (Node* found = FindOptionArray(&node)) {
      return found;
    }
  }
  return nullptr;
}

// The list's rows, in the order the menu would draw them: the elements that are
// not structure.
std::vector<std::string> Rows(const Node& array) {
  std::vector<std::string> rows;
  for (const Node& kid : array.kids) {
    if (kid.tag == kIfdef || kid.tag == kEndif || kid.tag == 0x08) {
      continue;
    }
    rows.push_back(kid.text);
  }
  return rows;
}

// How many elements sit between the array's first `#ifdef` and its `#endif`.
size_t BlockSize(const Node& array) {
  size_t directive = array.kids.size();
  for (size_t i = 0; i < array.kids.size(); ++i) {
    if (array.kids[i].tag == kIfdef) {
      directive = i;
      break;
    }
  }
  size_t endif = array.kids.size();
  for (size_t i = directive + 1; i < array.kids.size(); ++i) {
    if (array.kids[i].tag == kEndif) {
      endif = i;
      break;
    }
  }
  return endif - directive - 1;
}

std::string FirstIfdefName(const Node& array) {
  for (const Node& kid : array.kids) {
    if (kid.tag == kIfdef) {
      return kid.text;
    }
  }
  return {};
}

size_t CountTag(const Node& node, uint32_t tag) {
  size_t count = node.tag == tag ? 1 : 0;
  for (const Node& kid : node.kids) {
    count += CountTag(kid, tag);
  }
  return count;
}

std::string Join(const std::vector<std::string>& rows) {
  std::string text;
  for (const std::string& row : rows) {
    if (!text.empty()) {
      text += ",";
    }
    text += row;
  }
  return text;
}

}  // namespace

int main() {
  BeginCase("the fixture is a .dtb this module accepts");
  {
    const std::vector<uint8_t> file = FixtureFile(true);
    CHECK_TRUE(LooksLikeDtb(file.data(), file.size()));
    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    CHECK_TRUE(Join(Rows(*array)) ==
               "splash_start,splash_leaderboard,splash_achievements,splash_options,"
               "splash_upgrade,splash_dlc,splash_exit");
    CHECK_EQ(CountTag(root[0], kIfdef), 2);
    CHECK_EQ(CountTag(root[0], kEndif), 2);
  }

  BeginCase("the default rows are hidden, and the file keeps its length");
  {
    const std::vector<uint8_t> before = FixtureFile(true);
    std::vector<uint8_t> file = before;
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames(kDefaultRows));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.file_size, before.size());
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) != 0);

    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    // The three dead rows are gone and everything else is still there, in order.
    CHECK_TRUE(Join(Rows(*array)) ==
               "splash_start,splash_options,splash_upgrade,splash_exit");
    // The conditional that paid for them is unbalanced no more: it has the same
    // endif, its block is empty, and its macro name is the freed bytes longer.
    CHECK_EQ(CountTag(root[0], kIfdef), 2);
    CHECK_EQ(CountTag(root[0], kEndif), 2);
    CHECK_EQ(BlockSize(*array), 0);
    CHECK_EQ(FirstIfdefName(*array).size(), std::strlen("HX_XBOX") + outcome.freed);
    CHECK_EQ(FirstIfdefName(*array).compare(0, 7, "HX_XBOX"), 0);
    CHECK_EQ(FirstIfdefName(*array).find_first_not_of('z', 7), std::string::npos);
    // The second conditional is untouched: still 7 bytes, still the same macro.
    size_t ifdefs = 0;
    for (const Node& kid : array->kids) {
      if (kid.tag == kIfdef && ++ifdefs == 2) {
        CHECK_TRUE(kid.text == "HX_XBOX");
      }
    }
    // No name the file carries outside the row list was disturbed.
    const Outcome again = HideRows(file.data(), file.size(), ParseRowNames(kDefaultRows));
    CHECK_FALSE(again.applied);
    CHECK_TRUE(again.reason.find("none of the named rows") != std::string::npos);
  }

  BeginCase("a row inside the conditional's block leaves with it");
  {
    // The achievement list is the block's own content, so removing it empties
    // the block exactly rather than needing the block moved out of the way.
    std::vector<uint8_t> file = FixtureFile(true);
    const Outcome outcome = HideRows(file.data(), file.size(),
                                     ParseRowNames("splash_achievements"));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.removed.size(), 1);
    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    CHECK_TRUE(Join(Rows(*array)) == "splash_start,splash_leaderboard,splash_options,"
                                    "splash_upgrade,splash_dlc,splash_exit");
    CHECK_EQ(BlockSize(*array), 0);
  }

  BeginCase("a row outside the block takes the block's content with it unless it is moved out");
  {
    // Hiding one row makes the conditional's macro undefined, which would drop
    // whatever is left inside the block: the achievement row has to be moved in
    // front of the `#ifdef` so the menu keeps it, and the rows' own order must
    // not change.
    std::vector<uint8_t> file = FixtureFile(true);
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames("splash_dlc"));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.removed.size(), 1);
    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    CHECK_TRUE(Join(Rows(*array)) ==
               "splash_start,splash_leaderboard,splash_achievements,splash_options,"
               "splash_upgrade,splash_exit");
    CHECK_EQ(BlockSize(*array), 0);
    CHECK_EQ(CountTag(root[0], kIfdef), 2);
    CHECK_EQ(CountTag(root[0], kEndif), 2);
    CHECK_EQ(FirstIfdefName(*array).size(), std::strlen("HX_XBOX") + outcome.freed);
  }

  BeginCase("a row that is not there, and an empty list");
  {
    std::vector<uint8_t> file = FixtureFile(true);
    const Outcome missing = HideRows(file.data(), file.size(), ParseRowNames("splash_none"));
    CHECK_FALSE(missing.applied);
    CHECK_TRUE(missing.reason.find("none of the named rows") != std::string::npos);
    const Outcome empty = HideRows(file.data(), file.size(), {});
    CHECK_FALSE(empty.applied);
    CHECK_TRUE(empty.reason.find("no rows were named") != std::string::npos);
  }

  BeginCase("a name the file only uses as structure is never removed");
  {
    // The `#ifdef` nodes are not rows, and removing one would leave the file's
    // conditionals unbalanced - which the title's loader does not survive.
    std::vector<uint8_t> file = FixtureFile(true);
    const std::vector<uint8_t> before = file;
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames("HX_XBOX"));
    CHECK_FALSE(outcome.applied);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the bytes after the file are left alone");
  {
    // The read hook hands over a whole 64 KiB ark block, so the file it patches
    // usually has other entries behind it: only the file itself may change.
    const std::vector<uint8_t> before = FixtureFile(true);
    std::vector<uint8_t> block(before.size() + 4096, 0xA5);
    std::memcpy(block.data(), before.data(), before.size());
    const Outcome outcome = HideRows(block.data(), block.size(), ParseRowNames(kDefaultRows));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.file_size, before.size());
    bool tail_intact = true;
    for (size_t i = before.size(); i < block.size(); ++i) {
      tail_intact = tail_intact && block[i] == 0xA5;
    }
    CHECK_TRUE(tail_intact);
  }

  BeginCase("bytes that are not a .dtb are refused");
  {
    std::vector<uint8_t> junk(4096, 0x00);
    CHECK_FALSE(LooksLikeDtb(junk.data(), junk.size()));
    CHECK_FALSE(LooksLikeDtb(nullptr, 0));
    const std::vector<uint8_t> file = FixtureFile(true);
    CHECK_TRUE(LooksLikeDtb(file.data(), file.size()));
    const Outcome outcome = HideRows(junk.data(), junk.size(), ParseRowNames(kDefaultRows));
    CHECK_FALSE(outcome.applied);
    // A truncated file is not guessed at either.
    std::vector<uint8_t> short_file(file.begin(), file.begin() + 40);
    const Outcome truncated =
        HideRows(short_file.data(), short_file.size(), ParseRowNames(kDefaultRows));
    CHECK_FALSE(truncated.applied);
  }

  BeginCase("a list with no conditional to pay the bytes back with is refused");
  {
    const std::vector<uint8_t> rows =
        Block(kArray, 5, {Name(kSymbol, "splash_start"), Name(kSymbol, "splash_leaderboard"),
                          Name(kSymbol, "splash_exit")});
    const std::vector<uint8_t> body =
        Cat({std::vector<uint8_t>{0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00},
             Block(kCommand, 2, {Name(kSymbol, "new"), rows})});
    std::vector<uint8_t> file(body.size() + 4);
    EncodeFile(0x0BADF00Du, body.data(), body.size(), file.data());
    const std::vector<uint8_t> before = file;
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames("splash_leaderboard"));
    CHECK_FALSE(outcome.applied);
    CHECK_TRUE(outcome.reason.find("no conditional") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the title's own digest");
  {
    // The standard vectors: the empty string, "abc", and the 56-byte sentence
    // that exercises a message split across two blocks.
    const uint8_t empty[1] = {0};
    const uint8_t abc[3] = {'a', 'b', 'c'};
    const char* sentence = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const uint8_t expected_empty[20] = {0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                        0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
    const uint8_t expected_abc[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                      0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    const uint8_t expected_sentence[20] = {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
                                           0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1};
    uint8_t digest[20];
    Sha1Digest(empty, 0, digest);
    CHECK_MEM_EQ(digest, expected_empty, 20);
    Sha1Digest(abc, sizeof(abc), digest);
    CHECK_MEM_EQ(digest, expected_abc, 20);
    Sha1Digest(reinterpret_cast<const uint8_t*>(sentence), std::strlen(sentence), digest);
    CHECK_MEM_EQ(digest, expected_sentence, 20);
  }

  BeginCase("the row list grammar");
  {
    const std::vector<std::string> names =
        ParseRowNames(" splash_start,splash_exit , ,splash_dlc");
    CHECK_EQ(names.size(), 3);
    CHECK_TRUE(names[0] == "splash_start");
    CHECK_TRUE(names[1] == "splash_exit");
    CHECK_TRUE(names[2] == "splash_dlc");
    CHECK_EQ(ParseRowNames("").size(), 0);
    CHECK_EQ(ParseRowNames(" , , ").size(), 0);
    CHECK_EQ(ParseRowNames("one").size(), 1);
  }

  return Finish();
}
