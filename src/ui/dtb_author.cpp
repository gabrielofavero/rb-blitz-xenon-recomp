// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The authoring half of the compiled-DTA grammar behind src/ui/dtb_author.h. The tag table and the
// payload layouts mirror src/ui/menu_options.cpp exactly; the cipher is not duplicated - it is
// menu_options::EncodeFile, which is a declared API of that module. SDK-free.

#include "ui/dtb_author.h"

#include <cstring>

#include "ui/menu_options.h"

namespace rb_blitz::ui::dtb {

namespace {

// The reader walks the *decrypted* body, starting after the seven-byte root header.
class Reader {
 public:
  Reader(const uint8_t* data, size_t size, size_t pos) : data_(data), size_(size), pos_(pos) {}

  bool u16(uint16_t* out) {
    if (pos_ + 2 > size_) return false;
    *out = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
    pos_ += 2;
    return true;
  }
  bool u32(uint32_t* out) {
    if (pos_ + 4 > size_) return false;
    *out = static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
           (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
           (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return true;
  }
  bool Bytes(size_t count, std::string* out) {
    if (pos_ + count > size_) return false;
    out->assign(reinterpret_cast<const char*>(data_ + pos_), count);
    pos_ += count;
    return true;
  }
  size_t pos() const { return pos_; }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_;
};

uint32_t LoadU16(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}

uint32_t LoadU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void StoreU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>(value >> 8));
}

void StoreU32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

// The same cap menu_options uses: a root arity beyond this is a byte pattern that only looks like a
// header, not a file this title wrote.
constexpr size_t kMaxNodes = 1u << 14;

bool ParseNode(Reader* reader, Node* out, int depth) {
  if (depth > 64) {
    return false;  // the grammar is a tree; nothing in this title nests anywhere near this far
  }
  uint32_t tag = 0;
  if (!reader->u32(&tag)) return false;
  Kind kind;
  if (!KindOfTag(tag, &kind)) return false;
  out->tag = tag;
  out->scalar = 0;
  out->line = 0;
  out->deprecated = 0;
  out->text.clear();
  out->kids.clear();
  switch (kind) {
    case Kind::kScalar:
      return reader->u32(&out->scalar);
    case Kind::kText: {
      uint32_t length = 0;
      if (!reader->u32(&length)) return false;
      if (length > (1u << 20)) return false;
      return reader->Bytes(length, &out->text);
    }
    case Kind::kBlock: {
      uint16_t arity = 0;
      if (!reader->u16(&arity)) return false;
      if (!reader->u16(&out->line)) return false;
      if (!reader->u16(&out->deprecated)) return false;
      out->kids.reserve(arity);
      for (uint16_t i = 0; i < arity; ++i) {
        Node kid;
        if (!ParseNode(reader, &kid, depth + 1)) return false;
        out->kids.push_back(std::move(kid));
      }
      return true;
    }
  }
  return false;
}

void SerializeNode(const Node& node, std::vector<uint8_t>* out) {
  StoreU32(out, node.tag);
  Kind kind;
  if (!KindOfTag(node.tag, &kind)) {
    return;  // the builders cannot produce this; SerializeNodes checks first
  }
  switch (kind) {
    case Kind::kScalar:
      StoreU32(out, node.scalar);
      return;
    case Kind::kText:
      StoreU32(out, static_cast<uint32_t>(node.text.size()));
      out->insert(out->end(), node.text.begin(), node.text.end());
      return;
    case Kind::kBlock:
      StoreU16(out, static_cast<uint16_t>(node.kids.size()));
      StoreU16(out, node.line);
      StoreU16(out, node.deprecated);
      for (const Node& kid : node.kids) {
        SerializeNode(kid, out);
      }
      return;
  }
}

}  // namespace

bool KindOfTag(uint32_t tag, Kind* kind) {
  switch (tag) {
    case kInt:
    case kFloat:
    case 0x06:  // unhandled
    case 0x08:  // else
    case kEndif:
    case kAutorun:
      *kind = Kind::kScalar;
      return true;
    case kVar:
    case kFunc:
    case kObject:
    case kSymbol:
    case kIfdef:
    case kString:
    case 0x20:  // define
    case 0x21:  // include
    case 0x22:  // merge
    case 0x23:  // ifndef
    case 0x25:  // undef
      *kind = Kind::kText;
      return true;
    case kArray:
    case kCommand:
    case kProperty:
      *kind = Kind::kBlock;
      return true;
    default:
      return false;
  }
}

Node Node::Int(uint32_t value) {
  Node node;
  node.tag = kInt;
  node.scalar = value;
  return node;
}
Node Node::Symbol(std::string_view name) {
  Node node;
  node.tag = kSymbol;
  node.text.assign(name);
  return node;
}
Node Node::Var(std::string_view name) {
  Node node;
  node.tag = kVar;
  node.text.assign(name);
  return node;
}
Node Node::String(std::string_view text) {
  Node node;
  node.tag = kString;
  node.text.assign(text);
  return node;
}
Node Node::Array(std::vector<Node> kids, uint16_t line) {
  Node node;
  node.tag = kArray;
  node.line = line;
  node.kids = std::move(kids);
  return node;
}
Node Node::Command(std::vector<Node> kids, uint16_t line) {
  Node node;
  node.tag = kCommand;
  node.line = line;
  node.kids = std::move(kids);
  return node;
}
Node Node::Property(std::vector<Node> kids, uint16_t line) {
  Node node;
  node.tag = kProperty;
  node.line = line;
  node.kids = std::move(kids);
  return node;
}
Node Node::Ifdef(std::string_view macro) {
  Node node;
  node.tag = kIfdef;
  node.text.assign(macro);
  return node;
}
Node Node::Endif() {
  Node node;
  node.tag = kEndif;
  return node;
}

std::vector<uint8_t> SerializeNodes(const std::vector<Node>& nodes) {
  std::vector<uint8_t> out;
  for (const Node& node : nodes) {
    SerializeNode(node, &out);
  }
  return out;
}

std::vector<uint8_t> Compile(const std::vector<Node>& nodes, const RootHeader& header) {
  std::vector<uint8_t> body;
  body.push_back(0x01);  // "an array is present"
  StoreU16(&body, static_cast<uint16_t>(nodes.size()));
  StoreU16(&body, header.line);
  StoreU16(&body, header.deprecated);
  const std::vector<uint8_t> stream = SerializeNodes(nodes);
  body.insert(body.end(), stream.begin(), stream.end());

  std::vector<uint8_t> file(4 + body.size());
  menu_options::EncodeFile(header.seed, body.data(), body.size(), file.data());
  return file;
}

bool Parse(const uint8_t* file, size_t available, std::vector<Node>* out, RootHeader* header,
           std::string* reason) {
  auto fail = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (file == nullptr || available < 11) {
    return fail("too short to be a .dtb");
  }
  const uint32_t seed = LoadU32(file);
  const size_t body_size = available - 4;
  // Decode by encoding: the keystream is its own inverse, so EncodeFile on the encrypted body
  // with the file's own seed yields the plaintext. It writes the seed word first, so the body is
  // read from `plain` starting at +4.
  std::vector<uint8_t> plain(body_size + 4);
  menu_options::EncodeFile(seed, file + 4, body_size, plain.data());
  const uint8_t* body = plain.data() + 4;
  if (body[0] != 0x01) {
    return fail("not a compiled DTA array");
  }
  const uint32_t root_arity = LoadU16(body + 1);
  if (root_arity == 0 || root_arity > kMaxNodes) {
    return fail("implausible root arity");
  }
  const RootHeader local_header{seed, static_cast<uint16_t>(LoadU16(body + 3)),
                                static_cast<uint16_t>(LoadU16(body + 5))};

  Reader reader(body, body_size, 7);
  out->clear();
  out->reserve(root_arity);
  for (uint32_t i = 0; i < root_arity; ++i) {
    Node node;
    if (!ParseNode(&reader, &node, 0)) {
      return fail("the node stream does not parse");
    }
    out->push_back(std::move(node));
  }
  // The same proof menu_options::LoadFile makes: a file this module cannot reproduce byte for byte
  // is a file whose grammar it does not have.
  const size_t used = reader.pos() - 7;
  const std::vector<uint8_t> check = SerializeNodes(*out);
  if (check.size() != used || std::memcmp(check.data(), body + 7, used) != 0) {
    return fail("the node stream does not reserialize");
  }
  if (header != nullptr) {
    *header = local_header;
  }
  if (reason != nullptr) reason->clear();
  return true;
}

}  // namespace rb_blitz::ui::dtb
