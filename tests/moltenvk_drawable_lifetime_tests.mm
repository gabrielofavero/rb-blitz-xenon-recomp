// SPDX-License-Identifier: GPL-2.0-only
#import <Foundation/Foundation.h>
#include "check.h"
#include "MVKRetainedObject.h"
#include <atomic>
#include <thread>

static std::atomic<int> destroyed{0};
@class TestDrawable;
static MVKRetainedObject<TestDrawable*>* reenter_on_dealloc = nullptr;
@interface TestDrawable : NSObject
@end
@implementation TestDrawable
- (void)dealloc {
  if (reenter_on_dealloc) reenter_on_dealloc->clear();
  ++destroyed;
  [super dealloc];
}
@end

int main() {
  using namespace rb_blitz::test;
  @autoreleasepool {
    BeginCase("presentation keeps a drawable alive after concurrent acquisition retires it");
    MVKRetainedObject<TestDrawable*> slot;
    TestDrawable* candidate = [TestDrawable new];
    TestDrawable* snapshot = slot.publishIfEmptyAndGetRetained(candidate);
    [candidate release];
    std::thread retire([&] { slot.clear(); });
    retire.join();
    CHECK_EQ(destroyed.load(), 0);
    void (^scheduled)() = [^{ CHECK_TRUE([snapshot isKindOfClass:TestDrawable.class]); } copy];
    [snapshot release];
    CHECK_EQ(destroyed.load(), 0);
    scheduled();
    [scheduled release];
    CHECK_EQ(destroyed.load(), 1);

    BeginCase("a concurrent publisher reuses the owned object and releases its unused candidate");
    TestDrawable* a = [TestDrawable new];
    TestDrawable* b = [TestDrawable new];
    TestDrawable* first = slot.publishIfEmptyAndGetRetained(a);
    TestDrawable* second = slot.publishIfEmptyAndGetRetained(b);
    CHECK_TRUE(first == second);
    [a release]; [b release]; [first release]; [second release];
    slot.clear();
    CHECK_EQ(destroyed.load(), 3);

    BeginCase("retirement callbacks can reenter the owner without deadlock");
    TestDrawable* callbackObject = [TestDrawable new];
    TestDrawable* callbackRef = slot.publishIfEmptyAndGetRetained(callbackObject);
    [callbackObject release]; [callbackRef release];
    reenter_on_dealloc = &slot;
    slot.clear();
    reenter_on_dealloc = nullptr;
    CHECK_EQ(destroyed.load(), 4);

    BeginCase("retained snapshots survive repeated concurrent clear and publish");
    constexpr int count = 10000;
    std::atomic<bool> done{false};
    std::thread reader([&] {
      while (!done.load()) {
        TestDrawable* object = slot.getRetained();
        if (object && ![object isKindOfClass:TestDrawable.class]) std::abort();
        [object release];
      }
    });
    for (int i = 0; i < count; ++i) {
      TestDrawable* object = [TestDrawable new];
      TestDrawable* retained = slot.publishIfEmptyAndGetRetained(object);
      [object release]; [retained release];
      slot.clear();
    }
    done = true;
    reader.join();
    CHECK_EQ(destroyed.load(), count + 4);
  }
  return Finish();
}
