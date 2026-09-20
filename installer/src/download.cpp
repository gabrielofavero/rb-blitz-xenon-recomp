// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See download.h.

#include "download.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "util.h"
#include "util/sha256.h"

namespace rb_blitz::installer {
namespace {

// Windows 8.1+ discovers a static proxy, a PAC script and WPAD on its own; this
// is the only access type that does, and the installer requires Windows 10.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

constexpr std::size_t kReadBufferSize = 256 * 1024;

class InternetHandle {
 public:
  InternetHandle() = default;
  explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
  ~InternetHandle() { Close(); }

  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;

  HINTERNET get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

  // Releases the handle and takes ownership of `handle`, so a successful call
  // cleanly replaces a placeholder without leaking.
  void Reset(HINTERNET handle) {
    Close();
    handle_ = handle;
  }
  void Close() {
    if (handle_ != nullptr) {
      WinHttpCloseHandle(handle_);
      handle_ = nullptr;
    }
  }

 private:
  HINTERNET handle_ = nullptr;
};

class FileWriter {
 public:
  ~FileWriter() { Close(); }

  bool Open(const std::filesystem::path& path, std::string* error) {
    file_ = _wfopen(path.c_str(), L"wb");
    if (file_ == nullptr) {
      *error = S("cannot create ", path.string(), ": ", LastWin32Error());
      return false;
    }
    return true;
  }

  bool Write(const std::uint8_t* data, std::size_t size, std::string* error) {
    if (std::fwrite(data, 1, size, file_) != size) {
      *error = S("write failed: ", LastWin32Error());
      return false;
    }
    return true;
  }

  void Close() {
    if (file_ != nullptr) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

 private:
  std::FILE* file_ = nullptr;
};

// Splits `url` into the pieces WinHttpConnect/WinHttpOpenRequest need. CrackUrl
// writes into the buffer, so the parts are copied out before it is reused.
bool SplitUrl(std::wstring& url, std::wstring* host, std::wstring* path, INTERNET_PORT* port,
              bool* secure, std::string* error) {
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) == FALSE) {
    *error = S("not a usable URL: ", Narrow(url), " (", LastWin32Error(), ")");
    return false;
  }
  if (components.dwHostNameLength == 0) {
    *error = S("URL has no host: ", Narrow(url));
    return false;
  }
  const std::wstring scheme(components.lpszScheme, components.dwSchemeLength);
  if (!EqualsIgnoreCase(Narrow(scheme), "https") && !EqualsIgnoreCase(Narrow(scheme), "http")) {
    *error = S("unsupported URL scheme: ", Narrow(scheme));
    return false;
  }
  *secure = EqualsIgnoreCase(Narrow(scheme), "https");
  *host = std::wstring(components.lpszHostName, components.dwHostNameLength);
  // A URL with no path at all still needs "/" for the request line.
  const std::wstring_view url_path(components.lpszUrlPath, components.dwUrlPathLength);
  const std::wstring_view extra_info(components.lpszExtraInfo, components.dwExtraInfoLength);
  *path = url_path.empty() ? L"/" : std::wstring(url_path) + std::wstring(extra_info);
  *port = components.nPort;
  return true;
}

bool QueryStatusCode(HINTERNET request, int* status_code, std::string* error) {
  DWORD status = 0;
  DWORD size = sizeof(status);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                          WINHTTP_NO_HEADER_INDEX) == FALSE) {
    *error = S("cannot read the HTTP status: ", LastWin32Error());
    return false;
  }
  *status_code = static_cast<int>(status);
  return true;
}

std::uint64_t QueryContentLength(HINTERNET request) {
  wchar_t text[64] = {};
  DWORD size = sizeof(text) - sizeof(wchar_t);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text,
                          &size, WINHTTP_NO_HEADER_INDEX) == FALSE) {
    return 0;
  }
  const std::string narrow = Narrow(text);
  return narrow.empty() ? 0 : std::strtoull(narrow.c_str(), nullptr, 10);
}

}  // namespace

