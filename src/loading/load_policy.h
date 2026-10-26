// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace rb_blitz::loading {
inline bool ParseLoadGuardSeconds(std::string_view token, uint32_t* seconds) {
  if (token == "off") *seconds = 0;
  else if (token == "30s") *seconds = 30;
  else if (token == "60s") *seconds = 60;
  else return false;
  return true;
}
// Host config/commit publishes; each attempt takes one immutable snapshot.
inline std::atomic<uint32_t> load_guard_seconds{0};
inline bool PublishLoadGuardPolicy(std::string_view token) {
  uint32_t seconds = 0;
  if (!ParseLoadGuardSeconds(token, &seconds)) return false;
  load_guard_seconds.store(seconds, std::memory_order_release);
  return true;
}
inline uint32_t CurrentLoadGuardSeconds() {
  return load_guard_seconds.load(std::memory_order_acquire);
}
}  // namespace rb_blitz::loading
