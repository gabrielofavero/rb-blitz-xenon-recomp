// SPDX-License-Identifier: GPL-2.0-only
#include "platform/song_load_monitor.h"
#include "platform/game_restart.h"
#include <charconv>
#include <fstream>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rb_blitz::platform::song_load {
bool ShouldRestart(uint32_t code, bool loading) {
  // Windows fatal exception statuses and CRT abort. Ordinary application error
  // codes and a user terminating the task are not evidence of a load crash.
  const bool fatal = (code & 0xFFFF0000u) == 0xC0000000u || code == 0xE06D7363u ||
                     code == 0x80000003u || code == 0x40000015u || code == 3;
  return loading && fatal;
}

std::vector<std::string> RecoveryArguments(const std::vector<std::string>& arguments) {
  std::vector<std::string> out;
  for (const auto& arg : arguments) {
    const auto key = std::string_view(arg).substr(0, arg.find('='));
    if (key == "--song_load_recovered" || key == "--song_load_test_crash" ||
        key == "--no-song_load_test_crash" || key == "--restart-bootstrap" ||
        key == "--restart-handoff" || key == "--enhancements_skip_offline_dialog" ||
        key == "--no-enhancements_skip_offline_dialog") continue;
    out.push_back(arg);
  }
  // These overrides last for this recovery boot only. Discovery still runs; it
  // must rebuild a valid guest session before the main menu and its popup.
  // Keep background music arguments intact so recovery honors the user's setting.
  out.push_back("--song_load_recovered=true");
  out.push_back("--enhancements_skip_offline_dialog=true");
  return out;
}

#if defined(_WIN32)
namespace {
constexpr uint64_t kMagic = 0x5242425A4C473031ull;
struct Shared {
  uint64_t magic = kMagic;
  uint32_t parent = 0;
  volatile LONG loading = 0;
};
std::wstring Wide(const std::string& text) {
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(count, L'\0');
  if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), count);
  return out;
}
std::string Utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}
bool Spawn(const std::filesystem::path& executable, const std::vector<std::string>& args,
           HANDLE* inherited, size_t count, PROCESS_INFORMATION* process) {
  auto command = Wide(BuildCommandLine(executable, args));
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = count ? sizeof(startup) : sizeof(startup.StartupInfo);
  SIZE_T bytes = 0;
  std::vector<uint8_t> storage;
  if (count) {
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    storage.resize(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return false;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    inherited, count * sizeof(HANDLE), nullptr, nullptr)) {
      DeleteProcThreadAttributeList(startup.lpAttributeList);
      return false;
    }
  }
  const bool ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, count != 0,
      CREATE_UNICODE_ENVIRONMENT | (count ? EXTENDED_STARTUPINFO_PRESENT : 0),
      nullptr, nullptr, &startup.StartupInfo, process) != FALSE;
  if (count) DeleteProcThreadAttributeList(startup.lpAttributeList);
  return ok;
}
}

struct Session::Impl {
  HANDLE mapping = nullptr, process = nullptr;
  Shared* shared = nullptr;
  uint32_t pid = 0;
  ~Impl() {
    if (shared) UnmapViewOfFile(shared);
    if (mapping) CloseHandle(mapping);
    if (process) CloseHandle(process);
  }
};
Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;
void Session::SetLoading(bool loading) {
  if (impl_->shared) InterlockedExchange(&impl_->shared->loading, loading ? 1 : 0);
}
uint32_t Session::monitor_pid() const { return impl_->pid; }

