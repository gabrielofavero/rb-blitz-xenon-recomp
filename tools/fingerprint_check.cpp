// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Reads config/game_fingerprints.toml and either verifies the local game dump
// against it (--check, used by the build gate that runs before codegen) or turns
// it into the expectation header the runtime logs its boot identity against
// (--emit-header).
//
// Exit codes: 0 success, 1 game data does not match, 2 usage or fingerprint file
// error. The gate treats anything non-zero as fatal, so a file this tool cannot
// understand fails closed too.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "util/game_fingerprint.h"

namespace {

namespace fs = std::filesystem;
using rb_blitz::fingerprint::FileCheck;
using rb_blitz::fingerprint::GameFingerprint;
using rb_blitz::fingerprint::kEntrypointRole;

constexpr int kExitOk = 0;
constexpr int kExitMismatch = 1;
constexpr int kExitUsage = 2;

#ifdef RBBLITZ_PROJECT_DIR
constexpr const char* kDefaultProjectDir = RBBLITZ_PROJECT_DIR;
#else
constexpr const char* kDefaultProjectDir = ".";
#endif

// Where to look when the builder did not say. Kept in sync with the aliases in
// docs/deluxe-compat.md.
constexpr const char* kFingerprintFile = "config/game_fingerprints.toml";
constexpr const char* kGameRootDir = "game";

struct Options {
  fs::path project_dir = kDefaultProjectDir;
  fs::path fingerprints;
  fs::path game_root;
  fs::path emit_header;
  std::vector<std::string> roles;
  bool check = false;
  bool all = false;
  bool allow_mismatch = false;
  bool quiet = false;
};

void PrintUsage() {
  std::cout <<
      "Usage: rb_blitz_fingerprint [--project-dir <dir>] [--fingerprints <file>]\n"
      "                            [--root <dir>] [--check] [--role <role>] [--all]\n"
      "                            [--allow-mismatch] [--emit-header <file>] [--quiet]\n"
      "\n"
      "Compares the local game dump with config/game_fingerprints.toml, the record of\n"
      "the supported Rock Band Blitz revision.\n"
      "\n"
      "  --project-dir <dir>    repository root the defaults below are relative to\n"
      "  --fingerprints <file>  expected values (default <project-dir>/config/game_fingerprints.toml)\n"
      "  --root <dir>           game data root (default <project-dir>/game)\n"
      "  --check                verify files; the default action (exit 1 on mismatch)\n"
      "  --role <role>          restrict --check to one role, repeatable\n"
      "                         (default: " << kEntrypointRole << ")\n"
      "  --all                  --check every entry, including the 360 MB archive\n"
      "  --allow-mismatch       report mismatches but still exit 0\n"
      "  --emit-header <file>   write the C++ expectation header instead\n"
      "  --quiet                only report problems\n"
      "\n"
      "Exit codes: 0 match, 1 mismatch or unreadable game data, 2 usage or malformed\n"
      "fingerprint file.\n";
}

bool ParseOptions(int argc, char** argv, Options* options, std::string* error) {
  auto need_value = [&](int index, const char* flag) {
    if (index >= argc) {
      *error = std::string(flag) + " needs a value";
      return false;
    }
    return true;
  };

  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--help" || flag == "-h") {
      PrintUsage();
      std::exit(kExitOk);
    } else if (flag == "--project-dir") {
      if (!need_value(++i, "--project-dir")) return false;
      options->project_dir = argv[i];
    } else if (flag == "--fingerprints") {
      if (!need_value(++i, "--fingerprints")) return false;
      options->fingerprints = argv[i];
    } else if (flag == "--root") {
      if (!need_value(++i, "--root")) return false;
      options->game_root = argv[i];
    } else if (flag == "--emit-header") {
      if (!need_value(++i, "--emit-header")) return false;
      options->emit_header = argv[i];
    } else if (flag == "--role") {
      if (!need_value(++i, "--role")) return false;
      options->roles.emplace_back(argv[i]);
    } else if (flag == "--check") {
      options->check = true;
    } else if (flag == "--all") {
      options->all = true;
      options->check = true;
    } else if (flag == "--allow-mismatch") {
      options->allow_mismatch = true;
    } else if (flag == "--quiet") {
      options->quiet = true;
    } else {
      *error = "unknown option '" + std::string(flag) + "'";
      return false;
    }
  }

  if (options->fingerprints.empty()) {
    options->fingerprints = options->project_dir / kFingerprintFile;
  }
  if (options->game_root.empty()) {
    options->game_root = options->project_dir / kGameRootDir;
  }
  if (!options->check && options->emit_header.empty()) {
    options->check = true;
  }
  if (options->all && !options->roles.empty()) {
    *error = "--all and --role are mutually exclusive";
    return false;
  }
  if (!options->check && !options->roles.empty()) {
    *error = "--role only applies to --check";
    return false;
  }
  if (options->check && options->roles.empty() && !options->all) {
    options->roles.emplace_back(kEntrypointRole);
  }
  return true;
}

