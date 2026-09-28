// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for src/util/toolchain_pin.{h,cpp} - the parser and the comparison
// behind the build's toolchain gate and the boot stamp in src/rb_blitz_app.h. No
// SDK, no build, no compiler probe: what a machine reports is an input here, which
// is the whole reason the comparison is not part of tools/toolchain_check.cpp.
//
// The cases that matter are the ones a build would otherwise only show on a machine
// nobody has: a newer compiler (reported, still builds), an older one (fatal), the
// SDK on another commit (fatal, it is a pin), a component the probe could not
// answer (reported, not fatal), and the strict/allow switches that change which of
// those stop a build.

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "util/toolchain_pin.h"

namespace {

namespace fs = std::filesystem;
using rb_blitz::test::BeginCase;
using rb_blitz::test::Fail;
using rb_blitz::test::g_checks;
using rb_blitz::toolchain::CheckOptions;
using rb_blitz::toolchain::ComponentCheck;
using rb_blitz::toolchain::Kind;
using rb_blitz::toolchain::Pin;
using rb_blitz::toolchain::PinnedComponent;
using rb_blitz::toolchain::Reported;
using rb_blitz::toolchain::Verdict;

#ifdef RBBLITZ_PROJECT_DIR
constexpr const char* kDefaultProjectDir = RBBLITZ_PROJECT_DIR;
#else
constexpr const char* kDefaultProjectDir = ".";
#endif

void CheckStringEq(const char* file, int line, std::string_view actual, std::string_view expected) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, "expected \"" + std::string(expected) + "\"\n         actual   \"" +
                       std::string(actual) + "\"");
}

void CheckContains(const char* file, int line, std::string_view haystack,
                   std::string_view needle) {
  ++g_checks;
  if (haystack.find(needle) != std::string_view::npos) {
    return;
  }
  Fail(file, line, "expected to find \"" + std::string(needle) + "\" in\n         \"" +
                       std::string(haystack) + "\"");
}

void CheckVerdict(const char* file, int line, Verdict actual, Verdict expected) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string("verdict ") +
                       std::string(rb_blitz::toolchain::DescribeVerdict(actual)) + " != " +
                       std::string(rb_blitz::toolchain::DescribeVerdict(expected)));
}

#define CHECK_STR_EQ(actual, expected) CheckStringEq(__FILE__, __LINE__, actual, expected)
#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, haystack, needle)
#define CHECK_VERDICT(actual, expected) CheckVerdict(__FILE__, __LINE__, actual, expected)

// The shape of config/toolchain.toml, small enough to read in a test: a commit pin
// with an informational version, two version pins with minimums, one without, and a
// section that pins nothing.
constexpr const char* kSamplePin = R"(
# a pin in the shape of config/toolchain.toml
schema_version = 1

[sdk]
version = "0.10.0.0-dev.unknown"
commit = "c94f5ebdcb3c9d1a460ca48e04f9758448f8d518"

[clang]
version = "23.1.1"
minimum = "20"
path = 'C:\Program Files\LLVM\bin\clang++.exe'   # a literal string keeps the backslashes

[cmake]
version = "4.4.3"
minimum = "3.25"

[ninja]
version = "1.13.2"

[msvc]
version = "14.44.35207"

[windows_sdk]
version = "10.0.26100.0"

[notes]
text = "a section with no version or commit pins nothing"
)";

// The report order the pin asks for, so the SDK pin is read first.
constexpr const char* kExpectedOrder[] = {"sdk",     "clang", "cmake",
                                          "ninja",   "msvc",  "windows_sdk"};

Pin ParseSample() {
  Pin pin;
  std::string error;
  if (!rb_blitz::toolchain::ParsePin(kSamplePin, &pin, &error)) {
    Fail(__FILE__, __LINE__, "the sample pin did not parse: " + error);
  }
  return pin;
}

const ComponentCheck* FindCheck(const std::vector<ComponentCheck>& checks, std::string_view name) {
  for (const ComponentCheck& check : checks) {
    if (check.name == name) {
      return &check;
    }
  }
  return nullptr;
}

ComponentCheck* FindMutableCheck(std::vector<ComponentCheck>& checks, std::string_view name) {
  for (ComponentCheck& check : checks) {
    if (check.name == name) {
      return &check;
    }
  }
  return nullptr;
}

