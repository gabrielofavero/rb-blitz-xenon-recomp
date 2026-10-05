// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the game's own config reader (launcher/src/game_config.h, B4).

#include "game_config.h"

#include <fstream>
#include <sstream>

namespace rb_blitz::launcher {
namespace {

namespace fs = std::filesystem;

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string_view Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsSpace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsSpace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Everything before an unquoted `#`. TOML comments run to the end of the line, and a `#`
// inside a quoted value is part of the value.
std::string_view WithoutComment(std::string_view line) {
  char quote = '\0';
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char c = line[index];
    if (quote != '\0') {
      if (c == '\\' && quote == '"') {
        ++index;  // an escaped character inside a basic string
      } else if (c == quote) {
        quote = '\0';
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '#') {
      return line.substr(0, index);
    }
  }
  return line;
}

// The value as a cvar would see it: a quoted string loses its quotes, everything else is the
// text between `=` and the end of the line.
std::string Unquote(std::string_view value) {
  const char quote = value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
                             value.back() == value.front()
                         ? value.front()
                         : '\0';
  if (quote != '\0') {
    value = value.substr(1, value.size() - 2);
  }
  std::string out;
  out.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char c = value[index];
    // A literal string has no escapes; a basic string has the two the writer can emit.
    if (c == '\\' && quote == '"' && index + 1 < value.size()) {
      const char next = value[index + 1];
      if (next == '\\' || next == '"') {
        out.push_back(next);
        ++index;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

GameConfig ReadGameConfig(const fs::path& game_root) {
  GameConfig config;
  config.path = game_root / kGameConfigFileName;

  std::error_code code;
  if (!fs::exists(config.path, code)) {
    return config;
  }
  config.present = true;

  std::ifstream stream(config.path, std::ios::binary);
  if (!stream) {
    config.unreadable = true;
    config.error = "cannot read " + config.path.string();
    return config;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();

  std::istringstream lines(buffer.str());
  std::string raw;
  int line_number = 0;
  bool in_table = false;
  while (std::getline(lines, raw)) {
    ++line_number;
    const std::string_view line = Trim(WithoutComment(raw));
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[') {
      // A table's keys are not root cvars, and the game's own writer never emits one.
      in_table = true;
      continue;
    }
    if (in_table) {
      continue;
    }

    const std::size_t equals = line.find('=');
    const std::string_view name =
        equals == std::string_view::npos ? line : Trim(line.substr(0, equals));
    const std::string_view value =
        equals == std::string_view::npos ? std::string_view{} : Trim(line.substr(equals + 1));
    if (equals == std::string_view::npos || name.empty() || value.empty()) {
      config.unreadable = true;
      config.error = config.path.string() + ":" + std::to_string(line_number) +
                     " is not `name = value`";
      config.values.clear();
      return config;
    }
    if (value.front() == '[') {
      continue;  // an array: not a value a single cvar can be compared against
    }
    config.values[std::string(name)] = Unquote(value);
  }
  return config;
}

}  // namespace rb_blitz::launcher
