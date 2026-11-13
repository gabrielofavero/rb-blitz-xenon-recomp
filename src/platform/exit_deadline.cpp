// SPDX-License-Identifier: GPL-2.0-only
#include "platform/exit_deadline.h"

#include <atomic>
#include <cstdlib>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace rb_blitz::platform {

bool ArmExitDeadline(std::chrono::milliseconds timeout) {
  static std::atomic<bool> armed{false};
  if (armed.exchange(true)) return true;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  try {
    std::thread([deadline] {
      std::this_thread::sleep_until(deadline);
      // Do not log, flush, acquire application locks or invoke destructors here:
      // the cleanup thread may be stuck holding exactly those locks. Guest saves
      // have completed; unfinished host caches can be regenerated at next boot.
#if defined(_WIN32)
      TerminateProcess(GetCurrentProcess(), 0);
#endif
      std::_Exit(0);
    }).detach();
  } catch (const std::system_error&) {
    armed.store(false);
    return false;
  }
  return true;
}

}  // namespace rb_blitz::platform
