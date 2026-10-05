// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The profile reader/writer in profile.h. The interesting half is the writer, and it is
// a line patcher rather than a serializer: the document is scanned once to find where
// each key lives, the keys this module owns are scheduled as small edits, and every byte
// nobody asked to change is carried through untouched.
//
// The rules that follow from that, and are what the tests pin:
//   * a load-then-save with no edits reproduces the file byte for byte;
//   * a key this version does not know - and a whole table it does not know, including
//     repeated [[array-of-tables]] blocks - is never touched;
//   * a value that did not change keeps its line, so its spacing and its trailing
//     comment survive;
//   * a document that does not parse is reported with a reason and never written.

#include "launcher/profile.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace rb_blitz::launcher {

namespace fs = std::filesystem;

namespace {

// --- text -----------------------------------------------------------------

bool IsSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' ||
         value == '\v';
}

bool IsHorizontalSpace(char value) {
  return value == ' ' || value == '\t';
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

std::string BoolText(bool value) {
  return value ? "true" : "false";
}

bool ParseInteger(std::string_view text, long long* out) {
  if (text.empty()) {
    return false;
  }
  std::size_t index = 0;
  bool negative = false;
  if (text.front() == '-' || text.front() == '+') {
    negative = text.front() == '-';
    index = 1;
  }
  if (index == text.size()) {
    return false;
  }
  long long value = 0;
  for (; index < text.size(); ++index) {
    if (text[index] < '0' || text[index] > '9') {
      return false;
    }
    value = value * 10 + (text[index] - '0');
  }
  *out = negative ? -value : value;
  return true;
}

bool ParseBoolean(std::string_view text, bool* out) {
  if (text == "true") {
    *out = true;
    return true;
  }
  if (text == "false") {
    *out = false;
    return true;
  }
  return false;
}

std::string EscapeBasicString(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (const char character : text) {
    switch (character) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        if (static_cast<unsigned char>(character) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04X",
                        static_cast<unsigned int>(static_cast<unsigned char>(character)));
          out += buffer;
        } else {
          out += character;
        }
        break;
    }
  }
  return out;
}

std::string RenderValue(const std::string& value, ValueStyle style) {
  switch (style) {
    case ValueStyle::kBare:
      return value;
    case ValueStyle::kBasic:
      return "\"" + EscapeBasicString(value) + "\"";
    case ValueStyle::kLiteral:
      return "'" + value + "'";
  }
  return value;
}

// --- the scanned document -------------------------------------------------

// One `key = value` line, with the offsets a patch needs.
struct KeyRef {
  std::string section;  // "" for a top-level key
  std::string key;
  std::size_t line = 0;
  std::size_t line_begin = 0;
  std::size_t line_end = 0;
  std::size_t value_begin = 0;
  std::size_t value_end = 0;
  std::string value;  // unquoted
  ValueStyle style = ValueStyle::kBare;
};

struct Layout {
  std::string text;
  std::string newline = "\n";
  std::vector<std::size_t> line_begin;
  std::vector<std::size_t> line_end;  // one past the newline
  std::vector<KeyRef> keys;           // file order
  std::map<std::pair<std::string, std::string>, std::size_t> by_key;
  // section -> the last line that belongs to it, so a new key can be appended after it.
  std::map<std::string, std::size_t> section_last_line;
  bool has_header = false;
  std::size_t first_header_line = 0;
};

// Keys inside an `[[array table]]` are namespaced by occurrence: two blocks of the same
// array table must not look like a duplicate key, and nothing this module owns ever lives
// in one - so all this needs to do is keep them distinct.
std::string ArraySectionName(std::string_view header, std::size_t occurrence) {
  return std::string("\x01") + std::string(header) + "#" + std::to_string(occurrence);
}

