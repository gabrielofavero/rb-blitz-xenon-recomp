// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Compiles launcher/config/settings.toml (Contract 1 in docs/plans/launcher-plan.md
// §4.1) into launcher/out/generated/settings_table.h - the table the launcher builds
// its widgets, its tooltips and the game's command line from.
//
// It is also the schema's validator, because the rules the table has to satisfy are the
// difference between a row the launcher can render and one it cannot:
//
//   * a duplicate `key` (two rows would fight over one profile entry and one flag),
//   * a missing `tooltip` (the bottom bar has nothing to name for the focused row),
//   * `applies = "live"` without an evidence comment (a "live" claim nothing supports),
//   * a `group` that is not declared, an unknown `kind`/`tab`/`argv`, an `enum` without
//     `choices` or whose default is not among them, a `path_*` row without `validate`.
//
// The build fails here rather than shipping a blank tooltip. Run with --help for the
// argument list.
//
// This file is deliberately standalone: it is the first thing the launcher build
// compiles, so it must not depend on src/ or on the SDK.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void Fail(const std::string& message) {
  std::fprintf(stderr, "embed_settings: error: %s\n", message.c_str());
  ++g_failures;
}

// --- text helpers ---------------------------------------------------------

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

void TrimTrailingInPlace(std::string* text) {
  while (!text->empty() && IsSpace(text->back())) {
    text->pop_back();
  }
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> SplitList(std::string_view text, char separator) {
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    std::size_t end = text.find(separator, begin);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    parts.push_back(Trim(text.substr(begin, end - begin)));
    if (end == text.size()) {
      break;
    }
    begin = end + 1;
  }
  return parts;
}

// Basic strings use the escapes this file's schema needs (\\ \" \n \t \r); anything else
// keeps both characters, so a stray backslash is visible instead of silently dropped.
std::string UnescapeBasic(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\' || i + 1 >= text.size()) {
      out.push_back(text[i]);
      continue;
    }
    const char next = text[++i];
    switch (next) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '"':
        out.push_back('"');
        break;
      default:
        out.push_back('\\');
        out.push_back(next);
        break;
    }
  }
  return out;
}

// Splits a line at the first `#` outside a string, which is what TOML does.
void SplitComment(std::string_view line, std::string* code, std::string* comment) {
  char quote = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char character = line[i];
    if (quote == '"') {
      if (character == '\\') {
        ++i;
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
    if (character == '#') {
      *code = std::string(line.substr(0, i));
      *comment = std::string(line.substr(i + 1));
      return;
    }
  }
  *code = std::string(line);
  comment->clear();
}

bool ReadTextFile(const fs::path& path, std::string* out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    Fail("cannot open " + path.string());
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *out = buffer.str();
  return true;
}

bool WriteTextFile(const fs::path& path, std::string_view text) {
  std::error_code code;
  if (const fs::path parent = path.parent_path(); !parent.empty()) {
    fs::create_directories(parent, code);
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    Fail("cannot write " + path.string());
    return false;
  }
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream) {
    Fail("cannot write " + path.string());
    return false;
  }
  return true;
}

// --- the TOML subset ------------------------------------------------------

// `[[table]]` array entries plus `key = value`, which is all settings.toml is. Keeping
// this separate from any other parser is the price of a dependency-free first build
// step; the schema stays inside the subset both ends understand.
struct Value {
  std::string key;
  std::string text;                  // unquoted scalar, empty for an array
  std::vector<std::string> list;     // the elements of `choices`
  std::string comment;               // the trailing `# ...` on the key's own line
  bool quoted = false;
  bool is_array = false;
  std::size_t line = 0;              // 0-based index into Document::lines
};

struct Entry {
  std::string type;  // "group" | "setting"
  std::size_t line = 0;

  std::vector<Value> values;

  const Value* Find(std::string_view wanted) const {
    const auto found = std::find_if(values.begin(), values.end(),
                                    [wanted](const Value& value) { return value.key == wanted; });
    return found == values.end() ? nullptr : &*found;
  }
};

struct Document {
  std::vector<std::string> lines;
  std::vector<Entry> entries;
  bool has_schema_version = false;
  long schema_version = 0;
};

