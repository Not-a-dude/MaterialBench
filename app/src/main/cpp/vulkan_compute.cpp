// This entire file is a pure AI mess, unfortunately I don't have time to delve into it right now
#include <jni.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstring>
#include <thread>
#include <random>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "utils.h"
#include <vulkan/vulkan.h>
#include <android/log.h>

#include "shader.comp.spv.h"
#include "gemm_shader_tiled.comp.spv.h"

struct SharedVulkanContext {
    VkInstance instance{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue computeQueue{};
    uint32_t computeQueueFamilyIndex{};
    VkPhysicalDeviceProperties deviceProperties{};
};

struct VulkanNBodyContext {
    SharedVulkanContext* shared = nullptr;
    VkShaderModule shaderModule{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    VkCommandPool commandPool{};
    VkDescriptorPool descriptorPool{};

    VkBuffer positionsBuffer[2]{}, velocitiesBuffer[2]{};
    VkDeviceMemory positionsMemory[2]{}, velocitiesMemory[2]{};
    VkDescriptorSet descriptorSets[2]{};

    uint32_t numParticles{};
    float deltaTime{};
    float softeningSq{};
    float gravitationalConstant{};

    uint32_t currentReadIndex = 0;
    uint32_t currentWriteIndex = 1;

    uint32_t workgroupCount{};
    uint32_t workgroupSize{};
};

struct VulkanGEMMContext {
    SharedVulkanContext* shared = nullptr;
    VkShaderModule shaderModule{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    VkCommandPool commandPool{};
    VkDescriptorPool descriptorPool{};

    VkBuffer matrixABuffer{}, matrixBBuffer{}, matrixCBuffer{};
    VkDeviceMemory matrixAMemory{}, matrixBMemory{}, matrixCMemory{};
    VkDescriptorSet descriptorSet{};

    uint32_t M = 0, N = 0, K = 0;
    uint32_t workgroupCountX = 0, workgroupCountY = 0;
};

static std::unique_ptr<SharedVulkanContext> g_sharedContext = nullptr;
static std::mutex g_initMutex;
static std::mutex g_stressMutex;
static std::condition_variable g_stressCV;
static std::thread g_stressThread;
static std::atomic<bool> stop_gpu_stress(false);
static std::atomic<bool> g_stressThreadRunning(false);

bool initSharedVulkanContext(SharedVulkanContext& context);
void cleanupSharedVulkanContext(SharedVulkanContext& context);

SharedVulkanContext* getSharedContext() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (!g_sharedContext) {
        auto context = std::make_unique<SharedVulkanContext>();
        if (initSharedVulkanContext(*context)) {
            g_sharedContext = std::move(context);
        } else {
            LOGE("Failed to initialize SHARED Vulkan context");
            cleanupSharedVulkanContext(*context);
            return nullptr;
        }
    }
    return g_sharedContext.get();
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return -1; // Indicate failure
}

bool createBufferOptimized(SharedVulkanContext* shared,
                           VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties,
                           VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(shared->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(shared->device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    uint32_t memoryTypeIndex = findMemoryType(shared->physicalDevice, memRequirements.memoryTypeBits, properties);

    if (memoryTypeIndex == -1) {
        return false;
    }
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(shared->device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) return false;

    return vkBindBufferMemory(shared->device, buffer, bufferMemory, 0) == VK_SUCCESS;
}

bool initSharedVulkanContext(SharedVulkanContext& context) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MaterialBench Vulkan Compute";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &context.instance) != VK_SUCCESS) return false;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data());

    context.physicalDevice = devices[0];

    vkGetPhysicalDeviceProperties(context.physicalDevice, &context.deviceProperties);
    LOGI("Using shared device: %s", context.deviceProperties.deviceName);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &queueFamilyCount, queueFamilies.data());

    context.computeQueueFamilyIndex = -1;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            context.computeQueueFamilyIndex = i;
            break;
        }
    }
    if (context.computeQueueFamilyIndex == -1) return false;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = context.computeQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    if (vkCreateDevice(context.physicalDevice, &deviceCreateInfo, nullptr, &context.device) != VK_SUCCESS) return false;

    vkGetDeviceQueue(context.device, context.computeQueueFamilyIndex, 0, &context.computeQueue);
    return true;
}

void cleanupSharedVulkanContext(SharedVulkanContext& context) {
    if (context.device) {
        vkDestroyDevice(context.device, nullptr);
    }
    if (context.instance) {
        vkDestroyInstance(context.instance, nullptr);
    }
}

