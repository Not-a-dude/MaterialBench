#pragma once

#include "vulkan_context.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace materialbench::vulkan {

// Parameters for GEMM C = A * B:
// A: rows x depth, B: depth x columns, C: rows x columns.
// chunkWorkgroups* splits the task into dispatch chunks for progress reporting
// and cancellation.
struct GemmConfig {
    uint32_t rows = 0;
    uint32_t columns = 0;
    uint32_t depth = 0;
    uint32_t chunkWorkgroupsX = 32;
    uint32_t chunkWorkgroupsY = 32;
    uint32_t randomSeed = 0x4d42564b;
};

// Prepares and executes one matrix multiplication configuration.
// Owns the pipeline, descriptors, command pool, and matrix buffers.
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

    // Submits compute chunks to the compute queue sequentially.
    // progress receives a value in [0, 1] after each dispatch, and shouldStop is
    // checked between dispatches. Returns elapsed milliseconds or -1 on failure.
    int64_t run(const ProgressCallback& progress,
                const StopPredicate& shouldStop,
                bool validateResult);

private:
    GemmExecutor(VulkanContext& context, GemmConfig config);

    // Initialization is split by resource type; release() is safe even after
    // only part of the initialization sequence has completed.
    bool initialize();
    bool createPipeline();
    bool createBuffers();
    bool createDescriptors();
    bool createCommandPool();
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

    VulkanBuffer bufferA_;
    VulkanBuffer bufferB_;
    VulkanBuffer bufferC_;
};

}  // namespace materialbench::vulkan