bool ParseDocument(const std::string& text, Document* out) {
  out->lines.clear();
  out->entries.clear();

  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line(text, start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    out->lines.push_back(std::move(line));
    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }

  for (std::size_t index = 0; index < out->lines.size(); ++index) {
    std::string code;
    std::string comment;
    SplitComment(out->lines[index], &code, &comment);
    const std::string trimmed = Trim(code);
    if (trimmed.empty()) {
      continue;
    }

    if (StartsWith(trimmed, "[[")) {
      const std::size_t close = trimmed.find("]]");
      if (close == std::string::npos) {
        Fail("settings.toml line " + std::to_string(index + 1) +
             ": unterminated array table header");
        continue;
      }
      Entry entry;
      entry.type = Trim(std::string_view(trimmed).substr(2, close - 2));
      entry.line = index;
      if (entry.type != "group" && entry.type != "setting") {
        Fail("settings.toml line " + std::to_string(index + 1) + ": unknown table [[" +
             entry.type + "]]; only [[group]] and [[setting]] exist");
        continue;
      }
      out->entries.push_back(std::move(entry));
      continue;
    }

    if (trimmed.front() == '[') {
      Fail("settings.toml line " + std::to_string(index + 1) +
           ": only the [[group]] and [[setting]] array tables exist");
      continue;
    }

    const std::size_t equals = trimmed.find('=');
    if (equals == std::string::npos) {
      Fail("settings.toml line " + std::to_string(index + 1) + ": expected `key = value`");
      continue;
    }
    const std::string key_name = Trim(std::string_view(trimmed).substr(0, equals));
    const std::string raw_value = Trim(std::string_view(trimmed).substr(equals + 1));
    // schema_version and nothing else lives at the top level.
    if (key_name == "schema_version") {
      char* end = nullptr;
      out->schema_version = std::strtol(raw_value.c_str(), &end, 10);
      out->has_schema_version = end != nullptr && *end == '\0' && !raw_value.empty();
      if (!out->has_schema_version) {
        Fail("settings.toml line " + std::to_string(index + 1) + ": `schema_version = " +
             raw_value + "` is not a number");
      }
      continue;
    }
    if (out->entries.empty()) {
      Fail("settings.toml line " + std::to_string(index + 1) +
           ": unexpected top-level key `" + key_name + "`; rows belong under [[setting]]");
      continue;
    }

    Value value;
    value.key = key_name;
    value.line = index;
    value.comment = Trim(comment);
    std::string raw = Trim(std::string_view(trimmed).substr(equals + 1));

    if (StartsWith(raw, "\"\"\"") || StartsWith(raw, "'''")) {
      const std::string delimiter = raw.substr(0, 3);
      std::string source = raw.substr(3);
      std::size_t found = source.find(delimiter);
      std::size_t consumed = index;
      while (found == std::string::npos) {
        if (++consumed >= out->lines.size()) {
          break;
        }
        source += "\n";
        source += out->lines[consumed];
        found = source.find(delimiter);
      }
      if (found == std::string::npos) {
        Fail("settings.toml line " + std::to_string(index + 1) + ": unterminated multi-line `" +
             value.key + "`");
        break;
      }
      std::string body = source.substr(0, found);
      if (!body.empty() && body.front() == '\n') {
        body.erase(0, 1);
      }
      TrimTrailingInPlace(&body);
      value.text = delimiter[0] == '"' ? UnescapeBasic(body) : body;
      value.quoted = true;
      index = consumed;
    } else if (raw.size() >= 2 && (raw.front() == '"' || raw.front() == '\'')) {
      const char quote = raw.front();
      std::size_t close = std::string::npos;
      for (std::size_t i = 1; i < raw.size(); ++i) {
        if (quote == '"' && raw[i] == '\\') {
          ++i;
        } else if (raw[i] == quote) {
          close = i;
          break;
        }
      }
      if (close == std::string::npos) {
        Fail("settings.toml line " + std::to_string(index + 1) + ": unterminated string for `" +
             value.key + "`");
        continue;
      }
      const std::string_view body = std::string_view(raw).substr(1, close - 1);
      value.text = quote == '"' ? UnescapeBasic(body) : std::string(body);
      value.quoted = true;
    } else if (raw.size() >= 2 && raw.front() == '[') {
      const std::size_t close = raw.rfind(']');
      if (close == std::string::npos) {
        Fail("settings.toml line " + std::to_string(index + 1) + ": unterminated array for `" +
             value.key + "`");
        continue;
      }
      value.is_array = true;
      for (std::string element : SplitList(std::string_view(raw).substr(1, close - 1), ',')) {
        if (element.size() >= 2 && (element.front() == '"' || element.front() == '\'')) {
          const char quote = element.front();
          if (element.back() == quote) {
            element = quote == '"'
                          ? UnescapeBasic(std::string_view(element).substr(1, element.size() - 2))
                          : element.substr(1, element.size() - 2);
          }
        }
        value.list.push_back(std::move(element));
      }
    } else {
      value.text = raw;
    }

    out->entries.back().values.push_back(std::move(value));
  }

  return g_failures == 0;
}