const PinnedComponent* FindComponent(const Pin& pin, std::string_view key) {
  for (const PinnedComponent& component : pin.components) {
    if (component.key == key) {
      return &component;
    }
  }
  return nullptr;
}

// A machine that is the frozen set, so each case only has to change what it is
// about.
std::map<std::string, Reported> FrozenMachine() {
  return {
      {"sdk", {"c94f5ebdcb3c9d1a460ca48e04f9758448f8d518", "nightly-20260826-f5337cdc-2-gc94f5eb"}},
      {"clang", {"23.1.1", "C:\\Program Files\\LLVM\\bin\\clang++.exe"}},
      {"cmake", {"4.4.3", "C:\\Program Files\\CMake\\bin\\cmake.exe"}},
      {"ninja", {"1.13.2", "ninja.exe"}},
      {"msvc", {"14.44.35207", "from the compiler's include roots"}},
      {"windows_sdk", {"10.0.26100.0", "from the compiler's include roots"}},
  };
}

void TestCompareVersions() {
  BeginCase("CompareVersions");
  using rb_blitz::toolchain::CompareVersions;

  CHECK_EQ(CompareVersions("23.1.1", "23.1.1"), 0);
  CHECK_EQ(CompareVersions("23.1.0", "23.1.1"), -1);
  CHECK_EQ(CompareVersions("23.2.0", "23.1.9"), 1);
  // A missing component is zero, so a coarse minimum still works.
  CHECK_EQ(CompareVersions("23.1", "23.1.0"), 0);
  CHECK_EQ(CompareVersions("20", "20.1.8"), -1);
  CHECK_EQ(CompareVersions("3.25", "3.25.0"), 0);
  // Numeric, not lexicographic.
  CHECK_EQ(CompareVersions("1.9.0", "1.10.0"), -1);
  CHECK_EQ(CompareVersions("4.4.3", "4.10.0"), -1);
  // A suffix is not part of a component.
  CHECK_EQ(CompareVersions("23.1.1-x", "23.1.1"), 0);
  CHECK_EQ(CompareVersions("0.10.0.0-dev.unknown", "0.10.0.0"), 0);
  CHECK_EQ(CompareVersions("0.10.0.0-dev.unknown", "0.10.1"), -1);
}

void TestParsePin() {
  BeginCase("ParsePin");
  const Pin pin = ParseSample();

  CHECK_EQ(pin.schema_version, 1);
  // Six sections pin something; [notes] pins nothing and is left out.
  CHECK_EQ(pin.components.size(), 6);
  for (std::size_t i = 0; i < pin.components.size(); ++i) {
    CHECK_STR_EQ(pin.components[i].key, kExpectedOrder[i]);
  }

  const PinnedComponent* sdk = FindComponent(pin, "sdk");
  CHECK_TRUE(sdk != nullptr);
  if (sdk != nullptr) {
    CHECK_STR_EQ(sdk->name, "rexglue-sdk");
    CHECK_STR_EQ(sdk->expected, "c94f5ebdcb3c9d1a460ca48e04f9758448f8d518");
    CHECK_TRUE(sdk->kind == Kind::kCommit);
    CHECK_TRUE(sdk->require_exact);
    // The version behind a commit pin is recorded, not compared - it depends on
    // whether the SDK's tags were fetched.
    CHECK_CONTAINS(sdk->recorded_note, "0.10.0.0-dev.unknown");
  }

  const PinnedComponent* clang = FindComponent(pin, "clang");
  CHECK_TRUE(clang != nullptr);
  if (clang != nullptr) {
    CHECK_STR_EQ(clang->name, "clang");
    CHECK_STR_EQ(clang->expected, "23.1.1");
    CHECK_STR_EQ(clang->minimum, "20");
    CHECK_TRUE(clang->kind == Kind::kVersion);
    CHECK_FALSE(clang->require_exact);
    // A single-quoted value is literal: a Windows path keeps its backslashes.
    CHECK_STR_EQ(clang->recorded_path, "C:\\Program Files\\LLVM\\bin\\clang++.exe");
  }

  CHECK_STR_EQ(FindComponent(pin, "cmake")->minimum, "3.25");
  CHECK_STR_EQ(FindComponent(pin, "msvc")->name, "MSVC toolset");
  CHECK_STR_EQ(FindComponent(pin, "msvc")->expected, "14.44.35207");
  CHECK_STR_EQ(FindComponent(pin, "windows_sdk")->name, "Windows SDK");
}

