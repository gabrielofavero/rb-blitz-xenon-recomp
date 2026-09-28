// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// config/toolchain.toml parsing and the frozen-toolchain comparison behind
// tools/toolchain_check.cpp. See src/util/toolchain_pin.h for what the file
// records and why the probing is not here.

#include "util/toolchain_pin.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace rb_blitz::toolchain {

namespace {

constexpr std::size_t kVerdictWidth = 10;
constexpr std::size_t kNameWidth = 16;
constexpr std::size_t kVersionWidth = 18;

// How much of a commit a report and a stamp show.
constexpr std::size_t kShortCommitLength = 7;

// Display name and report rank per section. A section this table does not know is
// still checked, under its own name and after the known ones - the file is
// allowed to grow before this tool does.
struct KnownComponent {
  const char* key;
  const char* name;
  int rank;
};

constexpr KnownComponent kKnownComponents[] = {
    {"sdk", "rexglue-sdk", 0}, {"clang", "clang", 1},       {"cmake", "CMake", 2},
    {"ninja", "Ninja", 3},     {"msvc", "MSVC toolset", 4}, {"windows_sdk", "Windows SDK", 5},
};

const KnownComponent* FindKnown(std::string_view key) {
  for (const KnownComponent& known : kKnownComponents) {
    if (key == known.key) {
      return &known;
    }
  }
  return nullptr;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

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
  return text;
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

// What a component's pinned value looks like in a report: a commit is shortened,
// a version is shown as recorded.
std::string ShortValue(const PinnedComponent& component) {
  if (component.kind == Kind::kCommit && component.expected.size() > kShortCommitLength) {
    return component.expected.substr(0, kShortCommitLength);
  }
  return component.expected;
}

std::string ToCStringLiteral(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\x%02X", static_cast<unsigned char>(c));
          out += escaped;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

// Everything from the first '#' that is not inside a quoted string.
std::string_view StripComment(std::string_view line) {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (in_double) {
      if (c == '\\') {
        ++i;
      } else if (c == '"') {
        in_double = false;
      }
      continue;
    }
    if (in_single) {
      if (c == '\'') {
        in_single = false;
      }
      continue;
    }
    if (c == '"') {
      in_double = true;
    } else if (c == '\'') {
      in_single = true;
    } else if (c == '#') {
      return line.substr(0, i);
    }
  }
  return line;
}

// A double-quoted value takes C escapes, a single-quoted one is literal (which is
// what a Windows path wants), and a bare one is passed through, for numbers and
// booleans.
bool ParseValue(std::string_view value, std::string* out, std::string* error) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    std::string text;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
      const char c = value[i];
      if (c != '\\') {
        text += c;
        continue;
      }
      if (i + 2 > value.size() - 1) {
        return Fail(error, "a quoted value ends with a dangling escape");
      }
      const char escaped = value[++i];
      switch (escaped) {
        case 'n': text += '\n'; break;
        case 't': text += '\t'; break;
        case 'r': text += '\r'; break;
        case '\\': text += '\\'; break;
        case '"': text += '"'; break;
        default:
          return Fail(error, std::string("unknown escape \\") + escaped + " in a value");
      }
    }
    *out = std::move(text);
    return true;
  }

  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    *out = std::string(value.substr(1, value.size() - 2));
    return true;
  }

  if (value.empty()) {
    return Fail(error, "a value is missing");
  }
  *out = std::string(value);
  return true;
}

// The component a section describes, from its keys: a `commit` is the pin when
// there is one, otherwise a `version` is. A section with neither - metadata, or a
// future key this tool does not know - pins nothing.
bool MakeComponent(std::string_view key, const std::map<std::string, std::string>& section,
                   PinnedComponent* out, bool* has_pin, std::string* error) {
  const auto commit = section.find("commit");
  const auto version = section.find("version");
  const auto minimum = section.find("minimum");
  const auto path = section.find("path");
  const auto require_exact = section.find("require_exact");

  *has_pin = commit != section.end() || version != section.end();
  if (!*has_pin) {
    return true;
  }

  out->key = std::string(key);
  const KnownComponent* known = FindKnown(key);
  out->name = known != nullptr ? known->name : std::string(key);

  if (commit != section.end()) {
    out->expected = commit->second;
    out->kind = Kind::kCommit;
    out->require_exact = true;
    if (version != section.end()) {
      out->recorded_note = "reports " + version->second;
    }
  } else {
    out->expected = version->second;
    out->kind = Kind::kVersion;
  }

  if (minimum != section.end()) {
    out->minimum = minimum->second;
  }
  if (path != section.end()) {
    out->recorded_path = path->second;
  }
  if (require_exact != section.end()) {
    const std::string& text = require_exact->second;
    if (text != "true" && text != "false") {
      return Fail(error, "[" + std::string(key) + "] require_exact is not true or false");
    }
    out->require_exact = out->require_exact || text == "true";
  }
  return true;
}

