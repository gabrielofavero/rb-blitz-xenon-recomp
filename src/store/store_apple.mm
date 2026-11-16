// SPDX-License-Identifier: GPL-2.0-only
// iOS HTTP transport. Callers run these synchronous entry points on their existing workers;
// NSURLSession delivers bounded catalogue bodies and streaming packages on its delegate queue.
#import <Foundation/Foundation.h>

#include "store/store_http.h"
#include "store/store_transfer.h"
#include "store/rhythmverse_client.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
using namespace rb_blitz::store;

std::string Lower(std::string text) {
  for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return text;
}

NSString* NativeString(std::string_view text) {
  return [[NSString alloc] initWithBytes:text.data() length:text.size()
                               encoding:NSUTF8StringEncoding];
}

std::string Utf8(NSString* text) {
  const char* utf8 = text.UTF8String;
  return utf8 ? utf8 : "";
}

NSURL* AllowedUrl(std::string_view text, bool allow_loopback) {
  if (text.empty() || std::any_of(text.begin(), text.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f;
      })) return nil;
  NSString* native_text = NativeString(text);
  if (!native_text) return nil;
  NSURLComponents* parsed = [NSURLComponents componentsWithString:native_text];
  if (!parsed || parsed.user != nil || parsed.password != nil || !parsed.host.length) return nil;
  const std::string scheme = Lower(Utf8(parsed.scheme));
  if (scheme != "https" &&
      !(allow_loopback && scheme == "http" && IsLoopbackHost(Utf8(parsed.host)))) return nil;
  return parsed.URL;
}

struct Exchange {
  HttpResponse response;
  std::size_t body_limit = 0;
  const TransferReceiver* receiver = nullptr;
  bool stopped = false;
  bool head_received = false;
  dispatch_semaphore_t finished = dispatch_semaphore_create(0);
};

const HttpHeader* FindHeader(const std::vector<HttpHeader>& headers, std::string_view name) {
  for (const auto& header : headers) if (header.name == name) return &header;
  return nullptr;
}

TransferHead TransferHeaders(const HttpResponse& response) {
  TransferHead head;
  head.status = response.status;
  for (const auto& header : response.headers) {
    if (header.name == "content-length") {
      std::int64_t length = 0;
      const char* end = header.value.data() + header.value.size();
      const auto parsed = std::from_chars(header.value.data(), end, length);
      if (parsed.ec == std::errc{} && parsed.ptr == end && length > 0) head.content_length = length;
    } else if (header.name == "content-type") head.content_type = Lower(header.value);
    else if (header.name == "content-disposition") head.content_disposition = header.value;
    else if (header.name == "content-encoding") head.content_encoding = header.value;
    else if (header.name == "location") head.location = header.value;
    else if (header.name == "retry-after") head.retry_after = header.value;
  }
  return head;
}
}  // namespace

@interface RBBlitzHttpDelegate : NSObject <NSURLSessionDataDelegate> {
 @public
  Exchange* exchange;
}
@end

