// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Consumer for config/toolchain.toml: the build toolchain this project is frozen
// to, in the repository rather than in one machine's build tree.
//
// Two callers, one parser (tools/toolchain_check.cpp):
//   * `--check`, run by the build before codegen, compares the toolchain the
//     build is about to use against the frozen record and reports every component
//     (docs/toolchain.md). A component below its recorded minimum, or the SDK on
//     a different commit, stops the build; anything else is reported and builds,
//     unless the build asked for a strict toolchain.
//   * `--emit-header`, which turns what the check found into the stamp
//     src/rb_blitz_app.h logs at boot, so a log says which toolchain built the
//     binary next to the game data it booted.
//
// The file is parsed with the same deliberately small TOML subset the fingerprint
// reader uses - comments, `key = value`, `[section]` - plus single-quoted literal
// strings, which is what Windows paths want. Unknown sections and keys are
// ignored so the record can grow ahead of this tool.
//
// SDK-free and host-side, so tests/toolchain_tests.cpp covers it: the comparison
// takes what a machine reports as input instead of probing it here.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::toolchain {

inline constexpr int kSupportedSchemaVersion = 1;

// How a component's recorded value is compared.
enum class Kind {
  kVersion,  // dotted numbers, comparable: `minimum`, when set, is the oldest accepted
  kCommit,   // an exact revision: only equality is meaningful
};

// One component of the frozen set, as config/toolchain.toml records it. A section
// pins a component when it has a `commit` (Kind::kCommit, exact) or a `version`
// (Kind::kVersion); a section with a `commit` may also carry a `version`, which is
// then informational, as the SDK's is.
struct PinnedComponent {
  std::string key;            // config section, e.g. "clang"
  std::string name;           // display name, e.g. "MSVC toolset"
  std::string expected;       // the value a machine must report
  std::string minimum;        // "" = none recorded
  std::string recorded_path;  // "" = none recorded; informational only
  std::string recorded_note;  // informational, e.g. the SDK version behind a commit pin
  Kind kind = Kind::kVersion;
  bool require_exact = false;  // a difference is fatal (the SDK pin)
};

struct Pin {
  int schema_version = 0;
  std::vector<PinnedComponent> components;  // in report order
};

// Both return false and fill `error` with a `line N: ...` message on a malformed
// or unsupported file.
bool ParsePin(std::string_view text, Pin* out, std::string* error);
bool LoadPin(const std::filesystem::path& path, Pin* out, std::string* error);

// What the machine reported for one component. The probing lives in the tool;
// this is the input side of the comparison, which is what makes it testable
// without a toolchain. An empty `value` means "could not be determined".
struct Reported {
  std::string value;
  std::string detail;  // path, `git describe`, or why it is unknown
};

enum class Verdict {
  kMatch,
  kDifferent,  // a version that is not the frozen one but is not below the minimum
  kTooOld,     // below the recorded minimum
  kUnknown,    // the machine would not say
};

std::string_view DescribeVerdict(Verdict verdict);

struct ComponentCheck {
  std::string name;
  std::string expected;
  std::string minimum;
  std::string actual;
  std::string actual_detail;  // what the machine said about itself
  std::string detail;         // the reason, for a verdict that is not kMatch
  Kind kind = Kind::kVersion;
  Verdict verdict = Verdict::kUnknown;
  bool fatal = false;
};

struct CheckOptions {
  // Every deviation is fatal, for a release build that wants the frozen set or
  // nothing (CMake: -DRBBLITZ_STRICT_TOOLCHAIN=ON).
  bool strict = false;
  // Nothing is fatal, not even a minimum: report and build.
  bool allow_other_toolchain = false;
};

// One entry per pinned component, in the pin's order. A component the machine
// said nothing about comes back as kUnknown rather than an error: a build that
// cannot run the compiler fails on its own terms, and the report still explains
// what was expected.
std::vector<ComponentCheck> ComparePin(const Pin& pin,
                                       const std::map<std::string, Reported>& reported,
                                       const CheckOptions& options);

bool AllMatched(const std::vector<ComponentCheck>& checks);
bool AnyFatal(const std::vector<ComponentCheck>& checks);

// One line, aligned for reading a list of checks: verdict, component, what the
// machine reported, and either that detail or the reason it differs.
std::string FormatCheckLine(const ComponentCheck& check);

// The frozen set as one line: "rexglue-sdk c94f5ebd, clang 23.1.1, ...".
std::string FormatPin(const Pin& pin);

// What the machine has, as one line, with how it relates to the frozen set:
// "... (frozen set)" or "... (off the frozen set: Ninja 1.13.2, MSVC toolset 14.44.35207)".
std::string FormatStamp(const std::vector<ComponentCheck>& checks);

// C++ source of the generated header src/rb_blitz_app.h logs
// RBBLITZ_TOOLCHAIN_STAMP from. `source_name` is only used in the header comment.
bool EmitBuildHeader(const Pin& pin, const std::vector<ComponentCheck>& checks,
                     std::string_view source_name, std::string* out, std::string* error);

// Dotted-number ordering, for Kind::kVersion and the minimum test: components are
// compared left to right, a missing one counts as zero, and anything after the
// first non-numeric character of a component is ignored ("23.1.1-x" == "23.1.1").
// Negative when `a` is older than `b`.
int CompareVersions(std::string_view a, std::string_view b);

}  // namespace rb_blitz::toolchain
