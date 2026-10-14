// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The store in src/settings/store.h. The design mirrors controller-settings/profile.cpp's line patcher
// on purpose — do not rewrite the whole document — but strengthens the write path to meet
// parent plan §4.4:
//
//   * the file is re-read at save time; a key whose on-disk value differs from the value the
//     caller loaded is a conflict and nothing is written (never "last writer wins");
//   * a write goes to a flushed temporary sibling and then atomically replaces the
//     destination, so a failed write keeps the old bytes;
//   * a save that would render the identical text does not open the file at all.
//
// Everything is SDK-free and works on text, so the tests exercise it without a boot.

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "settings/store.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#endif

#include "settings/schema.h"
#include "fs/atomic_file.h"

namespace rb_blitz::settings {

namespace fs = std::filesystem;

namespace {

bool IsHorizontalSpace(char value) { return value == ' ' || value == '\t'; }

bool IsSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' ||
         value == '\v';
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

bool IsBareKeyChar(char value) {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-';
}

std::string UnescapeBasic(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char character = text[i];
    if (character != '\\' || i + 1 >= text.size()) {
      out += character;
      continue;
    }
    const char next = text[++i];
    switch (next) {
      case 'n':
        out += '\n';
        break;
      case 't':
        out += '\t';
        break;
      case 'r':
        out += '\r';
        break;
      case '\\':
        out += '\\';
        break;
      case '"':
        out += '"';
        break;
      default:
        out += next;
        break;
    }
  }
  return out;
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t newline = text.find('\n', start);
    const std::size_t end = newline == std::string::npos ? text.size() : newline + 1;
    lines.push_back(text.substr(start, end - start));
    start = end;
  }
  return lines;
}

std::size_t ContentLen(const std::string& line) {
  std::size_t end = line.find('\n');
  if (end == std::string::npos) {
    end = line.size();
  }
  if (end > 0 && line[end - 1] == '\r') {
    --end;
  }
  return end;
}

std::string LineNewline(const std::string& line) {
  if (line.size() >= 2 && line[line.size() - 2] == '\r' && line.back() == '\n') {
    return "\r\n";
  }
  if (!line.empty() && line.back() == '\n') {
    return "\n";
  }
  return "\n";
}

// Parses an array's inner text ("1920, 1080" or "'a', 'b'") into unquoted tokens.
bool ParseArrayTokens(std::string_view inner, std::vector<std::string>* out) {
  out->clear();
  std::size_t i = 0;
  while (i < inner.size()) {
    while (i < inner.size() && (IsSpace(inner[i]) || inner[i] == ',')) {
      ++i;
    }
    if (i >= inner.size()) {
      break;
    }
    if (inner[i] == '"') {
      std::string raw;
      ++i;
      while (i < inner.size() && inner[i] != '"') {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
          raw += inner[i];
          raw += inner[i + 1];
          i += 2;
          continue;
        }
        raw += inner[i++];
      }
      if (i >= inner.size()) {
        return false;
      }
      ++i;
      out->push_back(UnescapeBasic(raw));
    } else if (inner[i] == '\'') {
      std::string raw;
      ++i;
      while (i < inner.size() && inner[i] != '\'') {
        raw += inner[i++];
      }
      if (i >= inner.size()) {
        return false;
      }
      ++i;
      out->push_back(raw);
    } else {
      std::string token;
      while (i < inner.size() && inner[i] != ',') {
        token += inner[i++];
      }
      out->push_back(Trim(token));
    }
  }
  return true;
}

}  // namespace

namespace toml {

std::string RenderBare(std::string_view value) { return std::string(value); }

std::string RenderBasic(std::string_view value) {
  std::string out = "\"";
  for (const char character : value) {
    switch (character) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        out += character;
        break;
    }
  }
  out += "\"";
  return out;
}

std::string RenderLiteral(std::string_view value) {
  return "'" + std::string(value) + "'";
}

std::string RenderStringList(const std::vector<std::string>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out += ", ";
    }
    out += RenderLiteral(values[i]);
  }
  out += "]";
  return out;
}

