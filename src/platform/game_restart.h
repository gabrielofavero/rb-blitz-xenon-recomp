// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9 - the restart coordinator (parent plan §8). The §8 flow is a state machine over four
// operations the *host* performs, and none of them can live in the UI code:
//
//   request  -> verify the acknowledged revision is the one the caller thinks it is
//   drain    -> (the coordinator's own commit barrier; this module is told "saved")
//   spawn    -> start ONE child with a rebuilt command line, bound to this process's identity
//   wait     -> do not let the old process's file handles and GPU device overlap the new one
//
// Why the child is not simply "start the game again": the SDK's `ReXApp::OnClosing` calls
// `std::_Exit(0)`, so nothing after the close request is dependable - no destructors, no
// `OnShutdown`. Everything that must happen *before* the close (saving, spawning, the handshake)
// therefore happens here, on the UI thread, while the old process is still fully alive. The child
// is what performs the actual new boot, and it must wait for this process to be gone before it
// initializes the SDK, mounts saves, opens the audio/GPU device or starts the title.
//
// Two design rules come straight from §8 and are enforced rather than documented:
//
//   * **only ONE child, ever.** The request is exactly-once per (saved revision, request id) and a
//     second request while a restart is in flight is refused (`kAlreadyRequested`), so a held
//     button or a repeated frame cannot start two games.
//   * **the handoff carries no executable path.** A token is a fixed-shape, bounded, space-free
//     string built here from numbers plus the target vocabulary; a child re-executes *itself* and
//     can never be told by guest data to run something else (parent plan §8: "incapable of
//     launching an arbitrary executable supplied by guest data").
//
// SDK-free: the module needs no emulator, no window and no cvar registry, so the whole state
// machine and the whole argument rebuild are testable from `tests/game_restart_tests.cpp`,
// including the real `CreateProcess`/`WaitForSingleObject` half.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "settings/model.h"

