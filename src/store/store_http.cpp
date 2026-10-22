// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_http.h. Windows-only: winhttp.lib is linked by the game target (CMakeLists.txt).

#include "store/store_http.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace rb_blitz::store {
namespace {

// Windows 8.1+ discovers a static proxy, a PAC script and WPAD on its own. The installer already
// relies on this access type (installer/src/download.cpp); the game does too, for the same reason.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

class InternetHandle {
 public:
  InternetHandle() = default;
  explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
  ~InternetHandle() { Close(); }

  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;

  HINTERNET get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

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

std::string Win32Error(const char* what) {
  return std::string(what) + ": win32 error " + std::to_string(GetLastError());
}

std::wstring Widen(std::string_view text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}

std::string Narrow(const wchar_t* text, std::size_t length) {
  if (text == nullptr || length == 0) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string narrow(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), narrow.data(), size, nullptr, nullptr);
  return narrow;
}

std::string ToLowerAscii(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

std::string TrimAscii(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

HttpResponse Fail(HttpResponse response, std::string message) {
  response.ok = false;
  response.error = std::move(message);
  return response;
}

// Splits the raw response header block. The status line is skipped; a folded continuation line is
// appended to the previous value, which is what an old proxy in the path may still produce.
void ParseRawHeaders(const std::wstring& raw, std::vector<HttpHeader>* headers) {
  std::size_t begin = 0;
  bool first_line = true;
  while (begin < raw.size()) {
    std::size_t end = raw.find(L"\r\n", begin);
    if (end == std::wstring::npos) {
      end = raw.size();
    }
    const std::wstring line = raw.substr(begin, end - begin);
    begin = end + 2;
    if (first_line) {
      first_line = false;
      continue;
    }
    if (line.empty()) {
      continue;
    }
    if (line.front() == L' ' || line.front() == L'\t') {
      if (!headers->empty()) {
        headers->back().value += " " + TrimAscii(Narrow(line.c_str(), line.size()));
      }
      continue;
    }
    const std::size_t colon = line.find(L':');
    if (colon == std::wstring::npos) {
      continue;
    }
    HttpHeader header;
    header.name = ToLowerAscii(TrimAscii(Narrow(line.c_str(), colon)));
    header.value = TrimAscii(Narrow(line.c_str() + colon + 1, line.size() - colon - 1));
    headers->push_back(std::move(header));
  }
}

HttpResponse Perform(std::string_view url, std::string_view body, bool post,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     const HttpOptions& options) {
  HttpResponse response;

  const std::wstring wide_url = Widen(url);
  if (wide_url.empty()) {
    return Fail(std::move(response), "the URL is empty or not valid UTF-8");
  }
  if (wide_url.size() >= 2084) {  // INTERNET_MAX_URL_LENGTH - 1
    return Fail(std::move(response), "the URL is longer than WinHTTP accepts");
  }

  InternetHandle session(WinHttpOpen(options.user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    // A machine with a broken proxy configuration can refuse the automatic access type.
    session.Reset(WinHttpOpen(options.user_agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  }
  if (!session) {
    return Fail(std::move(response), Win32Error("WinHttpOpen"));
  }
  if (!WinHttpSetTimeouts(session.get(), options.resolve_timeout_ms, options.connect_timeout_ms,
                          options.send_timeout_ms, options.receive_timeout_ms)) {
    return Fail(std::move(response), Win32Error("WinHttpSetTimeouts"));
  }

  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);
  components.dwUserNameLength = static_cast<DWORD>(-1);
  components.dwPasswordLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &components)) {
    return Fail(std::move(response), Win32Error("WinHttpCrackUrl"));
  }
  if (components.nScheme != INTERNET_SCHEME_HTTPS) {
    return Fail(std::move(response), "the transport only speaks HTTPS");
  }

  std::wstring object(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength != 0) {
    object.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }
  // WinHttpConnect wants a null-terminated server name; the component points into the URL buffer,
  // where the host name is followed by '/', not by a terminator (measured: WinHttpConnect then fails
  // with 12005 ERROR_WINHTTP_INVALID_URL).
  const std::wstring host(components.lpszHostName, components.dwHostNameLength);

  InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
  if (!connection) {
    return Fail(std::move(response), Win32Error("WinHttpConnect"));
  }

  const wchar_t* verb = post ? L"POST" : L"GET";
  InternetHandle request(WinHttpOpenRequest(connection.get(), verb, object.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE));
  if (!request) {
    return Fail(std::move(response), Win32Error("WinHttpOpenRequest"));
  }

  // §3.1 step 3: the caller resolves every redirect itself, so the transport must never follow one.
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  std::wstring header_block;
  for (const auto& [name, value] : headers) {
    header_block += Widen(name);
    header_block += L": ";
    header_block += Widen(value);
    header_block += L"\r\n";
  }

  void* body_pointer = WINHTTP_NO_REQUEST_DATA;
  DWORD body_size = 0;
  if (post && !body.empty()) {
    body_pointer = const_cast<char*>(body.data());
    body_size = static_cast<DWORD>(body.size());
  }
  const wchar_t* header_pointer =
      header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str();

  if (!WinHttpSendRequest(request.get(), header_pointer,
                          header_block.empty() ? 0 : static_cast<DWORD>(header_block.size()),
                          body_pointer, body_size, body_size, 0)) {
    return Fail(std::move(response), Win32Error("WinHttpSendRequest"));
  }
  if (!WinHttpReceiveResponse(request.get(), nullptr)) {
    return Fail(std::move(response), Win32Error("WinHttpReceiveResponse"));
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                           WINHTTP_NO_HEADER_INDEX)) {
    return Fail(std::move(response), Win32Error("WinHttpQueryHeaders(status)"));
  }
  response.status = static_cast<int>(status);

  DWORD raw_size = 0;
  WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                      nullptr, &raw_size, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && raw_size > 0) {
    std::wstring raw(raw_size / sizeof(wchar_t), L'\0');
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &raw_size,
                            WINHTTP_NO_HEADER_INDEX)) {
      ParseRawHeaders(raw, &response.headers);
    }
  }

  std::vector<char> buffer(16 * 1024);
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
      return Fail(std::move(response), Win32Error("WinHttpReadData"));
    }
    if (read == 0) {
      break;
    }
    if (response.body.size() + read > options.max_body_bytes) {
      return Fail(std::move(response), "the response exceeds the " +
                                           std::to_string(options.max_body_bytes) + "-byte ceiling");
    }
    response.body.append(buffer.data(), read);
  }

  response.ok = true;
  return response;
}

}  // namespace

const HttpHeader* HttpResponse::Find(std::string_view name) const {
  const std::string wanted = ToLowerAscii(std::string(name));
  for (const HttpHeader& header : headers) {
    if (header.name == wanted) {
      return &header;
    }
  }
  return nullptr;
}

HttpResponse PostForm(std::string_view url, std::string_view body,
                      const std::vector<std::pair<std::string, std::string>>& headers,
                      const HttpOptions& options) {
  return Perform(url, body, true, headers, options);
}

HttpResponse Get(std::string_view url,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 const HttpOptions& options) {
  return Perform(url, {}, false, headers, options);
}

std::optional<int> RetryAfterSeconds(const HttpResponse& response) {
  const HttpHeader* header = response.Find("retry-after");
  if (header == nullptr || header->value.empty()) {
    return std::nullopt;
  }
  // Delta-seconds only: the API answers an integer. An HTTP-date (or anything else) is not parsed
  // - the caller's default backoff then applies, which is the conservative reading.
  int seconds = 0;
  for (const char c : header->value) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    if (seconds > 100000) {
      return std::nullopt;  // absurd value: let the clamp decide instead of overflowing int
    }
    seconds = seconds * 10 + (c - '0');
  }
  return seconds;
}

}  // namespace rb_blitz::store