std::string QuoteKeyIfNeeded(std::string_view key) {
  if (!key.empty() && std::all_of(key.begin(), key.end(), IsBareKeyChar)) {
    return std::string(key);
  }
  return RenderLiteral(key);
}

bool ParseBareInt(std::string_view text, int64_t* out) {
  if (text.empty()) {
    return false;
  }
  std::size_t i = 0;
  bool negative = false;
  if (text.front() == '-' || text.front() == '+') {
    negative = text.front() == '-';
    i = 1;
  }
  if (i >= text.size()) {
    return false;
  }
  int64_t value = 0;
  for (; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
    value = value * 10 + (text[i] - '0');
  }
  *out = negative ? -value : value;
  return true;
}

bool ParseBareBool(std::string_view text, bool* out) {
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

namespace {

// One key found by a scan: where it lives and how to rewrite just its value.
struct KeyRef {
  std::string section;
  std::string key;
  Style style = Style::kBare;
  std::string value;
  bool scalar = true;
  std::vector<std::string> list;
  bool list_ok = false;
  std::size_t line = 0;
  std::size_t span_end = 0;
  std::size_t value_begin = 0;
  std::size_t value_end = 0;
  std::size_t content_len = 0;
};

struct Scan {
  std::vector<KeyRef> keys;
  std::map<std::string, std::size_t> header_line;
  std::map<std::string, std::size_t> last_key_line;
  std::string newline = "\n";
};

// Unquotes a raw key spelling: `'a.b'`, `"a.b"` and `a-b` all yield `a.b` / `a-b`.
std::string LogicalKey(std::string_view raw) {
  const std::string trimmed = Trim(raw);
  if (trimmed.size() >= 2 && trimmed.front() == '\'' && trimmed.back() == '\'') {
    return trimmed.substr(1, trimmed.size() - 2);
  }
  if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
    return UnescapeBasic(std::string_view(trimmed).substr(1, trimmed.size() - 2));
  }
  return trimmed;
}

bool ScanDocument(const std::string& text, Scan* out, std::string* error) {
  const std::vector<std::string> lines = SplitLines(text);
  if (!lines.empty()) {
    out->newline = LineNewline(lines.front());
  }

  std::string section;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::size_t content_len = ContentLen(lines[i]);
    const std::string_view content(lines[i].data(), content_len);
    const std::string trimmed = Trim(content);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }
    if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[1] == '[') {
      const std::size_t close = trimmed.find("]]");
      if (close == std::string::npos) {
        *error = "line " + std::to_string(i + 1) + ": unterminated [[array table]] header";
        return false;
      }
      section = "\x01" + Trim(std::string_view(trimmed).substr(2, close - 2)) + "#" +
                std::to_string(i);
      out->header_line[section] = i;
      continue;
    }
    if (trimmed.front() == '[') {
      const std::size_t close = trimmed.find(']');
      if (close == std::string::npos) {
        *error = "line " + std::to_string(i + 1) + ": unterminated table header";
        return false;
      }
      section = Trim(std::string_view(trimmed).substr(1, close - 1));
      out->header_line[section] = i;
      continue;
    }

    std::size_t equals = std::string_view::npos;
    {
      char quote = 0;
      for (std::size_t c = 0; c < content.size(); ++c) {
        const char character = content[c];
        if (quote == '"') {
          if (character == '\\') {
            ++c;
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
          equals = c;
          break;
        }
      }
    }
    if (equals == std::string_view::npos) {
      *error = "line " + std::to_string(i + 1) + ": expected `key = value`";
      return false;
    }

    KeyRef ref;
    ref.section = section;
    ref.key = LogicalKey(content.substr(0, equals));
    if (ref.key.empty()) {
      *error = "line " + std::to_string(i + 1) + ": empty key";
      return false;
    }
    ref.line = i;
    ref.content_len = content_len;

    std::size_t value_start = equals + 1;
    while (value_start < content_len && IsHorizontalSpace(content[value_start])) {
      ++value_start;
    }
    if (value_start >= content_len || content[value_start] == '#') {
      *error = "line " + std::to_string(i + 1) + ": missing value for `" + ref.key + "`";
      return false;
    }
    ref.value_begin = value_start;

    const char first = content[value_start];
    if (first == '"') {
      std::size_t c = value_start + 1;
      bool closed = false;
      for (; c < content_len; ++c) {
        if (content[c] == '\\') {
          ++c;
          continue;
        }
        if (content[c] == '"') {
          closed = true;
          break;
        }
      }
      if (!closed) {
        *error = "line " + std::to_string(i + 1) + ": unterminated string for `" + ref.key + "`";
        return false;
      }
      ref.value = UnescapeBasic(content.substr(value_start + 1, c - value_start - 1));
      ref.style = Style::kBasic;
      ref.value_end = c + 1;
      ref.span_end = i;
    } else if (first == '\'') {
      const std::size_t close = content.find('\'', value_start + 1);
      if (close == std::string_view::npos) {
        *error = "line " + std::to_string(i + 1) + ": unterminated literal string for `" +
                 ref.key + "`";
        return false;
      }
      ref.value = std::string(content.substr(value_start + 1, close - value_start - 1));
      ref.style = Style::kLiteral;
      ref.value_end = close + 1;
      ref.span_end = i;
    } else if (first == '[' || first == '{') {
      int depth = 0;
      std::size_t end_line = i;
      std::size_t end_offset = 0;
      bool closed = false;
      for (std::size_t line = i; line < lines.size() && !closed; ++line) {
        const std::size_t len = ContentLen(lines[line]);
        const std::string_view body(lines[line].data(), len);
        const std::size_t from = line == i ? value_start : 0;
        for (std::size_t c = from; c < body.size(); ++c) {
          const char character = body[c];
          if (character == '[' || character == '{' || character == '(') {
            ++depth;
          } else if (character == ']' || character == '}' || character == ')') {
            if (--depth == 0) {
              closed = true;
              end_line = line;
              end_offset = c + 1;
              break;
            }
          }
        }
        if (!closed) {
          end_line = line;
          end_offset = len;
        }
      }
      if (!closed) {
        *error = "line " + std::to_string(i + 1) + ": unterminated array for `" + ref.key + "`";
        return false;
      }
      // The raw text of the value across its span, without the newlines.
      std::string raw;
      for (std::size_t line = i; line <= end_line; ++line) {
        const std::size_t len = ContentLen(lines[line]);
        const std::string_view body(lines[line].data(), len);
        const std::size_t from = line == i ? value_start : 0;
        const std::size_t to = line == end_line ? end_offset : len;
        raw += std::string(body.substr(from, to - from));
        if (line != end_line) {
          raw += ' ';
        }
      }
      ref.style = Style::kBare;
      ref.scalar = false;
      ref.value = raw;
      ref.span_end = end_line;
      ref.value_end = (end_line == i) ? end_offset : content_len;
      // An array of scalars parses as a list; an inline table is opaque.
      if (first == '[') {
        const std::size_t open = raw.find('[');
        const std::size_t close = raw.rfind(']');
        if (open != std::string::npos && close != std::string::npos && close > open) {
          ref.list_ok = ParseArrayTokens(std::string_view(raw).substr(open + 1, close - open - 1),
                                         &ref.list);
        }
      }
    } else {
      std::size_t c = value_start;
      while (c < content_len && content[c] != '#') {
        ++c;
      }
      std::size_t end = c;
      while (end > value_start && IsHorizontalSpace(content[end - 1])) {
        --end;
      }
      ref.value = std::string(content.substr(value_start, end - value_start));
      if (ref.value.empty()) {
        *error = "line " + std::to_string(i + 1) + ": empty value for `" + ref.key + "`";
        return false;
      }
      ref.style = Style::kBare;
      ref.value_end = end;
      ref.span_end = i;
    }

    out->last_key_line[section] = ref.span_end;
    const std::size_t span_end = ref.span_end;
    out->keys.push_back(std::move(ref));
    i = span_end;
  }
  return true;
}

