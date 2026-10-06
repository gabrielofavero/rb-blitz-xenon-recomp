// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the flat DLC library in src/fs/dlc_library.h, which
// src/hooks/dlc.cpp scans and hands to the SDK content manager
// (ContentManager::set_extra_content_library()). No SDK, no game image, no boot: the
// questions are what a package says it is, what a recursive walk finds, and what a
// name has to look like before the guest can hand it back.
//
// The fixtures are real files with a real STFS prefix, because the parser reads fixed
// offsets out of it - a name-only fixture would prove nothing about the offsets that
// docs/dlc.md and the SDK's StfsHeader fix.

#include "check.h"

#include "fs/dlc_library.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::fs;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

// The header is big-endian and the two fields the scanner reads are not adjacent, so a
// fixture is built by writing them at their documented offsets into a prefix, then
// padding the file out to the smallest container the SDK will mount.
void WritePackage(const fs::path& path, const char* magic, uint32_t title_id,
                  uint32_t content_type) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);

  std::vector<char> bytes(kStfsHeaderSize, '\0');
  std::memcpy(bytes.data(), magic, 4);
  const auto store_be = [&bytes](std::size_t offset, uint32_t value) {
    bytes[offset] = static_cast<char>(value >> 24);
    bytes[offset + 1] = static_cast<char>(value >> 16);
    bytes[offset + 2] = static_cast<char>(value >> 8);
    bytes[offset + 3] = static_cast<char>(value);
  };
  store_be(kStfsContentTypeOffset, content_type);
  store_be(kStfsTitleIdOffset, title_id);

  std::ofstream file(path, std::ios::binary);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void WriteFile(const fs::path& path, const std::string& contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary);
  file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

struct Scratch {
  fs::path base;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-dlc-library";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
};

const LibraryItem* FindItem(const LibraryScanResult& scan, const uint32_t title_id,
                            const uint32_t content_type, const std::string& file_name) {
  for (const auto& item : scan.items) {
    if (item.title_id == title_id && item.content_type == content_type &&
        item.file_name == file_name) {
      return &item;
    }
  }
  return nullptr;
}

const LibraryScanResult::Rejected* FindRejected(const LibraryScanResult& scan,
                                                const std::string& fragment) {
  for (const auto& rejected : scan.rejected) {
    if (rejected.entry.find(fragment) != std::string::npos) {
      return &rejected;
    }
  }
  return nullptr;
}

// What the dumped folder on this machine actually holds: RB3, RB2 and RB1 custom songs
// are CON containers of content type 1, and one RB1 marketplace package is LIVE.
constexpr uint32_t kRb3TitleId = 0x45410914;
constexpr uint32_t kRb2TitleId = 0x45410869;
constexpr uint32_t kRb1TitleId = 0x45410829;
constexpr uint32_t kBlitzTitleId = 0x5841122D;
constexpr uint32_t kSavedGame = 0x00000001;
constexpr uint32_t kMarketplace = 0x00000002;

}  // namespace

