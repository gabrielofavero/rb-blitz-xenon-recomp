// Tests for the installer helper.
//
// These cover the layers the wizard cannot verify on its own: path safety, the
// two container readers (deflate/zip and STFS), the config that gets embedded in
// the helper, and the install pipeline (plan -> verify -> commit -> finalize).
//
// Everything runs against data built in a scratch directory under %TEMP%: no
// game data, no mod payload and no downloaded archive is checked in. Only the
// three config files the installer pins (config/pins.toml,
// config/game_fingerprints.toml, config/ultimate_fingerprints.toml) are read
// from the working tree, because drift between those and the embedded copies is
// the failure this suite exists to catch.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "check.h"
#include "commands.h"
#include "config.h"
#include "inflate.h"
#include "install.h"
#include "stfs.h"
#include "util.h"
#include "zip.h"

#include "util/game_fingerprint.h"
#include "util/sha256.h"

namespace fs = std::filesystem;

using namespace rb_blitz::installer;
using rb_blitz::fingerprint::FileFingerprint;
using rb_blitz::fingerprint::GameFingerprint;
using rb_blitz::test::BeginCase;
using rb_blitz::util::HashFileHex;

namespace {

fs::path g_root;

// --- tiny helpers -----------------------------------------------------------

void CheckStringEq(const char* file, int line, std::string_view actual, std::string_view expected) {
  if (actual == expected) {
    return;
  }
  rb_blitz::test::Fail(file, line,
                       "expected \"" + std::string(expected) + "\", got \"" + std::string(actual) +
                           "\"");
}

void CheckContains(const char* file, int line, std::string_view haystack,
                   std::string_view needle) {
  if (haystack.find(needle) != std::string_view::npos) {
    return;
  }
  rb_blitz::test::Fail(file, line, "expected to find \"" + std::string(needle) + "\" in \"" +
                                       std::string(haystack) + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStringEq(__FILE__, __LINE__, actual, expected)
#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, haystack, needle)

std::string ReadFileOrEmpty(const fs::path& path) {
  std::string text;
  ReadFileText(path, &text, nullptr);
  return text;
}

std::vector<std::uint8_t> PayloadBytes() {
  static const std::string body = "Rock Band Blitz: inflate round trip. 0123456789\n";
  std::string text;
  for (int i = 0; i < 20; ++i) {
    text += body;
  }
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

void WriteText(const fs::path& path, std::string_view text) {
  std::string error;
  if (!EnsureParentDirectory(path, &error) ||
      !WriteFileText(path, text, false, &error)) {
    rb_blitz::test::Fail(__FILE__, __LINE__, "cannot write " + path.string() + ": " + error);
  }
}

void WriteBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
  std::string error;
  if (!EnsureParentDirectory(path, &error) ||
      !WriteFileBytes(path, bytes.data(), bytes.size(), false, &error)) {
    rb_blitz::test::Fail(__FILE__, __LINE__, "cannot write " + path.string() + ": " + error);
  }
}

void RemovePath(const fs::path& path) {
  std::string error;
  if (!RemoveTree(path, &error)) {
    rb_blitz::test::Fail(__FILE__, __LINE__, "cannot remove " + path.string() + ": " + error);
  }
}

void AppendBytes(std::vector<std::uint8_t>* out, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
}

void AppendString(std::vector<std::uint8_t>* out, std::string_view text) {
  AppendBytes(out, text.data(), text.size());
}

void AppendU16(std::vector<std::uint8_t>* out, std::uint16_t value) {
  out->push_back(static_cast<std::uint8_t>(value & 0xFF));
  out->push_back(static_cast<std::uint8_t>(value >> 8));
}

void AppendU32(std::vector<std::uint8_t>* out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out->push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

// --- a zip writer, so the tests do not depend on a zip library --------------

struct ZipMember {
  std::string name;                    // a trailing '/' marks a directory
  std::vector<std::uint8_t> data;      // uncompressed bytes
  std::vector<std::uint8_t> deflated;  // when set, the member is deflate-compressed
};

std::vector<std::uint8_t> BuildZip(const std::vector<ZipMember>& members) {
  std::vector<std::uint8_t> out;
  std::vector<std::uint8_t> central;

  for (const ZipMember& member : members) {
    const bool directory = !member.name.empty() && member.name.back() == '/';
    const bool deflated = !member.deflated.empty();
    const std::vector<std::uint8_t>& body = deflated ? member.deflated : member.data;
    const std::uint32_t crc =
        directory || member.data.empty() ? 0 : Crc32(member.data.data(), member.data.size());
    const std::uint64_t uncompressed = directory ? 0 : member.data.size();
    const std::uint64_t compressed = directory ? 0 : body.size();
    const std::uint16_t method = static_cast<std::uint16_t>(deflated ? 8 : 0);
    const auto offset = static_cast<std::uint32_t>(out.size());

    AppendU32(&out, 0x04034B50);
    AppendU16(&out, 20);
    AppendU16(&out, 0);
    AppendU16(&out, method);
    AppendU16(&out, 0);
    AppendU16(&out, 0x0021);
    AppendU32(&out, crc);
    AppendU32(&out, static_cast<std::uint32_t>(compressed));
    AppendU32(&out, static_cast<std::uint32_t>(uncompressed));
    AppendU16(&out, static_cast<std::uint16_t>(member.name.size()));
    AppendU16(&out, 0);
    AppendString(&out, member.name);
    AppendBytes(&out, body.data(), body.size());

    AppendU32(&central, 0x02014B50);
    AppendU16(&central, 20);
    AppendU16(&central, 20);
    AppendU16(&central, 0);
    AppendU16(&central, method);
    AppendU16(&central, 0);
    AppendU16(&central, 0x0021);
    AppendU32(&central, crc);
    AppendU32(&central, static_cast<std::uint32_t>(compressed));
    AppendU32(&central, static_cast<std::uint32_t>(uncompressed));
    AppendU16(&central, static_cast<std::uint16_t>(member.name.size()));
    AppendU16(&central, 0);
    AppendU16(&central, 0);
    AppendU16(&central, 0);
    AppendU16(&central, 0);
    AppendU32(&central, 0);
    AppendU32(&central, offset);
    AppendString(&central, member.name);
  }

  const auto directory_offset = static_cast<std::uint32_t>(out.size());
  AppendBytes(&out, central.data(), central.size());
  AppendU32(&out, 0x06054B50);
  AppendU16(&out, 0);
  AppendU16(&out, 0);
  AppendU16(&out, static_cast<std::uint16_t>(members.size()));
  AppendU16(&out, static_cast<std::uint16_t>(members.size()));
  AppendU32(&out, static_cast<std::uint32_t>(central.size()));
  AppendU32(&out, directory_offset);
  AppendU16(&out, 0);
  return out;
}

ZipMember StoredMember(std::string name, std::string_view text) {
  ZipMember member;
  member.name = std::move(name);
  member.data.assign(text.begin(), text.end());
  return member;
}

// The deflate stream a real zlib encoder produced for the round-trip payload,
// so the decoder is exercised against encoder output instead of something
// hand-written.
const std::vector<std::uint8_t>& DynamicDeflateStream() {
  static const std::vector<std::uint8_t> kDynamic{
      0x0B, 0xCA, 0x4F, 0xCE, 0x56, 0x70, 0x4A, 0xCC, 0x4B, 0x51, 0x70, 0xCA, 0xC9, 0x2C, 0xA9,
      0xB2, 0x52, 0xC8, 0xCC, 0x4B, 0xCB, 0x49, 0x2C, 0x49, 0x55, 0x28, 0xCA, 0x2F, 0x05, 0x0A,
      0x96, 0x14, 0x65, 0x16, 0xE8, 0x29, 0x18, 0x18, 0x1A, 0x19, 0x9B, 0x98, 0x9A, 0x99, 0x5B,
      0x58, 0x72, 0x05, 0x8D, 0xAA, 0x1F, 0x55, 0x3F, 0xAA, 0x7E, 0xD0, 0xA8, 0x07, 0x00};
  return kDynamic;
}

ZipMember DeflatedMember(std::string name, std::string_view text) {
  const std::vector<std::uint8_t> payload(text.begin(), text.end());
  // Only the round-trip payload has a hand-built deflate stream; anything else
  // is stored so the builder stays generic.
  if (payload != PayloadBytes()) {
    return StoredMember(std::move(name), text);
  }
  ZipMember member;
  member.name = std::move(name);
  member.data = payload;
  member.deflated = DynamicDeflateStream();
  return member;
}

// --- an STFS writer, so the tests do not depend on a downloaded package -----

struct StfsMember {
  std::string name;
  std::vector<std::uint8_t> data;
  int parent = -1;  // index into the member list; -1 is the volume root
  bool directory = false;
};

std::vector<std::uint8_t> BuildStfs(const std::vector<StfsMember>& members) {
  constexpr std::uint32_t kBlock = 0x1000;
  constexpr std::uint32_t kGroupBlocks = 170;  // logical blocks before a hash block
  constexpr std::uint32_t kHashBlocks = 1;     // read-only package: one per group

  const auto physical = [](std::uint32_t block) {
    return block + (block + kGroupBlocks) / kGroupBlocks * kHashBlocks;
  };
  const auto blocks_for = [](std::size_t size) {
    return static_cast<std::uint32_t>((size + kBlock - 1) / kBlock);
  };

  std::vector<std::uint32_t> start(members.size(), 0);
  std::vector<std::uint32_t> blocks(members.size(), 0);
  std::uint32_t next = 1;  // block 0 is the file table
  for (std::size_t i = 0; i < members.size(); ++i) {
    blocks[i] = members[i].directory ? 0 : blocks_for(members[i].data.size());
    if (blocks[i] != 0) {
      start[i] = next;
      next += blocks[i];
    }
  }

  std::vector<std::uint8_t> image(kBlock + (physical(next - 1) + 1) * kBlock, 0);

  const auto put16le = [&image](std::size_t offset, std::uint16_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xFF);
    image[offset + 1] = static_cast<std::uint8_t>(value >> 8);
  };
  const auto put24le = [&image](std::size_t offset, std::uint32_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xFF);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    image[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  };
  const auto put16be = [&image](std::size_t offset, std::uint16_t value) {
    image[offset] = static_cast<std::uint8_t>(value >> 8);
    image[offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
  };
  const auto put32be = [&image](std::size_t offset, std::uint32_t value) {
    image[offset] = static_cast<std::uint8_t>(value >> 24);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    image[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    image[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
  };

  image[0] = 'L';
  image[1] = 'I';
  image[2] = 'V';
  image[3] = 'E';
  put32be(0x340, kBlock);            // header size -> data starts at 0x1000
  put32be(0x344, 1);                 // content type: saved game
  put32be(0x354, 0x78492654);        // media id
  put32be(0x360, 0x58412D2D);        // title id
  image[0x379 + 2] = 0x01;           // volume flags: read-only format
  put16le(0x379 + 3, 1);             // file table block count
  put24le(0x379 + 5, 0);             // file table block number
  put32be(0x379 + 0x1C, kGroupBlocks);
  put32be(0x379 + 0x20, 0);          // free blocks
  put32be(0x379 + 0x24, static_cast<std::uint32_t>(members.size()));

  std::size_t entry = kBlock + static_cast<std::size_t>(physical(0)) * kBlock;
  for (std::size_t i = 0; i < members.size(); ++i) {
    const StfsMember& member = members[i];
    if (member.name.empty() || member.name.size() > 0x28) {
      rb_blitz::test::Fail(__FILE__, __LINE__, "unsupported test entry name");
      return image;
    }
    std::memcpy(&image[entry], member.name.data(), member.name.size());
    std::uint8_t flags = static_cast<std::uint8_t>(member.name.size());
    flags |= member.directory ? 0x80 : 0x40;
    image[entry + 0x28] = flags;
    put24le(entry + 0x29, blocks[i]);  // valid blocks
    put24le(entry + 0x2C, blocks[i]);  // allocated blocks
    put24le(entry + 0x2F, start[i]);
    put16be(entry + 0x32, member.parent < 0 ? 0xFFFF : static_cast<std::uint16_t>(member.parent));
    put32be(entry + 0x34, static_cast<std::uint32_t>(member.data.size()));
    entry += 0x40;
  }

  for (std::size_t i = 0; i < members.size(); ++i) {
    for (std::uint32_t block = 0; block < blocks[i]; ++block) {
      const std::size_t done = static_cast<std::size_t>(block) * kBlock;
      const std::size_t take = std::min<std::size_t>(kBlock, members[i].data.size() - done);
      std::memcpy(&image[kBlock + static_cast<std::size_t>(physical(start[i] + block)) * kBlock],
                  members[i].data.data() + done, take);
    }
  }
  return image;
}

std::vector<std::uint8_t> GamePackageBytes() {
  std::vector<StfsMember> members;
  StfsMember gen;
  gen.name = "gen";
  gen.directory = true;
  members.push_back(gen);
  members.push_back({"default.xex", std::vector<std::uint8_t>(512, 0x11), -1, false});
  members.push_back({"main_xbox.hdr", std::vector<std::uint8_t>(32, 0x22), 0, false});
  members.push_back(
      {"main_xbox_0.ark", std::vector<std::uint8_t>(5000, 0x33), 0, false});
  StfsMember arcade;
  arcade.name = "ArcadeInfo.xml";
  arcade.data = std::vector<std::uint8_t>(16, 0x44);
  members.push_back(arcade);
  return BuildStfs(members);
}

}  // namespace

// ---------------------------------------------------------------------------

static void TestUtil() {
  BeginCase("safe relative paths");

  const char* const kAccepted[] = {"default.xex",
                                   "gen/main_xbox_0.ark",
                                   "a/b/c.txt",
                                   "ultimate/gen/patch_xbox.hdr",
                                   "name with spaces/file.txt",
                                   "weird#char!.txt"};
  for (const char* value : kAccepted) {
    std::string error;
    CHECK_FALSE(SafeRelativePath(value, &error).empty());
    CHECK_TRUE(error.empty());
  }

  const char* const kRejected[] = {"",
                                   "..",
                                   "../escape",
                                   "gen/../../escape",
                                   "/absolute",
                                   "//server/share",
                                   "C:/windows",
                                   "a:b",
                                   "gen/",
                                   "gen//file",
                                   "con",
                                   "NUL.txt",
                                   "com3.ark",
                                   "trailing.",
                                   "trailing ",
                                   "bad<char",
                                   "bad>char",
                                   "bad|char",
                                   "bad?char",
                                   "bad*char",
                                   "bad\"char"};
  for (const char* value : kRejected) {
    std::string error;
    CHECK_TRUE(SafeRelativePath(value, &error).empty());
    CHECK_FALSE(error.empty());
  }

  const std::string long_component(300, 'x');
  std::string error;
  CHECK_TRUE(SafeRelativePath(long_component, &error).empty());
  std::string long_path;
  for (int i = 0; i < 40; ++i) {
    if (i != 0) {
      long_path += "/";
    }
    long_path += "component";
  }
  CHECK_TRUE(SafeRelativePath(long_path, &error).empty());

  BeginCase("posix paths and containment");

  CHECK_STR_EQ(PosixPath(fs::path("gen") / "file.ark"), "gen/file.ark");
  CHECK_TRUE(PathIsWithin(g_root, g_root / "install" / "game" / "default.xex"));
  CHECK_FALSE(PathIsWithin(g_root / "install", g_root / "dumps"));
  CHECK_FALSE(PathIsWithin(g_root, fs::temp_directory_path()));

  BeginCase("byte count formatting");

  CHECK_STR_EQ(HumanBytes(0), "0 bytes");
  CHECK_STR_EQ(HumanBytes(1), "1 byte");
  CHECK_STR_EQ(HumanBytes(1023), "1023 bytes");
  CHECK_STR_EQ(HumanBytes(3 * 1024 * 1024), "3.00 MB");
  CHECK_STR_EQ(HumanBytes(361769177), "345.0 MB");
  CHECK_STR_EQ(HexU32(0xDEADBEEFu), "0xDEADBEEF");

  BeginCase("string helpers");

  CHECK_STR_EQ(Lower("MiXeD"), "mixed");
  CHECK_TRUE(EqualsIgnoreCase("Default.XEX", "default.xex"));
  CHECK_STR_EQ(Trim("  spaced\t"), "spaced");
  CHECK_EQ(SplitLines("a\r\nb\nc").size(), 3);
  CHECK_STR_EQ(Join({"a", "b"}, "/"), "a/b");
  CHECK_STR_EQ(Narrow(Widen("C:\\Program Files\\Rock Band Blitz")),
               "C:\\Program Files\\Rock Band Blitz");

  BeginCase("crc32");

  const std::string check = "123456789";
  CHECK_EQ(Crc32(reinterpret_cast<const std::uint8_t*>(check.data()), check.size()), 0xCBF43926u);

  BeginCase("summary files");

  const fs::path summary = g_root / "summary.txt";
  WriteSummaryFile(summary, {{"stage", "game"}, {"detail", ""}});
  const std::string text = ReadFileOrEmpty(summary);
  CHECK_STR_EQ(text, "stage=game;detail=");
  CHECK_STR_EQ(ReadSummaryValue(text, "stage").value_or("<none>"), "game");
  CHECK_STR_EQ(ReadSummaryValue(text, "detail").value_or("<none>"), "");
  CHECK_FALSE(ReadSummaryValue(text, "missing").has_value());

  const fs::path progress = g_root / "progress.txt";
  WriteProgressFile(progress, 250, "clamped");
  CHECK_STR_EQ(ReadFileOrEmpty(progress), "100\nclamped");
}

static void TestInflate() {
  using rb_blitz::installer::InflateRawToVector;
  using rb_blitz::installer::MemorySource;
  using rb_blitz::installer::VectorSink;

  const std::vector<std::uint8_t> payload = PayloadBytes();
  const std::vector<std::uint8_t>& dynamic = DynamicDeflateStream();
  CHECK_EQ(payload.size(), 960u);
  CHECK_EQ(dynamic.size(), 59u);

  BeginCase("raw deflate round trip");

  MemorySource source(dynamic.data(), dynamic.size());
  std::vector<std::uint8_t> out;
  std::uint32_t crc = 0;
  std::string error;
  CHECK_TRUE(InflateRawToVector(source, 0, dynamic.size(), &out, &crc, &error));
  CHECK_MEM_EQ(out.data(), payload.data(), payload.size());
  CHECK_EQ(crc, 0x7ACAEF4Au);

  BeginCase("stored deflate blocks round trip");

  std::vector<std::uint8_t> stored{0x01};
  AppendU16(&stored, static_cast<std::uint16_t>(payload.size()));
  AppendU16(&stored, static_cast<std::uint16_t>(~payload.size()));
  AppendBytes(&stored, payload.data(), payload.size());
  MemorySource stored_source(stored.data(), stored.size());
  std::vector<std::uint8_t> stored_out;
  CHECK_TRUE(InflateRawToVector(stored_source, 0, stored.size(), &stored_out, &crc, &error));
  CHECK_MEM_EQ(stored_out.data(), payload.data(), payload.size());
  CHECK_EQ(crc, 0x7ACAEF4Au);

  BeginCase("deflate through a file sink");

  const fs::path target = g_root / "inflate/round-trip.bin";
  rb_blitz::installer::FileSink sink;
  CHECK_TRUE(EnsureParentDirectory(target, &error));
  CHECK_TRUE(sink.Open(target, &error));
  CHECK_TRUE(InflateRaw(source, 0, dynamic.size(), 0, std::ref(sink), nullptr, &error));
  CHECK_TRUE(sink.Close(&error));
  CHECK_EQ(sink.bytes_written(), payload.size());
  std::vector<std::uint8_t> written;
  CHECK_TRUE(ReadFileBytes(target, &written, &error));
  CHECK_EQ(written.size(), payload.size());
  CHECK_MEM_EQ(written.data(), payload.data(), payload.size());

  BeginCase("deflate respects the output ceiling");

  VectorSink ceiling;
  CHECK_FALSE(InflateRaw(source, 0, dynamic.size(), 100, std::ref(ceiling), nullptr, &error));
  CHECK_FALSE(error.empty());

  BeginCase("deflate rejects broken input");

  const std::uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  MemorySource bad(garbage, sizeof(garbage));
  CHECK_FALSE(InflateRawToVector(bad, 0, sizeof(garbage), &out, &crc, &error));
  CHECK_FALSE(error.empty());

  MemorySource truncated(dynamic.data(), 4);
  CHECK_FALSE(InflateRawToVector(truncated, 0, 4, &out, &crc, &error));
  CHECK_FALSE(error.empty());
}

static void TestZip() {
  using rb_blitz::installer::MemorySource;
  using rb_blitz::installer::VectorSink;
  using rb_blitz::installer::ZipArchive;

  const std::vector<std::uint8_t> payload = PayloadBytes();
  const std::string payload_text(payload.begin(), payload.end());

  const std::vector<ZipMember> members{
      StoredMember("Xbox/gen/patch_xbox.hdr", "header"),
      DeflatedMember("Xbox/gen/patch_xbox_0.ark", payload_text),
      StoredMember("Xbox/screenshots/", ""),
      StoredMember("readme.txt", "hello")};

  BeginCase("zip entry table");

  auto bytes = BuildZip(members);
  ZipArchive archive;
  std::string error;
  CHECK_TRUE(archive.Open(std::make_unique<MemorySource>(bytes.data(), bytes.size()), &error));
  CHECK_EQ(archive.entries().size(), members.size());

  const rb_blitz::installer::ZipEntry* header = archive.Find("Xbox/gen/patch_xbox.hdr");
  CHECK_TRUE(header != nullptr);
  CHECK_FALSE(header->is_directory);
  CHECK_EQ(header->method, 0u);
  CHECK_EQ(header->uncompressed_size, 6u);

  const rb_blitz::installer::ZipEntry* data = archive.Find("Xbox/gen/patch_xbox_0.ark");
  CHECK_TRUE(data != nullptr);
  CHECK_EQ(data->method, 8u);
  CHECK_EQ(data->uncompressed_size, payload.size());

  const rb_blitz::installer::ZipEntry* screenshots = archive.Find("Xbox/screenshots");
  CHECK_TRUE(screenshots != nullptr);
  CHECK_TRUE(screenshots->is_directory);

  CHECK_TRUE(archive.Find("Xbox/missing.ark") == nullptr);

  BeginCase("zip extraction");

  VectorSink stored;
  CHECK_TRUE(archive.Extract(*header, std::ref(stored), &error));
  CHECK_STR_EQ(std::string(stored.bytes().begin(), stored.bytes().end()), "header");

  VectorSink inflated;
  CHECK_TRUE(archive.Extract(*data, std::ref(inflated), &error));
  CHECK_MEM_EQ(inflated.bytes().data(), payload.data(), payload.size());

  BeginCase("zip extraction to a tree");

  const fs::path destination = g_root / "zip/extracted";
  std::vector<std::string> visited;
  CHECK_TRUE(archive.ExtractAll(
      destination, [](const ZipEntry& entry) { return entry.name.rfind("Xbox/", 0) == 0; },
      [&visited](const rb_blitz::installer::ZipEntry& entry) { visited.push_back(entry.name); },
      &error));
  CHECK_FALSE(DirectoryExists(destination / "readme.txt"));
  CHECK_TRUE(FileExists(destination / "Xbox" / "gen" / "patch_xbox.hdr"));
  CHECK_EQ(FileSizeOrZero(destination / "Xbox" / "gen" / "patch_xbox_0.ark"), payload.size());
  CHECK_TRUE(DirectoryExists(destination / "Xbox" / "screenshots"));
  CHECK_TRUE(std::find(visited.begin(), visited.end(), "Xbox/gen/patch_xbox.hdr") != visited.end());
  CHECK_TRUE(std::find(visited.begin(), visited.end(), "readme.txt") == visited.end());

  BeginCase("zip rejects a corrupted member");

  // Flip a byte in the stored member's payload: the sizes still match, so only
  // the CRC-32 check can catch it.
  std::vector<std::uint8_t> corrupt = bytes;
  const auto* raw = corrupt.data();
  bool flipped = false;
  for (std::size_t i = 0; i + 6 <= corrupt.size(); ++i) {
    if (std::memcmp(raw + i, "header", 6) == 0) {
      corrupt[i] ^= 0x20;
      flipped = true;
      break;
    }
  }
  CHECK_TRUE(flipped);
  ZipArchive broken;
  CHECK_TRUE(broken.Open(std::make_unique<MemorySource>(corrupt.data(), corrupt.size()), &error));
  const rb_blitz::installer::ZipEntry* broken_header = broken.Find("Xbox/gen/patch_xbox.hdr");
  CHECK_TRUE(broken_header != nullptr);
  VectorSink sink;
  CHECK_FALSE(broken.Extract(*broken_header, std::ref(sink), &error));
  CHECK_CONTAINS(error, "CRC-32");

  BeginCase("zip rejects escaping names");

  const std::vector<std::uint8_t> escaping =
      BuildZip({StoredMember("../escape.txt", "nope"), StoredMember("Xbox/ok.txt", "ok")});
  ZipArchive unsafe;
  CHECK_FALSE(unsafe.Open(std::make_unique<MemorySource>(escaping.data(), escaping.size()), &error));
  CHECK_FALSE(error.empty());

  BeginCase("zip rejects a truncated file");

  ZipArchive truncated;
  CHECK_FALSE(truncated.Open(std::make_unique<MemorySource>(bytes.data(), 40), &error));
  CHECK_FALSE(error.empty());
}

static void TestStfs() {
  BeginCase("stfs container");

  const std::vector<std::uint8_t> image = GamePackageBytes();
  const fs::path package = g_root / "package/synthetic.stfs";
  WriteBytes(package, image);

  rb_blitz::installer::StfsContainer container;
  std::string error;
  CHECK_TRUE(container.Open(package, &error));

  const rb_blitz::installer::StfsInfo& info = container.info();
  CHECK_EQ(info.title_id, 0x58412D2Du);
  CHECK_EQ(info.media_id, 0x78492654u);
  CHECK_EQ(info.data_file_count, 5u);
  CHECK_TRUE(info.read_only_format);
  CHECK_EQ(info.data_offset, 0x1000u);

  CHECK_EQ(container.entries().size(), 5u);
  const rb_blitz::installer::StfsEntry* gen = container.Find("gen");
  CHECK_TRUE(gen != nullptr);
  CHECK_TRUE(gen->is_directory);
  const rb_blitz::installer::StfsEntry* header = container.Find("gen/main_xbox.hdr");
  CHECK_TRUE(header != nullptr);
  CHECK_EQ(header->length, 32u);
  CHECK_FALSE(header->is_directory);
  const rb_blitz::installer::StfsEntry* ark = container.Find("gen/main_xbox_0.ark");
  CHECK_TRUE(ark != nullptr);
  CHECK_EQ(ark->length, 5000u);
  CHECK_TRUE(ark->contiguous);
  CHECK_EQ(ark->valid_blocks, 2u);
  CHECK_TRUE(container.Find("gen/") == nullptr);
  CHECK_TRUE(container.Find("gen/nothing") == nullptr);

  BeginCase("stfs extraction");

  const fs::path out_root = g_root / "stfs/extracted";
  const fs::path ark_target = out_root / "gen" / "main_xbox_0.ark";
  CHECK_TRUE(EnsureParentDirectory(ark_target, &error));
  CHECK_TRUE(container.ExtractToFile(*ark, ark_target, &error));
  CHECK_EQ(FileSizeOrZero(ark_target), 5000u);
  std::vector<std::uint8_t> extracted;
  CHECK_TRUE(ReadFileBytes(ark_target, &extracted, &error));
  CHECK_TRUE(std::all_of(extracted.begin(), extracted.end(),
                         [](std::uint8_t value) { return value == 0x33; }));

  const fs::path xex_target = out_root / "default.xex";
  CHECK_TRUE(container.ExtractToFile(*container.Find("default.xex"), xex_target, &error));
  CHECK_EQ(FileSizeOrZero(xex_target), 512u);

  BeginCase("stfs rejects a file that is not a package");

  const fs::path not_a_package = g_root / "package/garbage.bin";
  WriteBytes(not_a_package, std::vector<std::uint8_t>(0x4000, 0xAB));
  rb_blitz::installer::StfsContainer garbage;
  CHECK_FALSE(garbage.Open(not_a_package, &error));
  CHECK_FALSE(error.empty());

  rb_blitz::installer::StfsContainer missing;
  CHECK_FALSE(missing.Open(g_root / "package/nope.stfs", &error));
  CHECK_FALSE(error.empty());
}

static void TestConfig() {
  BeginCase("toml subset parser");

  const std::string text =
      "# leading comment\n"
      "schema_version = 1\n"
      "\n"
      "[installer]\n"
      "name = \"Rock Band Blitz (Xenon recomp)\"\n"
      "count = 42\n"
      "enabled = true\n"
      "\n"
      "[[files]]\n"
      "role = \"entrypoint\"\n"
      "\n"
      "[[files]]\n"
      "role = \"archive-data\"\n";
  rb_blitz::installer::TomlDocument document;
  std::string error;
  CHECK_TRUE(rb_blitz::installer::ParseTomlSubset(text, &document, &error));

  const rb_blitz::installer::TomlTable* installer = document.Find("installer");
  CHECK_TRUE(installer != nullptr);
  CHECK_FALSE(installer->is_array);
  CHECK_STR_EQ(installer->GetString("name", "<none>"), "Rock Band Blitz (Xenon recomp)");
  CHECK_EQ(installer->GetUnsigned("count", 0), 42u);
  CHECK_TRUE(installer->GetBool("enabled", false));
  CHECK_FALSE(installer->GetBool("missing", false));
  CHECK_TRUE(installer->GetBool("missing", true));
  CHECK_EQ(installer->GetUnsigned("missing", 7), 7u);
  CHECK_STR_EQ(installer->GetString("missing", "<none>"), "<none>");
  CHECK_TRUE(document.Find("nothing") == nullptr);

  const rb_blitz::installer::TomlTable* files = document.Find("files");
  CHECK_TRUE(files != nullptr);
  CHECK_TRUE(files->is_array);

  BeginCase("the pinned versions match the embedded copies");

  rb_blitz::installer::Pins pins;
  const fs::path pins_path = fs::path(RBBLITZ_INSTALLER_CONFIG_DIR) / "pins.toml";
  CHECK_TRUE(rb_blitz::installer::ParsePins(ReadFileOrEmpty(pins_path), &pins, &error));
  CHECK_EQ(pins.schema_version, 1u);
  CHECK_STR_EQ(pins.installer.short_name, "Rock Band Blitz");
  CHECK_STR_EQ(pins.installer.default_dir_name, "Rock Band Blitz");
  CHECK_STR_EQ(pins.installer.min_windows_build, "10.0.17763");
  CHECK_STR_EQ(pins.ultimate.version, "2.11");
  CHECK_EQ(pins.ultimate.size, 10366787u);
  CHECK_STR_EQ(pins.ultimate.archive_prefix, "Xbox");
  CHECK_STR_EQ(pins.ultimate.destination_dir, "ultimate");
  CHECK_TRUE(pins.Validate(&error));

  const rb_blitz::installer::Pins embedded = rb_blitz::installer::EmbeddedPins();
  CHECK_STR_EQ(embedded.installer.name, pins.installer.name);
  CHECK_STR_EQ(embedded.installer.version, pins.installer.version);
  CHECK_STR_EQ(embedded.installer.publisher, pins.installer.publisher);
  CHECK_STR_EQ(embedded.installer.homepage_url, pins.installer.homepage_url);
  CHECK_STR_EQ(embedded.installer.support_url, pins.installer.support_url);
  CHECK_STR_EQ(embedded.payload.url, pins.payload.url);
  CHECK_STR_EQ(embedded.payload.sha256, pins.payload.sha256);
  CHECK_EQ(embedded.payload.size, pins.payload.size);
  CHECK_STR_EQ(embedded.ultimate.url, pins.ultimate.url);
  CHECK_STR_EQ(embedded.ultimate.sha256, pins.ultimate.sha256);
  CHECK_EQ(embedded.ultimate.size, pins.ultimate.size);
  CHECK_STR_EQ(embedded.ultimate.release_url, pins.ultimate.release_url);

  BeginCase("a broken pin set is refused");

  rb_blitz::installer::Pins broken = pins;
  broken.ultimate.url.clear();
  CHECK_FALSE(broken.Validate(&error));
  CHECK_FALSE(error.empty());

  broken = pins;
  broken.installer.name.clear();
  CHECK_FALSE(broken.Validate(&error));

  BeginCase("pins toml that cannot be parsed");

  CHECK_FALSE(rb_blitz::installer::ParsePins("schema_version = 1\n[ultimate]\n", &pins, &error));
  CHECK_FALSE(error.empty());
}

static void TestFingerprints() {
  using rb_blitz::fingerprint::FindByRole;
  using rb_blitz::fingerprint::ParseGameFingerprint;

  std::string error;

  BeginCase("the game fingerprint matches the repository copy");

  GameFingerprint on_disk;
  const fs::path path = fs::path(RBBLITZ_ROOT_DIR) / "config" / "game_fingerprints.toml";
  CHECK_TRUE(rb_blitz::fingerprint::LoadGameFingerprint(path, &on_disk, &error));

  GameFingerprint embedded;
  CHECK_TRUE(ParseGameFingerprint(rb_blitz::installer::EmbeddedGameFingerprints(), &embedded,
                                  &error));
  CHECK_EQ(embedded.schema_version, on_disk.schema_version);
  CHECK_EQ(embedded.files.size(), on_disk.files.size());
  CHECK_EQ(embedded.files.size(), 3u);
  for (std::size_t i = 0; i < on_disk.files.size(); ++i) {
    CHECK_STR_EQ(embedded.files[i].role, on_disk.files[i].role);
    CHECK_STR_EQ(embedded.files[i].path, on_disk.files[i].path);
    CHECK_EQ(embedded.files[i].size, on_disk.files[i].size);
    CHECK_STR_EQ(embedded.files[i].sha256, on_disk.files[i].sha256);
  }

  const FileFingerprint* entrypoint = FindByRole(embedded, "entrypoint");
  CHECK_TRUE(entrypoint != nullptr);
  CHECK_STR_EQ(entrypoint->path, "default.xex");
  CHECK_EQ(entrypoint->size, 9023488u);
  const FileFingerprint* archive_header = FindByRole(embedded, "archive-header");
  CHECK_TRUE(archive_header != nullptr);
  CHECK_STR_EQ(archive_header->path, "gen/main_xbox.hdr");
  CHECK_EQ(archive_header->size, 113528u);
  const FileFingerprint* archive_data = FindByRole(embedded, "archive-data");
  CHECK_TRUE(archive_data != nullptr);
  CHECK_STR_EQ(archive_data->path, "gen/main_xbox_0.ark");
  CHECK_EQ(archive_data->size, 361769177u);

  BeginCase("the roles the installer requires are the ones in the file");

  CHECK_EQ(std::size(kGameRoles), 3u);
  for (std::string_view role : kGameRoles) {
    CHECK_TRUE(FindByRole(embedded, role) != nullptr);
  }
  for (std::string_view role : kUltimateRoles) {
    CHECK_TRUE(FindByRole(embedded, role) == nullptr);
  }

  BeginCase("the ultimate fingerprint matches the repository copy");

  GameFingerprint ultimate;
  CHECK_TRUE(ParseGameFingerprint(rb_blitz::installer::EmbeddedUltimateFingerprints(), &ultimate,
                                  &error));
  GameFingerprint ultimate_disk;
  CHECK_TRUE(rb_blitz::fingerprint::LoadGameFingerprint(
      fs::path(RBBLITZ_INSTALLER_CONFIG_DIR) / "ultimate_fingerprints.toml", &ultimate_disk,
      &error));
  CHECK_EQ(ultimate.files.size(), ultimate_disk.files.size());
  CHECK_EQ(ultimate.files.size(), 3u);
  for (std::size_t i = 0; i < ultimate_disk.files.size(); ++i) {
    CHECK_STR_EQ(ultimate.files[i].path, ultimate_disk.files[i].path);
    CHECK_EQ(ultimate.files[i].size, ultimate_disk.files[i].size);
    CHECK_STR_EQ(ultimate.files[i].sha256, ultimate_disk.files[i].sha256);
  }

  const FileFingerprint* ultimate_header = FindByRole(ultimate, kUltimateRoles[0]);
  CHECK_TRUE(ultimate_header != nullptr);
  CHECK_STR_EQ(ultimate_header->path, "gen/patch_xbox.hdr");
  CHECK_EQ(ultimate_header->size, 4381u);
  const FileFingerprint* ultimate_data = FindByRole(ultimate, kUltimateRoles[1]);
  CHECK_TRUE(ultimate_data != nullptr);
  CHECK_STR_EQ(ultimate_data->path, "gen/patch_xbox_0.ark");
  CHECK_EQ(ultimate_data->size, 14171207u);
}

// A dump folder holding every file a plan can know about. Contents are dummies:
// planning only looks at existence, and hashing is exercised separately.
static fs::path MakeDummyDump() {
  const fs::path dump = g_root / "dumps/Rock Band Blitz";
  WriteText(dump / "default.xex", "not really an executable");
  WriteText(dump / "gen/main_xbox.hdr", "header");
  WriteText(dump / "gen/main_xbox_0.ark", "archive");
  for (std::string_view name : kOptionalGameFiles) {
    WriteText(dump / fs::path(name), "optional");
  }
  return dump;
}

static void TestGamePlan() {
  const fs::path dump = MakeDummyDump();
  const fs::path package = g_root / "package/plan.stfs";
  WriteBytes(package, GamePackageBytes());

  BeginCase("the game folder is planned from the dump or its parent");

  std::string error;
  GameSourcePlan plan;
  CHECK_TRUE(PlanGameFolder(dump, &plan, &error));
  CHECK_TRUE(plan.kind == GameSourceKind::kFolder);
  CHECK_EQ(plan.RequiredCount(), 3u);
  CHECK_EQ(plan.PresentCount(), 6u);
  CHECK_TRUE(plan.Complete());
  CHECK_TRUE(plan.Missing().empty());
  CHECK_TRUE(plan.description.find("folder") != std::string::npos);
  for (const char* path : {"default.xex", "gen/main_xbox.hdr", "gen/main_xbox_0.ark"}) {
    const auto found = std::find_if(plan.files.begin(), plan.files.end(),
                                    [path](const GameFilePlan& file) { return file.path == path; });
    CHECK_TRUE(found != plan.files.end());
    if (found != plan.files.end()) {
      CHECK_TRUE(found->required);
      CHECK_TRUE(found->present);
      CHECK_TRUE(found->size > 0);
    }
  }

  GameSourcePlan from_parent;
  CHECK_TRUE(PlanGameFolder(dump.parent_path(), &from_parent, &error));
  CHECK_TRUE(from_parent.Complete());
  CHECK_STR_EQ(fs::weakly_canonical(from_parent.location).string(),
               fs::weakly_canonical(dump).string());

  BeginCase("a folder without the game is refused");

  const fs::path empty = g_root / "dumps/empty";
  WriteText(empty / "readme.txt", "nothing here");
  GameSourcePlan none;
  CHECK_FALSE(PlanGameFolder(empty, &none, &error));
  CHECK_CONTAINS(error, "default.xex");

  // A directory one level up, with several unrelated children, must not be
  // mistaken for a game root either.
  GameSourcePlan unrelated;
  CHECK_FALSE(PlanGameFolder(g_root, &unrelated, &error));
  CHECK_FALSE(error.empty());

  BeginCase("the game package is planned");

  GameSourcePlan packaged;
  CHECK_TRUE(PlanGamePackage(package, &packaged, &error));
  CHECK_TRUE(packaged.kind == GameSourceKind::kPackage);
  CHECK_EQ(packaged.package_entries, 5u);
  CHECK_EQ(packaged.RequiredCount(), 3u);
  CHECK_EQ(packaged.PresentCount(), 4u);
  CHECK_TRUE(packaged.Complete());
  CHECK_TRUE(packaged.Missing().empty());
  CHECK_EQ(packaged.volume.title_id, 0x58412D2Du);
  CHECK_TRUE(packaged.description.find("package") != std::string::npos);

  BeginCase("a package without the game data is refused");

  const fs::path partial = g_root / "package/partial.stfs";
  WriteBytes(partial, BuildStfs({{"default.xex", std::vector<std::uint8_t>(64, 0x01), -1, false}}));
  GameSourcePlan incomplete;
  CHECK_FALSE(PlanGamePackage(partial, &incomplete, &error));
  CHECK_CONTAINS(error, "does not hold");
}

static void TestGameImport() {
  const fs::path dump = MakeDummyDump();
  const fs::path game_root = g_root / "install/game";
  std::string error;

  BeginCase("an import of the wrong dump commits nothing");

  GameSourcePlan plan;
  CHECK_TRUE(PlanGameFolder(dump, &plan, &error));
  CHECK_FALSE(ImportGame(plan, game_root, ProgressSink{}, &error));
  CHECK_CONTAINS(error, "does not match the supported dump");
  CHECK_FALSE(DirectoryExists(StagingRoot(game_root)));
  CHECK_FALSE(FileExists(game_root / "default.xex"));
  CHECK_FALSE(FileExists(game_root / "gen"));

  BeginCase("an incomplete plan never reaches the disk");

  GameSourcePlan missing = plan;
  missing.files[0].present = false;
  CHECK_FALSE(ImportGame(missing, game_root, ProgressSink{}, &error));
  CHECK_FALSE(error.empty());
  CHECK_FALSE(DirectoryExists(StagingRoot(game_root)));

  BeginCase("an import refuses to read its own output");

  GameSourcePlan overlap;
  overlap.kind = GameSourceKind::kFolder;
  overlap.location = game_root / "default.xex";
  CHECK_FALSE(ImportGame(overlap, game_root, ProgressSink{}, &error));
  CHECK_FALSE(error.empty());

  GameSourcePlan nested;
  nested.kind = GameSourceKind::kFolder;
  nested.location = game_root;
  CHECK_FALSE(ImportGame(nested, game_root, ProgressSink{}, &error));
  CHECK_FALSE(error.empty());
}

static void TestPayloadAndFinalize() {
  const fs::path app = g_root / "install/app";
  const fs::path payload_src = g_root / "payload-src";
  const fs::path game_root = GameRoot(app);
  std::string error;

  BeginCase("a payload folder installs and is verified");

  struct Entry {
    std::string_view name;
    std::string_view text;
  };
  const Entry entries[] = {{kRuntimeExeName, "not really the runtime"},
                           {"rb_blitz.toml", "[backend]\n"},
                           {"rexruntime.dll", "not really a dll"}};

  std::vector<std::string> manifest;
  manifest.push_back("schema_version = 1");
  manifest.push_back("");
  manifest.push_back("[game]");
  manifest.push_back("name = \"rb_blitz test build\"");
  manifest.push_back("");

  std::uint64_t total = 0;
  for (const Entry& entry : entries) {
    const fs::path path = payload_src / entry.name;
    WriteText(path, entry.text);
    std::uint64_t size = 0;
    std::error_code ec;
    const std::string sha = HashFileHex(path, &size, ec);
    CHECK_FALSE(ec);
    total += size;
    manifest.push_back("[[files]]");
    manifest.push_back(S("role = \"payload-", entry.name, "\""));
    manifest.push_back(S("path = \"", entry.name, "\""));
    manifest.push_back(S("size = ", size));
    manifest.push_back(S("sha256 = \"", sha, "\""));
    manifest.push_back("");
  }
  WriteText(payload_src / kPayloadManifestName, Join(manifest, "\n"));

  const Pins embedded = EmbeddedPins();

  SourceRef source;
  source.kind = SourceKind::kFolder;
  source.path = payload_src;

  const fs::path progress_file = g_root / "install/progress.txt";
  ComponentResult payload;
  CHECK_TRUE(InstallPayload(source, app, embedded, ProgressSink{progress_file}, &payload, &error));
  CHECK_EQ(payload.files, 3u);
  CHECK_EQ(payload.bytes, total);
  CHECK_TRUE(payload.fingerprints_matched);
  CHECK_FALSE(payload.version.empty());
  CHECK_CONTAINS(payload.source, "folder");
  CHECK_TRUE(FileExists(app / kRuntimeExeName));
  CHECK_TRUE(FileExists(app / "rexruntime.dll"));
  CHECK_TRUE(FileExists(app / kPayloadManifestName));
  CHECK_TRUE(FileExists(app / "rb_blitz.toml"));
  CHECK_FALSE(DirectoryExists(StagingRoot(app)));
  CHECK_STR_EQ(ReadFileOrEmpty(progress_file), "100\ninstalled");

  BeginCase("the installed payload verifies and detects tampering");

  ComponentResult verify;
  CHECK_TRUE(VerifyPayload(app, embedded, &verify, &error));
  CHECK_EQ(verify.files, 3u);
  CHECK_TRUE(verify.fingerprints_matched);

  WriteText(app / kRuntimeExeName, "tampered");
  CHECK_FALSE(VerifyPayload(app, embedded, &verify, &error));
  CHECK_CONTAINS(error, "does not match");
  WriteText(app / kRuntimeExeName, entries[0].text);

  BeginCase("a payload zip installs the same way a folder does");

  // The download path hands the extractor an archive and no filter, which used
  // to throw before a single file had been written.
  const fs::path payload_zip = g_root / "install/payload.zip";
  std::vector<ZipMember> members;
  members.push_back(StoredMember("recompiled build/", ""));
  for (const Entry& entry : entries) {
    members.push_back(StoredMember(S("recompiled build/", entry.name), entry.text));
  }
  members.push_back(StoredMember(S("recompiled build/", kPayloadManifestName),
                                 ReadFileOrEmpty(payload_src / kPayloadManifestName)));
  WriteBytes(payload_zip, BuildZip(members));

  const fs::path zip_app = g_root / "install/app-zip";
  SourceRef zip_source;
  zip_source.kind = SourceKind::kZip;
  zip_source.path = payload_zip;
  ComponentResult from_zip;
  CHECK_TRUE(InstallPayload(zip_source, zip_app, embedded, ProgressSink{}, &from_zip, &error));
  CHECK_EQ(from_zip.files, 3u);
  CHECK_EQ(from_zip.bytes, total);
  CHECK_CONTAINS(from_zip.source, "archive");
  CHECK_TRUE(FileExists(zip_app / kRuntimeExeName));
  CHECK_TRUE(FileExists(zip_app / "rexruntime.dll"));
  CHECK_TRUE(FileExists(zip_app / kPayloadManifestName));
  CHECK_FALSE(DirectoryExists(StagingRoot(zip_app)));

  BeginCase("the install is finalised into a manifest and a report");

  EnsureDirectory(game_root, nullptr);
  WriteText(game_root / "default.xex", "dummy");

  rb_blitz::installer::log::Open(app / kInstallLogName);
  rb_blitz::installer::log::Info("test entry");

  InstallSummary summary;
  summary.install_dir = app;
  summary.game_dir = game_root;
  summary.game_source = "an extracted folder";
  summary.game_evidence = "default.xex: ok\ngen/main_xbox.hdr: ok";
  summary.payload = payload;
  summary.installer_version = "1.0.0-test";
  CHECK_TRUE(FinalizeInstall(summary, &error));

  const std::string manifest_text = ReadFileOrEmpty(app / kInstallManifestName);
  CHECK_CONTAINS(manifest_text, "schema_version = 1");
  CHECK_CONTAINS(manifest_text, "[install]");
  CHECK_CONTAINS(manifest_text, "ultimate_installed = false");
  CHECK_CONTAINS(manifest_text, "[payload]");
  CHECK_CONTAINS(manifest_text, "1.0.0-test");

  const std::string report_text = ReadFileOrEmpty(app / kInstallReportName);
  CHECK_CONTAINS(report_text, "Rock Band Blitz - install report");
  CHECK_CONTAINS(report_text, "Not installed.");
  CHECK_CONTAINS(report_text, "an extracted folder");
  CHECK_CONTAINS(report_text, "test entry");

  BeginCase("uninstall cleanup removes the game data and the payload build");

  std::vector<std::string> removed;
  CHECK_TRUE(UninstallCleanup(app, false, &removed, &error));
  CHECK_FALSE(DirectoryExists(game_root));
  CHECK_FALSE(FileExists(app / kInstallManifestName));
  CHECK_FALSE(FileExists(app / kInstallReportName));
  CHECK_FALSE(FileExists(app / kInstallLogName));
  CHECK_FALSE(FileExists(app / kPayloadManifestName));
  // The manifest is the only record of a downloaded payload, so the payload
  // files go too and are not left behind for the uninstaller to find.
  CHECK_FALSE(FileExists(app / kRuntimeExeName));
  CHECK_FALSE(FileExists(app / "rexruntime.dll"));
  CHECK_FALSE(removed.empty());

  BeginCase("uninstall cleanup can keep the game data");

  const fs::path app2 = g_root / "install/app2";
  const fs::path game2 = GameRoot(app2);
  EnsureDirectory(game2, nullptr);
  WriteText(game2 / "default.xex", "dummy");
  WriteText(app2 / kInstallManifestName, "schema_version = 1\n");
  std::vector<std::string> kept;
  CHECK_TRUE(UninstallCleanup(app2, true, &kept, &error));
  CHECK_TRUE(FileExists(game2 / "default.xex"));
  CHECK_FALSE(FileExists(app2 / kInstallManifestName));
  // The manifest and the report are always removed; only the game data stays.
  CHECK_EQ(kept.size(), 1u);

  BeginCase("uninstall cleanup tolerates a directory that is already gone");

  CHECK_TRUE(UninstallCleanup(g_root / "install/nothing-here", false, &removed, &error));
}

static void TestUltimate() {
  std::string error;
  const Pins pins = EmbeddedPins();

  BeginCase("the ultimate overlay needs an installed game");

  SourceRef no_game;
  no_game.kind = SourceKind::kFolder;
  no_game.path = g_root / "ultimate-src";
  ComponentResult result;
  CHECK_FALSE(InstallUltimate(no_game, g_root / "install/missing/game", pins, ProgressSink{},
                              &result, &error));
  CHECK_FALSE(error.empty());

  const fs::path game_root = g_root / "ultimate-install/game";
  EnsureDirectory(game_root, nullptr);
  WriteText(game_root / "default.xex", "the user's own executable");

  BeginCase("the ultimate overlay installs from a folder and drops default.xex");

  const fs::path folder = g_root / "ultimate-src";
  WriteText(folder / "gen/patch_xbox.hdr", "fake header");
  WriteText(folder / "gen/patch_xbox_0.ark", "fake archive");
  WriteText(folder / "default.xex", "the mod's own executable");
  WriteText(folder / "screenshots/keepme", "");

  SourceRef from_folder;
  from_folder.kind = SourceKind::kFolder;
  from_folder.path = folder;
  result = ComponentResult{};
  CHECK_TRUE(InstallUltimate(from_folder, game_root, pins, ProgressSink{}, &result, &error));
  CHECK_FALSE(result.fingerprints_matched);
  CHECK_STR_EQ(result.version, "unknown");
  CHECK_EQ(result.notes.size(), 1u);
  CHECK_EQ(result.files, 3u);
  CHECK_TRUE(FileExists(game_root / kUltimateDirName / "gen" / "patch_xbox.hdr"));
  CHECK_TRUE(FileExists(game_root / kUltimateDirName / "gen" / "patch_xbox_0.ark"));
  CHECK_TRUE(FileExists(game_root / kUltimateDirName / "screenshots" / "keepme"));
  CHECK_FALSE(FileExists(game_root / kUltimateDirName / "default.xex"));
  CHECK_FALSE(DirectoryExists(StagingRoot(game_root)));

  BeginCase("the ultimate overlay installs from an archive named like the release");

  const fs::path archive = g_root / "ultimate-src/Rock-Band-Blitz-Ultimate-2.11-Xbox.zip";
  const std::vector<ZipMember> members{
      StoredMember("Xbox/gen/patch_xbox.hdr", "fake header"),
      StoredMember("Xbox/gen/patch_xbox_0.ark", "fake archive"),
      StoredMember("Xbox/default.xex", "the mod's own executable"),
      StoredMember("Xbox/screenshots/keepme", ""),
      StoredMember("readme.txt", "not part of the payload")};
  WriteBytes(archive, BuildZip(members));

  SourceRef from_archive;
  from_archive.kind = SourceKind::kZip;
  from_archive.path = archive;
  result = ComponentResult{};
  CHECK_TRUE(InstallUltimate(from_archive, game_root, pins, ProgressSink{}, &result, &error));
  CHECK_EQ(result.files, 3u);
  CHECK_TRUE(FileExists(game_root / kUltimateDirName / "gen" / "patch_xbox_0.ark"));
  CHECK_FALSE(FileExists(game_root / kUltimateDirName / "default.xex"));
  CHECK_FALSE(FileExists(game_root / kUltimateDirName / "readme.txt"));
  CHECK_FALSE(DirectoryExists(StagingRoot(game_root)));

  BeginCase("the ultimate overlay replaces a previous install");

  WriteText(game_root / kUltimateDirName / "gen" / "stale.tmp", "from an older version");
  CHECK_TRUE(InstallUltimate(from_folder, game_root, pins, ProgressSink{}, &result, &error));
  CHECK_FALSE(FileExists(game_root / kUltimateDirName / "gen" / "stale.tmp"));

  BeginCase("the ultimate overlay refuses a folder without the mod");

  const fs::path empty = g_root / "ultimate-empty";
  WriteText(empty / "readme.txt", "nothing here");
  SourceRef nothing;
  nothing.kind = SourceKind::kFolder;
  nothing.path = empty;
  result = ComponentResult{};
  CHECK_FALSE(InstallUltimate(nothing, game_root, pins, ProgressSink{}, &result, &error));
  CHECK_FALSE(error.empty());
  CHECK_TRUE(DirectoryExists(game_root / kUltimateDirName / "gen"));

  BeginCase("an install report lists the ultimate overlay");

  const fs::path app = g_root / "ultimate-install";
  EnsureDirectory(app, nullptr);
  rb_blitz::installer::log::Open(app / kInstallLogName);
  InstallSummary summary;
  summary.install_dir = app;
  summary.game_dir = game_root;
  summary.game_source = "an extracted folder";
  summary.ultimate_installed = true;
  summary.ultimate = result;
  summary.installer_version = "1.0.0-test";
  CHECK_TRUE(FinalizeInstall(summary, &error));
  const std::string report = ReadFileOrEmpty(app / kInstallReportName);
  CHECK_CONTAINS(report, "Ultimate mod");
  CHECK_CONTAINS(ReadFileOrEmpty(app / kInstallManifestName), "ultimate_installed = true");
}

// The wizard only ever sees `--summary`, so a rejected command line has to leave
// its reason there: a silent exit 2 is what the wizard reports as "the helper did
// not say what went wrong".
static void TestCommandLine() {
  BeginCase("a rejected command line still reports through the summary");

  const fs::path summary = g_root / "cli/summary.txt";
  const fs::path space_summary = g_root / "cli/space.txt";
  EnsureDirectory(g_root / "cli", nullptr);

  CHECK_EQ(RunCommand({"check-space", "--dest", g_root.string(), "--required-bytes", "1",
                       "--details", (g_root / "cli/details.txt").string(), "--summary",
                       summary.string()}),
           kUsageExitCode);
  const std::string text = ReadFileOrEmpty(summary);
  CHECK_CONTAINS(text, "ok=0");
  CHECK_CONTAINS(text, "error=unknown option '--details'");

  BeginCase("every command accepts the reporting keys the wizard always passes");
  CHECK_EQ(RunCommand({"check-space", "--dest", g_root.string(), "--required-bytes", "1",
                       "--progress", (g_root / "cli/progress.txt").string(), "--summary",
                       space_summary.string()}),
           kSuccessExitCode);
  CHECK_CONTAINS(ReadFileOrEmpty(space_summary), "ok=1");

  // The wizard waits for `ok` to appear in the summary before it looks at the
  // stage result, so a call that dies outside a command's own error handling has
  // to end the summary itself instead of leaving the wizard waiting.
  BeginCase("a failure outside a command still ends the summary");

  const fs::path unhandled = g_root / "cli/unhandled.txt";
  ReportUnhandledFailure({"install-payload", "--summary", unhandled.string()}, "unhandled failure");
  const std::string unhandled_text = ReadFileOrEmpty(unhandled);
  CHECK_CONTAINS(unhandled_text, "ok=0");
  CHECK_CONTAINS(unhandled_text, "error=unhandled failure");
}

int main() {
  g_root = fs::temp_directory_path() / "rbblitz-installer-tests";
  std::string error;
  RemovePath(g_root);
  if (!EnsureDirectory(g_root, &error)) {
    std::fprintf(stderr, "cannot create %s: %s\n", g_root.string().c_str(), error.c_str());
    return 1;
  }
  std::printf("scratch: %s\n", g_root.string().c_str());

  TestUtil();
  TestInflate();
  TestZip();
  TestStfs();
  TestConfig();
  TestFingerprints();
  TestGamePlan();
  TestGameImport();
  TestPayloadAndFinalize();
  TestUltimate();
  TestCommandLine();

  const int result = rb_blitz::test::Finish();
  RemovePath(g_root);
  return result;
}
