// SPDX-License-Identifier: GPL-2.0-only
// Native Unix transport for the catalogue and streaming package policy.
#include "store/store_http.h"
#include "store/store_transfer.h"

#if defined(_WIN32)
#error "The libcurl transport is for non-Windows builds only"
#endif

#include <curl/curl.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "store/rhythmverse_client.h"

namespace rb_blitz::store {
namespace {

struct CurlDeleter {
  void operator()(CURL* handle) const { if (handle) curl_easy_cleanup(handle); }
};
struct UrlDeleter {
  void operator()(CURLU* handle) const { if (handle) curl_url_cleanup(handle); }
};
struct HeaderDeleter {
  void operator()(curl_slist* list) const { if (list) curl_slist_free_all(list); }
};
using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;
using UrlHandle = std::unique_ptr<CURLU, UrlDeleter>;
using HeaderList = std::unique_ptr<curl_slist, HeaderDeleter>;

std::string Lower(std::string text) {
  for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return text;
}

std::string Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return std::string(text);
}

bool AllowedUrl(std::string_view url, bool allow_loopback_http, bool* loopback) {
  *loopback = false;
  if (url.empty() || url.find('\0') != std::string_view::npos) return false;
  UrlHandle parsed(curl_url());
  if (!parsed) return false;
  const std::string value(url);
  if (curl_url_set(parsed.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return false;
  char* scheme_raw = nullptr;
  char* host_raw = nullptr;
  char* user_raw = nullptr;
  char* password_raw = nullptr;
  if (curl_url_get(parsed.get(), CURLUPART_USER, &user_raw, 0) == CURLUE_OK) {
    curl_free(user_raw);
    return false;
  }
  if (curl_url_get(parsed.get(), CURLUPART_PASSWORD, &password_raw, 0) == CURLUE_OK) {
    curl_free(password_raw);
    return false;
  }
  if (curl_url_get(parsed.get(), CURLUPART_SCHEME, &scheme_raw, 0) != CURLUE_OK ||
      curl_url_get(parsed.get(), CURLUPART_HOST, &host_raw, 0) != CURLUE_OK) {
    if (scheme_raw) curl_free(scheme_raw);
    if (host_raw) curl_free(host_raw);
    return false;
  }
  const std::string scheme = Lower(scheme_raw);
  const std::string host = host_raw;
  curl_free(scheme_raw);
  curl_free(host_raw);
  if (scheme == "https") return true;
  *loopback = scheme == "http" && IsLoopbackHost(host);
  return allow_loopback_http && *loopback;
}

bool Ready() {
  static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  return initialized;
}

bool Configure(CURL* curl, const std::string& url, bool loopback,
               int connect_timeout_ms, int operation_timeout_ms) {
  const long total = static_cast<long>(std::max(1, connect_timeout_ms)) +
                     static_cast<long>(std::max(1, operation_timeout_ms));
  return curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                          static_cast<long>(std::max(1, connect_timeout_ms))) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, total) == CURLE_OK &&
         curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 0L) == CURLE_OK &&
         (!loopback || curl_easy_setopt(curl, CURLOPT_NOPROXY, "*") == CURLE_OK)
#if LIBCURL_VERSION_NUM >= 0x075500
         && curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR,
                             loopback ? "http,https" : "https") == CURLE_OK
#else
         && curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                             static_cast<long>(loopback ? CURLPROTO_HTTP | CURLPROTO_HTTPS
                                                        : CURLPROTO_HTTPS)) == CURLE_OK
#endif
         ;
}

bool AddHeader(curl_slist** list, const std::string& header) {
  curl_slist* next = curl_slist_append(*list, header.c_str());
  if (!next) return false;
  *list = next;
  return true;
}

