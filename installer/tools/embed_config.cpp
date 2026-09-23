// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Compiles the installer's configuration into the two files the build consumes:
//
//   generated/embedded_config.h  the pins and both fingerprint files as string
//                                literals, for rb_blitz_setup_helper.exe
//   generated/pins.iss           the same pins as Inno Setup preprocessor
//                                defines, for setup.iss
//
// Both come from installer/config/pins.toml, so the wizard and the helper cannot
// disagree about what they are installing. Run with --help for the argument list.
//
// This file is deliberately standalone: it is the first thing the installer build
// compiles, so it must not depend on anything in src/.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void Fail(std::string_view message) {
  std::fprintf(stderr, "embed_config: error: %.*s\n", static_cast<int>(message.size()),
               message.data());
  ++g_failures;
}

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

std::string ReplaceAll(std::string text, std::string_view from, std::string_view to) {
  if (from.empty()) {
    return text;
  }
  std::string result;
  result.reserve(text.size());
  std::size_t position = 0;
  while (true) {
    const std::size_t found = text.find(from, position);
    if (found == std::string::npos) {
      result.append(text, position, std::string::npos);
      return result;
    }
    result.append(text, position, found - position);
    result.append(to);
    position = found + from.size();
  }
}

std::string ReadTextFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    Fail("cannot open " + path.string());
    return {};
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
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

// A raw string literal cannot carry a `)RBG_CFG"` sequence, and CRLF inside one
// is implementation-defined territory, so line endings are normalised here and
// the delimiter is checked.
constexpr std::string_view kRawDelimiter = "RBG_CFG";

std::string NormaliseForEmbedding(std::string text) {
  text = ReplaceAll(std::move(text), "\r\n", "\n");
  text = ReplaceAll(std::move(text), "\r", "\n");
  return text;
}

bool ValidateEmbeddable(std::string_view name, std::string_view text) {
  if (text.find(")" + std::string(kRawDelimiter) + "\"") != std::string_view::npos) {
    Fail(std::string(name) + " contains the raw string delimiter )" +
         std::string(kRawDelimiter) + "\"");
    return false;
  }
  if (const std::size_t null = text.find('\0'); null != std::string_view::npos) {
    Fail(std::string(name) + " contains a NUL byte");
    return false;
  }
  return true;
}

std::string EmitRawLiteral(std::string_view text) {
  std::string out = "R\"";
  out.append(kRawDelimiter);
  out.append("(");
  out.append(text);
  out.append(")");
  out.append(kRawDelimiter);
  out.append("\"");
  return out;
}

// --- a read-only view of the pins -----------------------------------------

// Just enough TOML to read `[table]` + `key = value`, which is all pins.toml is.
// Keeping this separate from src/config.cpp is the price of a dependency-free
// first build step; pins.toml stays inside the subset both understand.
struct PinsValue {
  std::string text;      // unquoted
  bool was_quoted = false;
  std::size_t line = 0;  // index into the split lines, for the --*-override path
  std::string key;
};

struct PinsTable {
  std::string name;
  std::vector<PinsValue> values;

  const PinsValue* Find(std::string_view wanted) const {
    const auto found = std::find_if(values.begin(), values.end(), [wanted](const PinsValue& value) {
      return value.key == wanted;
    });
    return found == values.end() ? nullptr : &*found;
  }

  std::string String(std::string_view key, std::string_view fallback = {}) const {
    const PinsValue* value = Find(key);
    return value != nullptr ? value->text : std::string(fallback);
  }
};

struct PinsFile {
  // The file as written, so an override can rewrite one value without flattening
  // the comments that explain the pins to whoever reads the generated header.
  std::vector<std::string> raw_lines;
  std::map<std::string, PinsTable, std::less<>> tables;

  const PinsTable* Find(std::string_view name) const {
    const auto found = tables.find(name);
    return found == tables.end() ? nullptr : &found->second;
  }
};

// Splits a line at the first `#` outside a string, which is what TOML does. An
// escaped quote inside a string is not handled: pins.toml has no such value, and
// the parsers on both sides share the limitation.
void SplitComment(std::string_view line, std::string* code, std::string* comment) {
  bool in_string = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (character == '"') {
      in_string = !in_string;
    } else if (character == '#' && !in_string) {
      *code = std::string(line.substr(0, index));
      *comment = std::string(line.substr(index));
      return;
    }
  }
  *code = std::string(line);
  comment->clear();
}

std::string StripComment(std::string_view line) {
  std::string code;
  std::string comment;
  SplitComment(line, &code, &comment);
  return code;
}

