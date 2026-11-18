// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rb_blitz::settings {

struct ProfileTextureLock {
  uint32_t pitch = 0;
  uint32_t data = 0;
};

// The title's texture lock writes pitch and data through its output argument;
// its return register is unspecified. Guest addresses may have their high bit set.
template <typename Lock, typename Clear, typename Unlock>
bool InitializeProfileResolveTextures(const std::array<uint32_t, 2>& resources,
                                     uint32_t width, uint32_t height,
                                     Lock&& lock, Clear&& clear, Unlock&& unlock) {
  const size_t bytes = ((size_t(width) + 31) / 32) * ((size_t(height) + 31) / 32) * 4096;
  for (const uint32_t resource : resources) {
    const ProfileTextureLock locked = lock(resource);
    if (!locked.data) return false;
    clear(locked.data, bytes);
    unlock(resource);
  }
  return true;
}

}  // namespace rb_blitz::settings
