// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9 - the restart coordinator and its bootstrap half (parent plan §8). See
// src/platform/game_restart.h for the design rules; this file is their implementation, split into
// three parts:
//
//   1. the handoff token  - a fixed-shape, bounded, space-free string of numbers plus the target;
//   2. the command line   - an allowlist rebuild, never a replay of the old line;
//   3. the process half   - `CreateProcessW` / `WaitForSingleObject`, and the bootstrap's
//                           verified-identity wait.
//
// Everything in parts 1 and 2 is platform-independent and is what the tests spend most of their
// checks on; part 3 is guarded, so a non-Windows build still compiles the state machine.

#include "platform/game_restart.h"

#include <array>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rb_blitz::platform {
namespace {

using rb_blitz::launcher::LaunchTarget;

// --- small text helpers ------------------------------------------------------------------------

std::string Hex(uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%llx", static_cast<unsigned long long>(value));
  return buffer;
}

// A hex field of 1..16 characters, no sign, no "0x". Strict on purpose: a token is either exactly
// the shape this build writes or it is refused.
bool ParseHex(std::string_view text, uint64_t* out) {
  if (text.empty() || text.size() > 16) {
    return false;
  }
  uint64_t value = 0;
  for (const char character : text) {
    uint32_t digit = 0;
    if (character >= '0' && character <= '9') {
      digit = static_cast<uint32_t>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = static_cast<uint32_t>(character - 'a' + 10);
    } else {
      return false;  // uppercase is not written by `Hex`, so it is not accepted either
    }
    value = (value << 4) | digit;
  }
  *out = value;
  return true;
}

std::vector<std::string_view> SplitFields(std::string_view text) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t dot = text.find('.', start);
    const size_t end = dot == std::string_view::npos ? text.size() : dot;
    fields.push_back(text.substr(start, end - start));
    if (dot == std::string_view::npos) {
      break;
    }
    start = dot + 1;
  }
  return fields;
}

// --- the target vocabulary ---------------------------------------------------------------------

struct TargetWord {
  LaunchTarget target;
  std::string_view token;
};

constexpr std::array<TargetWord, 3> kTargets{{
    {LaunchTarget::kCommon, "common"},
    {LaunchTarget::kUltimate, "ultimate"},
    {LaunchTarget::kDemo, "demo"},
}};

// --- the token argument on a command line -------------------------------------------------------

// The token text of an argument, when the argument is the handoff switch. Empty for anything else
// (which is not the same as "malformed": `IsHandoffSwitch` decides that). `out` may be null when
// only the yes/no answer is wanted.
bool HandoffTokenText(std::string_view argument, std::string_view* out) {
  constexpr std::string_view kSwitch = kHandoffSwitch;
  if (argument.size() <= kSwitch.size() || argument.substr(0, kSwitch.size()) != kSwitch) {
    return false;
  }
  if (out != nullptr) {
    *out = argument.substr(kSwitch.size());
  }
  return true;
}

#if defined(_WIN32)
std::wstring ToWide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        nullptr, 0);
  if (count <= 0) {
    return {};
  }
  std::wstring out(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
  return out;
}

uint64_t TicksOf(const FILETIME& time) {
  return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
#endif

}  // namespace

// ------------------------------------------------------------------------------------------------
// 1. The handoff token
// ------------------------------------------------------------------------------------------------

std::string_view TargetToken(LaunchTarget target) {
  for (const TargetWord& word : kTargets) {
    if (word.target == target) {
      return word.token;
    }
  }
  return "common";
}

bool TargetFromToken(std::string_view token, LaunchTarget* out) {
  for (const TargetWord& word : kTargets) {
    if (word.token == token) {
      if (out != nullptr) {
        *out = word.target;
      }
      return true;
    }
  }
  return false;
}

std::string EncodeHandoff(const HandoffToken& token) {
  std::string out = "v";
  out += Hex(token.version);
  out += '.';
  out += Hex(token.parent_pid);
  out += '.';
  out += Hex(token.parent_created);
  out += '.';
  out += Hex(token.nonce);
  out += '.';
  out += token.target.empty() ? std::string("common") : token.target;
  return out;
}