std::string_view LineContent(const Layout& layout, std::size_t line) {
  std::size_t end = layout.line_end[line];
  if (end > layout.line_begin[line] && layout.text[end - 1] == '\n') {
    --end;
  }
  if (end > layout.line_begin[line] && layout.text[end - 1] == '\r') {
    --end;
  }
  return std::string_view(layout.text).substr(layout.line_begin[line],
                                               end - layout.line_begin[line]);
}

bool FailAt(std::string* error, std::size_t line, const std::string& message) {
  *error = "line " + std::to_string(line + 1) + ": " + message;
  return false;
}

int BracketDelta(char character) {
  if (character == '[' || character == '{' || character == '(') {
    return 1;
  }
  if (character == ']' || character == '}' || character == ')') {
    return -1;
  }
  return 0;
}

bool Scan(const std::string& text, Layout* out, std::string* error) {
  out->text = text;

  std::size_t start = 0;
  while (true) {
    const std::size_t newline = out->text.find('\n', start);
    const std::size_t end = newline == std::string::npos ? out->text.size() : newline + 1;
    out->line_begin.push_back(start);
    out->line_end.push_back(end);
    if (newline == std::string::npos) {
      break;
    }
    start = end;
  }
  for (std::size_t line = 0; line < out->line_begin.size(); ++line) {
    const std::size_t end = out->line_end[line];
    if (end >= out->line_begin[line] + 2 && out->text[end - 2] == '\r') {
      out->newline = "\r\n";
      break;
    }
  }

  std::string section;
  std::map<std::string, std::size_t> array_occurrence;
  int continuation_depth = 0;

  for (std::size_t line = 0; line < out->line_begin.size(); ++line) {
    const std::string_view content = LineContent(*out, line);
    if (continuation_depth > 0) {
      for (const char character : content) {
        continuation_depth += BracketDelta(character);
      }
      continue;
    }

    const std::string trimmed = Trim(content);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
      const std::size_t close = trimmed.find("]]");
      if (close == std::string::npos) {
        return FailAt(error, line, "unterminated [[array table]] header");
      }
      const std::string header = Trim(std::string_view(trimmed).substr(2, close - 2));
      if (header.empty()) {
        return FailAt(error, line, "empty table header");
      }
      section = ArraySectionName(header, array_occurrence[header]++);
      out->section_last_line[section] = line;
      if (!out->has_header) {
        out->has_header = true;
        out->first_header_line = line;
      }
      continue;
    }
    if (trimmed.front() == '[') {
      const std::size_t close = trimmed.find(']');
      if (close == std::string::npos) {
        return FailAt(error, line, "unterminated table header");
      }
      const std::string header = Trim(std::string_view(trimmed).substr(1, close - 1));
      if (header.empty()) {
        return FailAt(error, line, "empty table header");
      }
      section = header;
      out->section_last_line[section] = line;
      if (!out->has_header) {
        out->has_header = true;
        out->first_header_line = line;
      }
      continue;
    }

    // `key = value`. The '=' and the quotes are found outside strings, so a value such as
    // "a = b" does not confuse the split.
    std::size_t equals = std::string_view::npos;
    {
      char quote = 0;
      for (std::size_t index = 0; index < content.size(); ++index) {
        const char character = content[index];
        if (quote == '"') {
          if (character == '\\') {
            ++index;
          } else if (character == '"') {
            quote = 0;
          }
          continue;
        }
        if (quote == '\'') {
          if (character == '\'') {
            quote = 0;
          }
          continue;
        }
        if (character == '"' || character == '\'') {
          quote = character;
          continue;
        }
        if (character == '=') {
          equals = index;
          break;
        }
      }
    }
    if (equals == std::string_view::npos) {
      return FailAt(error, line, "expected `key = value`");
    }

    KeyRef ref;
    ref.section = section;
    ref.key = Trim(content.substr(0, equals));
    ref.line = line;
    ref.line_begin = out->line_begin[line];
    ref.line_end = out->line_end[line];
    if (ref.key.empty()) {
      return FailAt(error, line, "empty key");
    }

    std::size_t value_start = equals + 1;
    while (value_start < content.size() && IsHorizontalSpace(content[value_start])) {
      ++value_start;
    }
    if (value_start >= content.size() || content[value_start] == '#') {
      return FailAt(error, line, "missing value for `" + ref.key + "`");
    }

    const char first = content[value_start];
    if (first == '"') {
      std::size_t index = value_start + 1;
      bool closed = false;
      for (; index < content.size(); ++index) {
        if (content[index] == '\\') {
          ++index;
          continue;
        }
        if (content[index] == '"') {
          closed = true;
          break;
        }
      }
      if (!closed) {
        return FailAt(error, line, "unterminated string for `" + ref.key + "`");
      }
      ref.value_begin = out->line_begin[line] + value_start;
      ref.value_end = out->line_begin[line] + index + 1;
      ref.value = UnquoteTomlString(content.substr(value_start + 1, index - value_start - 1));
      ref.style = ValueStyle::kBasic;
    } else if (first == '\'') {
      const std::size_t close = content.find('\'', value_start + 1);
      if (close == std::string_view::npos) {
        return FailAt(error, line, "unterminated literal string for `" + ref.key + "`");
      }
      ref.value_begin = out->line_begin[line] + value_start;
      ref.value_end = out->line_begin[line] + close + 1;
      ref.value = std::string(content.substr(value_start + 1, close - value_start - 1));
      ref.style = ValueStyle::kLiteral;
    } else if (first == '[' || first == '{') {
      // An array or inline table: opaque to this module, but its brackets have to balance
      // before the next line is read as a key.
      int depth = 0;
      std::size_t index = value_start;
      bool closed = false;
      for (; index < content.size(); ++index) {
        depth += BracketDelta(content[index]);
        if (depth == 0) {
          closed = true;
          break;
        }
      }
      const std::size_t token_end = closed ? index + 1 : content.size();
      continuation_depth = closed ? 0 : depth;
      ref.value_begin = out->line_begin[line] + value_start;
      ref.value_end = out->line_begin[line] + token_end;
      ref.value = std::string(content.substr(value_start, token_end - value_start));
      ref.style = ValueStyle::kBare;
    } else {
      std::size_t index = value_start;
      while (index < content.size() && content[index] != '#') {
        ++index;
      }
      while (index > value_start && IsHorizontalSpace(content[index - 1])) {
        --index;
      }
      if (index == value_start) {
        return FailAt(error, line, "empty value for `" + ref.key + "`");
      }
      ref.value_begin = out->line_begin[line] + value_start;
      ref.value_end = out->line_begin[line] + index;
      ref.value = std::string(content.substr(value_start, index - value_start));
      ref.style = ValueStyle::kBare;
    }

    const auto key = std::make_pair(ref.section, ref.key);
    if (!out->by_key.emplace(key, out->keys.size()).second) {
      return FailAt(error, line,
                    "duplicate key `" + ref.key + "` in the same table");
    }
    out->keys.push_back(std::move(ref));
    out->section_last_line[section] = line;
  }

  return true;
}

