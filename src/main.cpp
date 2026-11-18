// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>
#elif defined(RB_BLITZ_IOS)
#include "platform/ios_runtime.h"
#elif defined(__APPLE__)
#include <crt_externs.h>
#endif

#include "generated/default/rb_blitz_init.h"

#include "platform/game_restart.h"
#include "platform/window_host.h"
#include "rb_blitz_app.h"

#if defined(RB_BLITZ_IOS)
// SDL enters UIApplicationMain before calling the title and its asset importer.
#include <SDL3/SDL_main.h>
#endif

int RunRbBlitz(RbBlitzApp& app, const rex::CLIArgList& args) { return app.RunEntry(args); }
REX_TITLE_APP_MAIN(RbBlitzApp, RunRbBlitz)

// Run the process handoff before SDK initialization so two titles never mount one save tree.

std::vector<std::string> rb_blitz::platform::CommandLineArguments() {
#if defined(_WIN32)
  int count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::string> out;
  if (wide == nullptr) {
    return out;
  }
  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
    std::string text;
    if (bytes > 1) {
      text.resize(static_cast<size_t>(bytes));
      WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), bytes, nullptr, nullptr);
      text.pop_back();
    }
    out.push_back(std::move(text));
  }
  LocalFree(wide);
  return out;
#elif defined(RB_BLITZ_IOS)
  return IOSCommandLineArguments();
#elif defined(__APPLE__)
  std::vector<std::string> out;
  const int count = *_NSGetArgc();
  char* const* arguments = *_NSGetArgv();
  if (arguments == nullptr) return out;
  out.reserve(static_cast<size_t>(count));
  for (int index = 0; index < count; ++index) out.emplace_back(arguments[index]);
  return out;
#elif defined(__linux__)
  // /proc preserves the original argv bytes, including arguments with spaces. Read it before
  // the SDK entry point so a restart child cannot mount the game beside its parent.
  std::ifstream input("/proc/self/cmdline", std::ios::binary);
  std::vector<std::string> out;
  std::string argument;
  while (std::getline(input, argument, '\0')) out.push_back(argument);
  return out;
#else
#error Restart bootstrap needs a command-line implementation for this platform.
#endif
}

namespace {

constexpr int kBootstrapRefusedExitCode = 2;

struct RestartBootstrap {
  RestartBootstrap() {
    std::vector<std::string> arguments = rb_blitz::platform::CommandLineArguments();
    rb_blitz::platform::window_host::Bootstrap(arguments);
    std::vector<const char*> argv;
    argv.reserve(arguments.size());
    for (const std::string& argument : arguments) {
      argv.push_back(argument.c_str());
    }
    const rb_blitz::platform::BootstrapResult result = rb_blitz::platform::RunBootstrap(
        static_cast<int>(argv.size()), argv.data(), /*waiter=*/nullptr);
    if (result.action == rb_blitz::platform::BootstrapAction::kNone && !result.error.empty()) {
      // The parent is still alive, or its identity could not be confirmed. Starting the title now
      // would be the second instance Â§8 forbids.
#if defined(_WIN32)
      ExitProcess(kBootstrapRefusedExitCode);
#else
      std::_Exit(kBootstrapRefusedExitCode);
#endif
    }
    if (result.action == rb_blitz::platform::BootstrapAction::kNone) return;
    // The parent is gone: this process is the game, and it boots normally from here.
  }
};

// Runs before the SDK's platform entry point; see the comment above.
#if !defined(RB_BLITZ_IOS)
const RestartBootstrap g_restart_bootstrap;
#endif

}  // namespace
