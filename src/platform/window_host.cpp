// SPDX-License-Identifier: GPL-2.0-only
#include "platform/window_host.h"

#include <charconv>
#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "platform/game_restart.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rb_blitz::platform::window_host {

std::string Encode(const Identity& identity) {
  char text[80];
  std::snprintf(text, sizeof(text), "%x.%llx.%llx", identity.pid,
                static_cast<unsigned long long>(identity.created),
                static_cast<unsigned long long>(identity.window));
  return text;
}

bool Decode(std::string_view text, Identity* out) {
  if (!out || text.size() > 50) return false;
  uint64_t fields[3]{};
  for (int i = 0; i < 3; ++i) {
    const auto dot = text.find('.');
    const auto field = text.substr(0, dot);
    if (field.empty() || field.size() > 16 || (i < 2) != (dot != text.npos)) return false;
    for (char c : field) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    const auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), fields[i], 16);
    if (ec != std::errc{} || end != field.data() + field.size() || fields[i] == 0) return false;
    if (dot != text.npos) text.remove_prefix(dot + 1);
  }
  if (fields[0] > UINT32_MAX) return false;
  *out = {static_cast<uint32_t>(fields[0]), fields[1], fields[2]};
  return true;
}

#if defined(_WIN32)
namespace {
constexpr wchar_t kClass[] = L"RbBlitzPersistentWindow";
constexpr ULONG_PTR kProtocol = 0x52425731;
enum class Operation : uint32_t { kRegister, kAttach, kDisplay, kTitle, kExpectRestart };
struct Request {
  Operation operation{};
  uint32_t pid = 0;
  uint64_t parent_created = 0;
  uint32_t parent_pid = 0;
  uint32_t replacement_pid = 0;
  int32_t mode = 0;
  uint64_t window = 0;
  RECT bounds{};
  uint32_t resizable = 0;
  wchar_t title[256]{};
};

// Bootstrap uses only the trivial pointers. The focus callback is installed
// later, once the SDK has created the rendering window.
HWND client_host = nullptr;
HWND client_window = nullptr;
WNDPROC client_procedure = nullptr;
std::function<void(bool)> client_focus_changed;

LRESULT CALLBACK ClientProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_APP + 0x427) {
    SetFocus(window);
    return 0;
  }
  const auto result = CallWindowProcW(client_procedure, window, message, wparam, lparam);
  if (client_focus_changed && (message == WM_SETFOCUS || message == WM_KILLFOCUS))
    client_focus_changed(message == WM_SETFOCUS);
  if (message == WM_NCDESTROY) client_focus_changed = {};
  return result;
}

std::wstring Wide(std::string_view text) {
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(count, L'\0');
  if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), count);
  return out;
}

