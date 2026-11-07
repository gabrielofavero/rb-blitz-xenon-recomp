// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_update.h"

#include <cstdio>
#include <functional>
#include <string>

using rb_blitz::messages::OpenState;
using rb_blitz::messages::UpdateAction;
using rb_blitz::messages::kReleasesUrl;

namespace {
int failures = 0;
void Check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
}  // namespace

int main() {
  std::function<void()> pending;
  int opens = 0, closes = 0;
  std::string destination;
  bool close_after_open = false;
  UpdateAction action(
      [&](std::string_view url) {
        ++opens;
        destination = url;
        return true;
      },
      [&] {
        close_after_open = opens == 1 && destination == kReleasesUrl;
        ++closes;
        return true;
      },
      [&](std::function<void()> work) { pending = std::move(work); });
  Check(action.Press(), "first press queued");
  Check(action.state() == OpenState::kPending, "pending while opener has not run");
  Check(!action.Press(), "pending press refused");
  Check(opens == 0 && closes == 0, "guest press does not open or close inline");
  pending();
  Check(destination == kReleasesUrl, "opener receives only compiled Releases URL");
  Check(opens == 1 && closes == 1 && close_after_open,
        "successful handoff closes once after URL delivery");
  Check(action.state() == OpenState::kOpened, "successful state retained");
  Check(!action.Press() && opens == 1, "repeat press cannot open another tab");

  int failed_closes = 0, attempts = 0;
  UpdateAction failure(
      [&](std::string_view url) {
        Check(url == kReleasesUrl, "failure also uses fixed URL");
        ++attempts;
        return attempts > 1;
      },
      [&] {
        ++failed_closes;
        return true;
      },
      [&](std::function<void()> work) { pending = std::move(work); });
  Check(failure.Press(), "failure press queued");
  pending();
  Check(failure.state() == OpenState::kFailed && failed_closes == 0,
        "failed opener leaves game open and exposes error");
  Check(failure.Press(), "player can retry failed opener");
  pending();
  Check(failure.state() == OpenState::kOpened && attempts == 2 && failed_closes == 1,
        "successful retry closes once");
  std::printf("messages_update: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
