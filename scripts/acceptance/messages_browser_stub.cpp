// SPDX-License-Identifier: GPL-2.0-only
// CreateProcessW diagnostic opener: record exactly the URL passed by the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

int wmain(int argc, wchar_t* argv[]) {
  if (argc != 2 || !argv[1] || !*argv[1]) return 2;
  wchar_t path[32768]{};
  constexpr DWORD kPathChars = sizeof(path) / sizeof(path[0]);
  const DWORD length = GetEnvironmentVariableW(
      L"RBBLITZ_MESSAGES_BROWSER_RECEIPT", path, kPathChars);
  if (length == 0 || length >= kPathChars) return 3;
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[1], -1,
                                        nullptr, 0, nullptr, nullptr);
  if (count <= 1) return 4;
  std::string url(static_cast<size_t>(count), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[1], -1,
                          url.data(), count, nullptr, nullptr) != count) return 4;
  std::ofstream receipt(std::filesystem::path(path), std::ios::binary | std::ios::app);
  if (!receipt) return 5;
  receipt.write(url.data(), count - 1);
  receipt.put('\n');
  return receipt ? 0 : 6;
}
