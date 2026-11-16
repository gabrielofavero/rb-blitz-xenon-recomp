// SPDX-License-Identifier: GPL-2.0-only
// Render synthetic triangles through the production SPIR-V arithmetic builder
// and the pinned MoltenVK stack. No guest assets or window are needed.
#include <vulkan/vulkan.h>
#include <SPIRV/GLSL.std.450.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>
#include <StandAlone/ResourceLimits.h>
#include <rex/graphics/pipeline/shader/spirv_builder.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t kSize = 256;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void Check(VkResult result) { Require(result == VK_SUCCESS, "Vulkan operation failed"); }

std::vector<uint32_t> VertexShader(bool workaround) {
  rex::graphics::SpirvBuilder b(spv::Spv_1_0, 0, nullptr);
  b.addCapability(spv::CapabilityShader);
  b.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
  auto glsl = b.import("GLSL.std.450");
  if (workaround) b.setNoContractionFma(glsl);
  auto scalar = b.makeFloatType(32), v2 = b.makeVectorType(scalar, 2);
  auto v4 = b.makeVectorType(scalar, 4), boolean = b.makeBoolType();
  auto index = b.createVariable(spv::NoPrecision, spv::StorageClassInput,
      b.makeIntType(32), "index");
  b.addDecoration(index, spv::DecorationBuiltIn, spv::BuiltInVertexIndex);
  auto position = b.createVariable(spv::NoPrecision, spv::StorageClassOutput, v4, "position");
  b.addDecoration(position, spv::DecorationBuiltIn, spv::BuiltInPosition);
  auto uv = b.createVariable(spv::NoPrecision, spv::StorageClassOutput, v2, "uv");
  b.addDecoration(uv, spv::DecorationLocation, 0);
  auto precision = b.createVariable(spv::NoPrecision, spv::StorageClassOutput, scalar, "precision");
  b.addDecoration(precision, spv::DecorationLocation, 1);
  b.addDecoration(precision, spv::DecorationFlat);
  auto constants_type = b.makeStructType({v4}, "Constants");
  b.addDecoration(constants_type, spv::DecorationBlock);
  b.addMemberDecoration(constants_type, 0, spv::DecorationOffset, 0);
  auto constants = b.createVariable(spv::NoPrecision, spv::StorageClassPushConstant,
      constants_type, "constants");
  std::vector<spv::Id> points;
  for (auto point : {std::array<float, 2>{-.8f,-.8f}, {-.8f,.8f}, {.8f,.8f},
                     {-.8f,-.8f}, {.8f,.8f}, {.8f,-.8f}}) {
    points.push_back(b.makeCompositeConstant(v2,
        {b.makeFloatConstant(point[0]), b.makeFloatConstant(point[1])}));
  }
  auto array = b.makeArrayType(v2, b.makeUintConstant(6), 0);
  auto positions = b.createVariable(spv::NoPrecision, spv::StorageClassPrivate,
      array, "positions", b.makeCompositeConstant(array, points));
  spv::Block* entry;
  auto function = b.makeFunctionEntry(spv::NoPrecision, b.makeVoidType(), "main", {}, {}, &entry);
  auto ep = b.addEntryPoint(spv::ExecutionModelVertex, function, "main");
  for (auto interface : {index, position, uv, precision}) ep->addIdOperand(interface);
  auto point = b.createLoad(b.createAccessChain(spv::StorageClassPrivate, positions,
      std::vector<spv::Id>{b.createLoad(index, spv::NoPrecision)}), spv::NoPrecision);
  b.createStore(b.createCompositeConstruct(v4, {b.createCompositeExtract(point, scalar, 0),
      b.createCompositeExtract(point, scalar, 1), b.makeFloatConstant(0), b.makeFloatConstant(1)}),
      position);
  auto half = b.makeFloatConstant(.5f);
  auto value = b.createNoContractionBinOp(spv::OpVectorTimesScalar, v2, point, half);
  value = b.createNoContractionBinOp(spv::OpFAdd, v2, value,
      b.makeCompositeConstant(v2, {half, half}));
  b.createStore(value, uv);
  auto inputs = b.createLoad(b.createAccessChain(spv::StorageClassPushConstant, constants,
      std::vector<spv::Id>{b.makeIntConstant(0)}), spv::NoPrecision);
  auto a = b.createCompositeExtract(inputs, scalar, 0);
  auto c = b.createCompositeExtract(inputs, scalar, 1);
  auto product = b.createNoContractionBinOp(spv::OpFMul, scalar, a, a);
  auto difference = b.createNoContractionBinOp(spv::OpFSub, scalar, product, c);
  auto bad = b.createBinOp(spv::OpFUnordNotEqual, boolean, difference, b.makeFloatConstant(0));
  // Runtime -0 multiplied by +1 must retain its sign in the replacement Fma.
  auto zero = b.createCompositeExtract(inputs, scalar, 2);
  auto one = b.createCompositeExtract(inputs, scalar, 3);
  auto zero_product = b.createNoContractionBinOp(spv::OpFMul, scalar, zero, one);
  auto uint_type = b.makeUintType(32);
  auto wrong_sign = b.createBinOp(spv::OpINotEqual, boolean,
      b.createUnaryOp(spv::OpBitcast, uint_type, zero_product), b.makeUintConstant(0x80000000));
  bad = b.createBinOp(spv::OpLogicalOr, boolean, bad, wrong_sign);
  b.createStore(b.createTriOp(spv::OpSelect, scalar, bad,
      b.makeFloatConstant(1), b.makeFloatConstant(0)), precision);
  b.makeReturn(false); b.leaveFunction();
  std::vector<uint32_t> result;
  b.dump(result);
  return result;
}