// --- the schema -----------------------------------------------------------

enum class Kind { kBool, kInt, kFloat, kEnum, kString, kPathDir, kPathFile };
enum class Applies { kRestart, kLive };
enum class Tab { kGeneral, kGraphics, kController };

const std::map<std::string, Kind, std::less<>>& Kinds() {
  static const std::map<std::string, Kind, std::less<>> kinds = {
      {"bool", Kind::kBool},   {"int", Kind::kInt},         {"float", Kind::kFloat},
      {"enum", Kind::kEnum},   {"string", Kind::kString},   {"path_dir", Kind::kPathDir},
      {"path_file", Kind::kPathFile},
  };
  return kinds;
}

const std::map<std::string, Tab, std::less<>>& Tabs() {
  static const std::map<std::string, Tab, std::less<>> tabs = {
      {"general", Tab::kGeneral}, {"graphics", Tab::kGraphics}, {"controller", Tab::kController},
  };
  return tabs;
}

struct GroupRow {
  std::string name;
  Tab tab = Tab::kGeneral;
  bool unavailable = false;
  std::string note;
};

struct SettingRow {
  std::string key;
  Tab tab = Tab::kGeneral;
  std::string group;
  std::string label;
  Kind kind = Kind::kString;
  Applies applies = Applies::kRestart;
  std::string default_text;
  std::string tooltip;
  std::string choices;   // comma-separated, empty unless kEnum
  std::string validate;  // pipe-separated rules, empty unless a path kind
  std::string evidence;  // the `live:` comment's tail, empty unless kLive
  // Optional bounds for an int/float row, so the widget's slider carries the row's real
  // limits instead of the launcher inventing one.
  bool has_range = false;
  std::string min_text;
  std::string max_text;
  bool argv_flag = true;
};

struct Schema {
  long schema_version = 0;
  std::vector<GroupRow> groups;
  std::vector<SettingRow> settings;
};

std::string Location(std::size_t line) {
  return "settings.toml line " + std::to_string(line + 1) + ": ";
}

// A required scalar. Returns false and reports when it is missing or empty.
bool Require(const Entry& entry, std::string_view key,
             std::string* out) {
  const Value* value = entry.Find(key);
  if (value == nullptr) {
    Fail(Location(entry.line) + "[" + entry.type + "] is missing `" +
         std::string(key) + "`");
    return false;
  }
  if (value->is_array) {
    Fail(Location(value->line) + "`" + std::string(key) + "` must not be an array");
    return false;
  }
  if (value->text.empty()) {
    Fail(Location(value->line) + "`" + std::string(key) + "` is empty");
    return false;
  }
  *out = value->text;
  return true;
}

std::optional<Tab> ReadTab(const Entry& entry) {
  std::string tab;
  if (!Require(entry, "tab", &tab)) {
    return std::nullopt;
  }
  const auto found = Tabs().find(tab);
  if (found == Tabs().end()) {
    if (tab == "experimental") {
      Fail(Location(entry.line) +
           "tab `experimental` does not exist: the tab set is General, Graphics, "
           "Controller (D14)");
    } else {
      Fail(Location(entry.line) + "unknown tab `" + tab +
           "`; expected general, graphics or controller");
    }
    return std::nullopt;
  }
  return found->second;
}

