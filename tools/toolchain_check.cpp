// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Reads config/toolchain.toml and either compares the local toolchain against it
// (--check, run by the build before codegen) or turns what it found into the stamp
// header src/rb_blitz_app.h logs at boot (--emit-header).
//
// Everything it compares is probed here rather than passed in, so the same binary
// can answer "is this machine the frozen set?" on its own - a developer checking a
// box before reporting a build failure runs exactly what the build runs. The four
// probes:
//   * the compiler named by --cxx (default clang++ on PATH), through
//     `<cxx> -v -E -x c++ <empty file>`: its own version, and the MSVC toolset and
//     Windows SDK the build will actually take headers from, both read off the
//     include roots it prints;
//   * --cmake (default cmake on PATH) --version;
//   * --ninja (default ninja on PATH) --version;
//   * the ReXGlue SDK checkout at --sdk-dir, through `git rev-parse HEAD`.
// A probe that cannot run is not an error: the component comes back "unknown", the
// report says so, and a build that really cannot use its toolchain fails on its own
// terms.
//
// The report is one line for the frozen record and one per component, in the shape
// of the game-data gate:
//
//   toolchain  rexglue-sdk c94f5ebd, clang 23.1.1, ...  (config/toolchain.toml)
//   ok         rexglue-sdk      c94f5ebd          nightly-20260826-f5337cdc-2-gc94f5eb
//   ok         clang            23.1.1            C:\Program Files\LLVM\bin\clang++.exe
//   different  Ninja            1.13.2            frozen 1.13.1
//
// Exit codes: 0 usable toolchain, 1 a component below its minimum or the SDK off
// its pin (or anything at all, under --strict), 2 usage or pin file error.

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "util/toolchain_pin.h"

#if defined(_WIN32)
#define RBBLITZ_POPEN _popen
#define RBBLITZ_PCLOSE _pclose
#else
#define RBBLITZ_POPEN popen
#define RBBLITZ_PCLOSE pclose
#endif

namespace {

namespace fs = std::filesystem;
using rb_blitz::toolchain::CheckOptions;
using rb_blitz::toolchain::ComponentCheck;
using rb_blitz::toolchain::Pin;
using rb_blitz::toolchain::Reported;
using rb_blitz::toolchain::Verdict;

constexpr int kExitOk = 0;
constexpr int kExitToolchain = 1;
constexpr int kExitUsage = 2;

#ifdef RBBLITZ_PROJECT_DIR
constexpr const char* kDefaultProjectDir = RBBLITZ_PROJECT_DIR;
#else
constexpr const char* kDefaultProjectDir = ".";
#endif

// Where to look when the builder did not say. Kept in sync with the defaults in
// docs/toolchain.md.
constexpr const char* kPinFile = "config/toolchain.toml";
constexpr const char* kSdkDir = "rexglue-sdk";

struct Options {
  fs::path project_dir = kDefaultProjectDir;
  fs::path pin;
  fs::path sdk_dir;
  fs::path cxx = "clang++";
  fs::path cmake = "cmake";
  fs::path ninja = "ninja";
  fs::path emit_header;
  bool strict = false;
  bool allow_other_toolchain = false;
  bool quiet = false;
};

void PrintUsage() {
  std::cout << "Usage: rb_blitz_toolchain [--project-dir <dir>] [--pin <file>] [--check]\n"
               "                         [--cxx <path>] [--cmake <path>] [--ninja <path>]\n"
               "                         [--sdk-dir <dir>] [--strict] [--allow-other-toolchain]\n"
               "                         [--emit-header <file>] [--quiet]\n"
               "\n"
               "Compares the local toolchain with config/toolchain.toml, the record of the\n"
               "toolchain this project is frozen to (docs/toolchain.md).\n"
               "\n"
               "  --project-dir <dir>      repository root the defaults below are relative to\n"
               "  --pin <file>             frozen record (default <project-dir>/"
            << kPinFile
            << ")\n"
               "  --check                  compare and report, the default action\n"
               "  --cxx <path>             compiler to ask (default clang++ from PATH)\n"
               "  --cmake <path>           CMake to ask (default cmake from PATH)\n"
               "  --ninja <path>           Ninja to ask (default ninja from PATH)\n"
               "  --sdk-dir <dir>          ReXGlue SDK checkout (default <project-dir>/"
            << kSdkDir
            << ")\n"
               "  --strict                 any deviation is fatal, not only a minimum or the pin\n"
               "  --allow-other-toolchain  report deviations but still exit 0\n"
               "  --emit-header <file>     write the boot stamp header instead of reporting\n"
               "  --quiet                  report only what deviates\n"
               "\n"
               "Exit codes: 0 usable, 1 fatal deviation, 2 usage or malformed pin file.\n";
}

bool ParseOptions(int argc, char** argv, Options* options, std::string* error) {
  auto take_value = [&](int* index, const std::string& name) -> const char* {
    if (*index + 1 >= argc) {
      *error = name + " needs a value";
      return nullptr;
    }
    ++*index;
    return argv[*index];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      *error = "help";
      return false;
    } else if (arg == "--check") {
      continue;  // the default action; --check only documents intent
    } else if (arg == "--strict") {
      options->strict = true;
    } else if (arg == "--allow-other-toolchain") {
      options->allow_other_toolchain = true;
    } else if (arg == "--quiet") {
      options->quiet = true;
    } else if (arg == "--project-dir" || arg == "--pin" || arg == "--sdk-dir" ||
               arg == "--cxx" || arg == "--cmake" || arg == "--ninja" ||
               arg == "--emit-header") {
      const char* value = take_value(&i, arg);
      if (value == nullptr) {
        return false;
      }
      if (arg == "--project-dir") {
        options->project_dir = value;
      } else if (arg == "--pin") {
        options->pin = value;
      } else if (arg == "--sdk-dir") {
        options->sdk_dir = value;
      } else if (arg == "--cxx") {
        options->cxx = value;
      } else if (arg == "--cmake") {
        options->cmake = value;
      } else if (arg == "--ninja") {
        options->ninja = value;
      } else {
        options->emit_header = value;
      }
    } else {
      *error = "unknown argument: " + arg;
      return false;
    }
  }

