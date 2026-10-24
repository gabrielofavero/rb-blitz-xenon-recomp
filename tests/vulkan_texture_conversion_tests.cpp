// SPDX-License-Identifier: GPL-2.0-only
// Headless GPU validation of the production GLSL; no game or window is opened.
#define VK_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>
#include <StandAlone/ResourceLimits.h>
#include <rex/graphics/vulkan/texture_conversion_shader.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

#define VK_FUNCTIONS(X) \
  X(vkCreateInstance) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
  X(vkGetPhysicalDeviceProperties) X(vkCreateDevice) X(vkDestroyDevice) X(vkGetDeviceQueue) \
  X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
  X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
  X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) \
  X(vkDestroyShaderModule) X(vkCreateComputePipelines) X(vkDestroyPipeline) \
  X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) \
  X(vkUpdateDescriptorSets) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
  X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
  X(vkResetCommandBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
  X(vkCmdPushConstants) X(vkCmdDispatch) X(vkCmdPipelineBarrier) X(vkCmdFillBuffer) \
  X(vkQueueSubmit) X(vkQueueWaitIdle)
#define DECLARE(name) PFN_##name name = nullptr;
VK_FUNCTIONS(DECLARE)
#undef DECLARE

void Require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void Check(VkResult result) { Require(result == VK_SUCCESS, "Vulkan operation failed"); }

uint32_t Swap(uint32_t value, uint32_t endian) {
  // Independent byte-order oracle for the four guest encodings.
  auto b = std::bit_cast<std::array<uint8_t, 4>>(value);
  if (endian == 1 || endian == 2) { std::swap(b[0], b[1]); std::swap(b[2], b[3]); }
  if (endian == 2 || endian == 3) { std::swap(b[0], b[2]); std::swap(b[1], b[3]); }
  return std::bit_cast<uint32_t>(b);
}

struct Gpu {
  HMODULE loader = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkBuffer buffers[2] = {};
  VkDeviceMemory memory[2] = {};
  uint32_t* mapped[2] = {};
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkShaderModule shader = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;

  ~Gpu() {
    if (device) {
      if (queue) vkQueueWaitIdle(queue);
      if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
      if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
      if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
      if (shader) vkDestroyShaderModule(device, shader, nullptr);
      if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
      if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
      for (unsigned i = 0; i < 2; ++i) {
        if (mapped[i]) vkUnmapMemory(device, memory[i]);
        if (buffers[i]) vkDestroyBuffer(device, buffers[i], nullptr);
        if (memory[i]) vkFreeMemory(device, memory[i], nullptr);
      }
      vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
    if (loader) FreeLibrary(loader);
  }

  bool Open() {
    loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) return false;
    auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
    Require(get != nullptr, "missing Vulkan entry point");
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS) return false;
#define LOAD(name) name = reinterpret_cast<PFN_##name>(get(instance, #name)); Require(name != nullptr, #name);
    VK_FUNCTIONS(LOAD)
#undef LOAD
    uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) return false;
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    for (VkPhysicalDevice physical : devices) {
      uint32_t family_count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
      std::vector<VkQueueFamilyProperties> families(family_count);
      vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
      for (uint32_t family = 0; family < family_count; ++family) {
        if (!(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
        float priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        d.queueCreateInfoCount = 1; d.pQueueCreateInfos = &q;
        if (vkCreateDevice(physical, &d, nullptr, &device) != VK_SUCCESS) continue;
        vkGetDeviceQueue(device, family, 0, &queue);
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::printf("Headless compute device: %s\n", properties.deviceName);
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        p.queueFamilyIndex = family; p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(vkCreateCommandPool(device, &p, nullptr, &command_pool));
        VkPhysicalDeviceMemoryProperties types;
        vkGetPhysicalDeviceMemoryProperties(physical, &types);
        for (uint32_t i = 0; i < 2; ++i) {
          VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
          b.size = kBytes; b.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
          Check(vkCreateBuffer(device, &b, nullptr, &buffers[i]));
          VkMemoryRequirements requirements;
          vkGetBufferMemoryRequirements(device, buffers[i], &requirements);
          uint32_t type = 0;
          for (; type < types.memoryTypeCount; ++type) {
            constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((requirements.memoryTypeBits & (1u << type)) &&
                (types.memoryTypes[type].propertyFlags & flags) == flags) break;
          }
          Require(type < types.memoryTypeCount, "no coherent host memory for headless test");
          VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
          a.allocationSize = requirements.size; a.memoryTypeIndex = type;
          Check(vkAllocateMemory(device, &a, nullptr, &memory[i]));
          Check(vkBindBufferMemory(device, buffers[i], memory[i], 0));
          Check(vkMapMemory(device, memory[i], 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&mapped[i])));
        }
        return true;
      }
    }
    return false;
  }

  static constexpr uint32_t kWords = 131076;  // larger than the measured highway textures + guard
  static constexpr VkDeviceSize kBytes = kWords * sizeof(uint32_t);
};

