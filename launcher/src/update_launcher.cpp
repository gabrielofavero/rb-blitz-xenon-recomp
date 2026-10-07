// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See update_launcher.h.

#include "update_launcher.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "install_ultimate.h"  // OpenInShell: one place opens the OS shell

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace rb_blitz::launcher {
namespace {

// The build defines these from installer/config/pins.toml (launcher/CMakeLists.txt). The empty
// fallbacks are for a translation unit compiled outside that build - a syntax-only run - and say
// what they mean: no version to compare against, and no release channel.
#ifndef RBBLITZ_LAUNCHER_VERSION
#define RBBLITZ_LAUNCHER_VERSION ""
#endif
#ifndef RBBLITZ_LAUNCHER_UPDATE_URL
#define RBBLITZ_LAUNCHER_UPDATE_URL ""
#endif
#ifndef RBBLITZ_LAUNCHER_RELEASES_URL
#define RBBLITZ_LAUNCHER_RELEASES_URL ""
#endif

#ifdef _WIN32

// The uploader's own timeouts, in milliseconds: name resolution, connection, send, receive.
// Short on purpose - this is a launcher's background check, not a download - and they bound
// how long a shutdown waits for a request that the network has stopped answering.
constexpr int kResolveTimeoutMs = 2000;
constexpr int kConnectTimeoutMs = 2000;
constexpr int kSendTimeoutMs = 2000;
constexpr int kReceiveTimeoutMs = 4000;

// A manifest is a handful of lines. The cap is what separates "the server answered with
// something that is not a manifest" from a document nobody wants in memory; a redirect to a
// huge body is the only way to reach it.
constexpr std::size_t kMaxBodyBytes = 64 * 1024;

// The pieces of an absolute https URL that WinHTTP needs separately. Written here rather
// than with WinHttpCrackUrl so that a caller with no network - the parse - is decidable on
// its own.
struct HttpUrl {
  std::wstring host;
  std::wstring target;  // path and query, always starting with '/'
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  bool secure = true;
};

bool ParseHttpUrl(const std::string& url, HttpUrl* out, std::string* error) {
  std::string rest;
  std::string scheme;
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    *error = "the update URL has no scheme";
    return false;
  }
  scheme = url.substr(0, scheme_end);
  rest = url.substr(scheme_end + 3);
  if (scheme != "https" && scheme != "http") {
    *error = "the update URL is not http(s): " + scheme;
    return false;
  }
  // A URL with a user name or a password is not one this launcher is configured with, and
  // sending credentials to whatever the name resolves to is not a decision to make here.
  if (rest.find('@') != std::string::npos && rest.find('@') < rest.find('/')) {
    *error = "the update URL carries credentials";
    return false;
  }

  std::string authority = rest;
  std::string target = "/";
  if (const std::size_t slash = rest.find('/'); slash != std::string::npos) {
    authority = rest.substr(0, slash);
    target = rest.substr(slash);
  }
  if (authority.empty()) {
    *error = "the update URL has no host";
    return false;
  }
  std::string host = authority;
  if (const std::size_t colon = authority.rfind(':'); colon != std::string::npos) {
    host = authority.substr(0, colon);
    const std::string port_text = authority.substr(colon + 1);
    const unsigned long port = std::strtoul(port_text.c_str(), nullptr, 10);
    if (port == 0 || port > 65535) {
      *error = "the update URL's port is not a number";
      return false;
    }
    out->port = static_cast<INTERNET_PORT>(port);
  }
  if (host.empty()) {
    *error = "the update URL has no host";
    return false;
  }

  const int wide_length = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
  if (wide_length <= 1) {
    *error = "the update URL's host is not UTF-8";
    return false;
  }
  out->host.resize(static_cast<std::size_t>(wide_length - 1));
  MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, out->host.data(), wide_length);

  const int target_length = MultiByteToWideChar(CP_UTF8, 0, target.c_str(), -1, nullptr, 0);
  if (target_length <= 1) {
    *error = "the update URL's path is not UTF-8";
    return false;
  }
  out->target.resize(static_cast<std::size_t>(target_length - 1));
  MultiByteToWideChar(CP_UTF8, 0, target.c_str(), -1, out->target.data(), target_length);

  out->secure = scheme == "https";
  return true;
}

// A handle that closes when it leaves the function, so no early return leaks one.
class WinHttpHandle {
 public:
  explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
  ~WinHttpHandle() {
    if (handle_ != nullptr) {
      WinHttpCloseHandle(handle_);
    }
  }
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;

  HINTERNET get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

 private:
  HINTERNET handle_;
};

#endif  // _WIN32

}  // namespace

std::string UpdateManifestUrl() { return RBBLITZ_LAUNCHER_UPDATE_URL; }

std::string RunningVersion() { return RBBLITZ_LAUNCHER_VERSION; }

std::string ReleasesUrl() { return RBBLITZ_LAUNCHER_RELEASES_URL; }

