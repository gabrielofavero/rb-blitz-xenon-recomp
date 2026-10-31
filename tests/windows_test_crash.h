// SPDX-License-Identifier: GPL-2.0-only
// Keep a crashing host-side test visible in its console/CTest log without opening
// Windows application-error or Just-In-Time debugger dialogs.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

namespace rb_blitz::test {

inline LONG WINAPI LogUnhandledTestException(EXCEPTION_POINTERS* exception) {
  char message[160];
  const int length = std::snprintf(
      message, sizeof(message), "[ CRASH ] unhandled Windows exception 0x%08lX at %p\n",
      exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress);
  const HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
  if (length > 0 && stderr_handle != nullptr && stderr_handle != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(stderr_handle, message, static_cast<DWORD>(length), &written, nullptr);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

inline void SuppressWindowsTestCrashDialogs() {
  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter(LogUnhandledTestException);
}

}  // namespace rb_blitz::test

#else
namespace rb_blitz::test {
inline void SuppressWindowsTestCrashDialogs() {}
}  // namespace rb_blitz::test
#endif
