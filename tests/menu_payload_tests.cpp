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
  TestExtendingKeepsOnePart();
  TestBaseArkGetsItsOwnPart();
  TestMountedPartOfTheWrongSizeIsNotCarried();
  return Finish();
}
