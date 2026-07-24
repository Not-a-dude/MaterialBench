#include "gemm_executor.h"

#include "../utils.h"
#include "gemm_shader_tiled.comp.spv.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace materialbench::vulkan {
namespace {

// These values must match the register-tiled compute shader. A workgroup has
// 8x8 invocations and produces one 32x32 output tile.
constexpr uint32_t kLocalSizeX = 8;
constexpr uint32_t kLocalSizeY = 8;
constexpr uint32_t kOutputTileX = 32;
constexpr uint32_t kOutputTileY = 32;

// Small dispatch parameters are passed without a separate uniform buffer.
// The fields and their order must match the shader's push-constant block.
struct PushConstants {
    uint32_t rows;
    uint32_t columns;
    uint32_t depth;
    uint32_t baseWorkgroupX;
    uint32_t baseWorkgroupY;
};

bool vkSucceeded(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return true;
    LOGE("%s failed with VkResult %d", operation, static_cast<int>(result));
    return false;
}

// Calculates the size of a tightly packed row-major float matrix while checking
// for integer overflow.
bool matrixByteSize(uint32_t rows, uint32_t columns, VkDeviceSize& result) {
    constexpr uint64_t max = std::numeric_limits<VkDeviceSize>::max();
    const uint64_t elementCount = static_cast<uint64_t>(rows) * columns;
    if (rows == 0 || columns == 0 || elementCount > max / sizeof(float)) return false;
    result = static_cast<VkDeviceSize>(elementCount * sizeof(float));
    return true;
}

}  // namespace

std::unique_ptr<GemmExecutor> GemmExecutor::create(VulkanContext& context,
                                                   const GemmConfig& config) {
    auto executor = std::unique_ptr<GemmExecutor>(new GemmExecutor(context, config));
    if (!executor->initialize()) return nullptr;
    return executor;
}

GemmExecutor::GemmExecutor(VulkanContext& context, GemmConfig config)
    : context_(context), config_(config) {}

GemmExecutor::~GemmExecutor() {
    release();
}

bool GemmExecutor::initialize() {
    if (config_.rows == 0 || config_.columns == 0 || config_.depth == 0) {
        LOGE("Invalid GEMM configuration");
        return false;
    }

    // One 8x8 workgroup emits a 32x32 output tile. Partial edge tiles are
    // bounds-checked by the shader.
    workgroupCountX_ = (config_.columns + kOutputTileX - 1) / kOutputTileX;
    workgroupCountY_ = (config_.rows + kOutputTileY - 1) / kOutputTileY;

    const auto& limits = context_.properties().limits;
    if (kLocalSizeX * kLocalSizeY > limits.maxComputeWorkGroupInvocations ||
        kLocalSizeX > limits.maxComputeWorkGroupSize[0] ||
        kLocalSizeY > limits.maxComputeWorkGroupSize[1] ||
        workgroupCountX_ > limits.maxComputeWorkGroupCount[0] ||
        workgroupCountY_ > limits.maxComputeWorkGroupCount[1]) {
        LOGE("Vulkan compute limits do not support the GEMM dispatch configuration");
        return false;
    }

    return createPipeline() && createBuffers() && createDescriptors() &&
           createSubmissionObjects();
}

