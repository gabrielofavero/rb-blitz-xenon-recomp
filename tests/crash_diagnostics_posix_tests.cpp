// SPDX-License-Identifier: GPL-2.0-only
#include "platform/crash_diagnostics.h"
#include <csignal>
#include <filesystem>
#include <unistd.h>

namespace {
volatile sig_atomic_t observed = 0;
void RuntimeFault(int, siginfo_t*, void*) { observed = 1; }
}

int main() {
  struct sigaction runtime{};
  runtime.sa_sigaction = RuntimeFault;
  runtime.sa_flags = SA_SIGINFO;
  sigemptyset(&runtime.sa_mask);
  if (sigaction(SIGBUS, &runtime, nullptr) != 0) return 1;
  const auto root = std::filesystem::temp_directory_path() /
      ("rb-blitz-signal-test-" + std::to_string(getpid()));
  if (!rb_blitz::platform::InstallCrashDiagnostics(root)) return 2;
  raise(SIGBUS);
  std::filesystem::remove_all(root);
  return observed ? 0 : 3;
}
