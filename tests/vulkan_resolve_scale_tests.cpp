// SPDX-License-Identifier: GPL-2.0-only
// Check resolve sizing and the native allocation without a window or guest.
#include "check.h"
#include "gpu/xenos/vulkan/resolve_scale.h"
#if defined(RBBLITZ_RESOLVE_NATIVE_TEST)
#include <vulkan/vulkan.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#endif

using namespace rb_blitz::test;
using rex::gpu::vulkan::ClampResolveScaleToAllocationLimit;
using rex::gpu::vulkan::ConservativeDenseResolveBudget;
using rex::gpu::vulkan::DenseResolveFitsBudget;
constexpr uint64_t kGuestMemory = uint64_t(512) << 20;

#if defined(RBBLITZ_RESOLVE_NATIVE_TEST)
void NativeAllocation() {
  auto require = [](bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
  };
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  const char* portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#if defined(__APPLE__)
  info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = &portability;
#else
  (void)portability;
#endif
  VkInstance instance = VK_NULL_HANDLE;
  require(vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS, "create Vulkan instance");
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  require(count != 0, "find native GPU");
  std::vector<VkPhysicalDevice> physical(count);
  vkEnumeratePhysicalDevices(instance, &count, physical.data());
  VkPhysicalDeviceMaintenance3Properties allocation{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
  VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  properties.pNext = &allocation;
  vkGetPhysicalDeviceProperties2(physical[0], &properties);
  require(allocation.maxMemoryAllocationSize != 0, "read native allocation limit");
  uint32_t x = 3, y = 3;
  ClampResolveScaleToAllocationLimit(kGuestMemory, allocation.maxMemoryAllocationSize, false, x, y);
  // At 1x there is no scaled resolve allocation; exercise the unscaled shared buffer.
  const uint64_t bytes = kGuestMemory * x * y;
  CHECK_TRUE(bytes <= allocation.maxMemoryAllocationSize);
  std::printf("GPU=%s max_allocation=%llu requested=3x3 effective=%ux%u bytes=%llu\n",
      properties.properties.deviceName,
      static_cast<unsigned long long>(allocation.maxMemoryAllocationSize), x, y,
      static_cast<unsigned long long>(bytes));

  uint32_t queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &queue_count, nullptr);
  std::vector<VkQueueFamilyProperties> queues(queue_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &queue_count, queues.data());
  uint32_t family = 0;
  while (family < queue_count && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
  require(family < queue_count, "find compute queue");
  float priority = 1;
  VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue.queueFamilyIndex = family;
  queue.queueCount = 1;
  queue.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue;
  uint32_t extension_count = 0;
  vkEnumerateDeviceExtensionProperties(physical[0], nullptr, &extension_count, nullptr);
  std::vector<VkExtensionProperties> extensions(extension_count);
  vkEnumerateDeviceExtensionProperties(physical[0], nullptr, &extension_count, extensions.data());
  const char* subset = "VK_KHR_portability_subset";
  for (const auto& extension : extensions) {
    if (std::strcmp(extension.extensionName, subset) == 0) {
      device_info.enabledExtensionCount = 1;
      device_info.ppEnabledExtensionNames = &subset;
    }
  }
  VkDevice device = VK_NULL_HANDLE;
  require(vkCreateDevice(physical[0], &device_info, nullptr, &device) == VK_SUCCESS,
          "create native Vulkan device");
  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size = bytes;
  buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkBuffer buffer = VK_NULL_HANDLE;
  require(vkCreateBuffer(device, &buffer_info, nullptr, &buffer) == VK_SUCCESS,
          "create effective resolution buffer");
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical[0], &memory_properties);
  uint32_t type = 0;
  while (type < memory_properties.memoryTypeCount &&
         (!(requirements.memoryTypeBits & (1u << type)) ||
          !(memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
    ++type;
  require(type < memory_properties.memoryTypeCount, "find device local memory");
  VkMemoryAllocateInfo memory_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  memory_info.allocationSize = requirements.size;
  memory_info.memoryTypeIndex = type;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  require(vkAllocateMemory(device, &memory_info, nullptr, &memory) == VK_SUCCESS,
          "allocate effective resolution buffer");
  CHECK_TRUE(vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS);
  vkDestroyBuffer(device, buffer, nullptr);
  vkFreeMemory(device, memory, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);
}
#endif

int main(int argc, char** argv) {
#if defined(RBBLITZ_RESOLVE_NATIVE_TEST)
  if (argc == 2 && std::strcmp(argv[1], "--native") == 0) {
    BeginCase("effective resolution allocates on the native GPU");
    try { NativeAllocation(); }
    catch (const std::exception& error) { Fail(__FILE__, __LINE__, error.what()); }
    return Finish();
  }
#else
  (void)argc;
  (void)argv;
#endif
  BeginCase("3x startup respects the non-sparse allocation limit");
  uint32_t x = 3, y = 3;
  CHECK_TRUE(ClampResolveScaleToAllocationLimit(kGuestMemory, uint64_t(1) << 30, false, x, y));
  CHECK_EQ(x, 1u);
  CHECK_EQ(y, 1u);
  x = y = 3;
  CHECK_TRUE(ClampResolveScaleToAllocationLimit(kGuestMemory, uint64_t(2) << 30, false, x, y));
  CHECK_EQ(x, 2u);
  CHECK_EQ(y, 2u);
  BeginCase("exact limits, unknown limits and sparse buffers preserve the request");
  for (auto limit : {uint64_t(0), kGuestMemory * 9}) {
    x = y = 3;
    CHECK_FALSE(ClampResolveScaleToAllocationLimit(kGuestMemory, limit, false, x, y));
    CHECK_EQ(x, 3u);
    CHECK_EQ(y, 3u);
  }
  x = y = 3;
  CHECK_FALSE(ClampResolveScaleToAllocationLimit(kGuestMemory, uint64_t(1) << 30, true, x, y));
  CHECK_EQ(x, 3u);
  CHECK_EQ(y, 3u);
  BeginCase("strict dense admission reserves headroom using live or physical heap limits");
  const auto gib = uint64_t(1) << 30;
  CHECK_EQ(ConservativeDenseResolveBudget(8 * gib, 0, 0), 2 * gib);
  CHECK_EQ(ConservativeDenseResolveBudget(8 * gib, 6 * gib, 2 * gib), 2 * gib);
  CHECK_EQ(ConservativeDenseResolveBudget(8 * gib, 12 * gib, 2 * gib), 3 * gib);
  CHECK_EQ(ConservativeDenseResolveBudget(8 * gib, 6 * gib, 6 * gib), uint64_t(0));
  CHECK_EQ(ConservativeDenseResolveBudget(8 * gib, 6 * gib, 7 * gib), uint64_t(0));
  CHECK_TRUE(DenseResolveFitsBudget(kGuestMemory, 1, 1, 0));
  CHECK_TRUE(DenseResolveFitsBudget(kGuestMemory, 2, 2, 2 * gib));
  CHECK_FALSE(DenseResolveFitsBudget(kGuestMemory, 2, 2, 2 * gib - 1));
  CHECK_FALSE(DenseResolveFitsBudget(kGuestMemory, 4, 4, 2 * gib));
  CHECK_FALSE(DenseResolveFitsBudget(kGuestMemory, 2, 2, 0));
  CHECK_FALSE(DenseResolveFitsBudget(0, 2, 2, 2 * gib));
  BeginCase("strict budget checks avoid multiplication overflow at large limits");
  CHECK_FALSE(DenseResolveFitsBudget(UINT64_MAX, 7, 7, UINT64_MAX));
  CHECK_TRUE(DenseResolveFitsBudget(UINT64_MAX / 49, 7, 7, UINT64_MAX));
  BeginCase("all supported scale pairs stay in range and do not exceed a known limit");
  for (uint32_t initial_x = 1; initial_x <= 8; ++initial_x) {
    for (uint32_t initial_y = 1; initial_y <= 8; ++initial_y) {
      for (auto limit : {kGuestMemory, kGuestMemory * 2, kGuestMemory * 4,
                         kGuestMemory * 9 - 1, kGuestMemory * 16}) {
        x = initial_x;
        y = initial_y;
        ClampResolveScaleToAllocationLimit(kGuestMemory, limit, false, x, y);
        CHECK_TRUE(x >= 1 && x <= initial_x && y >= 1 && y <= initial_y);
        CHECK_TRUE(kGuestMemory * x * y <= limit);
      }
    }
  }
  return Finish();
}
