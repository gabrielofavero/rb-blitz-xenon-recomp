// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the schema's `validate` rules (launcher/src/path_validate.h, B1).

#include "path_validate.h"

#include <system_error>
#include <vector>

#include "fs/dlc_layout.h"
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
