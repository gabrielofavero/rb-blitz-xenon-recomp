// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/settings/menu_payload.cpp, the archive half of the generated Help & Options
// menu (P5b/P11). SDK-free: the fixtures are an index and bytes, and the assertions go through the
// project's own v6 reader (`settings/ultimate_adapter.h`), which is also what the runtime uses.
//
// The property that matters, and the reason this file exists: **the extended archive keeps the
// shape the title can read.** Every archive Rock Band Blitz ships or reads declares exactly one
// part (measured 2026-10-12 with `scripts/hmx_ark.py list`: `main_xbox.hdr` -> one part,
// `patch_xbox.hdr` -> one part). Extending the mounted patch by *adding* a second part - which is
// what this code used to do - produced an Ultimate boot that re-read the generated part forever and
// never presented (docs/settings/status.md (part 2, the black startup)). So: one part in, one part
// out, every record inside the part it belongs to, and the mounted bytes carried verbatim.

#include "check.h"

#include "settings/menu_payload.h"
#include "settings/ultimate_adapter.h"
#include "ui/menu_options.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::test;
using rb_blitz::settings::ExtendMenuArchive;
using rb_blitz::settings::MenuRecords;
using rb_blitz::settings::ultimate_adapter::ArkEntry;
using rb_blitz::settings::ultimate_adapter::ArkIndex;
using rb_blitz::settings::ultimate_adapter::ReadArkIndex;

// check.h carries no string comparison, and a payload path is exactly the kind of value whose
// failure needs both sides printed.
void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

// The third-party thanks both credit files carry (owner request, 2026-10-21), as the credits
// screen renders them: the role column's text, then the name column's.
const std::vector<std::string> kThanksRows = {
    "Recompiler|ReXGlue Team",   "Static Recompiler|XenonRecomp",
    "Emulation Research|Xenia Project", "Rock Band 3 Recomp|band3_recomp",
    "Customs Store|RhythmVerse", "Asset Tooling|MiloLib",
};

// The rows under one `heading` of a credits document, up to the next blank root - the shape
// retail's own file uses to separate sections. A `title_name` row is "role|name".
std::vector<std::string> SectionRows(const std::vector<rb_blitz::ui::dtb::Node>& roots,
                                     const std::string& heading) {
  std::vector<std::string> rows;
  bool in_section = false;
  for (const auto& root : roots) {
    if (root.tag != rb_blitz::ui::dtb::kArray || root.kids.empty()) {
      in_section = false;
      continue;
    }
    if (root.kids[0].tag == rb_blitz::ui::dtb::kSymbol && root.kids[0].text == "heading") {
      in_section = root.kids.size() >= 2 && root.kids[1].text == heading;
      continue;
    }
    if (in_section && root.kids.size() >= 3 && root.kids[0].text == "title_name") {
      rows.push_back(root.kids[1].text + "|" + root.kids[2].text);
    }
  }
  return rows;
}

// Both credit files must name the same projects: one table in the implementation, one
// expectation here.
void CheckThanksRows(const std::vector<rb_blitz::ui::dtb::Node>& roots) {
  const std::vector<std::string> rows = SectionRows(roots, "Thanks To");
  CHECK_EQ(rows.size(), kThanksRows.size());
  for (size_t i = 0; i < rows.size() && i < kThanksRows.size(); ++i) {
    CheckStrEq(rows[i], kThanksRows[i]);
  }
}

// `mounted` describes the patch archive the boot already has: one part, one record that starts at 0.
ArkIndex MountedIndex(size_t part_size) {
  ArkIndex index;
  index.version = 6;
  index.extra.assign(20, 0xFF);
  index.parts = {"gen/patch_xbox_0.ark"};
  index.part_sizes = {static_cast<uint32_t>(part_size)};
  index.checksums = {0};
  index.entries.push_back({"ulti/gen/mod_settings.dtb", 0, 128, 0});
  return index;
}