bool createNBodyComputePipeline(VulkanNBodyContext& context) {
    std::vector<char> shaderCode(reinterpret_cast<const char*>(shader_comp_spv),
                                 reinterpret_cast<const char*>(shader_comp_spv) + shader_comp_spv_len);

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = shaderCode.size();
    shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    if (vkCreateShaderModule(context.shared->device, &shaderModuleInfo, nullptr, &context.shaderModule) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding bindings[4]{};
    for(uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(context.shared->device, &layoutInfo, nullptr, &context.descriptorSetLayout) != VK_SUCCESS) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(uint32_t) + sizeof(float) * 3;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.shared->device, &pipelineLayoutInfo, nullptr, &context.pipelineLayout) != VK_SUCCESS) return false;

    struct SpecializationData {
        uint32_t local_size_x;
    } specializationData = { context.workgroupSize };
    specializationData.local_size_x = context.workgroupSize;

    VkSpecializationMapEntry specializationMapEntry{};
    specializationMapEntry.constantID = 0;
    specializationMapEntry.offset = 0;
    specializationMapEntry.size = sizeof(uint32_t);

    VkSpecializationInfo specializationInfo{};
    specializationInfo.mapEntryCount = 1;
    specializationInfo.pMapEntries = &specializationMapEntry;
    specializationInfo.dataSize = sizeof(specializationData);
    specializationInfo.pData = &specializationData;

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = context.shaderModule;
    shaderStageInfo.pName = "main";
    shaderStageInfo.pSpecializationInfo = &specializationInfo;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = context.pipelineLayout;

    return vkCreateComputePipelines(context.shared->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &context.pipeline) == VK_SUCCESS;
}

bool createGEMMComputePipeline(VulkanGEMMContext& context) {
    std::vector<char> shaderCode(reinterpret_cast<const char*>(gemm_shader_tiled_comp_spv),
                                 reinterpret_cast<const char*>(gemm_shader_tiled_comp_spv) + gemm_shader_tiled_comp_spv_len);

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = shaderCode.size();
    shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    if (vkCreateShaderModule(context.shared->device, &shaderModuleInfo, nullptr, &context.shaderModule) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding bindings[3]{};
    for(uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(context.shared->device, &layoutInfo, nullptr, &context.descriptorSetLayout) != VK_SUCCESS) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(uint32_t) * 3; // N, M, K

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.shared->device, &pipelineLayoutInfo, nullptr, &context.pipelineLayout) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = context.shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = context.pipelineLayout;

    return vkCreateComputePipelines(context.shared->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &context.pipeline) == VK_SUCCESS;
}