bool GemmExecutor::createPipeline() {
    // Vulkan reads SPIR-V as an aligned array of 32-bit words, so the embedded
    // byte array is copied into a std::vector<uint32_t>.
    if (gemm_shader_tiled_comp_spv_len == 0 ||
        gemm_shader_tiled_comp_spv_len % sizeof(uint32_t) != 0) {
        LOGE("Invalid GEMM SPIR-V bytecode size");
        return false;
    }

    std::vector<uint32_t> alignedCode(gemm_shader_tiled_comp_spv_len / sizeof(uint32_t));
    std::memcpy(alignedCode.data(), gemm_shader_tiled_comp_spv,
                gemm_shader_tiled_comp_spv_len);

    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = gemm_shader_tiled_comp_spv_len;
    shaderInfo.pCode = alignedCode.data();
    if (!vkSucceeded(vkCreateShaderModule(context_.device(), &shaderInfo, nullptr,
                                           &shaderModule_),
                     "vkCreateShaderModule")) {
        return false;
    }

    // Bindings 0, 1, and 2 correspond to storage buffers A, B, and C. Their
    // numbering must match layout(binding=...) declarations in the compute shader.
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t index = 0; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorLayoutInfo.pBindings = bindings.data();
    if (!vkSucceeded(vkCreateDescriptorSetLayout(context_.device(), &descriptorLayoutInfo,
                                                  nullptr, &descriptorSetLayout_),
                     "vkCreateDescriptorSetLayout")) {
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (!vkSucceeded(vkCreatePipelineLayout(context_.device(), &pipelineLayoutInfo, nullptr,
                                             &pipelineLayout_),
                     "vkCreatePipelineLayout")) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule_;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_;
    return vkSucceeded(vkCreateComputePipelines(context_.device(), VK_NULL_HANDLE, 1,
                                                 &pipelineInfo, nullptr, &pipeline_),
                       "vkCreateComputePipelines");
}

bool GemmExecutor::createBuffers() {
    // A, B, and C are tightly packed row-major float arrays with different dimensions.
    VkDeviceSize sizeA = 0;
    VkDeviceSize sizeB = 0;
    VkDeviceSize sizeC = 0;
    if (!matrixByteSize(config_.rows, config_.depth, sizeA) ||
        !matrixByteSize(config_.depth, config_.columns, sizeB) ||
        !matrixByteSize(config_.rows, config_.columns, sizeC)) {
        LOGE("GEMM matrix dimensions overflow VkDeviceSize");
        return false;
    }

    if (!context_.createHostStorageBuffer(sizeA, bufferA_) ||
        !context_.createHostStorageBuffer(sizeB, bufferB_) ||
        !context_.createHostStorageBuffer(sizeC, bufferC_)) {
        return false;
    }

    // A fixed seed makes the input data and validation reproducible.
    std::mt19937 random(config_.randomSeed);
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

    void* mapped = nullptr;
    if (!vkSucceeded(vkMapMemory(context_.device(), bufferA_.memory(), 0, sizeA, 0,
                                 &mapped),
                     "vkMapMemory(A)")) {
        return false;
    }
    auto* values = static_cast<float*>(mapped);
    for (uint64_t index = 0; index < static_cast<uint64_t>(config_.rows) * config_.depth;
         ++index) {
        values[index] = distribution(random);
    }
    vkUnmapMemory(context_.device(), bufferA_.memory());

    if (!vkSucceeded(vkMapMemory(context_.device(), bufferB_.memory(), 0, sizeB, 0,
                                 &mapped),
                     "vkMapMemory(B)")) {
        return false;
    }
    values = static_cast<float*>(mapped);
    for (uint64_t index = 0;
         index < static_cast<uint64_t>(config_.depth) * config_.columns; ++index) {
        values[index] = distribution(random);
    }
    vkUnmapMemory(context_.device(), bufferB_.memory());

    if (!vkSucceeded(vkMapMemory(context_.device(), bufferC_.memory(), 0, sizeC, 0,
                                 &mapped),
                     "vkMapMemory(C)")) {
        return false;
    }
    std::memset(mapped, 0, static_cast<size_t>(sizeC));
    vkUnmapMemory(context_.device(), bufferC_.memory());
    return true;
}

bool GemmExecutor::createDescriptors() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (!vkSucceeded(vkCreateDescriptorPool(context_.device(), &poolInfo, nullptr,
                                             &descriptorPool_),
                     "vkCreateDescriptorPool")) {
        return false;
    }

    VkDescriptorSetAllocateInfo allocationInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocationInfo.descriptorPool = descriptorPool_;
    allocationInfo.descriptorSetCount = 1;
    allocationInfo.pSetLayouts = &descriptorSetLayout_;
    if (!vkSucceeded(vkAllocateDescriptorSets(context_.device(), &allocationInfo,
                                               &descriptorSet_),
                     "vkAllocateDescriptorSets")) {
        return false;
    }

    const std::array<VkDescriptorBufferInfo, 3> bufferInfos{{
        {bufferA_.handle(), 0, bufferA_.size()},
        {bufferB_.handle(), 0, bufferB_.size()},
        {bufferC_.handle(), 0, bufferC_.size()},
    }};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t index = 0; index < writes.size(); ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet_;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &bufferInfos[index];
    }
    vkUpdateDescriptorSets(context_.device(), static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
    return true;
}

bool GemmExecutor::createSubmissionObjects() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                     VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_.computeQueueFamilyIndex();
    if (!vkSucceeded(vkCreateCommandPool(context_.device(), &poolInfo, nullptr,
                                         &commandPool_),
                     "vkCreateCommandPool")) {
        return false;
    }

    VkCommandBufferAllocateInfo allocationInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocationInfo.commandPool = commandPool_;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    if (!vkSucceeded(vkAllocateCommandBuffers(context_.device(), &allocationInfo,
                                               &commandBuffer_),
                     "vkAllocateCommandBuffers")) {
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    return vkSucceeded(vkCreateFence(context_.device(), &fenceInfo, nullptr, &fence_),
                       "vkCreateFence");
}

