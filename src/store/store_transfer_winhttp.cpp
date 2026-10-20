// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_transfer.h. Windows-only: winhttp.lib is linked by the game target.

#include "store/store_transfer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb_blitz::store {
namespace {

constexpr wchar_t kUserAgent[] = L"rb-blitz-store/0.1 (+https://rhythmverse.co/)";

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

std::wstring Widen(const std::string_view text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int length =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}

std::string Narrow(const wchar_t* text, const std::size_t length) {
  if (text == nullptr || length == 0) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string narrow(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), narrow.data(), size, nullptr,
                      nullptr);
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

// "name: value" pairs out of the raw response block, lowercased names, folded continuations.
void ParseRawHeaders(const std::wstring& raw, std::vector<std::pair<std::string, std::string>>* out) {
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
      if (!out->empty()) {
        out->back().second += " " + TrimAscii(Narrow(line.c_str(), line.size()));
      }
      continue;
    }
    const std::size_t colon = line.find(L':');
    if (colon == std::wstring::npos) {
      continue;
    }
    out->emplace_back(ToLowerAscii(TrimAscii(Narrow(line.c_str(), colon))),
                      TrimAscii(Narrow(line.c_str() + colon + 1, line.size() - colon - 1)));
  }
}

const std::string* FindHeader(const std::vector<std::pair<std::string, std::string>>& headers,
                              const std::string_view name) {
  for (const auto& [key, value] : headers) {
    if (key == name) {
      return &value;
    }
  }
  return nullptr;
}

bool QueryRawHeaders(HINTERNET request, std::vector<std::pair<std::string, std::string>>* out) {
  DWORD size = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                      nullptr, &size, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
    return false;
  }
  std::wstring raw(size / sizeof(wchar_t), L'\0');
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                           raw.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
    return false;
  }
  ParseRawHeaders(raw, out);
  return true;
}

TransferResult Perform(const std::string_view url, const TransferReceiver& receiver) {
  TransferResult result;

  const std::wstring wide_url = Widen(url);
  if (wide_url.empty()) {
    result.error = "the URL is empty or not valid UTF-8";
    return result;
  }
  if (wide_url.size() >= 2084) {  // INTERNET_MAX_URL_LENGTH - 1
    result.error = "the URL is longer than WinHTTP accepts";
    return result;
  }

  InternetHandle session(
      WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                  WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    session.Reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  }
  if (!session) {
    result.error = Win32Error("WinHttpOpen");
    return result;
  }
  if (!WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 30000)) {
    result.error = Win32Error("WinHttpSetTimeouts");
    return result;
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
    result.error = Win32Error("WinHttpCrackUrl");
    return result;
  }
  if (components.nScheme != INTERNET_SCHEME_HTTPS) {
    result.error = "the transport only speaks HTTPS";
    return result;
  }

  std::wstring object(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength != 0) {
    object.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }
  // WinHttpConnect wants a null-terminated server name; the component points into the URL buffer.
  const std::wstring host(components.lpszHostName, components.dwHostNameLength);

  InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
  if (!connection) {
    result.error = Win32Error("WinHttpConnect");
    return result;
  }

  InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", object.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE));
  if (!request) {
    result.error = Win32Error("WinHttpOpenRequest");
    return result;
  }

  // §3.1 step 3: the caller resolves every redirect itself.
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));
  // No decompression option is set: WinHTTP only decodes a gzip/deflate body when asked to, so
  // the bytes that arrive are the representation the length describes. The policy still refuses
  // an explicit `Content-Encoding` the server sends without being asked (store_download.cpp).

  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    result.error = Win32Error("WinHttpSendRequest");
    return result;
  }
  if (!WinHttpReceiveResponse(request.get(), nullptr)) {
    result.error = Win32Error("WinHttpReceiveResponse");
    return result;
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                           WINHTTP_NO_HEADER_INDEX)) {
    result.error = Win32Error("WinHttpQueryHeaders(status)");
    return result;
  }

  std::vector<std::pair<std::string, std::string>> headers;
  QueryRawHeaders(request.get(), &headers);

  TransferHead head;
  head.status = static_cast<int>(status);
  if (const std::string* value = FindHeader(headers, "content-length")) {
    try {
      const long long length = std::stoll(*value);
      if (length > 0) {
        head.content_length = length;
      }
    } catch (...) {
      // A malformed length is "unknown": the policy still bounds and validates the stream.
    }
  }
  if (const std::string* value = FindHeader(headers, "content-type")) {
    head.content_type = ToLowerAscii(*value);
  }
  if (const std::string* value = FindHeader(headers, "content-disposition")) {
    head.content_disposition = *value;
  }
  if (const std::string* value = FindHeader(headers, "content-encoding")) {
    head.content_encoding = *value;
  }
  if (const std::string* value = FindHeader(headers, "location")) {
    head.location = *value;
  }
  if (const std::string* value = FindHeader(headers, "retry-after")) {
    head.retry_after = *value;
  }

  if (receiver.on_head && !receiver.on_head(head)) {
    result.ok = true;  // the policy aborted deliberately (a redirect or a refusal)
    return result;
  }
  if (head.status != 200) {
    result.ok = true;  // no body is worth reading for a non-200 answer
    return result;
  }

  std::array<std::uint8_t, 64 * 1024> buffer{};
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) {
      result.error = Win32Error("WinHttpQueryDataAvailable");
      return result;
    }
    if (available == 0) {
      result.ok = true;
      return result;
    }
    const DWORD to_read = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), buffer.data(), to_read, &read)) {
      result.error = Win32Error("WinHttpReadData");
      return result;
    }
    if (read == 0) {
      result.ok = true;
      return result;
    }
    if (receiver.on_data && !receiver.on_data(std::span<const std::uint8_t>(buffer.data(), read))) {
      result.ok = true;  // the policy aborted deliberately
      return result;
    }
  }
}

}  // namespace

TransferFn WinHttpTransfer() {
  return [](const std::string_view url, const TransferReceiver& receiver) {
    return Perform(url, receiver);
  };
}

}  // namespace rb_blitz::store