int RankOf(std::string_view key) {
  const KnownComponent* known = FindKnown(key);
  return known != nullptr ? known->rank : 100;
}

// The numeric value of one dotted component, advancing past it and its separator.
// "1a" reads as 1, and everything after the first non-digit up to the next '.' is
// not part of the number, so "0.10.0.0-dev.unknown" reads as 0.10.0.0.
long long TakeVersionComponent(std::string_view text, std::size_t* index) {
  long long value = 0;
  while (*index < text.size() && std::isdigit(static_cast<unsigned char>(text[*index])) != 0) {
    value = value * 10 + (text[*index] - '0');
    ++*index;
  }
  while (*index < text.size() && text[*index] != '.') {
    ++*index;
  }
  if (*index < text.size() && text[*index] == '.') {
    ++*index;
  }
  return value;
}

}  // namespace

int CompareVersions(std::string_view a, std::string_view b) {
  std::size_t ia = 0;
  std::size_t ib = 0;
  while (ia < a.size() || ib < b.size()) {
    const long long va = TakeVersionComponent(a, &ia);
    const long long vb = TakeVersionComponent(b, &ib);
    if (va != vb) {
      return va < vb ? -1 : 1;
    }
  }
  return 0;
}

bool ParsePin(std::string_view text, Pin* out, std::string* error) {
  Pin pin;
  std::map<std::string, std::string> top_level;
  std::vector<std::string> section_order;
  std::map<std::string, std::map<std::string, std::string>> sections;
  std::string section;

  int line_number = 0;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    std::size_t end = text.find('\n', begin);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const std::string_view raw = text.substr(begin, end - begin);
    begin = end + 1;
    ++line_number;

    const std::string line = Trim(StripComment(raw));
    if (line.empty()) {
      continue;
    }

    if (line.front() == '[') {
      if (line.size() < 2 || line.back() != ']') {
        return Fail(error, AtLine(line_number, "malformed section header"));
      }
      std::string name = Trim(std::string_view(line).substr(1, line.size() - 2));
      if (name.empty()) {
        return Fail(error, AtLine(line_number, "empty section name"));
      }
      for (char c : name) {
        const bool accepted =
            std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
        if (!accepted) {
          return Fail(error, AtLine(line_number, "section name has an unsupported character"));
        }
      }
      section = std::move(name);
      if (sections.find(section) == sections.end()) {
        section_order.push_back(section);
      }
      continue;
    }

    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
      return Fail(error, AtLine(line_number, "expected `key = value` or `[section]`"));
    }
    const std::string key = Trim(std::string_view(line).substr(0, equals));
    if (key.empty()) {
      return Fail(error, AtLine(line_number, "empty key"));
    }
    std::string value;
    std::string value_error;
    if (!ParseValue(Trim(std::string_view(line).substr(equals + 1)), &value, &value_error)) {
      return Fail(error, AtLine(line_number, value_error));
    }
    if (section.empty()) {
      top_level[key] = std::move(value);
    } else {
      sections[section][key] = std::move(value);
    }
  }

  const auto schema = top_level.find("schema_version");
  if (schema == top_level.end()) {
    return Fail(error, "no schema_version at the top of the file");
  }
  try {
    pin.schema_version = std::stoi(schema->second);
  } catch (const std::exception&) {
    return Fail(error, "schema_version is not a number: " + schema->second);
  }
  if (pin.schema_version != kSupportedSchemaVersion) {
    return Fail(error, "unsupported schema_version " + std::to_string(pin.schema_version) +
                           " (this build understands " + std::to_string(kSupportedSchemaVersion) +
                           ")");
  }

  for (const std::string& name : section_order) {
    PinnedComponent component;
    bool has_pin = false;
    if (!MakeComponent(name, sections[name], &component, &has_pin, error)) {
      return false;
    }
    if (has_pin) {
      pin.components.push_back(std::move(component));
    }
  }

  if (pin.components.empty()) {
    return Fail(error, "nothing is pinned (expected a [section] with a commit or a version)");
  }

  std::stable_sort(pin.components.begin(), pin.components.end(),
                   [](const PinnedComponent& a, const PinnedComponent& b) {
                     return RankOf(a.key) < RankOf(b.key);
                   });

  *out = std::move(pin);
  return true;
}

