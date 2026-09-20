// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "util/game_fingerprint.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "util/sha256.h"

namespace rb_blitz::fingerprint {
namespace {

// Verdict text, plus the width the role and path columns are padded to so a list
// of checks lines up in a terminal.
constexpr std::size_t kVerdictWidth = 14;
constexpr std::size_t kRoleWidth = 16;
constexpr std::size_t kPathWidth = 26;

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsSpace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsSpace(text[end - 1])) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string PadTo(std::string text, std::size_t width) {
  if (text.size() < width) {
    text.append(width - text.size(), ' ');
  }
  text.push_back(' ');
  return text;
}

// A '#' inside a quoted value is data, not the start of a comment, so quote state
// is tracked while scanning.
std::string_view StripComment(std::string_view line) {
  bool in_string = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') {
      in_string = !in_string;
    } else if (line[i] == '#' && !in_string) {
      return line.substr(0, i);
    }
  }
  return line;
}

std::string AtLine(int line_number, std::string_view message) {
  return "line " + std::to_string(line_number) + ": " + std::string(message);
}

bool Fail(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
  return false;
}

// The subset of TOML values this file uses: `"..."` (returned verbatim, no escape
// sequences), decimal unsigned integers, and booleans.
bool ParseString(std::string_view value, std::string* out) {
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
    return false;
  }
  *out = std::string(value.substr(1, value.size() - 2));
  return true;
}

bool ParseUnsigned(std::string_view value, std::uint64_t* out) {
  if (value.empty()) {
    return false;
  }
  std::uint64_t result = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (result > (UINT64_MAX - digit) / 10) {
      return false;
    }
    result = result * 10 + digit;
  }
  *out = result;
  return true;
}

bool ParseBool(std::string_view value, bool* out) {
  if (value == "true") {
    *out = true;
    return true;
  }
  if (value == "false") {
    *out = false;
    return true;
  }
  return false;
}

// Accepts either case, stores lowercase, so comparisons against the lowercase
// digest HashFileHex() produces are plain string equality.
bool NormalizeSha256(std::string_view value, std::string* out) {
  if (value.size() != util::kSha256HexSize) {
    return false;
  }
  std::string hex;
  hex.reserve(value.size());
  for (const char c : value) {
    const char lower =
        (c >= 'A' && c <= 'F') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    const bool is_digit = lower >= '0' && lower <= '9';
    const bool is_hex_letter = lower >= 'a' && lower <= 'f';
    if (!is_digit && !is_hex_letter) {
      return false;
    }
    hex.push_back(lower);
  }
  *out = std::move(hex);
  return true;
}

// Escapes a value for a C++ string literal. Fingerprint values are identifiers and
// hex, so this only ever fires on a hand-edited file.
std::string ToCStringLiteral(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

FileCheck CheckFile(const std::filesystem::path& game_root, const FileFingerprint& entry) {
  FileCheck check;
  check.expected = entry;

  const auto full_path = game_root / std::filesystem::path(entry.path);
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(full_path, ec);
  if (ec) {
    check.verdict = (ec == std::errc::no_such_file_or_directory) ? FileVerdict::kMissing
                                                                : FileVerdict::kUnreadable;
    check.detail = ec.message();
    return check;
  }
  check.actual_size = static_cast<std::uint64_t>(size);

  // Size first: hashing is what makes verifying the 360 MB archive take seconds,
  // and a size mismatch already decides the verdict.
  if (check.actual_size != entry.size) {
    check.verdict = FileVerdict::kSizeMismatch;
    return check;
  }

  std::uint64_t hashed = 0;
  check.actual_sha256 = util::HashFileHex(full_path, &hashed, ec);
  if (check.actual_sha256.empty()) {
    check.verdict = FileVerdict::kUnreadable;
    check.detail = ec.message();
    return check;
  }

  check.verdict = (check.actual_sha256 == entry.sha256) ? FileVerdict::kMatch
                                                       : FileVerdict::kHashMismatch;
  return check;
}

}  // namespace

