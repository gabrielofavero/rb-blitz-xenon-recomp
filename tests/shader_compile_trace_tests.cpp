// SPDX-License-Identifier: GPL-2.0-only
// Exercises the actual shared runtime instrument, without a GPU or guest/UI.
#include "check.h"
#include <rex/cvar.h>
#include <rex/graphics/compile_trace.h>
#include <string_view>
#include <thread>
#include <vector>

namespace t = rex::graphics::compile_trace;
int main(int argc, char **argv) {
  const bool enabled = argc > 1 && std::string_view(argv[1]) == "enabled";
  CHECK_TRUE(rex::cvar::SetFlagByName("shader_compile_trace",
                                      enabled ? "true" : "false"));
  CHECK_EQ(t::Enabled(), enabled);
  t::SetPhase(t::Phase::kPostALoading, "test");
  CHECK_EQ(t::CurrentPhase(),
           enabled ? t::Phase::kPostALoading : t::Phase::kBoot);
  {
    t::Scope crossing(t::Event::kTranslation, "cross_phase", 0x1234);
    t::SetPhase(t::Phase::kMenu, "test");
    crossing.Success();
  }
  CHECK_EQ(
      t::ReadCounter(t::Phase::kPostALoading, t::Event::kTranslation).count,
      enabled ? 1 : 0);
  CHECK_EQ(t::Call(t::Event::kGuestPipeline, "failure", 1, [] { return -1; }),
           -1);
  CHECK_EQ(
      t::Call(t::Event::kPlaceholder, "compile_required", 2, [] { return 1; }),
      1);
  CHECK_EQ(t::ReadCounter(t::Phase::kMenu, t::Event::kGuestPipeline).failed,
           enabled ? 1 : 0);
  CHECK_EQ(t::ReadCounter(t::Phase::kMenu, t::Event::kPlaceholder).failed,
           enabled ? 1 : 0);
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i)
    workers.emplace_back([] {
      for (int n = 0; n < 600; ++n)
        t::Call(t::Event::kHelperCompute, "concurrent", 0, [] { return 0; });
    });
  for (auto &worker : workers)
    worker.join();
  const auto concurrent =
      t::ReadCounter(t::Phase::kMenu, t::Event::kHelperCompute);
  CHECK_EQ(concurrent.count, enabled ? 2400 : 0);
  CHECK_EQ(concurrent.failed, 0);
  CHECK_EQ(concurrent.suppressed, enabled ? 352 : 0);
  // Artificial timestamps test thresholds and invalid-duration rejection
  // without sleeping.
  for (uint64_t duration : {49999u, 50000u, 100000u, 250000u})
    t::Record(t::Event::kGpuFrame, t::Phase::kActiveSong, "fixture", 0, 10,
              10 + duration, true);
  t::Record(t::Event::kGpuFrame, t::Phase::kActiveSong, "bad", 0, 10, 9, true);
  const auto frames =
      t::ReadCounter(t::Phase::kActiveSong, t::Event::kGpuFrame);
  CHECK_EQ(frames.count, enabled ? 4 : 0);
  CHECK_EQ(frames.ge50, enabled ? 3 : 0);
  CHECK_EQ(frames.ge100, enabled ? 2 : 0);
  CHECK_EQ(frames.ge250, enabled ? 1 : 0);
  CHECK_EQ(frames.max_us, enabled ? 250000 : 0);
  return rb_blitz::test::Finish();
}
