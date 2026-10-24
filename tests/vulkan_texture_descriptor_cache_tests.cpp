// SPDX-License-Identifier: GPL-2.0-only
// Tests the actual cache/key used by the Vulkan command processor; no GPU or UI.
#include "check.h"
#include <rex/graphics/vulkan/texture_descriptor_cache.h>
#include <rex/graphics/vulkan/descriptor_binding_state.h>
#include <array>
#include <type_traits>

using namespace rb_blitz::test;
using rex::graphics::vulkan::TextureDescriptorCache;
using rex::graphics::vulkan::DescriptorSetPrefixMask;
using rex::graphics::vulkan::RetainDescriptorSetBindings;

template <typename T> T Handle(uintptr_t id) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(id);
  else return static_cast<T>(id);
}

int main() {
  BeginCase("descriptor identity includes layout, image views/order/layouts and samplers");
  TextureDescriptorCache cache;
  std::array<VkDescriptorImageInfo, 2> textures{};
  textures[0].imageView = Handle<VkImageView>(10);
  textures[1].imageView = Handle<VkImageView>(11);
  for (auto& texture : textures) texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  std::array<VkDescriptorImageInfo, 1> samplers{};
  samplers[0].sampler = Handle<VkSampler>(20);
  auto layout = Handle<VkDescriptorSetLayout>(30);
  auto set = Handle<VkDescriptorSet>(40);
  TextureDescriptorCache::View view{layout, textures, samplers};
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  cache.Insert(view, set);
  CHECK_TRUE(cache.Find(view) == set);
  CHECK_EQ(cache.size(), 1u);
  CHECK_TRUE(cache.Find({Handle<VkDescriptorSetLayout>(31), textures, samplers}) == VK_NULL_HANDLE);
  std::swap(textures[0], textures[1]);
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  std::swap(textures[0], textures[1]);
  textures[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  textures[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  auto old_view = textures[0].imageView;
  textures[0].imageView = Handle<VkImageView>(12);  // different signed/swizzled view
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  textures[0].imageView = old_view;
  samplers[0].sampler = Handle<VkSampler>(21);
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  samplers[0].sampler = Handle<VkSampler>(20);
  CHECK_TRUE(cache.Find({layout, {textures.data(), 1}, samplers}) == VK_NULL_HANDLE);
  CHECK_TRUE(cache.Find({layout, textures, {}}) == VK_NULL_HANDLE);

  BeginCase("unused fields do not split identity; inserted keys own their contents");
  textures[0].sampler = Handle<VkSampler>(999);
  samplers[0].imageView = Handle<VkImageView>(999);
  samplers[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  CHECK_TRUE(cache.Find(view) == set);
  cache.Insert(view, Handle<VkDescriptorSet>(41));
  CHECK_TRUE(cache.Find(view) == set);  // sets remain immutable
  textures[0].imageView = Handle<VkImageView>(100);
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  textures[0].imageView = old_view;
  CHECK_TRUE(cache.Find(view) == set);

  BeginCase("clear expires every descriptor before submission/frame/pool reuse");
  cache.Clear();
  CHECK_EQ(cache.size(), 0u);
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  cache.Insert(view, Handle<VkDescriptorSet>(42));
  CHECK_TRUE(cache.Find(view) == Handle<VkDescriptorSet>(42));

  BeginCase("cache is bounded and a full cache retains valid hits");
  TextureDescriptorCache bounded(1);
  bounded.Insert(view, set);
  textures[0].imageView = Handle<VkImageView>(101);
  bounded.Insert(view, Handle<VkDescriptorSet>(43));
  CHECK_TRUE(bounded.Find(view) == VK_NULL_HANDLE);
  CHECK_EQ(bounded.size(), 1u);
  textures[0].imageView = old_view;
  CHECK_TRUE(bounded.Find(view) == set);
  TextureDescriptorCache empty(0);
  empty.Insert(view, set);
  CHECK_EQ(empty.size(), 0u);
  cache.Clear();
  cache.Insert(view, VK_NULL_HANDLE);
  CHECK_EQ(cache.size(), 0u);

  BeginCase("alternating bindings avoid repeated allocations and writes within a submission");
  uint32_t misses = 0, hits = 0;
  for (uint32_t draw = 0; draw < 10000; ++draw) {
    samplers[0].sampler = Handle<VkSampler>(20 + draw % 2);
    auto found = cache.Find(view);
    if (found == VK_NULL_HANDLE) {
      ++misses;
      found = Handle<VkDescriptorSet>(1000 + misses);
      cache.Insert(view, found);
    } else {
      ++hits;
    }
    CHECK_TRUE(found == Handle<VkDescriptorSet>(1001 + draw % 2));
  }
  CHECK_EQ(misses, 2u);
  CHECK_EQ(hits, 9998u);
  cache.Clear();
  CHECK_TRUE(cache.Find(view) == VK_NULL_HANDLE);
  BeginCase("retain only unchanged, valid sets that were already bound");
  std::array<VkDescriptorSet, 4> previous{}, current{};
  for (uint32_t i = 0; i < previous.size(); ++i) previous[i] = Handle<VkDescriptorSet>(100 + i);
  for (uint32_t changed = 0; changed < 16; ++changed) {
    current = previous;
    for (uint32_t i = 0; i < current.size(); ++i) {
      if (changed & (1u << i)) current[i] = Handle<VkDescriptorSet>(200 + i);
    }
    for (uint32_t bound = 0; bound < 16; ++bound) {
      for (uint32_t valid = 0; valid < 16; ++valid) {
        CHECK_EQ(RetainDescriptorSetBindings(bound, valid, previous, current),
                 bound & valid & ~changed);
      }
    }
  }

  BeginCase("incompatible layout prefixes and external rendering require rebinding");
  current = previous;
  CHECK_EQ(DescriptorSetPrefixMask(0), 0u);
  CHECK_EQ(DescriptorSetPrefixMask(32), UINT32_MAX);
  CHECK_EQ(DescriptorSetPrefixMask(33), UINT32_MAX);
  // Vertex layout changes at set 2: the pixel set at 3 must be rebound even
  // when its own set handle/layout happens to match the previous draw.
  CHECK_EQ(RetainDescriptorSetBindings(15 & DescriptorSetPrefixMask(2), 15,
                                      previous, current), 3u);
  CHECK_EQ(RetainDescriptorSetBindings(15 & DescriptorSetPrefixMask(3), 15,
                                      previous, current), 7u);
  CHECK_EQ(RetainDescriptorSetBindings(0, 15, previous, current), 0u);
  CHECK_EQ(RetainDescriptorSetBindings(15, 15, {}, {}), 0u);

  BeginCase("identical consecutive draws need only the first descriptor bind");
  uint32_t bound = 0, bind_batches = 0;
  for (uint32_t draw = 0; draw < 10000; ++draw) {
    bound = RetainDescriptorSetBindings(bound, 15, previous, current);
    if ((15 & ~bound) != 0) ++bind_batches;
    bound |= 15;
  }
  CHECK_EQ(bind_batches, 1u);
  // A fresh command buffer must bind again, regardless of cache contents.
  bound = RetainDescriptorSetBindings(0, 15, previous, current);
  CHECK_EQ(bound, 0u);

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
