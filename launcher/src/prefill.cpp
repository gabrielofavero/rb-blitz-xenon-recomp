// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the first run's prefill (launcher/src/prefill.h, D4).

#include "prefill.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

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

// Everything before an unquoted `#`; a `#` inside a quoted value is part of the value.
std::string_view WithoutComment(std::string_view line) {
  char quote = '\0';
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char c = line[index];
    if (quote != '\0') {
      if (c == '\\' && quote == '"') {
        ++index;
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

// A TOML string loses its quotes and its escapes; everything else is the text as written.
// The installer's TomlString writer escapes `\`, `"`, and the control characters, so those
// are the ones decoded here.
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
    if (c == '\\' && quote == '"' && index + 1 < value.size()) {
      const char next = value[index + 1];
      switch (next) {
        case '\\':
        case '"':
          out.push_back(next);
          ++index;
          continue;
        case 'n':
          out.push_back('\n');
          ++index;
          continue;
        case 'r':
          out.push_back('\r');
          ++index;
          continue;
        case 't':
          out.push_back('\t');
          ++index;
          continue;
        default:
          break;
      }
    }
    out.push_back(c);
  }
  return out;
}

bool Exists(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::error_code code;
  return fs::exists(fs::path{path}, code) && !code;
}

}  // namespace

std::string_view PrefillSourceName(PrefillSource source) {
  switch (source) {
    case PrefillSource::kNone:
      return "nothing";
    case PrefillSource::kManifest:
      return kInstallManifestFileName;
    case PrefillSource::kFileSystem:
      return "the file system";
  }
  return "nothing";
}

InstallManifest ReadInstallManifest(const fs::path& dir) {
  InstallManifest manifest;
  manifest.path = dir / kInstallManifestFileName;

  std::error_code code;
  if (!fs::exists(manifest.path, code)) {
    return manifest;  // a hand install, or an installer older than the manifest
  }
  manifest.present = true;

  std::ifstream stream(manifest.path, std::ios::binary);
  if (!stream) {
    manifest.error = "cannot read " + manifest.path.string();
    return manifest;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  std::string document = buffer.str();
  // A BOM is not part of the manifest. The installer writes UTF-8 without one, but a user who
  // opens the file in an editor may save it back with one, and that must not read as "no
  // schema_version".
  if (document.size() >= 3 && static_cast<unsigned char>(document[0]) == 0xEF &&
      static_cast<unsigned char>(document[1]) == 0xBB &&
      static_cast<unsigned char>(document[2]) == 0xBF) {
    document.erase(0, 3);
  }

  const auto fail = [&manifest](int line_number, std::string_view reason) {
    manifest.error =
        manifest.path.string() + ":" + std::to_string(line_number) + " " + std::string(reason);
    manifest.parsed = false;
  };

  bool saw_schema = false;
  bool saw_install = false;
  std::string table;
  std::istringstream lines(document);
  std::string raw;
  int line_number = 0;
  while (std::getline(lines, raw)) {
    ++line_number;
    const std::string_view line = Trim(WithoutComment(raw));
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[') {
      if (line.size() >= 2 && line.back() == ']' && line[1] != '[') {
        table = std::string(Trim(line.substr(1, line.size() - 2)));
        if (table == "install") {
          saw_install = true;
        }
      } else {
        table.clear();  // an array-of-tables: its keys are not ours
      }
      continue;
    }

    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) {
      fail(line_number, "is not `name = value`");
      return manifest;
    }
    const std::string_view name = Trim(line.substr(0, equals));
    const std::string_view value = Trim(line.substr(equals + 1));
    if (name.empty() || value.empty()) {
      fail(line_number, "is not `name = value`");
      return manifest;
    }
    if (value.front() == '[') {
      continue;  // an array value: `executables`, for instance
    }
    const std::string value_text = Unquote(value);

    if (table.empty()) {
      if (name == "schema_version") {
        saw_schema = value_text == "1";
      }
    } else if (table == "install") {
      if (name == "directory") {
        manifest.install_dir = value_text;
      } else if (name == "game_directory") {
        manifest.game_dir = value_text;
      } else if (name == "payload_commit") {
        manifest.payload_commit = value_text;
      }
    } else if (table == "game_data") {
      if (name == "directory") {
        if (manifest.game_dir.empty()) {
          manifest.game_dir = value_text;
        }
      } else if (name == "source") {
        manifest.game_source = value_text;
      } else if (name == "ultimate_installed") {
        manifest.ultimate_installed = value_text == "true";
      }
    }
  }

  if (!saw_schema) {
    manifest.error = manifest.path.string() + " is not an install manifest (no schema_version = 1)";
    return manifest;
  }
  if (!saw_install) {
    manifest.error = manifest.path.string() + " is not an install manifest (no [install] table)";
    return manifest;
  }
  manifest.parsed = true;
  return manifest;
}