const KeyRef* FindKey(const Scan& scan, std::string_view section, std::string_view key) {
  for (const KeyRef& ref : scan.keys) {
    if (ref.section == section && ref.key == key) {
      return &ref;
    }
  }
  return nullptr;
}

}  // namespace

Document Document::Parse(const std::string& text, bool* ok, std::string* error) {
  Document document;
  document.original_ = text;
  std::string local_error;
  if (!document.reparse(text, &local_error)) {
    if (ok != nullptr) {
      *ok = false;
    }
    if (error != nullptr) {
      *error = local_error;
    }
    return document;
  }
  if (ok != nullptr) {
    *ok = true;
  }
  if (error != nullptr) {
    error->clear();
  }
  return document;
}

bool Document::reparse(std::string next, std::string* error) {
  Scan scan;
  if (!ScanDocument(next, &scan, error)) {
    parsed_ = false;
    error_ = error == nullptr ? std::string() : *error;
    return false;
  }
  text_ = std::move(next);
  parsed_ = true;
  error_.clear();
  return true;
}

bool Document::Has(std::string_view section, std::string_view key) const {
  return Get(section, key).found;
}

Item Document::Get(std::string_view section, std::string_view key) const {
  Scan scan;
  std::string error;
  if (!ScanDocument(text_, &scan, &error)) {
    return Item{};
  }
  const KeyRef* ref = FindKey(scan, section, key);
  if (ref == nullptr) {
    return Item{};
  }
  Item item;
  item.found = true;
  item.scalar = ref->scalar;
  item.style = ref->style;
  item.value = ref->value;
  item.list = ref->list;
  item.list_ok = ref->list_ok;
  return item;
}

