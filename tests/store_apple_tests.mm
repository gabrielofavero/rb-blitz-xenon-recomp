// SPDX-License-Identifier: GPL-2.0-only
// Exercise the iOS NSURLSession transport on the host, with no device or external network.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include "check.h"
#include "store/store_http.h"
#include "store/store_transfer.h"

@interface RBBlitzFixtureProtocol : NSURLProtocol
@end

@implementation RBBlitzFixtureProtocol
+ (BOOL)canInitWithRequest:(NSURLRequest*)request {
  return [request.URL.host isEqualToString:@"fixture.invalid"];
}
+ (NSURLRequest*)canonicalRequestForRequest:(NSURLRequest*)request { return request; }
- (void)startLoading {
  const bool large = [self.request.URL.path isEqualToString:@"/large"];
  NSData* body = large ? [NSMutableData dataWithLength:150000]
                      : [@"ok" dataUsingEncoding:NSUTF8StringEncoding];
  NSMutableDictionary* headers = [@{@"Content-Length": [@(body.length) stringValue],
                                     @"Content-Type": @"application/octet-stream"} mutableCopy];
  if ([self.request.URL.path isEqualToString:@"/encoded"]) headers[@"Content-Encoding"] = @"gzip";
  NSHTTPURLResponse* response = [[NSHTTPURLResponse alloc] initWithURL:self.request.URL
      statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:headers];
  [self.client URLProtocol:self didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageNotAllowed];
  [self.client URLProtocol:self didLoadData:body];
  [self.client URLProtocolDidFinishLoading:self];
}
- (void)stopLoading {}
@end

// NSURLSession configurations copy their protocol list. Inject the fixture at creation, keeping
// the production transport's session configuration and callbacks unchanged.
@interface NSURLSessionConfiguration (RBBlitzFixture)
+ (NSURLSessionConfiguration*)rb_blitz_fixtureConfiguration;
@end
@implementation NSURLSessionConfiguration (RBBlitzFixture)
+ (NSURLSessionConfiguration*)rb_blitz_fixtureConfiguration {
  NSURLSessionConfiguration* configuration = [self rb_blitz_fixtureConfiguration];
  configuration.protocolClasses = @[[RBBlitzFixtureProtocol class]];
  return configuration;
}
@end

int main() {
  @autoreleasepool {
    using namespace rb_blitz::store;
    using namespace rb_blitz::test;
    method_exchangeImplementations(
        class_getClassMethod([NSURLSessionConfiguration class], @selector(ephemeralSessionConfiguration)),
        class_getClassMethod([NSURLSessionConfiguration class], @selector(rb_blitz_fixtureConfiguration)));

    BeginCase("NSURLSession: bounded catalogue body and case-insensitive response headers");
    auto response = Get("https://fixture.invalid/ok", {});
    CHECK_TRUE(response.ok && response.status == 200 && response.body == "ok");
    CHECK_TRUE(response.Find("CONTENT-LENGTH") != nullptr);
    CHECK_TRUE(response.Find("absent") == nullptr);
    HttpOptions options;
    options.max_body_bytes = 4;
    response = Get("https://fixture.invalid/large", {}, options);
    CHECK_FALSE(response.ok);
    CHECK_TRUE(response.body.size() <= options.max_body_bytes);
    CHECK_TRUE(response.error.find("ceiling") != std::string::npos);

    BeginCase("NSURLSession: reject compressed bodies before decoded bytes reach policy");
    response = Get("https://fixture.invalid/encoded", {});
    CHECK_FALSE(response.ok);
    CHECK_TRUE(response.body.empty());

    BeginCase("NSURLSession: reject non-HTTPS public URLs and malformed headers without a request");
    CHECK_FALSE(Get("http://fixture.invalid/ok", {}).ok);
    CHECK_FALSE(Get("https://user:pass@fixture.invalid/ok", {}).ok);
    CHECK_FALSE(Get("file:///fixture.invalid/ok", {}).ok);
    CHECK_FALSE(Get("https://fixture.invalid/\xff", {}).ok);
    CHECK_FALSE(Get("https://fixture.invalid/ok", {{"Bad\r\nHeader", "value"}}).ok);
    CHECK_FALSE(Get("https://fixture.invalid/ok", {{"Header", "Bad\r\nValue"}}).ok);

    BeginCase("NSURLSession: stream packages in bounded chunks after exactly one head");
    std::size_t total = 0;
    std::size_t chunks = 0;
    std::size_t heads = 0;
    TransferReceiver receiver;
    receiver.on_head = [&](const auto& head) {
      ++heads;
      CHECK_TRUE(head.status == 200 && head.content_length == 150000);
      return true;
    };
    receiver.on_data = [&](auto data) {
      ++chunks;
      total += data.size();
      CHECK_TRUE(data.size() <= 65536);
      return true;
    };
    auto result = PlatformTransfer()("https://fixture.invalid/large", receiver);
    CHECK_TRUE(result.ok);
    CHECK_EQ(heads, 1u);
    CHECK_EQ(total, 150000u);
    CHECK_TRUE(chunks >= 3);

    BeginCase("NSURLSession: a policy head rejection stops delivery and preserves deliberate success");
    receiver.on_head = [](const auto&) { return false; };
    total = 0;
    result = PlatformTransfer()("https://fixture.invalid/large", receiver);
    CHECK_TRUE(result.ok);
    CHECK_EQ(total, 0u);

    BeginCase("NSURLSession: a policy body rejection stops subsequent chunks");
    receiver.on_head = [](const auto&) { return true; };
    receiver.on_data = [&](auto) { ++chunks; return false; };
    chunks = 0;
    result = PlatformTransfer()("https://fixture.invalid/large", receiver);
    CHECK_TRUE(result.ok);
    CHECK_EQ(chunks, 1u);
    CHECK_FALSE(PlatformTransfer()("http://127.0.0.1/payload", receiver).ok);
    return Finish();
  }
}