namespace rb_blitz::platform {

// The switch a bootstrap child is started with, and the token's version. The version is part of
// the text so a token from a future build is refused rather than misread.
inline constexpr char kHandoffSwitch[] = "--restart-handoff=";
inline constexpr uint32_t kHandoffVersion = 1;

// The switch that tells a fresh process to act as the bootstrap: wait for the parent, then start
// the game once. Never persisted, never written into a shortcut.
inline constexpr char kBootstrapSwitch[] = "--restart-bootstrap";

// ------------------------------------------------------------------------------------------------
// The handoff token
// ------------------------------------------------------------------------------------------------
//
//   v1.<parent_pid hex>.<parent_created hex>.<nonce hex>.<target>
//
// `parent_created` is the parent's process creation time (Windows FILETIME ticks). It is what makes
// the wait bind to the *process* rather than to a reused pid: the child opens the pid, compares
// the creation time, and refuses to wait on a stranger. `target` is the persisted target's frozen
// spelling, and it is the only non-numeric field - which is why the token is validated, not parsed
// loosely.
//
// Bounds: the whole token is at most 96 characters and every field is fixed-shape, so a hand-built
// or truncated token is refused rather than half-applied.
inline constexpr size_t kMaxHandoffChars = 96;

struct HandoffToken {
  uint32_t version = kHandoffVersion;
  uint32_t parent_pid = 0;       // never 0: a token for "no parent" is malformed
  uint64_t parent_created = 0;   // the parent's creation time, for the identity check
  uint64_t nonce = 0;            // never 0: the one-shot request identity
  std::string target = "common"; // the frozen vocabulary: common | demo | ultimate
};

std::string EncodeHandoff(const HandoffToken& token);

// False (with a reason) for a wrong version, a wrong field count, a non-hex field, a zero pid or
// nonce, an unknown target, or anything over the bound. Never partially fills `out`.
bool DecodeHandoff(std::string_view text, HandoffToken* out, std::string* error);

// True when the argument is the handoff switch (whether or not its token is valid).
bool IsHandoffSwitch(std::string_view argument);

// The handoff token of a command line, if there is one. `error` is set when the switch is present
// but its token is not acceptable - a malformed handoff must stop the bootstrap, not be ignored.
std::optional<HandoffToken> HandoffFromArguments(int argc, const char* const* argv,
                                                 std::string* error);

// The target's frozen spelling and its inverse, so a token and a `LaunchTarget` cannot disagree.
std::string_view TargetToken(rb_blitz::launcher::LaunchTarget target);
bool TargetFromToken(std::string_view token, rb_blitz::launcher::LaunchTarget* out);

// ------------------------------------------------------------------------------------------------
// The command line a child is started with
// ------------------------------------------------------------------------------------------------

struct LaunchContext {
  std::filesystem::path executable;        // the exe to start (this one, re-executed)
  std::filesystem::path game_data_root;    // absolute; empty = the game's own resolution
  std::filesystem::path working_directory; // empty = the executable's folder
  std::filesystem::path launcher_profile;  // identity the old process was started with
  std::string target = "common";           // the NEWLY persisted target (§8 step 2)
  // Developer arguments that survive a restart. Only these names are ever rebuilt onto the child's
  // command line; see `IsRetiredArgument` for the ones that are dropped instead.
  std::vector<std::string> developer_arguments;
};

// The arguments a child is started with, in a stable order: where the data is, the target, the
// profile identity, the surviving developer arguments, then the handoff switch. Deliberately NOT a
// replay of the old command line: the old line carries launcher-generated setting flags that would
// override the file the player just wrote (§8 step 2).
std::vector<std::string> BuildChildArguments(const LaunchContext& context,
                                             const HandoffToken& token);

// The full command line for `CreateProcess`/a log line: the quoted executable and its arguments.
std::string BuildCommandLine(const std::filesystem::path& executable,
                             const std::vector<std::string>& arguments);

// The Windows quoting rule (MSDN, "Parsing C++ command-line arguments"): a run of backslashes that
// precedes a quote is doubled and the quote is escaped; otherwise the run is emitted verbatim
// inside the quotes. The same rule the launcher's own `BuildLaunchCommand` uses, repeated here
// because this module is a separate target from the launcher.
std::string QuoteArgument(std::string_view text);

// Arguments a restart never rebuilds: the previous handoff (one-shot, consumed), the transient
// cache refresh (its whole meaning is "this once"), and the bootstrap switch itself.
bool IsRetiredArgument(std::string_view argument);

// ------------------------------------------------------------------------------------------------
// Spawning and waiting
// ------------------------------------------------------------------------------------------------
//
// An interface so the state machine is testable without a process, and a real implementation so the
// platform half is testable *with* one (tests/game_restart_tests.cpp exercises both).
class ChildLauncher {
 public:
  virtual ~ChildLauncher() = default;

  // Starts the child with a rebuilt command line and no shell. On success the child is running and
  // `WaitForExit` may be called; on failure `error` says why and nothing was started.
  virtual bool Spawn(const std::filesystem::path& executable,
                     const std::vector<std::string>& arguments,
                     const std::filesystem::path& working_directory, std::string* error) = 0;

  // Waits for the child to exit, bounded. False when the wait timed out or failed; `error` says
  // which. The handle is closed either way.
  virtual bool WaitForExit(uint32_t timeout_ms, std::string* error) = 0;

  // The running child's pid, or 0 when there is none. For the boot log and for the test that
  // proves the bootstrap's wait really happens against a live process.
  virtual uint32_t ChildPid() const = 0;
};

// The platform launcher: `CreateProcessW` with a UTF-8 command line converted properly, and a
// `WaitForSingleObject` bound. Null only if the platform has no implementation.
std::unique_ptr<ChildLauncher> CreateChildLauncher();

// The identity a handoff token carries: this process's pid and creation time, and the same two
// facts for an arbitrary pid (so a coordinator can build a token, and so a test can prove the
// mismatch case). `0`/false when the platform cannot answer.
uint32_t ThisProcessId();
uint64_t ThisProcessCreatedTicks();
bool ProcessCreatedTicks(uint32_t pid, uint64_t* out);

// ------------------------------------------------------------------------------------------------
// The coordinator
// ------------------------------------------------------------------------------------------------

enum class RestartPhase : uint8_t {
  kIdle = 0,     // nothing requested
  kSpawning,     // the child is being started
  kWaitingChild, // the child exists; it is waiting for this process to exit
  kReady,        // the handshake succeeded: the caller may now request the UI close
  kFailed,       // the spawn failed; the game keeps running and the error is kept
};

const char* RestartPhaseToken(RestartPhase phase);

struct RestartOutcome {
  rb_blitz::settings::RestartStatus status = rb_blitz::settings::RestartStatus::kOk;
  RestartPhase phase = RestartPhase::kIdle;
  std::string error;
};

// The exactly-once restart request, and the child it creates. One instance per boot, owned by the
// boot wiring; every method runs on the UI thread.
class RestartCoordinator {
 public:
  // `launcher` is borrowed and must outlive this object.
  explicit RestartCoordinator(ChildLauncher* launcher);

