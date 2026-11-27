// SPDX-License-Identifier: GPL-2.0-only
// Exercise compact resolve mappings with native GPU copies and fence retirement.
// No title assets, surface, window or renderer initialization are involved.
#include "check.h"
#include "gpu/xenos/host/vulkan/scaled_resolve_segments.h"
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using rex::gpu::vulkan::ScaledResolveSegments;
using namespace rb_blitz::test;
constexpr std::uint64_t kMiB = std::uint64_t(1) << 20;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void Check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "%s returned VkResult %d\n", operation, int(result));
    throw std::runtime_error(operation);
  }
}

struct Buffer {
  VkDevice device{};
  VkBuffer buffer{};
  VkDeviceMemory memory{};
  VkDeviceSize bytes{};
  VkDeviceSize allocation_bytes{};
  void* mapped{};
  ~Buffer() {
    if (mapped) {
      // A timeout or submit error must not free readback memory still in use.
      vkDeviceWaitIdle(device);
      vkUnmapMemory(device, memory);
    }
    if (buffer) vkDestroyBuffer(device, buffer, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
  }
};

struct Gpu {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  VkCommandPool pool{};
  VkCommandBuffer commands{};
  VkFence fence{};
  VkPhysicalDeviceMemoryProperties memory_types{};
  VkPhysicalDeviceProperties properties{};
  std::map<std::uint64_t, std::unique_ptr<Buffer>> buffers;
  std::vector<std::uint64_t> retired;
  std::uint64_t next_handle = 1;
  std::uint64_t allocated = 0;
  std::uint64_t peak_allocated = 0;
  unsigned migrations = 0;
  unsigned destroyed = 0;

