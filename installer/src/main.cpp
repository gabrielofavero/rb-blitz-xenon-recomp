// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Entry point of rb_blitz_setup_helper.exe.
//
// The command line arrives as UTF-16 because an install directory may contain
// characters the ANSI code page cannot represent ("C:\Users\Jörg\..."), so it is
// converted to UTF-8 once, here, and the rest of the helper works in UTF-8.
// Output goes straight back out as UTF-8 bytes.

#include <windows.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>


#include <exception>
#include <string>
#include <vector>

#include "commands.h"
#include "util.h"

namespace {

using rb_blitz::installer::Narrow;

// The helper's output is read in three places: piped into a file below
// net-local\logs when something goes wrong, shown in a terminal when a person
// runs it by hand, and shown in the wizard's own console window while
// `/DEBUG`-style logging is on. UTF-8 with binary stdio is the one encoding that
// survives all three, and it keeps the "\r\n" the usage text already contains
// from being turned into "\r\r\n" by the CRT.
void UseUtf8Console() {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
}

// The helper is a console application, so Windows gives it a console window even
// when the wizard starts it with SW_HIDE. The wizard runs it several times per
// install, and a console flashing up every time looks broken, so the window is
// hidden again when the caller asked for it. A person who runs the helper from a
// terminal keeps their console, because then the startup information says
// "show normally" (or says nothing at all).
void HideOwnConsoleWhenHidden() {
  const HWND console = GetConsoleWindow();
  if (console == nullptr) {
    return;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  GetStartupInfoW(&startup);
  if ((startup.dwFlags & STARTF_USESHOWWINDOW) != 0 && startup.wShowWindow == SW_HIDE) {
    ShowWindow(console, SW_HIDE);
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  UseUtf8Console();
  HideOwnConsoleWhenHidden();

  std::vector<std::string> args;
  args.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
  for (int index = 1; index < argc; ++index) {
    args.push_back(Narrow(argv[index]));
  }

  try {
    return rb_blitz::installer::RunCommand(args);
  } catch (const std::exception& exception) {
    const std::string reason =
        rb_blitz::installer::S("unhandled failure: ", exception.what());
    rb_blitz::installer::ReportUnhandledFailure(args, reason);
    std::fprintf(stderr, "error: %s\n", reason.c_str());
    return 1;
  } catch (...) {
    rb_blitz::installer::ReportUnhandledFailure(args, "unhandled failure");
    std::fputs("error: unhandled failure\n", stderr);
    return 1;
  }
}