bool createNBodyBuffersAndDescriptors(VulkanNBodyContext& context, uint32_t numParticles_param, float deltaTime_param, float softeningSq_param, float gravitationalConstant_param) {
    context.numParticles = numParticles_param;
    context.deltaTime = deltaTime_param;
    context.softeningSq = softeningSq_param;
    context.gravitationalConstant = gravitationalConstant_param;

    VkDeviceSize bufferSize = context.numParticles * sizeof(float) * 4;
    VkMemoryPropertyFlags deviceLocalMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags hostVisibleMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Create staging buffers
    VkBuffer stagingPositionsBuffer[2], stagingVelocitiesBuffer[2];
    VkDeviceMemory stagingPositionsMemory[2], stagingVelocitiesMemory[2];

    for (int i = 0; i < 2; ++i) {
        if (!createBufferOptimized(context.shared, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hostVisibleMemFlags, stagingPositionsBuffer[i], stagingPositionsMemory[i])) return false;
        if (!createBufferOptimized(context.shared, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hostVisibleMemFlags, stagingVelocitiesBuffer[i], stagingVelocitiesMemory[i])) return false;
    }

    // Create device local buffers
    for (int i = 0; i < 2; ++i) {
        if (!createBufferOptimized(context.shared, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, deviceLocalMemFlags, context.positionsBuffer[i], context.positionsMemory[i])) {
            LOGE("Failed to create device local positions buffer %d", i);
            return false;
        }
        if (!createBufferOptimized(context.shared, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, deviceLocalMemFlags, context.velocitiesBuffer[i], context.velocitiesMemory[i])) {
            LOGE("Failed to create device local velocities buffer %d", i);
            return false;
        }
    }

    std::default_random_engine rng(std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> massDist(1e10f, 1e12f);

    float* initialPositionsData;
    vkMapMemory(context.shared->device, stagingPositionsMemory[0], 0, bufferSize, 0, (void**)&initialPositionsData);
    float* initialVelocitiesData;
    vkMapMemory(context.shared->device, stagingVelocitiesMemory[0], 0, bufferSize, 0, (void**)&initialVelocitiesData);

    for (uint32_t i = 0; i < context.numParticles; ++i) {
        initialPositionsData[i * 4 + 0] = dist(rng); // x
        initialPositionsData[i * 4 + 1] = dist(rng); // y
        initialPositionsData[i * 4 + 2] = dist(rng); // z
        initialPositionsData[i * 4 + 3] = massDist(rng); // mass

        initialVelocitiesData[i * 4 + 0] = dist(rng) * 0.01f; // vx
        initialVelocitiesData[i * 4 + 1] = dist(rng) * 0.01f; // vy
        initialVelocitiesData[i * 4 + 2] = dist(rng) * 0.01f; // vz
        initialVelocitiesData[i * 4 + 3] = 0.0f; // unused
    }

    vkUnmapMemory(context.shared->device, stagingPositionsMemory[0]);
    vkUnmapMemory(context.shared->device, stagingVelocitiesMemory[0]);

    // Create command pool and buffer for transfer
    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.queueFamilyIndex = context.shared->computeQueueFamilyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool transferCommandPool;
    if (vkCreateCommandPool(context.shared->device, &poolCreateInfo, nullptr, &transferCommandPool) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = transferCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(context.shared->device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(context.shared->device, transferCommandPool, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;

    vkCmdCopyBuffer(commandBuffer, stagingPositionsBuffer[0], context.positionsBuffer[0], 1, &copyRegion);
    vkCmdCopyBuffer(commandBuffer, stagingVelocitiesBuffer[0], context.velocitiesBuffer[0], 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(context.shared->computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context.shared->computeQueue);

    vkDestroyCommandPool(context.shared->device, transferCommandPool, nullptr);

    // Destroy staging buffers
    for (int i = 0; i < 2; ++i) {
        vkDestroyBuffer(context.shared->device, stagingPositionsBuffer[i], nullptr);
        vkFreeMemory(context.shared->device, stagingPositionsMemory[i], nullptr);
        vkDestroyBuffer(context.shared->device, stagingVelocitiesBuffer[i], nullptr);
        vkFreeMemory(context.shared->device, stagingVelocitiesMemory[i], nullptr);
    }


    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    if (vkCreateDescriptorPool(context.shared->device, &poolInfo, nullptr, &context.descriptorPool) != VK_SUCCESS) return false;

    VkDescriptorSetLayout layouts[2] = {context.descriptorSetLayout, context.descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfoDesc{};
    allocInfoDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoDesc.descriptorPool = context.descriptorPool;
    allocInfoDesc.descriptorSetCount = 2;
    allocInfoDesc.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(context.shared->device, &allocInfoDesc, context.descriptorSets) != VK_SUCCESS) return false;

    for (int i = 0; i < 2; ++i) {
        uint32_t readIndex = i;
        uint32_t writeIndex = 1 - i;

        VkDescriptorBufferInfo positionsInputInfo{context.positionsBuffer[readIndex], 0, bufferSize};
        VkDescriptorBufferInfo velocitiesInputInfo{context.velocitiesBuffer[readIndex], 0, bufferSize};
        VkDescriptorBufferInfo positionsOutputInfo{context.positionsBuffer[writeIndex], 0, bufferSize};
        VkDescriptorBufferInfo velocitiesOutputInfo{context.velocitiesBuffer[writeIndex], 0, bufferSize};

        VkWriteDescriptorSet descriptorWrites[4]{};
        for (int j = 0; j < 4; ++j) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = context.descriptorSets[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[j].descriptorCount = 1;
        }
        descriptorWrites[0].pBufferInfo = &positionsInputInfo;
        descriptorWrites[1].pBufferInfo = &velocitiesInputInfo;
        descriptorWrites[2].pBufferInfo = &positionsOutputInfo;
        descriptorWrites[3].pBufferInfo = &velocitiesOutputInfo;

        vkUpdateDescriptorSets(context.shared->device, 4, descriptorWrites, 0, nullptr);
    }


    VkCommandPoolCreateInfo computePoolCreateInfo{};
    computePoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    computePoolCreateInfo.queueFamilyIndex = context.shared->computeQueueFamilyIndex;
    computePoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    return vkCreateCommandPool(context.shared->device, &computePoolCreateInfo, nullptr, &context.commandPool) == VK_SUCCESS;
}

bool createGEMMBuffersAndDescriptors(VulkanGEMMContext& context, uint32_t N_param, uint32_t M_param, uint32_t K_param) {
    context.N = N_param;
    context.M = M_param;
    context.K = K_param;

    VkDeviceSize bufferASize = context.N * context.K * sizeof(float);
    VkDeviceSize bufferBSize = context.K * context.M * sizeof(float);
    VkDeviceSize bufferCSize = context.N * context.M * sizeof(float);
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!createBufferOptimized(context.shared, bufferASize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memFlags, context.matrixABuffer, context.matrixAMemory)) return false;
    if (!createBufferOptimized(context.shared, bufferBSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memFlags, context.matrixBBuffer, context.matrixBMemory)) return false;
    if (!createBufferOptimized(context.shared, bufferCSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memFlags, context.matrixCBuffer, context.matrixCMemory)) return false;

    std::default_random_engine rng(std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float* aData;
    vkMapMemory(context.shared->device, context.matrixAMemory, 0, bufferASize, 0, (void**)&aData);
    for (uint32_t i = 0; i < context.N * context.K; ++i) aData[i] = dist(rng);
    vkUnmapMemory(context.shared->device, context.matrixAMemory);

    float* bData;
    vkMapMemory(context.shared->device, context.matrixBMemory, 0, bufferBSize, 0, (void**)&bData);
    for (uint32_t i = 0; i < context.K * context.M; ++i) bData[i] = dist(rng);
    vkUnmapMemory(context.shared->device, context.matrixBMemory);


    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(context.shared->device, &poolInfo, nullptr, &context.descriptorPool) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = context.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &context.descriptorSetLayout;
    if (vkAllocateDescriptorSets(context.shared->device, &allocInfo, &context.descriptorSet) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo matrixAInfo{context.matrixABuffer, 0, bufferASize};
    VkDescriptorBufferInfo matrixBInfo{context.matrixBBuffer, 0, bufferBSize};
    VkDescriptorBufferInfo matrixCInfo{context.matrixCBuffer, 0, bufferCSize};

    VkWriteDescriptorSet descriptorWrites[3]{};
    for (int i = 0; i < 3; ++i) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = context.descriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
    }
    descriptorWrites[0].pBufferInfo = &matrixAInfo;
    descriptorWrites[1].pBufferInfo = &matrixBInfo;
    descriptorWrites[2].pBufferInfo = &matrixCInfo;

    vkUpdateDescriptorSets(context.shared->device, 3, descriptorWrites, 0, nullptr);


    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.queueFamilyIndex = context.shared->computeQueueFamilyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    return vkCreateCommandPool(context.shared->device, &poolCreateInfo, nullptr, &context.commandPool) == VK_SUCCESS;
}

jlong runNBodyCompute(VulkanNBodyContext& context, uint32_t numSteps, JNIEnv* env, jobject activity_global_ref, jmethodID update_progress_method_id) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(context.shared->device, &allocInfo, &commandBuffer) != VK_SUCCESS) return -1;

    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(context.shared->device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
        return -1;
    }

    auto total_start = std::chrono::high_resolution_clock::now();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // Use ONE_TIME for recording everything once

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    for (uint32_t step = 0; step < numSteps; ++step) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipelineLayout, 0, 1, &context.descriptorSets[context.currentReadIndex], 0, nullptr);

        struct PushConstantsData {
            uint32_t numParticles;
            float deltaTime;
            float softeningSq;
            float gravitationalConstant;
        } pushConstantsData = {context.numParticles, context.deltaTime, context.softeningSq, context.gravitationalConstant};
        vkCmdPushConstants(commandBuffer, context.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsData), &pushConstantsData);

        vkCmdDispatch(commandBuffer, context.workgroupCount, 1, 1);

        // Add memory barrier to ensure writes from this step are visible to reads in the next step
        VkBufferMemoryBarrier bufferMemoryBarrier{};
        bufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bufferMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferMemoryBarrier.buffer = context.positionsBuffer[context.currentWriteIndex];
        bufferMemoryBarrier.offset = 0;
        bufferMemoryBarrier.size = VK_WHOLE_SIZE;

        VkBufferMemoryBarrier velocityMemoryBarrier{};
        velocityMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        velocityMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        velocityMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        velocityMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        velocityMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        velocityMemoryBarrier.buffer = context.velocitiesBuffer[context.currentWriteIndex];
        velocityMemoryBarrier.offset = 0;
        velocityMemoryBarrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Source stage for write
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Destination stage for read
                             0, 0, nullptr, 1, &bufferMemoryBarrier, 0, nullptr);
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Source stage for write
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Destination stage for read
                             0, 0, nullptr, 1, &velocityMemoryBarrier, 0, nullptr);

        std::swap(context.currentReadIndex, context.currentWriteIndex);

        if (activity_global_ref != nullptr && update_progress_method_id != nullptr) {
            // Update progress only at certain intervals to avoid too many JNI calls
            // For now, let's keep it per step for demonstration, but consider throttling.
            float progress = static_cast<float>(step + 1) / static_cast<float>(numSteps);
            env->CallVoidMethod(activity_global_ref, update_progress_method_id, progress);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                LOGE("Exception during progress callback");
            }
        }
    }
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(context.shared->computeQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(context.shared->device, fence, nullptr);
        vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
        return -1;
    }

    vkWaitForFences(context.shared->device, 1, &fence, VK_TRUE, UINT64_MAX);

    auto total_end = std::chrono::high_resolution_clock::now();
    vkDestroyFence(context.shared->device, fence, nullptr);
    vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
    return std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
}

