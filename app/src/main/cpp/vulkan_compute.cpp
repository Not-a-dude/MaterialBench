#include <jni.h>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <string>
#include <algorithm>
#include <vulkan/vulkan.h>
#include "shader.comp.spv.h"
#include "gemm_shader_tiled.comp.spv.h"
#include "utils.h"

// --- Structures ---

struct SharedVulkanContext {
    VkInstance instance{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue computeQueue{};
    uint32_t computeQueueFamilyIndex{UINT32_MAX};
};

struct GEMMContext {
    SharedVulkanContext* shared = nullptr;
    VkShaderModule shaderModule{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    VkCommandPool commandPool{};
    VkDescriptorPool descriptorPool{};
    VkBuffer bufA{}, bufB{}, bufC{};
    VkDeviceMemory memA{}, memB{}, memC{};
    VkDescriptorSet descriptorSet{};
    uint32_t N = 0, M = 0, K = 0;
    uint32_t workgroupCountX = 0, workgroupCountY = 0;
};

// --- Global State ---

static std::unique_ptr<SharedVulkanContext> g_sharedContext;
static std::mutex g_initMutex, g_stressMutex;
static std::thread g_stressThread;
static std::atomic<bool> stop_gpu_stress(false), g_stressThreadRunning(false);

// --- Helper Functions ---

static uint32_t findMemoryType(VkPhysicalDevice dev, uint32_t mask, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(dev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

static bool createBuffer(SharedVulkanContext* s, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer &buf, VkDeviceMemory &mem) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(s->device, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(s->device, buf, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    uint32_t mt = findMemoryType(s->physicalDevice, mr.memoryTypeBits, props);
    if (mt == UINT32_MAX) return false;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(s->device, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    return vkBindBufferMemory(s->device, buf, mem, 0) == VK_SUCCESS;
}

static bool initShared(SharedVulkanContext &ctx) {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "MaterialBench"; ai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &ai;
    if (vkCreateInstance(&ii, nullptr, &ctx.instance) != VK_SUCCESS) return false;
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &devCount, nullptr);
    if (devCount == 0) return false;
    std::vector<VkPhysicalDevice> devs(devCount);
    vkEnumeratePhysicalDevices(ctx.instance, &devCount, devs.data());
    ctx.physicalDevice = devs[0];
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &qCount, qfs.data());
    for (uint32_t i = 0; i < qCount; ++i) if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { ctx.computeQueueFamilyIndex = i; break; }
    if (ctx.computeQueueFamilyIndex == UINT32_MAX) return false;
    float qp = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = ctx.computeQueueFamilyIndex; qci.queueCount = 1; qci.pQueuePriorities = &qp;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qci;
    if (vkCreateDevice(ctx.physicalDevice, &di, nullptr, &ctx.device) != VK_SUCCESS) return false;
    vkGetDeviceQueue(ctx.device, ctx.computeQueueFamilyIndex, 0, &ctx.computeQueue);
    return true;
}

static void cleanupSharedVulkanContext(SharedVulkanContext &ctx) {
    if (ctx.device) {
        vkDeviceWaitIdle(ctx.device);
        vkDestroyDevice(ctx.device, nullptr);
    }
    if (ctx.instance) {
        vkDestroyInstance(ctx.instance, nullptr);
    }
}

static SharedVulkanContext* getSharedContext() {
    if (!g_sharedContext) {
        auto ctx = std::make_unique<SharedVulkanContext>();
        if (!initShared(*ctx)) { cleanupSharedVulkanContext(*ctx); return nullptr; }
        g_sharedContext = std::move(ctx);
    }
    return g_sharedContext.get();
}

static bool createComputePipeline(VkDevice dev, const uint32_t* code, size_t codeSize, uint32_t descriptorCount, uint32_t pushConstantSize,
                                  VkShaderModule &outModule, VkDescriptorSetLayout &outDSL, VkPipelineLayout &outPL, VkPipeline &outPipeline,
                                  uint32_t specWorkgroup = 0) {
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = codeSize; smci.pCode = code;
    if (vkCreateShaderModule(dev, &smci, nullptr, &outModule) != VK_SUCCESS) return false;
    std::vector<VkDescriptorSetLayoutBinding> binds(descriptorCount);
    for (uint32_t i = 0; i < descriptorCount; ++i) {
        binds[i].binding = i;
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsli.bindingCount = descriptorCount; dsli.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(dev, &dsli, nullptr, &outDSL) != VK_SUCCESS) return false;
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &outDSL; pli.pushConstantRangeCount = pushConstantSize ? 1 : 0; pli.pPushConstantRanges = pushConstantSize ? &pcr : nullptr;
    if (vkCreatePipelineLayout(dev, &pli, nullptr, &outPL) != VK_SUCCESS) return false;
    VkSpecializationInfo speci{}; VkSpecializationMapEntry sme{}; uint32_t specData = specWorkgroup;
    if (specWorkgroup) { sme.constantID = 0; sme.offset = 0; sme.size = sizeof(uint32_t); speci.mapEntryCount = 1; speci.pMapEntries = &sme; speci.dataSize = sizeof(uint32_t); speci.pData = &specData; }
    VkPipelineShaderStageCreateInfo pss{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pss.stage = VK_SHADER_STAGE_COMPUTE_BIT; pss.module = outModule; pss.pName = "main"; pss.pSpecializationInfo = specWorkgroup ? &speci : nullptr;
    VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pci.stage = pss; pci.layout = outPL;
    return vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &outPipeline) == VK_SUCCESS;
}

static bool createGEMMPipeline(GEMMContext &ctx) {
    return createComputePipeline(ctx.shared->device,
                                 reinterpret_cast<const uint32_t*>(gemm_shader_tiled_comp_spv), gemm_shader_tiled_comp_spv_len,
                                 3, sizeof(uint32_t) * 5,
                                 ctx.shaderModule, ctx.descriptorSetLayout, ctx.pipelineLayout, ctx.pipeline);
}

static bool createGEMMBuffersAndDescriptors(GEMMContext &ctx, uint32_t N, uint32_t M, uint32_t K) {
    ctx.N = N; ctx.M = M; ctx.K = K;
    VkDeviceSize sizeA = size_t(N) * size_t(K) * sizeof(float);
    VkDeviceSize sizeB = size_t(K) * size_t(M) * sizeof(float);
    VkDeviceSize sizeC = size_t(N) * size_t(M) * sizeof(float);
    if (!createBuffer(ctx.shared, sizeA, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ctx.bufA, ctx.memA)) return false;
    if (!createBuffer(ctx.shared, sizeB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ctx.bufB, ctx.memB)) return false;
    if (!createBuffer(ctx.shared, sizeC, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ctx.bufC, ctx.memC)) return false;
    std::default_random_engine rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float* p = nullptr;
    if (vkMapMemory(ctx.shared->device, ctx.memA, 0, sizeA, 0, (void**)&p) != VK_SUCCESS) return false;
    for (size_t i = 0; i < size_t(N) * K; ++i) p[i] = dist(rng);
    vkUnmapMemory(ctx.shared->device, ctx.memA);
    if (vkMapMemory(ctx.shared->device, ctx.memB, 0, sizeB, 0, (void**)&p) != VK_SUCCESS) return false;
    for (size_t i = 0; i < size_t(K) * M; ++i) p[i] = dist(rng);
    vkUnmapMemory(ctx.shared->device, ctx.memB);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize; dpci.maxSets = 1;
    if (vkCreateDescriptorPool(ctx.shared->device, &dpci, nullptr, &ctx.descriptorPool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo asi{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    asi.descriptorPool = ctx.descriptorPool; asi.descriptorSetCount = 1; asi.pSetLayouts = &ctx.descriptorSetLayout;
    if (vkAllocateDescriptorSets(ctx.shared->device, &asi, &ctx.descriptorSet) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo infos[3] = {{ctx.bufA, 0, sizeA}, {ctx.bufB, 0, sizeB}, {ctx.bufC, 0, sizeC}};
    VkWriteDescriptorSet wds[3]{};
    for (int i = 0; i < 3; ++i) {
        wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[i].dstSet = ctx.descriptorSet;
        wds[i].dstBinding = i;
        wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].descriptorCount = 1;
        wds[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(ctx.shared->device, 3, wds, 0, nullptr);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = ctx.shared->computeQueueFamilyIndex;
    // CRITICAL: Ensure we can release resources when resetting. This fixes the NULL pointer crashes in Mali drivers during stress tests.
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx.shared->device, &pci, nullptr, &ctx.commandPool) != VK_SUCCESS) return false;
    return true;
}

static jlong runGEMMCompute(GEMMContext &ctx, uint32_t N_param, uint32_t M_param, uint32_t K_param, JNIEnv* env, jobject activity_global_ref, jmethodID update_progress_method_id) {
    const uint32_t CHUNK_WG_X = 32;
    const uint32_t CHUNK_WG_Y = 32;
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = ctx.commandPool; allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(ctx.shared->device, &allocInfo, &cmd) != VK_SUCCESS) return -1;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; if (vkCreateFence(ctx.shared->device, &fci, nullptr, &fence) != VK_SUCCESS) { vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd); return -1; }

    auto t0 = std::chrono::high_resolution_clock::now();
    uint32_t totalBatches = 0;
    for (uint32_t by = 0; by < ctx.workgroupCountY; by += CHUNK_WG_Y) for (uint32_t bx = 0; bx < ctx.workgroupCountX; bx += CHUNK_WG_X) ++totalBatches;
    uint32_t batchIndex = 0;

    for (uint32_t by = 0; by < ctx.workgroupCountY; by += CHUNK_WG_Y) {
        for (uint32_t bx = 0; bx < ctx.workgroupCountX; bx += CHUNK_WG_X) {
            uint32_t dispatchX = std::min(CHUNK_WG_X, ctx.workgroupCountX - bx);
            uint32_t dispatchY = std::min(CHUNK_WG_Y, ctx.workgroupCountY - by);

            // Fix 1: Use RELEASE_RESOURCES_BIT to completely free driver internal state
            vkResetCommandBuffer(cmd, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

            VkCommandBufferBeginInfo bbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(cmd, &bbi) != VK_SUCCESS) { vkDestroyFence(ctx.shared->device, fence, nullptr); vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd); return -1; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipelineLayout, 0, 1, &ctx.descriptorSet, 0, nullptr);
            struct PC { uint32_t N, M, K, baseX, baseY; };
            PC pc{N_param, M_param, K_param, bx, by};
            vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, dispatchX, dispatchY, 1);

            // Fix 2: Add explicit memory barrier to ensure GPU write completion visibility
            VkMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);

            if (vkEndCommandBuffer(cmd) != VK_SUCCESS) { vkDestroyFence(ctx.shared->device, fence, nullptr); vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd); return -1; }
            vkResetFences(ctx.shared->device, 1, &fence);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
            if (vkQueueSubmit(ctx.shared->computeQueue, 1, &si, fence) != VK_SUCCESS) { vkDestroyFence(ctx.shared->device, fence, nullptr); vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd); return -1; }
            if (vkWaitForFences(ctx.shared->device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) { vkDestroyFence(ctx.shared->device, fence, nullptr); vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd); return -1; }

            // Fix 3: Ensure queue is idle to prevent command buffer reuse race in driver
            vkQueueWaitIdle(ctx.shared->computeQueue);

            ++batchIndex;
            if (activity_global_ref && update_progress_method_id) {
                float progress = float(batchIndex) / float(totalBatches);
                env->CallVoidMethod(activity_global_ref, update_progress_method_id, progress);
                if (env->ExceptionCheck()) { env->ExceptionClear(); LOGE("Exception during GEMM progress callback"); }
            }
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    float* cData = nullptr;
    VkDeviceSize cSize = size_t(N_param) * size_t(M_param) * sizeof(float);
    if (vkMapMemory(ctx.shared->device, ctx.memC, 0, cSize, 0, (void**)&cData) == VK_SUCCESS) {
        LOGI("=== GEMM (first 5x5) ===");
        for (uint32_t i = 0; i < std::min<uint32_t>(5, N_param); ++i) {
            std::string row;
            for (uint32_t j = 0; j < std::min<uint32_t>(5, M_param); ++j) row += std::to_string(cData[i * M_param + j]) + " ";
            LOGI("%s", row.c_str());
        }
        vkUnmapMemory(ctx.shared->device, ctx.memC);
    }
    vkDestroyFence(ctx.shared->device, fence, nullptr);
    vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd);
    return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

static void cleanupGEMM(GEMMContext &ctx) {
    if (!ctx.shared || !ctx.shared->device) return;
    VkDevice d = ctx.shared->device;
    vkDeviceWaitIdle(d);
    if (ctx.bufA) vkDestroyBuffer(d, ctx.bufA, nullptr);
    if (ctx.memA) vkFreeMemory(d, ctx.memA, nullptr);
    if (ctx.bufB) vkDestroyBuffer(d, ctx.bufB, nullptr);
    if (ctx.memB) vkFreeMemory(d, ctx.memB, nullptr);
    if (ctx.bufC) vkDestroyBuffer(d, ctx.bufC, nullptr);
    if (ctx.memC) vkFreeMemory(d, ctx.memC, nullptr);
    if (ctx.descriptorPool) vkDestroyDescriptorPool(d, ctx.descriptorPool, nullptr);
    if (ctx.pipeline) vkDestroyPipeline(d, ctx.pipeline, nullptr);
    if (ctx.pipelineLayout) vkDestroyPipelineLayout(d, ctx.pipelineLayout, nullptr);
    if (ctx.descriptorSetLayout) vkDestroyDescriptorSetLayout(d, ctx.descriptorSetLayout, nullptr);
    if (ctx.shaderModule) vkDestroyShaderModule(d, ctx.shaderModule, nullptr);
    if (ctx.commandPool) vkDestroyCommandPool(d, ctx.commandPool, nullptr);
}

static void gpu_stress_task() {
    g_stressThreadRunning.store(true, std::memory_order_relaxed);
    GEMMContext ctx;
    {
        std::lock_guard<std::mutex> lock(g_initMutex);
        SharedVulkanContext* shared = getSharedContext();
        if (!shared) { g_stressThreadRunning.store(false, std::memory_order_relaxed); return; }
        ctx.shared = shared;
        uint32_t N = 512, M = 512, K = 384;
        const uint32_t TILE = 16;
        ctx.workgroupCountX = (M + TILE - 1) / TILE;
        ctx.workgroupCountY = (N + TILE - 1) / TILE;
        if (!createGEMMPipeline(ctx) || !createGEMMBuffersAndDescriptors(ctx, N, M, K)) { cleanupGEMM(ctx); g_stressThreadRunning.store(false, std::memory_order_relaxed); return; }
    }
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = ctx.commandPool; allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(ctx.shared->device, &allocInfo, &cmd) != VK_SUCCESS) { cleanupGEMM(ctx); g_stressThreadRunning.store(false, std::memory_order_relaxed); return; }
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; if (vkCreateFence(ctx.shared->device, &fci, nullptr, &fence) != VK_SUCCESS) { vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd); cleanupGEMM(ctx); g_stressThreadRunning.store(false, std::memory_order_relaxed); return; }
    const uint32_t CHUNK_WG_X = 16, CHUNK_WG_Y = 16;
    while (!stop_gpu_stress.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(g_initMutex);
        if (!g_sharedContext) break;
        for (uint32_t by = 0; by < ctx.workgroupCountY && !stop_gpu_stress.load(std::memory_order_relaxed); by += CHUNK_WG_Y) {
            for (uint32_t bx = 0; bx < ctx.workgroupCountX && !stop_gpu_stress.load(std::memory_order_relaxed); bx += CHUNK_WG_X) {
                uint32_t dx = std::min(CHUNK_WG_X, ctx.workgroupCountX - bx);
                uint32_t dy = std::min(CHUNK_WG_Y, ctx.workgroupCountY - by);

                // Fix 1: Full resource release on reset
                vkResetCommandBuffer(cmd, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

                VkCommandBufferBeginInfo bbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                if (vkBeginCommandBuffer(cmd, &bbi) != VK_SUCCESS) goto stress_end;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipelineLayout, 0, 1, &ctx.descriptorSet, 0, nullptr);
                struct PC { uint32_t N, M, K, baseX, baseY; } pc = { ctx.N, ctx.M, ctx.K, bx, by };
                vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, dx, dy, 1);

                // Fix 2: Explicit barrier
                VkMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);

                if (vkEndCommandBuffer(cmd) != VK_SUCCESS) goto stress_end;
                vkResetFences(ctx.shared->device, 1, &fence);
                VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
                if (vkQueueSubmit(ctx.shared->computeQueue, 1, &si, fence) != VK_SUCCESS) goto stress_end;
                if (vkWaitForFences(ctx.shared->device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) goto stress_end;

                // Fix 3: Strict idle wait
                vkQueueWaitIdle(ctx.shared->computeQueue);
            }
        }
    }
    stress_end:
    vkDestroyFence(ctx.shared->device, fence, nullptr);
    vkFreeCommandBuffers(ctx.shared->device, ctx.commandPool, 1, &cmd);
    cleanupGEMM(ctx);
    g_stressThreadRunning.store(false, std::memory_order_relaxed);
}

