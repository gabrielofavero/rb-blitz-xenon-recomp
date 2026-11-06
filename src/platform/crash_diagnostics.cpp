// SPDX-License-Identifier: GPL-2.0-only
#include "platform/crash_diagnostics.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace rb_blitz::platform {
namespace {
std::wstring dump_directory;
volatile LONG captured = 0;

void WriteLine(HANDLE file, const char* line) {
  DWORD written = 0;
  WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
}

void Capture(EXCEPTION_POINTERS* exception, const char* source) {
  if (!exception || !exception->ExceptionRecord ||
      InterlockedCompareExchange(&captured, 1, 0) != 0) return;

  FILETIME time{};
  GetSystemTimeAsFileTime(&time);
  const unsigned long long stamp =
      (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  const DWORD pid = GetCurrentProcessId();
  const DWORD thread = GetCurrentThreadId();
  wchar_t basename[128];
  swprintf_s(basename, L"rb_blitz_%lu_%llu", pid, stamp);
  wchar_t record_path[4096];
  wchar_t dump_path[4096];
  if (swprintf_s(record_path, L"%ls%ls.txt", dump_directory.c_str(), basename) < 0 ||
      swprintf_s(dump_path, L"%ls%ls.dmp", dump_directory.c_str(), basename) < 0) return;

  const HANDLE record = CreateFileW(record_path, GENERIC_WRITE, FILE_SHARE_READ,
                                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (record == INVALID_HANDLE_VALUE) return;
  char line[256];
  sprintf_s(line, "source=%s pid=%lu thread=%lu exception=0x%08lX address=%p\r\n",
            source, pid, thread, exception->ExceptionRecord->ExceptionCode,
            exception->ExceptionRecord->ExceptionAddress);
  WriteLine(record, line);
  // Preserve the fault address even if writing the dump fails on a damaged heap.
  FlushFileBuffers(record);

  const HANDLE dump = CreateFileW(dump_path, GENERIC_WRITE, FILE_SHARE_READ,
                                  nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump == INVALID_HANDLE_VALUE) {
    sprintf_s(line, "dump_failed=CreateFileW error=%lu\r\n", GetLastError());
  } else {
    MINIDUMP_EXCEPTION_INFORMATION details{thread, exception, FALSE};
    constexpr auto kind = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
        MiniDumpWithIndirectlyReferencedMemory);
    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), pid, dump, kind,
                                          &details, nullptr, nullptr);
    const DWORD error = written ? 0 : GetLastError();
    CloseHandle(dump);
    sprintf_s(line, "dump_written=%u error=%lu\r\n", written != FALSE, error);
  }
  WriteLine(record, line);
  FlushFileBuffers(record);
  CloseHandle(record);
}

LONG CALLBACK OnVectoredException(EXCEPTION_POINTERS* exception) {
  if (!exception || !exception->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
  // The emulator handles many ordinary first-chance access violations. These
  // two statuses identify a fatal heap check or stack-buffer overrun instead.
  const DWORD code = exception->ExceptionRecord->ExceptionCode;
  if (code == 0xC0000374u || code == 0xC0000409u)
    Capture(exception, "vectored");
  return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception) {
  Capture(exception, "unhandled");
  return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

bool InstallCrashDiagnostics(const std::filesystem::path& cache_root) {
  std::error_code error;
  const auto directory = cache_root / "crash-dumps";
  std::filesystem::create_directories(directory, error);
  if (error) return false;
  dump_directory = directory.wstring() + L'\\';
  if (dump_directory.size() > 3900) return false;
  if (!AddVectoredExceptionHandler(1, OnVectoredException)) return false;
  SetUnhandledExceptionFilter(OnUnhandledException);
  return true;
}
}  // namespace rb_blitz::platform
