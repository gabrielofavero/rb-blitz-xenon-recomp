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
