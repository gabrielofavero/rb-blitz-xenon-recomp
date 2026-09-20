// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The helper's command surface. Each command is one thing the Inno Setup wizard
// needs to do that Pascal cannot: read the file table of an STFS package, extract
// a zip, download and hash a release, place files, verify them. Commands are
// deliberately small and independent, so the wizard can run one, show the result,
// and decide what to do next.

#pragma once

#include <string>
#include <vector>

namespace rb_blitz::installer {

// The exit codes the wizard matches on.
inline constexpr int kSuccessExitCode = 0;
inline constexpr int kFailedExitCode = 1;
inline constexpr int kUsageExitCode = 2;

// Runs one subcommand. `args` does not include argv[0]. Returns the exit code:
//   0  the command succeeded
//   1  the command failed (`--summary` and the log explain why)
//   2  the command line itself was wrong
int RunCommand(const std::vector<std::string>& args);

// Reports a failure that happened outside any command's own error handling, so
// `--summary` still says the call is over. The wizard treats "no summary yet" as
// "still running", so a helper that dies without writing one would stall it for
// its whole timeout instead of failing in front of the user.
void ReportUnhandledFailure(const std::vector<std::string>& args, std::string_view reason);

// The `--help` text: every command, and the keys each one writes to `--summary`.
std::string UsageText();

}  // namespace rb_blitz::installer