extern "C" {

JNIEXPORT void JNICALL Java_com_komarudude_materialbench_ui_MainActivity_nativeStopGpuStress(JNIEnv *env, jobject thiz) {
    if (!g_stressThreadRunning.load(std::memory_order_relaxed)) return;
    stop_gpu_stress.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_stressMutex);
    if (g_stressThread.joinable()) { g_stressThread.join(); LOGI("GPU stress stopped"); }
}

JNIEXPORT void JNICALL Java_com_komarudude_materialbench_ui_MainActivity_nativeStartGpuStress(JNIEnv *env, jobject thiz) {
    if (g_stressThreadRunning.load(std::memory_order_relaxed)) return;
    stop_gpu_stress.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_stressMutex);
    if (g_stressThread.joinable()) g_stressThread.join();
    g_stressThread = std::thread(gpu_stress_task);
}

JNIEXPORT void JNICALL Java_com_komarudude_materialbench_ui_MainActivity_nativeCleanup(JNIEnv *env, jobject thiz) {
    Java_com_komarudude_materialbench_ui_MainActivity_nativeStopGpuStress(env, thiz);
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_sharedContext) { cleanupSharedVulkanContext(*g_sharedContext); g_sharedContext.reset(); }
}

JNIEXPORT void JNICALL Java_com_komarudude_materialbench_ui_BenchActivity_nativeBenchCleanup(JNIEnv *env, jobject thiz) {
    Java_com_komarudude_materialbench_ui_MainActivity_nativeStopGpuStress(env, thiz);
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_sharedContext) { cleanupSharedVulkanContext(*g_sharedContext); g_sharedContext.reset(); }
}

