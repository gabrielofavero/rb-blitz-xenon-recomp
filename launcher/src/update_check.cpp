// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See update_check.h. The parser is a small reader for the flat `key = value` document the
// installer's tools/embed_config.cpp writes, deliberately not a general one: the manifest's
// shape is decided by this build, and a parser that accepted more than that would be
// accepting keys nobody promised.

#include "update_check.h"

#include <cctype>
#include <cstddef>
#include <utility>

namespace rb_blitz::launcher {
namespace {

std::string Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

// Everything before a `#`, unless it is inside a quoted value - the same rule the two
// parsers on the installer's side use, and pins.toml/manifest rely on it.
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

bool IsAllDigits(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return false;
    }
  }
  return true;
}

// A basic string with no escapes, which is what the generator writes: a value that needed
// one would have failed the build rather than reached here.
bool ParseString(std::string_view raw, std::string* out) {
  if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
    return false;
  }
  const std::string_view inner = raw.substr(1, raw.size() - 2);
  if (inner.find('"') != std::string_view::npos) {
    return false;
  }
  out->assign(inner);
  return true;
}

bool ParseBool(std::string_view raw, bool* out) {
  if (raw == "true") {
    *out = true;
    return true;
  }
  if (raw == "false") {
    *out = false;
    return true;
  }
  return false;
}

// The next dotted component of a version, as a number. Anything that is not a number counts
// as 0, which is also what a component the string does not have counts as.
int NextVersionPart(std::string_view text, std::size_t* index) {
  std::size_t begin = *index;
  while (*index < text.size() && text[*index] != '.') {
    ++*index;
  }
  const std::string_view part = text.substr(begin, *index - begin);
  if (*index < text.size()) {
    ++*index;
  }
  // A component too long to be a version's is not a number this launcher compares.
  if (part.size() > 9) {
    return 0;
  }
  int value = 0;
  for (const char character : part) {
    if (character < '0' || character > '9') {
      return 0;
    }
    value = value * 10 + (character - '0');
  }
  return value;
}

}  // namespace

int CompareVersions(std::string_view left, std::string_view right) {
  std::size_t left_index = 0;
  std::size_t right_index = 0;
  while (true) {
    const int left_part = NextVersionPart(left, &left_index);
    const int right_part = NextVersionPart(right, &right_index);
    if (left_part != right_part) {
      return left_part < right_part ? -1 : 1;
    }
    if (left_index >= left.size() && right_index >= right.size()) {
      return 0;
    }
  }
}

bool ParseReleaseManifest(std::string_view text, ReleaseManifest* out, std::string* error) {
  auto fail = [error](std::string message) {
    if (error != nullptr) {
      *error = std::move(message);
    }
    return false;
  };

  // A UTF-8 byte order mark: the generator does not write one, but a server or an editor on the
  // way to here can, and a mark read as part of the first key would make the whole manifest
  // unreadable (the profile's own reader tolerates one for the same reason).
  constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
  if (text.substr(0, kByteOrderMark.size()) == kByteOrderMark) {
    text.remove_prefix(kByteOrderMark.size());
  }

  ReleaseManifest manifest;
  bool has_schema = false;
  bool has_version = false;
  std::size_t line_number = 0;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t end = text.find('\n', begin);
    const std::string_view line =
        text.substr(begin, (end == std::string_view::npos ? text.size() : end) - begin);
    begin = end == std::string_view::npos ? text.size() + 1 : end + 1;
    ++line_number;

    const std::string trimmed = Trim(StripComment(line));
    if (trimmed.empty()) {
      continue;
    }
    const std::size_t equals = trimmed.find('=');
    if (equals == std::string::npos) {
      return fail("line " + std::to_string(line_number) + " of the release manifest is not a key");
    }
    // Both are owned strings rather than views into `trimmed`: Trim returns by value, and a view
    // of its temporary is a view of nothing.
    const std::string key = Trim(std::string_view(trimmed).substr(0, equals));
    const std::string value_text = Trim(std::string_view(trimmed).substr(equals + 1));

    if (key == "schema_version") {
      if (!IsAllDigits(value_text)) {
        return fail("the release manifest's schema_version is not a number");
      }
      manifest.schema_version = std::stoi(std::string(value_text));
      has_schema = true;
    } else if (key == "version") {
      if (!ParseString(value_text, &manifest.version)) {
        return fail("the release manifest's version is not a string");
      }
      has_version = true;
    } else if (key == "requires_game_data") {
      if (!ParseBool(value_text, &manifest.requires_game_data)) {
        return fail("the release manifest's requires_game_data is not true or false");
      }
    } else if (key == "payload_url") {
      if (!ParseString(value_text, &manifest.payload_url)) {
        return fail("the release manifest's payload_url is not a string");
      }
    }
    // Every other key belongs to the updater (payload_sha256, payload_size, ...): it is
    // read by a tool that knows what to do with it, and ignoring it here means a release can
    // add one without this build refusing to see the update.
  }

  if (!has_schema) {
    return fail("the release manifest has no schema_version");
  }
  if (manifest.schema_version != kReleaseManifestSchemaVersion) {
    return fail("the release manifest says schema_version " +
                std::to_string(manifest.schema_version) + ", and this launcher understands " +
                std::to_string(kReleaseManifestSchemaVersion));
  }
  if (!has_version || manifest.version.empty()) {
    return fail("the release manifest has no version");
  }
  *out = std::move(manifest);
  return true;
}