// The v6 rule the reader implements: an entry's offset is absolute across the ark's parts, so it
// selects a part by accumulating the sizes. `scripts/hmx_ark.py check_layout` is the same check.
bool FitsAnArkPart(const ArkIndex& index, const ArkEntry& entry, std::string* why) {
  uint64_t remaining = entry.offset;
  for (size_t i = 0; i < index.parts.size(); ++i) {
    if (remaining < index.part_sizes[i]) {
      if (remaining + entry.size > index.part_sizes[i]) {
        *why = entry.path + ": " + std::to_string(entry.size) + " bytes at 0x" +
               std::to_string(remaining) + " run past part " + std::to_string(i) + " (" +
               std::to_string(index.part_sizes[i]) + " bytes)";
        return false;
      }
      return true;
    }
    remaining -= index.part_sizes[i];
  }
  *why = entry.path + ": offset 0x" + std::to_string(entry.offset) + " lies past the last part";
  return false;
}

// Every index this code produces has to survive the project's own reader *and* keep every record
// inside a part - the reader refuses a header whose three counts disagree, so parsing at all is
// half the check.
void CheckLayout(const std::vector<uint8_t>& header, const std::vector<uint8_t>& part,
                 const char* what) {
  ArkIndex parsed;
  std::string error;
  if (!ReadArkIndex(header.data(), header.size(), &parsed, &error)) {
    Fail(__FILE__, __LINE__, std::string(what) + ": the generated header does not parse: " + error);
    return;
  }
  CHECK_EQ(parsed.parts.size(), 1u);
  CHECK_EQ(parsed.part_sizes.size(), 1u);
  CHECK_EQ(parsed.checksums.size(), 1u);
  CHECK_EQ(static_cast<size_t>(parsed.part_sizes.front()), part.size());
  for (const ArkEntry& entry : parsed.entries) {
    std::string why;
    CHECK_TRUE(FitsAnArkPart(parsed, entry, &why));
    if (!why.empty()) {
      Fail(__FILE__, __LINE__, std::string(what) + ": " + why);
    }
  }
}

void TestExtendingKeepsOnePart() {
  BeginCase("extending the mounted patch keeps it a one-part archive (the regression)");

  const size_t mounted_size = 4096;
  const ArkIndex mounted = MountedIndex(mounted_size);
  std::vector<uint8_t> mounted_bytes(mounted_size);
  for (size_t i = 0; i < mounted_bytes.size(); ++i) {
    mounted_bytes[i] = static_cast<uint8_t>(i * 7 + 3);  // distinguishable from our records
  }

  MenuRecords records;
  records["ui/gen/ui.dtb"] = std::vector<uint8_t>(97, 0x11);
  records["ui/options_menu/gen/settings_menu.milo_xbox"] = std::vector<uint8_t>(5000, 0x22);

  std::vector<uint8_t> part;
  std::string part_path;
  const std::vector<uint8_t> header =
      ExtendMenuArchive(mounted, records, mounted_bytes, &part_path, &part);

  // One part, and it is the mounted one: same record name, mounted bytes carried verbatim at the
  // head, only the appended records after them.
  CheckStrEq(part_path, "gen/patch_xbox_0.ark");
  // The mounted bytes, then the two records with the 16-byte alignment between them and nothing
  // else: 97 bytes of record, 15 bytes of padding, then 5000 bytes.
  CHECK_EQ(part.size(), mounted_size + 97 + 15 + 5000);
  CHECK_EQ(static_cast<size_t>(std::memcmp(part.data(), mounted_bytes.data(), mounted_size)), 0u);

  CheckLayout(header, part, "extended patch");

  // The record the payload already had keeps its offset and size: nothing that was readable before
  // moves, which is what makes carrying the bytes safe.
  ArkIndex parsed;
  std::string error;
  CHECK_TRUE(ReadArkIndex(header.data(), header.size(), &parsed, &error));
  const ArkEntry* kept = parsed.Find("ulti/gen/mod_settings.dtb");
  CHECK_TRUE(kept != nullptr);
  if (kept != nullptr) {
    CHECK_EQ(kept->offset, 0u);
    CHECK_EQ(kept->size, 128u);
  }
  const ArkEntry* added = parsed.Find("ui/options_menu/gen/settings_menu.milo_xbox");
  CHECK_TRUE(added != nullptr);
  if (added != nullptr) {
    CHECK_EQ(added->size, 5000u);
    // Past the mounted part's bytes - i.e. it really is in the appended region, not overwriting.
    CHECK_TRUE(added->offset >= mounted_size);
  }
}