bool DecodeHandoff(std::string_view text, HandoffToken* out, std::string* error) {
  if (out == nullptr) {
    return false;
  }
  if (text.size() > kMaxHandoffChars) {
    *error = "the handoff token is longer than the bound";
    return false;
  }
  const std::vector<std::string_view> fields = SplitFields(text);
  if (fields.size() != 5 || fields[0].size() < 2 || fields[0].front() != 'v') {
    *error = "the handoff token is not the five-field v<version> shape";
    return false;
  }
  uint64_t version = 0;
  uint64_t pid = 0;
  uint64_t created = 0;
  uint64_t nonce = 0;
  if (!ParseHex(fields[0].substr(1), &version) || !ParseHex(fields[1], &pid) ||
      !ParseHex(fields[2], &created) || !ParseHex(fields[3], &nonce)) {
    *error = "a handoff field is not hexadecimal";
    return false;
  }
  if (version != kHandoffVersion) {
    *error = "the handoff token has a different version";
    return false;
  }
  if (pid == 0 || pid > 0xFFFFFFFFull) {
    *error = "the handoff token has no valid parent process";
    return false;
  }
  if (nonce == 0) {
    *error = "the handoff token has no request identity";
    return false;
  }
  if (!TargetFromToken(fields[4], nullptr)) {
    *error = "the handoff token names an unknown target";
    return false;
  }
  HandoffToken token;
  token.version = static_cast<uint32_t>(version);
  token.parent_pid = static_cast<uint32_t>(pid);
  token.parent_created = created;
  token.nonce = nonce;
  token.target = std::string(fields[4]);
  *out = token;
  return true;
}

bool IsHandoffSwitch(std::string_view argument) {
  return HandoffTokenText(argument, nullptr);
}

std::optional<HandoffToken> HandoffFromArguments(int argc, const char* const* argv,
                                                 std::string* error) {
  for (int index = 1; index < argc; ++index) {
    std::string_view text;
    if (!HandoffTokenText(argv[index], &text)) {
      continue;
    }
    HandoffToken token;
    std::string decode_error;
    if (!DecodeHandoff(text, &token, &decode_error)) {
      // A handoff switch whose token is unacceptable is *fatal* rather than ignored: ignoring it
      // would start a second game while the first is still running, which is the one outcome the
      // token exists to prevent.
      *error = decode_error;
      return std::nullopt;
    }
    return token;
  }
  return std::nullopt;
}

// ------------------------------------------------------------------------------------------------
// 2. The command line
// ------------------------------------------------------------------------------------------------

std::string QuoteArgument(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  std::size_t backslashes = 0;
  for (const char character : text) {
    if (character == '\\') {
      ++backslashes;
      continue;
    }
    if (character == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out.push_back('"');
    } else {
      out.append(backslashes, '\\');
      out.push_back(character);
    }
    backslashes = 0;
  }
  out.append(backslashes * 2, '\\');
  out.push_back('"');
  return out;
}

bool IsRetiredArgument(std::string_view argument) {
  if (IsHandoffSwitch(argument)) {
    return true;  // one-shot: the previous handoff is consumed by this boot
  }
  if (argument == kBootstrapSwitch) {
    return true;
  }
  // The DLC cache refresh means "scan the library once, now"; rebuilding it onto a restart would
  // re-run a scan the player never asked for twice (§8's one-shot policy).
  return argument.size() > 2 && argument.substr(0, 2) == "--" &&
         argument.substr(2).find("refresh_dlc_cache") != std::string_view::npos;
}

std::vector<std::string> BuildChildArguments(const LaunchContext& context,
                                             const HandoffToken& token) {
  std::vector<std::string> arguments;
  const auto utf8 = [](const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
  };
  if (!context.game_data_root.empty()) {
    arguments.push_back("--game_data_root=" + utf8(context.game_data_root));
  }
  // The target is what the boot is *for*, so it is passed explicitly rather than left to the file
  // alone: the file is what the menu wrote, and passing it removes any window in which an older
  // reader could pick up the previous value (§8's "carrying the newly persisted target").
  LaunchTarget target = LaunchTarget::kCommon;
  if (!TargetFromToken(context.target, &target)) {
    target = LaunchTarget::kCommon;
  }
  // The saved file is the target's source. Passing a generated setting as a CLI flag would
  // make it an external override on every subsequent boot, disabling the Target row.
  if (!context.launcher_profile.empty()) {
    arguments.push_back("--launcher_profile=" + utf8(context.launcher_profile));
  }
  for (size_t i = 0; i < context.developer_arguments.size(); ++i) {
    const std::string& argument = context.developer_arguments[i];
    // Identity switches are rebuilt once. CLI11 rejects duplicate string options.
    if (argument == "--game_data_root" || argument == "--launcher_profile") { ++i; continue; }
    if (argument.starts_with("--game_data_root=") || argument.starts_with("--launcher_profile=")) continue;
    if (IsRetiredArgument(argument)) {
      continue;
    }
    arguments.push_back(argument);
  }
  arguments.push_back(std::string(kHandoffSwitch) + EncodeHandoff(token));
  return arguments;
}