void TestParseTolerates() {
  BeginCase("ParsePin tolerates");
  Pin pin;
  std::string error;

  // Comments, blank lines and CRLF; an unknown key and an unknown section are
  // ignored, so the file can grow ahead of this tool.
  const char* text =
      "# comment\r\n"
      "schema_version = 1\r\n"
      "\r\n"
      "[clang]\r\n"
      "version = \"23.1.1\"   # trailing comment\r\n"
      "future_key = \"a value this tool does not know\"\r\n"
      "[future_component]\r\n"
      "version = \"1.0\"\r\n";
  CHECK_TRUE(rb_blitz::toolchain::ParsePin(text, &pin, &error));
  CHECK_EQ(pin.components.size(), 2);
  CHECK_STR_EQ(pin.components[0].expected, "23.1.1");
  // An unknown section is still checked, under its own name.
  CHECK_STR_EQ(pin.components[1].name, "future_component");
  CHECK_STR_EQ(pin.components[1].expected, "1.0");

  // require_exact on a version pin makes that one exact without touching the rest.
  const char* exact = "schema_version = 1\n[clang]\nversion = \"23.1.1\"\nrequire_exact = true\n";
  CHECK_TRUE(rb_blitz::toolchain::ParsePin(exact, &pin, &error));
  CHECK_TRUE(pin.components[0].require_exact);
}

void TestParseErrors() {
  BeginCase("ParsePin errors");
  auto fails_with = [](const char* text, std::string_view needle) {
    Pin pin;
    std::string error;
    if (rb_blitz::toolchain::ParsePin(text, &pin, &error)) {
      Fail(__FILE__, __LINE__, std::string("expected a parse error for: ") + text);
      return;
    }
    CheckContains(__FILE__, __LINE__, error, needle);
  };

  fails_with("[clang]\nversion = \"1.0\"\n", "schema_version");
  fails_with("schema_version = 2\n[clang]\nversion = \"1.0\"\n", "unsupported schema_version");
  fails_with("schema_version = x\n[clang]\nversion = \"1.0\"\n", "not a number");
  fails_with("schema_version = 1\n[clang\nversion = \"1.0\"\n", "malformed section header");
  fails_with("schema_version = 1\n[]\n", "empty section name");
  fails_with("schema_version = 1\n[clang]\nversion \"1.0\"\n", "expected `key = value`");
  fails_with("schema_version = 1\n[clang]\nversion =\n", "value is missing");
  fails_with("schema_version = 1\n[clang]\nversion = \"a\\q\"\n", "unknown escape");
  fails_with("schema_version = 1\n[clang]\nversion = \"1.0\"\nrequire_exact = maybe\n",
             "not true or false");
  // A file that parses but pins nothing would silently check nothing.
  fails_with("schema_version = 1\n[notes]\ntext = \"hello\"\n", "nothing is pinned");
}

