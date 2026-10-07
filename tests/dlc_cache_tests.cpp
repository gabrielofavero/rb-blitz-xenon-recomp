// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the R7 DLC enumeration cache in src/fs/dlc_cache.h, which
// src/hooks/dlc.cpp reads before it re-scans a flat library. No SDK, no game image, no
// boot: the questions are whether a scan's result survives the round trip, whether the
// fingerprint moves when the tree does, and whether every way a cache can be wrong is a
// miss rather than a use.
//
// The fixtures are real files with a real STFS prefix because the scan reads fixed
// offsets out of them (tests/dlc_library_tests.cpp makes the same point).

#include "check.h"

#include "fs/dlc_cache.h"
#include "fs/dlc_library.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::fs;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

constexpr uint32_t kRb3TitleId = 0x45410914;
constexpr uint32_t kSavedGame = 0x00000001;
constexpr uint32_t kMarketplace = 0x00000002;

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
    base = fs::temp_directory_path(ec) / "rb_blitz-dlc-cache";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
};

}  // namespace

int main() {
  BeginCase("the fingerprint is stable for an unchanged tree");
  {
    Scratch s;
    const fs::path root = s.base / "library";
    WritePackage(root / "one", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(root / "nested" / "two", "LIVE", kRb3TitleId, kSavedGame);
    WriteFile(root / "readme.txt", "not a package");

    DlcTreeFingerprint first;
    std::string reason;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &first, &reason));

    DlcTreeFingerprint second;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &second, &reason));
    CHECK_TRUE(first == second);
    CHECK_EQ(first.file_count, 3);
  }

  BeginCase("the fingerprint moves when a package is added, removed or changed");
  {
    Scratch s;
    const fs::path root = s.base / "library";
    const fs::path package = root / "one";
    WritePackage(package, "CON ", kRb3TitleId, kSavedGame);

    DlcTreeFingerprint before;
    std::string reason;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &before, &reason));

    WritePackage(root / "two", "CON ", kRb3TitleId, kSavedGame);
    DlcTreeFingerprint added;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &added, &reason));
    CHECK_TRUE(added != before);

    WritePackage(root / "two", "CON ", kRb3TitleId, kMarketplace);
    DlcTreeFingerprint resized;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &resized, &reason));
    CHECK_TRUE(resized != added);

    std::error_code ec;
    fs::remove(root / "two", ec);
    DlcTreeFingerprint removed;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &removed, &reason));
    CHECK_TRUE(removed != added);
    CHECK_TRUE(removed.file_count == 1);

    WriteFile(package, "a same-size but different mtime");  // rewrite in place
    DlcTreeFingerprint touched;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &touched, &reason));
    CHECK_TRUE(touched != removed);
  }

  BeginCase("the fingerprint distinguishes two roots that hold the same relative path");
  {
    Scratch s;
    const fs::path a = s.base / "a";
    const fs::path b = s.base / "b";
    WritePackage(a / "song", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(b / "song", "CON ", kRb3TitleId, kMarketplace);

    DlcTreeFingerprint ab;
    std::string reason;
    CHECK_TRUE(ComputeLibraryFingerprint({a, b}, &ab, &reason));
    DlcTreeFingerprint ba;
    CHECK_TRUE(ComputeLibraryFingerprint({b, a}, &ba, &reason));
    CHECK_TRUE(ab != ba);
  }

  BeginCase("a scan's result survives serialize and parse");
  {
    Scratch s;
    const fs::path root = s.base / "library";
    WritePackage(root / "Adele - Skyfall", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(root / "nested" / "A song with a name longer than the guest can carry, ok",
                 "LIVE", kRb3TitleId, kMarketplace);
    WriteFile(root / "badsongs" / "not really", "still not a package");
    WriteFile(root / "broken", "CON ");  // carries the magic, too short to mount: rejected

    DlcTreeFingerprint fingerprint;
    std::string reason;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &fingerprint, &reason));
    const LibraryScanResult scan = ScanDlcLibraries({root});
    const DlcLibraryCache written =
        MakeDlcLibraryCache({root}, kMarketplace, fingerprint, scan);

    const std::vector<uint8_t> bytes = SerializeDlcCache(written);
    DlcLibraryCache read;
    CHECK_TRUE(ParseDlcCache(bytes.data(), bytes.size(), &read, &reason));

    CHECK_TRUE(read.fingerprint == written.fingerprint);
    CHECK_TRUE(read.roots == written.roots);
    CHECK_EQ(read.force_content_type, kMarketplace);
    CHECK_EQ(read.files_seen, written.files_seen);
    CHECK_EQ(read.other_files, written.other_files);
    CHECK_EQ(read.adapted_saved_games, written.adapted_saved_games);
    CHECK_EQ(read.items.size(), written.items.size());
    CHECK_EQ(read.rejected.size(), written.rejected.size());
    for (std::size_t i = 0; i < written.items.size(); ++i) {
      CHECK_EQ(read.items[i].title_id, written.items[i].title_id);
      CHECK_EQ(read.items[i].content_type, written.items[i].content_type);
      CHECK_TRUE(read.items[i].file_name == written.items[i].file_name);
      CHECK_TRUE(read.items[i].host_path == written.items[i].host_path);
    }
    CHECK_EQ(read.rejected.size(), 1);
    CHECK_TRUE(read.rejected[0].reason == written.rejected[0].reason);
  }

  BeginCase("a saved cache loads and matches its own tree");
  {
    Scratch s;
    const fs::path root = s.base / "library";
    WritePackage(root / "one", "CON ", kRb3TitleId, kSavedGame);
    WritePackage(root / "two", "CON ", kRb3TitleId, kSavedGame);

    DlcTreeFingerprint fingerprint;
    std::string reason;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &fingerprint, &reason));
    const DlcLibraryCache cache =
        MakeDlcLibraryCache({root}, 0, fingerprint, ScanDlcLibraries({root}));

    const fs::path file = DlcCachePath(s.base / "cache");
    CHECK_TRUE(SaveDlcCache(file, cache, &reason));
    CHECK_TRUE(fs::exists(file));

    DlcLibraryCache loaded;
    CHECK_TRUE(LoadDlcCache(file, &loaded, &reason));
    CHECK_TRUE(DlcCacheMatches(loaded, {root}, 0, fingerprint, &reason));
  }

  BeginCase("a match fails, with a reason, on every change D8 lists");
  {
    Scratch s;
    const fs::path root = s.base / "library";
    WritePackage(root / "one", "CON ", kRb3TitleId, kSavedGame);

    DlcTreeFingerprint fingerprint;
    std::string reason;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &fingerprint, &reason));
    const DlcLibraryCache cache =
        MakeDlcLibraryCache({root}, 0, fingerprint, ScanDlcLibraries({root}));

    // A changed tree: the cached fingerprint no longer describes it.
    WritePackage(root / "two", "CON ", kRb3TitleId, kSavedGame);
    DlcTreeFingerprint moved;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &moved, &reason));
    CHECK_FALSE(DlcCacheMatches(cache, {root}, 0, moved, &reason));
    CHECK_TRUE(!reason.empty());

    // A deleted package moves the fingerprint the other way.
    std::error_code ec;
    fs::remove(root / "two", ec);
    fs::remove(root / "one", ec);
    DlcTreeFingerprint emptied;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &emptied, &reason));
    CHECK_FALSE(DlcCacheMatches(cache, {root}, 0, emptied, &reason));

    // The content-type override is part of what the scan means.
    CHECK_FALSE(DlcCacheMatches(cache, {root}, kMarketplace, fingerprint, &reason));

    // A different library list is a different source.
    CHECK_FALSE(DlcCacheMatches(cache, {root, s.base / "other"}, 0, fingerprint, &reason));
  }

  BeginCase("a damaged or absent cache file is a miss, never an empty enumeration");
  {
    Scratch s;
    const fs::path file = DlcCachePath(s.base / "cache");

    DlcLibraryCache cache;
    std::string reason;
    CHECK_FALSE(LoadDlcCache(file, &cache, &reason));  // absent
    CHECK_TRUE(!reason.empty());

    WriteFile(file, "");
    CHECK_FALSE(LoadDlcCache(file, &cache, &reason));  // empty
    CHECK_TRUE(!reason.empty());

    WriteFile(file, "this is not a cache at all");
    CHECK_FALSE(LoadDlcCache(file, &cache, &reason));  // bad magic
    CHECK_TRUE(!reason.empty());

    // A cache whose version changed is discarded rather than misread.
    DlcLibraryCache real;
    real.roots = {s.base / "library"};
    real.fingerprint.hash = 0x1234;
    const std::vector<uint8_t> bytes = SerializeDlcCache(real);
    WriteFile(file, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    CHECK_TRUE(LoadDlcCache(file, &cache, &reason));
    CHECK_EQ(cache.fingerprint.hash, 0x1234);

    std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
    WriteFile(file, std::string(reinterpret_cast<const char*>(truncated.data()),
                                truncated.size()));
    CHECK_FALSE(LoadDlcCache(file, &cache, &reason));  // truncated body
    CHECK_TRUE(!reason.empty());
  }

  BeginCase("a refresh rewrites the cache to describe the new tree");
  {
    Scratch s;
    const fs::path root = s.base / "library";
    const fs::path file = DlcCachePath(s.base / "cache");
    WritePackage(root / "one", "CON ", kRb3TitleId, kSavedGame);

    std::string reason;
    DlcTreeFingerprint before;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &before, &reason));
    CHECK_TRUE(SaveDlcCache(file, MakeDlcLibraryCache({root}, 0, before,
                                                      ScanDlcLibraries({root})),
                            &reason));

    // The library changes, then a refresh scans again and rewrites the cache.
    WritePackage(root / "two", "CON ", kRb3TitleId, kSavedGame);
    DlcTreeFingerprint after;
    CHECK_TRUE(ComputeLibraryFingerprint({root}, &after, &reason));

    DlcLibraryCache stale;
    CHECK_TRUE(LoadDlcCache(file, &stale, &reason));
    CHECK_TRUE(!DlcCacheMatches(stale, {root}, 0, after, &reason));  // stale before refresh

    CHECK_TRUE(SaveDlcCache(file, MakeDlcLibraryCache({root}, 0, after,
                                                      ScanDlcLibraries({root})),
                            &reason));

    DlcLibraryCache refreshed;
    CHECK_TRUE(LoadDlcCache(file, &refreshed, &reason));
    CHECK_TRUE(DlcCacheMatches(refreshed, {root}, 0, after, &reason));
    CHECK_EQ(refreshed.items.size(), 2);
  }

  return Finish();
}