  // §8 step 1 and 3-4. `saved_revision` is what the caller believes is durable; `current_revision`
  // is what the service reports now. A mismatch is `kStaleRevision` and nothing is spawned: a
  // restart for a revision somebody else has since changed would boot into a state the player
  // never confirmed.
  RestartOutcome Request(uint64_t saved_revision, uint64_t current_revision, uint64_t request_id,
                         const LaunchContext& context);

  // §8 step 5: the child is up and waiting; the caller may now ask the UI to close. False when no
  // handshake has happened (nothing to close for).
  bool CloseRequested() const { return phase_ == RestartPhase::kReady; }

  RestartPhase phase() const { return phase_; }
  const std::string& error() const { return error_; }
  const HandoffToken& token() const { return token_; }
  uint64_t request_id() const { return request_id_; }
  // The command line the child was started with, for the log and for a failure message.
  const std::string& command_line() const { return command_line_; }
  // How many children this coordinator has started. Never more than one for a whole boot.
  int spawned() const { return spawned_; }

  // Drops a failed attempt so a later request can try again, keeping the error for the log. A
  // successful handshake is *not* resettable: one child per boot is the rule.
  void ResetAfterFailure();

 private:
  ChildLauncher* launcher_ = nullptr;
  RestartPhase phase_ = RestartPhase::kIdle;
  std::string error_;
  HandoffToken token_;
  uint64_t request_id_ = 0;
  bool requested_ = false;
  int spawned_ = 0;
  std::string command_line_;
};

// ------------------------------------------------------------------------------------------------
// The bootstrap half (the child's first instructions)
// ------------------------------------------------------------------------------------------------

enum class BootstrapAction : uint8_t {
  kNone = 0,    // this process was not started as a bootstrap: carry on normally
  kStartGame,   // the parent is gone: start the game, once, with `arguments`
};

struct BootstrapResult {
  BootstrapAction action = BootstrapAction::kNone;
  std::vector<std::string> arguments;  // the handoff switch removed, retired args dropped
  std::string error;
};

// The bootstrap's whole job, callable from `main()` before the SDK exists: if the command line
// carries a handoff token, wait (bounded) for the parent's *verified identity* to exit, then say
// so. `wait_timeout_ms` is a safety net, not a policy: a parent that is already gone returns
// immediately.
//
// `waiter` is the platform half (tests pass their own). Null uses the real one.
class ParentWaiter {
 public:
  virtual ~ParentWaiter() = default;
  // True when the process with this identity existed and exited before the timeout. False when it
  // is gone already (also fine - the caller must start), when the identity does not match, or when
  // the wait timed out; `error` distinguishes "gone" from "wrong process" and "timeout".
  enum class WaitResult : uint8_t { kExited = 0, kAlreadyGone, kIdentityMismatch, kTimeout, kFailed };
  virtual WaitResult WaitForExit(uint32_t pid, uint64_t created, uint32_t timeout_ms,
                                 std::string* error) = 0;
};

std::unique_ptr<ParentWaiter> CreateParentWaiter();

BootstrapResult RunBootstrap(int argc, const char* const* argv, ParentWaiter* waiter,
                             uint32_t wait_timeout_ms = 30000);

}  // namespace rb_blitz::platform
