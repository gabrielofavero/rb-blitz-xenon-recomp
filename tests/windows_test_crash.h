// SPDX-License-Identifier: GPL-2.0-only
// Keep a crashing host-side test visible in its console/CTest log without dialogs.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <exception>

namespace rb_blitz::test {

inline void WriteCrashLine(const char* message, size_t length) noexcept {
  const HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
  if (stderr_handle != nullptr && stderr_handle != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(stderr_handle, message, static_cast<DWORD>(length), &written, nullptr);
  }
}

inline LONG WINAPI LogUnhandledTestException(EXCEPTION_POINTERS* exception) {
  char message[160];
  const int length = std::snprintf(
      message, sizeof(message), "[ CRASH ] Windows exception 0x%08lX at %p\n",
      exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress);
  if (length > 0) WriteCrashLine(message, static_cast<size_t>(length));
  return EXCEPTION_EXECUTE_HANDLER;
}

inline void LogTerminatedTestException() noexcept {
  const char* detail = "unknown reason";
  if (const auto pending = std::current_exception()) {
    try {
      std::rethrow_exception(pending);
    } catch (const std::exception& error) {
      detail = error.what();
    } catch (...) {
      detail = "non-standard C++ exception";
    }
  }
  char message[512];
  const int length = std::snprintf(message, sizeof(message),
                                   "[ CRASH ] std::terminate: %s\n", detail);
  if (length > 0) {
    WriteCrashLine(message, static_cast<size_t>(length) < sizeof(message)
                                ? static_cast<size_t>(length) : sizeof(message) - 1);
  }
  ExitProcess(3);
}

inline void SuppressWindowsTestCrashDialogs() {
  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter(LogUnhandledTestException);
  std::set_terminate(LogTerminatedTestException);
}

}  // namespace rb_blitz::test

#else
namespace rb_blitz::test {
inline void SuppressWindowsTestCrashDialogs() {}
}  // namespace rb_blitz::test
#endif