  if (options->pin.empty()) {
    options->pin = options->project_dir / kPinFile;
  }
  if (options->sdk_dir.empty()) {
    options->sdk_dir = options->project_dir / kSdkDir;
  }
  return true;
}

// Runs `command` through the shell and returns everything it printed, stderr
// included. Empty output means the command could not run or said nothing, which
// the callers report as "unknown" rather than as a failure.
//
// The command is wrapped in a second pair of quotes before it reaches cmd.exe,
// because popen hands it to `cmd /c` and cmd strips the first and last quote of a
// command line that starts with one: an unwrapped `"C:\Program Files\...\clang++.exe"
// --version` arrives as `C:\Program Files\...\clang++.exe` plus a stray quote and
// fails as "The filename, directory name, or volume label syntax is incorrect".
// Wrapping makes cmd strip the wrapper instead, leaving the real quoting intact.
std::string Run(const std::string& command) {
  std::string output;
#if defined(_WIN32)
  const std::string shell_command = "\"" + command + "\"";
#else
  const std::string& shell_command = command;
#endif
  std::FILE* pipe = RBBLITZ_POPEN(shell_command.c_str(), "r");
  if (pipe == nullptr) {
    return output;
  }
  std::array<char, 4096> buffer{};
  std::size_t read = 0;
  while ((read = std::fread(buffer.data(), 1, buffer.size(), pipe)) > 0) {
    output.append(buffer.data(), read);
  }
  RBBLITZ_PCLOSE(pipe);
  return output;
}

std::string Quote(const fs::path& path) { return "\"" + path.string() + "\""; }

std::string FirstMatch(const std::string& text, const std::regex& pattern, int group = 1) {
  std::smatch match;
  if (std::regex_search(text, match, pattern) && match.size() > static_cast<std::size_t>(group) &&
      match[group].matched) {
    return match[group].str();
  }
  return {};
}