void TestBaseArkGetsItsOwnPart() {
  BeginCase("with no mounted archive the records get a part of their own");

  ArkIndex empty;
  empty.version = 6;
  empty.extra.assign(20, 0xFF);

  MenuRecords records;
  records["ui/splash/gen/splash.dtb"] = std::vector<uint8_t>(64, 0x33);

  std::vector<uint8_t> part;
  std::string part_path;
  const std::vector<uint8_t> header = ExtendMenuArchive(empty, records, {}, &part_path, &part);

  CheckStrEq(part_path, "gen/settings_xbox_0.ark");
  CHECK_EQ(part.size(), 64u);
  CheckLayout(header, part, "base patch");

  ArkIndex parsed;
  std::string error;
  CHECK_TRUE(ReadArkIndex(header.data(), header.size(), &parsed, &error));
  const ArkEntry* added = parsed.Find("ui/splash/gen/splash.dtb");
  CHECK_TRUE(added != nullptr);
  if (added != nullptr) {
    CHECK_EQ(added->offset, 0u);  // the only part starts at the ark's own origin
  }
}

void TestMountedPartOfTheWrongSizeIsNotCarried() {
  BeginCase("a mounted part that does not match its recorded size is not carried");

  // Defensive: if the bytes on disk disagree with the index (a truncated download, a hand-edited
  // payload), carrying them would produce an archive whose kept entries point at the wrong data.
  // Appending a part of our own leaves the mounted archive's own entries untouched instead.
  ArkIndex mounted = MountedIndex(4096);
  const std::vector<uint8_t> short_bytes(1024, 0x44);

  MenuRecords records;
  records["ui/gen/ui.dtb"] = std::vector<uint8_t>(32, 0x55);

  std::vector<uint8_t> part;
  std::string part_path;
  const std::vector<uint8_t> header =
      ExtendMenuArchive(mounted, records, short_bytes, &part_path, &part);

  CheckStrEq(part_path, "gen/settings_xbox_0.ark");
  ArkIndex parsed;
  std::string error;
  CHECK_TRUE(ReadArkIndex(header.data(), header.size(), &parsed, &error));
  // Two parts, because the mounted one was left alone rather than half-replaced.
  CHECK_EQ(parsed.parts.size(), 2u);
  for (const ArkEntry& entry : parsed.entries) {
    std::string why;
    CHECK_TRUE(FitsAnArkPart(parsed, entry, &why));
  }
  const ArkEntry* kept = parsed.Find("ulti/gen/mod_settings.dtb");
  CHECK_TRUE(kept != nullptr);
  if (kept != nullptr) {
    CHECK_EQ(kept->offset, 0u);
    CHECK_EQ(kept->size, 128u);
  }
}

}  // namespace

