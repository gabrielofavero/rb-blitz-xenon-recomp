// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9's restart half (src/platform/game_restart.h), tested without the game:
//
//   * the handoff token: the fixed shape, the bound, and every refusal (a wrong version, a
//     truncated token, a zero pid or nonce, an unknown target, a token from the future);
//   * the command line: the quoting rule the launcher established (and the app's Play used), and the allowlist rebuild -
//     what survives a restart, and the one-shot arguments that must not;
//   * the state machine: exactly-once, the stale revision, the "nothing saved yet" case, a child
//     creation failure that leaves the parent running, and a second request that is refused;
//   * the process-level legs, which are the point of this pass and run for real: a child that
//     waits for its parent's *verified identity*, the identity-mismatch case, the timeout that
//     refuses to start beside a living parent, and a failed spawn.
//
// The test executable doubles as the child: argv[0] is what the restart coordinator spawns, so the process
// legs need no fixture binary and no shell.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "check.h"
#include "platform/game_restart.h"

// The check helpers live in rb_blitz::test; the short alias keeps the case bodies readable.
namespace test = rb_blitz::test;

namespace {

using namespace rb_blitz::platform;

// The test executable's own path, set in main: every process leg spawns *this* binary, so the
// checkpoint needs no fixture and no shell.
std::string g_self_path;
const std::string& SelfPath() { return g_self_path; }

// ------------------------------------------------------------------------------------------------
// The child side (this executable, re-invoked)
// ------------------------------------------------------------------------------------------------

std::filesystem::path TempDir() {
  std::filesystem::path dir = std::filesystem::temp_directory_path() / "rb_blitz_restart_tests";
  std::error_code code;
  std::filesystem::create_directories(dir, code);
  return dir;
}

std::string Number(uint64_t value) { return std::to_string(value); }

// The whole result of a bootstrap run, written where the parent can read it.
struct ChildReport {
  std::string action = "none";
  size_t arguments = 0;
  bool kept_refresh = false;
  bool kept_handoff = false;
  long long elapsed_ms = 0;
  std::string error;
};

std::string RenderReport(const ChildReport& report) {
  std::string out = "action=" + report.action + "\n";
  out += "arguments=" + std::to_string(report.arguments) + "\n";
  out += std::string("kept_refresh=") + (report.kept_refresh ? "1" : "0") + "\n";
  out += std::string("kept_handoff=") + (report.kept_handoff ? "1" : "0") + "\n";
  out += "elapsed_ms=" + std::to_string(report.elapsed_ms) + "\n";
  out += "error=" + report.error + "\n";
  return out;
}

ChildReport ParseReport(const std::string& text) {
  ChildReport report;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find('\n', start);
    const std::string line = text.substr(start, end == std::string::npos ? std::string::npos
                                                                        : end - start);
    const std::size_t equals = line.find('=');
    if (equals != std::string::npos) {
      const std::string key = line.substr(0, equals);
      const std::string value = line.substr(equals + 1);
      if (key == "action") {
        report.action = value;
      } else if (key == "arguments") {
        report.arguments = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
      } else if (key == "kept_refresh") {
        report.kept_refresh = value == "1";
      } else if (key == "kept_handoff") {
        report.kept_handoff = value == "1";
      } else if (key == "elapsed_ms") {
        report.elapsed_ms = std::strtoll(value.c_str(), nullptr, 10);
      } else if (key == "error") {
        report.error = value;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return report;
}

// `--child-bootstrap <pid> <created> <timeout_ms> <out>`: build the token a real restart would
// have handed us, run the bootstrap, and write down what it decided. The argument list it is given
// deliberately contains the transient cache refresh, so the report can prove it was dropped.
int RunChildBootstrap(int argc, char** argv) {
  if (argc < 6) {
    return 2;
  }
  const uint32_t pid = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
  const uint64_t created = std::strtoull(argv[3], nullptr, 10);
  const uint32_t timeout = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
  const std::filesystem::path out = argv[5];

  HandoffToken token;
  token.parent_pid = pid;
  token.parent_created = created;
  token.nonce = 0x1234;
  token.target = "ultimate";
  const std::string handoff = std::string(kHandoffSwitch) + EncodeHandoff(token);

  std::vector<std::string> storage = {argv[0], handoff, "--refresh_dlc_cache",
                                      "--enhancements_dlc_cache=true"};
  std::vector<const char*> child_argv;
  for (const std::string& text : storage) {
    child_argv.push_back(text.c_str());
  }

  const auto began = std::chrono::steady_clock::now();
  const BootstrapResult result =
      RunBootstrap(static_cast<int>(child_argv.size()), child_argv.data(), nullptr, timeout);
  const auto ended = std::chrono::steady_clock::now();

  ChildReport report;
  report.action = result.action == BootstrapAction::kStartGame ? "start" : "none";
  report.arguments = result.arguments.size();
  report.elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(ended - began).count();
  report.error = result.error;
  for (const std::string& argument : result.arguments) {
    if (std::string_view(argument).find("refresh_dlc_cache") != std::string_view::npos) {
      report.kept_refresh = true;
    }
    if (IsHandoffSwitch(argument)) {
      report.kept_handoff = true;
    }
  }

  std::ofstream file(out, std::ios::binary | std::ios::trunc);
  file << RenderReport(report);
  return 0;
}

// The child modes the process legs use: sleep, exit now, or bootstrap.
int RunChild(int argc, char** argv) {
  const std::string_view mode = argv[1];
  if (mode == "--child-sleep") {
    const unsigned long long ms = std::strtoull(argv[2], nullptr, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return 0;
  }
  if (mode == "--child-exit-code") {
    return static_cast<int>(std::strtol(argv[2], nullptr, 10));
  }
  if (mode == "--child-bootstrap") {
    return RunChildBootstrap(argc, argv);
  }
  return 3;
}

// ------------------------------------------------------------------------------------------------
// Doubles
// ------------------------------------------------------------------------------------------------

class FakeLauncher : public ChildLauncher {
 public:
  bool Spawn(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
             const std::filesystem::path& working_directory, std::string* error) override {
    ++spawns;
    last_executable = executable;
    last_arguments = arguments;
    last_working_directory = working_directory;
    if (!succeed) {
      *error = failure;
      return false;
    }
    return true;
  }

  bool WaitForExit(uint32_t, std::string* error) override {
    (void)error;
    return wait_succeeds;
  }

  uint32_t ChildPid() const override { return pid; }

  bool succeed = true;
  bool wait_succeeds = true;
  std::string failure = "the spawn failed";
  int spawns = 0;
  uint32_t pid = 4242;
  std::filesystem::path last_executable;
  std::vector<std::string> last_arguments;
  std::filesystem::path last_working_directory;
};

class FakeWaiter : public ParentWaiter {
 public:
  explicit FakeWaiter(WaitResult result) : result_(result) {}

  WaitResult WaitForExit(uint32_t pid, uint64_t created, uint32_t timeout_ms,
                         std::string* error) override {
    calls += 1;
    saw_pid = pid;
    saw_created = created;
    saw_timeout = timeout_ms;
    if (result_ == WaitResult::kTimeout) {
      *error = "the parent did not exit within the timeout";
    } else if (result_ == WaitResult::kFailed) {
      *error = "the wait on the parent failed";
    } else if (result_ == WaitResult::kIdentityMismatch) {
      *error = "the pid now belongs to a different process";
    }
    return result_;
  }

  int calls = 0;
  uint32_t saw_pid = 0;
  uint64_t saw_created = 0;
  uint32_t saw_timeout = 0;

 private:
  WaitResult result_;
};

// ------------------------------------------------------------------------------------------------
// The cases
// ------------------------------------------------------------------------------------------------

void HandoffRoundTrips() {
  test::BeginCase("handoff: the token round-trips and is bounded");

  HandoffToken token;
  token.parent_pid = 0x1F2E;
  token.parent_created = 0x1D9C4A3B2C1D0E0Full;
  token.nonce = 0xFEDCBA9876543210ull;
  token.target = "ultimate";

  const std::string encoded = EncodeHandoff(token);
  CHECK_TRUE(encoded.size() <= kMaxHandoffChars);
  CHECK_EQ(encoded.find(' '), std::string::npos);

  HandoffToken decoded;
  std::string error;
  CHECK_TRUE(DecodeHandoff(encoded, &decoded, &error));
  CHECK_EQ(decoded.parent_pid, token.parent_pid);
  CHECK_EQ(decoded.parent_created, token.parent_created);
  CHECK_EQ(decoded.nonce, token.nonce);
  CHECK_TRUE(decoded.target == "ultimate");
  CHECK_EQ(decoded.version, kHandoffVersion);
  // Re-encoding a decoded token is stable: the shape has one spelling.
  CHECK_TRUE(EncodeHandoff(decoded) == encoded);

  for (const std::string_view target : {"common", "demo", "ultimate"}) {
    HandoffToken other;
    other.parent_pid = 1;
    other.parent_created = 2;
    other.nonce = 3;
    other.target = std::string(target);
    HandoffToken again;
    CHECK_TRUE(DecodeHandoff(EncodeHandoff(other), &again, &error));
    CHECK_TRUE(again.target == target);
  }
}

void HandoffRefusals() {
  test::BeginCase("handoff: every malformed token is refused, never half-applied");

  const struct {
    const char* text;
    const char* why;
  } cases[] = {
      {"", "empty"},
      {"v1.1.2.3", "four fields"},
      {"v1.1.2.3.4.5", "six fields"},
      {"v2.1.2.3.common", "a version from a different build"},
      {"v1.1.2.zz.common", "a non-hex field"},
      {"v1.0.2.3.common", "no parent process"},
      {"v1.1.2.0.common", "no request identity"},
      {"v1.1.2.3.platinum", "an unknown target"},
      {"v1.1.2.3.", "an empty target"},
      {"v1.-1.2.3.common", "a negative field"},
      {"v1.11111111111111111.2.3.common", "a field over the width"},
  };
  for (const auto& item : cases) {
    HandoffToken token;
    std::string error;
    CHECK_FALSE(DecodeHandoff(item.text, &token, &error));
    CHECK_FALSE(error.empty());
    // The refusal must not leave a usable token behind.
    CHECK_EQ(token.nonce, 0);
  }

  // The bound is a bound: 96 characters is fine, more is not - even if it is otherwise shaped.
  std::string over(kMaxHandoffChars + 1, 'a');
  HandoffToken token;
  std::string error;
  CHECK_FALSE(DecodeHandoff(over, &token, &error));

  // Uppercase hex is not what this build writes, so it is not what it accepts.
  CHECK_FALSE(DecodeHandoff("v1.1F.2.3.common", &token, &error));
}

void HandoffFromCommandLine() {
  test::BeginCase("handoff: the switch is found on a command line, and a bad one is fatal");

  HandoffToken token;
  token.parent_pid = 7;
  token.parent_created = 9;
  token.nonce = 11;
  token.target = "demo";
  const std::string good = std::string(kHandoffSwitch) + EncodeHandoff(token);

  const std::string a0 = "rb_blitz.exe";
  const std::string a1 = "--game_data_root=D:\\Games";
  std::vector<const char*> argv = {a0.c_str(), a1.c_str(), good.c_str()};
  std::string error;
  const std::optional<HandoffToken> found =
      HandoffFromArguments(static_cast<int>(argv.size()), argv.data(), &error);
  CHECK_TRUE(found.has_value());
  if (found.has_value()) {
    CHECK_EQ(found->parent_pid, 7u);
    CHECK_TRUE(found->target == "demo");
  }
  CHECK_TRUE(error.empty());

  // A plain boot has no handoff, which is not an error.
  std::vector<const char*> plain = {a0.c_str(), a1.c_str()};
  CHECK_FALSE(HandoffFromArguments(static_cast<int>(plain.size()), plain.data(), &error)
                  .has_value());
  CHECK_TRUE(error.empty());

  // The switch with a bad token is refused loudly: ignoring it could start a second game.
  const std::string bad = std::string(kHandoffSwitch) + "v1.1.2.3.platinum";
  std::vector<const char*> broken = {a0.c_str(), bad.c_str()};
  CHECK_FALSE(
      HandoffFromArguments(static_cast<int>(broken.size()), broken.data(), &error).has_value());
  CHECK_FALSE(error.empty());
  CHECK_TRUE(IsHandoffSwitch(bad));
  CHECK_FALSE(IsHandoffSwitch("--game_data_root=D:\\Games"));
}

void QuotingFollowsTheWindowsRule() {
  test::BeginCase("command line: quoting is the rule the settings app already uses");

  CHECK_TRUE(QuoteArgument("plain") == "\"plain\"");
  CHECK_TRUE(QuoteArgument("with space") == "\"with space\"");
  CHECK_TRUE(QuoteArgument("") == "\"\"");
  // A trailing backslash must be doubled, or the closing quote would be escaped.
  CHECK_TRUE(QuoteArgument("C:\\dir\\") == "\"C:\\dir\\\\\"");
  // A quote is escaped, and the backslash run before it is doubled.
  CHECK_TRUE(QuoteArgument("a\"b") == "\"a\\\"b\"");
  CHECK_TRUE(QuoteArgument("C:\\a\\\"b") == "\"C:\\a\\\\\\\"b\"");
  // Non-ASCII survives as bytes (the wide conversion is the platform half's job).
  CHECK_TRUE(QuoteArgument("M\u00fasica") == "\"M\u00fasica\"");

  const std::string line = BuildCommandLine("C:\\Program Files\\rb_blitz.exe",
                                            {"--game_data_root=D:\\Games\\Blitz",
                                             "--log_file=D:\\My Logs\\boot.log"});
  CHECK_TRUE(line == "\"C:\\Program Files\\rb_blitz.exe\" \"--game_data_root=D:\\Games\\Blitz\" "
                     "\"--log_file=D:\\My Logs\\boot.log\"");

}

void ChildArgumentsAreAnAllowlist() {
  test::BeginCase("command line: the child gets an allowlist, never a replay");

  HandoffToken token;
  token.parent_pid = 3;
  token.parent_created = 5;
  token.nonce = 7;
  token.target = "ultimate";

  LaunchContext context;
  context.executable = "C:\\Program Files\\rb_blitz.exe";
  context.game_data_root = "D:\\Games\\Blitz";
  context.working_directory = "C:\\Program Files";
  context.profile_file = "C:\\Users\\me\\AppData\\blitz\\launcher.toml";
  context.target = "ultimate";
  context.developer_arguments = {"--log_level=debug", "--refresh_dlc_cache",
                                 std::string(kHandoffSwitch) + "v1.1.2.3.common"};

  const std::vector<std::string> arguments = BuildChildArguments(context, token);
  CHECK_EQ(arguments.size(), 4);
  CHECK_TRUE(arguments[0] == "--game_data_root=D:\\Games\\Blitz");
  CHECK_TRUE(arguments[1] == "--launcher_profile=C:\\Users\\me\\AppData\\blitz\\launcher.toml");
  CHECK_TRUE(arguments[2] == "--log_level=debug");
  CHECK_TRUE(IsHandoffSwitch(arguments[3]));
  for (const auto& argument : arguments) {
    CHECK_TRUE(argument.find("refresh_dlc_cache") == std::string::npos);
    CHECK_TRUE(argument.find("ultimate_mode") == std::string::npos);
    CHECK_TRUE(argument.find("license_mask") == std::string::npos);
  }
  // SDK string flags refuse duplicates. Old identity must never be replayed alongside the new.
  context.developer_arguments = {"--game_data_root=old", "--launcher_profile", "old.toml",
                                 "--log_file=D:\\My Logs\\boot.log", "--resolution_scale=2"};
  const auto rebuilt = BuildChildArguments(context, token);
  CHECK_EQ(rebuilt.size(), 5);
  CHECK_TRUE(rebuilt[2] == "--log_file=D:\\My Logs\\boot.log");
  CHECK_TRUE(rebuilt[3] == "--resolution_scale=2");
  CHECK_TRUE(IsRetiredArgument("--refresh_dlc_cache"));
  CHECK_TRUE(IsRetiredArgument("--enhancements_dlc_cache=true"));
  CHECK_FALSE(IsRetiredArgument("--resolution_scale=2"));

}

void ArgumentRetirement() {
  test::BeginCase("command line: the one-shot policy");
  // A previous handoff is consumed by the boot that received it; rebuilding it would make the
  // child wait for a parent that is already gone by design.
  CHECK_TRUE(IsRetiredArgument("--restart-handoff=v1.1.2.3.common"));
  CHECK_TRUE(IsRetiredArgument("--restart-bootstrap"));
  CHECK_FALSE(IsRetiredArgument("--restart-handoffs=1"));  // not the switch: no prefix match
  CHECK_FALSE(IsRetiredArgument("--resolution_scale=2"));  // an explicit override is preserved
}

void CoordinatorIsExactlyOnce() {
  test::BeginCase("coordinator: one child per boot, and the failures keep the game running");

  FakeLauncher child;
  RestartCoordinator coordinator(&child);

  LaunchContext context;
  context.executable = "C:\\Program Files\\rb_blitz.exe";
  context.game_data_root = "D:\\Games\\Blitz";
  context.target = "ultimate";

  // A revision nobody saved yet is not a restart: there is nothing to boot into.
  RestartOutcome outcome = coordinator.Request(0, 0, 1, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kNotSaved));
  CHECK_EQ(child.spawns, 0);

  // A stale revision is refused, and nothing is started.
  outcome = coordinator.Request(3, 4, 1, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kStaleRevision));
  CHECK_EQ(child.spawns, 0);

  // The real request: one child, the handshake phase, the rebuilt command line.
  outcome = coordinator.Request(4, 4, 9, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kOk));
  CHECK_EQ(static_cast<int>(outcome.phase), static_cast<int>(RestartPhase::kReady));
  CHECK_TRUE(coordinator.CloseRequested());
  CHECK_EQ(coordinator.spawned(), 1);
  CHECK_EQ(child.spawns, 1);
  CHECK_TRUE(child.last_executable == context.executable);
  CHECK_TRUE(child.last_arguments.back().find(kHandoffSwitch) != std::string::npos);
  CHECK_TRUE(coordinator.command_line().find(".ultimate") != std::string::npos);
  CHECK_EQ(coordinator.token().nonce, 9u);
  CHECK_TRUE(coordinator.token().target == "ultimate");

  // A second request - a held button, a redrawn panel - is refused and starts nothing.
  outcome = coordinator.Request(4, 4, 9, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kAlreadyRequested));
  CHECK_EQ(child.spawns, 1);
  CHECK_EQ(coordinator.spawned(), 1);

