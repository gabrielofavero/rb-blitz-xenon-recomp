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

// RndTex wrappers and Xbox sampler slots hold raw texture pointers. Rebind the
// existing objects through native setters before retiring the old resources.
// Texture order: two scene buffers, two resolves, then the depth texture.
template <typename ReadSampler, typename BindSampler, typename BindWrapper>
void RebindProfileTextureUsers(const std::array<uint32_t, 5>& old_textures,
                              const std::array<uint32_t, 5>& new_textures,
                              const std::array<uint32_t, 3>& wrappers,
                              ReadSampler&& read_sampler, BindSampler&& bind_sampler,
                              BindWrapper&& bind_wrapper) {
  constexpr std::array<size_t, 3> wrapper_textures{0, 1, 4};
  for (size_t i = 0; i < wrappers.size(); ++i)
    if (wrappers[i]) bind_wrapper(wrappers[i], new_textures[wrapper_textures[i]]);
  // Includes vertex texture stages; the native device initializes 26 slots.
  for (uint32_t stage = 0; stage < 26; ++stage) {
    const uint32_t texture = read_sampler(stage);
    if (!texture) continue;
    for (size_t i = 0; i < old_textures.size(); ++i) {
      if (texture != old_textures[i]) continue;
      bind_sampler(stage, new_textures[i]);
      break;
    }
  }
}

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
