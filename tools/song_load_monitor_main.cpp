// SPDX-License-Identifier: GPL-2.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "platform/song_load_monitor.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!wide) return 2;
  std::vector<std::string> args;
  for (int i = 1; i < count; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
    std::string text(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
    text.pop_back();
    args.push_back(std::move(text));
  }
  LocalFree(wide);
  return rb_blitz::platform::song_load::RunMonitor(args);
}