std::vector<uint32_t> FragmentShader(bool workaround) {
  rex::graphics::SpirvBuilder b(spv::Spv_1_0, 0, nullptr);
  b.addCapability(spv::CapabilityShader);
  b.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
  auto glsl = b.import("GLSL.std.450");
  if (workaround) b.setNoContractionFma(glsl);
  auto scalar = b.makeFloatType(32), v2 = b.makeVectorType(scalar, 2);
  auto v4 = b.makeVectorType(scalar, 4), boolean = b.makeBoolType();
  auto b2 = b.makeVectorType(boolean, 2);
  auto input = b.createVariable(spv::NoPrecision, spv::StorageClassInput, v2, "uv");
  auto output = b.createVariable(spv::NoPrecision, spv::StorageClassOutput, v4, "color");
  auto vertex_precision = b.createVariable(spv::NoPrecision, spv::StorageClassInput,
      scalar, "vertex_precision");
  b.addDecoration(vertex_precision, spv::DecorationLocation, 1);
  b.addDecoration(vertex_precision, spv::DecorationFlat);
  auto constants_type = b.makeStructType({v4}, "Constants");
  b.addDecoration(constants_type, spv::DecorationBlock);
  b.addMemberDecoration(constants_type, 0, spv::DecorationOffset, 0);
  auto constants = b.createVariable(spv::NoPrecision, spv::StorageClassPushConstant,
      constants_type, "constants");
  b.addDecoration(input, spv::DecorationLocation, 0);
  b.addDecoration(output, spv::DecorationLocation, 0);
  spv::Block* entry;
  auto function = b.makeFunctionEntry(spv::NoPrecision, b.makeVoidType(), "main", {}, {}, &entry);
  auto ep = b.addEntryPoint(spv::ExecutionModelFragment, function, "main");
  ep->addIdOperand(input); ep->addIdOperand(output); ep->addIdOperand(vertex_precision);
  b.addExecutionMode(function, spv::ExecutionModeOriginUpperLeft);
  auto splat = [&](float value, unsigned count) {
    return b.makeCompositeConstant(count == 2 ? v2 : v4,
        std::vector<spv::Id>(count, b.makeFloatConstant(value)));
  };
  auto value = b.createLoad(input, spv::NoPrecision);
  // These operations precede derivatives in real guest texture instructions.
  value = b.createNoContractionBinOp(spv::OpFAdd, v2, value, splat(.0002f, 2));
  value = b.createNoContractionBinOp(spv::OpFSub, v2, value, splat(.0001f, 2));
  value = b.createNoContractionBinOp(spv::OpFMul, v2, value, splat(1.f, 2));
  value = b.createNoContractionBinOp(spv::OpVectorTimesScalar, v2, value, b.makeFloatConstant(1.f));
  auto bad_gradient = [&](spv::Op derivative) {
    auto d = b.createUnaryOp(derivative, v2, value);
    auto magnitude = b.createUnaryBuiltinCall(v2, glsl, GLSLstd450FAbs, d);
    auto bad = b.createBinOp(spv::OpFOrdGreaterThan, b2, magnitude, splat(.005f, 2));
    return b.createUnaryOp(spv::OpAny, boolean, bad);
  };
  auto bad_x = bad_gradient(spv::OpDPdx), bad_y = bad_gradient(spv::OpDPdy);
  auto bad = b.createBinOp(spv::OpLogicalOr, boolean, bad_x, bad_y);
  auto bad_vertex = b.createBinOp(spv::OpFUnordNotEqual, boolean,
      b.createLoad(vertex_precision, spv::NoPrecision), b.makeFloatConstant(0));
  bad = b.createBinOp(spv::OpLogicalOr, boolean, bad, bad_vertex);
  // Exercise separate rounding with runtime values: (1+2^-23)^2 rounds to
  // 1+2^-22. Subtracting that must give zero, rather than the fused 2^-46.
  auto values = b.createLoad(b.createAccessChain(spv::StorageClassPushConstant, constants,
      std::vector<spv::Id>{b.makeIntConstant(0)}), spv::NoPrecision);
  auto a = b.createCompositeExtract(values, scalar, 0);
  auto c = b.createCompositeExtract(values, scalar, 1);
  auto product = b.createNoContractionBinOp(spv::OpFMul, scalar, a, a);
  auto difference = b.createNoContractionBinOp(spv::OpFSub, scalar, product, c);
  auto sum = b.createNoContractionBinOp(spv::OpFAdd, scalar, product,
      b.createUnaryOp(spv::OpFNegate, scalar, c));
  for (auto result : {difference, sum}) {
    auto wrong = b.createBinOp(spv::OpFUnordNotEqual, boolean, result, b.makeFloatConstant(0));
    bad = b.createBinOp(spv::OpLogicalOr, boolean, bad, wrong);
  }
  auto color = [&](float red, float green) {
    return b.makeCompositeConstant(v4, {b.makeFloatConstant(red), b.makeFloatConstant(green),
                                        b.makeFloatConstant(0), b.makeFloatConstant(1)});
  };
  b.createStore(b.createTriOp(spv::OpSelect, v4, bad, color(1, 0), color(0, 1)), output);
  b.makeReturn(false); b.leaveFunction();
  std::vector<uint32_t> result;
  b.dump(result);
  return result;
}

