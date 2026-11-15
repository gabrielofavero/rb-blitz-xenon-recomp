// SPDX-License-Identifier: GPL-2.0-only
// Exercises the actual shared runtime instrument, without a GPU or guest/UI.
#include "check.h"
#include <rex/cvar.h>
#include <rex/graphics/compile_trace.h>
#include <rex/logging.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace t = rex::graphics::compile_trace;
int main(int argc, char **argv) {
  std::ostringstream captured;
  rex::LogConfig logging;
  logging.extra_sinks.push_back(std::make_shared<spdlog::sinks::ostream_sink_mt>(captured));
  rex::InitLogging(logging);
  const bool use_default = argc <= 1;
  const bool enabled = use_default || std::string_view(argv[1]) == "enabled";
  if (use_default) {
    CHECK_TRUE(rex::cvar::GetFlagByName("shader_compile_trace") == "true");
    CHECK_TRUE(rex::cvar::GetFlagSource("shader_compile_trace") ==
               rex::cvar::Source::kDefault);
  } else {
    CHECK_TRUE(rex::cvar::SetFlagByName("shader_compile_trace",
                                        enabled ? "true" : "false"));
  }
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
  // Non-compilation spans retain only long operations, including failures.
  for (auto event : {t::Event::kGpuOperation, t::Event::kGuestWork, t::Event::kFileRead,
                     t::Event::kGpuIdle, t::Event::kPresent}) {
    for (uint64_t duration : {49999u, 50000u, 100000u, 250000u})
      t::Record(event, t::Phase::kActiveSong, "resource", 0, 10,
                10 + duration, duration != 100000);
    t::Record(event, t::Phase::kActiveSong, "bad", 0, 10, 9, true);
    const auto operations = t::ReadCounter(t::Phase::kActiveSong, event);
    CHECK_EQ(operations.count, enabled ? 3 : 0);
    CHECK_EQ(operations.failed, enabled ? 1 : 0);
    CHECK_EQ(operations.total_us, enabled ? 400000 : 0);
    CHECK_EQ(operations.ge50, enabled ? 3 : 0);
    CHECK_EQ(operations.ge100, enabled ? 2 : 0);
    CHECK_EQ(operations.ge250, enabled ? 1 : 0);
  }
  // Exhaustion in an earlier screen must not hide the next song. Cumulative
  // counts retain the old suppression, while the next detail is admitted.
  t::SetPhase(t::Phase::kActiveSong, "next song");
  t::Call(t::Event::kHelperCompute, "next_song", 0, [] { return 0; });
  const auto next_song = t::ReadCounter(t::Phase::kActiveSong, t::Event::kHelperCompute);
  CHECK_EQ(next_song.count, enabled ? 1 : 0);
  CHECK_EQ(next_song.suppressed, 0);
  t::Record(t::Event::kFileRead, t::Phase::kActiveSong, "songs\\Bounce \"Man\"\n.mogg",
            0, 10, 50010, true);
  rex::FlushLogging();
  const auto output = captured.str();
  CHECK_EQ(output.find("site=\"songs\\\\Bounce \\\"Man\\\"\\u000a.mogg\"") != std::string::npos,
           enabled);
  CHECK_EQ(output.find("site=\"next_song\"") != std::string::npos, enabled);
  rex::ShutdownLogging();
  return rb_blitz::test::Finish();
}
