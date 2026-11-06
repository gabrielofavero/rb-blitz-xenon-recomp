// SPDX-License-Identifier: GPL-2.0-only
#include "platform/crash_diagnostics.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {
bool RunChild(const std::filesystem::path& exe, const std::filesystem::path& directory,
              const char* mode, const char* expected_source) {
  std::filesystem::create_directories(directory);
  SetEnvironmentVariableW(L"RB_BLITZ_CRASH_TEST_DIR", directory.c_str());
  const std::wstring command = L'"' + exe.wstring() + L"\" --child " +
                               std::wstring(mode, mode + std::strlen(mode));
  std::wstring mutable_command = command;
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
  CloseHandle(process.hThread);
  const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
  DWORD exit_code = 0;
  const bool exited = wait == WAIT_OBJECT_0 &&
                      GetExitCodeProcess(process.hProcess, &exit_code) && exit_code != 0;
  CloseHandle(process.hProcess);
  if (!exited) return false;

  bool record_ok = false, dump_ok = false;
  for (const auto& entry : std::filesystem::directory_iterator(directory / "crash-dumps")) {
    if (entry.path().extension() == ".txt") {
      std::ifstream file(entry.path());
      const std::string text((std::istreambuf_iterator<char>(file)), {});
      record_ok = text.find(expected_source) != std::string::npos &&
                  text.find("dump_written=1") != std::string::npos;
    } else if (entry.path().extension() == ".dmp") {
      std::ifstream file(entry.path(), std::ios::binary);
      char signature[4]{};
      file.read(signature, sizeof(signature));
      dump_ok = std::filesystem::file_size(entry.path()) > 4096 &&
                std::string(signature, sizeof(signature)) == "MDMP";
    }
  }
  return record_ok && dump_ok;
}
}  // namespace

int main(int argc, char** argv) {
  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (argc == 3 && std::string(argv[1]) == "--child") {
    wchar_t directory[4096];
    if (!GetEnvironmentVariableW(L"RB_BLITZ_CRASH_TEST_DIR", directory, 4096) ||
        !rb_blitz::platform::InstallCrashDiagnostics(directory)) return 2;
    RaiseException(std::string(argv[2]) == "heap" ? 0xC0000374u : 0xE0424242u,
                   0, 0, nullptr);
    return 3;
  }
  wchar_t image[4096];
  if (!GetModuleFileNameW(nullptr, image, 4096)) return 1;
  const auto root = std::filesystem::temp_directory_path() /
                    ("rb_blitz_crash_diagnostics_test_" + std::to_string(GetCurrentProcessId()));
  const bool generic = RunChild(image, root / "generic",
                                "generic", "source=unhandled");
  const bool heap = RunChild(image, root / "heap",
                             "heap", "source=vectored");
  if (!generic || !heap) {
    std::cerr << "crash capture failed; artifacts: " << root << '\n';
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