std::vector<std::string> Document::Keys(std::string_view section) const {
  Scan scan;
  std::string error;
  std::vector<std::string> keys;
  if (!ScanDocument(text_, &scan, &error)) {
    return keys;
  }
  for (const KeyRef& ref : scan.keys) {
    if (ref.section == section) {
      keys.push_back(ref.key);
    }
  }
  return keys;
}

bool Document::HasSection(std::string_view section) const {
  Scan scan;
  std::string error;
  if (!ScanDocument(text_, &scan, &error)) {
    return false;
  }
  return scan.header_line.count(std::string(section)) != 0;
}

void Document::Set(std::string_view section, std::string_view key, std::string_view rendered_value) {
  Scan scan;
  std::string error;
  if (!ScanDocument(text_, &scan, &error)) {
    return;
  }
  const std::string newline = scan.newline;
  std::vector<std::string> lines = SplitLines(text_);
  const KeyRef* ref = FindKey(scan, section, key);
  if (ref != nullptr) {
    const std::string& first = lines[ref->line];
    const std::string head = first.substr(0, ref->value_begin);
    const std::string tail =
        first.substr(ref->value_end, ref->content_len - ref->value_end);
    std::string rebuilt;
    for (std::size_t i = 0; i < ref->line; ++i) {
      rebuilt += lines[i];
    }
    rebuilt += head + std::string(rendered_value) + tail + LineNewline(first);
    for (std::size_t i = ref->span_end + 1; i < lines.size(); ++i) {
      rebuilt += lines[i];
    }
    std::string ignored;
    reparse(std::move(rebuilt), &ignored);
    return;
  }

  const std::string key_text = QuoteKeyIfNeeded(key);
  const std::string assignment = key_text + " = " + std::string(rendered_value) + newline;

  std::size_t insert_at = lines.size();
  const std::string section_name(section);
  if (const auto header = scan.header_line.find(section_name); header != scan.header_line.end()) {
    if (const auto last = scan.last_key_line.find(section_name);
        last != scan.last_key_line.end()) {
      insert_at = last->second + 1;
    } else {
      insert_at = header->second + 1;
    }
  } else if (!section_name.empty()) {
    // The section does not exist yet: append a header (with a blank separator) and the key.
    std::string appended;
    if (!text_.empty() && text_.back() != '\n') {
      appended += newline;
    }
    if (!text_.empty()) {
      appended += newline;
    }
    appended += "[" + section_name + "]" + newline + assignment;
    std::string ignored;
    reparse(text_ + appended, &ignored);
    return;
  } else {
    // A top-level key: after the last top-level key, else at the very top.
    insert_at = 0;
    for (const KeyRef& other : scan.keys) {
      if (other.section == section_name) {
        insert_at = other.span_end + 1;
      }
    }
  }

  std::string rebuilt;
  for (std::size_t i = 0; i < insert_at && i < lines.size(); ++i) {
    rebuilt += lines[i];
  }
  rebuilt += assignment;
  for (std::size_t i = insert_at; i < lines.size(); ++i) {
    rebuilt += lines[i];
  }
  std::string ignored;
  reparse(std::move(rebuilt), &ignored);
}