struct Gpu {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  VkCommandPool commands{};
  VkBuffer buffer{};
  VkDeviceMemory buffer_memory{};
  uint8_t* pixels{};
  VkPipelineLayout layout{};
  VkPhysicalDeviceMemoryProperties memory_types{};
  VkPhysicalDeviceProperties properties{};

  ~Gpu() {
    if (device) {
      vkDeviceWaitIdle(device);
      if (pixels) vkUnmapMemory(device, buffer_memory);
      if (buffer) vkDestroyBuffer(device, buffer, nullptr);
      if (buffer_memory) vkFreeMemory(device, buffer_memory, nullptr);
      if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
      if (commands) vkDestroyCommandPool(device, commands, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
  }

  uint32_t MemoryType(uint32_t mask, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memory_types.memoryTypeCount; ++i)
      if ((mask & (1u << i)) && (memory_types.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("no compatible Vulkan memory type");
  }

  bool Open() {
    const char* extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &extension;
    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) return false;
    uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) return false;
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    physical = devices.front();
    vkGetPhysicalDeviceProperties(physical, &properties);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_types);
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
    std::vector<VkQueueFamilyProperties> queues(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues.data());
    uint32_t family = 0;
    while (family < families && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == families) return false;
    float priority = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
    const char* portability = "VK_KHR_portability_subset";
    VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    d.queueCreateInfoCount = 1; d.pQueueCreateInfos = &q;
    d.enabledExtensionCount = 1; d.ppEnabledExtensionNames = &portability;
    Check(vkCreateDevice(physical, &d, nullptr, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = family;
    Check(vkCreateCommandPool(device, &pool, nullptr, &commands));
    VkBufferCreateInfo buf{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buf.size = kSize * kSize * 4; buf.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateBuffer(device, &buf, nullptr, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(device, &allocation, nullptr, &buffer_memory));
    Check(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    Check(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&pixels)));
    VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    l.pushConstantRangeCount = 1; l.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(device, &l, nullptr, &layout));
    std::printf("Headless graphics device: %s\n", properties.deviceName);
    return true;
  }

  struct Image {
    VkDevice device;
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    ~Image() {
      if (view) vkDestroyImageView(device, view, nullptr);
      if (image) vkDestroyImage(device, image, nullptr);
      if (memory) vkFreeMemory(device, memory, nullptr);
    }
  };
  void MakeImage(Image& target, VkSampleCountFlagBits samples) {
    VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    i.imageType = VK_IMAGE_TYPE_2D; i.format = VK_FORMAT_R8G8B8A8_UNORM;
    i.extent = {kSize, kSize, 1}; i.mipLevels = 1; i.arrayLayers = 1; i.samples = samples;
    i.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Check(vkCreateImage(device, &i, nullptr, &target.image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, target.image, &requirements);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = requirements.size;
    a.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device, &a, nullptr, &target.memory));
    Check(vkBindImageMemory(device, target.image, target.memory, 0));
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = target.image; v.viewType = VK_IMAGE_VIEW_TYPE_2D; v.format = i.format;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(vkCreateImageView(device, &v, nullptr, &target.view));
  }