int64_t GemmExecutor::run(const ProgressCallback& progress,
                          const StopPredicate& shouldStop,
                          bool validateResult) {
    if (shouldStop && shouldStop()) return 0;

    if (!vkSucceeded(vkResetCommandBuffer(commandBuffer_, 0),
                     "vkResetCommandBuffer")) {
        return -1;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!vkSucceeded(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
                     "vkBeginCommandBuffer")) {
        return -1;
    }

    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    const PushConstants constants{config_.rows, config_.columns, config_.depth, 0, 0};
    vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer_, workgroupCountX_, workgroupCountY_, 1);

    if (!vkSucceeded(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer") ||
        !vkSucceeded(vkResetFences(context_.device(), 1, &fence_),
                     "vkResetFences")) {
        return -1;
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;

    const auto start = std::chrono::steady_clock::now();
    if (!vkSucceeded(vkQueueSubmit(context_.computeQueue(), 1, &submitInfo, fence_),
                     "vkQueueSubmit")) {
        return -1;
    }
    if (!vkSucceeded(vkWaitForFences(context_.device(), 1, &fence_, VK_TRUE,
                                      UINT64_MAX),
                     "vkWaitForFences")) {
        context_.waitIdle();
        return -1;
    }
    const auto finish = std::chrono::steady_clock::now();

    if (progress) progress(1.0f);
    if (validateResult && !validate()) return -1;

    return std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();
}

bool GemmExecutor::validate() const {
    // A complete CPU GEMM would be too expensive, so reference values are
    // calculated for five samples covering the corners and interior.
    void* mappedA = nullptr;
    void* mappedB = nullptr;
    void* mappedC = nullptr;
    if (!vkSucceeded(vkMapMemory(context_.device(), bufferA_.memory(), 0,
                                 bufferA_.size(), 0, &mappedA),
                     "vkMapMemory(validation A)")) {
        return false;
    }
    if (!vkSucceeded(vkMapMemory(context_.device(), bufferB_.memory(), 0,
                                 bufferB_.size(), 0, &mappedB),
                     "vkMapMemory(validation B)")) {
        vkUnmapMemory(context_.device(), bufferA_.memory());
        return false;
    }
    if (!vkSucceeded(vkMapMemory(context_.device(), bufferC_.memory(), 0,
                                 bufferC_.size(), 0, &mappedC),
                     "vkMapMemory(validation C)")) {
        vkUnmapMemory(context_.device(), bufferB_.memory());
        vkUnmapMemory(context_.device(), bufferA_.memory());
        return false;
    }

    const auto* a = static_cast<const float*>(mappedA);
    const auto* b = static_cast<const float*>(mappedB);
    const auto* c = static_cast<const float*>(mappedC);
    const std::array<std::array<uint32_t, 2>, 5> samples{{
        {0, 0},
        {config_.rows / 3, config_.columns / 3},
        {config_.rows / 2, config_.columns / 2},
        {config_.rows - 1, config_.columns - 1},
        {config_.rows - 1, 0},
    }};

    bool valid = true;
    for (const auto& sample : samples) {
        const uint32_t row = sample[0];
        const uint32_t column = sample[1];
        float expected = 0.0f;
        for (uint32_t k = 0; k < config_.depth; ++k) {
            expected += a[static_cast<uint64_t>(row) * config_.depth + k] *
                        b[static_cast<uint64_t>(k) * config_.columns + column];
        }
        const float actual = c[static_cast<uint64_t>(row) * config_.columns + column];
        // The tolerance accounts for different floating-point accumulation orders
        // on the CPU and GPU.
        const float tolerance = std::max(0.05f, std::abs(expected) * 0.002f);
        if (!std::isfinite(actual) || std::abs(expected - actual) > tolerance) {
            LOGE("GEMM validation failed at (%u, %u): expected=%f actual=%f tolerance=%f",
                 row, column, expected, actual, tolerance);
            valid = false;
            break;
        }
    }

    vkUnmapMemory(context_.device(), bufferC_.memory());
    vkUnmapMemory(context_.device(), bufferB_.memory());
    vkUnmapMemory(context_.device(), bufferA_.memory());
    if (valid) LOGI("GEMM validation passed");
    return valid;
}

void GemmExecutor::release() {
    // Complete all commands before destroying the resources they use.
    // VulkanBuffer releases the matrix buffers later through RAII.
    context_.waitIdle();
    const VkDevice device = context_.device();
    if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device, fence_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (descriptorSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    if (shaderModule_ != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, shaderModule_, nullptr);

    fence_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSet_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    shaderModule_ = VK_NULL_HANDLE;
}

}  // namespace materialbench::vulkan
