// SPDX-License-Identifier: GPL-2.0-only
#include "platform/crash_diagnostics.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cwchar>
#elif defined(__linux__) || defined(__APPLE__)
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#else
#error Crash diagnostics needs a platform implementation.
#endif

namespace rb_blitz::platform {
namespace {
#if defined(_WIN32)
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
#else
// The handler cannot allocate, format with stdio, or use the filesystem library. Keep the path
// and record fixed-size and let the OS produce its normal fatal-signal report/core dump afterward.
char dump_directory[3900]{};
volatile sig_atomic_t captured = 0;
constexpr int kFatalSignals[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};

char* AppendUnsigned(char* out, unsigned long value) {
  char reverse[32];
  unsigned count = 0;
  do {
    reverse[count++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (count != 0) *out++ = reverse[--count];
  return out;
}

void OnFatalSignal(int number, siginfo_t*, void*) {
  if (!captured) {
    captured = 1;
    const pid_t pid = getpid();
    char path[4096];
    char* end = path;
    for (const char* source = dump_directory; *source; ++source) *end++ = *source;
    const char basename[] = "rb_blitz_";
    for (const char* source = basename; *source; ++source) *end++ = *source;
    end = AppendUnsigned(end, static_cast<unsigned long>(pid));
    *end++ = '_';
    end = AppendUnsigned(end, static_cast<unsigned long>(number));
    *end++ = '.';
    *end++ = 't';
    *end++ = 'x';
    *end++ = 't';
    *end = '\0';
    const int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file >= 0) {
      char line[128];
      char* cursor = line;
      const char prefix[] = "source=signal pid=";
      for (const char* source = prefix; *source; ++source) *cursor++ = *source;
      cursor = AppendUnsigned(cursor, static_cast<unsigned long>(pid));
      const char middle[] = " signal=";
      for (const char* source = middle; *source; ++source) *cursor++ = *source;
      cursor = AppendUnsigned(cursor, static_cast<unsigned long>(number));
      *cursor++ = '\n';
      (void)write(file, line, static_cast<size_t>(cursor - line));
      close(file);
    }
  }
  struct sigaction normal{};
  normal.sa_handler = SIG_DFL;
  sigemptyset(&normal.sa_mask);
  (void)sigaction(number, &normal, nullptr);
  if (kill(getpid(), number) == 0) return;
  _exit(128 + number);
}
#endif
}  // namespace

bool InstallCrashDiagnostics(const std::filesystem::path& cache_root) {
  std::error_code error;
  const auto directory = cache_root / "crash-dumps";
  std::filesystem::create_directories(directory, error);
  if (error) return false;
#if defined(_WIN32)
  dump_directory = directory.wstring() + L'\\';
  if (dump_directory.size() > 3900) return false;
  if (!AddVectoredExceptionHandler(1, OnVectoredException)) return false;
  SetUnhandledExceptionFilter(OnUnhandledException);
  return true;
#else
  const std::string path = directory.string() + '/';
  if (path.size() >= sizeof(dump_directory) - 64) return false;
  std::memcpy(dump_directory, path.c_str(), path.size() + 1);
  struct sigaction action{};
  action.sa_sigaction = OnFatalSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_RESETHAND;
  struct sigaction previous[sizeof(kFatalSignals) / sizeof(kFatalSignals[0])]{};
  for (size_t index = 0; index < sizeof(kFatalSignals) / sizeof(kFatalSignals[0]); ++index) {
    if (sigaction(kFatalSignals[index], &action, &previous[index]) != 0) {
      while (index != 0) {
        --index;
        (void)sigaction(kFatalSignals[index], &previous[index], nullptr);
      }
      return false;
    }
  }
  return true;
#endif
}
}  // namespace rb_blitz::platform