std::filesystem::path ImagePath(HANDLE process) {
  std::wstring path(32768, L'\0');
  DWORD length = DWORD(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return {};
  path.resize(length);
  return path;
}

bool Send(Request request) {
  request.pid = GetCurrentProcessId();
  COPYDATASTRUCT data{kProtocol, sizeof(request), &request};
  DWORD_PTR result = 0;
  return client_host && SendMessageTimeoutW(client_host, WM_COPYDATA, 0,
      reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000, &result) && result == 1;
}

bool RecoverableExit(DWORD code) {
  return (code & 0xFFFF0000u) == 0xC0000000u || code == 0xE06D7363u ||
         code == 0x80000003u || code == 0x40000015u || code == 3 || code == 0xE042424Cu;
}

struct Host {
  HWND window = nullptr, child = nullptr;
  HANDLE process = nullptr, pending = nullptr, job = nullptr;
  DWORD pid = 0;
  bool closing = false, configured = false;
  int mode = 0;
  RECT windowed{};
  DWORD windowed_style = WS_OVERLAPPEDWINDOW;
  ULONGLONG exit_deadline = 0;

  ~Host() {
    if (process) CloseHandle(process);
    if (pending) CloseHandle(pending);
    // The job also contains the game's load monitor and any recovery child.
    if (job) CloseHandle(job);
  }

  bool Advance() {
    if (closing || !pending || WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
    CloseHandle(process);
    process = pending;
    pending = nullptr;
    pid = GetProcessId(process);
    child = nullptr;
    exit_deadline = 0;
    return true;
  }

  bool Register(const Request& request) {
    if (closing) return false;
    if (request.pid == pid) return true;
    if (pending) return GetProcessId(pending) == request.pid;
    DWORD code = 0;
    const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    if (request.parent_pid) {
      uint64_t created = 0;
      if (request.parent_pid != pid || !ProcessCreatedTicks(pid, &created) ||
          created != request.parent_created) return false;
    } else if (!exited || !GetExitCodeProcess(process, &code) || !RecoverableExit(code)) {
      return false;
    }
    HANDLE replacement = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION |
                                      PROCESS_TERMINATE | PROCESS_SET_QUOTA, FALSE, request.pid);
    if (!replacement) return false;
    BOOL in_job = FALSE;
    const bool valid = ImagePath(replacement) == ImagePath(GetCurrentProcess()) &&
        IsProcessInJob(replacement, job, &in_job) &&
        (in_job || AssignProcessToJobObject(job, replacement));
    if (!valid) { CloseHandle(replacement); return false; }
    pending = replacement;
    return true;
  }

  bool Display(int requested) {
    if (requested < 0 || requested > 2 || !child) return false;
    // A replacement boot must not restore a minimized window or discard a
    // maximized placement just to reapply the already active display mode.
    if (configured && mode == requested) return true;
    if (mode == 0 && configured) GetWindowRect(window, &windowed);
    RECT target = windowed;
    DWORD style = windowed_style | WS_CLIPCHILDREN;
    if (requested != 0) {
      MONITORINFO info{sizeof(info)};
      const auto monitor = configured ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
                                      : MonitorFromRect(&windowed, MONITOR_DEFAULTTONEAREST);
      if (!GetMonitorInfoW(monitor, &info)) return false;
      target = info.rcMonitor;
      style = WS_POPUP | WS_CLIPCHILDREN;
    }
    SetLastError(0);
    if (!SetWindowLongPtrW(window, GWL_STYLE, style) && GetLastError()) return false;
    if (!SetWindowPos(window, nullptr, target.left, target.top, target.right - target.left,
                      target.bottom - target.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE)) return false;
    mode = requested;
    configured = true;
    // Windows' fullscreen mode in this project is desktop fullscreen; neither
    // display mode switches nor an exclusive swap chain are needed here.
    return true;
  }

  LRESULT Receive(const Request& request) {
    // A replacement may finish its parent wait and send Attach while this
    // thread is still draining messages from the previous game generation.
    Advance();
    if (request.operation == Operation::kRegister) return Register(request);
    if (closing || request.pid != pid) return 0;
    if (request.operation == Operation::kExpectRestart) {
      Request replacement = request;
      replacement.pid = request.replacement_pid;
      replacement.parent_pid = pid;
      return Register(replacement);
    }
    if (request.operation == Operation::kAttach) {
      HWND incoming = reinterpret_cast<HWND>(request.window);
      DWORD owner = 0;
      GetWindowThreadProcessId(incoming, &owner);
      if (!incoming || owner != pid || GetParent(incoming) != window) return 0;
      child = incoming;
      windowed_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
      if (request.resizable) windowed_style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
      // Keep the player's placement across every boot. A changed resolution
      // changes the render surface while the persistent window keeps its size.
      if (!configured) {
        windowed = request.bounds;
        RECT client{};
        GetClientRect(incoming, &client);
        RECT frame{0, 0, client.right, client.bottom};
        AdjustWindowRectExForDpi(&frame, windowed_style, FALSE, 0, GetDpiForWindow(window));
        windowed.right = windowed.left + frame.right - frame.left;
        windowed.bottom = windowed.top + frame.bottom - frame.top;
        HMONITOR monitor = MonitorFromRect(&request.bounds, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) {
          const int width = windowed.right - windowed.left, height = windowed.bottom - windowed.top;
          windowed.left = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
          windowed.top = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
          windowed.right = windowed.left + width;
          windowed.bottom = windowed.top + height;
        }
      }
      return 1;
    }
    if (request.operation == Operation::kDisplay) {
      if (!Display(request.mode)) return 0;
      if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOW);
      if (GetForegroundWindow() == window) PostMessageW(child, WM_APP + 0x427, 0, 0);
      return 1;
    }
    if (request.operation == Operation::kTitle) {
      if (request.title[255]) return 0;
      SetWindowTextW(window, request.title);
      return 1;
    }
    return 0;
  }
};

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    host = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    host->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
  }
  if (!host) return DefWindowProcW(window, message, wparam, lparam);
  switch (message) {
    case WM_COPYDATA: {
      const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
      if (!data || data->dwData != kProtocol || data->cbData != sizeof(Request) || !data->lpData) return 0;
      return host->Receive(*static_cast<const Request*>(data->lpData));
    }
    case WM_SIZE:
      if (host->child && wparam != SIZE_MINIMIZED) {
        RECT client{};
        GetClientRect(window, &client);
        // Never wait on the game's UI thread from the host: it may itself be
        // waiting for a display request to return from this thread.
        SetWindowPos(host->child, nullptr, 0, 0, client.right, client.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
      }
      return 0;
    case WM_SETFOCUS:
      if (host->child) PostMessageW(host->child, WM_APP + 0x427, 0, 0);
      return 0;
    case WM_CLOSE:
      host->closing = true;
      host->exit_deadline = GetTickCount64() + 5000;
      if (host->child) PostMessageW(host->child, WM_CLOSE, 0, 0);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

int RunHost(const std::vector<std::string>& arguments) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  Host host;
  host.job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!host.job || !SetInformationJobObject(host.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 2;
  WNDCLASSW klass{};
  klass.lpfnWndProc = WindowProcedure;
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  klass.hIcon = LoadIconW(klass.hInstance, MAKEINTRESOURCEW(1));
  klass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  klass.lpszClassName = kClass;
  if (!RegisterClassW(&klass)) return 2;
  HWND window = CreateWindowExW(0, kClass, L"Rock Band Blitz", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, klass.hInstance, &host);
  if (!window) return 2;
  auto child_arguments = std::vector<std::string>(arguments.begin() + 1, arguments.end());
  child_arguments.push_back(std::string(kSwitch) + Encode({GetCurrentProcessId(), ThisProcessCreatedTicks(),
                                                          reinterpret_cast<uint64_t>(window)}));
  const auto executable = ImagePath(GetCurrentProcess());
  auto command = Wide(BuildCommandLine(executable, child_arguments));
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                       nullptr, nullptr, &startup, &child)) { DestroyWindow(window); return 2; }
  host.process = child.hProcess;
  host.pid = child.dwProcessId;
  if (!AssignProcessToJobObject(host.job, child.hProcess)) {
    TerminateProcess(child.hProcess, 2);
    CloseHandle(child.hThread);
    DestroyWindow(window);
    return 2;
  }
  ResumeThread(child.hThread);
  CloseHandle(child.hThread);
  DWORD result = 0;
  bool running = true;
  while (running) {
    MsgWaitForMultipleObjects(1, &host.process, FALSE, 50, QS_ALLINPUT);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) { running = false; break; }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running) break;
    if (WaitForSingleObject(host.process, 0) == WAIT_OBJECT_0) {
      GetExitCodeProcess(host.process, &result);
      if (host.Advance()) {
        continue;
      } else if (!host.closing && RecoverableExit(result)) {
        // The existing load monitor may be starting its recovery process.
        if (!host.exit_deadline) host.exit_deadline = GetTickCount64() + 10000;
        if (GetTickCount64() >= host.exit_deadline) break;
        // A signaled process would make MsgWait spin throughout this grace.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
      } else break;
    }
    if (host.closing && GetTickCount64() >= host.exit_deadline) break;
  }
  DestroyWindow(window);
  return static_cast<int>(result);
}
} // namespace