bool LoadPin(const std::filesystem::path& path, Pin* out, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Fail(error, "cannot read " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return ParsePin(buffer.str(), out, error);
}

std::string_view DescribeVerdict(Verdict verdict) {
  switch (verdict) {
    case Verdict::kMatch: return "ok";
    case Verdict::kDifferent: return "different";
    case Verdict::kTooOld: return "too old";
    case Verdict::kUnknown: return "unknown";
  }
  return "?";
}

std::vector<ComponentCheck> ComparePin(const Pin& pin,
                                       const std::map<std::string, Reported>& reported,
                                       const CheckOptions& options) {
  std::vector<ComponentCheck> checks;
  checks.reserve(pin.components.size());

  for (const PinnedComponent& component : pin.components) {
    ComponentCheck check;
    check.name = component.name;
    check.expected = component.expected;
    check.minimum = component.minimum;
    check.kind = component.kind;

    const auto found = reported.find(component.key);
    if (found == reported.end() || found->second.value.empty()) {
      check.verdict = Verdict::kUnknown;
      check.detail = (found != reported.end() && !found->second.detail.empty())
                         ? found->second.detail
                         : std::string("not reported");
      check.actual_detail = check.detail;
      checks.push_back(std::move(check));
      continue;
    }

    check.actual = found->second.value;
    check.actual_detail = found->second.detail;

    if (check.actual == check.expected) {
      check.verdict = Verdict::kMatch;
    } else if (component.kind == Kind::kCommit) {
      check.verdict = Verdict::kDifferent;
      check.detail = "frozen " + ShortValue(component);
      check.fatal = component.require_exact;
    } else if (!component.minimum.empty() &&
               CompareVersions(check.actual, component.minimum) < 0) {
      check.verdict = Verdict::kTooOld;
      check.detail = "needs " + component.minimum + " or newer";
      check.fatal = true;
    } else {
      check.verdict = Verdict::kDifferent;
      check.detail = "frozen " + component.expected;
      check.fatal = component.require_exact;
    }

    if (options.strict && check.verdict != Verdict::kMatch) {
      check.fatal = true;
    }
    checks.push_back(std::move(check));
  }

  if (options.allow_other_toolchain) {
    for (ComponentCheck& check : checks) {
      check.fatal = false;
    }
  }

  return checks;
}

bool AllMatched(const std::vector<ComponentCheck>& checks) {
  return std::all_of(checks.begin(), checks.end(), [](const ComponentCheck& check) {
    return check.verdict == Verdict::kMatch;
  });
}

bool AnyFatal(const std::vector<ComponentCheck>& checks) {
  return std::any_of(checks.begin(), checks.end(),
                     [](const ComponentCheck& check) { return check.fatal; });
}

std::string FormatCheckLine(const ComponentCheck& check) {
  // A commit is shortened for reading; the comparison above used all 40 characters.
  std::string actual = check.actual;
  if (check.kind == Kind::kCommit && actual.size() > kShortCommitLength) {
    actual = actual.substr(0, kShortCommitLength);
  }
  std::string line = PadTo(std::string(DescribeVerdict(check.verdict)), kVerdictWidth) +
                     PadTo(check.name, kNameWidth) + PadTo(actual, kVersionWidth);
  line += check.verdict == Verdict::kMatch ? check.actual_detail : check.detail;
  return Trim(line);
}

std::string FormatPin(const Pin& pin) {
  std::string line;
  for (const PinnedComponent& component : pin.components) {
    if (!line.empty()) {
      line += ", ";
    }
    line += component.name + " " + ShortValue(component);
  }
  return line;
}

std::string FormatStamp(const std::vector<ComponentCheck>& checks) {
  std::string line;
  std::string off;
  for (const ComponentCheck& check : checks) {
    std::string value = check.actual.empty() ? std::string("unknown") : check.actual;
    if (check.kind == Kind::kCommit && value.size() > kShortCommitLength) {
      value = value.substr(0, kShortCommitLength);
    }
    if (!line.empty()) {
      line += ", ";
    }
    line += check.name + " " + value;
    if (check.verdict != Verdict::kMatch) {
      if (!off.empty()) {
        off += ", ";
      }
      off += check.name + " " + value;
    }
  }
  if (line.empty()) {
    return "not reported";
  }
  return line + (off.empty() ? " (frozen set)" : " (off the frozen set: " + off + ")");
}

bool EmitBuildHeader(const Pin& pin, const std::vector<ComponentCheck>& checks,
                     std::string_view source_name, std::string* out, std::string* error) {
  if (checks.empty()) {
    return Fail(error, "nothing was checked, so there is no stamp to emit");
  }

  std::ostringstream header;
  header << "// Generated by tools/toolchain_check.cpp from " << source_name
         << " -- do not edit.\n"
         << "//\n"
         << "// The toolchain the binary including this header was built with, logged at\n"
         << "// boot by src/rb_blitz_app.h next to the build and game data identity. The\n"
         << "// same tool checked this machine against the frozen set as a build step; a\n"
         << "// deviation is part of the stamp rather than an error here.\n"
         << "//\n"
         << "// Frozen set (" << source_name << "): " << FormatPin(pin) << "\n"
         << "\n"
         << "#pragma once\n"
         << "\n"
         << "#define RBBLITZ_TOOLCHAIN_STAMP \"" << ToCStringLiteral(FormatStamp(checks))
         << "\"\n";
  *out = header.str();
  return true;
}

}  // namespace rb_blitz::toolchain
