// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/ui/menu_options.h for what this is and why the edit is shaped this
// way. The evidence for the grammar, the conditional behaviour and the edits
// the title accepts is in docs/engine/main-menu-flow.md.

#include "ui/menu_options.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace rb_blitz::menu_options {
namespace {

// --- the file's own obfuscation -------------------------------------------

// Park-Miller with Schrage's division, spelled the way the title spells it
// (scripts/hmx_ark.py, docs/assets.md "dtb").
constexpr int32_t kLcgMod = 0x7FFFFFFF;
constexpr int32_t kLcgDiv = 0x1F31D;  // 2147483647 / 16807
constexpr int32_t kLcgMul = 0x41A7;   // 16807
constexpr int32_t kLcgRem = 0xB14;    // 2836

int32_t CryptRound(int32_t key) {
  const int32_t quotient = key / kLcgDiv;
  const int64_t wide = static_cast<int64_t>(key - quotient * kLcgDiv) * kLcgMul -
                       static_cast<int64_t>(quotient) * kLcgRem;
  int32_t ret = static_cast<int32_t>(static_cast<uint32_t>(wide));
  if (ret <= 0) {
    ret += kLcgMod;
  }
  return ret;
}

uint32_t LoadU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t LoadU16(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                               (static_cast<uint16_t>(p[1]) << 8));
}

void StoreU16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>(value >> 8);
}

void StoreU32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

// The stream a .dtb body is xored with: the low byte of each key, advanced
// before the next byte is taken.
struct Keystream {
  int32_t key = 0;
  uint8_t Next() {
    const uint8_t byte = static_cast<uint8_t>(key & 0xFF);
    key = CryptRound(key);
    return byte;
  }
};

Keystream StreamFor(uint32_t seed) {
  Keystream stream;
  stream.key = CryptRound(static_cast<int32_t>(seed));
  return stream;
}

// --- the node grammar ------------------------------------------------------

// The tag field is four bytes wide with a zero high half in every file this
// title ships, and the payload that follows it is what tells the kinds apart.
enum NodeKind {
  kScalarPayload,
  kTextPayload,
  kBlockPayload,
};

bool KindOf(uint32_t tag, NodeKind* kind) {
  switch (tag) {
    case 0x00:  // int
    case 0x01:  // float
    case 0x06:  // unhandled
    case 0x08:  // else
    case 0x09:  // endif
    case 0x24:  // autorun
      *kind = kScalarPayload;
      return true;
    case 0x02:  // var ($name)
    case 0x03:  // func
    case 0x04:  // object
    case 0x05:  // symbol
    case 0x07:  // ifdef
    case 0x12:  // string
    case 0x20:  // define
    case 0x21:  // include
    case 0x22:  // merge
    case 0x23:  // ifndef
    case 0x25:  // undef
      *kind = kTextPayload;
      return true;
    case 0x10:  // array ( ... )
    case 0x11:  // command { ... }
    case 0x13:  // property [ ... ]
      *kind = kBlockPayload;
      return true;
    default:
      return false;
  }
}

// A row of the option list is a name, and the three kinds that can carry one.
bool IsRowName(uint32_t tag) { return tag == 0x02 || tag == 0x05 || tag == 0x12; }

constexpr uint32_t kIfdef = 0x07;
constexpr uint32_t kIfndef = 0x23;
constexpr uint32_t kEndif = 0x09;
constexpr uint32_t kArray = 0x10;

struct Node {
  uint32_t tag = 0;
  uint32_t scalar = 0;
  uint16_t line = 0;
  uint16_t deprecated = 0;
  std::vector<uint8_t> text;  // text payloads
  std::vector<Node> kids;     // block payloads
};

// The three first names the menu's option array always lists; an array that has
// both is the menu's, and no other array in the game does.
constexpr char kMenuFirstRow[] = "splash_start";
constexpr char kMenuLastRow[] = "splash_exit";

struct Reader {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t pos = 0;
  bool ok = true;

  bool Take(size_t count) {
    if (!ok || count > size - pos) {
      ok = false;
      return false;
    }
    pos += count;
    return true;
  }
  uint8_t U8() {
    if (!Take(1)) return 0;
    return data[pos - 1];
  }
  uint16_t U16() {
    if (!Take(2)) return 0;
    return LoadU16(data + pos - 2);
  }
  uint32_t U32() {
    if (!Take(4)) return 0;
    return LoadU32(data + pos - 4);
  }
  std::vector<uint8_t> Bytes(size_t count) {
    if (!Take(count)) return {};
    return std::vector<uint8_t>(data + pos - count, data + pos);
  }
};

