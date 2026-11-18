// SPDX-License-Identifier: GPL-2.0-only
// Exercise the installed public trace API across SDK sessions without a GPU or title.
#include <rex/diagnostics/compile_trace.h>
#include <rex/init.h>
#include <rex/settings_registry.h>

#include <cstdio>

int main() {
  namespace trace = rex::diagnostics::compile_trace;
  for (unsigned session = 0; session < 2; ++session) {
    if (!rex::Init({})) return 1;
    if (!trace::Enabled()) {
      rex::Shutdown();
      return 2;
    }
    const auto before = trace::ReadCounter(trace::Phase::kMenu, trace::Event::kGuestWork);
    trace::Record(trace::Event::kGuestWork, trace::Phase::kMenu, "installed consumer", 0,
                  10, 50010, true);
    const auto after = trace::ReadCounter(trace::Phase::kMenu, trace::Event::kGuestWork);
    if (after.count != before.count + 1 || after.total_us != before.total_us + 50000) {
      rex::Shutdown();
      return 3;
    }
    if (!REX_SETTINGS().SetText("shader_compile_trace", "false") || trace::Enabled()) {
      rex::Shutdown();
      return 4;
    }
    rex::Shutdown();
  }
  std::puts("Installed trace exports, legacy alias and SDK session reset: pass");
}