bool IsNumeric(std::string_view text, bool allow_fraction) {
  if (text.empty()) {
    return false;
  }
  std::size_t i = (text.front() == '-' || text.front() == '+') ? 1 : 0;
  if (i == text.size()) {
    return false;
  }
  bool digit_seen = false;
  bool point_seen = false;
  for (; i < text.size(); ++i) {
    const char character = text[i];
    if (character >= '0' && character <= '9') {
      digit_seen = true;
    } else if (character == '.' && allow_fraction && !point_seen) {
      point_seen = true;
    } else {
      return false;
    }
  }
  return digit_seen;
}

// The comment that has to back a `live` row: on the `applies` line, or on a
// comment-only line directly above it, which is how the schema reads best when the
// evidence is a full sentence.
std::string LiveEvidence(const Document& document, const Value& applies) {
  auto after_prefix = [](std::string_view comment) -> std::optional<std::string> {
    if (!StartsWith(comment, "live:")) {
      return std::nullopt;
    }
    const std::string tail = Trim(comment.substr(5));
    if (tail.empty()) {
      return std::nullopt;
    }
    return tail;
  };

  if (const auto found = after_prefix(applies.comment)) {
    return *found;
  }
  if (applies.line > 0) {
    std::string code;
    std::string comment;
    SplitComment(document.lines[applies.line - 1], &code, &comment);
    if (Trim(code).empty()) {
      if (const auto found = after_prefix(Trim(comment))) {
        return *found;
      }
    }
  }
  Fail(Location(applies.line) +
       "`applies = \"live\"` needs an evidence comment naming what makes the change land: "
       "add `# live: <the change callback or the re-read site>`");
  return {};
}