@implementation RBBlitzHttpDelegate
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response newRequest:(NSURLRequest*)request
    completionHandler:(void (^)(NSURLRequest*))completionHandler {
  // The caller validates every Location before deciding whether to make another request.
  completionHandler(nil);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
    completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
  if (![response isKindOfClass:[NSHTTPURLResponse class]]) {
    exchange->response.error = "no HTTP status line";
    completionHandler(NSURLSessionResponseCancel);
    return;
  }
  NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
  exchange->response.status = static_cast<int>(http.statusCode);
  exchange->head_received = true;
  for (NSString* name in http.allHeaderFields) {
    exchange->response.headers.push_back(
        {Lower(Utf8(name)), Utf8([http.allHeaderFields[name] description])});
  }
  if (exchange->receiver) {
    const TransferHead head = TransferHeaders(exchange->response);
    if ((exchange->receiver->on_head && !exchange->receiver->on_head(head)) || head.status != 200) {
      exchange->stopped = true;
      completionHandler(NSURLSessionResponseCancel);
      return;
    }
  }
  // NSURLSession decodes compressed responses. Request identity and reject an unsolicited
  // encoding before any bytes reach callers, keeping their raw length/hash checks truthful.
  const auto* encoding = FindHeader(exchange->response.headers, "content-encoding");
  if (encoding && !encoding->value.empty() && Lower(encoding->value) != "identity") {
    exchange->response.error = "unexpected HTTP content encoding";
    completionHandler(NSURLSessionResponseCancel);
    return;
  }
  completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task
    didReceiveData:(NSData*)data {
  if (exchange->stopped || !exchange->response.error.empty()) return;
  if (exchange->receiver) {
    const auto* bytes = static_cast<const std::uint8_t*>(data.bytes);
    for (std::size_t offset = 0; offset < data.length;) {
      const auto count = std::min<std::size_t>(64 * 1024, data.length - offset);
      if (exchange->receiver->on_data &&
          !exchange->receiver->on_data(std::span<const std::uint8_t>(bytes + offset, count))) {
        exchange->stopped = true;
        [task cancel];
        return;
      }
      offset += count;
    }
  } else {
    if (data.length > exchange->body_limit -
                          std::min(exchange->body_limit, exchange->response.body.size())) {
      exchange->response.error = "the response exceeds the " +
          std::to_string(exchange->body_limit) + "-byte ceiling";
      [task cancel];
      return;
    }
    exchange->response.body.append(static_cast<const char*>(data.bytes), data.length);
  }
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
  if (error && !exchange->stopped && exchange->response.error.empty())
    exchange->response.error = Utf8(error.localizedDescription);
  if (!exchange->head_received && exchange->response.error.empty())
    exchange->response.error = "no HTTP status line";
  exchange->response.ok = exchange->head_received && exchange->response.error.empty();
  dispatch_semaphore_signal(exchange->finished);
}
@end

namespace rb_blitz::store {
namespace {

void Perform(NSMutableURLRequest* request, Exchange* exchange, double timeout_seconds) {
  NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
  configuration.timeoutIntervalForRequest = timeout_seconds;
  configuration.timeoutIntervalForResource = timeout_seconds;
  configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
  configuration.URLCache = nil;
  configuration.HTTPCookieStorage = nil;
  configuration.URLCredentialStorage = nil;
  NSOperationQueue* queue = [[NSOperationQueue alloc] init];
  queue.maxConcurrentOperationCount = 1;
  RBBlitzHttpDelegate* delegate = [[RBBlitzHttpDelegate alloc] init];
  delegate->exchange = exchange;
  NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration delegate:delegate
                                               delegateQueue:queue];
  [[session dataTaskWithRequest:request] resume];
  dispatch_semaphore_wait(exchange->finished, DISPATCH_TIME_FOREVER);
  // The last callback has published the result. Wait for its operation to return before the
  // stack-owned exchange/receiver can go out of scope.
  [session finishTasksAndInvalidate];
  [queue waitUntilAllOperationsAreFinished];
}

HttpResponse Request(std::string_view url, std::string_view body, bool post,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     const HttpOptions& options) {
  @autoreleasepool {
    Exchange exchange;
    exchange.body_limit = options.max_body_bytes;
    NSURL* native_url = AllowedUrl(url, true);
    if (!native_url) {
      exchange.response.error = "the transport only speaks HTTPS or loopback HTTP";
      return exchange.response;
    }
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:native_url];
    [request setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
    for (const auto& [name, value] : headers) {
      if (name.empty() || name.find_first_of("\r\n:") != std::string::npos ||
          name.find('\0') != std::string::npos || value.find_first_of("\r\n") != std::string::npos ||
          value.find('\0') != std::string::npos || !NativeString(name) || !NativeString(value)) {
        exchange.response.error = "invalid HTTP request headers";
        return exchange.response;
      }
      [request setValue:NativeString(value) forHTTPHeaderField:NativeString(name)];
    }
    std::string agent;
    if (options.user_agent) {
      for (const wchar_t* c = options.user_agent; *c; ++c) {
        if (*c < 0x20 || *c > 0x7e) {
          exchange.response.error = "invalid user agent";
          return exchange.response;
        }
        agent.push_back(static_cast<char>(*c));
      }
    }
    [request setValue:NativeString(agent) forHTTPHeaderField:@"User-Agent"];
    if (post) {
      request.HTTPMethod = @"POST";
      request.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
    }
    const double timeout = (double(std::max(1, options.connect_timeout_ms)) +
                            std::max(1, options.send_timeout_ms) +
                            std::max(1, options.receive_timeout_ms)) / 1000.0;
    Perform(request, &exchange, timeout);
    return exchange.response;
  }
}
}  // namespace

const HttpHeader* HttpResponse::Find(std::string_view name) const {
  return FindHeader(headers, Lower(std::string(name)));
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
  const auto* header = response.Find("retry-after");
  if (!header || header->value.empty()) return std::nullopt;
  int seconds = 0;
  for (const char c : header->value) {
    if (c < '0' || c > '9' || seconds > 100000) return std::nullopt;
    seconds = seconds * 10 + c - '0';
  }
  return seconds;
}

TransferFn PlatformTransfer() {
  return [](std::string_view url, const TransferReceiver& receiver) -> TransferResult {
    @autoreleasepool {
      NSURL* native_url = AllowedUrl(url, false);
      if (!native_url) return {false, "the transport only speaks HTTPS"};
      Exchange exchange;
      exchange.receiver = &receiver;
      NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:native_url];
      [request setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
      [request setValue:@"rb-blitz-store/0.1 (+https://rhythmverse.co/)"
          forHTTPHeaderField:@"User-Agent"];
      Perform(request, &exchange, 1810.0);
      return {exchange.stopped || exchange.response.ok, exchange.response.error};
    }
  };
}
}  // namespace rb_blitz::store
