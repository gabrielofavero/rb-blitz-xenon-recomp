// SPDX-License-Identifier: GPL-2.0-only
// Run host tests without Windows crash dialogs, including faults before main().
#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

// CreateProcess takes one mutable Windows command line, so quote each argument
// by the same backslash/quote rules used by the C runtime's argv parser.
std::string QuoteArgument(std::string_view argument) {
  std::string quoted = "\"";
  size_t slashes = 0;
  for (const char character : argument) {
    if (character == '\\') {
      ++slashes;
    } else if (character == '"') {
      quoted.append(slashes * 2 + 1, '\\');
      quoted += '"';
      slashes = 0;
    } else {
      quoted.append(slashes, '\\');
      quoted += character;
      slashes = 0;
    }
  }
  quoted.append(slashes * 2, '\\');
  quoted += '"';
  return quoted;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: rb_blitz_windows_test_runner <test.exe> [arguments...]\n");
    return 2;
  }

  std::string command_line = QuoteArgument(argv[1]);
  for (int i = 2; i < argc; ++i) {
    command_line += ' ';
    command_line += QuoteArgument(argv[i]);
  }

  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(argv[1], command_line.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &startup, &process)) {
    std::fprintf(stderr, "[ TEST LAUNCH FAILED ] Windows error %lu\n", GetLastError());
    return 2;
  }

  const DWORD wait_result = WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 0;
  const BOOL got_exit_code = GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (wait_result != WAIT_OBJECT_0 || !got_exit_code) {
    std::fprintf(stderr, "[ TEST WAIT FAILED ] Windows error %lu\n", GetLastError());
    return 2;
  }
  if (exit_code != 0) {
    std::fprintf(stderr, "[ TEST EXIT ] %s exited with code 0x%08lX\n", argv[1], exit_code);
  }
  return static_cast<int>(exit_code);
}