const KeyRef* FindKey(const Layout& layout, std::string_view section, std::string_view key) {
  const auto found =
      layout.by_key.find(std::make_pair(std::string(section), std::string(key)));
  return found == layout.by_key.end() ? nullptr : &layout.keys[found->second];
}

// --- the patch ------------------------------------------------------------

struct Edit {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string replacement;
};

// The keys to add, grouped by section, in the order they were scheduled.
struct Pending {
  std::vector<Edit> edits;
  std::map<std::string, std::vector<std::string>> section_lines;
  std::vector<std::string> section_order;
};

void AddLine(Pending* pending, const std::string& section, const std::string& line) {
  auto found = pending->section_lines.find(section);
  if (found == pending->section_lines.end()) {
    pending->section_order.push_back(section);
    found = pending->section_lines.emplace(section, std::vector<std::string>{}).first;
  }
  found->second.push_back(line);
}

// Rewrites the key's value when it differs, and schedules the key when the document does
// not have it. An unchanged value is left completely alone, which is what keeps comments
// and spacing.
void Schedule(const Layout& layout, Pending* pending, const std::string& section,
              const std::string& key, const std::string& value, ValueStyle style) {
  if (const KeyRef* ref = FindKey(layout, section, key); ref != nullptr) {
    if (ref->style == style && ref->value == value) {
      return;
    }
    pending->edits.push_back(Edit{ref->value_begin, ref->value_end, RenderValue(value, style)});
    return;
  }
  AddLine(pending, section, key + " = " + RenderValue(value, style));
}

