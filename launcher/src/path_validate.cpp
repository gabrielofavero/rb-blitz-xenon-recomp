// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the schema's `validate` rules (launcher/src/path_validate.h, B1).

#include "path_validate.h"

#include <system_error>
#include <vector>

#include "fs/dlc_layout.h"
#include "fs/dlc_library.h"
#include "fs/path_policy.h"

namespace rb_blitz::launcher {
namespace {

namespace fs = std::filesystem;

// D4's refusal, in the user's words: the runtime redirects writes inside the game root to
// the platform user folder, so a row that offered one would offer a setting that silently
// does something else.
std::string InsideGameRootReason(const fs::path& value, const fs::path& game_root) {
  std::string reason = "Refused: ";
  reason += value.string();
  reason += " is inside the game data folder (";
  reason += game_root.string();
  reason += "). The runtime redirects its writable roots to the platform user folder, so "
            "the setting would silently do something else.";
  return reason;
}

// The DLC root the runtime accepts is one of two shapes, and src/hooks/dlc.cpp decides between
// them the same way: a folder named like the <title_id>/<content_type>/<package> layout is
// mounted as that structure, and anything else is read as a flat library
// (src/fs/dlc_library.h) - thousands of loose CON/LIVE containers in no particular
// arrangement, which is what a dumped song folder is (docs/dlc.md §2.1). A folder of loose
// packages is therefore accepted here rather than refused: refusing it would turn away exactly
// the layout the runtime is built to read. The library walk itself is not repeated - a cold one
// is ~22 s on a large exFAT library (docs/dlc.md §4.1), far too long to run while the panel
// draws, and nothing the walk finds can refuse a folder the runtime would take.
PathVerdict CheckDlcLayout(const fs::path& value) {
  PathVerdict verdict;
  std::error_code ec;
  if (!fs::is_directory(value, ec) || ec) {
    // Not a refusal: an absent DLC folder is the normal case (dlc_layout.h says so), and the
    // row's tooltip already explains what the folder has to hold when it exists.
    verdict.note = "Not a folder yet; the game starts with whatever DLC is in the content "
                   "root until one is.";
    return verdict;
  }

  if (!rb_blitz::fs::HasStructuredLayout(value)) {
    // An empty folder reads the same either way, so it keeps the row's own wording; a folder
    // with anything in it is the flat library the runtime reads where it lies.
    std::error_code empty_ec;
    if (fs::is_empty(value, empty_ec) && !empty_ec) {
      verdict.note = "No DLC packages found under it yet.";
      return verdict;
    }
    verdict.note = "Not the <title_id>/<content_type>/<package> layout, so the game reads it as "
                   "a flat song library: the loose CON/LIVE packages under it are mounted where "
                   "they lie.";
    return verdict;
  }

  const rb_blitz::fs::DlcScanResult scan = rb_blitz::fs::ScanDlcRoot(value);
  if (!scan.rejected.empty()) {
    // The first refusal is the one worth showing: the scanner reports every offending entry,
    // and a row that cannot hold content is the mistake worth naming.
    verdict.ok = false;
    verdict.reason = "Refused: " + scan.rejected.front().entry + " " +
                     scan.rejected.front().reason +
                     ". Expected <title_id>\\<content_type>\\<package>, both id levels as 8 "
                     "upper-case hex digits.";
    return verdict;
  }

  if (scan.packages.empty()) {
    verdict.note = "No DLC packages found under it yet.";
  } else {
    verdict.note = std::to_string(scan.packages.size()) +
                   (scan.packages.size() == 1 ? " package found." : " packages found.");
  }
  return verdict;
}

}  // namespace

PathVerdict ValidatePathValue(std::string_view rules, const fs::path& value,
                              const fs::path& game_root) {
  PathVerdict verdict;
  if (value.empty() || rules.empty()) {
    return verdict;
  }

  std::size_t start = 0;
  while (start <= rules.size()) {
    const std::size_t end = rules.find('|', start);
    const std::string_view rule =
        rules.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    start = end == std::string_view::npos ? rules.size() + 1 : end + 1;
    if (rule.empty()) {
      continue;
    }

    if (rule == "exists") {
      std::error_code ec;
      if (!fs::exists(value, ec) || ec) {
        verdict.ok = false;
        verdict.reason = "Refused: " + value.string() + " does not exist.";
        return verdict;
      }
    } else if (rule == "dlc_layout") {
      PathVerdict dlc = CheckDlcLayout(value);
      if (!dlc.ok) {
        return dlc;
      }
      if (!dlc.note.empty()) {
        verdict.note = std::move(dlc.note);
      }
    } else if (rule == "inside_game_root:forbid") {
      if (rb_blitz::fs::IsSameOrInside(value, game_root)) {
        verdict.ok = false;
        verdict.reason = InsideGameRootReason(value, game_root);
        return verdict;
      }
    } else {
      // A rule the schema names and this build does not know is reported, not waved
      // through: the generator is what keeps the two in step.
      verdict.ok = false;
      verdict.reason = "Refused: this build does not know the rule '" + std::string(rule) + "'.";
      return verdict;
    }
  }
  return verdict;
}

}  // namespace rb_blitz::launcher
