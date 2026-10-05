// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the General tab's install action (launcher/src/install_ultimate.h, B8).

#include "install_ultimate.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace rb_blitz::launcher {
namespace {

namespace fs = std::filesystem;

// The installer's helper, put in `{app}` next to the launcher (installer/setup.iss).
constexpr const char* kHelperName = "rb_blitz_setup_helper.exe";
// Where the helper puts the payload inside the game root, and the staging folder it uses
// while verifying (installer/src/install.h). The staging folder is removed when an install is
// cancelled so "cancel" is not the same as "half-written".
constexpr const char* kUltimateName = "ultimate";
constexpr const char* kStagingName = ".staging";

std::string Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\r' || text[begin] == '\n')) {
    ++begin;
  }
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string ReadTextFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return {};
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

// The helper's `--summary` format: one `key=value;key=value` line (installer/src/commands.cpp).
std::map<std::string, std::string> ParseSummary(const std::string& text) {
  std::map<std::string, std::string> values;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find(';', start);
    const std::string_view pair(
        text.data() + start, (end == std::string_view::npos ? text.size() : end) - start);
    const std::size_t equals = pair.find('=');
    if (equals != std::string_view::npos) {
      values[Trim(pair.substr(0, equals))] = Trim(pair.substr(equals + 1));
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return values;
}

#ifdef _WIN32

// Windows command-line quoting, by the rules CommandLineToArgvW parses by.
std::wstring Quote(const std::wstring& argument) {
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

// True when the helper was located; fills `error` with the one thing a user can act on.
bool LocateHelper(const fs::path& search_dir, const fs::path& game_root, fs::path* out) {
  const std::vector<fs::path> candidates = {
      search_dir / kHelperName,
      game_root / kHelperName,
      game_root.parent_path() / kHelperName,
  };
  for (const fs::path& candidate : candidates) {
    if (candidate.empty()) {
      continue;
    }
    std::error_code code;
    if (fs::exists(candidate, code)) {
      *out = candidate;
      return true;
    }
  }
  return false;
}

// The pinned Ultimate release URL, read out of the helper itself (`version --summary`) so the
// launcher never duplicates the pin. Empty when the helper has none or cannot be run.
std::string QueryPinnedUrl(const fs::path& helper, const fs::path& summary) {
  std::error_code code;
  fs::remove(summary, code);

  const std::wstring command =
      Quote(helper.wstring()) + L" version --summary " + Quote(summary.wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  std::wstring mutable_command = command;
  if (!CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
    return {};
  }
  CloseHandle(info.hThread);
  if (WaitForSingleObject(info.hProcess, 10000) == WAIT_TIMEOUT) {
    TerminateProcess(info.hProcess, 1);
  }
  CloseHandle(info.hProcess);
  return ParseSummary(ReadTextFile(summary))["ultimate_url"];
}

DWORD ExitCodeOf(HANDLE process) {
  DWORD code = 0;
  GetExitCodeProcess(process, &code);
  return code;
}

#endif  // _WIN32

}  // namespace

fs::path UltimateInstaller::WorkDir() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH + 1] = {};
  const DWORD length = GetTempPathW(MAX_PATH, buffer);
  const fs::path base = length > 0 ? fs::path(std::wstring(buffer, length))
                                   : fs::path(std::wstring(L"."));
  return base / L"rb_blitz_launcher";
#else
  return fs::temp_directory_path() / "rb_blitz_launcher";
#endif
}

UltimateInstaller::~UltimateInstaller() { Cancel(); }

void UltimateInstaller::Fail(const std::string& reason) {
  status_ = Status::kFailed;
  message_ = reason;
  progress_percent_ = -1;
  progress_detail_.clear();
}

void UltimateInstaller::Reset() {
  Cancel();
  status_ = Status::kIdle;
  message_.clear();
  progress_percent_ = -1;
  progress_detail_.clear();
  manual_url_.clear();
  std::error_code code;
  fs::remove(summary_path_, code);
  fs::remove(progress_path_, code);
  fs::remove(log_path_, code);
  // The pinned URL is fetched into the same folder rather than held in memory only when it is
  // needed, so it is removed with the rest.
  fs::remove(WorkDir() / L"pinned-url.txt", code);
}

void UltimateInstaller::Cancel() {
#ifdef _WIN32
  if (process_ != nullptr) {
    HANDLE process = static_cast<HANDLE>(process_);
    TerminateProcess(process, 1);
    WaitForSingleObject(process, 5000);
    CloseHandle(process);
    process_ = nullptr;
  }
  // The helper stages under the game root and only moves the payload into place after it
  // verifies, so a cancelled run leaves the game's own files alone; the staging folder is
  // removed here because the helper's own cleanup does not run when it is terminated.
  if (!manual_destination_.empty()) {
    std::error_code code;
    fs::remove_all(manual_destination_.parent_path() / kStagingName, code);
  }
#endif
  if (status_ == Status::kRunning) {
    status_ = Status::kFailed;
    message_ = "cancelled";
    progress_percent_ = -1;
    progress_detail_.clear();
  }
}