// Asks the compiler about itself and files the three answers it gives in one run:
// its version, the MSVC toolset whose headers it will include, and the Windows SDK
// version those headers come from. This is the check docs/build-and-run.md §1
// describes for a hand-written toolchain check, with the parsing done for you.
void ProbeCompiler(const fs::path& cxx, Reported* compiler, Reported* msvc, Reported* windows_sdk) {
  const fs::path probe = fs::temp_directory_path() / "rb_blitz_toolchain_probe.cpp";
  {
    std::ofstream file(probe, std::ios::binary);
    file << "// written by rb_blitz_toolchain to ask the compiler what it is\n";
  }
  const std::string output = Run(Quote(cxx) + " -v -E -x c++ " + Quote(probe) + " 2>&1");
  std::error_code ec;
  fs::remove(probe, ec);

  if (output.empty()) {
    const std::string reason = "could not run " + cxx.string();
    compiler->detail = reason;
    msvc->detail = reason;
    windows_sdk->detail = reason;
    return;
  }

  compiler->value = FirstMatch(output, std::regex("clang version ([0-9]+\\.[0-9]+\\.[0-9]+)"));
  const std::string installed_dir = FirstMatch(output, std::regex("InstalledDir: (.+)"));
  if (compiler->value.empty()) {
    // Something ran but did not describe itself as clang, which is worth saying
    // rather than printing as a path.
    compiler->detail = "no clang version in what " + cxx.string() + " printed";
  } else if (cxx.has_parent_path()) {
    // The path the caller named is what the build will use, so it is the one to
    // print; a bare `clang++` was resolved through PATH, and then the directory
    // clang reports about itself is the useful half.
    compiler->detail = cxx.string();
  } else {
    compiler->detail = installed_dir.empty() ? cxx.string() : installed_dir;
  }

  msvc->value = FirstMatch(output, std::regex("VC[\\\\/]Tools[\\\\/]MSVC[\\\\/]([^\\\\/\"\\s]+)"));
  msvc->detail = msvc->value.empty() ? "no MSVC toolset in the compiler's include roots"
                                     : "from the compiler's include roots";

  windows_sdk->value = FirstMatch(
      output, std::regex("Windows Kits[\\\\/][0-9.]+[\\\\/]Include[\\\\/]([^\\\\/\"\\s]+)"));
  windows_sdk->detail = windows_sdk->value.empty()
                            ? "no Windows Kits include root in the compiler's search path"
                            : "from the compiler's include roots";
}

std::map<std::string, Reported> Probe(const Options& options) {
  std::map<std::string, Reported> reported;

  Reported compiler;
  Reported msvc;
  Reported windows_sdk;
  ProbeCompiler(options.cxx, &compiler, &msvc, &windows_sdk);
  reported["clang"] = compiler;
  reported["msvc"] = msvc;
  reported["windows_sdk"] = windows_sdk;

  Reported cmake;
  const std::string cmake_output = Run(Quote(options.cmake) + " --version 2>&1");
  cmake.value =
      FirstMatch(cmake_output, std::regex("cmake version ([0-9]+\\.[0-9]+\\.[0-9]+)"));
  cmake.detail = cmake.value.empty() ? "could not run " + options.cmake.string()
                                     : options.cmake.string();
  reported["cmake"] = cmake;

  Reported ninja;
  const std::string ninja_output = Run(Quote(options.ninja) + " --version 2>&1");
  ninja.value = FirstMatch(ninja_output, std::regex("^([0-9]+\\.[0-9]+\\.[0-9]+)"));
  ninja.detail = ninja.value.empty() ? "could not run " + options.ninja.string()
                                     : options.ninja.string();
  reported["ninja"] = ninja;

  Reported sdk;
  const std::string head = Run("git -C " + Quote(options.sdk_dir) + " rev-parse HEAD 2>&1");
  sdk.value = FirstMatch(head, std::regex("^([0-9a-f]{40})"));
  if (sdk.value.empty()) {
    sdk.detail = "no git checkout at " + options.sdk_dir.string();
  } else {
    sdk.detail = FirstMatch(Run("git -C " + Quote(options.sdk_dir) +
                                " describe --tags --always 2>&1"),
                            std::regex("^([0-9a-fA-Za-z.\\-]+)"));
    if (sdk.detail.empty()) {
      sdk.detail = options.sdk_dir.string();
    }
  }
  reported["sdk"] = sdk;

  return reported;
}