std::unique_ptr<Schema> Build(const Document& document) {
  auto schema = std::make_unique<Schema>();
  schema->schema_version = document.schema_version;

  if (!document.has_schema_version) {
    Fail("settings.toml has no `schema_version`");
  } else if (document.schema_version != 1) {
    Fail("settings.toml schema_version " + std::to_string(document.schema_version) +
         " is not one this tool knows (1)");
  }

  std::set<std::pair<int, std::string>> declared_groups;
  std::set<std::pair<int, std::string>> group_rows;

  for (const Entry& entry : document.entries) {
    if (entry.type != "group") {
      continue;
    }
    GroupRow group;
    if (!Require(entry, "name", &group.name)) {
      continue;
    }
    const std::optional<Tab> tab = ReadTab(entry);
    if (!tab) {
      continue;
    }
    group.tab = *tab;

    if (const Value* status = entry.Find("status"); status != nullptr) {
      if (status->text == "unavailable") {
        group.unavailable = true;
      } else {
        Fail(Location(status->line) + "unknown group status `" + status->text +
             "`; the only status is \"unavailable\"");
      }
    }
    if (group.unavailable) {
      if (!Require(entry, "note", &group.note)) {
        continue;
      }
    } else if (entry.Find("note") != nullptr) {
      Fail(Location(entry.line) + "group `" + group.name +
           "` has a note but no status; a note is the one line an unavailable group shows");
    }

    const auto key = std::make_pair(static_cast<int>(group.tab), group.name);
    if (!declared_groups.insert(key).second) {
      Fail(Location(entry.line) + "duplicate group `" + group.name + "`");
      continue;
    }
    schema->groups.push_back(std::move(group));
  }

  std::set<std::string> keys;
  for (const Entry& entry : document.entries) {
    if (entry.type != "setting") {
      continue;
    }
    const std::size_t line = entry.line;
    SettingRow row;

    if (!Require(entry, "key", &row.key)) {
      continue;
    }
    if (!keys.insert(row.key).second) {
      Fail(Location(entry.line) + "duplicate key `" + row.key +
           "`; one key, one row (and one flag)");
      continue;
    }
    const std::optional<Tab> tab = ReadTab(entry);
    if (!tab) {
      continue;
    }
    row.tab = *tab;
    if (!Require(entry, "group", &row.group) ||
        !Require(entry, "label", &row.label)) {
      continue;
    }
    if (declared_groups.find(std::make_pair(static_cast<int>(row.tab), row.group)) ==
        declared_groups.end()) {
      Fail(Location(line) + "row `" + row.key + "` names group `" + row.group +
           "`, which is not declared for that tab");
      continue;
    }
    group_rows.insert(std::make_pair(static_cast<int>(row.tab), row.group));

    std::string kind;
    if (!Require(entry, "kind", &kind)) {
      continue;
    }
    const auto kind_found = Kinds().find(kind);
    if (kind_found == Kinds().end()) {
      Fail(Location(line) + "row `" + row.key + "` has unknown kind `" + kind + "`");
      continue;
    }
    row.kind = kind_found->second;

    // `default = ""` is a real default (an unset path or an unset preset), so an
    // explicitly quoted empty string is accepted where a bare empty value is not.
    const Value* default_value = entry.Find("default");
    if (default_value == nullptr || default_value->is_array ||
        (default_value->text.empty() && !default_value->quoted)) {
      Fail(Location(default_value != nullptr ? default_value->line : entry.line) + "row `" +
           row.key + "` has no `default`");
      continue;
    }
    row.default_text = default_value->text;
    const Value* tooltip = entry.Find("tooltip");
    if (tooltip == nullptr || tooltip->is_array || tooltip->text.empty()) {
      Fail(Location(line) + "row `" + row.key +
           "` has no tooltip; the bottom bar names the focused row's tooltip by "
           "construction (D7)");
      continue;
    }
    row.tooltip = tooltip->text;

    std::string applies;
    if (!Require(entry, "applies", &applies)) {
      continue;
    }
    if (applies == "live") {
      row.applies = Applies::kLive;
      const Value* value = entry.Find("applies");
      row.evidence = LiveEvidence(document, *value);
    } else if (applies != "restart") {
      Fail(Location(line) + "row `" + row.key + "` has unknown applies `" + applies +
           "`; expected restart or live");
      continue;
    }

    std::string argv;
    if (!Require(entry, "argv", &argv)) {
      continue;
    }
    if (argv == "flag") {
      row.argv_flag = true;
      if (row.key.find('.') != std::string::npos) {
        Fail(Location(line) + "row `" + row.key +
             "` is argv = \"flag\" but is not a cvar name; a launcher-level row is argv = "
             "\"none\"");
        continue;
      }
    } else if (argv == "none") {
      row.argv_flag = false;
    } else {
      Fail(Location(line) + "row `" + row.key + "` has unknown argv `" + argv +
           "`; expected flag or none");
      continue;
    }

    const Value* choices = entry.Find("choices");
    const Value* validate = entry.Find("validate");
    if (row.kind == Kind::kEnum) {
      if (choices == nullptr || !choices->is_array || choices->list.empty()) {
        Fail(Location(line) + "enum row `" + row.key + "` has no `choices`");
        continue;
      }
      for (const std::string& choice : choices->list) {
        if (!row.choices.empty()) {
          row.choices += ",";
        }
        row.choices += choice;
      }
      if (std::find(choices->list.begin(), choices->list.end(), row.default_text) ==
          choices->list.end()) {
        Fail(Location(line) + "enum row `" + row.key + "` defaults to `" +
             row.default_text + "`, which is not one of its choices");
        continue;
      }
    } else if (choices != nullptr) {
      Fail(Location(line) + "row `" + row.key +
           "` has `choices` but its kind is not enum");
      continue;
    }

    const bool is_path = row.kind == Kind::kPathDir || row.kind == Kind::kPathFile;
    if (is_path) {
      std::string rules;
      if (validate == nullptr || validate->is_array) {
        Fail(Location(line) + "path row `" + row.key + "` has no `validate` rule");
        continue;
      }
      rules = validate->text;
      for (const std::string& rule : SplitList(rules, '|')) {
        if (rule != "exists" && rule != "dlc_layout" && rule != "inside_game_root:forbid") {
          Fail(Location(validate->line) + "path row `" + row.key +
               "` has unknown validate rule `" + rule +
               "`; expected exists, dlc_layout or inside_game_root:forbid");
          rules.clear();
          break;
        }
      }
      if (rules.empty()) {
        continue;
      }
      row.validate = rules;
    } else if (validate != nullptr) {
      Fail(Location(line) + "row `" + row.key +
           "` has `validate` but its kind is not a path");
      continue;
    }

    if (row.kind == Kind::kBool && row.default_text != "true" && row.default_text != "false") {
      Fail(Location(line) + "bool row `" + row.key + "` defaults to `" +
           row.default_text + "`; expected true or false");
      continue;
    }
    if (row.kind == Kind::kInt && !IsNumeric(row.default_text, false)) {
      Fail(Location(line) + "int row `" + row.key + "` defaults to `" +
           row.default_text + "`, which is not an integer");
      continue;
    }
    if (row.kind == Kind::kFloat && !IsNumeric(row.default_text, true)) {
      Fail(Location(line) + "float row `" + row.key + "` defaults to `" +
           row.default_text + "`, which is not a number");
      continue;
    }

    // Optional bounds, so a numeric row's widget is a slider over the range the cvar really
    // accepts rather than a free field the launcher has to guess about.
    const Value* min_value = entry.Find("min");
    const Value* max_value = entry.Find("max");
    if (min_value != nullptr || max_value != nullptr) {
      if (row.kind != Kind::kInt && row.kind != Kind::kFloat) {
        Fail(Location(line) + "row `" + row.key +
             "` has `min`/`max` but its kind is not int or float");
        continue;
      }
      if (min_value == nullptr || max_value == nullptr || min_value->is_array ||
          max_value->is_array || !IsNumeric(min_value->text, row.kind == Kind::kFloat) ||
          !IsNumeric(max_value->text, row.kind == Kind::kFloat)) {
        Fail(Location(line) + "row `" + row.key +
             "` needs a numeric `min` and `max`");
        continue;
      }
      const double low = std::strtod(min_value->text.c_str(), nullptr);
      const double high = std::strtod(max_value->text.c_str(), nullptr);
      const double fallback = std::strtod(row.default_text.c_str(), nullptr);
      if (low >= high) {
        Fail(Location(line) + "row `" + row.key + "` has min >= max");
        continue;
      }
      if (fallback < low || fallback > high) {
        Fail(Location(line) + "row `" + row.key + "` defaults to `" + row.default_text +
             "`, which is outside its own min/max");
        continue;
      }
      row.has_range = true;
      row.min_text = min_value->text;
      row.max_text = max_value->text;
    }

    schema->settings.push_back(std::move(row));
  }

  for (const GroupRow& group : schema->groups) {
    const auto key = std::make_pair(static_cast<int>(group.tab), group.name);
    if (group.unavailable && group_rows.count(key) != 0) {
      Fail("group `" + group.name +
           "` is status = \"unavailable\" but has rows; a group that has rows is not "
           "unavailable (D14)");
    }
  }

  return schema;
}