bool ParseNode(Reader& reader, Node& out, int depth) {
  if (depth > 32) {
    return false;
  }
  const uint32_t tag = reader.U32();
  if (!reader.ok) {
    return false;
  }
  NodeKind kind = kScalarPayload;
  if (!KindOf(tag, &kind)) {
    return false;
  }
  out.tag = tag;
  switch (kind) {
    case kScalarPayload:
      out.scalar = reader.U32();
      break;
    case kTextPayload: {
      const uint32_t length = reader.U32();
      if (!reader.ok || length > reader.size - reader.pos) {
        return false;
      }
      out.text = reader.Bytes(length);
      break;
    }
    case kBlockPayload: {
      const uint16_t arity = reader.U16();
      out.line = reader.U16();
      out.deprecated = reader.U16();
      if (!reader.ok || arity > reader.size - reader.pos) {
        return false;
      }
      out.kids.reserve(arity);
      for (uint16_t i = 0; i < arity; ++i) {
        Node kid;
        if (!ParseNode(reader, kid, depth + 1)) {
          return false;
        }
        out.kids.push_back(std::move(kid));
      }
      break;
    }
  }
  return reader.ok;
}

void SerializeNode(const Node& node, std::vector<uint8_t>* out) {
  uint8_t header[4];
  StoreU32(header, node.tag);
  out->insert(out->end(), header, header + 4);
  switch (node.tag) {
    case 0x00:
    case 0x01:
    case 0x06:
    case 0x08:
    case 0x09:
    case 0x24:
      StoreU32(header, node.scalar);
      out->insert(out->end(), header, header + 4);
      return;
    case 0x10:
    case 0x11:
    case 0x13: {
      uint8_t block[6];
      StoreU16(block, static_cast<uint16_t>(node.kids.size()));
      StoreU16(block + 2, node.line);
      StoreU16(block + 4, node.deprecated);
      out->insert(out->end(), block, block + 6);
      for (const Node& kid : node.kids) {
        SerializeNode(kid, out);
      }
      return;
    }
    default: {
      StoreU32(header, static_cast<uint32_t>(node.text.size()));
      out->insert(out->end(), header, header + 4);
      out->insert(out->end(), node.text.begin(), node.text.end());
      return;
    }
  }
}

size_t NodeSize(const Node& node) {
  std::vector<uint8_t> scratch;
  SerializeNode(node, &scratch);
  return scratch.size();
}

bool IsText(const Node& node, const char* text) {
  const size_t length = std::strlen(text);
  return node.text.size() == length &&
         std::memcmp(node.text.data(), text, length) == 0;
}

// The menu's option array, found by the two rows that open and close it.
Node* FindOptionArray(Node& node) {
  if (node.tag == kArray) {
    bool first = false;
    bool last = false;
    for (const Node& kid : node.kids) {
      first = first || IsText(kid, kMenuFirstRow);
      last = last || IsText(kid, kMenuLastRow);
    }
    if (first && last) {
      return &node;
    }
  }
  for (Node& kid : node.kids) {
    if (Node* found = FindOptionArray(kid)) {
      return found;
    }
  }
  return nullptr;
}

// The `#endif` that closes the `#ifdef` at `index`, or `kids.size()` when the
// condition is not closed inside this array. Nested conditionals are counted,
// because a directive pair inside the block is not this block's end.
size_t MatchEndif(const std::vector<Node>& kids, size_t index) {
  int depth = 0;
  for (size_t i = index; i < kids.size(); ++i) {
    if (kids[i].tag == kIfdef || kids[i].tag == kIfndef) {
      ++depth;
    } else if (kids[i].tag == kEndif) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return kids.size();
}

// The largest root arity a compiled DTA in this title could plausibly carry,
// used to reject a byte pattern that only looks like a file header.
constexpr size_t kMaxNodes = 1u << 14;

}  // namespace

bool LooksLikeDtb(const uint8_t* file, size_t available) {
  // The seed, then the header every compiled DTA this title ships starts with:
  // "an array is present", a root arity in 1..kMaxNodes, and the array's line.
  if (file == nullptr || available < 7 + 4) {
    return false;
  }
  const uint32_t seed = LoadU32(file);
  Keystream stream = StreamFor(seed);
  // This test runs on every byte of every read the title makes, so the first
  // decoded byte - the one that rejects almost every candidate - is taken
  // before the rest, and the keystream is only advanced when it passes.
  uint8_t header[7];
  if ((header[0] = static_cast<uint8_t>(file[4] ^ stream.Next())) != 0x01) {
    return false;
  }
  for (size_t i = 1; i < sizeof(header); ++i) {
    header[i] = file[4 + i] ^ stream.Next();
  }
  const uint16_t arity = LoadU16(header + 1);
  const uint16_t deprecated = LoadU16(header + 5);
  return arity > 0 && arity <= kMaxNodes && deprecated == 0;
}