  // A request id of zero (what a DTA script sends) still yields a non-zero identity, so the
  // handoff is still one-shot.
  FakeLauncher second;
  RestartCoordinator other(&second);
  outcome = other.Request(1, 1, 0, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kOk));
  CHECK_TRUE(other.token().nonce != 0);
}

void CoordinatedSpawnFailureKeepsTheParent() {
  test::BeginCase("coordinator: a failed spawn keeps the game running and reports it");

  FakeLauncher child;
  child.succeed = false;
  child.failure = "CreateProcess failed with error 2";
  RestartCoordinator coordinator(&child);

  LaunchContext context;
  context.executable = "C:\\missing\\rb_blitz.exe";
  const RestartOutcome outcome = coordinator.Request(1, 1, 5, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kChildFailed));
  CHECK_EQ(static_cast<int>(outcome.phase), static_cast<int>(RestartPhase::kFailed));
  CHECK_EQ(coordinator.CloseRequested(), false);
  CHECK_EQ(coordinator.spawned(), 0);
  CHECK_TRUE(coordinator.error().find("error 2") != std::string::npos);

  // A failed attempt may be retried; a successful handshake never is.
  child.succeed = true;
  coordinator.ResetAfterFailure();
  const RestartOutcome retry = coordinator.Request(1, 1, 6, context);
  CHECK_EQ(static_cast<int>(retry.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kOk));
  CHECK_EQ(coordinator.spawned(), 1);
  coordinator.ResetAfterFailure();
  CHECK_EQ(coordinator.spawned(), 1);
  CHECK_TRUE(coordinator.CloseRequested());
}