void HeaderLine(std::string_view line, std::vector<HttpHeader>* headers, int* status) {
  if (line.starts_with("HTTP/")) {
    headers->clear();
    *status = 0;
    const auto first_space = line.find(' ');
    if (first_space != std::string_view::npos) {
      int value = 0;
      const auto number = line.substr(first_space + 1, 3);
      if (std::from_chars(number.data(), number.data() + number.size(), value).ec == std::errc{})
        *status = value;
    }
    return;
  }
  if (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
    if (!headers->empty()) headers->back().value += " " + Trim(line);
    return;
  }
  const auto colon = line.find(':');
  if (colon == std::string_view::npos) return;
  headers->push_back({Lower(Trim(line.substr(0, colon))), Trim(line.substr(colon + 1))});
}

struct HttpContext {
  HttpResponse response;
  std::size_t limit = 0;
  bool oversized = false;
};

size_t HttpHeaderCallback(char* data, size_t size, size_t count, void* user) {
  const size_t bytes = size * count;
  auto* context = static_cast<HttpContext*>(user);
  HeaderLine(std::string_view(data, bytes), &context->response.headers, &context->response.status);
  return bytes;
}

size_t HttpBodyCallback(char* data, size_t size, size_t count, void* user) {
  const size_t bytes = size * count;
  auto* context = static_cast<HttpContext*>(user);
  if (bytes > context->limit - std::min(context->limit, context->response.body.size())) {
    context->oversized = true;
    return 0;
  }
  context->response.body.append(data, bytes);
  return bytes;
}

HttpResponse Request(std::string_view url, std::string_view body, bool post,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     const HttpOptions& options) {
  HttpContext context;
  context.limit = options.max_body_bytes;
  bool loopback = false;
  if (!Ready() || !AllowedUrl(url, true, &loopback)) {
    context.response.error = "the transport only speaks HTTPS or loopback HTTP";
    return context.response;
  }
  CurlHandle curl(curl_easy_init());
  if (!curl || !Configure(curl.get(), std::string(url), loopback,
                          options.connect_timeout_ms,
                          options.send_timeout_ms + options.receive_timeout_ms)) {
    context.response.error = "cannot configure libcurl";
    return context.response;
  }
  HeaderList request_headers;
  curl_slist* list = nullptr;
  if (!AddHeader(&list, "Accept-Encoding: identity")) {
    context.response.error = "cannot allocate HTTP headers";
    return context.response;
  }
  request_headers.reset(list);
  for (const auto& [name, value] : headers) {
    if (name.find_first_of("\r\n:") != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos ||
        !AddHeader(&list, name + ": " + value)) {
      context.response.error = "invalid HTTP request headers";
      request_headers.release();
      curl_slist_free_all(list);
      return context.response;
    }
    request_headers.release();
    request_headers.reset(list);
  }
  std::string agent;
  if (options.user_agent) {
    for (const wchar_t* c = options.user_agent; *c; ++c) {
      if (*c < 0x20 || *c > 0x7e) {
        context.response.error = "invalid user agent";
        return context.response;
      }
      agent.push_back(static_cast<char>(*c));
    }
  }
  curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, agent.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, request_headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, HttpHeaderCallback);
  curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &context);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, HttpBodyCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);
  if (post) {
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.empty() ? "" : body.data());
  }
  const CURLcode outcome = curl_easy_perform(curl.get());
  if (outcome != CURLE_OK) {
    context.response.error = context.oversized
        ? "the response exceeds the " + std::to_string(context.limit) + "-byte ceiling"
        : curl_easy_strerror(outcome);
    return context.response;
  }
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  context.response.status = static_cast<int>(status);
  context.response.ok = status > 0;
  if (!context.response.ok) context.response.error = "no HTTP status line";
  return context.response;
}

struct TransferContext {
  const TransferReceiver* receiver = nullptr;
  std::vector<HttpHeader> headers;
  int status = 0;
  bool head_sent = false;
  bool deliberately_stopped = false;
};

const HttpHeader* FindHeader(const std::vector<HttpHeader>& headers, std::string_view name) {
  for (const auto& header : headers) if (header.name == name) return &header;
  return nullptr;
}