bool WriteFile(const fs::path& path, std::string_view contents, std::string* error) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
      *error = "cannot create " + path.parent_path().string() + ": " + ec.message();
      return false;
    }
  }
  // Binary mode keeps the line endings as emitted, so the header is identical on
  // every host and diffing a regenerated copy is meaningful.
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    *error = "cannot write " + path.string();
    return false;
  }
  file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  file.close();
  if (!file) {
    *error = "cannot write " + path.string();
    return false;
  }
  return true;
}

// The one-line summary that identifies the supported revision, so a build log
// says which dump it was checked against rather than only that a hash matched.
std::string DescribeFingerprint(const GameFingerprint& fingerprint, const fs::path& source) {
  std::string line = fingerprint.name.empty() ? std::string("(unnamed game)") : fingerprint.name;
  if (!fingerprint.title_id.empty()) {
    line += "  title " + fingerprint.title_id;
  }
  if (!fingerprint.media_id.empty()) {
    line += "  media " + fingerprint.media_id;
  }
  if (!fingerprint.region.empty()) {
    line += "  " + fingerprint.region;
  }
  if (!fingerprint.xex_version.empty()) {
    line += "  xex " + fingerprint.xex_version;
  }
  if (fingerprint.title_update_applied) {
    line += "  title update applied";
  }
  line += "  (schema " + std::to_string(fingerprint.schema_version) + ", " +
          source.filename().string() + ")";
  return line;
}

void PrintMismatchAdvice(const fs::path& game_root) {
  std::cerr << "\nThe game data at \"" << game_root.string()
            << "\" is not the revision config/game_fingerprints.toml describes.\n"
               "Recompiled addresses come from that revision, so building against another one\n"
               "produces code that does not match the dump.\n"
               "  * A Rock Band Blitz Deluxe or otherwise modified content root is expected to\n"
               "    differ. Codegen still has to run on the vanilla dump; see\n"
               "    docs/deluxe-compat.md, and configure with -DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON\n"
               "    to build anyway.\n"
               "  * Otherwise re-dump the game, or if you are deliberately moving to a new\n"
               "    revision, update config/game_fingerprints.toml (sha256sum or Get-FileHash)\n"
               "    and commit the new sizes and digests together with the regenerated code.\n";
}

int RunCheck(const Options& options, const GameFingerprint& fingerprint) {
  std::vector<FileCheck> checks;
  std::string error;
  if (!rb_blitz::fingerprint::VerifyRoles(fingerprint, options.game_root, options.roles, &checks,
                                          &error)) {
    std::cerr << "rb_blitz_fingerprint: " << error << "\n";
    return kExitUsage;
  }

  if (!options.quiet) {
    std::cout << DescribeFingerprint(fingerprint, options.fingerprints) << "\n";
    for (const FileCheck& check : checks) {
      std::cout << rb_blitz::fingerprint::FormatCheckLine(check) << "\n";
    }
  }

  if (rb_blitz::fingerprint::AllMatched(checks)) {
    return kExitOk;
  }

  if (options.quiet) {
    for (const FileCheck& check : checks) {
      if (check.verdict != rb_blitz::fingerprint::FileVerdict::kMatch) {
        std::cerr << rb_blitz::fingerprint::FormatCheckLine(check) << "\n";
      }
    }
  }
  PrintMismatchAdvice(options.game_root);
  return options.allow_mismatch ? kExitOk : kExitMismatch;
}

int RunEmitHeader(const Options& options, const GameFingerprint& fingerprint) {
  std::string header;
  std::string error;
  if (!rb_blitz::fingerprint::EmitExpectedHeader(fingerprint, options.fingerprints.filename().string(),
                                                 &header, &error)) {
    std::cerr << "rb_blitz_fingerprint: " << error << "\n";
    return kExitUsage;
  }
  if (!WriteFile(options.emit_header, header, &error)) {
    std::cerr << "rb_blitz_fingerprint: " << error << "\n";
    return kExitUsage;
  }
  if (!options.quiet) {
    std::cout << "wrote " << options.emit_header.string() << "\n";
  }
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!ParseOptions(argc, argv, &options, &error)) {
    std::cerr << "rb_blitz_fingerprint: " << error << "\n\n";
    PrintUsage();
    return kExitUsage;
  }

  GameFingerprint fingerprint;
  if (!rb_blitz::fingerprint::LoadGameFingerprint(options.fingerprints, &fingerprint, &error)) {
    std::cerr << "rb_blitz_fingerprint: " << options.fingerprints.string() << ": " << error << "\n";
    return kExitUsage;
  }

  int result = kExitOk;
  if (options.check) {
    result = RunCheck(options, fingerprint);
  }
  if (result == kExitOk && !options.emit_header.empty()) {
    result = RunEmitHeader(options, fingerprint);
  }
  return result;
}