jlong runGEMMCompute(VulkanGEMMContext& context, uint32_t N_param, uint32_t M_param, uint32_t K_param, JNIEnv* env, jobject activity_global_ref, jmethodID update_progress_method_id) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(context.shared->device, &allocInfo, &commandBuffer) != VK_SUCCESS) return -1;

    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(context.shared->device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
        return -1;
    }

    auto total_start = std::chrono::high_resolution_clock::now();

    auto vkCmdDispatchBase = (PFN_vkCmdDispatchBase) vkGetDeviceProcAddr(context.shared->device, "vkCmdDispatchBase");
    if (!vkCmdDispatchBase) {
        LOGE("vkCmdDispatchBase is not available, cannot provide progress for GEMM");
        // Fallback to old behavior
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipelineLayout, 0, 1, &context.descriptorSet, 0, nullptr);
        struct PushConstantsData { uint32_t N, M, K; } pushConstantsData = { N_param, M_param, K_param };
        vkCmdPushConstants(commandBuffer, context.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsData), &pushConstantsData);
        vkCmdDispatch(commandBuffer, context.workgroupCountX, context.workgroupCountY, 1);
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(context.shared->computeQueue, 1, &submitInfo, fence);
        vkWaitForFences(context.shared->device, 1, &fence, VK_TRUE, UINT64_MAX);
    } else {
        const uint32_t progress_steps = 100;
        uint32_t rows_per_step = (context.workgroupCountY + progress_steps - 1) / progress_steps;
        if (rows_per_step == 0) rows_per_step = 1;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        struct PushConstantsData { uint32_t N, M, K; } pushConstantsData = { N_param, M_param, K_param };

        for (uint32_t y_base = 0; y_base < context.workgroupCountY; y_base += rows_per_step) {
            uint32_t groupCountY = std::min(rows_per_step, context.workgroupCountY - y_base);

            vkBeginCommandBuffer(commandBuffer, &beginInfo);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipelineLayout, 0, 1, &context.descriptorSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, context.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsData), &pushConstantsData);
            vkCmdDispatchBase(commandBuffer, 0, y_base, 0, context.workgroupCountX, groupCountY, 1);
            vkEndCommandBuffer(commandBuffer);

            vkResetFences(context.shared->device, 1, &fence);
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            if (vkQueueSubmit(context.shared->computeQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
                vkDestroyFence(context.shared->device, fence, nullptr);
                vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
                return -1;
            }
            vkWaitForFences(context.shared->device, 1, &fence, VK_TRUE, UINT64_MAX);
            vkResetCommandBuffer(commandBuffer, 0);

            if (activity_global_ref != nullptr && update_progress_method_id != nullptr) {
                float progress = static_cast<float>(y_base + groupCountY) / static_cast<float>(context.workgroupCountY);
                env->CallVoidMethod(activity_global_ref, update_progress_method_id, progress);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    LOGE("Exception during progress callback");
                }
            }
        }
    }

    auto total_end = std::chrono::high_resolution_clock::now();

    vkDestroyFence(context.shared->device, fence, nullptr);
    vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);

    return std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
}


