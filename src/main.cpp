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
#elif defined(__APPLE__)
#include <crt_externs.h>
#endif

#include "generated/default/rb_blitz_init.h"

#include "platform/game_restart.h"
#include "platform/window_host.h"
#include "rb_blitz_app.h"

// The graphics backend the game renders with - the Renderer row of Controller Settings' Audio / Video
// tab, and the flag Controller Settings passes as `--gpu_backend=`. It is defined here, beside the app
// that acts on it (RbBlitzApp::SelectGpuBackend), because which backend to load is the app's
// decision rather than the SDK's: the GPU plugin can only run a backend it was compiled with,
// and the app is what knows how to fall back when it cannot.
//
// Windows keeps the SDK's historical "any" default. Linux and macOS have no DirectX backend,
// so their default is explicitly Vulkan even before settings.toml is read.
#if defined(_WIN32)
REXCVAR_DEFINE_STRING(gpu_backend, "any", "GPU",
                      "Graphics backend to render with: d3d12, vulkan, or any (whichever this "
                      "build has)")
    .allowed({"d3d12", "vulkan", "any"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#else
REXCVAR_DEFINE_STRING(gpu_backend, "vulkan", "GPU", "Graphics backend to render with: vulkan")
    .allowed({"vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#endif

REX_DEFINE_APP(rb_blitz, RbBlitzApp::Create)

// ------------------------------------------------------------------------------------------------
// P11a: the restart bootstrap (§8, handed over by P9)
// ------------------------------------------------------------------------------------------------
//
// A restart spawns exactly one child, and that child's *first* instruction is to wait for the
// parent's verified identity to disappear before anything else happens: no SDK setup, no save mount,
// no audio or GPU device, no title. Two titles writing the same saves is the one outcome §8 forbids,
// and the SDK's own entry point (rexglue-sdk/src/ui/windowed_app_main_sdl.cpp) parses the command
// line as its very first act with no hook in front of it - so the wait lives here, in a static
// object's constructor, which runs before the entry point is called at all.
//
// Three outcomes, and only three:
//
//   * no handoff switch             -> not a bootstrap; this does nothing at all;
//   * the parent's identity is gone -> carry on into an ordinary boot. The handoff switch stays on
//                                      this process's command line and is reported as an unknown
//                                      cvar by the registry, which is harmless: the handoff is
//                                      one-shot, carries no executable path and is never persisted;
//   * the wait failed or timed out  -> refuse to start. A parent that is still alive means starting
//                                      here would be the second title §8 forbids, so the child exits
//                                      with a distinct code instead. There is no log to write to yet:
//                                      logging is SDK-side and this runs before the SDK exists.
//
// The identity check is the token's own (`platform::HandoffToken` carries the parent's pid *and* its
// creation time), so a reused pid is refused rather than waited on - that is P9's tested contract,
// not this file's.

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
      // would be the second instance §8 forbids.
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
const RestartBootstrap g_restart_bootstrap;

}  // namespace