// --- output ---------------------------------------------------------------

std::string CppLiteral(std::string_view text) {
  std::string out = "\"";
  for (const char character : text) {
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
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(character) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\x%02x",
                        static_cast<unsigned int>(static_cast<unsigned char>(character)));
          out += buffer;
        } else {
          out += character;
        }
        break;
    }
  }
  out += "\"";
  return out;
}

std::string_view TabConstant(Tab tab) {
  switch (tab) {
    case Tab::kGeneral:
      return "Tab::kGeneral";
    case Tab::kGraphics:
      return "Tab::kGraphics";
    case Tab::kController:
      return "Tab::kController";
  }
  return "Tab::kGeneral";
}

std::string_view KindConstant(Kind kind) {
  switch (kind) {
    case Kind::kBool:
      return "Kind::kBool";
    case Kind::kInt:
      return "Kind::kInt";
    case Kind::kFloat:
      return "Kind::kFloat";
    case Kind::kEnum:
      return "Kind::kEnum";
    case Kind::kString:
      return "Kind::kString";
    case Kind::kPathDir:
      return "Kind::kPathDir";
    case Kind::kPathFile:
      return "Kind::kPathFile";
  }
  return "Kind::kString";
}

std::string_view AppliesConstant(Applies applies) {
  return applies == Applies::kLive ? "Applies::kLive" : "Applies::kRestart";
}

