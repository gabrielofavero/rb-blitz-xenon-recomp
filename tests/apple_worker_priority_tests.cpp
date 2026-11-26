// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include <future>
#include <pthread.h>
#include <rex/platform/thread.h>
#include <rex/platform/sync.h>

int main() {
  using namespace rb_blitz::test;
  BeginCase("Apple workers receive their requested QoS before executing work");
  for (int priority : {-2, -1, 1, 2}) {
    std::promise<qos_class_t> result;
    auto done = result.get_future();
    auto thread = rex::platform::Thread::Create({.initial_priority = priority}, [&] {
      qos_class_t qos = QOS_CLASS_UNSPECIFIED;
      int relative = 0;
      pthread_get_qos_class_np(pthread_self(), &qos, &relative);
      result.set_value(qos);
    });
    CHECK_TRUE(thread != nullptr);
    if (!thread) continue;
    CHECK_EQ(done.get(), priority < 0 ? (priority < -1 ? QOS_CLASS_BACKGROUND : QOS_CLASS_UTILITY) :
        (priority > 1 ? QOS_CLASS_USER_INTERACTIVE : QOS_CLASS_USER_INITIATED));
    CHECK_TRUE(rex::platform::Wait(thread.get(), false, std::chrono::seconds(2)) ==
               rex::platform::WaitResult::kSuccess);
  }
  return Finish();
}
