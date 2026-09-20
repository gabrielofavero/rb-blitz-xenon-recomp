// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for src/util/sha256.{h,cpp} and src/util/game_fingerprint.{h,cpp} -
// the two pieces behind the build gate and the boot identity line that consume
// config/game_fingerprints.toml. No SDK, no GPU, no game boot: docs/rb3-references.md
// §7.3 asks for exactly this, and the whole point of the fingerprint check is that
// it can run before anything else does.
//
// The digest vectors are not recalled from memory: they come from a separate
// implementation (Python hashlib) plus the FIPS 180-4 / NIST CAVP example
// messages, so a mistake in the round function or in the padding boundary shows
// up as a failure here rather than as a wrong build gate.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "util/game_fingerprint.h"
#include "util/sha256.h"

namespace {

namespace fs = std::filesystem;
using rb_blitz::fingerprint::FileCheck;
using rb_blitz::fingerprint::FileVerdict;
using rb_blitz::fingerprint::GameFingerprint;
using rb_blitz::test::BeginCase;
using rb_blitz::test::Fail;
using rb_blitz::test::g_checks;

#ifdef RBBLITZ_PROJECT_DIR
constexpr const char* kDefaultProjectDir = RBBLITZ_PROJECT_DIR;
#else
constexpr const char* kDefaultProjectDir = ".";
#endif

void CheckStringEq(const char* file, int line, std::string_view actual, std::string_view expected) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, "expected \"" + std::string(expected) + "\"\n         actual   \"" +
                       std::string(actual) + "\"");
}

void CheckContains(const char* file, int line, std::string_view haystack,
                   std::string_view needle) {
  ++g_checks;
  if (haystack.find(needle) != std::string_view::npos) {
    return;
  }
  Fail(file, line, "expected to find \"" + std::string(needle) + "\" in\n         \"" +
                       std::string(haystack) + "\"");
}

void CheckVerdict(const char* file, int line, FileVerdict actual, FileVerdict expected) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string("verdict ") + std::string(rb_blitz::fingerprint::DescribeVerdict(actual)) +
                       " != " + std::string(rb_blitz::fingerprint::DescribeVerdict(expected)));
}

#define CHECK_STR_EQ(actual, expected) CheckStringEq(__FILE__, __LINE__, actual, expected)
#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, haystack, needle)
#define CHECK_VERDICT(actual, expected) CheckVerdict(__FILE__, __LINE__, actual, expected)