UpdateReport EvaluateUpdate(const UpdateCheck& check, std::string_view running_version,
                            std::string_view declined_version) {
  UpdateReport report;
  // Never started: a build with no release channel, or the run that asked for no check. The
  // launcher then has nothing to say about updates at all, and says that rather than claiming
  // to be up to date.
  if (check.state == UpdateCheck::State::kUnchecked) {
    report.status = UpdateStatus::kDisabled;
    return report;
  }
  if (check.state != UpdateCheck::State::kDone) {
    report.status = UpdateStatus::kChecking;
    return report;
  }
  // No manifest, a manifest this build cannot read, or a release with nothing to install:
  // three different accidents with one answer, because the launcher has nothing useful to
  // say about any of them and D19 asks it to say nothing.
  if (!check.fetched || check.manifest.payload_url.empty()) {
    report.status = UpdateStatus::kNoUpdate;
    return report;
  }
  if (CompareVersions(check.manifest.version, running_version) <= 0) {
    report.status = UpdateStatus::kNoUpdate;
    return report;
  }

  report.status = UpdateStatus::kAvailable;
  report.version = check.manifest.version;
  report.requires_game_data = check.manifest.requires_game_data;
  // Asked once per release. A decline is remembered by version rather than by "the user
  // said no", so a release that arrives later is asked about on its own - which is the one
  // case D19 allows a second prompt - and the same one never is.
  report.ask = declined_version.empty() ||
               CompareVersions(report.version, declined_version) > 0;
  return report;
}

std::string UpdateButtonText(const UpdateReport& report) {
  switch (report.status) {
    case UpdateStatus::kDisabled:
      return "This launcher does not check for newer releases (--no-update-check, or a build "
             "with no release channel).";
    case UpdateStatus::kChecking:
      return "Checking for a newer release...";
    case UpdateStatus::kNoUpdate:
      return "You are running the newest release. The launcher checks each time it starts.";
    case UpdateStatus::kAvailable:
      break;
  }
  return "Install " + report.version + ". The launcher closes so the update can replace it, "
                                   "and opens again when the update is done.";
}

std::string UpdatePromptText(const UpdateReport& report) {
  std::string text = "Rock Band Blitz " + report.version + " is available.";
  if (report.requires_game_data) {
    text += " This update changes what the game reads out of your own game files, so it will "
            "ask for the package or the folder they are in.";
  } else {
    text += " The game files that are already installed are kept, so there is nothing to "
            "answer.";
  }
  return text;
}

}  // namespace rb_blitz::launcher