int main() {
  try {
    // Compile the exact source used by the runtime before checking GPU availability.
    glslang::InitializeProcess();
    glslang::TShader shader(EShLangCompute);
    const char* source = rex::graphics::vulkan::kTextureConversionShader;
    shader.setStrings(&source, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(&glslang::DefaultTBuiltInResource, 450, false, messages)) {
      std::fprintf(stderr, "%s\n", shader.getInfoLog());
      throw std::runtime_error("production shader failed to compile");
    }
    glslang::TProgram program;
    program.addShader(&shader);
    Require(program.link(messages), "shader link failed");
    std::vector<uint32_t> spirv;
    glslang::SpvOptions options{};
    options.disableOptimizer = true;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv, &options);
    Gpu gpu;
    if (!gpu.Open()) { std::puts("SKIP: no usable Vulkan device"); return 77; }
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 1; set.pBindings = &binding;
    Check(vkCreateDescriptorSetLayout(gpu.device, &set, nullptr, &gpu.set_layout));
    VkDescriptorSetLayout layouts[] = {gpu.set_layout, gpu.set_layout};
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 20};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 2; layout.pSetLayouts = layouts;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(gpu.device, &layout, nullptr, &gpu.layout));
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = spirv.size() * 4; module.pCode = spirv.data();
    Check(vkCreateShaderModule(gpu.device, &module, nullptr, &gpu.shader));
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = gpu.shader; pipeline.stage.pName = "main"; pipeline.layout = gpu.layout;
    Check(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &gpu.pipeline));
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2; pool.poolSizeCount = 1; pool.pPoolSizes = &pool_size;
    Check(vkCreateDescriptorPool(gpu.device, &pool, nullptr, &gpu.pool));
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = gpu.pool; allocate.descriptorSetCount = 2; allocate.pSetLayouts = layouts;
    VkDescriptorSet sets[2];
    Check(vkAllocateDescriptorSets(gpu.device, &allocate, sets));
    VkDescriptorBufferInfo buffers[] = {{gpu.buffers[0], 0, Gpu::kBytes}, {gpu.buffers[1], 0, Gpu::kBytes}};
    VkWriteDescriptorSet writes[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[i].dstSet = sets[i]; writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &buffers[i];
    }
    vkUpdateDescriptorSets(gpu.device, 2, writes, 0, nullptr);
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool = gpu.command_pool; commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commands.commandBufferCount = 1;
    VkCommandBuffer cmd;
    Check(vkAllocateCommandBuffers(gpu.device, &commands, &cmd));
    uint64_t checks = 0;
    constexpr uint32_t edge[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678, 0x80808080};
    for (uint32_t endian = 0; endian < 4; ++endian) {
      for (uint32_t sign = 0; sign < 2; ++sign) {
        for (uint32_t integer = 0; integer < 2; ++integer) {
          for (uint32_t count : {1u, 4u, 63u, 64u, 65u, 131072u}) {
            std::vector<uint32_t> expected(count);
            uint32_t random = 0x12345678;
            for (uint32_t i = 0; i < Gpu::kWords; ++i) {
              random = random * 1664525u + 1013904223u;
              uint32_t value = i < std::size(edge) ? edge[i] : random;
              gpu.mapped[1][i] = Swap(value, endian);
              if (i < count) {
                float result = sign ? float(std::bit_cast<int32_t>(value)) : float(value);
                if (!integer) result *= sign ? (1.0f / 2147483647.0f) : (1.0f / 4294967295.0f);
                expected[i] = Swap(std::bit_cast<uint32_t>(result), endian);
              }
            }
            Check(vkResetCommandBuffer(cmd, 0));
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            Check(vkBeginCommandBuffer(cmd, &begin));
            // A GPU write, deliberately absent from the CPU reference, proves the
            // conversion reads GPU contents rather than a stale CPU mirror.
            vkCmdFillBuffer(cmd, gpu.buffers[1], 0, 4, Swap(0x40000000, endian));
            float first = integer ? 1073741824.0f : (sign ? 0.5f : 0.25f);
            expected[0] = Swap(std::bit_cast<uint32_t>(first), endian);
            vkCmdFillBuffer(cmd, gpu.buffers[0], 0, Gpu::kBytes, 0xdeadbeef);
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            before.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
            before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.layout, 0, 2, sets, 0, nullptr);
            uint32_t groups = (count + 63) / 64;
            // Force multi-row dispatch even on small fixtures.
            uint32_t row = std::min(groups, 7u);
            uint32_t constants[] = {count, endian, sign, integer, row};
            vkCmdPushConstants(cmd, gpu.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);
            vkCmdDispatch(cmd, row, (groups + row - 1) / row, 1);
            VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &after, 0, nullptr, 0, nullptr);
            Check(vkEndCommandBuffer(cmd));
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
            Check(vkQueueSubmit(gpu.queue, 1, &submit, VK_NULL_HANDLE));
            Check(vkQueueWaitIdle(gpu.queue));
            for (uint32_t i = 0; i < count; ++i) {
              if (gpu.mapped[0][i] != expected[i]) {
                std::fprintf(stderr, "Mismatch endian=%u sign=%u integer=%u count=%u i=%u: %08x != %08x\n",
                    endian, sign, integer, count, i, gpu.mapped[0][i], expected[i]);
                throw std::runtime_error("GPU conversion mismatch");
              }
              ++checks;
            }
            Require(gpu.mapped[0][count] == 0xdeadbeef, "tail invocation wrote past the range");
          }
        }
      }
    }
    std::printf("PASS: %llu GPU words, all endian/signed/integer modes, tails, multi-row and GPU writes\n",
                static_cast<unsigned long long>(checks));
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