bool ParseGameFingerprint(std::string_view text, GameFingerprint* out, std::string* error) {
  enum class Section { kRoot, kGame, kFiles, kOther };

  GameFingerprint fingerprint;
  Section section = Section::kRoot;
  bool have_schema_version = false;

  // Fields of the [[files]] entry currently being read.
  FileFingerprint entry;
  bool have_role = false;
  bool have_path = false;
  bool have_size = false;
  bool have_sha256 = false;
  int entry_line = 0;

  int line_number = 0;
  std::size_t position = 0;
  while (position <= text.size()) {
    const std::size_t eol = text.find('\n', position);
    const std::string_view line =
        text.substr(position, (eol == std::string_view::npos) ? std::string_view::npos : eol - position);
    position = (eol == std::string_view::npos) ? text.size() + 1 : eol + 1;
    ++line_number;

    const std::string content = Trim(StripComment(line));
    if (content.empty()) {
      continue;
    }

    if (content.front() == '[') {
      if (section == Section::kFiles) {
        if (!have_role || !have_path || !have_size || !have_sha256) {
          return Fail(error, AtLine(entry_line, "incomplete [[files]] entry"));
        }
        fingerprint.files.push_back(entry);
        entry = FileFingerprint{};
        have_role = have_path = have_size = have_sha256 = false;
      }

      if (content == "[game]") {
        section = Section::kGame;
      } else if (content == "[[files]]") {
        section = Section::kFiles;
        entry_line = line_number;
      } else if (content.size() > 2 && content.front() == '[' && content.back() == ']') {
        section = Section::kOther;
      } else {
        return Fail(error, AtLine(line_number, "malformed section header"));
      }
      continue;
    }

    const std::size_t equals = content.find('=');
    if (equals == std::string::npos) {
      return Fail(error, AtLine(line_number, "expected 'key = value'"));
    }
    const std::string key = Trim(std::string_view(content).substr(0, equals));
    const std::string_view value = StripComment(content).substr(equals + 1);
    const std::string trimmed_value = Trim(value);
    if (key.empty() || trimmed_value.empty()) {
      return Fail(error, AtLine(line_number, "expected 'key = value'"));
    }

    auto parse_string = [&](std::string* target) {
      if (!ParseString(trimmed_value, target)) {
        return Fail(error, AtLine(line_number, "expected a quoted string for '" + key + "'"));
      }
      return true;
    };

    switch (section) {
      case Section::kRoot: {
        if (key != "schema_version") {
          break;  // Unknown top-level keys are ignored: the file may grow.
        }
        std::uint64_t version = 0;
        if (!ParseUnsigned(trimmed_value, &version)) {
          return Fail(error, AtLine(line_number, "schema_version must be a decimal integer"));
        }
        fingerprint.schema_version = static_cast<int>(version);
        have_schema_version = true;
        break;
      }
      case Section::kGame: {
        if (key == "name") {
          if (!parse_string(&fingerprint.name)) return false;
        } else if (key == "title_id") {
          if (!parse_string(&fingerprint.title_id)) return false;
        } else if (key == "media_id") {
          if (!parse_string(&fingerprint.media_id)) return false;
        } else if (key == "region") {
          if (!parse_string(&fingerprint.region)) return false;
        } else if (key == "xex_version") {
          if (!parse_string(&fingerprint.xex_version)) return false;
        } else if (key == "xex_base_version") {
          if (!parse_string(&fingerprint.xex_base_version)) return false;
        } else if (key == "arcade_project_version") {
          if (!parse_string(&fingerprint.arcade_project_version)) return false;
        } else if (key == "title_update_applied") {
          if (!ParseBool(trimmed_value, &fingerprint.title_update_applied)) {
            return Fail(error, AtLine(line_number, "title_update_applied must be true or false"));
          }
        }
        break;
      }
      case Section::kFiles: {
        if (key == "role") {
          if (!parse_string(&entry.role)) return false;
          have_role = true;
        } else if (key == "path") {
          if (!parse_string(&entry.path)) return false;
          have_path = true;
        } else if (key == "size") {
          if (!ParseUnsigned(trimmed_value, &entry.size)) {
            return Fail(error, AtLine(line_number, "size must be a decimal integer"));
          }
          have_size = true;
        } else if (key == "sha256") {
          std::string digest;
          if (!ParseString(trimmed_value, &digest)) {
            return Fail(error, AtLine(line_number, "expected a quoted string for 'sha256'"));
          }
          if (!NormalizeSha256(digest, &entry.sha256)) {
            return Fail(error, AtLine(line_number,
                                      "sha256 must be 64 hexadecimal characters"));
          }
          have_sha256 = true;
        }
        break;
      }
      case Section::kOther:
        break;
    }
  }

  if (section == Section::kFiles) {
    if (!have_role || !have_path || !have_size || !have_sha256) {
      return Fail(error, AtLine(entry_line, "incomplete [[files]] entry"));
    }
    fingerprint.files.push_back(entry);
  }

  if (!have_schema_version) {
    return Fail(error, "missing schema_version");
  }
  if (fingerprint.schema_version != kSupportedSchemaVersion) {
    return Fail(error, "unsupported schema_version " + std::to_string(fingerprint.schema_version) +
                           " (this build understands " +
                           std::to_string(kSupportedSchemaVersion) + ")");
  }
  if (fingerprint.files.empty()) {
    return Fail(error, "no [[files]] entries");
  }

  *out = std::move(fingerprint);
  return true;
}

bool LoadGameFingerprint(const std::filesystem::path& path, GameFingerprint* out,
                         std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Fail(error, "cannot read " + path.string());
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  if (file.bad()) {
    return Fail(error, "cannot read " + path.string());
  }
  return ParseGameFingerprint(contents.str(), out, error);
}

const FileFingerprint* FindByRole(const GameFingerprint& fingerprint, std::string_view role) {
  for (const FileFingerprint& file : fingerprint.files) {
    if (file.role == role) {
      return &file;
    }
  }
  return nullptr;
}