std::string BuildCommandLine(const std::filesystem::path& executable,
                             const std::vector<std::string>& arguments) {
  const auto bytes = executable.u8string();
  std::string out = QuoteArgument(std::string(bytes.begin(), bytes.end()));
  for (const std::string& argument : arguments) {
    out += ' ';
    out += QuoteArgument(argument);
  }
  return out;
}

// ------------------------------------------------------------------------------------------------
// 3. The process half
// ------------------------------------------------------------------------------------------------

uint32_t ThisProcessId() {
#if defined(_WIN32)
  return static_cast<uint32_t>(GetCurrentProcessId());
#else
  return 0;
#endif
}

uint64_t ThisProcessCreatedTicks() {
#if defined(_WIN32)
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
    return 0;
  }
  return TicksOf(created);
#else
  return 0;
#endif
}

bool ProcessCreatedTicks(uint32_t pid, uint64_t* out) {
  if (out == nullptr) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (handle == nullptr) {
    return false;
  }
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  const bool ok = GetProcessTimes(handle, &created, &exited, &kernel, &user) != 0;
  CloseHandle(handle);
  if (!ok) {
    return false;
  }
  *out = TicksOf(created);
  return true;
#else
  (void)pid;
  return false;
#endif
}

namespace {

#if defined(_WIN32)
class WindowsChildLauncher : public ChildLauncher {
 public:
  ~WindowsChildLauncher() override {
    if (process_ != nullptr) {
      CloseHandle(process_);
    }
  }

  bool Spawn(const std::filesystem::path& executable,
             const std::vector<std::string>& arguments,
             const std::filesystem::path& working_directory, std::string* error) override {
    if (process_ != nullptr) {
      *error = "a restart child is already running";
      return false;
    }
    if (executable.empty()) {
      *error = "there is no executable to start";
      return false;
    }
    const std::wstring wide_executable = executable.wstring();
    // CreateProcessW may write into the command-line buffer, so it must be a mutable copy and it
    // must outlive the call. A child is started with no shell, so a path with spaces, an ampersand
    // or a quote is passed exactly as this build quoted it.
    std::wstring command_line = ToWide(BuildCommandLine(executable, arguments));
    std::wstring wide_working_directory = working_directory.wstring();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const BOOL started = CreateProcessW(
        wide_executable.empty() ? nullptr : wide_executable.c_str(), command_line.data(), nullptr,
        nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
        wide_working_directory.empty() ? nullptr : wide_working_directory.c_str(), &startup, &info);
    if (!started) {
      *error = "CreateProcess failed with error " + std::to_string(GetLastError());
      return false;
    }
    process_ = info.hProcess;
    child_pid_ = static_cast<uint32_t>(info.dwProcessId);
    CloseHandle(info.hThread);
    return true;
  }

  bool WaitForExit(uint32_t timeout_ms, std::string* error) override {
    if (process_ == nullptr) {
      *error = "no child to wait for";
      return false;
    }
    const DWORD waited = WaitForSingleObject(process_, timeout_ms);
    CloseHandle(process_);
    process_ = nullptr;
    if (waited == WAIT_OBJECT_0) {
      return true;
    }
    *error = waited == WAIT_TIMEOUT ? "the parent did not exit within the timeout"
                                    : "the wait on the parent failed";
    return false;
  }

  uint32_t ChildPid() const override { return child_pid_; }

