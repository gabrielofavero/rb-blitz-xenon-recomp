// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Consumer for config/game_fingerprints.toml: the sizes and SHA-256 digests of
// the supported, unmodified Rock Band Blitz dump.
//
// Two callers, one parser (the build-time tool tools/fingerprint_check.cpp):
//   * a fail-closed build gate on the codegen input, so a wrong revision stops
//     the build instead of producing recompiled code for addresses that do not
//     exist in the local dump (docs/ultimate-compat.md covers the opt-out for a
//     deliberately modified content root);
//   * tools/fingerprint_check.cpp --emit-header, which turns the same parse into
//     the expected values src/rb_blitz_app.h logs at boot.
//
// The parser understands only the subset of TOML this one file uses: comments,
// `key = value` with strings/unsigned integers/booleans, `[game]` and `[[files]]`
// sections. Unknown keys and sections are ignored so the file can grow.
//
// SDK-free and host-side, so tests/fingerprint_tests.cpp covers it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::fingerprint {

inline constexpr int kSupportedSchemaVersion = 1;

// Role of the [[files]] entry the codegen input and the runtime boot log both
// refer to: the game's entrypoint XEX.
inline constexpr std::string_view kEntrypointRole = "entrypoint";

struct FileFingerprint {
  std::string role;
  std::string path;  // relative to the game data root, '/'-separated
  std::uint64_t size = 0;
  std::string sha256;  // lowercase hex, 64 characters
};

struct GameFingerprint {
  int schema_version = 0;
  std::string name;
  std::string title_id;
  std::string media_id;
  std::string region;
  std::string xex_version;
  std::string xex_base_version;
  std::string arcade_project_version;
  bool title_update_applied = false;
  std::vector<FileFingerprint> files;
};

// Both return false and fill `error` with a `line N: ...` message on a malformed
// or unsupported file.
bool ParseGameFingerprint(std::string_view text, GameFingerprint* out, std::string* error);
bool LoadGameFingerprint(const std::filesystem::path& path, GameFingerprint* out,
                         std::string* error);

const FileFingerprint* FindByRole(const GameFingerprint& fingerprint, std::string_view role);

enum class FileVerdict {
  kMatch,
  kMissing,
  kSizeMismatch,
  kHashMismatch,
  kUnreadable,
};

std::string_view DescribeVerdict(FileVerdict verdict);

struct FileCheck {
  FileFingerprint expected;
  FileVerdict verdict = FileVerdict::kMissing;
  std::uint64_t actual_size = 0;
  std::string actual_sha256;
  std::string detail;  // I/O error text, for kUnreadable and kMissing
};

// Verifies each requested role under `game_root`. An empty `roles` means every
// entry, in file order. Returns false only for a setup error (a role the
// fingerprint file does not describe); a file that does not match comes back as
// a verdict in `out`, so callers decide whether that is fatal.
bool VerifyRoles(const GameFingerprint& fingerprint, const std::filesystem::path& game_root,
                 const std::vector<std::string>& roles, std::vector<FileCheck>* out,
                 std::string* error);

bool AllMatched(const std::vector<FileCheck>& checks);

// One line, aligned for reading a list of checks: verdict, role, path, evidence.
std::string FormatCheckLine(const FileCheck& check);

// C++ source of the generated expectation header consumed by src/rb_blitz_app.h.
// `source_name` is only used in the header comment. Fails when the fingerprint
// has no entrypoint entry, which would leave the runtime nothing to compare.
bool EmitExpectedHeader(const GameFingerprint& fingerprint, std::string_view source_name,
                        std::string* out, std::string* error);

}  // namespace rb_blitz::fingerprint