bool UltimateInstaller::Start(const fs::path& game_root, const fs::path& search_dir,
                              std::string* error) {
  Reset();
  if (game_root.empty()) {
    Fail("No game folder was found next to the launcher, so there is nothing to install into.");
    if (error != nullptr) {
      *error = message_;
    }
    return false;
  }
  manual_destination_ = game_root / kUltimateName;

#ifdef _WIN32
  if (!LocateHelper(search_dir, game_root, &helper_path_)) {
    Fail("The installer's helper (" + std::string(kHelperName) +
         ") is not next to the launcher. Run the installer to get it, or install the "
         "Ultimate release by hand into " +
         manual_destination_.string() + ".");
    if (error != nullptr) {
      *error = message_;
    }
    return false;
  }

  std::error_code code;
  const fs::path work = WorkDir();
  fs::create_directories(work, code);
  summary_path_ = work / L"ultimate-summary.txt";
  progress_path_ = work / L"ultimate-progress.txt";
  log_path_ = work / L"ultimate-log.txt";
  fs::remove(summary_path_, code);
  fs::remove(progress_path_, code);
  fs::remove(log_path_, code);

  manual_url_ = QueryPinnedUrl(helper_path_, work / L"pinned-url.txt");

  const std::wstring command =
      Quote(helper_path_.wstring()) + L" install-ultimate --dest " + Quote(game_root.wstring()) +
      L" --from-pinned --progress " + Quote(progress_path_.wstring()) + L" --summary " +
      Quote(summary_path_.wstring()) + L" --log " + Quote(log_path_.wstring());

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  std::wstring mutable_command = command;
  if (!CreateProcessW(helper_path_.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
    Fail("Could not start the installer helper (Windows error " +
         std::to_string(GetLastError()) + ").");
    if (error != nullptr) {
      *error = message_;
    }
    return false;
  }
  CloseHandle(info.hThread);
  process_ = info.hProcess;
  status_ = Status::kRunning;
  message_.clear();
  progress_percent_ = -1;
  progress_detail_ = "starting";
  return true;
#else
  Fail("Installing Ultimate from the launcher is only implemented on Windows.");
  if (error != nullptr) {
    *error = message_;
  }
  return false;
#endif
}

void UltimateInstaller::Poll() {
  if (status_ != Status::kRunning) {
    return;
  }

  // The helper writes two lines: percent, then the current step.
  const std::string progress = ReadTextFile(progress_path_);
  if (!progress.empty()) {
    const std::size_t newline = progress.find('\n');
    const std::string first = Trim(progress.substr(0, newline));
    progress_percent_ = std::atoi(first.c_str());
    if (newline != std::string::npos) {
      progress_detail_ = Trim(progress.substr(newline + 1));
    }
  }

#ifdef _WIN32
  HANDLE process = static_cast<HANDLE>(process_);
  if (process == nullptr) {
    return;
  }
  if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
    return;
  }
  const DWORD exit_code = ExitCodeOf(process);
  CloseHandle(process);
  process_ = nullptr;

  const std::map<std::string, std::string> summary = ParseSummary(ReadTextFile(summary_path_));
  const auto ok = summary.find("ok");
  if (ok == summary.end()) {
    Fail("The installer helper stopped without reporting (exit code " +
         std::to_string(exit_code) + ").");
    return;
  }
  if (ok->second == "1") {
    status_ = Status::kSucceeded;
    const auto version = summary.find("version");
    message_ = version != summary.end() && !version->second.empty()
                   ? "Installed the Ultimate mod (" + version->second + ")."
                   : "Installed the Ultimate mod.";
    progress_percent_ = 100;
    progress_detail_ = "installed";
    return;
  }
  const auto error = summary.find("error");
  status_ = Status::kFailed;
  message_ = error != summary.end() && !error->second.empty()
                 ? error->second
                 : "The installer helper reported a failure.";
  progress_percent_ = -1;
  progress_detail_.clear();
#endif
}

void OpenInShell(const std::string& url) {
#ifdef _WIN32
  if (url.empty()) {
    return;
  }
  const int needed =
      MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), needed);
  ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)url;
#endif
}

void OpenFolder(const std::filesystem::path& folder) {
#ifdef _WIN32
  if (folder.empty()) {
    return;
  }
  std::error_code code;
  std::filesystem::create_directories(folder, code);
  ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)folder;
#endif
}

}  // namespace rb_blitz::launcher