void TestVerdicts() {
  BeginCase("ComparePin verdicts");
  const Pin pin = ParseSample();
  CheckOptions options;

  std::vector<ComponentCheck> checks =
      rb_blitz::toolchain::ComparePin(pin, FrozenMachine(), options);
  CHECK_TRUE(rb_blitz::toolchain::AllMatched(checks));
  CHECK_FALSE(rb_blitz::toolchain::AnyFatal(checks));

  // A newer compiler than the frozen one is reported and builds: the frozen set is
  // what the last acceptance run was made with, not a wall.
  auto newer = FrozenMachine();
  newer["clang"] = {"23.2.0", "C:\\LLVM\\bin\\clang++.exe"};
  checks = rb_blitz::toolchain::ComparePin(pin, newer, options);
  const ComponentCheck* clang = FindCheck(checks, "clang");
  CHECK_TRUE(clang != nullptr);
  if (clang != nullptr) {
    CHECK_VERDICT(clang->verdict, Verdict::kDifferent);
    CHECK_FALSE(clang->fatal);
    CHECK_STR_EQ(clang->actual, "23.2.0");
    CHECK_CONTAINS(clang->detail, "23.1.1");
  }

  // Below the recorded minimum stops the build.
  auto old = FrozenMachine();
  old["clang"] = {"19.1.0", "C:\\LLVM\\bin\\clang++.exe"};
  checks = rb_blitz::toolchain::ComparePin(pin, old, options);
  clang = FindCheck(checks, "clang");
  CHECK_VERDICT(clang->verdict, Verdict::kTooOld);
  CHECK_TRUE(clang->fatal);
  CHECK_CONTAINS(clang->detail, "20");
  CHECK_TRUE(rb_blitz::toolchain::AnyFatal(checks));

  // The minimum is what decides "too old", not the frozen version: 20.1.8 is not
  // the frozen 23.1.1 and still builds.
  auto older_but_supported = FrozenMachine();
  older_but_supported["clang"] = {"20.1.8", "C:\\LLVM\\bin\\clang++.exe"};
  checks = rb_blitz::toolchain::ComparePin(pin, older_but_supported, options);
  CHECK_VERDICT(FindCheck(checks, "clang")->verdict, Verdict::kDifferent);
  CHECK_FALSE(FindCheck(checks, "clang")->fatal);

  // The SDK is a pin: another commit is fatal, there is no ordering to argue about.
  auto other_sdk = FrozenMachine();
  other_sdk["sdk"] = {"f5337cdc947ff6d4c4196737e2c807a48f2a1fc2", "nightly-20260901"};
  checks = rb_blitz::toolchain::ComparePin(pin, other_sdk, options);
  const ComponentCheck* sdk = FindCheck(checks, "rexglue-sdk");
  CHECK_TRUE(sdk != nullptr);
  if (sdk != nullptr) {
    CHECK_VERDICT(sdk->verdict, Verdict::kDifferent);
    CHECK_TRUE(sdk->fatal);
    // The report shortens a commit; the comparison does not.
    CHECK_CONTAINS(sdk->detail, "c94f5eb");
  }

  // A component the probe could not answer is reported, not fatal: a build that
  // cannot run the compiler has its own error to give.
  auto unanswered = FrozenMachine();
  unanswered["msvc"] = {"", "no MSVC toolset in the compiler's include roots"};
  checks = rb_blitz::toolchain::ComparePin(pin, unanswered, options);
  const ComponentCheck* msvc = FindCheck(checks, "MSVC toolset");
  CHECK_TRUE(msvc != nullptr);
  if (msvc != nullptr) {
    CHECK_VERDICT(msvc->verdict, Verdict::kUnknown);
    CHECK_FALSE(msvc->fatal);
    CHECK_CONTAINS(msvc->detail, "include roots");
  }
  CHECK_FALSE(rb_blitz::toolchain::AnyFatal(checks));

  // ... and a component the report does not mention at all reads the same way.
  auto missing = FrozenMachine();
  missing.erase("windows_sdk");
  checks = rb_blitz::toolchain::ComparePin(pin, missing, options);
  const ComponentCheck* unknown_sdk = FindCheck(checks, "Windows SDK");
  CHECK_TRUE(unknown_sdk != nullptr);
  if (unknown_sdk != nullptr) {
    CHECK_VERDICT(unknown_sdk->verdict, Verdict::kUnknown);
    CHECK_CONTAINS(unknown_sdk->detail, "not reported");
  }
}

void TestPolicies() {
  BeginCase("ComparePin policies");
  const Pin pin = ParseSample();

  auto old = FrozenMachine();
  old["cmake"] = {"3.20.0", "cmake.exe"};

  CheckOptions report_only;
  CHECK_TRUE(
      rb_blitz::toolchain::AnyFatal(rb_blitz::toolchain::ComparePin(pin, old, report_only)));

  CheckOptions relaxed;
  relaxed.allow_other_toolchain = true;
  CHECK_FALSE(
      rb_blitz::toolchain::AnyFatal(rb_blitz::toolchain::ComparePin(pin, old, relaxed)));

  // Strict turns every deviation fatal, including the ones that only warn.
  auto newer = FrozenMachine();
  newer["ninja"] = {"1.14.0", "ninja.exe"};
  CheckOptions strict;
  strict.strict = true;
  auto checks = rb_blitz::toolchain::ComparePin(pin, newer, strict);
  CHECK_TRUE(rb_blitz::toolchain::AnyFatal(checks));
  CHECK_VERDICT(FindCheck(checks, "Ninja")->verdict, Verdict::kDifferent);
  CHECK_TRUE(FindCheck(checks, "Ninja")->fatal);

  // -DRBBLITZ_ALLOW_OTHER_TOOLCHAIN=ON wins over strict: it is the "I know, build
  // it" switch.
  CheckOptions both = strict;
  both.allow_other_toolchain = true;
  CHECK_FALSE(
      rb_blitz::toolchain::AnyFatal(rb_blitz::toolchain::ComparePin(pin, newer, both)));
}