void Bootstrap(const std::vector<std::string>& arguments) {
  Identity identity;
  bool hosted = false;
  for (const auto& argument : arguments) {
    if (!std::string_view(argument).starts_with(kSwitch)) continue;
    if (hosted || !Decode(std::string_view(argument).substr(kSwitch.size()), &identity)) ExitProcess(2);
    hosted = true;
  }
  if (!hosted) ExitProcess(RunHost(arguments));
  uint64_t created = 0;
  client_host = reinterpret_cast<HWND>(identity.window);
  DWORD pid = 0;
  wchar_t name[64]{};
  GetWindowThreadProcessId(client_host, &pid);
  GetClassNameW(client_host, name, 64);
  if (pid != identity.pid || !ProcessCreatedTicks(pid, &created) || created != identity.created ||
      std::wstring_view(name) != kClass) ExitProcess(2);
  Request request{};
  std::string error;
  std::vector<const char*> argv;
  for (const auto& argument : arguments) argv.push_back(argument.c_str());
  const auto handoff = HandoffFromArguments(int(argv.size()), argv.data(), &error);
  if (!error.empty()) ExitProcess(2);
  if (handoff) {
    request.parent_pid = handoff->parent_pid;
    request.parent_created = handoff->parent_created;
  }
  if (!Send(request)) ExitProcess(2);
}

