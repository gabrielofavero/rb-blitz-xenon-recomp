// SPDX-License-Identifier: GPL-2.0-only
#include "platform/exit_deadline.h"

#include <string_view>
#include <thread>

using namespace std::chrono_literals;

struct BlockedDestructor {
  bool block = false;
  ~BlockedDestructor() { if (block) std::this_thread::sleep_for(5s); }
};
BlockedDestructor cleanup;

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string_view mode = argv[1];
  if (!rb_blitz::platform::ArmExitDeadline(mode == "normal" ? 2s : 100ms)) return 3;
  if (mode == "normal") return 7;
  if (mode == "destructor") {
    cleanup.block = true;
    return 7;
  }
  if (mode == "repeat" && !rb_blitz::platform::ArmExitDeadline(10s)) return 3;
  std::this_thread::sleep_for(5s);
  return 7;
}