  ~Gpu() {
    if (device) {
      vkDeviceWaitIdle(device);
      buffers.clear();
      if (fence) vkDestroyFence(device, fence, nullptr);
      if (pool) vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
  }

  bool Open() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
#if defined(__APPLE__)
    const char* portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &portability;
#endif
    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) return false;
    std::uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate device count");
    if (!count) return false;
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
    std::uint32_t family = 0;
    bool found = false;
    for (auto candidate : devices) {
      std::uint32_t family_count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
      std::vector<VkQueueFamilyProperties> families(family_count);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
      for (family = 0; family < family_count; ++family) {
        if (families[family].queueCount && (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
          physical = candidate;
          found = true;
          break;
        }
      }
      if (found) break;
    }
    if (!found) return false;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_types);
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::uint32_t extension_count = 0;
    Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr),
          "enumerate extension count");
    std::vector<VkExtensionProperties> extensions(extension_count);
    Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count,
                                              extensions.data()), "enumerate extensions");
    const char* subset = "VK_KHR_portability_subset";
    const bool use_subset = std::any_of(extensions.begin(), extensions.end(), [&](const auto& item) {
      return std::strcmp(item.extensionName, subset) == 0;
    });
    float priority = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    d.queueCreateInfoCount = 1;
    d.pQueueCreateInfos = &q;
    d.enabledExtensionCount = use_subset ? 1 : 0;
    d.ppEnabledExtensionNames = use_subset ? &subset : nullptr;
    Check(vkCreateDevice(physical, &d, nullptr, &device), "create device");
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    p.queueFamilyIndex = family;
    Check(vkCreateCommandPool(device, &p, nullptr, &pool), "create command pool");
    VkCommandBufferAllocateInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    c.commandPool = pool;
    c.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    c.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(device, &c, &commands), "allocate command buffer");
    VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(vkCreateFence(device, &f, nullptr, &fence), "create submission fence");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(commands, &begin), "begin command buffer");
    std::printf("Native segmented resolve device: %s\n", properties.deviceName);
    return true;
  }

  std::unique_ptr<Buffer> Allocate(VkDeviceSize bytes, bool host) {
    auto result = std::make_unique<Buffer>();
    result->device = device;
    result->bytes = bytes;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateBuffer(device, &info, nullptr, &result->buffer), "create compact buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, result->buffer, &requirements);
    result->allocation_bytes = requirements.size;
    const VkMemoryPropertyFlags flags = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT :
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    std::uint32_t type = 0;
    while (type < memory_types.memoryTypeCount &&
           (!(requirements.memoryTypeBits & (1u << type)) ||
            (memory_types.memoryTypes[type].propertyFlags & flags) != flags)) ++type;
    Require(type < memory_types.memoryTypeCount, "find compatible buffer memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    Check(vkAllocateMemory(device, &allocation, nullptr, &result->memory), "allocate compact memory");
    Check(vkBindBufferMemory(device, result->buffer, result->memory, 0), "bind compact memory");
    if (host)
      Check(vkMapMemory(device, result->memory, 0, VK_WHOLE_SIZE, 0, &result->mapped),
            "map readback memory");
    return result;
  }

  void TransferBarrier() {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
  }

  std::uint64_t AllocateSegment(std::uint64_t bytes) {
    Require(bytes <= 16 * kMiB - allocated, "segmented buffers stay below 16 MiB");
    const auto handle = next_handle++;
    auto buffer = Allocate(bytes, false);
    const auto allocation_bytes = buffer->allocation_bytes;
    Require(allocation_bytes <= 16 * kMiB - allocated, "native allocations stay below 16 MiB");
    vkCmdFillBuffer(commands, buffer->buffer, 0, VK_WHOLE_SIZE, 0);
    buffers.emplace(handle, std::move(buffer));
    allocated += allocation_bytes;
    peak_allocated = std::max(peak_allocated, allocated);
    return handle;
  }

  void Copy(std::uint64_t source, std::uint64_t destination, std::uint64_t source_offset,
            std::uint64_t destination_offset, std::uint64_t bytes) {
    TransferBarrier();
    VkBufferCopy region{source_offset, destination_offset, bytes};
    vkCmdCopyBuffer(commands, buffers.at(source)->buffer, buffers.at(destination)->buffer, 1, &region);
    ++migrations;
  }

  void Fill(const ScaledResolveSegments::Binding& binding, std::uint32_t pattern) {
    TransferBarrier();
    vkCmdFillBuffer(commands, buffers.at(binding.handle)->buffer, binding.offset, binding.length,
                    pattern);
  }

  void Read(const ScaledResolveSegments::Binding& binding, Buffer& readback,
            std::uint64_t destination_offset) {
    TransferBarrier();
    VkBufferCopy region{binding.offset, destination_offset, binding.length};
    vkCmdCopyBuffer(commands, buffers.at(binding.handle)->buffer, readback.buffer, 1, &region);
  }

  void Retire(std::uint64_t handle) { retired.push_back(handle); }

  void RetireCompleted(std::uint64_t completed_submission) {
    if (!completed_submission) return;
    Require(vkGetFenceStatus(device, fence) == VK_SUCCESS, "retire only after submission fence");
    for (auto handle : retired) {
      allocated -= buffers.at(handle)->allocation_bytes;
      buffers.erase(handle);
      ++destroyed;
    }
    retired.clear();
  }

  void SubmitAndWait(Buffer& readback) {
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &host, 0, nullptr, 0, nullptr);
    Check(vkEndCommandBuffer(commands), "end command buffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    Check(vkQueueSubmit(queue, 1, &submit, fence), "submit copy and migration");
    RetireCompleted(0);
    CHECK_EQ(destroyed, 0);
    Check(vkWaitForFences(device, 1, &fence, VK_TRUE, 30'000'000'000ull), "wait submission fence");
    VkMappedMemoryRange invalidate{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    invalidate.memory = readback.memory;
    invalidate.size = VK_WHOLE_SIZE;
    Check(vkInvalidateMappedMemoryRanges(device, 1, &invalidate), "invalidate GPU readback");
  }
};

void Run(Gpu& gpu) {
  BeginCase("native GPU preserves high virtual aliases through compact segment migration");
  const std::uint64_t high = 5 * 1024 * kMiB + 3 * ScaledResolveSegments::kAlignment;
  const std::uint64_t low = 96 * kMiB + ScaledResolveSegments::kAlignment;
  ScaledResolveSegments segments(8 * 1024 * kMiB);
  auto allocate = [&](std::uint64_t bytes) { return gpu.AllocateSegment(bytes); };
  auto copy = [&](auto source, auto destination, auto source_offset, auto destination_offset,
                  auto bytes) { gpu.Copy(source, destination, source_offset, destination_offset, bytes); };
  auto retire = [&](auto handle) { gpu.Retire(handle); };
  auto acquire = [&](std::uint64_t start, std::uint64_t length) {
    ScaledResolveSegments::Binding binding;
    Require(segments.Acquire(start, length, allocate, copy, retire, binding), "acquire compact segment");
    return binding;
  };

  const auto first = acquire(high, kMiB);
  const auto second = acquire(high + 2 * kMiB, kMiB);
  const auto distant = acquire(low, ScaledResolveSegments::kAlignment);
  CHECK_EQ(segments.spans().size(), 3);
  gpu.Fill(first, 0x11223344);
  gpu.Fill(second, 0x55667788);
  gpu.Fill(distant, 0x90ABCDEF);
  ScaledResolveSegments::Binding old_alias;
  Require(segments.Find(high + 2 * ScaledResolveSegments::kAlignment,
                         ScaledResolveSegments::kAlignment, old_alias), "find old alias");
  CHECK_EQ(old_alias.handle, first.handle);
  CHECK_EQ(old_alias.offset, 2 * ScaledResolveSegments::kAlignment);

  const auto bridge = acquire(high + kMiB / 2, 2 * kMiB);
  CHECK_EQ(segments.spans().size(), 2);
  CHECK_EQ(bridge.offset, kMiB / 2);
  CHECK_TRUE(bridge.handle != first.handle && bridge.handle != second.handle);
  CHECK_EQ(gpu.migrations, 2);
  CHECK_EQ(gpu.retired.size(), 2);
  CHECK_EQ(gpu.destroyed, 0);
  CHECK_TRUE(gpu.buffers.contains(old_alias.handle));
  const auto gap = acquire(high + kMiB, kMiB);
  CHECK_EQ(gap.handle, bridge.handle);
  gpu.Fill(gap, 0xA1B2C3D4);
  const auto overwrite = acquire(high + 128 * 1024, 4096);
  gpu.Fill(overwrite, 0xCAFEBABE);
  ScaledResolveSegments::Binding current_alias;
  Require(segments.Find(high + 2 * ScaledResolveSegments::kAlignment,
                         ScaledResolveSegments::kAlignment, current_alias), "find migrated alias");
  CHECK_EQ(current_alias.handle, bridge.handle);
  CHECK_EQ(current_alias.offset, old_alias.offset);

  BeginCase("native allocation refusal preserves existing GPU mappings and data");
  ScaledResolveSegments::Binding refused{123, 456, 789};
  auto refuse = [](std::uint64_t) -> std::uint64_t { return 0; };
  CHECK_FALSE(segments.Acquire(high + 3 * kMiB - ScaledResolveSegments::kAlignment,
                                2 * ScaledResolveSegments::kAlignment, refuse, copy, retire, refused));
  CHECK_EQ(refused.handle, 123);
  CHECK_EQ(refused.offset, 456);
  CHECK_EQ(refused.length, 789);
  CHECK_EQ(segments.spans().size(), 2);
  CHECK_EQ(gpu.migrations, 2);
  CHECK_EQ(gpu.retired.size(), 2);

  const auto merged = acquire(high, 3 * kMiB);
  const std::uint64_t readback_bytes = 3 * kMiB + 2 * ScaledResolveSegments::kAlignment;
  auto readback = gpu.Allocate(readback_bytes, true);
  CHECK_TRUE(gpu.peak_allocated + readback->allocation_bytes <= 16 * kMiB);
  gpu.Read(merged, *readback, 0);
  // This binding represents a descriptor recorded before migration. Its old
  // backing must remain readable until the submission using it completes.
  gpu.Read(old_alias, *readback, 3 * kMiB);
  gpu.Read(distant, *readback, 3 * kMiB + ScaledResolveSegments::kAlignment);
  segments.Clear(retire);
  CHECK_TRUE(segments.spans().empty());
  CHECK_EQ(gpu.retired.size(), 4);
  CHECK_EQ(vkGetFenceStatus(gpu.device, gpu.fence), VK_NOT_READY);
  gpu.RetireCompleted(0);
  CHECK_EQ(gpu.destroyed, 0);
  gpu.SubmitAndWait(*readback);

  BeginCase("native readback matches an independent logical-address oracle after the fence");
  const auto* actual = static_cast<const std::uint32_t*>(readback->mapped);
  std::vector<std::uint32_t> expected(readback_bytes / 4);
  std::fill_n(expected.begin(), kMiB / 4, 0x11223344);
  std::fill_n(expected.begin() + kMiB / 4, kMiB / 4, 0xA1B2C3D4);
  std::fill_n(expected.begin() + 2 * kMiB / 4, kMiB / 4, 0x55667788);
  std::fill_n(expected.begin() + 128 * 1024 / 4, 4096 / 4, 0xCAFEBABE);
  std::fill_n(expected.begin() + 3 * kMiB / 4, ScaledResolveSegments::kAlignment / 4, 0x11223344);
  std::fill_n(expected.begin() + (3 * kMiB + ScaledResolveSegments::kAlignment) / 4,
              ScaledResolveSegments::kAlignment / 4, 0x90ABCDEF);
  const auto mismatch = std::mismatch(expected.begin(), expected.end(), actual);
  if (mismatch.first != expected.end())
    std::fprintf(stderr, "first readback mismatch at word %zu: expected %08X actual %08X\n",
                  std::size_t(mismatch.first - expected.begin()), *mismatch.first, *mismatch.second);
  CHECK_TRUE(mismatch.first == expected.end());
  gpu.RetireCompleted(1);
  CHECK_EQ(gpu.destroyed, 4);
  CHECK_TRUE(gpu.retired.empty());
  CHECK_TRUE(gpu.buffers.empty());
  CHECK_EQ(gpu.allocated, 0);
  std::printf("logical_size=8192 MiB high_address=%llu compact_peak=%llu readback=%llu migrations=%u\n",
      static_cast<unsigned long long>(high), static_cast<unsigned long long>(gpu.peak_allocated),
      static_cast<unsigned long long>(readback_bytes), gpu.migrations);
}
}  // namespace

int main() {
  try {
    Gpu gpu;
    if (!gpu.Open()) {
      std::puts("SKIP: no native Vulkan compute device available");
      return 77;
    }
    Run(gpu);
    return rb_blitz::test::Finish();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Native segmented resolve test failed: %s\n", error.what());
    return 1;
  }
}