bool ExpectRestart(uint32_t replacement_pid) {
  if (!client_host) return true;
  Request request{};
  request.operation = Operation::kExpectRestart;
  request.replacement_pid = replacement_pid;
  request.parent_created = ThisProcessCreatedTicks();
  return Send(request);
}

bool Attach(void* native_window, bool resizable, std::function<void(bool)> focus_changed) {
  if (!client_host) return true;
  client_window = static_cast<HWND>(native_window);
  Request request{};
  request.operation = Operation::kAttach;
  request.window = reinterpret_cast<uint64_t>(client_window);
  GetWindowRect(client_window, &request.bounds);
  const auto style = GetWindowLongPtrW(client_window, GWL_STYLE);
  request.resizable = resizable;
  SetWindowLongPtrW(client_window, GWL_STYLE,
      (style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CHILD);
  SetWindowLongPtrW(client_window, GWL_EXSTYLE, GetWindowLongPtrW(client_window, GWL_EXSTYLE) & ~WS_EX_APPWINDOW);
  SetLastError(0);
  if (!SetParent(client_window, client_host) && GetLastError()) return false;
  client_procedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
      client_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ClientProcedure)));
  if (!client_procedure) return false;
  client_focus_changed = std::move(focus_changed);
  return Send(request);
}

bool SetDisplayMode(int mode) {
  Request request{};
  request.operation = Operation::kDisplay;
  request.mode = mode;
  if (!Send(request)) return false;
  RECT client{};
  return GetClientRect(client_host, &client) && SetWindowPos(client_window, nullptr, 0, 0,
      client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void SetTitle(std::string_view title) {
  Request request{};
  request.operation = Operation::kTitle;
  const auto wide = Wide(title);
  const auto count = (std::min)(wide.size(), size_t(255));
  std::copy_n(wide.data(), count, request.title);
  Send(request);
}
bool Active() { return client_host != nullptr; }
#else
void Bootstrap(const std::vector<std::string>&) {}
bool ExpectRestart(uint32_t) { return true; }
bool Attach(void*, bool, std::function<void(bool)>) { return true; }
bool SetDisplayMode(int) { return false; }
void SetTitle(std::string_view) {}
bool Active() { return false; }
#endif

} // namespace rb_blitz::platform::window_host