void cleanupVulkanNBody(VulkanNBodyContext& context) {
    if (!context.shared || !context.shared->device) return;

    VkDevice device = context.shared->device;
    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < 2; ++i) {
        if (context.positionsBuffer[i]) vkDestroyBuffer(device, context.positionsBuffer[i], nullptr);
        if (context.positionsMemory[i]) vkFreeMemory(device, context.positionsMemory[i], nullptr);
        if (context.velocitiesBuffer[i]) vkDestroyBuffer(device, context.velocitiesBuffer[i], nullptr);
        if (context.velocitiesMemory[i]) vkFreeMemory(device, context.velocitiesMemory[i], nullptr);
    }

    if (context.descriptorPool) vkDestroyDescriptorPool(device, context.descriptorPool, nullptr);
    if (context.pipeline) vkDestroyPipeline(device, context.pipeline, nullptr);
    if (context.pipelineLayout) vkDestroyPipelineLayout(device, context.pipelineLayout, nullptr);
    if (context.descriptorSetLayout) vkDestroyDescriptorSetLayout(device, context.descriptorSetLayout, nullptr);
    if (context.shaderModule) vkDestroyShaderModule(device, context.shaderModule, nullptr);
    if (context.commandPool) vkDestroyCommandPool(device, context.commandPool, nullptr);
}

