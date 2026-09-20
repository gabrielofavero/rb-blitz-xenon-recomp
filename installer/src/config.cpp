// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See config.h. The parser is intentionally the same shape as the one in
// src/util/game_fingerprint.cpp: a hand-written reader for the handful of TOML
// constructs these files use, so there is no dependency to vendor and no way for
// the installer's view of the pins to differ from the emulator's view of its
// fingerprints.

#include "config.h"

#include <cctype>
#include <stdexcept>
#include <utility>

#include "embedded_config.h"
#include "util.h"

namespace rb_blitz::installer {
namespace {

std::string_view StripComment(std::string_view line) {
  bool in_string = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    if (line[index] == '"') {
      in_string = !in_string;
    } else if (line[index] == '#' && !in_string) {
      return line.substr(0, index);
    }
  }
  return line;
}

std::string AtLine(std::size_t line_number, std::string_view message) {
  return S("line ", line_number, ": ", message);
}

bool IsBareKeyChar(char c) {
  const unsigned char byte = static_cast<unsigned char>(c);
  return std::isalnum(byte) != 0 || c == '_' || c == '-' || c == '.';
}

bool IsAllDigits(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

// `raw` is already comment-stripped and trimmed. Values are stored as text: the
// readers decide what a string, an integer or a boolean is, and a malformed bare
// value (a bare word, a unit suffix) is rejected here rather than later.
bool ParseValue(std::string_view raw, TomlValue* out, std::string* error, std::size_t line) {
  if (raw.empty()) {
    *error = AtLine(line, "missing value");
    return false;
  }
  if (raw.front() == '"') {
    if (raw.size() < 2 || raw.back() != '"') {
      *error = AtLine(line, "unterminated string");
      return false;
    }
    const std::string inner = std::string(raw.substr(1, raw.size() - 2));
    if (inner.find('"') != std::string::npos) {
      *error = AtLine(line, "quotes inside a string must not be escaped");
      return false;
    }
    out->text = inner;
    return true;
  }
  if (IsAllDigits(raw) || raw == "true" || raw == "false") {
    out->text = std::string(raw);
    return true;
  }
  *error = AtLine(line, S("unsupported value ", raw, " (expected text, an integer or a boolean)"));
  return false;
}

// `[name]` or `[[name]]`. Returns the name and whether it is an array of tables.
bool ParseTableHeader(std::string_view header, std::string* name, bool* is_array,
                      std::string* error, std::size_t line) {
  std::string_view inner = header.substr(1, header.size() - 2);
  *is_array = false;
  if (!inner.empty() && inner.front() == '[') {
    if (inner.size() < 2 || inner.back() != ']') {
      *error = AtLine(line, "malformed array-of-tables header");
      return false;
    }
    inner = inner.substr(1, inner.size() - 2);
    *is_array = true;
  }
  const std::string trimmed = Trim(inner);
  if (trimmed.empty()) {
    *error = AtLine(line, "empty table name");
    return false;
  }
  for (const char c : trimmed) {
    if (!IsBareKeyChar(c)) {
      *error = AtLine(line, S("unsupported table name ", header));
      return false;
    }
  }
  *name = std::string(inner);
  return true;
}

constexpr std::string_view kHexDigits = "0123456789abcdef";

bool IsSha256Hex(std::string_view text) {
  if (text.size() != 64) {
    return false;
  }
  for (const char c : text) {
    if (kHexDigits.find(static_cast<char>(std::tolower(static_cast<unsigned char>(c)))) ==
        std::string_view::npos) {
      return false;
    }
  }
  return true;
}

std::string ReadString(const TomlTable* table, std::string_view key) {
  if (table == nullptr) {
    return {};
  }
  const std::optional<std::string> value = table->Get(key);
  return value.has_value() ? *value : std::string();
}

}  // namespace

// --- toml accessors -------------------------------------------------------

std::optional<std::string> TomlTable::Get(std::string_view key) const {
  for (auto it = values.rbegin(); it != values.rend(); ++it) {
    if (it->first == key) {
      return it->second.text;
    }
  }
  return std::nullopt;
}

std::string TomlTable::GetString(std::string_view key, std::string_view fallback) const {
  const std::optional<std::string> value = Get(key);
  return value.has_value() ? *value : std::string(fallback);
}

std::uint64_t TomlTable::GetUnsigned(std::string_view key, std::uint64_t fallback) const {
  const std::optional<std::string> value = Get(key);
  if (!value.has_value() || !IsAllDigits(*value)) {
    return fallback;
  }
  std::uint64_t result = 0;
  for (const char c : *value) {
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (result > (UINT64_MAX - digit) / 10) {
      return fallback;
    }
    result = result * 10 + digit;
  }
  return result;
}

bool TomlTable::GetBool(std::string_view key, bool fallback) const {
  const std::optional<std::string> value = Get(key);
  if (!value.has_value()) {
    return fallback;
  }
  if (*value == "true") {
    return true;
  }
  if (*value == "false") {
    return false;
  }
  return fallback;
}

const TomlTable* TomlDocument::Find(std::string_view name) const {
  for (auto it = tables.rbegin(); it != tables.rend(); ++it) {
    if (it->name == name) {
      return &*it;
    }
  }
  return nullptr;
}

bool ParseTomlSubset(std::string_view text, TomlDocument* out, std::string* error) {
  TomlDocument document;
  // Index 0 is the root table, so top-level keys such as schema_version have a
  // home; every other table is appended in file order.
  document.tables.push_back(TomlTable{});
  std::size_t line_number = 0;
  for (const std::string& raw_line : SplitLines(text)) {
    ++line_number;
    const std::string line = Trim(StripComment(raw_line));
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[') {
      if (line.size() < 3 || line.back() != ']') {
        *error = AtLine(line_number, "malformed table header");
        return false;
      }
      TomlTable table;
      if (!ParseTableHeader(line, &table.name, &table.is_array, error, line_number)) {
        return false;
      }
      document.tables.push_back(std::move(table));
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) {
      *error = AtLine(line_number, "expected `key = value`");
      return false;
    }
    const std::string key = Trim(line.substr(0, equals));
    if (key.empty()) {
      *error = AtLine(line_number, "empty key");
      return false;
    }
    for (const char c : key) {
      if (!IsBareKeyChar(c)) {
        *error = AtLine(line_number, S("unsupported key ", key));
        return false;
      }
    }
    const std::string value_text = Trim(line.substr(equals + 1));
    TomlValue value;
    if (!ParseValue(value_text, &value, error, line_number)) {
      return false;
    }
    document.tables.back().values.emplace_back(key, std::move(value));
  }
  *out = std::move(document);
  return true;
}

// --- pins -----------------------------------------------------------------

bool Pins::Validate(std::string* error) const {
  auto fail = [error](std::string message) {
    if (error != nullptr) {
      *error = std::move(message);
    }
    return false;
  };

  if (schema_version != 1) {
    return fail(S("unsupported pins schema_version ", schema_version));
  }
  if (installer.name.empty() || installer.short_name.empty()) {
    return fail("[installer] name and short_name are required");
  }
  if (installer.version.empty() || installer.default_dir_name.empty()) {
    return fail("[installer] version and default_dir_name are required");
  }
  if (installer.min_windows_build.empty()) {
    return fail("[installer] min_windows_build is required");
  }
  if (!payload.url.empty() && !IsSha256Hex(payload.sha256)) {
    return fail("[payload] a download url needs a 64-character sha256");
  }
  if (!payload.url.empty() && payload.size == 0) {
    return fail("[payload] a download url needs a size");
  }
  if (ultimate.version.empty()) {
    return fail("[ultimate] version is required");
  }
  if (ultimate.url.empty() || !IsSha256Hex(ultimate.sha256) || ultimate.size == 0) {
    return fail("[ultimate] url, sha256 and size are required");
  }
  if (ultimate.destination_dir.empty()) {
    return fail("[ultimate] destination_dir is required");
  }
  std::string reason;
  if (SafeRelativePath(ultimate.destination_dir, &reason).empty()) {
    return fail(S("[ultimate] ", reason));
  }
  return true;
}

bool ParsePins(std::string_view text, Pins* out, std::string* error) {
  TomlDocument document;
  if (!ParseTomlSubset(text, &document, error)) {
    return false;
  }
  const TomlTable* root = document.Find("");
  const TomlTable* installer_table = document.Find("installer");
  const TomlTable* payload_table = document.Find("payload");
  const TomlTable* ultimate_table = document.Find("ultimate");
  if (installer_table == nullptr || payload_table == nullptr || ultimate_table == nullptr) {
    if (error != nullptr) {
      *error = "pins.toml must contain [installer], [payload] and [ultimate]";
    }
    return false;
  }

  Pins pins;
  if (root != nullptr) {
    pins.schema_version = static_cast<int>(root->GetUnsigned("schema_version", 0));
  }
  pins.installer.name = ReadString(installer_table, "name");
  pins.installer.short_name = ReadString(installer_table, "short_name");
  pins.installer.version = ReadString(installer_table, "version");
  pins.installer.publisher = ReadString(installer_table, "publisher");
  pins.installer.homepage_url = ReadString(installer_table, "homepage_url");
  pins.installer.support_url = ReadString(installer_table, "support_url");
  pins.installer.default_dir_name = ReadString(installer_table, "default_dir_name");
  pins.installer.min_windows_build = ReadString(installer_table, "min_windows_build");

  pins.payload.version = ReadString(payload_table, "version");
  pins.payload.url = ReadString(payload_table, "url");
  pins.payload.sha256 = Lower(ReadString(payload_table, "sha256"));
  pins.payload.size = payload_table->GetUnsigned("size", 0);

  pins.ultimate.version = ReadString(ultimate_table, "version");
  pins.ultimate.url = ReadString(ultimate_table, "url");
  pins.ultimate.sha256 = Lower(ReadString(ultimate_table, "sha256"));
  pins.ultimate.size = ultimate_table->GetUnsigned("size", 0);
  pins.ultimate.repository_url = ReadString(ultimate_table, "repository_url");
  pins.ultimate.release_url = ReadString(ultimate_table, "release_url");
  pins.ultimate.archive_prefix = ReadString(ultimate_table, "archive_prefix");
  pins.ultimate.destination_dir = ReadString(ultimate_table, "destination_dir");

  if (!pins.Validate(error)) {
    return false;
  }
  *out = std::move(pins);
  return true;
}

const Pins& EmbeddedPins() {
  // A malformed embedded pins.toml is a build bug, not a user error, so it is
  // reported as an exception the entry point turns into a readable message.
  static const Pins pins = [] {
    Pins parsed;
    std::string error;
    if (!ParsePins(embedded::kPinsToml, &parsed, &error)) {
      throw std::runtime_error(S("embedded pins.toml is invalid: ", error));
    }
    return parsed;
  }();
  return pins;
}

const std::string& EmbeddedGameFingerprints() {
  static const std::string text = embedded::kGameFingerprintsToml;
  return text;
}

const std::string& EmbeddedUltimateFingerprints() {
  static const std::string text = embedded::kUltimateFingerprintsToml;
  return text;
}

}  // namespace rb_blitz::installer