bool ParsePins(const std::string& text, PinsFile* out) {
  out->raw_lines.clear();
  out->tables.clear();
  out->tables[""] = PinsTable{};
  std::string current;

  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    out->raw_lines.emplace_back(text, start, end - start);
    const std::size_t line_index = out->raw_lines.size() - 1;
    const std::string line = Trim(StripComment(out->raw_lines.back()));

    if (!line.empty()) {
      if (line.front() == '[') {
        const std::size_t close = line.find(']');
        if (close == std::string::npos) {
          Fail("pins.toml line " + std::to_string(line_index + 1) + ": unterminated table header");
          return false;
        }
        current = Trim(std::string_view(line).substr(1, close - 1));
        out->tables[current] = PinsTable{current, {}};
      } else if (const std::size_t equals = line.find('='); equals != std::string::npos) {
        PinsValue value;
        value.key = Trim(std::string_view(line).substr(0, equals));
        value.line = line_index;
        std::string raw = Trim(std::string_view(line).substr(equals + 1));
        // Trailing comments were stripped above, so a quoted value is exactly
        // "..." here.
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
          value.text = raw.substr(1, raw.size() - 2);
          value.was_quoted = true;
        } else {
          value.text = std::move(raw);
        }
        out->tables[current].values.push_back(std::move(value));
      }
    }

    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }
  return true;
}

// Rewrites `key = "value"` in place, so an override reaches both the embedded TOML
// and the ISPP defines without a second source of truth.
bool ApplyOverride(PinsFile* pins, std::string_view table_name, std::string_view key,
                   const std::string& value) {
  PinsTable* table = nullptr;
  if (const auto found = pins->tables.find(table_name); found != pins->tables.end()) {
    table = &found->second;
  }
  const PinsValue* existing = table != nullptr ? table->Find(key) : nullptr;
  if (existing == nullptr) {
    Fail("pins.toml [" + std::string(table_name) + "] has no '" + std::string(key) +
         "' to override");
    return false;
  }

  std::string code;
  std::string comment;
  SplitComment(pins->raw_lines[existing->line], &code, &comment);
  const std::size_t equals = code.find('=');
  if (equals == std::string::npos) {
    Fail("pins.toml line " + std::to_string(existing->line + 1) + ": no '=' to override");
    return false;
  }
  pins->raw_lines[existing->line] = code.substr(0, equals + 1) + " " +
                                    (existing->was_quoted ? "\"" + value + "\"" : value) + comment;

  // The defines are emitted from the parsed view, so it has to move with the text.
  for (PinsValue& candidate : table->values) {
    if (candidate.key == key) {
      candidate.text = value;
    }
  }
  return true;
}

// --- output ---------------------------------------------------------------

// ISPP string literals are double quoted and escape an embedded quote by
// doubling it. Values are single line by construction; that is checked rather
// than assumed, because a stray newline in pins.toml would silently truncate the
// define and Inno Setup would fail somewhere unrelated.
bool EmitIsppString(std::ostream& out, std::string_view name, const std::string& value) {
  if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
    Fail("ISPP define " + std::string(name) + " spans more than one line");
    return false;
  }
  for (const char character : value) {
    if (static_cast<unsigned char>(character) > 0x7F) {
      // setup.iss is read as ANSI unless it carries a UTF-8 BOM; rather than
      // depend on how the include is decoded, keep the defines ASCII and give a
      // clear message if someone puts an accent in a name.
      Fail("ISPP define " + std::string(name) + " is not ASCII: '" + value + "'");
      return false;
    }
  }
  out << "#define " << name << " \"" << ReplaceAll(value, "\"", "\"\"") << "\"\n";
  return true;
}

void EmitIsppNumber(std::ostream& out, std::string_view name, const std::string& value) {
  const std::string trimmed = Trim(value);
  const bool numeric =
      !trimmed.empty() &&
      std::all_of(trimmed.begin(), trimmed.end(), [](char c) { return c >= '0' && c <= '9'; });
  out << "#define " << name << " " << (numeric ? trimmed : "0") << "\n";
}

struct Arguments {
  fs::path pins;
  fs::path game_fingerprints;
  fs::path ultimate_fingerprints;
  fs::path header;
  fs::path ispp;
  std::map<std::string, std::string, std::less<>> overrides;  // "table.key" -> value
};