PrefillPlan PlanPrefill(const fs::path& launcher_dir, const PrefillInputs& inputs) {
  PrefillPlan plan;
  plan.manifest = ReadInstallManifest(launcher_dir);

  // Only a first run is prefilled. A profile that already exists is the user's, whether it was
  // written here or by a launcher installed somewhere else - that is the second-install case,
  // and overwriting it would be a reset the user did not ask for.
  if (inputs.has_profile) {
    plan.note = "a profile already exists, so nothing is prefilled";
    return plan;
  }

  // The file system's answer: the no-manifest case, and the fallback when the manifest's paths
  // are gone. B1's detection is the same scan either way.
  const auto use_file_system = [&plan, &inputs](std::string note) {
    plan.source = PrefillSource::kFileSystem;
    plan.applies = inputs.game_root_found;
    plan.game_dir = inputs.detected_game_dir;
    plan.target = inputs.ultimate_available ? LaunchTarget::kUltimate : LaunchTarget::kCommon;
    plan.note = std::move(note);
  };

  if (!plan.manifest.present) {
    use_file_system(inputs.game_root_found
                        ? "no install-manifest.toml next to the launcher: using the game folder "
                          "the file system shows"
                        : "no install-manifest.toml and no game folder was found, so there is "
                          "nothing to prefill");
    return plan;
  }
  if (!plan.manifest.parsed) {
    use_file_system("the install manifest could not be read (" + plan.manifest.error +
                    "): using the game folder the file system shows");
    return plan;
  }

  // A manifest that outlived its install is the one case D4 asks to say out loud rather than
  // silently trust.
  if (!Exists(plan.manifest.game_dir)) {
    plan.missing.push_back(plan.manifest.game_dir);
  }
  if (!plan.manifest.install_dir.empty() && !Exists(plan.manifest.install_dir)) {
    plan.missing.push_back(plan.manifest.install_dir);
  }
  if (!plan.missing.empty()) {
    use_file_system("the install manifest names a folder that is no longer there: re-scanned "
                    "the file system");
    plan.rescanned = true;
    return plan;
  }

  plan.applies = true;
  plan.source = PrefillSource::kManifest;
  plan.game_dir = plan.manifest.game_dir.empty() ? inputs.detected_game_dir : plan.manifest.game_dir;
  plan.target = plan.manifest.ultimate_installed ? LaunchTarget::kUltimate : LaunchTarget::kCommon;
  plan.note = "prefilled from install-manifest.toml";
  return plan;
}

bool ApplyPrefill(Profile& profile, const PrefillPlan& plan) {
  if (!plan.applies) {
    return false;
  }
  bool changed = false;
  if (!plan.game_dir.empty() && profile.game_dir != plan.game_dir) {
    profile.game_dir = plan.game_dir;
    changed = true;
  }
  if (profile.target != plan.target) {
    profile.target = plan.target;
    changed = true;
  }
  return changed;
}

std::string DescribePrefill(const PrefillPlan& plan) {
  std::string out;
  out += "manifest       : ";
  out += plan.manifest.path.string();
  if (!plan.manifest.present) {
    out += " (not there)";
  } else if (!plan.manifest.parsed) {
    out += " (unreadable: " + plan.manifest.error + ")";
  } else {
    out += " (read)";
  }
  out += "\n";

  if (plan.manifest.parsed) {
    const auto line = [&out](const char* label, const std::string& value) {
      out += label;
      out += value.empty() ? "(unnamed)" : value;
      out += "\n";
    };
    line("install dir    : ", plan.manifest.install_dir);
    line("game dir       : ", plan.manifest.game_dir);
    line("payload commit : ", plan.manifest.payload_commit);
    line("game source    : ", plan.manifest.game_source);
    out += "ultimate       : ";
    out += plan.manifest.ultimate_installed ? "installed\n" : "not installed\n";
  }

  out += "first run      : ";
  out += plan.applies ? "yes\n" : "no\n";
  out += "source         : ";
  out += std::string(PrefillSourceName(plan.source));
  out += "\n";
  out += "game folder    : ";
  out += plan.game_dir.empty() ? "(none)\n" : plan.game_dir + "\n";
  out += "target         : ";
  out += std::string(LaunchTargetName(plan.target));
  out += "\n";
  if (!plan.missing.empty()) {
    out += "missing        : ";
    for (std::size_t index = 0; index < plan.missing.size(); ++index) {
      if (index != 0) {
        out += "; ";
      }
      out += plan.missing[index];
    }
    out += "\n";
  }
  if (plan.rescanned) {
    out += "re-scanned     : yes\n";
  }
  if (!plan.note.empty()) {
    out += "note           : " + plan.note + "\n";
  }
  return out;
}

}  // namespace rb_blitz::launcher