 private:
  HANDLE process_ = nullptr;
  uint32_t child_pid_ = 0;
};
#endif

#if defined(_WIN32)
class WindowsParentWaiter : public ParentWaiter {
 public:
  WaitResult WaitForExit(uint32_t pid, uint64_t created, uint32_t timeout_ms,
                         std::string* error) override {
    HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr) {
      const DWORD code = GetLastError();
      // A pid with no process behind it is the ordinary "the parent already left" case, and it is
      // a *success* for the bootstrap: it may start the game.
      if (code == ERROR_INVALID_PARAMETER) {
        return WaitResult::kAlreadyGone;
      }
      *error = "OpenProcess failed with error " + std::to_string(code);
      return WaitResult::kFailed;
    }
    if (created != 0) {
      FILETIME creation{};
      FILETIME exit_time{};
      FILETIME kernel{};
      FILETIME user{};
      if (GetProcessTimes(handle, &creation, &exit_time, &kernel, &user) && TicksOf(creation) != created) {
        // The pid has been reused: this is a stranger, not our parent. Waiting on it would be
        // waiting on the wrong process, and refusing is the safe answer.
        CloseHandle(handle);
        *error = "the pid now belongs to a different process";
        return WaitResult::kIdentityMismatch;
      }
    }
    const DWORD waited = WaitForSingleObject(handle, timeout_ms);
    CloseHandle(handle);
    if (waited == WAIT_OBJECT_0) {
      return WaitResult::kExited;
    }
    if (waited == WAIT_TIMEOUT) {
      *error = "the parent did not exit within the timeout";
      return WaitResult::kTimeout;
    }
    *error = "the wait on the parent failed";
    return WaitResult::kFailed;
  }
};
#endif

}  // namespace

std::unique_ptr<ChildLauncher> CreateChildLauncher() {
#if defined(_WIN32)
  return std::make_unique<WindowsChildLauncher>();
#else
  return nullptr;
#endif
}

std::unique_ptr<ParentWaiter> CreateParentWaiter() {
#if defined(_WIN32)
  return std::make_unique<WindowsParentWaiter>();
#else
  return nullptr;
#endif
}

// ------------------------------------------------------------------------------------------------
// The coordinator
// ------------------------------------------------------------------------------------------------

const char* RestartPhaseToken(RestartPhase phase) {
  switch (phase) {
    case RestartPhase::kIdle:
      return "idle";
    case RestartPhase::kSpawning:
      return "spawning";
    case RestartPhase::kWaitingChild:
      return "waiting_child";
    case RestartPhase::kReady:
      return "ready";
    case RestartPhase::kFailed:
      return "failed";
  }
  return "?";
}

RestartCoordinator::RestartCoordinator(ChildLauncher* launcher) : launcher_(launcher) {}