std::string EmitHeader(const Schema& schema, std::string_view source_name) {
  std::ostringstream out;
  out << "// Generated by launcher/tools/embed_settings.cpp from " << source_name << ".\n"
      << "// Do not edit: edit the .toml and rebuild the rb_blitz_launcher_settings target.\n"
      << "//\n"
      << "// Every row the launcher shows, its tooltip and its command-line destination, so a\n"
      << "// row cannot exist in the UI without a tooltip or be saved without being launchable\n"
      << "// (docs/plans/launcher-plan.md §4.1). The `default_text` field is the compiled\n"
      << "// default as written; a consumer reads it according to `kind`.\n"
      << "\n"
      << "#pragma once\n"
      << "\n"
      << "#include <array>\n"
      << "#include <cstdint>\n"
      << "#include <string_view>\n"
      << "\n"
      << "namespace rb_blitz::launcher::settings {\n"
      << "\n"
      << "inline constexpr int kSchemaVersion = " << schema.schema_version << ";\n"
      << "\n"
      << "enum class Kind : std::uint8_t {\n"
      << "  kBool,\n  kInt,\n  kFloat,\n  kEnum,\n  kString,\n  kPathDir,\n  kPathFile,\n};\n"
      << "\n"
      << "enum class Applies : std::uint8_t {\n  kRestart,\n  kLive,\n};\n"
      << "\n"
      << "enum class Tab : std::uint8_t {\n  kGeneral,\n  kGraphics,\n  kController,\n};\n"
      << "\n"
      << "// A tab's category. `unavailable` groups have no rows and carry the one line the tab\n"
      << "// shows for them instead of an empty or disabled widget (D14).\n"
      << "struct Group {\n"
      << "  std::string_view name;\n"
      << "  Tab tab;\n"
      << "  bool unavailable;\n"
      << "  std::string_view note;\n"
      << "};\n"
      << "\n"
      << "// One row. `choices` and `validate` are comma- and pipe-separated respectively\n"
      << "// (empty when the kind does not use them); `evidence` is the `live:` comment that\n"
      << "// backs `applies == Applies::kLive`; `has_range` carries the optional `min`/`max`\n"
      << "// of a numeric row so its widget can be a slider over the real limits.\n"
      << "struct Setting {\n"
      << "  std::string_view key;\n"
      << "  Tab tab;\n"
      << "  std::string_view group;\n"
      << "  std::string_view label;\n"
      << "  Kind kind;\n"
      << "  Applies applies;\n"
      << "  std::string_view default_text;\n"
      << "  std::string_view tooltip;\n"
      << "  std::string_view choices;\n"
      << "  std::string_view validate;\n"
      << "  std::string_view evidence;\n"
      << "  bool has_range;\n"
      << "  std::string_view min_text;\n"
      << "  std::string_view max_text;\n"
      << "  bool argv_flag;\n"
      << "};\n"
      << "\n"
      << "inline constexpr std::string_view kTabNames[] = {\"general\", \"graphics\", \"controller\"};\n"
      << "inline constexpr std::string_view kKindNames[] = {\"bool\",   \"int\",    \"float\",\n"
      << "                                               \"enum\",   \"string\", \"path_dir\",\n"
      << "                                               \"path_file\"};\n"
      << "inline constexpr std::string_view kAppliesNames[] = {\"restart\", \"live\"};\n"
      << "\n"
      << "constexpr std::string_view TabName(Tab tab) {\n"
      << "  return kTabNames[static_cast<std::size_t>(tab)];\n"
      << "}\n"
      << "\n"
      << "constexpr std::string_view KindName(Kind kind) {\n"
      << "  return kKindNames[static_cast<std::size_t>(kind)];\n"
      << "}\n"
      << "\n"
      << "constexpr std::string_view AppliesName(Applies applies) {\n"
      << "  return kAppliesNames[static_cast<std::size_t>(applies)];\n"
      << "}\n"
      << "\n"
      << "inline constexpr std::array<Group, " << schema.groups.size() << "> kGroups = {{\n";
  for (const GroupRow& group : schema.groups) {
    out << "    {.name = " << CppLiteral(group.name) << ", .tab = " << TabConstant(group.tab)
        << ", .unavailable = " << (group.unavailable ? "true" : "false")
        << ", .note = " << CppLiteral(group.note) << "},\n";
  }
  out << "}};\n"
      << "\n"
      << "inline constexpr std::array<Setting, " << schema.settings.size()
      << "> kSettings = {{\n";
  for (const SettingRow& row : schema.settings) {
    out << "    {.key = " << CppLiteral(row.key) << ", .tab = " << TabConstant(row.tab)
        << ", .group = " << CppLiteral(row.group) << ", .label = " << CppLiteral(row.label)
        << ", .kind = " << KindConstant(row.kind) << ", .applies = " << AppliesConstant(row.applies)
        << ", .default_text = " << CppLiteral(row.default_text)
        << ", .tooltip = " << CppLiteral(row.tooltip) << ", .choices = " << CppLiteral(row.choices)
        << ", .validate = " << CppLiteral(row.validate)
        << ", .evidence = " << CppLiteral(row.evidence)
        << ", .has_range = " << (row.has_range ? "true" : "false")
        << ", .min_text = " << CppLiteral(row.min_text)
        << ", .max_text = " << CppLiteral(row.max_text)
        << ", .argv_flag = " << (row.argv_flag ? "true" : "false") << "},\n";
  }
  out << "}};\n"
      << "\n"
      << "}  // namespace rb_blitz::launcher::settings\n";
  return out.str();
}