int main() {
  BeginCase("a package's identity is read from its own header, not its folder");
  {
    Scratch s;
    const fs::path package = s.base / "anything" / "nested" / "Adele - Skyfall";
    WritePackage(package, "CON ", kRb3TitleId, kSavedGame);

    PackageIdentity identity;
    std::string reason;
    CHECK_TRUE(ReadPackageIdentity(package, &identity, &reason) == PackageIdentityResult::kRead);
    CHECK_EQ(identity.title_id, kRb3TitleId);
    CHECK_EQ(identity.content_type, kSavedGame);

    // The two other magics are containers too.
    WritePackage(s.base / "pirs", "PIRS", kRb1TitleId, kMarketplace);
    WritePackage(s.base / "live", "LIVE", kRb2TitleId, kSavedGame);
    CHECK_TRUE(ReadPackageIdentity(s.base / "pirs", &identity, &reason) ==
               PackageIdentityResult::kRead);
    CHECK_EQ(identity.title_id, kRb1TitleId);
    CHECK_TRUE(ReadPackageIdentity(s.base / "live", &identity, &reason) ==
               PackageIdentityResult::kRead);
    CHECK_EQ(identity.title_id, kRb2TitleId);
  }

  BeginCase("what is not a container is silent, what cannot be mounted is reported");
  {
    Scratch s;
    // A library holds other files; they are not mistakes and only get counted.
    WriteFile(s.base / "song.dat", "MOGG not a container");
    WriteFile(s.base / "notes.txt", "where do the songs go?");

    PackageIdentity identity;
    std::string reason;
    CHECK_TRUE(ReadPackageIdentity(s.base / "song.dat", &identity, &reason) ==
               PackageIdentityResult::kNotPackage);
    CHECK_TRUE(ReadPackageIdentity(s.base / "notes.txt", &identity, &reason) ==
               PackageIdentityResult::kNotPackage);

    // The magic is there but the file cannot be an STFS container: that is reported,
    // because it is a package the user expected to work.
    WriteFile(s.base / "truncated", "CON \x00\x00\x00\x00");
    CHECK_TRUE(ReadPackageIdentity(s.base / "truncated", &identity, &reason) ==
               PackageIdentityResult::kUnreadable);
    CHECK_TRUE(reason.find("shorter") != std::string::npos);

    // A mountable container whose header names no title id has nowhere to be filed.
    WritePackage(s.base / "no-title", "LIVE", 0, kMarketplace);
    CHECK_TRUE(ReadPackageIdentity(s.base / "no-title", &identity, &reason) ==
               PackageIdentityResult::kUnreadable);
    CHECK_TRUE(reason.find("title id") != std::string::npos);
  }

  BeginCase("a library is walked recursively, whatever the arrangement");
  {
    Scratch s;
    // Flat, exactly like a dumped song folder ...
    WritePackage(s.base / "Adele - Skyfall", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(s.base / "ACDC - Back in Black (Live)", "CON ", kRb3TitleId, kSavedGame);
    // ... and nested, because nothing says a library is one level deep.
    WritePackage(s.base / "Rock Band 2" / "pack" / "Blink-182 - Dammit", "CON ", kRb2TitleId,
                 kSavedGame);
    WritePackage(s.base / "Rock Band 1" / "deep" / "still" / "Deeper", "CON ", kRb1TitleId,
                 kSavedGame);
    // A folder that holds no package must not derail the walk.
    WriteFile(s.base / "badsongs" / "broken.dat", "not a package");

    const LibraryScanResult scan = ScanDlcLibraries({s.base});
    CHECK_EQ(scan.items.size(), 4);
    CHECK_EQ(scan.files_seen, 5);
    CHECK_EQ(scan.other_files, 1);
    CHECK_EQ(scan.rejected.size(), 0);
    // Presented as marketplace content, because a saved-game package is not a thing the
    // title lists (see the adaptation case below).
    CHECK_EQ(scan.adapted_saved_games, 4);
    CHECK_TRUE(FindItem(scan, kRb3TitleId, kMarketplace, "Adele - Skyfall") != nullptr);
    CHECK_TRUE(FindItem(scan, kRb2TitleId, kMarketplace, "Blink-182 - Dammit") != nullptr);
    CHECK_TRUE(FindItem(scan, kRb1TitleId, kMarketplace, "Deeper") != nullptr);
  }

  BeginCase("several roots are one library, and a missing root is reported");
  {
    Scratch s;
    const fs::path first = s.base / "first";
    const fs::path second = s.base / "second";
    WritePackage(first / "one", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(second / "two", "CON ", kRb2TitleId, kSavedGame);

    const LibraryScanResult scan = ScanDlcLibraries({first, second, s.base / "missing"});
    CHECK_EQ(scan.items.size(), 2);
    CHECK_EQ(scan.rejected.size(), 1);
    CHECK_TRUE(FindRejected(scan, "missing") != nullptr);
  }

  BeginCase("a name fits the guest's field, and a long one keeps a readable prefix");
  {
    PackageIdentity identity{kRb3TitleId, kSavedGame};
    std::set<std::string> used;

    const std::string short_name = "Adele - Skyfall";
    CHECK_TRUE(UniqueLibraryFileName(short_name, identity, &used) == short_name);

    const std::string long_name =
        "Panic at the Disco - Lying Is The Most Fun A Girl Can Have Without Taking Her Clothes Off";
    const std::string shortened = UniqueLibraryFileName(long_name, identity, &used);
    CHECK_TRUE(shortened.size() <= kMaxContentFileName);
    CHECK_TRUE(shortened.rfind("Panic at the Disco", 0) == 0);
    CHECK_TRUE(shortened.find('~') != std::string::npos);
    // Deterministic: the same package gets the same name on the next boot.
    std::set<std::string> again;
    CHECK_TRUE(UniqueLibraryFileName(long_name, identity, &again) == shortened);

    // A duplicate short name cannot be handed out twice.
    const std::string second = UniqueLibraryFileName(short_name, identity, &used);
    CHECK_TRUE(second != short_name);
    CHECK_TRUE(second.size() <= kMaxContentFileName);
  }

  BeginCase("names are unique within a title/content-type group, not across the library");
  {
    Scratch s;
    WritePackage(s.base / "A" / "same-name", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(s.base / "B" / "same-name", "CON ", kRb3TitleId, kSavedGame);
    // Same name, different group: it keeps its plain name.
    WritePackage(s.base / "C" / "same-name", "CON ", kRb2TitleId, kSavedGame);

    const LibraryScanResult scan = ScanDlcLibraries({s.base});
    CHECK_EQ(scan.items.size(), 3);
    std::set<std::string> rb3_names;
    std::size_t rb2_plain = 0;
    for (const auto& item : scan.items) {
      CHECK_TRUE(item.file_name.size() <= kMaxContentFileName);
      if (item.title_id == kRb3TitleId) {
        rb3_names.insert(item.file_name);
      } else if (item.file_name == "same-name") {
        ++rb2_plain;
      }
    }
    CHECK_EQ(rb3_names.size(), 2);
    CHECK_EQ(rb2_plain, 1);
  }

  BeginCase("a truncated UTF-8 sequence is never a name");
  {
    // "São Paulo" is what one of the real packages ends with; a prefix cut through the
    // ã would hand the guest a byte string that is not text.
    const std::string name =
        "Linkin Park - Somewhere I Belong - Unshatter Film Soundtrack - Live in S\u00e3o Paulo";
    const std::string prefix = TruncateUtf8(name, 32);
    CHECK_TRUE(prefix.size() <= 32);
    CHECK_TRUE(name.rfind(prefix, 0) == 0);
    // No dangling lead byte: the last byte is not the start of a multi-byte sequence.
    const unsigned char last = static_cast<unsigned char>(prefix.back());
    CHECK_FALSE((last & 0x80) != 0 && (last & 0xC0) != 0x80);
  }

  BeginCase("a content type is parsed as 8 hex digits");
  {
    uint32_t value = 0;
    CHECK_TRUE(ParseHex32("00000002", &value));
    CHECK_EQ(value, 0x00000002u);
    CHECK_TRUE(ParseHex32("00009000", &value));
    CHECK_EQ(value, 0x00009000u);
    CHECK_TRUE(ParseHex32("45410914", &value));
    CHECK_EQ(value, 0x45410914u);
    // Lower case is the same number, because it is a value and not a directory name.
    CHECK_TRUE(ParseHex32("0000000a", &value));
    CHECK_EQ(value, 0x0000000Au);
    CHECK_FALSE(ParseHex32("", &value));
    CHECK_FALSE(ParseHex32("0000000G", &value));
    CHECK_FALSE(ParseHex32("0x02", &value));
    CHECK_FALSE(ParseHex32("123456789", &value));  // wider than the field
    // Fewer digits are the same value, because a content type is a number.
    CHECK_TRUE(ParseHex32("2", &value));
    CHECK_EQ(value, 0x00000002u);
  }

  BeginCase("the content type a library package is presented under");
  {
    // A saved-game package becomes marketplace content, and nothing else moves.
    CHECK_EQ(LibraryContentType(kSavedGame, 0), kMarketplaceContentType);
    CHECK_EQ(LibraryContentType(kMarketplace, 0), kMarketplaceContentType);
    CHECK_EQ(LibraryContentType(0x00009000u, 0), 0x00009000u);
    // An explicit type replaces the answer for everything, including a package that is
    // already that type and one the adaptation would have moved.
    CHECK_EQ(LibraryContentType(kSavedGame, 0x00009000u), 0x00009000u);
    CHECK_EQ(LibraryContentType(kMarketplace, kSavedGame), kSavedGame);

    Scratch s;
    // A custom-song folder: saved-game packages the title enumerates but never lists.
    WritePackage(s.base / "Adele - Skyfall", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(s.base / "ACDC - Back in Black", "CON ", kRb3TitleId, kSavedGame);
    // A marketplace package the adaptation must leave alone, and one of a type that is
    // neither.
    WritePackage(s.base / "Rock Band 1" / "Green Day Rock Band", "LIVE", kRb1TitleId,
                 kMarketplace);
    WritePackage(s.base / "Blitz" / "jumpsuit", "LIVE", kBlitzTitleId, 0x00009000);

    const LibraryScanResult scan = ScanDlcLibraries({s.base});
    CHECK_EQ(scan.items.size(), 4);
    CHECK_EQ(scan.adapted_saved_games, 2);
    CHECK_TRUE(FindItem(scan, kRb3TitleId, kMarketplace, "Adele - Skyfall") != nullptr);
    CHECK_TRUE(FindItem(scan, kRb1TitleId, kMarketplace, "Green Day Rock Band") != nullptr);
    CHECK_TRUE(FindItem(scan, kBlitzTitleId, 0x00009000, "jumpsuit") != nullptr);
    CHECK_TRUE(FindItem(scan, kRb3TitleId, kSavedGame, "Adele - Skyfall") == nullptr);

    // Forced to one type: every item moves there, and the names are unchanged (a name
    // is the package's identity, not a property of the group).
    const LibraryScanResult forced = ScanDlcLibraries({s.base}, kMarketplace);
    CHECK_EQ(forced.items.size(), 4);
    CHECK_EQ(forced.adapted_saved_games, 0);  // nothing was adapted, everything forced
    CHECK_TRUE(FindItem(forced, kBlitzTitleId, kMarketplace, "jumpsuit") != nullptr);
    CHECK_TRUE(FindItem(forced, kBlitzTitleId, 0x00009000, "jumpsuit") == nullptr);
    CHECK_EQ(forced.rejected.size(), 0);
  }

  BeginCase("the cvar value splits on ';' and ignores empty entries");
  {
    const std::vector<std::string> parts =
        SplitPathList(" ; D:\\Games\\YARG Songs ;D:\\Elsewhere; ");
    CHECK_EQ(parts.size(), 2);
    CHECK_TRUE(parts[0] == "D:\\Games\\YARG Songs");
    CHECK_TRUE(parts[1] == "D:\\Elsewhere");
    CHECK_EQ(SplitPathList("").size(), 0);
    CHECK_EQ(SplitPathList("   ").size(), 0);
  }

  BeginCase("what is scanned is what the hook hands the content manager");
  {
    Scratch s;
    WritePackage(s.base / "Adele - Skyfall", "CON ", kRb3TitleId, kSavedGame);
    const LibraryScanResult scan = ScanDlcLibraries({s.base});
    CHECK_EQ(scan.items.size(), 1);
    if (!scan.items.empty()) {
      CHECK_TRUE(scan.items[0].host_path == s.base / "Adele - Skyfall");
      CHECK_TRUE(fs::is_regular_file(scan.items[0].host_path));
    }
  }

  return Finish();
}