std::filesystem::path UpdaterPath() {
#ifdef _WIN32
  // The same folder the setup executable installs it into (installer/setup.iss:
  // {localappdata}\{#UpdateDirName}\update\RockBandBlitzUpdater.exe). %LOCALAPPDATA% is
  // what {localappdata} expands to, and is read rather than resolved through
  // SHGetKnownFolderPath so that no COM call is needed to find a file.
  wchar_t buffer[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return {};
  }
  return std::filesystem::path(std::wstring(buffer, length)) / L"rb_blitz" / L"update" /
         L"RockBandBlitzUpdater.exe";
#else
  return {};
#endif
}

bool FetchReleaseManifest(const std::string& url, std::string* body, std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (url.empty()) {
    return fail("this build has no update channel");
  }

#ifdef _WIN32
  HttpUrl parts;
  if (!ParseHttpUrl(url, &parts, error)) {
    return false;
  }

  WinHttpHandle session(WinHttpOpen(L"rb_blitz_launcher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    return fail("WinHttpOpen failed");
  }
  WinHttpSetTimeouts(session.get(), kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs,
                     kReceiveTimeoutMs);

  WinHttpHandle connection(
      WinHttpConnect(session.get(), parts.host.c_str(), parts.port, 0));
  if (!connection) {
    return fail("WinHttpConnect failed");
  }

  const DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
  WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", parts.target.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
  if (!request) {
    return fail("WinHttpOpenRequest failed");
  }
  // GitHub serves the redirect from /releases/latest/download/... to the newest release, and
  // following it is the whole point of that URL.
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                          0, 0, 0)) {
    return fail("the update request could not be sent");
  }
  if (!WinHttpReceiveResponse(request.get(), nullptr)) {
    return fail("the update request got no answer");
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                           WINHTTP_NO_HEADER_INDEX)) {
    return fail("the update answer had no status");
  }
  if (status != 200) {
    return fail("the update request got HTTP " + std::to_string(status));
  }

  std::string text;
  char buffer[4096];
  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) {
      return fail("the update answer ended early");
    }
    if (available == 0) {
      break;
    }
    const DWORD wanted =
        available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), buffer, wanted, &read)) {
      return fail("the update answer ended early");
    }
    if (read == 0) {
      break;
    }
    if (text.size() + read > kMaxBodyBytes) {
      return fail("the update answer is too large to be a release manifest");
    }
    text.append(buffer, read);
  }
  if (text.empty()) {
    return fail("the update answer was empty");
  }
  *body = std::move(text);
  return true;
#else
  return fail("this platform has no update check");
#endif
}

struct UpdateChecker::Worker {
  mutable std::mutex mutex;
  // Shared with the worker thread, which is joined before this is destroyed, so there is
  // nothing to keep alive past the launcher's own lifetime.
  UpdateCheck check;
  std::thread thread;

  ~Worker() {
    if (thread.joinable()) {
      thread.join();
    }
  }
};

UpdateChecker::~UpdateChecker() { delete worker_; }

void UpdateChecker::Start(const std::string& url) {
  if (worker_ != nullptr) {
    return;
  }
  // A fresh Worker is kUnchecked: nothing has been asked yet.
  worker_ = new Worker();
  if (url.empty()) {
    // A build with no release channel, or a run that asked for no check: the state stays
    // kUnchecked, so the button says the launcher does not check rather than claiming to have
    // checked and found nothing.
    return;
  }
  worker_->check.state = UpdateCheck::State::kChecking;

  UpdateChecker::Worker* worker = worker_;
  worker->thread = std::thread([worker, url] {
    std::string body;
    ReleaseManifest manifest;
    bool fetched = FetchReleaseManifest(url, &body, nullptr) &&
                   ParseReleaseManifest(body, &manifest, nullptr);
    std::lock_guard<std::mutex> lock(worker->mutex);
    worker->check.state = UpdateCheck::State::kDone;
    worker->check.fetched = fetched;
    if (fetched) {
      worker->check.manifest = std::move(manifest);
    }
  });
}

UpdateCheck UpdateChecker::Poll() const {
  if (worker_ == nullptr) {
    return {};
  }
  std::lock_guard<std::mutex> lock(worker_->mutex);
  return worker_->check;
}

bool StartUpdater(const std::filesystem::path& updater, std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
#ifdef _WIN32
  if (updater.empty()) {
    return fail("the updater is not installed");
  }
  std::error_code code;
  if (!std::filesystem::exists(updater, code)) {
    return fail(updater.string() + " is missing");
  }

  // No arguments: the updater is a build of the installer wizard, and the release manifest
  // it fetches for itself is the whole of what it needs to know. Started without a window of
  // its own (the wizard is one) and without waiting - the launcher has to be gone before the
  // update replaces it, and leaving is the caller's business.
  std::wstring command = updater.wstring();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  if (!CreateProcessW(updater.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &info)) {
    return fail("could not start " + updater.string());
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return true;
#else
  (void)updater;
  return fail("this platform has no updater");
#endif
}

void OpenReleasesPage() {
  const std::string url = ReleasesUrl();
  if (!url.empty()) {
    OpenInShell(url);
  }
}

}  // namespace rb_blitz::launcher