bool Document::SetIfAbsent(std::string_view section, std::string_view key,
                           std::string_view rendered_value) {
  if (Has(section, key)) {
    return false;
  }
  Set(section, key, rendered_value);
  return true;
}

void Document::Erase(std::string_view section, std::string_view key) {
  Scan scan;
  std::string error;
  if (!ScanDocument(text_, &scan, &error)) {
    return;
  }
  const KeyRef* ref = FindKey(scan, section, key);
  if (ref == nullptr) {
    return;
  }
  std::vector<std::string> lines = SplitLines(text_);
  std::string rebuilt;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i >= ref->line && i <= ref->span_end) {
      continue;
    }
    rebuilt += lines[i];
  }
  std::string ignored;
  reparse(std::move(rebuilt), &ignored);
}

}  // namespace toml

// --- typed document --------------------------------------------------------

namespace {

bool ParseValueFromItem(const Descriptor& descriptor, const toml::Item& item, Value* out) {
  switch (descriptor.value_kind) {
    case ValueKind::kBool: {
      bool value = false;
      if (!toml::ParseBareBool(item.value, &value)) {
        return false;
      }
      out->kind = ValueKind::kBool;
      out->boolean = value;
      return true;
    }
    case ValueKind::kInt: {
      int64_t value = 0;
      if (!toml::ParseBareInt(item.value, &value)) {
        return false;
      }
      out->kind = ValueKind::kInt;
      out->integer = static_cast<int32_t>(value);
      return true;
    }
    case ValueKind::kEnum:
    case ValueKind::kString: {
      if (item.value.empty()) {
        return false;
      }
      out->kind = ValueKind::kEnum;
      out->text = item.value;
      return true;
    }
    case ValueKind::kResolution: {
      if (item.scalar && item.value == "auto") {
        out->kind = ValueKind::kResolution;
        out->width = out->height = 0;
        return true;
      }
      if (!item.list_ok || item.list.size() != 2) {
        return false;
      }
      int64_t width = 0;
      int64_t height = 0;
      if (!toml::ParseBareInt(item.list[0], &width) ||
          !toml::ParseBareInt(item.list[1], &height) || width <= 0 || height <= 0 ||
          width > INT32_MAX || height > INT32_MAX) {
        return false;
      }
      out->kind = ValueKind::kResolution;
      out->width = static_cast<int32_t>(width);
      out->height = static_cast<int32_t>(height);
      return true;
    }
    case ValueKind::kList: {
      if (!item.list_ok) {
        return false;
      }
      out->kind = ValueKind::kList;
      out->list = item.list;
      return true;
    }
    case ValueKind::kNone:
      return false;
  }
  return false;
}

std::string ProvenanceKey(const StorageKey& storage) {
  return std::string(storage.section) + "." + std::string(storage.key);
}

}  // namespace

bool PersistedValue(SettingId id, const Value& value, std::string* rendered, toml::Style* style) {
  const StorageKey storage = StorageKeyFor(id);
  if (!storage.persisted()) {
    return false;
  }
  switch (value.kind) {
    case ValueKind::kBool:
      *rendered = value.boolean ? "true" : "false";
      *style = toml::Style::kBare;
      return true;
    case ValueKind::kInt:
      *rendered = std::to_string(value.integer);
      *style = toml::Style::kBare;
      return true;
    case ValueKind::kEnum:
      if (value.text.empty()) {
        return false;
      }
      *rendered = toml::RenderLiteral(value.text);
      *style = toml::Style::kLiteral;
      return true;
    case ValueKind::kString:
      *rendered = toml::RenderBasic(value.text);
      *style = toml::Style::kBasic;
      return true;
    case ValueKind::kResolution:
      if (IsAutoResolution(value)) {
        *rendered = "'auto'";
        *style = toml::Style::kLiteral;
        return true;
      }
      if (value.width <= 0 || value.height <= 0) {
        return false;
      }
      *rendered =
          "[" + std::to_string(value.width) + ", " + std::to_string(value.height) + "]";
      *style = toml::Style::kBare;
      return true;
    case ValueKind::kList:
      *rendered = toml::RenderStringList(value.list);
      *style = toml::Style::kBare;
      return true;
    case ValueKind::kNone:
      return false;
  }
  return false;
}