void cleanupVulkanGEMM(VulkanGEMMContext& context) {
    if (!context.shared || !context.shared->device) return;

    VkDevice device = context.shared->device;
    vkDeviceWaitIdle(device);

    if (context.matrixABuffer) vkDestroyBuffer(device, context.matrixABuffer, nullptr);
    if (context.matrixAMemory) vkFreeMemory(device, context.matrixAMemory, nullptr);
    if (context.matrixBBuffer) vkDestroyBuffer(device, context.matrixBBuffer, nullptr);
    if (context.matrixBMemory) vkFreeMemory(device, context.matrixBMemory, nullptr);
    if (context.matrixCBuffer) vkDestroyBuffer(device, context.matrixCBuffer, nullptr);
    if (context.matrixCMemory) vkFreeMemory(device, context.matrixCMemory, nullptr);

    if (context.descriptorPool) vkDestroyDescriptorPool(device, context.descriptorPool, nullptr);
    if (context.pipeline) vkDestroyPipeline(device, context.pipeline, nullptr);
    if (context.pipelineLayout) vkDestroyPipelineLayout(device, context.pipelineLayout, nullptr);
    if (context.descriptorSetLayout) vkDestroyDescriptorSetLayout(device, context.descriptorSetLayout, nullptr);
    if (context.shaderModule) vkDestroyShaderModule(device, context.shaderModule, nullptr);
    if (context.commandPool) vkDestroyCommandPool(device, context.commandPool, nullptr);
}