std::vector<std::string> ParseRowNames(std::string_view text) {
  std::vector<std::string> names;
  size_t at = 0;
  while (at <= text.size()) {
    const size_t comma = text.find(',', at);
    const size_t end = comma == std::string_view::npos ? text.size() : comma;
    size_t first = at;
    size_t last = end;
    while (first < last && (text[first] == ' ' || text[first] == '\t')) {
      ++first;
    }
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
      --last;
    }
    if (last > first) {
      names.emplace_back(text.substr(first, last - first));
    }
    if (comma == std::string_view::npos) {
      break;
    }
    at = comma + 1;
  }
  return names;
}

Outcome HideRows(uint8_t* file, size_t available,
                 const std::vector<std::string>& names) {
  Outcome outcome;
  if (file == nullptr || available < 11) {
    outcome.reason = "too short to be a .dtb";
    return outcome;
  }
  if (names.empty()) {
    outcome.reason = "no rows were named";
    return outcome;
  }

  const uint32_t seed = LoadU32(file);
  const size_t body_size = available - 4;

  // Decode the whole window and parse the root array. The file's own length
  // comes from the root's arity, so a .dtb that sits inside a larger read is
  // handled exactly; anything after the last node belongs to the next file.
  std::vector<uint8_t> body(body_size);
  {
    Keystream stream = StreamFor(seed);
    for (size_t i = 0; i < body_size; ++i) {
      body[i] = file[4 + i] ^ stream.Next();
    }
  }
  if (body[0] != 0x01) {
    outcome.reason = "not a compiled DTA array";
    return outcome;
  }
  Reader reader{body.data(), body.size(), 7};
  const uint16_t root_arity = LoadU16(body.data() + 1);
  if (root_arity == 0 || root_arity > kMaxNodes) {
    outcome.reason = "implausible root arity";
    return outcome;
  }
  std::vector<Node> root;
  root.reserve(root_arity);
  for (uint16_t i = 0; i < root_arity; ++i) {
    Node node;
    if (!ParseNode(reader, node, 0)) {
      outcome.reason = "the node stream does not parse";
      return outcome;
    }
    root.push_back(std::move(node));
  }
  const size_t body_used = reader.pos;
  outcome.file_size = body_used + 4;
  if (outcome.file_size > available) {
    outcome.reason = "the node stream runs past the bytes available";
    outcome.file_size = 0;
    return outcome;
  }
  // Only a file this module can reproduce byte for byte is safe to rewrite: it
  // proves the grammar above is the one the file was written with.
  {
    std::vector<uint8_t> check;
    check.reserve(body_used - 7);
    for (const Node& node : root) {
      SerializeNode(node, &check);
    }
    if (check.size() != body_used - 7 ||
        std::memcmp(check.data(), body.data() + 7, body_used - 7) != 0) {
      outcome.reason = "the node stream does not reserialize";
      return outcome;
    }
  }

  Node* array = nullptr;
  for (Node& node : root) {
    if ((array = FindOptionArray(node)) != nullptr) {
      break;
    }
  }
  if (array == nullptr) {
    outcome.reason = "no main-menu option list in this file";
    return outcome;
  }

  // Drop the named rows. Only name-carrying elements are candidates: the
  // `#ifdef`/`#else`/`#endif` nodes are structure, and removing one would leave
  // the file's conditionals unbalanced, which the title's loader does not
  // survive (a dangling `#endif` pops an empty stack).
  std::vector<Node> kids;
  kids.reserve(array->kids.size());
  for (Node& kid : array->kids) {
    const bool hide = IsRowName(kid.tag) &&
                      std::any_of(names.begin(), names.end(), [&](const std::string& name) {
                        return kid.text.size() == name.size() &&
                               std::memcmp(kid.text.data(), name.data(), name.size()) == 0;
                      });
    if (hide) {
      outcome.freed += NodeSize(kid);
      outcome.removed.push_back(std::string(kid.text.begin(), kid.text.end()));
      continue;
    }
    kids.push_back(std::move(kid));
  }
  if (outcome.removed.empty()) {
    outcome.reason = "none of the named rows are in the option list";
    return outcome;
  }

  // The array's own conditional pays for the removed bytes. Its macro name
  // grows until it matches no macro at all, which makes the title's loader skip
  // what the conditional guards - so that block has to be empty first, or the
  // skimming would take a row the player asked to keep. Moving the block's
  // elements in front of the `#ifdef` is free: directives are not rows, so the
  // menu's own order does not change.
  size_t directive = kids.size();
  for (size_t i = 0; i < kids.size(); ++i) {
    if (kids[i].tag == kIfdef) {
      directive = i;
      break;
    }
  }
  if (directive == kids.size()) {
    outcome.reason = "the option list carries no conditional to take the freed bytes";
    outcome.removed.clear();
    outcome.freed = 0;
    return outcome;
  }
  const size_t endif = MatchEndif(kids, directive);
  if (endif == kids.size()) {
    outcome.reason = "the option list's conditional is not closed in the array";
    outcome.removed.clear();
    outcome.freed = 0;
    return outcome;
  }
  if (endif > directive + 1) {
    const size_t block_size = endif - directive - 1;
    std::vector<Node> block;
    block.reserve(block_size);
    for (size_t i = directive + 1; i < endif; ++i) {
      block.push_back(std::move(kids[i]));
    }
    kids.erase(kids.begin() + directive + 1, kids.begin() + endif);
    kids.insert(kids.begin() + directive, std::make_move_iterator(block.begin()),
                std::make_move_iterator(block.end()));
    directive += block_size;  // the conditional moved behind its own block
  }

  // Grow the macro name by exactly the bytes the rows took with them.
  kids[directive].text.insert(kids[directive].text.end(), outcome.freed, uint8_t{'z'});

  array->kids = std::move(kids);

  // Rebuild the file. The body must come out exactly as long as it went in: the
  // ark index has no room for a different size, and a patch that cannot balance
  // is not written at all. What is rebuilt is the node stream, which starts
  // after the seven-byte root header.
  const size_t node_bytes = body_used - 7;
  std::vector<uint8_t> rebuilt;
  rebuilt.reserve(node_bytes);
  for (const Node& node : root) {
    SerializeNode(node, &rebuilt);
  }
  if (rebuilt.size() != node_bytes) {
    outcome.applied = false;
    outcome.reason = "the edit does not preserve the file's length";
    outcome.removed.clear();
    outcome.freed = 0;
    return outcome;
  }

  // Write it back through the file's own seed, so nothing but the edited nodes
  // changes, then prove the caller's buffer now holds that file.
  Sha1Digest(file, outcome.file_size, outcome.digest_before);
  {
    Keystream stream = StreamFor(seed);
    for (size_t i = 0; i < 7; ++i) {  // the root header, which is not touched
      stream.Next();
    }
    for (size_t i = 0; i < rebuilt.size(); ++i) {
      file[4 + 7 + i] = rebuilt[i] ^ stream.Next();
    }
  }
  Sha1Digest(file, outcome.file_size, outcome.digest_after);
  outcome.applied = true;
  return outcome;
}