void CoordinatorWithoutALauncher() {
  test::BeginCase("coordinator: no child in this build is a reported failure");

  RestartCoordinator coordinator(nullptr);
  LaunchContext context;
  context.executable = "rb_blitz.exe";
  const RestartOutcome outcome = coordinator.Request(1, 1, 1, context);
  CHECK_EQ(static_cast<int>(outcome.status),
           static_cast<int>(rb_blitz::settings::RestartStatus::kChildFailed));
  CHECK_FALSE(outcome.error.empty());
}

void BootstrapWithoutAHandoff() {
  test::BeginCase("bootstrap: an ordinary boot is untouched");

  const std::string a0 = "rb_blitz.exe";
  const std::string a1 = "--game_data_root=D:\\Games";
  std::vector<const char*> argv = {a0.c_str(), a1.c_str()};

  FakeWaiter waiter(FakeWaiter::WaitResult::kExited);
  const BootstrapResult result =
      RunBootstrap(static_cast<int>(argv.size()), argv.data(), &waiter, 1000);
  CHECK_EQ(static_cast<int>(result.action), static_cast<int>(BootstrapAction::kNone));
  CHECK_EQ(waiter.calls, 0);
  CHECK_TRUE(result.error.empty());
}

void BootstrapWaitsForTheParent() {
  test::BeginCase("bootstrap: it waits for the parent's verified identity, then starts once");

  HandoffToken token;
  token.parent_pid = 1234;
  token.parent_created = 555;
  token.nonce = 66;
  const std::string handoff = std::string(kHandoffSwitch) + EncodeHandoff(token);
  const std::string a0 = "rb_blitz.exe";
  const std::string a1 = "--refresh_dlc_cache";
  std::vector<const char*> argv = {a0.c_str(), handoff.c_str(), a1.c_str()};

  // The parent exits: start, with the one-shot argument dropped.
  {
    FakeWaiter waiter(FakeWaiter::WaitResult::kExited);
    const BootstrapResult result =
        RunBootstrap(static_cast<int>(argv.size()), argv.data(), &waiter, 30000);
    CHECK_EQ(static_cast<int>(result.action), static_cast<int>(BootstrapAction::kStartGame));
    CHECK_EQ(waiter.calls, 1);
    CHECK_EQ(waiter.saw_pid, 1234u);
    CHECK_EQ(waiter.saw_created, 555u);
    CHECK_EQ(result.arguments.size(), 1);  // the exe and nothing else
    CHECK_TRUE(result.arguments[0] == a0);
    for (const std::string& argument : result.arguments) {
      CHECK_FALSE(argument.find("refresh_dlc_cache") != std::string::npos);
      CHECK_FALSE(IsHandoffSwitch(argument));
    }
  }

  // A parent that was already gone is a start too.
  {
    FakeWaiter waiter(FakeWaiter::WaitResult::kAlreadyGone);
    const BootstrapResult result =
        RunBootstrap(static_cast<int>(argv.size()), argv.data(), &waiter, 30000);
    CHECK_EQ(static_cast<int>(result.action), static_cast<int>(BootstrapAction::kStartGame));
  }

  // A reused pid: the process we were told to wait for no longer exists, so starting is right.
  {
    FakeWaiter waiter(FakeWaiter::WaitResult::kIdentityMismatch);
    const BootstrapResult result =
        RunBootstrap(static_cast<int>(argv.size()), argv.data(), &waiter, 30000);
    CHECK_EQ(static_cast<int>(result.action), static_cast<int>(BootstrapAction::kStartGame));
    CHECK_TRUE(result.error.find("different process") != std::string::npos);
  }

  // A living parent and no time to wait: refuse. Two titles writing the same saves is the one
  // outcome §8 forbids.
  {
    FakeWaiter waiter(FakeWaiter::WaitResult::kTimeout);
    const BootstrapResult result =
        RunBootstrap(static_cast<int>(argv.size()), argv.data(), &waiter, 30000);
    CHECK_EQ(static_cast<int>(result.action), static_cast<int>(BootstrapAction::kNone));
    CHECK_TRUE(result.error.find("timeout") != std::string::npos);
  }
  {
    FakeWaiter waiter(FakeWaiter::WaitResult::kFailed);
    const BootstrapResult result =
        RunBootstrap(static_cast<int>(argv.size()), argv.data(), &waiter, 30000);
    CHECK_EQ(static_cast<int>(result.action), static_cast<int>(BootstrapAction::kNone));
    CHECK_FALSE(result.error.empty());
  }
}