bool DownloadToFile(const DownloadRequest& request, DownloadResult* out, std::string* error) {
  if (request.url.empty()) {
    *error = "empty download URL";
    return false;
  }
  if (!EnsureParentDirectory(request.destination, error)) {
    return false;
  }

  std::wstring url = Widen(request.url);
  std::wstring host;
  std::wstring path;
  bool secure = true;
  INTERNET_PORT port = 0;
  if (!SplitUrl(url, &host, &path, &port, &secure, error)) {
    return false;
  }

  InternetHandle session(WinHttpOpen(Widen(request.user_agent).c_str(),
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    *error = S("WinHttpOpen failed: ", LastWin32Error());
    return false;
  }
  // A stalled server must not leave the wizard on its progress page forever.
  WinHttpSetTimeouts(session.get(), 20000, 30000, 30000, 120000);
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

  InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), port, 0));
  if (!connection) {
    *error = S("cannot connect to ", Narrow(host), ": ", LastWin32Error());
    return false;
  }

  InternetHandle http_request(WinHttpOpenRequest(
      connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
  if (!http_request) {
    *error = S("cannot start the request: ", LastWin32Error());
    return false;
  }
  // GitHub release assets answer with a redirect to a signed storage URL, so
  // every hop has to be followed rather than just the first.
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(http_request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  if (WinHttpSendRequest(http_request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE) {
    *error = S("request failed: ", LastWin32Error());
    return false;
  }
  if (WinHttpReceiveResponse(http_request.get(), nullptr) == FALSE) {
    *error = S("no response: ", LastWin32Error());
    return false;
  }

  int status_code = 0;
  if (!QueryStatusCode(http_request.get(), &status_code, error)) {
    return false;
  }
  out->status_code = status_code;
  out->final_url = request.url;
  if (status_code != 200) {
    *error = S("server answered HTTP ", status_code, " for ", request.url);
    return false;
  }

  const std::uint64_t content_length = QueryContentLength(http_request.get());
  const std::uint64_t expected = request.expected_size != 0 ? request.expected_size : content_length;
  if (request.expected_size != 0 && content_length != 0 &&
      content_length != request.expected_size) {
    *error = S("the server offers ", HumanBytes(content_length), " but this release pins ",
               HumanBytes(request.expected_size));
    return false;
  }

  FileWriter writer;
  if (!writer.Open(request.destination, error)) {
    return false;
  }

  util::Sha256 sha;
  std::vector<std::uint8_t> buffer(kReadBufferSize);
  std::uint64_t total = 0;
  int last_percent = -1;
  bool ok = true;
  for (;;) {
    DWORD available = 0;
    if (WinHttpQueryDataAvailable(http_request.get(), &available) == FALSE) {
      *error = S("connection lost after ", HumanBytes(total), ": ", LastWin32Error());
      ok = false;
      break;
    }
    if (available == 0) {
      break;  // End of body.
    }
    const DWORD chunk = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
    DWORD read = 0;
    if (WinHttpReadData(http_request.get(), buffer.data(), chunk, &read) == FALSE) {
      *error = S("read failed after ", HumanBytes(total), ": ", LastWin32Error());
      ok = false;
      break;
    }
    if (read == 0) {
      break;
    }
    if (!writer.Write(buffer.data(), read, error)) {
      ok = false;
      break;
    }
    sha.Update(buffer.data(), read);
    total += read;
    if (expected != 0 && !request.progress_file.empty()) {
      const int percent = static_cast<int>(
          std::min<std::uint64_t>(99, (total * 100) / std::max<std::uint64_t>(1, expected)));
      if (percent != last_percent) {
        last_percent = percent;
        WriteProgressFile(request.progress_file, percent);
      }
    }
  }
  writer.Close();

  if (!ok) {
    RemoveFile(request.destination, nullptr);
    return false;
  }
  if (expected != 0 && total != expected) {
    RemoveFile(request.destination, nullptr);
    *error = S("incomplete download: ", HumanBytes(total), " of ", HumanBytes(expected));
    return false;
  }

  out->size = total;
  out->sha256 = util::ToHex(sha.Final());
  if (!request.expected_sha256.empty() &&
      !EqualsIgnoreCase(out->sha256, request.expected_sha256)) {
    RemoveFile(request.destination, nullptr);
    *error = S("SHA-256 mismatch: expected ", Lower(request.expected_sha256), ", got ", out->sha256);
    return false;
  }
  if (!request.progress_file.empty()) {
    WriteProgressFile(request.progress_file, 100, S(HumanBytes(total), " downloaded"));
  }
  return true;
}

}  // namespace rb_blitz::installer
