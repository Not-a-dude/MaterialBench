#include "vulkan_context.h"

#include "../utils.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace materialbench::vulkan {
namespace {

bool vkSucceeded(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return true;
    LOGE("%s failed with VkResult %d", operation, static_cast<int>(result));
    return false;
}

}  // namespace

VulkanBuffer::~VulkanBuffer() {
    reset();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept {
    *this = std::move(other);
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this == &other) return *this;

    // Release the currently owned resources and take over the handles.
    // std::exchange clears the source, so its destructor will destroy nothing.
    reset();
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
    size_ = std::exchange(other.size_, 0);
    return *this;
}

void VulkanBuffer::reset() {
    // Destroy VkBuffer before freeing its bound memory, as required by Vulkan.
    // VK_NULL_HANDLE checks make this method idempotent.
    if (device_ != VK_NULL_HANDLE) {
        if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer_, nullptr);
        if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    size_ = 0;
}

std::unique_ptr<VulkanContext> VulkanContext::create() {
    auto context = std::unique_ptr<VulkanContext>(new VulkanContext());
    if (!context->initialize()) return nullptr;
    return context;
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
}

bool VulkanContext::initialize() {
    // A compute-only context needs only a basic instance without window-system
    // extensions. Request Vulkan 1.1 on supported Android devices.
    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "MaterialBench";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "MaterialBench Compute";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &applicationInfo;
    if (!vkSucceeded(vkCreateInstance(&instanceInfo, nullptr, &instance_), "vkCreateInstance")) {
        return false;
    }

    uint32_t deviceCount = 0;
    if (!vkSucceeded(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
                     "vkEnumeratePhysicalDevices(count)") || deviceCount == 0) {
        LOGE("No Vulkan physical device is available");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (!vkSucceeded(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
                     "vkEnumeratePhysicalDevices(list)")) {
        return false;
    }

    // Find a physical device and a compute-capable queue family. Prefer a
    // dedicated compute queue without GRAPHICS_BIT; otherwise use a shared
    // graphics/compute queue.
    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties candidateProperties{};
        vkGetPhysicalDeviceProperties(candidate, &candidateProperties);
        if (candidateProperties.limits.maxPushConstantsSize < sizeof(uint32_t) * 5) continue;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        if (familyCount == 0) continue;
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        for (uint32_t index = 0; index < familyCount; ++index) {
            if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
            const bool dedicated = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0;
            const int score = dedicated ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                physicalDevice_ = candidate;
                computeQueueFamilyIndex_ = index;
                properties_ = candidateProperties;
            }
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE || computeQueueFamilyIndex_ == UINT32_MAX) {
        LOGE("No Vulkan device with a compute queue is available");
        return false;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = computeQueueFamilyIndex_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (!vkSucceeded(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_),
                     "vkCreateDevice")) {
        return false;
    }

    vkGetDeviceQueue(device_, computeQueueFamilyIndex_, 0, &computeQueue_);
    if (computeQueue_ == VK_NULL_HANDLE) {
        LOGE("vkGetDeviceQueue returned a null compute queue");
        return false;
    }

    LOGI("Using Vulkan compute device: %s", properties_.deviceName);
    return true;
}

uint32_t VulkanContext::findMemoryType(uint32_t typeMask,
                                       VkMemoryPropertyFlags required) const {
    // typeMask identifies memory types compatible with the buffer. Select the
    // first compatible type containing all required memory property flags.
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeMask & (1u << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    return UINT32_MAX;
}

bool VulkanContext::createHostStorageBuffer(VkDeviceSize size, VulkanBuffer& output) const {
    // output either receives a fully initialized buffer or remains empty.
    output.reset();
    if (size == 0 || size > properties_.limits.maxStorageBufferRange) {
        LOGE("Unsupported storage buffer size: %llu (device limit: %u)",
             static_cast<unsigned long long>(size), properties_.limits.maxStorageBufferRange);
        return false;
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer),
                     "vkCreateBuffer")) {
        return false;
    }

    // VkBuffer does not contain memory by itself. The driver reports the required
    // size, alignment, and compatible memory types. The allocation is then bound
    // to the buffer explicitly with vkBindBufferMemory.
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    const uint32_t memoryType = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX) {
        LOGE("No host-visible coherent memory type for storage buffer");
        vkDestroyBuffer(device_, buffer, nullptr);
        return false;
    }

    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!vkSucceeded(vkAllocateMemory(device_, &allocationInfo, nullptr, &memory),
                     "vkAllocateMemory")) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return false;
    }

    if (!vkSucceeded(vkBindBufferMemory(device_, buffer, memory, 0),
                     "vkBindBufferMemory")) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        return false;
    }

    output.device_ = device_;
    output.buffer_ = buffer;
    output.memory_ = memory;
    output.size_ = size;
    return true;
}

void VulkanContext::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) {
        vkSucceeded(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

}  // namespace materialbench::vulkan
