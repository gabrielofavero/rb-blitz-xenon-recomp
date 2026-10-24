// SPDX-License-Identifier: GPL-2.0-only
// Read-only P0 evidence: use the production DTB parser and offline transform.
#include "ui/dtb_author.h"
#include "ui/menu_options.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>

namespace dtb = rb_blitz::ui::dtb;
void Print(const dtb::Node& node) {
  dtb::Kind kind;
  if (!dtb::KindOfTag(node.tag, &kind)) return;
  if (kind == dtb::Kind::kBlock) {
    const char open = node.tag == dtb::kArray ? '(' : node.tag == dtb::kCommand ? '{' : '[';
    const char close = open == '(' ? ')' : open == '{' ? '}' : ']';
    std::cout << open;
    bool first = true;
    for (const auto& kid : node.kids) {
      if (!first) std::cout << ' ';
      first = false;
      Print(kid);
    }
    std::cout << close;
  } else if (kind == dtb::Kind::kText) {
    if (node.tag == dtb::kVar) std::cout << '$';
    else if (node.tag == 0x20) std::cout << "#define ";
    else if (node.tag == dtb::kIfdef) std::cout << "#ifdef ";
    else if (node.tag == 0x23) std::cout << "#ifndef ";
    if (node.tag == dtb::kString) std::cout << std::quoted(node.text);
    else std::cout << node.text;
  } else {
    // Keep tag information: #else/#endif and floats are not integer statements.
    std::cout << "<" << node.tag << ":" << node.scalar << ">";
  }
}
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: loading_record_inspect FILE.dtb [--offline]\n";
    return 1;
  }
  std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
  if (!input || input.tellg() < 0 || input.tellg() > 4 * 1024 * 1024) return 1;
  input.seekg(0);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (argc == 3) {
    if (std::string_view(argv[2]) != "--offline") return 1;
    const auto result = rb_blitz::menu_options::SkipOfflinePrompts(bytes.data(), bytes.size());
    std::cerr << "offline_transform applied=" << result.applied << " reason=" << result.reason << '\n';
  }
  std::vector<dtb::Node> roots;
  dtb::RootHeader header;
  std::string error;
  if (!dtb::Parse(bytes.data(), bytes.size(), &roots, &header, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  for (const auto& root : roots) {
    if (root.tag == dtb::kCommand && root.kids.size() > 3) {
      // One line per object method/property, so long handlers stay searchable.
      std::cout << "object ";
      for (size_t i = 0; i < 3; ++i) { Print(root.kids[i]); std::cout << ' '; }
      std::cout << '\n';
      for (size_t i = 3; i < root.kids.size(); ++i) { Print(root.kids[i]); std::cout << '\n'; }
    } else { Print(root); std::cout << '\n'; }
  }
}