bool Session::Start(const std::filesystem::path& monitor, const std::vector<std::string>& args,
                     const std::filesystem::path& log, std::string* error) {
  if (impl_->mapping) { if (error) *error = "monitor already configured"; return false; }
  auto state = std::make_unique<Impl>();
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
  state->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, sizeof(Shared), nullptr);
  if (state->mapping)
    state->shared = static_cast<Shared*>(MapViewOfFile(state->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId());
  HANDLE ready = CreateEventW(&attributes, TRUE, FALSE, nullptr);
  if (!state->shared || !parent || !ready) {
    if (parent) CloseHandle(parent);
    if (ready) CloseHandle(ready);
    if (error) *error = "cannot create monitor handoff";
    return false;
  }
  state->shared->magic = kMagic;
  state->shared->parent = GetCurrentProcessId();
  InterlockedExchange(&state->shared->loading, 0);
  std::vector<std::string> command{
      "--parent=" + std::to_string(reinterpret_cast<uintptr_t>(parent)),
      "--state=" + std::to_string(reinterpret_cast<uintptr_t>(state->mapping)),
      "--ready=" + std::to_string(reinterpret_cast<uintptr_t>(ready)),
      "--log=" + Utf8(log), "--"};
  command.insert(command.end(), args.begin(), args.end());
  HANDLE handles[] = {parent, state->mapping, ready};
  PROCESS_INFORMATION child{};
  const bool started = Spawn(monitor, command, handles, 3, &child);
  CloseHandle(parent);
  if (!started) {
    CloseHandle(ready);
    if (error) *error = "cannot start crash monitor (Windows error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(child.hThread);
  HANDLE signals[] = {ready, child.hProcess};
  const DWORD wait = WaitForMultipleObjects(2, signals, FALSE, 10000);
  CloseHandle(ready);
  if (wait != WAIT_OBJECT_0) {
    // The page remains disarmed. An initializing monitor may still finish and
    // wait for this parent, but cannot restart it without an active load.
    CloseHandle(child.hProcess);
    if (error) *error = "crash monitor did not acknowledge readiness";
    return false;
  }
  state->process = child.hProcess;
  state->pid = child.dwProcessId;
  impl_ = std::move(state);
  return true;
}

int RunMonitor(const std::vector<std::string>& args) {
  if (args.size() < 5 || !args[0].starts_with("--parent=") ||
      !args[1].starts_with("--state=") || !args[2].starts_with("--ready=") ||
      !args[3].starts_with("--log=") || args[4] != "--") return 2;
  const auto handle = [](std::string_view value) -> HANDLE {
    uintptr_t number = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (ec != std::errc{} || end != value.data() + value.size() || number == 0 || number == UINTPTR_MAX) return nullptr;
    return reinterpret_cast<HANDLE>(number);
  };
  HANDLE parent = handle(std::string_view(args[0]).substr(9));
  HANDLE mapping = handle(std::string_view(args[1]).substr(8));
  HANDLE ready = handle(std::string_view(args[2]).substr(8));
  if (!parent || !mapping || !ready) return 2;
  auto* state = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  if (!state || state->magic != kMagic || state->parent != GetProcessId(parent)) {
    if (state) UnmapViewOfFile(state);
    return 2;
  }
  std::wstring image(32768, L'\0');
  DWORD length = DWORD(image.size());
  if (!QueryFullProcessImageNameW(parent, 0, image.data(), &length)) return 2;
  image.resize(length);
  if (!SetEvent(ready)) return 2;
  CloseHandle(ready);
  if (WaitForSingleObject(parent, INFINITE) != WAIT_OBJECT_0) return 2;
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(parent, &exit_code)) return 2;
  const bool loading = InterlockedCompareExchange(&state->loading, 0, 0) == 1;
  UnmapViewOfFile(state);
  CloseHandle(mapping);
  CloseHandle(parent);
  const bool recover = ShouldRestart(exit_code, loading);
  std::ofstream log(std::filesystem::path(Wide(args[3].substr(6))), std::ios::app);
  log << "parent_exit=" << std::hex << exit_code << " loading=" << loading << " restart=" << recover << '\n';
  log.flush();
  if (!recover) return 0;
  PROCESS_INFORMATION replacement{};
  const auto recovery = RecoveryArguments({args.begin() + 5, args.end()});
  if (!Spawn(std::filesystem::path(image), recovery, nullptr, 0, &replacement)) {
    log << "restart_failed=" << GetLastError() << '\n';
    return 3;
  }
  log << "replacement_pid=" << std::dec << replacement.dwProcessId << '\n';
  CloseHandle(replacement.hThread);
  CloseHandle(replacement.hProcess);
  return 0;
}
#else
struct Session::Impl {};
Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;
bool Session::Start(const std::filesystem::path&, const std::vector<std::string>&,
                    const std::filesystem::path&, std::string* error) {
  if (error) *error = "crash restart is supported on Windows only";
  return false;
}
void Session::SetLoading(bool) {}
uint32_t Session::monitor_pid() const { return 0; }
int RunMonitor(const std::vector<std::string>&) { return 2; }
#endif
}  // namespace rb_blitz::platform::song_load