std::string Apply(const Layout& layout, const Pending& pending) {
  std::string text = layout.text;

  // Where each group of new lines goes. A group either continues a table that is already
  // open at that offset, or starts a new one - and when both land on the same offset (a
  // `[settings]` table that is the last thing in the file, and a `[launcher]` that has to
  // be created after it) the continuation has to come first, or the new header would
  // swallow the rows meant for the table above it.
  struct Block {
    std::size_t offset;
    int rank;
    std::size_t sequence;
    std::string text;
  };
  std::vector<Block> blocks;
  std::size_t sequence = 0;
  for (const std::string& section : pending.section_order) {
    const std::vector<std::string>& lines = pending.section_lines.at(section);

    std::size_t offset = text.size();
    bool starts_a_table = false;
    const auto last = layout.section_last_line.find(section);
    if (last != layout.section_last_line.end()) {
      // After the section's last line, which keeps any trailing comments with it.
      offset = layout.line_end[last->second];
    } else if (section.empty()) {
      // Top level: before the first table, after the last top-level key, or at the end.
      offset = layout.has_header ? layout.line_begin[layout.first_header_line] : text.size();
    } else {
      // A table this document does not have goes at the end of it.
      starts_a_table = true;
    }

    std::string block;
    // A block of lines has to begin at the start of a line. The insertion point is not always
    // one: a document whose last line has no trailing newline ends in the middle of a line, so
    // the separator belongs before the block - and an unseparated `[remap]` would land on the
    // end of `fullscreen = false` and turn both into one broken line.
    if (offset > 0 && text[offset - 1] != '\n') {
      block += layout.newline;
    }
    if (starts_a_table) {
      block += "[" + section + "]" + layout.newline;
    }
    for (const std::string& line : lines) {
      block += line + layout.newline;
    }
    blocks.push_back(Block{offset, starts_a_table ? 1 : 0, sequence++, std::move(block)});
  }
  std::sort(blocks.begin(), blocks.end(), [](const Block& left, const Block& right) {
    if (left.offset != right.offset) {
      return left.offset < right.offset;
    }
    if (left.rank != right.rank) {
      return left.rank < right.rank;
    }
    return left.sequence < right.sequence;
  });
  std::map<std::size_t, std::string> inserts;
  for (const Block& block : blocks) {
    inserts[block.offset] += block.text;
  }

  std::vector<Edit> ops = pending.edits;
  for (const auto& [offset, block] : inserts) {
    ops.push_back(Edit{offset, offset, block});
  }
  std::sort(ops.begin(), ops.end(), [](const Edit& left, const Edit& right) {
    if (left.begin != right.begin) {
      return left.begin > right.begin;
    }
    return left.end > right.end;
  });
  for (const Edit& op : ops) {
    text.replace(op.begin, op.end - op.begin, op.replacement);
  }
  return text;
}

std::string LaunchTargetLiteral(LaunchTarget target) {
  return std::string(LaunchTargetName(target));
}

// --- load helpers ---------------------------------------------------------

bool ReadWholeFile(const fs::path& path, std::string* out, std::string* error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    *error = "cannot read " + path.string();
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *out = buffer.str();
  return true;
}

}  // namespace

// --- the public surface ---------------------------------------------------