int main() {
  BeginCase("the generated locale names a cached list only after a real host hit");
  {
    using rb_blitz::ui::dtb::Node;
    namespace options = rb_blitz::menu_options;
    std::vector<Node> locale{Node::Array({Node::Symbol(options::kEnumeratingContentKey),
                                        Node::String(options::kDiscoveringContentLabel)})};
    CHECK_FALSE(rb_blitz::settings::SetDiscoveryCacheLabel(&locale, false));
    CheckStrEq(locale[0].kids[1].text, options::kDiscoveringContentLabel);
    CHECK_TRUE(rb_blitz::settings::SetDiscoveryCacheLabel(&locale, true));
    CheckStrEq(locale[0].kids[1].text, options::kLoadingCacheLabel);
    CHECK_TRUE(rb_blitz::settings::SetDiscoveryCacheLabel(&locale, true));
    CheckStrEq(locale[0].kids[1].text, options::kLoadingCacheLabel);
  }
  BeginCase("the generated locale renames the downloadable-content row when R7 owns it");
  {
    using rb_blitz::ui::dtb::Node;
    namespace options = rb_blitz::menu_options;
    std::vector<Node> locale{Node::Array(
        {Node::Symbol(options::kDlcRowKey), Node::String(options::kDlcRowLabel)})};
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowLabel(&locale, false));
    CheckStrEq(locale[0].kids[1].text, options::kDlcRowLabel);
    CHECK_TRUE(rb_blitz::settings::SetRefreshRowLabel(&locale, true));
    CheckStrEq(locale[0].kids[1].text, options::kDlcRowRefreshLabel);
    // A translation of the shipped label, or a locale that never carried it, is left
    // alone: the replacement needs the exact text (the same scope the in-flight
    // menu_filter relabel has always had). ReplaceLocaleEntry reports done either way -
    // "left as it ships" is the outcome, not a refusal.
    std::vector<Node> translated{Node::Array(
        {Node::Symbol(options::kDlcRowKey), Node::String("Inhalt herunterladen")})};
    CHECK_TRUE(rb_blitz::settings::SetRefreshRowLabel(&translated, true));
    CheckStrEq(translated[0].kids[1].text, "Inhalt herunterladen");
  }
  BeginCase("the payload's own splash copy carries the refresh action when R7 owns the row");
  {
    using rb_blitz::ui::dtb::Node;
    namespace options = rb_blitz::menu_options;
    // The switch the row is dispatched in, with a sibling case so the walk has to find the right
    // one. The shipped body is retail's own online-store branch, which is what the press must no
    // longer run with R7 on.
    const auto fixture = [] {
      return std::vector<Node>{Node::Command(
          {Node::Symbol("switch"), Node::Symbol("selected_sym"),
           Node::Array({Node::Symbol("splash_start"),
                        Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                                       Node::Symbol("demo_screen")})}),
           Node::Array({Node::Symbol(options::kDlcRowKey),
                        Node::Command({Node::Symbol("if_else"), Node::Symbol("online"),
                                       Node::Symbol("store"), Node::Symbol("popup")})})})};
    };

    std::vector<Node> splash = fixture();
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowAction(&splash, false));
    CHECK_EQ(splash[0].kids.size(), 4u);  // the switch is untouched while the flag is off
    CHECK_EQ(splash[0].kids[3].kids.size(), 2u);  // ...and so is the shipped body

    CHECK_TRUE(rb_blitz::settings::SetRefreshRowAction(&splash, true));
    // kids: the row symbol, then exactly two statements - no padding, because this record is the
    // payload's own and may grow, which is the whole reason the action is built here.
    const Node& kase = splash[0].kids[3];
    CHECK_EQ(kase.kids.size(), 3u);
    CheckStrEq(kase.kids[0].text, options::kDlcRowKey);
    const Node& discover = kase.kids[1];
    CHECK_EQ(discover.kids.size(), 3u);
    CheckStrEq(discover.kids[0].text, "ui");
    CheckStrEq(discover.kids[1].text, "goto_screen");
    CheckStrEq(discover.kids[2].text, options::kRefreshRowDiscoverScreen);
    const Node& call = kase.kids[2];
    CHECK_EQ(call.kids.size(), 2u);
    CheckStrEq(call.kids[0].text, "file_exists");
    CheckStrEq(call.kids[1].text, options::kRefreshSentinelPath);
    // The sibling case is untouched: the walk found the dlc case by name.
    CHECK_EQ(splash[0].kids[2].kids.size(), 2u);

    // Idempotent the same way the in-flight edit is: a case that already carries the sentinel is
    // left alone, so the payload and the hook can both be armed without rewriting twice.
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowAction(&splash, true));
    CHECK_EQ(splash[0].kids[3].kids.size(), 3u);

    // A record that ships no such case is a refusal, not a failure: the in-flight edit still owns
    // the retail copy.
    std::vector<Node> other{Node::Command(
        {Node::Symbol("switch"), Node::Symbol("selected_sym"),
         Node::Array({Node::Symbol("splash_start"),
                      Node::Command({Node::Symbol("ui"), Node::Symbol("goto_screen"),
                                     Node::Symbol("demo_screen")})})})};
    CHECK_FALSE(rb_blitz::settings::SetRefreshRowAction(&other, true));
  }
  BeginCase("the recompilation credits are inserted before the retail credits");
  {
    using rb_blitz::ui::dtb::Node;
    std::vector<Node> roots;
    for (int i = 0; i < 3; ++i) roots.push_back(Node::Array({}));
    roots.push_back(
        Node::Array({Node::Symbol("heading"), Node::String("Core Development Team")}));
    roots.push_back(Node::Array({Node::Symbol("title_name"), Node::String("Project Lead"),
                                 Node::String("Matthew Nordhaus")}));

    std::string why;
    CHECK_TRUE(rb_blitz::settings::AddRecompilationCredits(&roots, false, &why));
    // Three spacers, the project's heading + row, a blank, the thanks heading + its six rows, a
    // blank, then the official section untouched.
    CHECK_EQ(roots.size(), 16u);
    CheckStrEq(roots[3].kids[0].text, "heading");
    CheckStrEq(roots[3].kids[1].text, "Recompilation Project");
    CheckStrEq(roots[4].kids[0].text, "title_name");
    CheckStrEq(roots[4].kids[1].text, "Developer");
    CheckStrEq(roots[4].kids[2].text, "Gabriel Fávero");
    CHECK_EQ(roots[5].kids.size(), 0u);
    CheckStrEq(roots[6].kids[1].text, "Thanks To");
    CheckThanksRows(roots);
    CHECK_EQ(roots[13].kids.size(), 0u);
    CheckStrEq(roots[14].kids[1].text, "Core Development Team");
    CheckStrEq(roots[15].kids[2].text, "Matthew Nordhaus");

    // The edited roots compile to a DTA the project's own parser reads back.
    const std::vector<uint8_t> bytes = rb_blitz::ui::dtb::Compile(roots);
    std::vector<Node> reparsed;
    rb_blitz::ui::dtb::RootHeader header;
    CHECK_TRUE(rb_blitz::ui::dtb::Parse(bytes.data(), bytes.size(), &reparsed, &header, &why));
    CHECK_EQ(reparsed.size(), roots.size());

    // A different document is left alone rather than rewritten.
    std::vector<Node> other{
        Node::Array({Node::Symbol("heading"), Node::String("Some Other Team")})};
    CHECK_FALSE(rb_blitz::settings::AddRecompilationCredits(&other, false, &why));
    CHECK_EQ(other.size(), 1u);
  }
  BeginCase("the recompilation credits rename the Ultimate section and split its row");
  {
    using rb_blitz::ui::dtb::Node;
    const auto fixture = [] {
      std::vector<Node> roots;
      for (int i = 0; i < 2; ++i) roots.push_back(Node::Array({}));
      roots.push_back(Node::Array(
          {Node::Symbol("heading"), Node::String("Rock Band Blitz Ultimate 2.0 Development Team")}));
      roots.push_back(Node::Array(
          {Node::Symbol("title_name"), Node::String("Only Dev"), Node::String("Synchrony")}));
      for (int i = 0; i < 2; ++i) roots.push_back(Node::Array({}));
      roots.push_back(
          Node::Array({Node::Symbol("heading"), Node::String("Core Development Team")}));
      return roots;
    };

    std::vector<Node> roots = fixture();
    std::string why;
    CHECK_TRUE(rb_blitz::settings::AddRecompilationCredits(&roots, true, &why));
    // The one row became two plus the thanks block, so the file grew by nine roots.
    CHECK_EQ(roots.size(), 16u);
    CheckStrEq(roots[2].kids[1].text, "Recompilation Project & Ultimate 2.0 Mod");
    CheckStrEq(roots[3].kids[1].text, "Recompilation Developer");
    CheckStrEq(roots[3].kids[2].text, "Gabriel Fávero");
    CheckStrEq(roots[4].kids[1].text, "Ultimate 2.0 Developer");
    CheckStrEq(roots[4].kids[2].text, "Synchrony");
    CHECK_EQ(roots[5].kids.size(), 0u);
    CheckStrEq(roots[6].kids[1].text, "Thanks To");
    CheckThanksRows(roots);
    // The payload's own blank run is not lengthened by the insertion, and the official section
    // after it keeps its place.
    CHECK_EQ(roots[13].kids.size(), 0u);
    CHECK_EQ(roots[14].kids.size(), 0u);
    CheckStrEq(roots[15].kids[1].text, "Core Development Team");

    // A newer payload whose one row does not match is left untouched, including its heading.
    std::vector<Node> moved = fixture();
    moved[3].kids[1].text = "Someone Else";
    CHECK_FALSE(rb_blitz::settings::AddRecompilationCredits(&moved, true, &why));
    CheckStrEq(moved[2].kids[1].text, "Rock Band Blitz Ultimate 2.0 Development Team");
    CHECK_EQ(moved.size(), fixture().size());
  }
  TestExtendingKeepsOnePart();
  TestBaseArkGetsItsOwnPart();
  TestMountedPartOfTheWrongSizeIsNotCarried();
  return Finish();
}
