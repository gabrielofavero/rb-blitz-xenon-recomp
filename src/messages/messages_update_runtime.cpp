// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_update.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "messages/messages_service.h"

REXCVAR_DEFINE_STRING(messages_browser_opener_path, "", "Diagnostics",
                      "Host-only absolute path to a browser stand-in executable for Messages "
                      "acceptance runs. It receives the fixed Releases URL as its sole argument.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::messages {
namespace {
std::unique_ptr<UpdateAction> g_action;
constexpr std::string_view kPrefix = "rbbz-messages/update/";

#if defined(_WIN32)
std::wstring Wide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  if (count <= 0) return {};
  std::wstring result(static_cast<size_t>(count), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                          static_cast<int>(utf8.size()), result.data(), count) != count)
    return {};
  return result;
}
#endif

bool OpenBrowser(const std::string& diagnostic_path, std::string_view url) {
#if defined(_WIN32)
  const std::wstring wide_url = Wide(std::string(url));
  if (wide_url.empty()) return false;
  if (!diagnostic_path.empty()) {
    const std::wstring executable = Wide(diagnostic_path);
    if (executable.empty() || executable.find(L'"') != std::wstring::npos ||
        !std::filesystem::path(executable).is_absolute()) return false;
    std::wstring command = L"\"" + executable + L"\" \"" + wide_url + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                                        &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
  }
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide_url.c_str(),
                                                  nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
  // The guest cannot supply this URL: UpdateAction passes the fixed Releases URL.
  // argv avoids shell interpretation of a diagnostic path or future URL bytes.
  const std::string executable = diagnostic_path.empty()
#if defined(__APPLE__)
      ? "open" : diagnostic_path;
#else
      ? "xdg-open" : diagnostic_path;
#endif
  if (!diagnostic_path.empty() && !std::filesystem::path(diagnostic_path).is_absolute())
    return false;
  std::string target(url);
  char* argv[] = {const_cast<char*>(executable.c_str()), target.data(), nullptr};
  pid_t process = 0;
  const int status = diagnostic_path.empty()
      ? posix_spawnp(&process, executable.c_str(), nullptr, nullptr, argv, environ)
      : posix_spawn(&process, executable.c_str(), nullptr, nullptr, argv, environ);
  if (status != 0) return false;
  std::thread([process] { int child_status = 0; waitpid(process, &child_status, 0); }).detach();
  return true;
#endif
}
}  // namespace

void InstallUpdateAction(UpdateAction::Close close) {
  if (g_action) return;
  const std::string diagnostic_path = REXCVAR_GET(messages_browser_opener_path);
  if (!diagnostic_path.empty())
    REXLOG_INFO("messages: diagnostic browser opener configured");
  g_action = std::make_unique<UpdateAction>(
      [diagnostic_path](std::string_view url) {
        const bool ok = OpenBrowser(diagnostic_path, url);
        if (ok) REXLOG_INFO("messages: releases browser handoff accepted");
        else REXLOG_ERROR("messages: releases browser handoff failed");
        return ok;
      },
      [close = std::move(close)] {
        const bool queued = close();
        if (!queued) REXLOG_ERROR("messages: browser opened but game close was not queued");
        return queued;
      },
      [](std::function<void()> work) { std::thread(std::move(work)).detach(); });
}

bool AnswerUpdateQuery(std::string_view path, bool* answer) {
  if (!path.starts_with(kPrefix)) return false;
  *answer = false;
  if (path == "rbbz-messages/update/available") {
    *answer = UpdateOffer() == VersionStatus::kUpdateAvailable;
  } else if (path == "rbbz-messages/update/open-failed") {
    *answer = g_action && g_action->state() == OpenState::kFailed;
  }
  return true;
}

std::string UpdateMessage() {
  if (UpdateOffer() != VersionStatus::kUpdateAvailable) return {};
  const auto feed = Snapshot();
  if (!feed) return {};
  return "UPDATE GAME\nVersion " + feed->latest_version +
         " is available. Open the releases page to download the new setup? The game will close.";
}

bool HandleUpdateWrite(std::string_view path) {
  if (!path.starts_with(kPrefix)) return false;
  if (path == "rbbz-messages/update/open-releases/0" && g_action &&
      UpdateOffer() == VersionStatus::kUpdateAvailable) {
    if (g_action->Press()) REXLOG_INFO("messages: releases browser handoff requested");
  }
  return true;
}
}  // namespace rb_blitz::messages
