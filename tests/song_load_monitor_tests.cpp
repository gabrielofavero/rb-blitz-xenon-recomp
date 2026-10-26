// SPDX-License-Identifier: GPL-2.0-only
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "check.h"
#include "platform/song_load_monitor.h"
#include "platform/game_restart.h"
#include "loading/crash_dialog_state.h"
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;
using namespace rb_blitz;
using namespace test;
std::wstring Wide(const std::string& text) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
  return out;
}
bool WaitFile(const fs::path& path) {
  for (unsigned i = 0; i < 500; ++i) {
    if (fs::exists(path) && fs::file_size(path)) return true;
    Sleep(10);
  }
  return false;
}
int main(int argc, char** argv) {
  using namespace platform::song_load;
  if (argc > 1 && std::string_view(argv[1]) == "--parent-case") {
    const fs::path report(Wide(argv[2]));
    const std::vector<std::string> arguments(argv + 1, argv + argc);
    if (std::find(arguments.begin(), arguments.end(), "--song_load_recovered=true") != arguments.end()) {
      std::ofstream result(report.string() + ".recovered");
      result << "new process " << GetCurrentProcessId();
      for (const auto& arg : arguments) result << '\n' << arg;
      return 0;
    }
    Session session;
    std::string error;
    if (!session.Start(fs::path(Wide(argv[5])), arguments, report.string() + ".log", &error,
                       std::string_view(argv[9]) == "1")) return 20;
    const auto timeout_ms = static_cast<uint32_t>(std::stoul(argv[7]));
    session.SetLoading(std::string_view(argv[4]) == "1", timeout_ms);
    std::ofstream(report.string() + ".started") << session.monitor_pid();
    const auto gate = reinterpret_cast<HANDLE>(std::stoull(argv[6]));
    if (WaitForSingleObject(gate, 5000) != WAIT_OBJECT_0) return 21;
    const std::string_view mode(argv[8]);
    if (mode == "hold") Sleep(INFINITE);
    if (mode == "complete" || mode == "reenter") {
      session.SetLoading(false);
      if (mode == "reenter") session.SetLoading(true);
      Sleep(timeout_ms + 200);
    }
    ExitProcess(static_cast<UINT>(std::stoull(argv[3])));
  }
  if (argc != 2) return 2;
  BeginCase("only a fatal process exit during an armed load is recoverable");
  CHECK_FALSE(ShouldRestart(0, true));
  CHECK_FALSE(ShouldRestart(0xC0000005, false));
  CHECK_FALSE(ShouldRestart(1, true));
  CHECK_FALSE(ShouldRestart(0xFFFFFFFF, true));
  CHECK_TRUE(ShouldRestart(0xC0000005, true));
  CHECK_TRUE(ShouldRestart(0xC0000409, true));
  BeginCase("absolute 30s and 60s boundaries use injected time; zero disables expiry");
  for (const uint64_t duration : {30000ull, 60000ull}) {
    CHECK_FALSE(DeadlineExpired(100 + duration - 1, 100 + duration));
    CHECK_TRUE(DeadlineExpired(100 + duration, 100 + duration));
    CHECK_TRUE(DeadlineExpired(100 + duration + 1, 100 + duration));
  }
  CHECK_FALSE(DeadlineExpired(UINT64_MAX, 0));
  auto args = RecoveryArguments({"--song_load_test_crash=true", "--restart-bootstrap",
      "--restart-handoff=old", "--gpu_backend=vulkan", "--song_load_recovered=true",
      "--enhancements_menu_dlc_songs=true"});
  CHECK_TRUE(std::find(args.begin(), args.end(), "--song_load_test_crash=true") == args.end());
  CHECK_EQ(std::count(args.begin(), args.end(), "--song_load_recovered=true"), 1);
  CHECK_TRUE(std::find(args.begin(), args.end(), "--gpu_backend=vulkan") != args.end());
  BeginCase("recovery preserves background music settings without forcing an override");
  CHECK_EQ(std::count(args.begin(), args.end(), "--enhancements_menu_dlc_songs=true"), 1);
  CHECK_TRUE(std::find(args.begin(), args.end(), "--enhancements_menu_dlc_songs=false") == args.end());
  const auto music_off = RecoveryArguments({"--enhancements_menu_dlc_songs=false"});
  CHECK_EQ(std::count(music_off.begin(), music_off.end(), "--enhancements_menu_dlc_songs=false"), 1);
  const auto music_default = RecoveryArguments({});
  CHECK_TRUE(std::none_of(music_default.begin(), music_default.end(), [](const auto& arg) {
    return arg.starts_with("--enhancements_menu_dlc_songs=");
  }));
  CHECK_EQ(RunMonitor({"--parent=garbage", "--state=1", "--ready=2", "--log=unused", "--"}), 2);
  const auto timeout_args = RecoveryArguments({"--song_load_timeout=60", "--song_load_test_stall=true",
                                               "--song_load_guard=30s"}, 30);
  CHECK_TRUE(std::find(timeout_args.begin(), timeout_args.end(), "--song_load_timeout=60") == timeout_args.end());
  CHECK_TRUE(std::find(timeout_args.begin(), timeout_args.end(), "--song_load_test_stall=true") == timeout_args.end());
  CHECK_EQ(std::count(timeout_args.begin(), timeout_args.end(), "--song_load_timeout=30"), 1);
  CHECK_EQ(std::count(timeout_args.begin(), timeout_args.end(), "--song_load_guard=30s"), 1);

  BeginCase("native dialog waits for the stable menu, discovery and one-shot consumption");
  loading::CrashDialogState state;
  bool answer = true;
  CHECK_FALSE(state.Query("ordinary.file", &answer));
  state.recovered = true;
  state.Query("rbbz-load/recovery/start", &answer); CHECK_FALSE(answer);
  state.Query("rbbz-load/recovery/active", &answer); CHECK_TRUE(answer);
  state.menu_ready = true;
  state.Query("rbbz-load/recovery/popup", &answer); CHECK_FALSE(answer);
  state.Query("rbbz-load/recovery/start", &answer); CHECK_TRUE(answer);
  state.Query("rbbz-load/recovery/starting", &answer); CHECK_TRUE(answer);
  state.Query("rbbz-load/recovery/start", &answer); CHECK_FALSE(answer);
  state.menu_ready = false;
  state.Query("rbbz-load/recovery/popup", &answer); CHECK_FALSE(answer);
  state.menu_ready = true;
  state.Query("rbbz-load/recovery/popup", &answer); CHECK_TRUE(answer);
  state.Query("rbbz-load/recovery/popup", &answer); CHECK_FALSE(answer);
  state.Query("rbbz-load/recovery/active", &answer); CHECK_FALSE(answer);
  for (const uint32_t seconds : {30u, 60u}) {
    state.timeout_seconds = seconds;
    state.Query("rbbz-load/recovery/timeout30", &answer); CHECK_EQ(answer, seconds == 30);
    state.Query("rbbz-load/recovery/timeout60", &answer); CHECK_EQ(answer, seconds == 60);
  }

  BeginCase("real external monitor waits for parent death and starts exactly one replacement");
  const fs::path root = fs::temp_directory_path() / ("rb load guard & " + std::to_string(GetCurrentProcessId()));
  fs::create_directories(root);
  wchar_t image[32768];
  const DWORD length = GetModuleFileNameW(nullptr, image, 32768);
  const fs::path executable(std::wstring(image, length));
  for (unsigned scenario = 0; scenario < 8; ++scenario) {
    const fs::path report = root / std::to_string(scenario);
    const bool loading = scenario != 2;
    const bool timeout = scenario == 3 || scenario == 7;
    const bool recover_crashes = scenario < 6;
    const uint32_t code = scenario == 1 || scenario == 4 || scenario == 5 ? 0 : 0xC0000005;
    const uint32_t timeout_ms = timeout || scenario == 4 || scenario == 5 ? 500 : 0;
    const std::string mode = timeout ? "hold" : scenario == 4 ? "complete" : scenario == 5 ? "reenter" : "exit";
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetHandleInformation(gate, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    auto command = Wide(platform::BuildCommandLine(executable, {"--parent-case", report.string(),
        std::to_string(code), loading ? "1" : "0", argv[1], std::to_string(reinterpret_cast<uintptr_t>(gate)),
        std::to_string(timeout_ms), mode, recover_crashes ? "1" : "0"}));
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION parent{};
    CHECK_TRUE(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                             nullptr, nullptr, &startup, &parent));
    CHECK_TRUE(WaitFile(report.string() + ".started"));
    DWORD pid = 0;
    std::ifstream(report.string() + ".started") >> pid;
    HANDLE monitor = OpenProcess(SYNCHRONIZE, FALSE, pid);
    CHECK_TRUE(monitor != nullptr);
    CHECK_FALSE(fs::exists(report.string() + ".recovered"));
    SetEvent(gate);
    CHECK_EQ(WaitForSingleObject(parent.hProcess, 5000), WAIT_OBJECT_0);
    CHECK_EQ(WaitForSingleObject(monitor, 5000), WAIT_OBJECT_0);
    if (timeout || (recover_crashes && ShouldRestart(code, loading))) {
      CHECK_TRUE(WaitFile(report.string() + ".recovered"));
      std::ifstream result(report.string() + ".recovered");
      const std::string text{std::istreambuf_iterator<char>(result), std::istreambuf_iterator<char>()};
      CHECK_EQ(text.find("--song_load_timeout=30") != std::string::npos, timeout);
    } else CHECK_FALSE(fs::exists(report.string() + ".recovered"));
    CloseHandle(gate); CloseHandle(monitor); CloseHandle(parent.hProcess); CloseHandle(parent.hThread);
  }
  // Only this test's uniquely named disposable directory is removed.
  if (g_failures == 0) fs::remove_all(root);
  else std::printf("Monitor evidence retained in %s\n", root.string().c_str());
  return Finish();
}