DocumentLoad LoadSettingsDocument(const fs::path& file) {
  DocumentLoad load;
  std::string text;
  bool existed = false;
  if (!ReadFileText(file, &text, &existed)) {
    load.status = DocumentStatus::kMalformed;
    load.error = "cannot read " + file.string();
    return load;
  }
  if (!existed) {
    load.status = DocumentStatus::kMissing;
    return load;
  }

  bool ok = false;
  std::string error;
  toml::Document doc = toml::Document::Parse(text, &ok, &error);
  if (!ok) {
    load.status = DocumentStatus::kMalformed;
    load.error = file.string() + ": " + error;
    return load;
  }

  const toml::Item version = doc.Get("", "schema_version");
  if (!version.found) {
    load.status = DocumentStatus::kMalformed;
    load.error = file.string() + ": no schema_version";
    return load;
  }
  int64_t schema_version = 0;
  if (!toml::ParseBareInt(version.value, &schema_version)) {
    load.status = DocumentStatus::kMalformed;
    load.error = file.string() + ": schema_version is not an integer";
    return load;
  }
  if (schema_version != 1) {
    load.status = DocumentStatus::kUnsupportedVersion;
    load.error = file.string() + ": schema_version " + std::to_string(schema_version) +
                 " is not supported";
    return load;
  }
  load.document.schema_version = static_cast<int32_t>(schema_version);

  for (const schema::SchemaRow& row : schema::Rows()) {
    if (!row.storage.persisted() || IsAction(row.descriptor.id)) {
      continue;
    }
    const toml::Item item = doc.Get(row.storage.section, row.storage.key);
    if (!item.found) {
      continue;
    }
    Value value;
    if (ParseValueFromItem(row.descriptor, item, &value)) {
      load.document.values[row.descriptor.id] = std::move(value);
    }
  }

  for (const std::string& key : doc.Keys("provenance")) {
    for (const schema::SchemaRow& row : schema::Rows()) {
      if (!row.storage.persisted()) {
        continue;
      }
      if (ProvenanceKey(row.storage) != key) {
        continue;
      }
      const toml::Item item = doc.Get("provenance", key);
      Origin origin = Origin::kDefault;
      if (item.value == "manual") {
        origin = Origin::kManual;
      } else if (item.value == "automatic") {
        origin = Origin::kAutomatic;
      }
      load.document.origins[row.descriptor.id] = origin;
    }
  }

  if (const toml::Item policy = doc.Get("automatic", "policy_version"); policy.found) {
    int64_t value = 0;
    if (toml::ParseBareInt(policy.value, &value)) {
      load.document.automatic_policy_version = static_cast<int32_t>(value);
    }
  }

  if (const toml::Item imported = doc.Get("migration", "imported"); imported.found) {
    bool value = false;
    if (toml::ParseBareBool(imported.value, &value)) {
      load.document.migration_imported = value;
    }
  }

  for (const std::string& key : doc.Keys("ultimate_overrides")) {
    const toml::Item item = doc.Get("ultimate_overrides", key);
    if (!item.found) {
      continue;
    }
    if (item.scalar) {
      Value value;
      if (item.style == toml::Style::kBare &&
          (item.value == "true" || item.value == "false")) {
        value.kind = ValueKind::kBool;
        value.boolean = item.value == "true";
      } else if (item.style == toml::Style::kBare) {
        int64_t number = 0;
        if (toml::ParseBareInt(item.value, &number)) {
          value.kind = ValueKind::kInt;
          value.integer = static_cast<int32_t>(number);
        } else {
          continue;
        }
      } else {
        value.kind = ValueKind::kEnum;
        value.text = item.value;
      }
      load.document.ultimate_overrides[key] = std::move(value);
    }
  }

  load.status = DocumentStatus::kOk;
  return load;
}