RestartOutcome RestartCoordinator::Request(uint64_t saved_revision, uint64_t current_revision,
                                           uint64_t request_id, const LaunchContext& context) {
  RestartOutcome outcome;
  if (requested_) {
    // §8: a held confirm input must not trigger both save and restart twice. The first request is
    // the only one; a repeat is refused, not queued.
    outcome.status = rb_blitz::settings::RestartStatus::kAlreadyRequested;
    outcome.phase = phase_;
    outcome.error = "a restart is already in flight";
    return outcome;
  }
  if (saved_revision != current_revision) {
    // The player confirmed a revision somebody else has since changed; booting into it would
    // apply a state nobody confirmed.
    outcome.status = rb_blitz::settings::RestartStatus::kStaleRevision;
    outcome.phase = phase_;
    outcome.error = "settings.stale_revision";
    return outcome;
  }
  if (saved_revision == 0) {
    outcome.status = rb_blitz::settings::RestartStatus::kNotSaved;
    outcome.phase = phase_;
    outcome.error = "nothing durable has been saved yet";
    return outcome;
  }
  if (launcher_ == nullptr) {
    outcome.status = rb_blitz::settings::RestartStatus::kChildFailed;
    outcome.phase = RestartPhase::kFailed;
    phase_ = RestartPhase::kFailed;
    error_ = "this build has no way to start a process";
    outcome.error = error_;
    return outcome;
  }

  token_ = HandoffToken{};
  token_.parent_pid = ThisProcessId();
  token_.parent_created = ThisProcessCreatedTicks();
  // A DTA script sends request id 0 ("no id"), so the one-shot identity is derived instead of
  // taken: it only has to be non-zero and unique to this request.
  const uint64_t derived = 0x9E3779B97F4A7C15ull * (static_cast<uint64_t>(token_.parent_pid) + 1) ^
                           token_.parent_created ^ request_id;
  token_.nonce = request_id != 0 ? request_id : (derived == 0 ? 1 : derived);
  LaunchTarget target = LaunchTarget::kCommon;
  token_.target = TargetFromToken(context.target, &target) ? std::string(context.target)
                                                           : std::string("common");
  token_.version = kHandoffVersion;

  // From here on the request is taken: `requested_` is what makes it exactly-once.
  requested_ = true;
  request_id_ = request_id;
  phase_ = RestartPhase::kSpawning;

  const std::vector<std::string> arguments = BuildChildArguments(context, token_);
  command_line_ = BuildCommandLine(context.executable, arguments);
  std::string spawn_error;
  if (!launcher_->Spawn(context.executable, arguments, context.working_directory, &spawn_error)) {
    // §8 step 4: a failed child creation keeps the original game running and reports the failure.
    phase_ = RestartPhase::kFailed;
    error_ = spawn_error;
    outcome.status = rb_blitz::settings::RestartStatus::kChildFailed;
    outcome.phase = phase_;
    outcome.error = spawn_error;
    return outcome;
  }
  ++spawned_;
  phase_ = RestartPhase::kWaitingChild;

  // The handshake, stated plainly: the child's first instruction is to wait for *this* process to
  // exit, so a successful `Spawn` means it is running and has not yet touched the SDK, the save
  // mount, the audio/GPU device or the title. That is the readiness this coordinator needs before
  // asking the UI to close (§8 step 4). A pipe-based child signal would be stronger and is not
  // needed while the child does nothing else first.
  phase_ = RestartPhase::kReady;
  outcome.status = rb_blitz::settings::RestartStatus::kOk;
  outcome.phase = phase_;
  return outcome;
}

void RestartCoordinator::ResetAfterFailure() {
  if (phase_ != RestartPhase::kFailed) {
    return;  // a successful handshake is final: one child per boot
  }
  phase_ = RestartPhase::kIdle;
  requested_ = false;
  error_.clear();
}

// ------------------------------------------------------------------------------------------------
// The bootstrap
// ------------------------------------------------------------------------------------------------

BootstrapResult RunBootstrap(int argc, const char* const* argv, ParentWaiter* waiter,
                             uint32_t wait_timeout_ms) {
  BootstrapResult result;
  std::string error;
  const std::optional<HandoffToken> token = HandoffFromArguments(argc, argv, &error);
  if (!token.has_value()) {
    // No handoff switch at all is the ordinary case. A malformed one is reported so the boot log
    // names it instead of silently starting a second game.
    result.error = error;
    return result;
  }

  // The arguments the game continues with: the one-shot and transient ones are gone for good.
  for (int index = 0; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (IsRetiredArgument(argument)) {
      continue;
    }
    result.arguments.push_back(std::string(argument));
  }

  ParentWaiter* effective = waiter;
  std::unique_ptr<ParentWaiter> owned;
  if (effective == nullptr) {
    owned = CreateParentWaiter();
    effective = owned.get();
  }
  if (effective == nullptr) {
    result.error = "this platform has no way to wait for the parent process";
    return result;
  }

  std::string wait_error;
  switch (effective->WaitForExit(token->parent_pid, token->parent_created, wait_timeout_ms,
                                 &wait_error)) {
    case ParentWaiter::WaitResult::kExited:
    case ParentWaiter::WaitResult::kAlreadyGone:
      // The parent is gone (or was never there): start the game, once.
      result.action = BootstrapAction::kStartGame;
      break;
    case ParentWaiter::WaitResult::kIdentityMismatch:
      // The pid was reused, so the process we were told to wait for no longer exists. Starting is
      // correct; the observation is kept so the log explains why there was no wait.
      result.action = BootstrapAction::kStartGame;
      result.error = wait_error;
      break;
    case ParentWaiter::WaitResult::kTimeout:
    case ParentWaiter::WaitResult::kFailed:
      // Never start beside a parent that may still be running: two titles writing the same saves is
      // exactly what §8 forbids.
      result.action = BootstrapAction::kNone;
      result.error = wait_error;
      break;
  }
  return result;
}

}  // namespace rb_blitz::platform
