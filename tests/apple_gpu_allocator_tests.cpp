// SPDX-License-Identifier: GPL-2.0-only
// Compare retained native allocator blocks without launching a game or window.
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
#include "gpu/xenos/host/vulkan/memory_policy.h"
#include "check.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

using namespace rb_blitz::test;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Allocation { VkBuffer buffer; VmaAllocation memory; };
uint64_t Retained(VkInstance instance, VkPhysicalDevice physical, VkDevice device, uint64_t block) {
  VmaVulkanFunctions functions{};
  functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
  VmaAllocatorCreateInfo info{};
  info.instance = instance; info.physicalDevice = physical; info.device = device;
  info.vulkanApiVersion = VK_API_VERSION_1_1;
  info.pVulkanFunctions = &functions;
  info.preferredLargeHeapBlockSize = block;
  VmaAllocator allocator;
  Require(vmaCreateAllocator(&info, &allocator) == VK_SUCCESS, "create allocator");
  std::vector<Allocation> allocations;
  for (int i = 0; i < 96; ++i) {
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = 4 << 20; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocation{}; allocation.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    Allocation result{};
    Require(vmaCreateBuffer(allocator, &buffer, &allocation, &result.buffer, &result.memory, nullptr)
        == VK_SUCCESS, "allocate native buffer");
    allocations.push_back(result);
  }
  // Retain a live allocation after a loading burst, as the texture cache does.
  for (size_t i = 0; i + 1 < allocations.size(); ++i)
    vmaDestroyBuffer(allocator, allocations[i].buffer, allocations[i].memory);
  VmaTotalStatistics statistics{};
  vmaCalculateStatistics(allocator, &statistics);
  const auto bytes = statistics.total.statistics.blockBytes;
  CHECK_EQ(statistics.total.statistics.allocationBytes, 4 << 20);
  vmaDestroyBuffer(allocator, allocations.back().buffer, allocations.back().memory);
  vmaDestroyAllocator(allocator);
  return bytes;
}
void HostBufferRoundTrip(VkPhysicalDevice physical, VkDevice device, uint32_t family) {
  BeginCase("imported MoltenVK host pages preserve GPU writes at both ends of a large buffer");
  VkPhysicalDeviceExternalMemoryHostPropertiesEXT host{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
  VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  properties.pNext = &host;
  vkGetPhysicalDeviceProperties2(physical, &properties);
  Require(host.minImportedHostPointerAlignment != 0, "host pointer alignment");
  const uint64_t bytes = uint64_t(512) << 20;
  const size_t alignment = std::max(size_t(getpagesize()), size_t(host.minImportedHostPointerAlignment));
  void* reserved = mmap(nullptr, bytes + alignment, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  Require(reserved != MAP_FAILED, "reserve host buffer pages");
  const uintptr_t address = (uintptr_t(reserved) + alignment - 1) & ~(uintptr_t(alignment) - 1);
  void* pointer = reinterpret_cast<void*>(address);
  VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
  external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.pNext = &external;
  buffer_info.size = bytes;
  buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer;
  Require(vkCreateBuffer(device, &buffer_info, nullptr, &buffer) == VK_SUCCESS, "create imported buffer");
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  Require(requirements.size <= bytes, "imported allocation fits host reservation");
  auto get_host = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(
      vkGetDeviceProcAddr(device, "vkGetMemoryHostPointerPropertiesEXT"));
  Require(get_host != nullptr, "host pointer query function");
  VkMemoryHostPointerPropertiesEXT pointer_properties{VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
  Require(get_host(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, pointer,
                  &pointer_properties) == VK_SUCCESS, "query compatible host memory types");
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
  uint32_t type = 0;
  const uint32_t bits = requirements.memoryTypeBits & pointer_properties.memoryTypeBits;
  while (type < memory_properties.memoryTypeCount &&
         (!(bits & (1u << type)) ||
          !(memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) ++type;
  Require(type < memory_properties.memoryTypeCount, "coherent imported type");
  VkImportMemoryHostPointerInfoEXT import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT};
  import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
  import.pHostPointer = pointer;
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.pNext = &import; allocation.allocationSize = bytes; allocation.memoryTypeIndex = type;
  VkDeviceMemory memory;
  Require(vkAllocateMemory(device, &allocation, nullptr, &memory) == VK_SUCCESS, "import host allocation");
  Require(vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS, "bind host pages");
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.queueFamilyIndex = family;
  VkCommandPool pool;
  Require(vkCreateCommandPool(device, &pool_info, nullptr, &pool) == VK_SUCCESS, "create transfer pool");
  VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_info.commandPool = pool; command_info.commandBufferCount = 1;
  VkCommandBuffer command;
  Require(vkAllocateCommandBuffers(device, &command_info, &command) == VK_SUCCESS, "allocate transfer command");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  Require(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS, "begin transfer");
  vkCmdFillBuffer(command, buffer, 0, 4096, 0x12345678);
  vkCmdFillBuffer(command, buffer, bytes - 4096, 4096, 0x87654321);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);
  Require(vkEndCommandBuffer(command) == VK_SUCCESS, "end transfer");
  VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
  Require(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS, "submit transfer");
  Require(vkQueueWaitIdle(queue) == VK_SUCCESS, "complete transfer");
  const auto* words = static_cast<const uint32_t*>(pointer);
  CHECK_EQ(words[0], 0x12345678); CHECK_EQ(words[1023], 0x12345678);
  CHECK_EQ(words[bytes / 4 - 1024], 0x87654321); CHECK_EQ(words[bytes / 4 - 1], 0x87654321);
  vkDestroyCommandPool(device, pool, nullptr);
  vkDestroyBuffer(device, buffer, nullptr);
  vkFreeMemory(device, memory, nullptr);
  munmap(reserved, bytes + alignment);
}
}
int main() {
  try {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
    const char* portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &portability;
    VkInstance instance;
    Require(vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS, "create instance");
    uint32_t count = 0; vkEnumeratePhysicalDevices(instance, &count, nullptr);
    Require(count > 0, "find GPU");
    std::vector<VkPhysicalDevice> devices(count); vkEnumeratePhysicalDevices(instance, &count, devices.data());
    auto physical = devices[0];
    uint32_t family_count = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
    uint32_t family = 0;
    while (family < family_count && !(families[family].queueFlags & VK_QUEUE_TRANSFER_BIT)) ++family;
    Require(family < family_count, "find transfer queue");
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    const char* subset = "VK_KHR_portability_subset";
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue;
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, extensions.data());
    bool host_import = false;
    for (const auto& extension : extensions)
      host_import |= std::strcmp(extension.extensionName, "VK_EXT_external_memory_host") == 0;
    const char* enabled[] = {subset, "VK_EXT_external_memory_host"};
    device_info.enabledExtensionCount = host_import ? 2 : 1;
    device_info.ppEnabledExtensionNames = enabled;
    VkDevice device;
    Require(vkCreateDevice(physical, &device_info, nullptr, &device) == VK_SUCCESS, "create device");
    BeginCase("native allocator releases loading-burst slack with small MoltenVK blocks");
    const auto baseline = Retained(instance, physical, device, 0);
    const auto mac = Retained(instance, physical, device,
        rex::gpu::vulkan::PreferredAllocatorBlockSize(true, false));
    const auto ios = Retained(instance, physical, device,
        rex::gpu::vulkan::PreferredAllocatorBlockSize(true, true));
    std::printf("retained_block_bytes baseline=%llu mac=%llu ios_policy_on_mac=%llu\n",
        (unsigned long long)baseline, (unsigned long long)mac, (unsigned long long)ios);
    CHECK_TRUE(mac < baseline); CHECK_TRUE(ios <= mac);
    CHECK_EQ(rex::gpu::vulkan::PreferredAllocatorBlockSize(false, false), 0);
    CHECK_EQ(rex::gpu::vulkan::PreferredAllocatorBlockSize(false, true), 0);
    if (host_import) HostBufferRoundTrip(physical, device, family);
    else std::printf("Host import extension unavailable; native import check skipped\n");
    vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
  } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
  return Finish();
}