size_t TransferHeaderCallback(char* data, size_t size, size_t count, void* user) {
  const size_t bytes = size * count;
  auto* context = static_cast<TransferContext*>(user);
  const std::string_view line(data, bytes);
  HeaderLine(line, &context->headers, &context->status);
  if (line != "\r\n" && line != "\n") return bytes;
  if (context->status < 200) return bytes;  // informational response
  TransferHead head;
  head.status = context->status;
  if (const auto* value = FindHeader(context->headers, "content-length")) {
    std::int64_t length = 0;
    const auto end = value->value.data() + value->value.size();
    if (std::from_chars(value->value.data(), end, length).ptr == end && length > 0)
      head.content_length = length;
  }
  if (const auto* value = FindHeader(context->headers, "content-type")) head.content_type = Lower(value->value);
  if (const auto* value = FindHeader(context->headers, "content-disposition")) head.content_disposition = value->value;
  if (const auto* value = FindHeader(context->headers, "content-encoding")) head.content_encoding = value->value;
  if (const auto* value = FindHeader(context->headers, "location")) head.location = value->value;
  if (const auto* value = FindHeader(context->headers, "retry-after")) head.retry_after = value->value;
  context->head_sent = true;
  if ((context->receiver->on_head && !context->receiver->on_head(head)) || head.status != 200) {
    context->deliberately_stopped = true;
    return 0;
  }
  return bytes;
}

size_t TransferBodyCallback(char* data, size_t size, size_t count, void* user) {
  const size_t bytes = size * count;
  auto* context = static_cast<TransferContext*>(user);
  if (context->receiver->on_data &&
      !context->receiver->on_data(std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t*>(data), bytes))) {
    context->deliberately_stopped = true;
    return 0;
  }
  return bytes;
}

TransferResult Transfer(std::string_view url, const TransferReceiver& receiver) {
  bool loopback = false;
  if (!Ready() || !AllowedUrl(url, false, &loopback))
    return {false, "the transport only speaks HTTPS"};
  CurlHandle curl(curl_easy_init());
  if (!curl || !Configure(curl.get(), std::string(url), false, 10000, 1800000))
    return {false, "cannot configure libcurl"};
  HeaderList headers(curl_slist_append(nullptr, "Accept-Encoding: identity"));
  if (!headers) return {false, "cannot allocate HTTP headers"};
  TransferContext context{&receiver};
  curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "rb-blitz-store/0.1 (+https://rhythmverse.co/)");
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, TransferHeaderCallback);
  curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &context);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, TransferBodyCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);
  const CURLcode outcome = curl_easy_perform(curl.get());
  if (context.deliberately_stopped) return {true, {}};
  if (outcome != CURLE_OK) return {false, curl_easy_strerror(outcome)};
  if (!context.head_sent) return {false, "no HTTP status line"};
  return {true, {}};
}

}  // namespace

const HttpHeader* HttpResponse::Find(std::string_view name) const {
  const std::string wanted = Lower(std::string(name));
  return FindHeader(headers, wanted);
}

HttpResponse PostForm(std::string_view url, std::string_view body,
                      const std::vector<std::pair<std::string, std::string>>& headers,
                      const HttpOptions& options) {
  return Request(url, body, true, headers, options);
}

HttpResponse Get(std::string_view url,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 const HttpOptions& options) {
  return Request(url, {}, false, headers, options);
}

std::optional<int> RetryAfterSeconds(const HttpResponse& response) {
  const HttpHeader* header = response.Find("retry-after");
  if (!header || header->value.empty()) return std::nullopt;
  int seconds = 0;
  for (const char c : header->value) {
    if (c < '0' || c > '9' || seconds > 100000) return std::nullopt;
    seconds = seconds * 10 + (c - '0');
  }
  return seconds;
}

TransferFn PlatformTransfer() {
  return [](std::string_view url, const TransferReceiver& receiver) {
    return Transfer(url, receiver);
  };
}

}  // namespace rb_blitz::store