  void Render(bool workaround, VkSampleCountFlagBits samples) {
    Image output{device}, multisample{device};
    MakeImage(output, VK_SAMPLE_COUNT_1_BIT);
    bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;
    if (msaa) MakeImage(multisample, samples);
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = samples; attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    attachments[1] = attachments[0]; attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolve{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
    subpass.pResolveAttachments = msaa ? &resolve : nullptr;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0; dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = msaa ? 2 : 1; rp.pAttachments = attachments.data();
    rp.subpassCount = 1; rp.pSubpasses = &subpass;
    rp.dependencyCount = 1; rp.pDependencies = &dependency;
    VkRenderPass pass;
    Check(vkCreateRenderPass(device, &rp, nullptr, &pass));
    VkImageView views[] = {msaa ? multisample.view : output.view, output.view};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = pass; fb.attachmentCount = rp.attachmentCount; fb.pAttachments = views;
    fb.width = kSize; fb.height = kSize; fb.layers = 1;
    VkFramebuffer framebuffer;
    Check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer));
    VkShaderModule modules[2];
    auto vs = VertexShader(workaround), fs = FragmentShader(workaround);
    for (unsigned i = 0; i < 2; ++i) {
      const auto& code = i ? fs : vs;
      VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      m.codeSize = code.size() * 4; m.pCode = code.data();
      Check(vkCreateShaderModule(device, &m, nullptr, &modules[i]));
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
      stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
      stages[i].module = modules[i]; stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, float(kSize), float(kSize), 0, 1};
    VkRect2D scissor{{0, 0}, {kSize, kSize}};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.pViewports = &viewport; vp.scissorCount = 1; vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.lineWidth = 1;
    VkSampleMask mask = samples == VK_SAMPLE_COUNT_4_BIT ? 9 : ~0u;
    VkPipelineMultisampleStateCreateInfo multi{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multi.rasterizationSamples = samples; multi.pSampleMask = &mask;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1; blending.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    p.stageCount = 2; p.pStages = stages; p.pVertexInputState = &vertex;
    p.pInputAssemblyState = &assembly; p.pViewportState = &vp; p.pRasterizationState = &raster;
    p.pMultisampleState = &multi; p.pColorBlendState = &blending;
    p.layout = layout; p.renderPass = pass;
    VkPipeline pipeline;
    Check(vkCreateGraphicsPipelines(device, {}, 1, &p, nullptr, &pipeline));
    VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    a.commandPool = commands; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount = 1;
    VkCommandBuffer cmd;
    Check(vkAllocateCommandBuffers(device, &a, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(vkBeginCommandBuffer(cmd, &begin));
    VkClearValue clear{}; clear.color.float32[2] = 1; clear.color.float32[3] = 1;
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    render.renderPass = pass; render.framebuffer = framebuffer; render.renderArea = scissor;
    render.clearValueCount = 1; render.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &render, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const float precision_inputs[] = {0x1.000002p0f, 0x1.000004p0f, -0.0f, 1.0f};
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(precision_inputs), precision_inputs);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {kSize, kSize, 1};
    vkCmdCopyImageToBuffer(cmd, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    Check(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    // Match the presenter's render -> semaphore -> tracking-fence handoff.
    // Run with synchronous and asynchronous MoltenVK submission to ensure
    // queued Metal encoding finishes before the host reads or recycles data.
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore rendered;
    Check(vkCreateSemaphore(device, &semaphore_info, nullptr, &rendered));
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence completed;
    Check(vkCreateFence(device, &fence_info, nullptr, &completed));
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &rendered;
    Check(vkQueueSubmit(queue, 1, &submit, {}));
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo track{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    track.waitSemaphoreCount = 1; track.pWaitSemaphores = &rendered;
    track.pWaitDstStageMask = &wait_stage;
    Check(vkQueueSubmit(queue, 1, &track, completed));
    Check(vkWaitForFences(device, 1, &completed, VK_TRUE, 10'000'000'000ull));
    vkDestroyFence(device, completed, nullptr);
    vkDestroySemaphore(device, rendered, nullptr);
    unsigned bad = 0, covered = 0;
    for (unsigned i = 0; i < kSize * kSize; ++i) {
      if (pixels[i * 4]) ++bad;
      if (pixels[i * 4 + 1]) ++covered;
    }
    std::printf("%ux samples, workaround=%d: %u bad gradient pixels, %u green pixels\n",
        unsigned(samples), workaround, bad, covered);
    vkFreeCommandBuffers(device, commands, 1, &cmd);
    vkDestroyPipeline(device, pipeline, nullptr);
    for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr); vkDestroyRenderPass(device, pass, nullptr);
    Require(covered > 30000, "fixture failed to render the triangles");
    Require(bad == 0, "helper-lane texture gradient corruption");
  }
};
}  // namespace

int main(int argc, char** argv) {
  try {
    glslang::InitializeProcess();
    Gpu gpu;
    if (!gpu.Open()) { std::puts("SKIP: no usable Vulkan graphics device"); return 77; }
    bool workaround = !(argc == 2 && std::strcmp(argv[1], "--baseline") == 0);
    for (auto samples : {VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT})
      if (gpu.properties.limits.framebufferColorSampleCounts & samples) gpu.Render(workaround, samples);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what()); return 1;
  }
}
