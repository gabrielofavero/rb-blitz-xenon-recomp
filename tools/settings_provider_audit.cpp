// SPDX-License-Identifier: GPL-2.0-only
// Bounded static audit of the owner's installed Ultimate DTBs. Output contains only
// known settings-variable references and callback names, never extracted DTB bodies.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "settings/ultimate_adapter.h"
#include "ui/dtb_author.h"

namespace {
namespace adapter = rb_blitz::settings::ultimate_adapter;
namespace dtb = rb_blitz::ui::dtb;

std::string Escape(std::string_view input) {
  std::string result;
  for (unsigned char ch : input) {
    if (ch == '\\' || ch == '"') result += '\\';
    if (ch == '\n') { result += "\\n"; continue; }
    if (ch == '\r') { result += "\\r"; continue; }
    if (ch == '\t') { result += "\\t"; continue; }
    result += static_cast<char>(ch);
  }
  return result;
}

std::string Phase(std::string_view path) {
  if (path.find("ulti_init") != path.npos) return "initialization";
  if (path.find("mod_settings") != path.npos || path.find("ulti_macros") != path.npos ||
      path.find("ulti_funcs") != path.npos) return "row_callback";
  if (path.find("song_select") != path.npos) return "panel_entry";
  if (path.find("game_enter") != path.npos || path.find("config/") != path.npos ||
      path.find("load_gems") != path.npos) return "song_load";
  if (path.find("beatmatcher_callbacks") != path.npos) return "poll_or_event";
  return "unclassified";
}

struct ReferenceStats {
  unsigned count = 0;
  std::map<std::string, unsigned> contexts;
  std::map<std::string, unsigned> accesses;
};

void CountReferences(const dtb::Node& node, const std::set<std::string>& known,
                     std::map<std::string, ReferenceStats>* counts,
                     std::string_view context = {}) {
  if (node.tag == dtb::kVar && known.count(node.text)) {
    ReferenceStats& stats = (*counts)[node.text];
    ++stats.count;
    const std::string name(context);
    ++stats.contexts[name.empty() ? "unnamed" : name];
    const bool write = context == "set" || context == "toggle_var" ||
                       context == "set_var";
    const bool row = context == "bool" || context == "var";
    ++stats.accesses[write ? "write" : row ? "row_binding" : "read_candidate"];
  }
  const std::string_view child_context = node.kids.empty() || node.kids.front().text.empty()
      ? context : std::string_view(node.kids.front().text);
  for (const auto& child : node.kids) CountReferences(child, known, counts, child_context);
}

void FindCallbacks(const dtb::Node& node, std::string_view row,
                   std::set<std::string>* callbacks, bool within_row = false,
                   bool within_script = false) {
  if (!node.kids.empty() && node.kids.front().text == row) within_row = true;
  if (within_row && !node.kids.empty() && node.kids.front().text == "sel_script")
    within_script = true;
  if (within_script && node.tag == dtb::kCommand && !node.kids.empty() &&
      !node.kids.front().text.empty()) callbacks->insert(node.kids.front().text);
  for (const auto& child : node.kids)
    FindCallbacks(child, row, callbacks, within_row, within_script);
}

void CollectCommands(const dtb::Node& node, std::set<std::string>* commands) {
  if (node.tag == dtb::kCommand && !node.kids.empty() &&
      !node.kids.front().text.empty()) commands->insert(node.kids.front().text);
  for (const auto& child : node.kids) CollectCommands(child, commands);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "--check")) {
    std::cerr << "usage: rb_blitz_settings_provider_audit <game/ultimate> [--check]\n";
    return 2;
  }
  const std::filesystem::path root(argv[1]);
  std::string error;
  std::vector<uint8_t> header;
  if (!adapter::ReadFileBytes(root / "gen/patch_xbox.hdr", &header, &error)) {
    std::cerr << error << '\n'; return 1;
  }
  adapter::ArkIndex index;
  if (!adapter::ReadArkIndex(header.data(), header.size(), &index, &error)) {
    std::cerr << error << '\n'; return 1;
  }
  if (index.parts.size() != 1) {
    std::cerr << "audit expects one installed archive part\n"; return 1;
  }
  std::vector<uint8_t> archive;
  if (!adapter::ReadFileBytes(root / "gen/patch_xbox_0.ark", &archive, &error)) {
    std::cerr << error << '\n'; return 1;
  }
  adapter::RecordBytes records;
  if (!adapter::ReadProviderRecords(root, &records, &error)) {
    std::cerr << error << '\n'; return 1;
  }
  const auto shape = adapter::CheckShape(records);
  if (!shape.ok) { std::cerr << "installed provider shape is incompatible\n"; return 1; }
  std::vector<dtb::Node> macro_roots;
  dtb::RootHeader macro_header;
  if (!dtb::Parse(records.ulti_macros.data(), records.ulti_macros.size(),
                  &macro_roots, &macro_header, &error)) {
    std::cerr << "cannot parse provider row macros: " << error << '\n'; return 1;
  }
  std::map<std::string, std::set<std::string>> macro_callbacks;
  for (size_t i = 0; i + 1 < macro_roots.size(); ++i) {
    if (macro_roots[i].tag != 0x20) continue;  // #define name, followed by its body
    CollectCommands(macro_roots[i + 1], &macro_callbacks[macro_roots[i].text]);
  }
  std::set<std::string> known;
  for (const auto& binding : adapter::RowBindings()) known.insert(std::string(binding.variable));
  for (const auto& binding : adapter::NativeDefaultBindings())
    known.insert(std::string(binding.variable));

  std::map<std::string, std::map<std::string, ReferenceStats>> references;
  std::vector<dtb::Node> settings_roots;
  unsigned scanned = 0, skipped_compressed = 0, parse_failed = 0;
  for (const auto& entry : index.entries) {
    if (!entry.path.ends_with(".dtb")) continue;
    if (entry.compressed()) { ++skipped_compressed; continue; }
    if (entry.offset > archive.size() || entry.size > archive.size() - entry.offset) {
      std::cerr << "archive entry out of bounds: " << entry.path << '\n'; return 1;
    }
    std::vector<dtb::Node> roots;
    dtb::RootHeader parsed_header;
    if (!dtb::Parse(archive.data() + entry.offset, size_t(entry.size), &roots,
                    &parsed_header, &error)) { ++parse_failed; continue; }
    ++scanned;
    if (entry.path == adapter::RecordName(adapter::Record::kModSettings))
      settings_roots = roots;
    auto& counts = references[entry.path];
    for (const auto& node : roots) CountReferences(node, known, &counts);
  }
  if (settings_roots.empty() || scanned != 42 || known.size() != 18 ||
      parse_failed != 0 || skipped_compressed != 0) {
    std::cerr << "incomplete DTB audit: parsed=" << scanned
              << " compressed=" << skipped_compressed << " failed=" << parse_failed << '\n';
    return 1;
  }
  if (argc == 3) {
    std::cout << "parsed " << scanned << " installed DTBs; audited " << known.size()
              << " known variables\n";
    return 0;
  }
  std::cout << "{\"schema\":1,\"audit\":\"static_provider_references\",\"parsed_dtbs\":"
            << scanned << ",\"variables\":[";
  bool first_variable = true;
  for (const auto& variable : known) {
    if (!first_variable) std::cout << ',';
    first_variable = false;
    const adapter::RowShape* row = nullptr;
    for (const auto& panel : shape.panels)
      for (const auto& candidate : panel.rows)
        if (candidate.variable == variable) row = &candidate;
    std::set<std::string> callbacks;
    if (row != nullptr)
      for (const auto& node : settings_roots)
        FindCallbacks(node, row->symbol, &callbacks);
    std::cout << "{\"variable\":\"" << Escape(variable) << "\",\"row\":\""
              << Escape(row ? row->symbol : "") << "\",\"row_macro\":\""
              << Escape(row ? row->macro : "") << "\",\"callbacks\":[";
    bool first_callback = true;
    for (const auto& callback : callbacks) {
      if (!first_callback) std::cout << ',';
      first_callback = false;
      std::cout << '"' << Escape(callback) << '"';
    }
    std::cout << "],\"macro_callbacks\":[";
    first_callback = true;
    if (row != nullptr) {
      for (const auto& callback : macro_callbacks[row->macro]) {
        if (!first_callback) std::cout << ',';
        first_callback = false;
        std::cout << '"' << Escape(callback) << '"';
      }
    }
    std::cout << "],\"references\":[";
    bool first_reference = true;
    for (const auto& [path, counts] : references) {
      auto it = counts.find(variable);
      if (it == counts.end()) continue;
      if (!first_reference) std::cout << ',';
      first_reference = false;
      std::cout << "{\"file\":\"" << Escape(path) << "\",\"phase\":\""
                << Phase(path) << "\",\"count\":" << it->second.count;
      for (const auto& [label, categories] : {
               std::pair<std::string_view, const std::map<std::string, unsigned>&>(
                   "contexts", it->second.contexts),
               std::pair<std::string_view, const std::map<std::string, unsigned>&>(
                   "accesses", it->second.accesses)}) {
        std::cout << ",\"" << label << "\":{";
        bool first = true;
        for (const auto& [name, count] : categories) {
          if (!first) std::cout << ',';
          first = false;
          std::cout << '"' << Escape(name) << "\":" << count;
        }
        std::cout << '}';
      }
      std::cout << '}';
    }
    std::cout << "]}";
  }
  std::cout << "]}\n";
}