JNIEXPORT jlong JNICALL Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunVulkanGEMMBenchmark(JNIEnv *env, jobject thiz, jobject activity_param) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    SharedVulkanContext* shared = getSharedContext();
    if (!shared) { LOGE("No shared context"); return -1; }
    jobject activity_global_ref = env->NewGlobalRef(activity_param);
    jclass activity_class = env->GetObjectClass(activity_global_ref);
    jmethodID update_progress_method_id = nullptr;
    if (activity_class) update_progress_method_id = env->GetMethodID(activity_class, "updateBenchmarkProgress", "(F)V");
    GEMMContext ctx;
    ctx.shared = shared;
    uint32_t N = 8192, M = 8192, K = 5120;
    const uint32_t TILE_DIM = 16;
    ctx.workgroupCountX = (M + TILE_DIM - 1) / TILE_DIM;
    ctx.workgroupCountY = (N + TILE_DIM - 1) / TILE_DIM;
    if (!createGEMMPipeline(ctx) || !createGEMMBuffersAndDescriptors(ctx, N, M, K)) {
        cleanupGEMM(ctx);
        env->DeleteGlobalRef(activity_global_ref);
        return -1;
    }
    jlong duration = runGEMMCompute(ctx, N, M, K, env, activity_global_ref, update_progress_method_id);
    cleanupGEMM(ctx);
    env->DeleteGlobalRef(activity_global_ref);
    return duration;
}

}