void TestFormatting() {
  BeginCase("Formatting");
  const Pin pin = ParseSample();
  const std::string pin_line = rb_blitz::toolchain::FormatPin(pin);
  CHECK_CONTAINS(pin_line, "rexglue-sdk c94f5eb");
  CHECK_CONTAINS(pin_line, "clang 23.1.1");
  CHECK_CONTAINS(pin_line, "MSVC toolset 14.44.35207");
  CHECK_CONTAINS(pin_line, "Windows SDK 10.0.26100.0");
  // A commit is shortened for reading; a version is not.
  CHECK_TRUE(pin_line.find("c94f5ebdcb3c9d1a460ca48e04f9758448f8d518") == std::string::npos);

  std::vector<ComponentCheck> checks =
      rb_blitz::toolchain::ComparePin(pin, FrozenMachine(), CheckOptions{});
  std::string stamp = rb_blitz::toolchain::FormatStamp(checks);
  CHECK_CONTAINS(stamp, "rexglue-sdk c94f5eb");
  CHECK_CONTAINS(stamp, "clang 23.1.1");
  CHECK_CONTAINS(stamp, "(frozen set)");

  auto newer = FrozenMachine();
  newer["ninja"] = {"1.14.0", "ninja.exe"};
  checks = rb_blitz::toolchain::ComparePin(pin, newer, CheckOptions{});
  stamp = rb_blitz::toolchain::FormatStamp(checks);
  CHECK_CONTAINS(stamp, "off the frozen set: Ninja 1.14.0");
  CHECK_TRUE(stamp.find("(frozen set)") == std::string::npos);

  const ComponentCheck* ninja = FindCheck(checks, "Ninja");
  CHECK_TRUE(ninja != nullptr);
  if (ninja != nullptr) {
    const std::string line = rb_blitz::toolchain::FormatCheckLine(*ninja);
    CHECK_CONTAINS(line, "different");
    CHECK_CONTAINS(line, "1.14.0");
    CHECK_CONTAINS(line, "frozen 1.13.2");
  }

  // A matched line carries what the machine said about itself, not a reason.
  checks = rb_blitz::toolchain::ComparePin(pin, FrozenMachine(), CheckOptions{});
  const std::string clang_line = rb_blitz::toolchain::FormatCheckLine(*FindCheck(checks, "clang"));
  CHECK_CONTAINS(clang_line, "C:\\Program Files\\LLVM\\bin\\clang++.exe");

  // A component that could not be measured is *not* the frozen set: the stamp used to
  // skip it and then call the remainder frozen, which is how a build with an
  // unreadable compiler reported itself as the frozen toolchain.
  auto unanswered = FrozenMachine();
  unanswered["clang"] = {"", "no clang version in what clang++ printed"};
  checks = rb_blitz::toolchain::ComparePin(pin, unanswered, CheckOptions{});
  stamp = rb_blitz::toolchain::FormatStamp(checks);
  CHECK_CONTAINS(stamp, "clang unknown");
  CHECK_CONTAINS(stamp, "off the frozen set: clang unknown");
  CHECK_TRUE(stamp.find("(frozen set)") == std::string::npos);
}

