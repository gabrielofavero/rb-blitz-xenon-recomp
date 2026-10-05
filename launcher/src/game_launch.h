// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Starting the game (docs/plans/launcher-plan.md §4.3, Contract 3; prompt B7).
//
// Two halves, and only the second one knows about the operating system:
//
//   * BuildLaunchCommand is the contract, as a pure function. The argv is the profile's own
//     values and nothing else - a row is passed only when it differs from the compiled default
//     (D3 rank 1), which is the same rule the precedence badge is decided by. Pure text, so
//     the contract is a unit test and `--print-command` is the same code path as a real start.
//   * GameProcess is the start itself, and the one thing after it that matters: whether the
//     game is still running. The launcher refuses a second start while it is, because two
//     copies of the title writing one save folder is not something to discover by trying.
//
// SDK-free and ImGui-free on purpose, like the profile module it reads.

#pragma once

#include <filesystem>
#include <string>

#include "profile_session.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

// The name the game's executable has in the payload, next to the launcher (D1).
inline constexpr const char* kGameExecutableName = "rb_blitz.exe";

// What the launcher would hand the game, split the way CreateProcessW takes it. `arguments` is
// the whole tail with every value already quoted, which is also what a bug report wants: the
// exact command line is the first thing anybody asks for.
struct LaunchCommand {
  std::filesystem::path executable;
  std::string arguments;
  std::filesystem::path working_directory;
  // False when the game cannot be started at all, with `error` saying which piece is missing.
  // A command that is not ok is still printable: what the launcher *would* have run is part of
  // the diagnosis.
  bool ok = false;
  std::string error;
};

// The full command line, executable and tail, for printing.
std::string FormatLaunchCommand(const LaunchCommand& command);

// `target` is the target the launcher will really use - D5's fallback already applied - and
// `roots` is where the game's data was found. Nothing is started and nothing is written.
LaunchCommand BuildLaunchCommand(const ProfileSession& session, const GameRoots& roots,
                                 LaunchTarget target);

// Where the game writes its own log. The runtime names the folder `<exe dir>\logs` and the files
// `<app name>_NNN.log` (`rexglue-sdk/src/ui/rex_app.cpp`, `core/logging.cpp`), and the launcher
// starts the game with the install folder as the working directory, so this is where a boot
// failure's only record lands - a WIN32-subsystem binary prints nothing else.
std::filesystem::path GameLogDirectory(const LaunchCommand& command);

// The whole sentence a failed start shows: the reason, the exact command line the launcher built
// (still printable when the command is not `ok`), and where the game's own log goes. Pure, so the
// failing-start case is asserted by a test rather than by reading a bar.
std::string LaunchFailureMessage(const LaunchCommand& command, std::string_view error);

// What the launcher checks before it spawns anything, so a problem the game would meet silently
// is refused here with a reason instead. Empty when the launch may go ahead.
//
// The game reads the profile as a config file, and `rex::cvar::LoadConfig` treats a file it
// cannot open exactly like a file that is not there - it starts on the compiled defaults and says
// so only in its own log. So the launcher verifies that the file the game is about to read is
// readable, and that the folder holding it is writable: if it is not, "the launcher wrote a
// profile" cannot be kept true. A profile that has not been written yet is not an error - there
// is nothing to read, and the defaults are the right answer.
std::string LaunchReadiness(const ProfileSession& session);

// The started game. Owns the process handle, so "is it still running?" does not need a name or
// a PID lookup, and closes it when the launcher lets go.
class GameProcess {
 public:
  GameProcess() = default;
  ~GameProcess();
  GameProcess(const GameProcess&) = delete;
  GameProcess& operator=(const GameProcess&) = delete;

  // Starts `command`. Returns false and fills `error` when it could not be started; the command
  // has to be `ok` first.
  bool Start(const LaunchCommand& command, std::string* error);
  // True while the game is alive.
  bool Running() const;
  // Forgets the process without touching it: the launcher gives up its handle, the game keeps
  // running. Only used when the handle is no longer needed.
  void Release();

 private:
  void* handle_ = nullptr;
};

}  // namespace rb_blitz::launcher