void gpu_stress_task() {
    {
        std::lock_guard<std::mutex> lock(g_stressMutex);
        g_stressThreadRunning.store(true, std::memory_order_relaxed);
    }
    g_stressCV.notify_all();

    LOGI("GPU stress test started");

    SharedVulkanContext* shared = getSharedContext();
    if (!shared) {
        LOGE("Failed to get shared context for GEMM stress test");
        g_stressThreadRunning.store(false, std::memory_order_relaxed);
        return;
    }

    VulkanGEMMContext context{};
    context.shared = shared;

    uint32_t N = 512;
    uint32_t M = 512;
    uint32_t K = 384;
    const uint32_t TILE_DIM = 16;

    context.N = N;
    context.M = M;
    context.K = K;

    context.workgroupCountX = (M + TILE_DIM - 1) / TILE_DIM;
    context.workgroupCountY = (N + TILE_DIM - 1) / TILE_DIM;
    LOGI("Stress test GEMM (Tiled): N=%u, M=%u, K=%u, Tile=%u, Workgroups: X=%u, Y=%u",
         N, M, K, TILE_DIM, context.workgroupCountX, context.workgroupCountY);

    if (!createGEMMComputePipeline(context)) {
        LOGE("Failed to create compute pipeline for GEMM stress test");
        cleanupVulkanGEMM(context);
        g_stressThreadRunning.store(false, std::memory_order_relaxed);
        return;
    }

    if (!createGEMMBuffersAndDescriptors(context, N, M, K)) {
        LOGE("Failed to create buffers and descriptors for GEMM stress test");
        cleanupVulkanGEMM(context);
        g_stressThreadRunning.store(false, std::memory_order_relaxed);
        return;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(context.shared->device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffers for GEMM stress test");
        cleanupVulkanGEMM(context);
        g_stressThreadRunning.store(false, std::memory_order_relaxed);
        return;
    }

    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(context.shared->device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        LOGE("Failed to create fence for GEMM stress test");
        vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
        cleanupVulkanGEMM(context);
        g_stressThreadRunning.store(false, std::memory_order_relaxed);
        return;
    }

    while (!stop_gpu_stress.load(std::memory_order_relaxed)) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            LOGE("Failed to begin command buffer for GPU stress test");
            break;
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipelineLayout, 0, 1, &context.descriptorSet, 0, nullptr);

        struct PushConstantsData {
            uint32_t N;
            uint32_t M;
            uint32_t K;
        } pushConstantsData = { N, M, K };

        vkCmdPushConstants(commandBuffer, context.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsData), &pushConstantsData);

        vkCmdDispatch(commandBuffer, context.workgroupCountX, context.workgroupCountY, 1);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            LOGE("Failed to end command buffer for GPU stress test");
            break;
        }

        VkResult resetResult = vkResetFences(context.shared->device, 1, &fence);
        if (resetResult != VK_SUCCESS) {
            LOGE("Failed to reset fence for GPU stress test: %d", resetResult);
            break;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(context.shared->computeQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
            LOGE("Failed to submit queue for GPU stress test");
            break;
        }

        VkResult waitResult = vkWaitForFences(context.shared->device, 1, &fence, VK_TRUE, UINT64_MAX);

        if (waitResult == VK_TIMEOUT) {
            continue;
        } else if (waitResult != VK_SUCCESS) {
            LOGE("Failed to wait for fence in GPU stress test: %d", waitResult);
            break;
        }

        if (stop_gpu_stress.load(std::memory_order_relaxed)) {
            break;
        }
    }

    vkDestroyFence(context.shared->device, fence, nullptr);
    vkFreeCommandBuffers(context.shared->device, context.commandPool, 1, &commandBuffer);
    cleanupVulkanGEMM(context);

    g_stressThreadRunning.store(false, std::memory_order_relaxed);
    LOGI("GPU stress test finished");
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_ui_MainActivity_nativeCleanup(
        JNIEnv *env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_sharedContext) {
        cleanupSharedVulkanContext(*g_sharedContext);
        g_sharedContext.reset(nullptr);
        LOGI("Vulkan Context cleaned up.");
    }
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeBenchCleanup(
        JNIEnv *env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_sharedContext) {
        cleanupSharedVulkanContext(*g_sharedContext);
        g_sharedContext.reset(nullptr);
        LOGI("Vulkan Context cleaned up.");
    }
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunVulkanNBodyBenchmark(
        JNIEnv *env, jobject thiz, jobject activity) {

    SharedVulkanContext* shared = getSharedContext();
    if (!shared) {
        LOGE("Failed to get/init shared Vulkan context for N-Body");
        return -1;
    }

    jobject activity_global_ref = env->NewGlobalRef(activity);
    jclass activity_class = env->GetObjectClass(activity_global_ref);
    jmethodID update_progress_method_id = env->GetMethodID(activity_class, "updateBenchmarkProgress", "(F)V");

    VulkanNBodyContext context{};
    context.shared = shared;

    uint32_t numParticles = 32768;
    const uint32_t numSteps = 1000;
    float deltaTime = 0.01f;
    float softeningSq = 1.0f;
    float gravitationalConstant = 1.0f;

    context.numParticles = numParticles;
    context.deltaTime = deltaTime;
    context.softeningSq = softeningSq;
    context.gravitationalConstant = gravitationalConstant;

    context.workgroupSize = 64;
    context.workgroupCount = static_cast<uint32_t>((context.numParticles + context.workgroupSize - 1) / context.workgroupSize);
    LOGI("N-Body Manual workgroup size: %u, count: %u", context.workgroupSize, context.workgroupCount);

    if (!createNBodyComputePipeline(context)) {
        LOGE("Failed to create compute pipeline");
        cleanupVulkanNBody(context);
        env->DeleteGlobalRef(activity_global_ref);
        return -1;
    }

    if (!createNBodyBuffersAndDescriptors(context, numParticles, deltaTime, softeningSq, gravitationalConstant)) {
        LOGE("Failed to create buffers and descriptors");
        cleanupVulkanNBody(context);
        env->DeleteGlobalRef(activity_global_ref);
        return -1;
    }

    jlong duration = runNBodyCompute(context, numSteps, env, activity_global_ref, update_progress_method_id);

    // Removed direct vkMapMemory of device local buffer. If read-back is needed, a staging buffer copy is required.
    LOGI("N-Body simulation finished. Final positions of first 3 particles (readback currently disabled for device-local memory):");


    cleanupVulkanNBody(context);
    env->DeleteGlobalRef(activity_global_ref);

    return duration;
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunVulkanGEMMBenchmark(
        JNIEnv *env, jobject thiz, jobject activity_param) {

    SharedVulkanContext* shared = getSharedContext();
    if (!shared) {
        LOGE("Failed to get/init shared Vulkan context for GEMM");
        return -1;
    }

    jobject activity_global_ref = env->NewGlobalRef(activity_param);
    jclass activity_class = env->GetObjectClass(activity_global_ref);
    jmethodID update_progress_method_id = env->GetMethodID(activity_class, "updateBenchmarkProgress", "(F)V");
    if (update_progress_method_id == nullptr) {
        LOGE("Could not find method updateBenchmarkProgress");
        env->DeleteGlobalRef(activity_global_ref);
        return -1;
    }

    auto N = static_cast<uint32_t>(8192);
    auto M = static_cast<uint32_t>(8192);
    auto K = static_cast<uint32_t>(5120);

    VulkanGEMMContext context{};
    context.shared = shared;

    const uint32_t TILE_DIM = 16;

    context.workgroupCountX = (M + TILE_DIM - 1) / TILE_DIM;
    context.workgroupCountY = (N + TILE_DIM - 1) / TILE_DIM;
    LOGI("GEMM (Tiled): N=%u, M=%u, K=%u, Tile=%u, Workgroups: X=%u, Y=%u",
         N, M, K, TILE_DIM, context.workgroupCountX, context.workgroupCountY);

    if (!createGEMMComputePipeline(context)) {
        LOGE("Failed to create compute GEMM pipeline");
        cleanupVulkanGEMM(context);
        env->DeleteGlobalRef(activity_global_ref);
        return -1;
    }

    if (!createGEMMBuffersAndDescriptors(context, N, M, K)) {
        LOGE("Failed to create GEMM buffers and descriptors");
        cleanupVulkanGEMM(context);
        env->DeleteGlobalRef(activity_global_ref);
        return -1;
    }

    jlong duration = runGEMMCompute(context, N, M, K, env, activity_global_ref, update_progress_method_id);

    float* cData;
    vkMapMemory(context.shared->device, context.matrixCMemory, 0, N * M * sizeof(float), 0, (void**)&cData);

    LOGI("GEMM calculation finished. First few values of Matrix C:");
    for (uint32_t i = 0; i < std::min((uint32_t)5, N); ++i) {
        for (uint32_t j = 0; j < std::min((uint32_t)5, M); ++j) {
            LOGI("  C[%u][%u]: %.2f", i, j, cData[i * M + j]);
        }
    }
    vkUnmapMemory(context.shared->device, context.matrixCMemory);

    cleanupVulkanGEMM(context);
    env->DeleteGlobalRef(activity_global_ref);

    return duration;
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_ui_MainActivity_nativeStartGpuStress(
        JNIEnv *env, jobject thiz) {
    if (g_stressThreadRunning.load(std::memory_order_relaxed)) {
        LOGI("GPU stress test is already running");
        return;
    }

    stop_gpu_stress.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(g_stressMutex);
    if (g_stressThread.joinable()) {
        g_stressThread.join();
    }

    g_stressThread = std::thread(gpu_stress_task);
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_ui_MainActivity_nativeStopGpuStress(
        JNIEnv *env, jobject thiz) {
    if (!g_stressThreadRunning.load(std::memory_order_relaxed)) {
        LOGI("GPU stress test is not running");
        return;
    }

    LOGI("Stopping GPU stress test...");
    stop_gpu_stress.store(true, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(g_stressMutex);
    if (g_stressThread.joinable()) {
        g_stressThread.join();
        LOGI("GPU stress test stopped successfully");
    }
}

}