#pragma once

#include "vulkan_context.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace materialbench::vulkan {

// Parameters for GEMM C = A * B:
// A: rows x depth, B: depth x columns, C: rows x columns.
struct GemmConfig {
    uint32_t rows = 0;
    uint32_t columns = 0;
    uint32_t depth = 0;
    uint32_t randomSeed = 0x4d42564b;
};

struct GemmRunMetrics {
    uint64_t elapsedNanoseconds = 0;
};

// Prepares and executes one matrix multiplication configuration.
// Owns the pipeline, descriptors, persistent submission objects, and buffers.
// The supplied VulkanContext must outlive this executor.
class GemmExecutor final {
public:
    using ProgressCallback = std::function<void(float)>;
    using StopPredicate = std::function<bool()>;

    static std::unique_ptr<GemmExecutor> create(VulkanContext& context,
                                                 const GemmConfig& config);
    ~GemmExecutor();

    GemmExecutor(const GemmExecutor&) = delete;
    GemmExecutor& operator=(const GemmExecutor&) = delete;

    // Executes one complete GEMM in one queue submission. The command buffer and
    // fence are reused across calls. Returns elapsed milliseconds or -1 on error.
    int64_t run(const ProgressCallback& progress,
                const StopPredicate& shouldStop,
                bool validateResult,
                GemmRunMetrics* metrics = nullptr);

private:
    GemmExecutor(VulkanContext& context, GemmConfig config);

    bool initialize();
    bool createPipeline();
    bool createBuffers();
    bool createDescriptors();
    bool createSubmissionObjects();
    bool validate() const;
    void release();

    VulkanContext& context_;
    GemmConfig config_;
    uint32_t workgroupCountX_ = 0;
    uint32_t workgroupCountY_ = 0;

    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    VulkanBuffer bufferA_;
    VulkanBuffer bufferB_;
    VulkanBuffer bufferC_;
};

}  // namespace materialbench::vulkan