void TestEmitBuildHeader() {
  BeginCase("EmitBuildHeader");
  const Pin pin = ParseSample();
  std::vector<ComponentCheck> checks =
      rb_blitz::toolchain::ComparePin(pin, FrozenMachine(), CheckOptions{});

  std::string header;
  std::string error;
  CHECK_TRUE(
      rb_blitz::toolchain::EmitBuildHeader(pin, checks, "config/toolchain.toml", &header, &error));
  CHECK_CONTAINS(header, "#pragma once");
  CHECK_CONTAINS(header, "#define RBBLITZ_TOOLCHAIN_STAMP \"");
  CHECK_CONTAINS(header, "rexglue-sdk c94f5eb");
  CHECK_CONTAINS(header, "clang 23.1.1");
  CHECK_CONTAINS(header, "(frozen set)");
  // The frozen set is in the comment, so the header explains itself.
  CHECK_CONTAINS(header, "Frozen set (config/toolchain.toml)");

  // A component the machine got wrong is part of the stamp, so the log of a binary
  // built off the frozen set says so without anyone reading the build output.
  auto newer = FrozenMachine();
  newer["windows_sdk"] = {"10.0.26100.1", "from the compiler's include roots"};
  checks = rb_blitz::toolchain::ComparePin(pin, newer, CheckOptions{});
  CHECK_TRUE(
      rb_blitz::toolchain::EmitBuildHeader(pin, checks, "config/toolchain.toml", &header, &error));
  CHECK_CONTAINS(header, "off the frozen set: Windows SDK 10.0.26100.1");

  // The stamp is a C string literal: whatever a version string contains has to
  // survive the trip into the generated header.
  checks = rb_blitz::toolchain::ComparePin(pin, FrozenMachine(), CheckOptions{});
  ComponentCheck* clang = FindMutableCheck(checks, "clang");
  CHECK_TRUE(clang != nullptr);
  if (clang != nullptr) {
    clang->actual = "23.1.1\"quote\\slash";
  }
  CHECK_TRUE(
      rb_blitz::toolchain::EmitBuildHeader(pin, checks, "config/toolchain.toml", &header, &error));
  CHECK_CONTAINS(header, "clang 23.1.1\\\"quote\\\\slash");

  // Emitting a stamp for nothing is an error rather than an empty header.
  CHECK_FALSE(
      rb_blitz::toolchain::EmitBuildHeader(pin, {}, "config/toolchain.toml", &header, &error));
}

void TestRealPinFile(const fs::path& project_dir) {
  BeginCase("config/toolchain.toml");
  const fs::path path = project_dir / "config" / "toolchain.toml";
  Pin pin;
  std::string error;
  if (!rb_blitz::toolchain::LoadPin(path, &pin, &error)) {
    // A missing file is not this suite's business - the build gate is where that
    // fails, and the suite has to run without a configured checkout too.
    std::printf("         [ SKIP ] %s: %s\n", path.string().c_str(), error.c_str());
    return;
  }
  CHECK_EQ(pin.schema_version, rb_blitz::toolchain::kSupportedSchemaVersion);
  CHECK_EQ(pin.components.size(), 6);

  const PinnedComponent* sdk = FindComponent(pin, "sdk");
  const PinnedComponent* clang = FindComponent(pin, "clang");
  const PinnedComponent* cmake = FindComponent(pin, "cmake");
  CHECK_TRUE(sdk != nullptr);
  CHECK_TRUE(clang != nullptr);
  CHECK_TRUE(cmake != nullptr);
  if (sdk != nullptr) {
    // The pin has to be a full commit: a prefix would be a comparison a short hash
    // could collide with.
    CHECK_EQ(sdk->expected.size(), 40);
    CHECK_TRUE(sdk->kind == Kind::kCommit);
  }
  if (clang != nullptr) {
    // The minimums the project already assumes: the clang the linux presets name...
    CHECK_STR_EQ(clang->minimum, "20");
  }
  if (cmake != nullptr) {
    // ... and CMake's own cmake_minimum_required.
    CHECK_STR_EQ(cmake->minimum, "3.25");
  }

  // The real file has to describe every component the tool probes, and with a value
  // to compare, or a build would silently stop checking one of them.
  for (const char* key : kExpectedOrder) {
    const PinnedComponent* component = FindComponent(pin, key);
    CHECK_TRUE(component != nullptr);
    if (component != nullptr) {
      CHECK_FALSE(component->expected.empty());
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  fs::path project_dir = kDefaultProjectDir;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--project-dir" && i + 1 < argc) {
      project_dir = argv[++i];
    }
  }

  std::printf("project dir: %s\n", project_dir.string().c_str());

  TestCompareVersions();
  TestParsePin();
  TestParseTolerates();
  TestParseErrors();
  TestVerdicts();
  TestPolicies();
  TestFormatting();
  TestEmitBuildHeader();
  TestRealPinFile(project_dir);

  return rb_blitz::test::Finish();
}
