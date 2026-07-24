#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace materialbench::vulkan {

// RAII wrapper around VkBuffer and its associated VkDeviceMemory.
// Owns both Vulkan objects, releases them in the destructor, and supports
// ownership transfer. Copying is disabled to prevent double destruction.
class VulkanBuffer final {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    // Immediately releases the buffer and memory. Repeated calls are safe.
    void reset();

    // The returned handles do not transfer ownership and remain valid only while
    // this VulkanBuffer and the VulkanContext that created it are alive.
    VkBuffer handle() const { return buffer_; }
    VkDeviceMemory memory() const { return memory_; }
    VkDeviceSize size() const { return size_; }

private:
    friend class VulkanContext;

    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
};

// Owns the minimal set of Vulkan objects required for headless compute:
// instance -> physical device -> logical device -> compute queue.
// No surface or swapchain is created because this code does not render anything.
class VulkanContext final {
public:
    static std::unique_ptr<VulkanContext> create();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkQueue computeQueue() const { return computeQueue_; }
    uint32_t computeQueueFamilyIndex() const { return computeQueueFamilyIndex_; }
    const VkPhysicalDeviceProperties& properties() const { return properties_; }

    // Creates a storage buffer backed by HOST_VISIBLE | HOST_COHERENT memory.
    // The CPU can write and read it directly without a staging buffer or explicit
    // flush/invalidate operations.
    bool createHostStorageBuffer(VkDeviceSize size, VulkanBuffer& output) const;

    // Waits until all work submitted to the logical device has completed.
    void waitIdle() const;

private:
    VulkanContext() = default;

    bool initialize();
    uint32_t findMemoryType(uint32_t typeMask,
                            VkMemoryPropertyFlags required,
                            VkMemoryPropertyFlags preferred) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    uint32_t computeQueueFamilyIndex_ = UINT32_MAX;
};

}  // namespace materialbench::vulkan