SaveOutcome SaveSettingsDocument(const fs::path& file, const SaveRequest& request) {
  SaveOutcome outcome;
  rb_blitz::fs::FileLock lock;
  std::string lock_error;
  if (!lock.Acquire(file, &lock_error)) {
    outcome.status = CommitStatus::kIoError;
    outcome.errors.push_back(lock_error);
    return outcome;
  }
  std::string text;
  bool existed = false;
  if (!ReadFileText(file, &text, &existed)) {
    outcome.status = CommitStatus::kIoError;
    outcome.errors.push_back("cannot read " + file.string());
    return outcome;
  }

  bool ok = true;
  std::string error;
  toml::Document doc = toml::Document::Parse(existed ? text : std::string(), &ok, &error);
  if (!ok) {
    outcome.status = CommitStatus::kInvalid;
    outcome.errors.push_back(file.string() + ": " + error);
    return outcome;
  }
  if (existed) {
    const toml::Item version = doc.Get("", "schema_version");
    if (!version.found) {
      outcome.status = CommitStatus::kInvalid;
      outcome.errors.push_back(file.string() + ": no schema_version");
      return outcome;
    }
    int64_t schema_version = 0;
    if (!toml::ParseBareInt(version.value, &schema_version) || schema_version != 1) {
      outcome.status = CommitStatus::kInvalid;
      outcome.errors.push_back(file.string() + ": unsupported schema_version");
      return outcome;
    }
  }

  // Conflict detection: a key whose on-disk value differs from the loaded base is another
  // writer's, so nothing is written rather than clobbering it.
  for (const auto& [id, desired] : request.changed) {
    (void)desired;
    const StorageKey storage = StorageKeyFor(id);
    if (!storage.persisted()) {
      continue;
    }
    Value disk;
    bool disk_found = false;
    const toml::Item item = doc.Get(storage.section, storage.key);
    if (item.found) {
      const schema::SchemaRow* row = schema::FindRow(id);
      if (row != nullptr && ParseValueFromItem(row->descriptor, item, &disk)) {
        disk_found = true;
      }
    }
    const auto base = request.base.find(id);
    const bool base_found = base != request.base.end();
    bool conflict = false;
    if (base_found != disk_found) {
      conflict = true;
    } else if (base_found && !ValueEquals(base->second, disk)) {
      conflict = true;
    }
    if (conflict) {
      outcome.conflicts.push_back(id);
    }
  }
  if (!outcome.conflicts.empty()) {
    outcome.status = CommitStatus::kConflict;
    return outcome;
  }

  doc.SetIfAbsent("", "schema_version", "1");

  for (const auto& [id, value] : request.changed) {
    std::string rendered;
    toml::Style style = toml::Style::kBare;
    if (!PersistedValue(id, value, &rendered, &style)) {
      continue;
    }
    const StorageKey storage = StorageKeyFor(id);
    doc.Set(storage.section, storage.key, rendered);
  }

  // Provenance is per field; a row back at its default drops its entry so the table stays
  // small. A manual/automatic marker is never inferred from the value. Only the rows the
  // request names are touched, so a save that does not manage provenance cannot erase a
  // marker another save wrote.
  for (const auto& [id, origin] : request.origins) {
    const StorageKey storage = StorageKeyFor(id);
    if (!storage.persisted()) {
      continue;
    }
    const std::string key = ProvenanceKey(storage);
    if (origin == Origin::kDefault) {
      doc.Erase("provenance", key);
    } else {
      doc.Set("provenance", key, toml::RenderLiteral(OriginToken(origin)));
    }
  }

  for (const auto& [key, value] : request.overrides_changed) {
    std::string rendered;
    toml::Style style = toml::Style::kBare;
    switch (value.kind) {
      case ValueKind::kBool:
        rendered = value.boolean ? "true" : "false";
        break;
      case ValueKind::kInt:
        rendered = std::to_string(value.integer);
        break;
      case ValueKind::kEnum:
      case ValueKind::kString:
        rendered = toml::RenderLiteral(value.text);
        break;
      default:
        continue;
    }
    doc.Set("ultimate_overrides", key, rendered);
  }
  for (const std::string& key : request.overrides_removed) {
    doc.Erase("ultimate_overrides", key);
  }

  if (request.set_policy_version) {
    doc.Set("automatic", "policy_version", std::to_string(request.policy_version));
  }
  if (request.set_migration_imported) {
    doc.Set("migration", "imported", "true");
  }

  if (!doc.dirty()) {
    outcome.status = CommitStatus::kNoChanges;
    return outcome;
  }

  if (!WriteFileAtomic(file, doc.Render(), &error)) {
    outcome.status = CommitStatus::kIoError;
    outcome.errors.push_back(error);
    return outcome;
  }
  outcome.status = CommitStatus::kSaved;
  outcome.wrote = true;
  return outcome;
}