std::string_view LaunchTargetName(LaunchTarget target) {
  switch (target) {
    case LaunchTarget::kCommon:
      return "common";
    case LaunchTarget::kDemo:
      return "demo";
    case LaunchTarget::kUltimate:
      return "ultimate";
  }
  return "ultimate";
}

std::optional<LaunchTarget> ParseLaunchTarget(std::string_view text) {
  if (text == "common") {
    return LaunchTarget::kCommon;
  }
  if (text == "demo") {
    return LaunchTarget::kDemo;
  }
  if (text == "ultimate") {
    return LaunchTarget::kUltimate;
  }
  return std::nullopt;
}

std::string QuoteTomlString(std::string_view text) {
  return "\"" + EscapeBasicString(text) + "\"";
}

std::string UnquoteTomlString(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '\\' || index + 1 >= text.size()) {
      out += text[index];
      continue;
    }
    switch (text[++index]) {
      case 'b':
        out += '\b';
        break;
      case 't':
        out += '\t';
        break;
      case 'n':
        out += '\n';
        break;
      case 'f':
        out += '\f';
        break;
      case 'r':
        out += '\r';
        break;
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      default:
        // An escape this module never writes (a \uXXXX, say) is kept verbatim rather
        // than guessed at.
        out += '\\';
        out += text[index];
        break;
    }
  }
  return out;
}

ProfileSetting* Profile::FindSetting(std::string_view key) {
  for (ProfileSetting& setting : settings) {
    if (setting.key == key) {
      return &setting;
    }
  }
  return nullptr;
}

const ProfileSetting* Profile::FindSetting(std::string_view key) const {
  return const_cast<Profile*>(this)->FindSetting(key);
}

void Profile::Set(std::string_view key, std::string value) {
  if (ProfileSetting* setting = FindSetting(key); setting != nullptr) {
    setting->value = std::move(value);
    setting->style = ValueStyle::kBare;
    return;
  }
  settings.push_back(ProfileSetting{std::string(key), std::move(value), ValueStyle::kBare});
}

void Profile::SetString(std::string_view key, std::string value) {
  if (ProfileSetting* setting = FindSetting(key); setting != nullptr) {
    setting->value = std::move(value);
    setting->style = ValueStyle::kBasic;
    return;
  }
  settings.push_back(ProfileSetting{std::string(key), std::move(value), ValueStyle::kBasic});
}

void Profile::EraseSetting(std::string_view key) {
  settings.erase(std::remove_if(settings.begin(), settings.end(),
                                [key](const ProfileSetting& setting) {
                                  return setting.key == key;
                                }),
                 settings.end());
}