struct Arguments {
  fs::path settings;
  fs::path header;
};

void PrintUsage() {
  std::fprintf(stderr,
               "usage: rb_blitz_embed_settings --settings <settings.toml> --header <out.h>\n"
               "\n"
               "Compiles the launcher's settings schema, and refuses a table with a duplicate\n"
               "key, a row without a tooltip, or a `live` row without an evidence comment.\n");
}

bool ParseArguments(int argc, char** argv, Arguments* out) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    auto take = [&](fs::path* target) {
      if (index + 1 >= argc) {
        Fail("missing value for " + std::string(argument));
        return false;
      }
      *target = fs::path(argv[++index]);
      return true;
    };
    if (argument == "--settings") {
      if (!take(&out->settings)) {
        return false;
      }
    } else if (argument == "--header") {
      if (!take(&out->header)) {
        return false;
      }
    } else if (argument == "--help" || argument == "-h") {
      PrintUsage();
      return false;
    } else {
      Fail("unknown argument " + std::string(argument));
      return false;
    }
  }
  if (out->settings.empty() || out->header.empty()) {
    PrintUsage();
    Fail("--settings and --header are both required");
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  if (!ParseArguments(argc, argv, &arguments)) {
    return 1;
  }

  std::string text;
  if (!ReadTextFile(arguments.settings, &text)) {
    return 1;
  }

  Document document;
  if (!ParseDocument(text, &document)) {
    return 1;
  }

  std::unique_ptr<Schema> schema = Build(document);
  if (g_failures != 0) {
    std::fprintf(stderr, "embed_settings: %d problem(s) in %s\n", g_failures,
                 arguments.settings.string().c_str());
    return 1;
  }

  const std::string header = EmitHeader(*schema, arguments.settings.filename().string());
  if (!WriteTextFile(arguments.header, header)) {
    return 1;
  }

  std::size_t counts[3] = {0, 0, 0};
  for (const SettingRow& row : schema->settings) {
    ++counts[static_cast<std::size_t>(row.tab)];
  }
  std::printf("embed_settings: %zu rows in %zu groups (general %zu, graphics %zu, controller %zu)\n",
              schema->settings.size(), schema->groups.size(), counts[0], counts[1], counts[2]);
  std::printf("embed_settings: wrote %s\n", arguments.header.string().c_str());
  return 0;
}