std::string_view DescribeVerdict(FileVerdict verdict) {
  switch (verdict) {
    case FileVerdict::kMatch:
      return "ok";
    case FileVerdict::kMissing:
      return "MISSING";
    case FileVerdict::kSizeMismatch:
      return "SIZE MISMATCH";
    case FileVerdict::kHashMismatch:
      return "HASH MISMATCH";
    case FileVerdict::kUnreadable:
      return "UNREADABLE";
  }
  return "UNKNOWN";
}

bool VerifyRoles(const GameFingerprint& fingerprint, const std::filesystem::path& game_root,
                 const std::vector<std::string>& roles, std::vector<FileCheck>* out,
                 std::string* error) {
  std::vector<FileCheck> checks;
  if (roles.empty()) {
    checks.reserve(fingerprint.files.size());
    for (const FileFingerprint& entry : fingerprint.files) {
      checks.push_back(CheckFile(game_root, entry));
    }
  } else {
    checks.reserve(roles.size());
    for (const std::string& role : roles) {
      const FileFingerprint* entry = FindByRole(fingerprint, role);
      if (entry == nullptr) {
        return Fail(error, "no [[files]] entry with role '" + role + "'");
      }
      checks.push_back(CheckFile(game_root, *entry));
    }
  }
  *out = std::move(checks);
  return true;
}

bool AllMatched(const std::vector<FileCheck>& checks) {
  return std::all_of(checks.begin(), checks.end(),
                     [](const FileCheck& check) { return check.verdict == FileVerdict::kMatch; });
}

std::string FormatCheckLine(const FileCheck& check) {
  std::string line = PadTo(std::string(DescribeVerdict(check.verdict)), kVerdictWidth) +
                     PadTo(check.expected.role, kRoleWidth) + PadTo(check.expected.path, kPathWidth);
  switch (check.verdict) {
    case FileVerdict::kMatch:
      line += std::to_string(check.expected.size) + " bytes  sha256 " + check.actual_sha256;
      break;
    case FileVerdict::kSizeMismatch:
      line += "size " + std::to_string(check.actual_size) + " (expected " +
              std::to_string(check.expected.size) + ")";
      break;
    case FileVerdict::kHashMismatch:
      line += std::to_string(check.actual_size) + " bytes  sha256 " + check.actual_sha256 +
              " (expected " + check.expected.sha256 + ")";
      break;
    case FileVerdict::kMissing:
      line += "(no such file)";
      break;
    case FileVerdict::kUnreadable:
      line += "(" + check.detail + ")";
      break;
  }
  return line;
}

bool EmitExpectedHeader(const GameFingerprint& fingerprint, std::string_view source_name,
                        std::string* out, std::string* error) {
  const FileFingerprint* entrypoint = FindByRole(fingerprint, kEntrypointRole);
  if (entrypoint == nullptr) {
    return Fail(error, "no [[files]] entry with role '" + std::string(kEntrypointRole) + "'");
  }

  std::ostringstream header;
  header << "// Generated by tools/fingerprint_check.cpp from " << source_name
         << " -- do not edit.\n"
         << "//\n"
         << "// Expected values for the supported game dump, used by the boot identity\n"
         << "// line in src/rb_blitz_app.h. Regenerate by building (docs/build-and-run.md).\n"
         << "\n"
         << "#pragma once\n"
         << "\n"
         << "#include <cstdint>\n"
         << "\n"
         << "namespace rb_blitz::fingerprint::vanilla {\n"
         << "\n"
         << "inline constexpr int kSchemaVersion = " << fingerprint.schema_version << ";\n"
         << "inline constexpr const char* kGameName = \"" << ToCStringLiteral(fingerprint.name)
         << "\";\n"
         << "inline constexpr const char* kTitleId = \"" << ToCStringLiteral(fingerprint.title_id)
         << "\";\n"
         << "inline constexpr const char* kMediaId = \"" << ToCStringLiteral(fingerprint.media_id)
         << "\";\n"
         << "inline constexpr const char* kRegion = \"" << ToCStringLiteral(fingerprint.region)
         << "\";\n"
         << "inline constexpr const char* kXexVersion = \""
         << ToCStringLiteral(fingerprint.xex_version) << "\";\n"
         << "\n"
         << "// The entrypoint [[files]] entry: the file the runtime hashes at boot.\n"
         << "inline constexpr const char* kEntrypointPath = \""
         << ToCStringLiteral(entrypoint->path) << "\";\n"
         << "inline constexpr std::uint64_t kEntrypointSize = " << entrypoint->size << ";\n"
         << "inline constexpr const char* kEntrypointSha256 = \""
         << ToCStringLiteral(entrypoint->sha256) << "\";\n"
         << "\n"
         << "}  // namespace rb_blitz::fingerprint::vanilla\n";

  *out = header.str();
  return true;
}

}  // namespace rb_blitz::fingerprint