// --- SHA-1 ----------------------------------------------------------------

namespace {

struct Sha1 {
  uint32_t state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  uint64_t length = 0;
  uint8_t block[64] = {};
  size_t filled = 0;

  static uint32_t Rol(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32 - bits));
  }

  void AbsorbBlock() {
    uint32_t w[80];
    for (size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
             (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (size_t i = 16; i < 80; ++i) {
      w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (size_t i = 0; i < 80; ++i) {
      uint32_t f = 0;
      uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t temp = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
  }

  void Absorb(const uint8_t* data, size_t size) {
    length += size;
    while (size > 0) {
      const size_t take = std::min(size, sizeof(block) - filled);
      std::memcpy(block + filled, data, take);
      filled += take;
      data += take;
      size -= take;
      if (filled == sizeof(block)) {
        AbsorbBlock();
        filled = 0;
      }
    }
  }

  void Finish(uint8_t out[20]) {
    const uint64_t bits = length * 8;
    const uint8_t one = 0x80;
    Absorb(&one, 1);
    const uint8_t zero = 0;
    while (filled != 56) {
      Absorb(&zero, 1);
    }
    uint8_t tail[8];
    for (size_t i = 0; i < 8; ++i) {
      tail[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    }
    Absorb(tail, sizeof(tail));
    for (size_t i = 0; i < 5; ++i) {
      out[i * 4 + 0] = static_cast<uint8_t>(state[i] >> 24);
      out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
      out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
      out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
  }
};

}  // namespace

void Sha1Digest(const uint8_t* data, size_t size, uint8_t out[20]) {
  Sha1 sha;
  sha.Absorb(data, size);
  sha.Finish(out);
}

void EncodeFile(uint32_t seed, const uint8_t* body, size_t size, uint8_t* out) {
  out[0] = static_cast<uint8_t>(seed & 0xFF);
  out[1] = static_cast<uint8_t>((seed >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((seed >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((seed >> 24) & 0xFF);
  Keystream stream = StreamFor(seed);
  for (size_t i = 0; i < size; ++i) {
    out[4 + i] = body[i] ^ stream.Next();
  }
}

}  // namespace rb_blitz::menu_options