// The commit of the recompiled build is recorded in every install manifest, so a
// ref cannot be compiled in: build.ps1 resolves "latest" and refs with git, and a
// build that does not go through the script has no step that could.
bool IsCommitId(std::string_view text) {
  return text.size() == 40 && std::all_of(text.begin(), text.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
         });
}

std::string Usage() {
  return "usage: rb_blitz_embed_config --pins <f> --game-fingerprints <f> "
         "--ultimate-fingerprints <f> --header <f> --ispp <f>\n"
         "                             [--payload-version <v>] [--payload-url <u>]\n"
         "                             [--payload-sha256 <h>] [--payload-size <n>]\n"
         "                             [--payload-commit <id>]\n";
}

bool ParseArguments(int argc, char** argv, Arguments* out) {
  const auto next = [&](int* index) -> std::optional<std::string> {
    if (*index + 1 >= argc) {
      return std::nullopt;
    }
    return std::string(argv[++*index]);
  };

  for (int index = 1; index < argc; ++index) {
    const std::string_view name = argv[index];
    if (name == "--help" || name == "-h") {
      std::fputs(Usage().c_str(), stdout);
      return false;
    }
    const std::optional<std::string> value = next(&index);
    if (!value.has_value()) {
      Fail(std::string(name) + " needs a value");
      return false;
    }
    if (name == "--pins") {
      out->pins = *value;
    } else if (name == "--game-fingerprints") {
      out->game_fingerprints = *value;
    } else if (name == "--ultimate-fingerprints") {
      out->ultimate_fingerprints = *value;
    } else if (name == "--header") {
      out->header = *value;
    } else if (name == "--ispp") {
      out->ispp = *value;
    } else if (name == "--payload-version") {
      out->overrides["payload.version"] = *value;
    } else if (name == "--payload-url") {
      out->overrides["payload.url"] = *value;
    } else if (name == "--payload-sha256") {
      out->overrides["payload.sha256"] = *value;
    } else if (name == "--payload-size") {
      out->overrides["payload.size"] = *value;
    } else if (name == "--payload-commit") {
      out->overrides["payload.commit"] = *value;
    } else {
      Fail(std::string("unknown argument ") + std::string(name));
      return false;
    }
  }

  for (const auto& [key, flag] : std::vector<std::pair<fs::path Arguments::*, std::string>>{
           {&Arguments::pins, "--pins"},
           {&Arguments::game_fingerprints, "--game-fingerprints"},
           {&Arguments::ultimate_fingerprints, "--ultimate-fingerprints"},
           {&Arguments::header, "--header"},
           {&Arguments::ispp, "--ispp"}}) {
    if ((out->*key).empty()) {
      Fail(flag + " is required");
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  if (!ParseArguments(argc, argv, &arguments)) {
    if (g_failures != 0) {
      std::fputs(Usage().c_str(), stderr);
      return 1;
    }
    return 2;  // --help
  }

  std::string pins_text = ReadTextFile(arguments.pins);
  if (g_failures != 0) {
    return 1;
  }
  const std::string game_fingerprints =
      NormaliseForEmbedding(ReadTextFile(arguments.game_fingerprints));
  const std::string ultimate_fingerprints =
      NormaliseForEmbedding(ReadTextFile(arguments.ultimate_fingerprints));
  if (g_failures != 0) {
    return 1;
  }

  PinsFile pins;
  if (!ParsePins(pins_text, &pins)) {
    return 1;
  }
  for (const auto& [dotted, value] : arguments.overrides) {
    const std::size_t dot = dotted.find('.');
    if (!ApplyOverride(&pins, dotted.substr(0, dot), dotted.substr(dot + 1), value)) {
      return 1;
    }
  }
  // Re-joined from the (possibly overridden) source lines, so the embedded TOML
  // and the defines describe the same thing.
  pins_text = NormaliseForEmbedding([&pins] {
    std::string text;
    for (const std::string& line : pins.raw_lines) {
      text += line;
      text += "\n";
    }
    return text;
  }());

  if (!ValidateEmbeddable("pins.toml", pins_text) ||
      !ValidateEmbeddable("game_fingerprints.toml", game_fingerprints) ||
      !ValidateEmbeddable("ultimate_fingerprints.toml", ultimate_fingerprints)) {
    return 1;
  }

  std::string header;
  header += "// Generated by tools/embed_config.cpp from:\n";
  header += "//   installer/config/pins.toml\n";
  header += "//   installer/config/ultimate_fingerprints.toml\n";
  header += "//   config/game_fingerprints.toml\n";
  header += "// Do not edit; edit the sources and rebuild.\n";
  header += "#pragma once\n\n";
  header += "namespace rb_blitz::installer::embedded {\n\n";
  header += "inline constexpr char kPinsToml[] = " + EmitRawLiteral(pins_text) + ";\n\n";
  header += "inline constexpr char kGameFingerprintsToml[] = " + EmitRawLiteral(game_fingerprints) +
            ";\n\n";
  header += "inline constexpr char kUltimateFingerprintsToml[] = " +
            EmitRawLiteral(ultimate_fingerprints) + ";\n\n";
  header += "}  // namespace rb_blitz::installer::embedded\n";
  if (!WriteTextFile(arguments.header, header)) {
    return 1;
  }

  const PinsTable* installer = pins.Find("installer");
  const PinsTable* payload = pins.Find("payload");
  const PinsTable* ultimate = pins.Find("ultimate");
  if (installer == nullptr || payload == nullptr || ultimate == nullptr) {
    Fail("pins.toml is missing an [installer], [payload] or [ultimate] table");
    return 1;
  }
  const std::string pinned_commit = payload->String("commit");
  if (!pinned_commit.empty() && !IsCommitId(pinned_commit)) {
    Fail("[payload] commit '" + pinned_commit +
         "' is not a 40-character commit id: installer\\build.ps1 resolves \"latest\" and refs "
         "with git before the pins are compiled in (or write a commit id in pins.toml)");
    return 1;
  }

  std::ostringstream ispp;
  ispp << "// Generated by tools/embed_config.cpp from installer/config/pins.toml.\n";
  ispp << "// Do not edit; edit pins.toml and rebuild.\n";
  ispp << "\n";
  ispp << "#ifndef RBBLITZ_PINS_ISS\n";
  ispp << "#define RBBLITZ_PINS_ISS\n";
  ispp << "\n";
  const std::pair<std::string_view, std::string_view> installer_strings[] = {
      {"AppName", "name"},
      {"AppShortName", "short_name"},
      {"AppVersion", "version"},
      {"AppPublisher", "publisher"},
      {"AppHomepageUrl", "homepage_url"},
      {"AppSupportUrl", "support_url"},
      {"AppDefaultDirName", "default_dir_name"},
  };
  for (const auto& [define, key] : installer_strings) {
    EmitIsppString(ispp, define, installer->String(key));
  }
  EmitIsppString(ispp, "AppMinWindowsBuild", installer->String("min_windows_build"));
  ispp << "\n";
  ispp << "// The recompiled build. PayloadUrl is empty when this build ships the payload\n";
  ispp << "// inside the setup executable instead of downloading it, and PayloadCommit is the\n";
  ispp << "// commit it was built from - empty when the build did not record one.\n";
  EmitIsppString(ispp, "PayloadVersion", payload->String("version"));
  EmitIsppString(ispp, "PayloadCommit", pinned_commit);
  EmitIsppString(ispp, "PayloadUrl", payload->String("url"));
  EmitIsppString(ispp, "PayloadSha256", payload->String("sha256"));
  EmitIsppNumber(ispp, "PayloadSize", payload->String("size"));
  EmitIsppNumber(ispp, "PayloadHasDownload", payload->String("url").empty() ? "0" : "1");
  ispp << "\n";
  ispp << "// Rock Band Blitz Ultimate, fetched on demand from its own release. Never\n";
  ispp << "// bundled: see installer/README.md, section \"The Ultimate mod\".\n";
  EmitIsppString(ispp, "UltimateVersion", ultimate->String("version"));
  EmitIsppString(ispp, "UltimateUrl", ultimate->String("url"));
  EmitIsppString(ispp, "UltimateSha256", ultimate->String("sha256"));
  EmitIsppNumber(ispp, "UltimateSize", ultimate->String("size"));
  EmitIsppString(ispp, "UltimateRepositoryUrl", ultimate->String("repository_url"));
  EmitIsppString(ispp, "UltimateReleaseUrl", ultimate->String("release_url"));
  EmitIsppString(ispp, "UltimateDestinationDir", ultimate->String("destination_dir"));
  ispp << "\n";
  ispp << "#endif\n";
  if (g_failures == 0 && !WriteTextFile(arguments.ispp, ispp.str())) {
    return 1;
  }
  if (g_failures != 0) {
    return 1;
  }

  std::fprintf(stdout,
               "embed_config: wrote %s (%zu bytes) and %s\n", arguments.header.string().c_str(),
               header.size(), arguments.ispp.string().c_str());
  return 0;
}