int Report(const std::vector<ComponentCheck>& checks, const Pin& pin, const fs::path& pin_path,
           const Options& options) {
  const bool matched = rb_blitz::toolchain::AllMatched(checks);
  const bool fatal = rb_blitz::toolchain::AnyFatal(checks);

  if (!options.quiet) {
    fs::path pin_shown = pin_path;
    std::cout << "toolchain  " << rb_blitz::toolchain::FormatPin(pin) << "  ("
              << pin_shown.make_preferred().string() << ")\n";
  }
  for (const ComponentCheck& check : checks) {
    if (options.quiet && check.verdict == Verdict::kMatch) {
      continue;
    }
    std::cout << rb_blitz::toolchain::FormatCheckLine(check) << "\n";
  }

  // "unknown" means the probe could not read that component rather than that it
  // differs, and the two want different reactions, so they are counted apart.
  std::string unmeasured;
  std::string deviating;
  for (const ComponentCheck& check : checks) {
    if (check.verdict == Verdict::kMatch) {
      continue;
    }
    if (check.verdict == Verdict::kUnknown) {
      unmeasured += (unmeasured.empty() ? "" : ", ") + check.name;
    } else {
      deviating += (deviating.empty() ? "" : ", ") + check.name;
    }
  }

  if (!unmeasured.empty()) {
    std::cout << "  [note] could not be measured on this machine: " << unmeasured << "\n";
  }
  if (!deviating.empty()) {
    std::cout << "  not the frozen set: " << deviating << "\n";
  }

  if (fatal && !options.allow_other_toolchain) {
    std::cout << "This is not the frozen toolchain, and the deviation is one that stops the build"
              << (options.strict ? " (strict was asked for)" : "")
              << ". Fix the toolchain (docs/toolchain.md), or build with "
                 "-DRBBLITZ_ALLOW_OTHER_TOOLCHAIN=ON to report and continue.\n";
    return kExitToolchain;
  }
  if (!deviating.empty() || !unmeasured.empty()) {
    std::cout << "Not the frozen toolchain; building anyway. docs/toolchain.md records what it "
                 "is, and -DRBBLITZ_STRICT_TOOLCHAIN=ON makes this fatal.\n";
  } else if (!options.quiet) {
    std::cout << "ok         the frozen toolchain\n";
  }
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!ParseOptions(argc, argv, &options, &error)) {
    if (error == "help") {
      PrintUsage();
      return kExitOk;
    }
    std::cerr << "rb_blitz_toolchain: " << error << "\n\n";
    PrintUsage();
    return kExitUsage;
  }

  Pin pin;
  if (!rb_blitz::toolchain::LoadPin(options.pin, &pin, &error)) {
    std::cerr << "rb_blitz_toolchain: " << options.pin.string() << ": " << error << "\n";
    return kExitUsage;
  }

  CheckOptions check_options;
  check_options.strict = options.strict;
  check_options.allow_other_toolchain = options.allow_other_toolchain;

  const std::vector<ComponentCheck> checks =
      rb_blitz::toolchain::ComparePin(pin, Probe(options), check_options);

  if (!options.emit_header.empty()) {
    std::string header;
    if (!rb_blitz::toolchain::EmitBuildHeader(pin, checks, options.pin.filename().string(), &header,
                                             &error)) {
      std::cerr << "rb_blitz_toolchain: " << error << "\n";
      return kExitUsage;
    }
    std::ofstream file(options.emit_header, std::ios::binary | std::ios::trunc);
    if (!file) {
      std::cerr << "rb_blitz_toolchain: cannot write " << options.emit_header.string() << "\n";
      return kExitUsage;
    }
    file << header;
    if (!options.quiet) {
      // The emitted stamp is the build's own record of itself, so it is worth a
      // line in the build log even when nothing deviates.
      std::cout << "toolchain  " << rb_blitz::toolchain::FormatStamp(checks) << " -> "
                << options.emit_header.string() << "\n";
    }
    return kExitOk;
  }

  return Report(checks, pin, options.pin, options);
}
