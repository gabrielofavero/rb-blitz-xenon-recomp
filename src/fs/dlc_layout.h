// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The shape of the DLC directory that src/hooks/dlc.cpp registers as an extra,
// read-only content root. The guest never opens this directory: it enumerates
// content through the emulated Xbox content APIs
// (XamContentAggregateCreateEnumerator) and then opens what it found, and the SDK
// resolves both against the content root src/hooks/dlc.cpp hands it. That fixes the
// layout to
//
//   <dlc_root>/<title_id>/<content_type>/<package>
//
// with the names spelled the way the SDK spells them when it builds the search path
// (docs/dlc.md). It also holds the decision src/hooks/dlc.cpp makes about that root -
// register it, or leave the SDK's own content root as the only source - because that
// decision is a function of the directory and the scan, not of the runtime. Kept free
// of any SDK dependency for the same reason src/fs/path_policy.h is - it is host path
// arithmetic and directory reading, and tests/dlc_layout_tests.cpp has to cover it
// without booting the game (docs/rb3-references.md §7.3).

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::fs {

// A directory name the content root can hold. The SDK formats both the title id and
// the content type with fmt("{:08X}") and then looks the directory up by that exact
// name, so on a case-sensitive host a lower-case spelling is not found: only
// 8-digit upper-case hex qualifies. The content root's own first level is a 16-digit
// xuid and is deliberately not accepted here - dropping a whole content-root subtree
// into the DLC folder is a mistake worth reporting, not silently enumerating nothing.
inline bool IsContentRootName(const std::string_view name) {
  if (name.size() != 8) {
    return false;
  }
  for (const char c : name) {
    const bool digit = c >= '0' && c <= '9';
    const bool upper = c >= 'A' && c <= 'F';
    if (!digit && !upper) {
      return false;
    }
  }
  return true;
}

// One package found under the DLC root: the STFS container itself, or an extracted
// directory. `file_name` is what the guest receives from the enumerator and hands
// back to open the item, so it has to stay exactly the directory entry's name.
struct DlcPackage {
  std::filesystem::path host_path;
  std::string title_id;
  std::string content_type;
  std::string file_name;
};

struct DlcScanResult {
  std::vector<DlcPackage> packages;

  // Entries that cannot be content, with the reason, so a misplaced folder or a
  // file that is not a package is reported instead of quietly doing nothing.
  struct Rejected {
    std::string entry;  // relative to the DLC root, '/' separators
    std::string reason;
  };
  std::vector<Rejected> rejected;
};

// The magic a content package starts with ("CON ", "LIVE" or "PIRS"). Only a cheap
// read of the first four bytes: the SDK validates the rest of the STFS header and is
// the authority on what it will mount. A file without the magic is not content and
// the enumerator skips it, so it is reported here instead.
inline bool LooksLikeContentPackage(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  char magic[4] = {};
  file.read(magic, sizeof(magic));
  if (file.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
    return false;
  }
  const std::string_view text(magic, sizeof(magic));
  return text == "CON " || text == "LIVE" || text == "PIRS";
}

// Resolves the configured DLC root: an empty value selects <game_data_root>/dlc, a
// relative value resolves against game_data_root. Purely lexical, so the result is
// the configuration's own arithmetic and not a function of the working directory.
inline std::filesystem::path ResolveDlcRoot(const std::string_view configured,
                                            const std::filesystem::path& game_data_root) {
  if (configured.empty()) {
    return (game_data_root / "dlc").lexically_normal();
  }
  std::filesystem::path root{std::string(configured)};
  if (root.is_relative()) {
    root = game_data_root / root;
  }
  return root.lexically_normal();
}

// True when the root holds the layout above - at least one child directory named like
// a content-root level. A folder of loose containers has none, which is what makes it
// a flat library instead (src/fs/dlc_library.h). Read from the first level only: a
// library may nest any way it likes below its own root, and it is the presence of the
// title level, not of any package, that distinguishes the two.
inline bool HasStructuredLayout(const std::filesystem::path& dlc_root) {
  std::error_code ec;
  std::filesystem::directory_iterator it(dlc_root, ec);
  if (ec) {
    return false;
  }
  for (const auto& entry : it) {
    if (entry.is_directory(ec) && !ec && IsContentRootName(entry.path().filename().string())) {
      return true;
    }
  }
  return false;
}

// What src/hooks/dlc.cpp does with what it found. Every value but the three
// registrations is a refusal to add a source, which leaves the SDK's own content root
// as the only one - the faithful behaviour - and the hook says why in the log.
enum class DlcRegistration {
  kNoDirectory,       // nothing at the configured root, and no library configured
  kNoSource,          // sources were configured but none holds a mountable package
  kNoContentManager,  // no content manager to register the sources with
  kRegisterStructured,           // the <title_id>/<content_type>/<package> root alone
  kRegisterLibrary,              // a flat library alone (src/fs/dlc_library.h)
  kRegisterBoth,                 // both: a structured root and at least one library
};

// The order is the hook's and each step is a precondition of the next: a source that
// holds nothing is not reported as unregisterable, and sources are only offered to a
// manager that exists. `structured_packages` is what ScanDlcRoot() accepted;
// `library_packages` is what ScanDlcLibraries() accepted. `configured` is whether
// anything at all was asked for - a root to scan or a library to scan - so that a
// truly empty configuration reads differently from a configured one that found
// nothing.
constexpr DlcRegistration DecideDlcRegistration(bool configured, std::size_t structured_packages,
                                                std::size_t library_packages,
                                                bool has_content_manager) {
  if (!configured) {
    return DlcRegistration::kNoDirectory;
  }
  if (structured_packages == 0 && library_packages == 0) {
    return DlcRegistration::kNoSource;
  }
  if (!has_content_manager) {
    return DlcRegistration::kNoContentManager;
  }
  if (structured_packages != 0 && library_packages != 0) {
    return DlcRegistration::kRegisterBoth;
  }
  return structured_packages != 0 ? DlcRegistration::kRegisterStructured
                                  : DlcRegistration::kRegisterLibrary;
}

namespace detail {

inline std::string RelativeEntry(const std::filesystem::path& dlc_root,
                                 const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::path relative = std::filesystem::relative(path, dlc_root, ec);
  return (!ec && !relative.empty()) ? relative.generic_string() : path.generic_string();
}

// The child directories of `parent` whose names are content-root names, reporting
// everything else. `level` is what the level is supposed to hold, for the message.
inline std::vector<std::filesystem::path> ChildContentDirectories(
    const std::filesystem::path& parent, const std::string_view level,
    const std::filesystem::path& dlc_root, DlcScanResult* result) {
  std::vector<std::filesystem::path> children;

  std::error_code ec;
  std::filesystem::directory_iterator it(parent, ec);
  if (ec) {
    result->rejected.push_back({RelativeEntry(dlc_root, parent), "cannot be read"});
    return children;
  }

  for (const auto& entry : it) {
    if (!entry.is_directory(ec) || ec) {
      result->rejected.push_back({RelativeEntry(dlc_root, entry.path()),
                                  "not a directory: expected <" + std::string(level) + ">"});
      continue;
    }
    if (!IsContentRootName(entry.path().filename().string())) {
      result->rejected.push_back({RelativeEntry(dlc_root, entry.path()),
                                  "not a " + std::string(level) +
                                      ": expected 8 upper-case hex digits"});
      continue;
    }
    children.push_back(entry.path());
  }

  return children;
}

}  // namespace detail

// Finds <dlc_root>/<title_id>/<content_type>/<package>. A directory that does not
// exist yields nothing, not an error: an absent DLC folder is the normal case.
inline DlcScanResult ScanDlcRoot(const std::filesystem::path& dlc_root) {
  DlcScanResult result;

  std::error_code ec;
  if (!std::filesystem::is_directory(dlc_root, ec) || ec) {
    return result;
  }

  for (const auto& title_path :
       detail::ChildContentDirectories(dlc_root, "title_id", dlc_root, &result)) {
    const std::string title_id = title_path.filename().string();

    for (const auto& type_path :
         detail::ChildContentDirectories(title_path, "content_type", dlc_root, &result)) {
      const std::string content_type = type_path.filename().string();

      std::filesystem::directory_iterator items(type_path, ec);
      if (ec) {
        result.rejected.push_back({detail::RelativeEntry(dlc_root, type_path), "cannot be read"});
        continue;
      }

      for (const auto& item : items) {
        const bool is_directory = item.is_directory(ec) && !ec;
        const bool is_file = !is_directory && item.is_regular_file(ec) && !ec;
        if (!is_directory && !is_file) {
          result.rejected.push_back(
              {detail::RelativeEntry(dlc_root, item.path()), "not a file or directory"});
          continue;
        }
        if (is_file && !LooksLikeContentPackage(item.path())) {
          result.rejected.push_back({detail::RelativeEntry(dlc_root, item.path()),
                                     "not a content package: no CON/LIVE/PIRS magic"});
          continue;
        }

        result.packages.push_back(
            {item.path(), title_id, content_type, item.path().filename().string()});
      }
    }
  }

  return result;
}

}  // namespace rb_blitz::fs