ProfileLoadResult LoadProfile(const fs::path& path) {
  ProfileLoadResult result;

  std::error_code code;
  const bool exists = fs::exists(path, code);
  if (code || !exists) {
    result.status = ProfileStatus::kMissingFile;
    return result;
  }

  std::string text;
  if (!ReadWholeFile(path, &text, &result.error)) {
    result.status = ProfileStatus::kMalformed;
    return result;
  }

  Layout layout;
  if (!Scan(text, &layout, &result.error)) {
    result.status = ProfileStatus::kMalformed;
    result.error = path.string() + ": " + result.error;
    return result;
  }

  Profile profile;
  profile.source_text = text;

  auto malformed = [&](const std::string& message) {
    result.status = ProfileStatus::kMalformed;
    result.error = path.string() + ": " + message;
  };

  if (const KeyRef* ref = FindKey(layout, "", "schema_version"); ref != nullptr) {
    long long version = 0;
    if (ref->style != ValueStyle::kBare || !ParseInteger(ref->value, &version)) {
      malformed("schema_version is not an integer");
      return result;
    }
    if (version != kProfileSchemaVersion) {
      malformed("schema_version " + std::to_string(version) +
                " is not one this build knows (" + std::to_string(kProfileSchemaVersion) + ")");
      return result;
    }
  }
  // A profile without schema_version is a sparse one a person wrote; it is read with the
  // defaults below rather than refused, and a save adds the version.

  auto read_int = [&](std::string_view key, int* out) {
    const KeyRef* ref = FindKey(layout, "window", key);
    if (ref == nullptr) {
      return true;
    }
    long long value = 0;
    if (ref->style != ValueStyle::kBare || !ParseInteger(ref->value, &value)) {
      malformed("window." + std::string(key) + " is not an integer");
      return false;
    }
    *out = static_cast<int>(value);
    return true;
  };
  if (!read_int("width", &profile.window_width) || !read_int("height", &profile.window_height)) {
    return result;
  }

  if (const KeyRef* ref = FindKey(layout, "launcher", "version"); ref != nullptr) {
    long long value = 0;
    if (ref->style != ValueStyle::kBare || !ParseInteger(ref->value, &value)) {
      malformed("launcher.version is not an integer");
      return result;
    }
    profile.launcher_version = static_cast<int>(value);
  }
  if (const KeyRef* ref = FindKey(layout, "launcher", "portable"); ref != nullptr) {
    if (ref->style != ValueStyle::kBare || !ParseBoolean(ref->value, &profile.portable)) {
      malformed("launcher.portable is not true or false");
      return result;
    }
  }

  auto read_string = [&](std::string_view section, std::string_view key, std::string* out) {
    const KeyRef* ref = FindKey(layout, section, key);
    if (ref == nullptr) {
      return true;
    }
    if (ref->style == ValueStyle::kBare) {
      malformed(std::string(section) + "." + std::string(key) + " is not a quoted string");
      return false;
    }
    *out = ref->value;
    return true;
  };
  if (!read_string("launch", "game_dir", &profile.game_dir) ||
      !read_string("launch", "user_data_dir", &profile.user_data_dir) ||
      !read_string("launch", "dlc_dir", &profile.dlc_dir)) {
    return result;
  }

  if (const KeyRef* ref = FindKey(layout, "launch", "target"); ref != nullptr) {
    if (ref->style == ValueStyle::kBare) {
      malformed("launch.target is not a quoted string");
      return result;
    }
    const std::optional<LaunchTarget> target = ParseLaunchTarget(ref->value);
    if (!target) {
      malformed("launch.target `" + ref->value + "` is not common, demo or ultimate");
      return result;
    }
    profile.target = *target;
  }

  for (const KeyRef& ref : layout.keys) {
    if (ref.section == "settings") {
      profile.settings.push_back(ProfileSetting{ref.key, ref.value, ref.style});
    } else if (ref.section == "remap") {
      profile.remap.push_back(ProfileSetting{ref.key, ref.value, ref.style});
    }
  }

  result.status = ProfileStatus::kOk;
  result.profile = std::move(profile);
  return result;
}