// --- Store -----------------------------------------------------------------

Store::Store(StorePaths paths) : paths_(std::move(paths)) {}

DocumentLoad Store::Load() {
  load_ = LoadSettingsDocument(paths_.settings_file);
  document_ = load_.document;
  base_.clear();
  for (const auto& [id, value] : document_.values) {
    base_[id] = value;
  }
  changed_.clear();
  origins_.clear();
  overrides_changed_.clear();
  overrides_removed_.clear();
  set_policy_version_ = false;
  set_migration_imported_ = false;
  return load_;
}

void Store::MarkChanged(SettingId id, const Value& value, Origin origin) {
  document_.values[id] = value;
  document_.origins[id] = origin;
  changed_[id] = value;
  origins_[id] = origin;
}

void Store::MarkOverride(std::string key, const Value& value) {
  document_.ultimate_overrides[key] = value;
  overrides_changed_[key] = value;
  overrides_removed_.erase(std::remove(overrides_removed_.begin(), overrides_removed_.end(), key),
                           overrides_removed_.end());
}

void Store::RemoveOverride(std::string_view key) {
  document_.ultimate_overrides.erase(std::string(key));
  overrides_changed_.erase(std::string(key));
  overrides_removed_.push_back(std::string(key));
}

void Store::MarkPolicyVersion(int32_t version) {
  document_.automatic_policy_version = version;
  set_policy_version_ = true;
}

void Store::MarkMigrationImported() { set_migration_imported_ = true; }

bool Store::has_changes() const {
  return !changed_.empty() || !overrides_changed_.empty() || !overrides_removed_.empty() ||
         set_policy_version_ || set_migration_imported_;
}

SaveOutcome Store::Save() {
  SaveRequest request;
  request.base = base_;
  request.changed = changed_;
  for (const auto& [id, origin] : origins_) {
    request.origins[id] = origin;
  }
  for (const schema::SchemaRow& row : schema::Rows()) {
    if (!row.storage.persisted()) {
      continue;
    }
    if (const auto origin = document_.origins.find(row.descriptor.id);
        origin != document_.origins.end()) {
      request.origins[row.descriptor.id] = origin->second;
    }
  }
  request.overrides_changed = overrides_changed_;
  request.overrides_removed = overrides_removed_;
  request.set_policy_version = set_policy_version_;
  request.policy_version = document_.automatic_policy_version;
  request.set_migration_imported = set_migration_imported_;

  SaveOutcome outcome = SaveSettingsDocument(paths_.settings_file, request);
  if (outcome.wrote) {
    for (const auto& [id, value] : changed_) {
      base_[id] = value;
    }
    changed_.clear();
    origins_.clear();
    overrides_changed_.clear();
    overrides_removed_.clear();
    set_policy_version_ = false;
    set_migration_imported_ = false;
  }
  return outcome;
}

// --- file helpers ----------------------------------------------------------

bool ReadFileText(const fs::path& file, std::string* text, bool* existed) {
  std::error_code ec;
  *existed = fs::exists(file, ec);
  if (ec) return false;
  if (!*existed) {
    *existed = false;
    text->clear();
    return true;
  }
  const auto size = fs::file_size(file, ec);
  if (ec || size > 4 * 1024 * 1024) return false;
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    text->clear();
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *text = buffer.str();
  return true;
}

bool WriteFileAtomic(const fs::path& file, const std::string& text, std::string* error) {
  return rb_blitz::fs::WriteAtomic(file, text, error);
}

}  // namespace rb_blitz::settings