void RealSpawnFailsForAMissingExecutable() {
  test::BeginCase("process: a spawn of something that is not there fails cleanly");

  std::unique_ptr<ChildLauncher> child = CreateChildLauncher();
  CHECK_TRUE(child != nullptr);
  if (child == nullptr) {
    return;
  }
  std::string error;
  const bool started = child->Spawn("Z:\\this\\does\\not\\exist\\rb_blitz.exe", {},
                                       TempDir(), &error);
  CHECK_FALSE(started);
  CHECK_FALSE(error.empty());
  CHECK_EQ(child->ChildPid(), 0u);
}

void RealChildRunsAndExits() {
  test::BeginCase("process: a real child is started and waited for");

  std::unique_ptr<ChildLauncher> child = CreateChildLauncher();
  CHECK_TRUE(child != nullptr);
  if (child == nullptr) {
    return;
  }
  const std::string self = SelfPath();
  std::string error;
  CHECK_TRUE(child->Spawn(self, {"--child-exit-code", "7"}, TempDir(), &error));
  CHECK_TRUE(child->ChildPid() != 0);
  CHECK_TRUE(child->WaitForExit(30000, &error));
  CHECK_TRUE(error.empty());
}

void RealProcessBootstrapWait() {
  test::BeginCase("process: the child really waits for the parent to exit, then starts");

  std::unique_ptr<ChildLauncher> parent = CreateChildLauncher();
  std::unique_ptr<ChildLauncher> child = CreateChildLauncher();
  CHECK_TRUE(parent != nullptr && child != nullptr);
  if (parent == nullptr || child == nullptr) {
    return;
  }
  const std::filesystem::path dir = TempDir();
  const std::filesystem::path out = dir / "bootstrap-waited.txt";
  std::error_code code;
  std::filesystem::remove(out, code);

  const std::string self = SelfPath();
  std::string error;
  // The "parent" lives ~1.2 s: long enough that the child must actually wait for it.
  CHECK_TRUE(parent->Spawn(self, {"--child-sleep", "1200"}, dir, &error));
  const uint32_t parent_pid = parent->ChildPid();
  CHECK_TRUE(parent_pid != 0);
  uint64_t created = 0;
  CHECK_TRUE(ProcessCreatedTicks(parent_pid, &created));
  CHECK_EQ(created == 0, false);

  CHECK_TRUE(child->Spawn(self,
                          {"--child-bootstrap", Number(parent_pid), Number(created), "15000",
                           out.string()},
                          dir, &error));
  // While the parent is alive the child must not have started the game.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK_FALSE(std::filesystem::exists(out));

  CHECK_TRUE(child->WaitForExit(30000, &error));
  CHECK_TRUE(parent->WaitForExit(30000, &error));
  CHECK_TRUE(std::filesystem::exists(out));

  std::ifstream file(out, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const ChildReport report = ParseReport(text);
  CHECK_TRUE(report.action == "start");
  CHECK_TRUE(report.error.empty());
  // It waited: the parent's sleep is 1200 ms and the child cannot have finished before it.
  CHECK_EQ(report.elapsed_ms >= 900, true);
  // The one-shot and retired cache switches did not survive the bootstrap.
  CHECK_EQ(report.kept_refresh, false);
  CHECK_EQ(report.kept_handoff, false);
  CHECK_EQ(report.arguments, 1);  // only the exe remains
}

void RealProcessIdentityMismatchStartsImmediately() {
  test::BeginCase("process: a reused pid does not hold the child hostage");

  std::unique_ptr<ChildLauncher> parent = CreateChildLauncher();
  std::unique_ptr<ChildLauncher> child = CreateChildLauncher();
  CHECK_TRUE(parent != nullptr && child != nullptr);
  if (parent == nullptr || child == nullptr) {
    return;
  }
  const std::filesystem::path dir = TempDir();
  const std::filesystem::path out = dir / "bootstrap-mismatch.txt";
  std::error_code code;
  std::filesystem::remove(out, code);

  const std::string self = SelfPath();
  std::string error;
  CHECK_TRUE(parent->Spawn(self, {"--child-sleep", "1500"}, dir, &error));
  const uint32_t parent_pid = parent->ChildPid();
  uint64_t created = 0;
  CHECK_TRUE(ProcessCreatedTicks(parent_pid, &created));

  // The token names a creation time that is not this process's: the identity does not match, so the
  // wait must not happen - the process the token was made for is gone.
  CHECK_TRUE(child->Spawn(self,
                          {"--child-bootstrap", Number(parent_pid), Number(created + 1), "15000",
                           out.string()},
                          dir, &error));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  CHECK_TRUE(std::filesystem::exists(out));  // it decided at once, while the parent still lives

  std::ifstream file(out, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const ChildReport report = ParseReport(text);
  CHECK_TRUE(report.action == "start");
  CHECK_EQ(report.elapsed_ms < 500, true);
  CHECK_TRUE(report.error.find("different process") != std::string::npos);

  CHECK_TRUE(child->WaitForExit(30000, &error));
  CHECK_TRUE(parent->WaitForExit(30000, &error));
}

void RealProcessTimeoutRefusesToStart() {
  test::BeginCase("process: a living parent past the timeout is not started beside");

  std::unique_ptr<ChildLauncher> parent = CreateChildLauncher();
  std::unique_ptr<ChildLauncher> child = CreateChildLauncher();
  CHECK_TRUE(parent != nullptr && child != nullptr);
  if (parent == nullptr || child == nullptr) {
    return;
  }
  const std::filesystem::path dir = TempDir();
  const std::filesystem::path out = dir / "bootstrap-timeout.txt";
  std::error_code code;
  std::filesystem::remove(out, code);

  const std::string self = SelfPath();
  std::string error;
  CHECK_TRUE(parent->Spawn(self, {"--child-sleep", "2500"}, dir, &error));
  const uint32_t parent_pid = parent->ChildPid();
  uint64_t created = 0;
  CHECK_TRUE(ProcessCreatedTicks(parent_pid, &created));

  CHECK_TRUE(child->Spawn(self,
                          {"--child-bootstrap", Number(parent_pid), Number(created), "400",
                           out.string()},
                          dir, &error));
  CHECK_TRUE(child->WaitForExit(30000, &error));

  std::ifstream file(out, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const ChildReport report = ParseReport(text);
  CHECK_TRUE(report.action == "none");
  CHECK_TRUE(report.error.find("timeout") != std::string::npos);

  CHECK_TRUE(parent->WaitForExit(30000, &error));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]).substr(0, 7) == "--child") {
    return RunChild(argc, argv);
  }
  g_self_path = argc > 0 ? argv[0] : "rb_blitz_game_restart_tests.exe";

  HandoffRoundTrips();
  HandoffRefusals();
  HandoffFromCommandLine();
  QuotingFollowsTheWindowsRule();
  ChildArgumentsAreAnAllowlist();
  ArgumentRetirement();
  CoordinatorIsExactlyOnce();
  CoordinatedSpawnFailureKeepsTheParent();
  CoordinatorWithoutALauncher();
  BootstrapWithoutAHandoff();
  BootstrapWaitsForTheParent();
  RealSpawnFailsForAMissingExecutable();
  RealChildRunsAndExits();
  RealProcessBootstrapWait();
  RealProcessIdentityMismatchStartsImmediately();
  RealProcessTimeoutRefusesToStart();
  return test::Finish();
}