// ---------------------------------------------------------------------------
// A temp directory that cleans up after itself, so a failing run leaves nothing
// behind for the next one to trip over.
class TempDir {
 public:
  explicit TempDir(const char* name) {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("rb_blitz_test_" + std::string(name) + "_" +
                                         std::to_string(++counter));
    std::error_code ec;
    fs::remove_all(path_, ec);
    fs::create_directories(path_, ec);
    if (ec) {
      std::printf("         ! could not create %s: %s\n", path_.string().c_str(),
                  ec.message().c_str());
    }
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

bool WriteText(const fs::path& path, std::string_view text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.flush();
  return static_cast<bool>(file);
}

// A fingerprint with one entrypoint, matching whatever the caller says the file
// holds, so each verdict test only has to corrupt the one thing it is about.
GameFingerprint FingerprintFor(const std::string& relative_path, std::uint64_t size,
                               std::string sha256, std::string role = "entrypoint") {
  GameFingerprint fingerprint;
  fingerprint.schema_version = rb_blitz::fingerprint::kSupportedSchemaVersion;
  fingerprint.name = "Test Game";
  fingerprint.files.push_back(
      {std::move(role), relative_path, size, std::move(sha256)});
  return fingerprint;
}

std::string HashOf(const fs::path& path) {
  std::error_code ec;
  std::uint64_t size = 0;
  return rb_blitz::util::HashFileHex(path, &size, ec);
}

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------

struct RepeatingVector {
  std::size_t length;  // of a run of 'a'
  const char* hex;
};

// Hashlib output for 'a' repeated N times. The 54..66 band straddles the
// one-block/two-block padding boundary at 55/56 bytes, which is where a padding
// bug lives, so it is covered on both sides.
constexpr RepeatingVector kRepeatingVectors[] = {
    {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
    {2, "961b6dd3ede3cb8ecbaacbd68de040cd78eb2ed5889130cceb4c49268ea4d506"},
    {3, "9834876dcfb05cb167a5c24953eba58c4ac89b1adf57f28f2f9d09af107ee8f0"},
    {4, "61be55a8e2f6b4e172338bddf184d6dbee29c98853e0a0485ecee7f27b9af0b4"},
    {5, "ed968e840d10d2d313a870bc131a4e2c311d7ad09bdf32b3418147221f51a6e2"},
    {6, "ed02457b5c41d964dbd2f2a609d63fe1bb7528dbe55e1abf5b52c249cd735797"},
    {7, "e46240714b5db3a23eee60479a623efba4d633d27fe4f03c904b9e219a7fbe60"},
    {8, "1f3ce40415a2081fa3eee75fc39fff8e56c22270d1a978a7249b592dcebd20b4"},
    {54, "a3f01b6939256127582ac8ae9fb47a382a244680806a3f613a118851c1ca1d47"},
    {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
    {58, "d5c039b748aa64665782974ec3dc3025c042edf54dcdc2b5de31385b094cb678"},
    {59, "111bb261277afd65f0744b247cd3e47d386d71563d0ed995517807d5ebd4fba3"},
    {60, "11ee391211c6256460b6ed375957fadd8061cafbb31daf967db875aebd5aaad4"},
    {61, "35d5fc17cfbbadd00f5e710ada39f194c5ad7c766ad67072245f1fad45f0f530"},
    {62, "f506898cc7c2e092f9eb9fadae7ba50383f5b46a2a4fe5597dbb553a78981268"},
    {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {66, "ac137fce49837c7c2945f6160d3c0e679e6f40070850420a22bc10e0692cbdc7"},
};

struct LiteralVector {
  const char* text;
  const char* hex;
};

// FIPS 180-4 / NIST example messages: one block, two blocks, and the boundary
// cases that exercise multi-block compression.
constexpr LiteralVector kLiteralVectors[] = {
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopq"
     "rlmnopqrsmnopqrstnopqrstu",
     "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
};

void TestSha256Vectors() {
  BeginCase("sha256_vectors");
  for (const RepeatingVector& vector : kRepeatingVectors) {
    const std::string input(vector.length, 'a');
    CHECK_STR_EQ(rb_blitz::util::HashHex(input), vector.hex);
  }
  for (const LiteralVector& vector : kLiteralVectors) {
    CHECK_STR_EQ(rb_blitz::util::HashHex(vector.text), vector.hex);
  }

  // The NIST million-'a' message: 15,625 blocks through Compress and a length
  // field that no short input can check.
  const std::string million(1000000, 'a');
  CHECK_STR_EQ(rb_blitz::util::HashHex(million),
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

void TestSha256Incremental() {
  BeginCase("sha256_incremental");

  // A 112-byte message fed in every chunk size that crosses a 64-byte block
  // boundary, plus a pathological one-byte-at-a-time run.
  const std::string message(kLiteralVectors[2].text);
  const std::string expected = kLiteralVectors[2].hex;

  for (std::size_t chunk = 1; chunk <= 71; ++chunk) {
    rb_blitz::util::Sha256 hash;
    for (std::size_t offset = 0; offset < message.size(); offset += chunk) {
      hash.Update(message.data() + offset, std::min(chunk, message.size() - offset));
    }
    CHECK_STR_EQ(rb_blitz::util::ToHex(hash.Final()), expected);
  }

  // Splitting exactly on a block boundary must not change anything: this is the
  // case where an implementation that hashes the buffer too early or too late
  // still passes a one-shot test.
  for (std::size_t split = 0; split <= message.size(); ++split) {
    rb_blitz::util::Sha256 hash;
    hash.Update(std::string_view(message).substr(0, split));
    hash.Update(std::string_view(message).substr(split));
    CHECK_STR_EQ(rb_blitz::util::ToHex(hash.Final()), expected);
  }

  rb_blitz::util::Sha256 empty;
  CHECK_STR_EQ(rb_blitz::util::ToHex(empty.Final()), kRepeatingVectors[0].hex);

  // An Update of zero bytes must be a no-op, not a block flush.
  rb_blitz::util::Sha256 zero_length;
  zero_length.Update(nullptr, 0);
  zero_length.Update(std::string_view(message));
  zero_length.Update("", 0);
  CHECK_STR_EQ(rb_blitz::util::ToHex(zero_length.Final()), expected);
}

void TestSha256File() {
  BeginCase("sha256_file");
  TempDir temp("sha256_file");

  const fs::path path = temp.path() / "payload.bin";
  // 200 KiB: larger than the internal block buffer, and not a multiple of it.
  std::string contents;
  for (std::size_t i = 0; i < 204800; ++i) {
    contents.push_back(static_cast<char>(i * 7 + (i >> 3)));
  }
  CHECK_TRUE(WriteText(path, contents));

  std::error_code ec;
  std::uint64_t size = 0;
  const std::string file_hex = rb_blitz::util::HashFileHex(path, &size, ec);
  CHECK_FALSE(ec);
  CHECK_EQ(size, contents.size());
  CHECK_STR_EQ(file_hex, rb_blitz::util::HashHex(contents));

  // A missing file must report, not assert or return a plausible digest.
  std::uint64_t missing_size = 0;
  std::error_code missing_ec;
  CHECK_STR_EQ(rb_blitz::util::HashFileHex(temp.path() / "absent.bin", &missing_size, missing_ec),
               "");
  CHECK_TRUE(static_cast<bool>(missing_ec));
}

// ---------------------------------------------------------------------------
// Fingerprint parsing
// ---------------------------------------------------------------------------

void TestParseGolden(const fs::path& project_dir) {
  BeginCase("fingerprint_parse_golden");

  const fs::path path = project_dir / "config/game_fingerprints.toml";
  if (!fs::exists(path)) {
    CHECK_TRUE(false);  // The file is tracked; a missing one is a broken checkout.
    return;
  }

  GameFingerprint fingerprint;
  std::string error;
  CHECK_TRUE(rb_blitz::fingerprint::LoadGameFingerprint(path, &fingerprint, &error));
  CHECK_STR_EQ(error, "");
  CHECK_EQ(fingerprint.schema_version, rb_blitz::fingerprint::kSupportedSchemaVersion);
  CHECK_STR_EQ(fingerprint.name, "Rock Band Blitz");
  CHECK_STR_EQ(fingerprint.title_id, "5841122D");
  CHECK_STR_EQ(fingerprint.media_id, "78492654");
  CHECK_STR_EQ(fingerprint.region, "region-free");
  CHECK_STR_EQ(fingerprint.xex_version, "0.0.0.2");
  CHECK_FALSE(fingerprint.title_update_applied);
  CHECK_EQ(fingerprint.files.size(), 3);

  const auto* entrypoint =
      rb_blitz::fingerprint::FindByRole(fingerprint, rb_blitz::fingerprint::kEntrypointRole);
  CHECK_TRUE(entrypoint != nullptr);
  if (entrypoint != nullptr) {
    CHECK_STR_EQ(entrypoint->path, "default.xex");
    CHECK_EQ(entrypoint->size, 9023488);
    CHECK_EQ(entrypoint->sha256.size(), rb_blitz::util::kSha256HexSize);
  }
  CHECK_TRUE(rb_blitz::fingerprint::FindByRole(fingerprint, "archive-data") != nullptr);
  CHECK_TRUE(rb_blitz::fingerprint::FindByRole(fingerprint, "no-such-role") == nullptr);
}

void TestParseTolerates(const fs::path& project_dir) {
  BeginCase("fingerprint_parse_tolerates");

  // Unknown keys and sections are ignored so the file can grow, comments keep
  // working, hex is accepted in either case, a '#' inside a string is data, and
  // CRLF line endings parse the same as LF.
  const std::string text =
      "# leading comment\n"
      "schema_version = 1\n"
      "unknown_top_level = \"ignored\"\n"
      "\n"
      "[game]\n"
      "name = \"Rock Band Blitz\"   # trailing comment\n"
      "title_id = \"5841122D\"\n"
      "region = \"a # b\"\n"
      "\n"
      "[future_section]\n"
      "whatever = 42\n"
      "\n"
      "[[files]]\n"
      "role = \"entrypoint\"\n"
      "path = \"default.xex\"\n"
      "size = 9023488\n"
      "sha256 = \"E2195D62A9E5C0D5D7E5B1A5B1A5B1A5B1A5B1A5B1A5B1A5B1A5B1A5B1A5B1A5\"\n";

  GameFingerprint fingerprint;
  std::string error;
  CHECK_TRUE(rb_blitz::fingerprint::ParseGameFingerprint(text, &fingerprint, &error));
  CHECK_STR_EQ(error, "");
  CHECK_STR_EQ(fingerprint.region, "a # b");
  CHECK_EQ(fingerprint.files.size(), 1);
  CHECK_STR_EQ(fingerprint.files[0].sha256,
               "e2195d62a9e5c0d5d7e5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5");

  std::string crlf = text;
  std::string::size_type position = 0;
  while ((position = crlf.find('\n', position)) != std::string::npos) {
    crlf.insert(position, 1, '\r');
    position += 2;
  }
  GameFingerprint crlf_fingerprint;
  CHECK_TRUE(rb_blitz::fingerprint::ParseGameFingerprint(crlf, &crlf_fingerprint, &error));
  CHECK_STR_EQ(crlf_fingerprint.files[0].path, "default.xex");

  // And the real file is unaffected by anything the tests assume about it.
  GameFingerprint real;
  CHECK_TRUE(rb_blitz::fingerprint::LoadGameFingerprint(
      project_dir / "config/game_fingerprints.toml", &real, &error));
  CHECK_EQ(real.schema_version, 1);
}

void TestParseErrors(const fs::path& project_dir) {
  BeginCase("fingerprint_parse_errors");

  const std::string valid_entry =
      "role = \"entrypoint\"\n"
      "path = \"default.xex\"\n"
      "size = 1\n"
      "sha256 = \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"\n";

  struct Case {
    const char* name;
    std::string text;
    const char* expected_error;  // substring, "" to require success
  };

  std::vector<Case> cases = {
      {"empty file", "", "missing schema_version"},
      {"no schema_version", "[game]\nname = \"x\"\n[[files]]\n" + valid_entry, "missing schema_version"},
      {"unsupported schema_version", "schema_version = 2\n[[files]]\n" + valid_entry,
       "unsupported schema_version 2"},
      {"schema_version not an integer", "schema_version = \"1\"\n[[files]]\n" + valid_entry,
       "schema_version must be a decimal integer"},
      {"no files", "schema_version = 1\n[game]\nname = \"x\"\n", "no [[files]] entries"},
      {"missing key value", "schema_version = 1\n[[files]]\nrole\n", "expected 'key = value'"},
      {"empty value", "schema_version = 1\n[[files]]\nrole = \n", "expected 'key = value'"},
      {"malformed section", "schema_version = 1\n[game\n", "malformed section header"},
      {"unquoted name", "schema_version = 1\n[game]\nname = Rock Band Blitz\n",
       "expected a quoted string for 'name'"},
      {"size not an integer", "schema_version = 1\n[[files]]\nrole = \"r\"\npath = \"p\"\n"
                              "size = twelve\nsha256 = \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"\n",
       "size must be a decimal integer"},
      {"size too large", "schema_version = 1\n[[files]]\nrole = \"r\"\npath = \"p\"\n"
                         "size = 99999999999999999999\n"
                         "sha256 = \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"\n",
       "size must be a decimal integer"},
      {"bad boolean", "schema_version = 1\n[game]\ntitle_update_applied = yes\n",
       "title_update_applied must be true or false"},
      {"short sha256", "schema_version = 1\n[[files]]\nrole = \"r\"\npath = \"p\"\nsize = 1\n"
                       "sha256 = \"e3b0c442\"\n",
       "sha256 must be 64 hexadecimal characters"},
      {"non-hex sha256", "schema_version = 1\n[[files]]\nrole = \"r\"\npath = \"p\"\nsize = 1\n"
                         "sha256 = \"z3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"\n",
       "sha256 must be 64 hexadecimal characters"},
      {"incomplete files entry", "schema_version = 1\n[[files]]\nrole = \"r\"\npath = \"p\"\n",
       "line 2: incomplete [[files]] entry"},
      {"incomplete last files entry", "schema_version = 1\n[[files]]\n" + valid_entry +
                                          "[[files]]\nrole = \"other\"\n",
       "line 7: incomplete [[files]] entry"},
  };

  for (const Case& item : cases) {
    GameFingerprint fingerprint;
    std::string error;
    const bool ok = rb_blitz::fingerprint::ParseGameFingerprint(item.text, &fingerprint, &error);
    if (item.expected_error[0] == '\0') {
      CHECK_TRUE(ok);
      continue;
    }
    CHECK_FALSE(ok);
    // Substring, not equality: the point is which mistake was diagnosed, and the
    // message may carry a line number in front of it. The case name is already
    // part of the failure report, so it does not belong in the needle.
    CHECK_CONTAINS(error, item.expected_error);
  }

  // Line numbers have to point at the offending line, or the error is useless:
  // "line 12" of a 200-line file is what makes a broken fingerprint file fixable.
  GameFingerprint fingerprint;
  std::string error;
  CHECK_FALSE(rb_blitz::fingerprint::ParseGameFingerprint(
      "schema_version = 1\n[game]\nname = \"x\"\nmedia_id = 42\n", &fingerprint, &error));
  CHECK_CONTAINS(error, "line 4:");

  if (fs::exists(project_dir / "config/game_fingerprints.toml")) {
    CHECK_FALSE(rb_blitz::fingerprint::LoadGameFingerprint(project_dir / "config/absent.toml",
                                                           &fingerprint, &error));
    CHECK_CONTAINS(error, "cannot read");
  }
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

void TestVerifyRoles() {
  BeginCase("fingerprint_verify_roles");
  TempDir temp("verify_roles");

  const std::string contents = "the quick brown fox jumps over the lazy dog";
  const std::string digest = rb_blitz::util::HashHex(contents);
  CHECK_TRUE(WriteText(temp.path() / "entrypoint.bin", contents));
  const GameFingerprint fingerprint = FingerprintFor("entrypoint.bin",
                                                     contents.size(), digest);

  std::vector<FileCheck> checks;
  std::string error;

  CHECK_TRUE(rb_blitz::fingerprint::VerifyRoles(fingerprint, temp.path(), {}, &checks, &error));
  CHECK_EQ(checks.size(), 1);
  CHECK_VERDICT(checks[0].verdict, FileVerdict::kMatch);
  CHECK_EQ(checks[0].actual_size, contents.size());
  CHECK_STR_EQ(checks[0].actual_sha256, digest);
  CHECK_TRUE(rb_blitz::fingerprint::AllMatched(checks));
  CHECK_CONTAINS(rb_blitz::fingerprint::FormatCheckLine(checks[0]), digest);

  const std::vector<std::string> entrypoint_roles = {"entrypoint"};

  // A size mismatch is decided without hashing, so actual_sha256 stays empty.
  GameFingerprint wrong_size = fingerprint;
  wrong_size.files[0].size = contents.size() + 1;
  CHECK_TRUE(
      rb_blitz::fingerprint::VerifyRoles(wrong_size, temp.path(), entrypoint_roles, &checks, &error));
  CHECK_VERDICT(checks[0].verdict, FileVerdict::kSizeMismatch);
  CHECK_STR_EQ(checks[0].actual_sha256, "");
  CHECK_EQ(checks[0].actual_size, contents.size());
  CHECK_FALSE(rb_blitz::fingerprint::AllMatched(checks));

  // Same size, different content: the case a size check alone would wave through.
  GameFingerprint wrong_hash = fingerprint;
  wrong_hash.files[0].sha256 =
      "0000000000000000000000000000000000000000000000000000000000000000";
  CHECK_TRUE(
      rb_blitz::fingerprint::VerifyRoles(wrong_hash, temp.path(), entrypoint_roles, &checks, &error));
  CHECK_VERDICT(checks[0].verdict, FileVerdict::kHashMismatch);
  CHECK_STR_EQ(checks[0].actual_sha256, digest);
  CHECK_CONTAINS(rb_blitz::fingerprint::FormatCheckLine(checks[0]), "expected");

  CHECK_TRUE(rb_blitz::fingerprint::VerifyRoles(
      FingerprintFor("absent.bin", 1, digest), temp.path(), entrypoint_roles, &checks,
      &error));
  CHECK_VERDICT(checks[0].verdict, FileVerdict::kMissing);

  // A directory where a file belongs. Whether the OS lets the size probe read
  // the entry and report 0, or refuses outright, differs, so both outcomes are
  // accepted - what must never happen is a match, or a digest being computed.
  std::error_code ec;
  fs::create_directory(temp.path() / "not_a_file.bin", ec);
  CHECK_FALSE(ec);
  CHECK_TRUE(rb_blitz::fingerprint::VerifyRoles(
      FingerprintFor("not_a_file.bin", 1, digest), temp.path(), entrypoint_roles,
      &checks, &error));
  CHECK_TRUE(checks[0].verdict == FileVerdict::kUnreadable ||
             checks[0].verdict == FileVerdict::kSizeMismatch);
  CHECK_STR_EQ(checks[0].actual_sha256, "");

  // The verdict names have to be distinguishable in a build log, and an
  // I/O failure has to print its reason rather than a bare "UNREADABLE".
  CHECK_TRUE(rb_blitz::fingerprint::DescribeVerdict(FileVerdict::kUnreadable) !=
             rb_blitz::fingerprint::DescribeVerdict(FileVerdict::kMissing));
  CHECK_TRUE(rb_blitz::fingerprint::DescribeVerdict(FileVerdict::kHashMismatch) !=
             rb_blitz::fingerprint::DescribeVerdict(FileVerdict::kSizeMismatch));
  FileCheck unreadable;
  unreadable.expected = {"entrypoint", "default.xex", 1, digest};
  unreadable.verdict = FileVerdict::kUnreadable;
  unreadable.detail = "access denied";
  CHECK_CONTAINS(rb_blitz::fingerprint::FormatCheckLine(unreadable), "access denied");
  CHECK_CONTAINS(rb_blitz::fingerprint::FormatCheckLine(unreadable), "entrypoint");

  // A role the fingerprint file does not describe is a setup error, not a
  // verdict: the caller asked for something the file cannot answer.
  CHECK_FALSE(rb_blitz::fingerprint::VerifyRoles(fingerprint, temp.path(), {"archive-data"}, &checks,
                                                 &error));
  CHECK_CONTAINS(error, "archive-data");

  // Roles are checked in the order asked for, so `--role a --role b` reads the
  // way the caller wrote it.
  CHECK_TRUE(WriteText(temp.path() / "second.bin", contents));
  GameFingerprint two = fingerprint;
  two.files.push_back({"archive-header", "second.bin", contents.size(), digest});
  CHECK_TRUE(rb_blitz::fingerprint::VerifyRoles(two, temp.path(),
                                                {"archive-header", "entrypoint"}, &checks, &error));
  CHECK_EQ(checks.size(), 2);
  CHECK_STR_EQ(checks[0].expected.role, "archive-header");
  CHECK_STR_EQ(checks[1].expected.role, "entrypoint");
}

void TestEmitExpectedHeader() {
  BeginCase("fingerprint_emit_header");

  GameFingerprint fingerprint;
  fingerprint.schema_version = 1;
  fingerprint.name = "Rock Band Blitz";
  fingerprint.title_id = "5841122D";
  fingerprint.media_id = "78492654";
  fingerprint.region = "region-free";
  fingerprint.xex_version = "0.0.0.2";
  fingerprint.files.push_back(
      {"entrypoint", "default.xex", 9023488,
       "e2195d62a9e5c0d5d7e5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5"});

  std::string header;
  std::string error;
  CHECK_TRUE(rb_blitz::fingerprint::EmitExpectedHeader(fingerprint, "game_fingerprints.toml",
                                                       &header, &error));
  CHECK_STR_EQ(error, "");
  CHECK_CONTAINS(header, "#pragma once");
  CHECK_CONTAINS(header, "namespace rb_blitz::fingerprint::vanilla");
  CHECK_CONTAINS(header, "kEntrypointPath = \"default.xex\"");
  CHECK_CONTAINS(header, "kEntrypointSize = 9023488");
  CHECK_CONTAINS(header,
                 "kEntrypointSha256 = "
                 "\"e2195d62a9e5c0d5d7e5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5b1a5\"");
  CHECK_CONTAINS(header, "kGameName = \"Rock Band Blitz\"");
  CHECK_CONTAINS(header, "kSchemaVersion = 1");

  // Values land inside string literals, so a quote or backslash in the file must
  // not be able to break the generated header.
  GameFingerprint hostile = fingerprint;
  hostile.name = "Rock \"Band\" \\ Blitz";
  CHECK_TRUE(rb_blitz::fingerprint::EmitExpectedHeader(hostile, "x.toml", &header, &error));
  CHECK_CONTAINS(header, "kGameName = \"Rock \\\"Band\\\" \\\\ Blitz\"");

  // Without an entrypoint there is nothing for the runtime to compare, so the
  // tool has to fail rather than emit a header full of the wrong file.
  GameFingerprint no_entrypoint = fingerprint;
  no_entrypoint.files[0].role = "archive-data";
  CHECK_FALSE(rb_blitz::fingerprint::EmitExpectedHeader(no_entrypoint, "x.toml", &header, &error));
  CHECK_CONTAINS(error, "entrypoint");
}

// The one test that touches the real dump: the gate is only useful if the
// committed fingerprints describe the files actually in game/. Entrypoint only -
// hashing the 360 MB archive is what `--all` is for, and it would put a second
// of I/O into every test run.
void TestRealGameDump(const fs::path& project_dir, bool allow_mismatch) {
  BeginCase("fingerprint_real_game_dump");

  const fs::path path = project_dir / "config/game_fingerprints.toml";
  const fs::path entrypoint = project_dir / "game/default.xex";
  if (!fs::exists(path) || !fs::exists(entrypoint)) {
    std::printf("         [ SKIP ] no game dump at %s\n",
                (project_dir / "game").string().c_str());
    return;
  }

  GameFingerprint fingerprint;
  std::string error;
  CHECK_TRUE(rb_blitz::fingerprint::LoadGameFingerprint(path, &fingerprint, &error));

  std::vector<FileCheck> checks;
  CHECK_TRUE(rb_blitz::fingerprint::VerifyRoles(
      fingerprint, project_dir / "game", {std::string(rb_blitz::fingerprint::kEntrypointRole)},
      &checks, &error));
  CHECK_EQ(checks.size(), 1);
  if (checks[0].verdict != FileVerdict::kMatch) {
    std::printf("         %s\n", rb_blitz::fingerprint::FormatCheckLine(checks[0]).c_str());
    if (allow_mismatch) {
      // A modified content root is a supported configuration
      // (-DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON), so it must not fail the suite.
      std::printf("         [ SKIP ] game data is not the supported revision\n");
      return;
    }
  }
  CHECK_VERDICT(checks[0].verdict, FileVerdict::kMatch);
  CHECK_STR_EQ(checks[0].actual_sha256, HashOf(entrypoint));
}

}  // namespace

int main(int argc, char** argv) {
  fs::path project_dir = kDefaultProjectDir;
  bool allow_mismatch = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--project-dir" && i + 1 < argc) {
      project_dir = argv[++i];
    } else if (arg == "--allow-mismatch") {
      allow_mismatch = true;
    }
  }

  std::printf("project dir: %s\n", project_dir.string().c_str());

  TestSha256Vectors();
  TestSha256Incremental();
  TestSha256File();
  TestParseGolden(project_dir);
  TestParseTolerates(project_dir);
  TestParseErrors(project_dir);
  TestVerifyRoles();
  TestEmitExpectedHeader();
  TestRealGameDump(project_dir, allow_mismatch);

  return rb_blitz::test::Finish();
}
