// SPDX-License-Identifier: GPL-2.0-only
#include "platform/ios_ultimate_download.h"
#include <fstream>
#import <Foundation/Foundation.h>

// A serial URLSession delegate streams to disk, never holding the release ZIP
// in memory. The shared installer verifies size and SHA-256 before extraction.
@interface RBBlitzUltimateDownload : NSObject <NSURLSessionDataDelegate> {
 @public
  std::ofstream stream;
  rb_blitz::installer::portable::ImportCallbacks callbacks;
  uint64_t received;
  uint64_t limit;
  unsigned redirects;
}
@property(nonatomic, strong) dispatch_semaphore_t finished;
@property(nonatomic, copy) NSString* failure;
@end

@implementation RBBlitzUltimateDownload
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
    completionHandler:(void (^)(NSURLSessionResponseDisposition))completion {
  NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
  if (![response isKindOfClass:NSHTTPURLResponse.class] || http.statusCode != 200 ||
      (response.expectedContentLength >= 0 && uint64_t(response.expectedContentLength) != limit)) {
    self.failure = @"The Ultimate server returned an unexpected response or file size.";
    completion(NSURLSessionResponseCancel);
    return;
  }
  completion(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response newRequest:(NSURLRequest*)request
    completionHandler:(void (^)(NSURLRequest*))completion {
  if (++redirects > 5 || ![request.URL.scheme.lowercaseString isEqualToString:@"https"]) {
    self.failure = @"The Ultimate download redirected to an unsupported address.";
    completion(nil);
  } else {
    completion(request);
  }
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task
    didReceiveData:(NSData*)data {
  if (data.length > limit - received || (callbacks.cancelled && callbacks.cancelled())) {
    self.failure = @"The Ultimate download was canceled or exceeded its expected size.";
    [task cancel];
    return;
  }
  stream.write(static_cast<const char*>(data.bytes), data.length);
  if (!stream) {
    self.failure = @"Could not save the Ultimate download. Check available storage.";
    [task cancel];
    return;
  }
  received += data.length;
  if (callbacks.progress) callbacks.progress(int(40 * received / limit), "Downloading Ultimate");
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
  stream.close();
  if (!self.failure && error) self.failure = error.localizedDescription;
  if (!self.failure && stream.fail()) self.failure = @"Could not finish saving the Ultimate download.";
  dispatch_semaphore_signal(self.finished);
}
@end

namespace rb_blitz::platform {
bool DownloadIOSUltimate(const std::filesystem::path& destination,
                        const installer::PinsUltimate& pins,
                        const installer::portable::ImportCallbacks& callbacks,
                        std::string* error) {
  @autoreleasepool {
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:pins.url.c_str()]];
    if (![url.scheme.lowercaseString isEqualToString:@"https"] || !pins.size) {
      if (error) *error = "The Ultimate download configuration is invalid";
      return false;
    }
    RBBlitzUltimateDownload* delegate = [RBBlitzUltimateDownload new];
    delegate->callbacks = callbacks;
    delegate->limit = pins.size;
    delegate->stream.open(destination, std::ios::binary | std::ios::trunc);
    if (!delegate->stream) {
      if (error) *error = "Cannot create the Ultimate download file";
      return false;
    }
    delegate.finished = dispatch_semaphore_create(0);
    NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
    configuration.timeoutIntervalForRequest = 60;
    configuration.timeoutIntervalForResource = 30 * 60;
    NSOperationQueue* queue = [NSOperationQueue new];
    queue.maxConcurrentOperationCount = 1;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration delegate:delegate delegateQueue:queue];
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    [request setValue:@"rb_blitz_ios_importer/0.1" forHTTPHeaderField:@"User-Agent"];
    NSURLSessionDataTask* task = [session dataTaskWithRequest:request];
    [task resume];
    // This runs only on the importer worker; UIKit and the delegate stay free.
    while (dispatch_semaphore_wait(delegate.finished, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC))) {
      if (callbacks.cancelled && callbacks.cancelled()) [task cancel];
    }
    [session finishTasksAndInvalidate];
    if (delegate.failure) {
      if (error) *error = delegate.failure.UTF8String;
      return false;
    }
    return true;
  }
}
}  // namespace rb_blitz::platform