bool ComposeProfile(const Profile& profile, std::string* text, std::string* error) {
  if (profile.source_text.empty()) {
    *text = RenderProfile(profile);
    return true;
  }

  Layout layout;
  std::string reason;
  if (!Scan(profile.source_text, &layout, &reason)) {
    *error = reason;
    return false;
  }

  Pending pending;
  Schedule(layout, &pending, "", "schema_version", std::to_string(kProfileSchemaVersion),
           ValueStyle::kBare);
  Schedule(layout, &pending, "launcher", "version", std::to_string(profile.launcher_version),
           ValueStyle::kBare);
  Schedule(layout, &pending, "launcher", "portable", BoolText(profile.portable),
           ValueStyle::kBare);
  Schedule(layout, &pending, "window", "width", std::to_string(profile.window_width),
           ValueStyle::kBare);
  Schedule(layout, &pending, "window", "height", std::to_string(profile.window_height),
           ValueStyle::kBare);
  Schedule(layout, &pending, "launch", "target", LaunchTargetLiteral(profile.target),
           ValueStyle::kBasic);
  Schedule(layout, &pending, "launch", "game_dir", profile.game_dir, ValueStyle::kBasic);
  Schedule(layout, &pending, "launch", "user_data_dir", profile.user_data_dir,
           ValueStyle::kBasic);
  Schedule(layout, &pending, "launch", "dlc_dir", profile.dlc_dir, ValueStyle::kBasic);
  for (const ProfileSetting& setting : profile.settings) {
    Schedule(layout, &pending, "settings", setting.key, setting.value, setting.style);
  }
  for (const ProfileSetting& binding : profile.remap) {
    Schedule(layout, &pending, "remap", binding.key, binding.value, binding.style);
  }
  // `[settings]` and `[remap]` are modelled as a whole, so a key the model no longer has is
  // dropped. Everything outside them is untouched, which is what protects a newer launcher's
  // keys.
  for (const KeyRef& ref : layout.keys) {
    const bool modelled = ref.section == "settings" || ref.section == "remap";
    if (!modelled) {
      continue;
    }
    const std::vector<ProfileSetting>& rows =
        ref.section == "settings" ? profile.settings : profile.remap;
    const bool kept = std::any_of(rows.begin(), rows.end(), [&](const ProfileSetting& row) {
      return row.key == ref.key;
    });
    if (!kept) {
      pending.edits.push_back(Edit{ref.line_begin, ref.line_end, ""});
    }
  }

  *text = Apply(layout, pending);
  return true;
}

bool SaveProfile(const fs::path& path, const Profile& profile, std::string* error) {
  std::string text;
  if (!ComposeProfile(profile, &text, error)) {
    *error = path.string() + ": " + *error;
    return false;
  }

  // A save that would not change a byte leaves the file alone, so an unchanged profile keeps
  // the mtime of its last real change instead of earning a new one every time the launcher
  // closes.
  std::string current;
  if (ReadWholeFile(path, &current, error)) {
    if (current == text) {
      error->clear();
      return true;
    }
  }
  error->clear();

  std::error_code code;
  if (!path.parent_path().empty()) {
    fs::create_directories(path.parent_path(), code);
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    *error = "cannot write " + path.string();
    return false;
  }
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream) {
    *error = "cannot write " + path.string();
    return false;
  }
  return true;
}

std::string RenderProfile(const Profile& profile) {
  const std::string newline = "\n";
  std::string text;
  text += "schema_version = " + std::to_string(kProfileSchemaVersion) + newline;
  text += newline;
  text += "[launcher]" + newline;
  text += "version = " + std::to_string(profile.launcher_version) + newline;
  text += "portable = " + BoolText(profile.portable) + newline;
  text += newline;
  text += "[window]" + newline;
  text += "width = " + std::to_string(profile.window_width) + newline;
  text += "height = " + std::to_string(profile.window_height) + newline;
  text += newline;
  text += "[launch]" + newline;
  text += "target = " + RenderValue(LaunchTargetLiteral(profile.target), ValueStyle::kBasic) +
          newline;
  text += "game_dir = " + RenderValue(profile.game_dir, ValueStyle::kBasic) + newline;
  text += "user_data_dir = " + RenderValue(profile.user_data_dir, ValueStyle::kBasic) + newline;
  text += "dlc_dir = " + RenderValue(profile.dlc_dir, ValueStyle::kBasic) + newline;
  if (!profile.settings.empty()) {
    text += newline;
    text += "[settings]" + newline;
    for (const ProfileSetting& setting : profile.settings) {
      text += setting.key + " = " + RenderValue(setting.value, setting.style) + newline;
    }
  }
  if (!profile.remap.empty()) {
    text += newline;
    text += "[remap]" + newline;
    for (const ProfileSetting& binding : profile.remap) {
      text += binding.key + " = " + RenderValue(binding.value, binding.style) + newline;
    }
  }
  return text;
}

}  // namespace rb_blitz::launcher
