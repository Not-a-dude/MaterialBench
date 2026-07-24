#include "compute_service.h"

#include "../utils.h"

#include <exception>
#include <utility>

namespace materialbench::vulkan {
namespace {

// Both modes use the same large, register-tiled FP32 GEMM. One multiplication
// performs roughly 137 GFLOP while using about 192 MiB across A, B, and C.
constexpr GemmConfig kBenchmarkConfig{
    4096,  // rows
    4096,  // columns
    4096,  // depth
    0x4d42564b,
};

constexpr GemmConfig kStressConfig{
    512,  // rows
    512,  // columns
    512,  // depth
    0x53545253,
};

}  // namespace

ComputeService& ComputeService::instance() {
    static ComputeService service;
    return service;
}

ComputeService::~ComputeService() {
    cleanup();
}

VulkanContext* ComputeService::ensureContext() {
    // Create the context only on first use. The caller must hold gpuMutex_ to
    // prevent duplicate initialization or cleanup while the context is in use.
    if (!context_) context_ = VulkanContext::create();
    return context_.get();
}

int64_t ComputeService::runBenchmark(const GemmExecutor::ProgressCallback& progress) {
    // try_lock avoids blocking the UI when the GPU is already busy. Checking the
    // running flag first provides a clearer error when stress mode is active.
    if (stressRunning_.load(std::memory_order_acquire)) {
        LOGE("Cannot start Vulkan benchmark while GPU stress is running");
        return -1;
    }

    std::unique_lock<std::mutex> gpuLock(gpuMutex_, std::try_to_lock);
    if (!gpuLock.owns_lock()) {
        LOGE("Vulkan compute device is busy");
        return -1;
    }

    VulkanContext* context = ensureContext();
    if (context == nullptr) return -1;

    auto executor = GemmExecutor::create(*context, kBenchmarkConfig);
    if (!executor) return -1;

    return executor->run(progress, {}, true);
}

void ComputeService::startStress() {
    // A completed std::thread remains joinable. Move it out of the field while
    // holding the mutex, then perform the potentially slow join without the lock.
    std::thread finishedThread;
    {
        std::lock_guard<std::mutex> lock(stressMutex_);
        if (stressRunning_.load(std::memory_order_acquire)) return;
        if (stressThread_.joinable()) finishedThread = std::move(stressThread_);
    }
    if (finishedThread.joinable()) finishedThread.join();

    std::lock_guard<std::mutex> lock(stressMutex_);
    if (stressRunning_.load(std::memory_order_acquire)) return;

    stopRequested_.store(false, std::memory_order_release);
    stressRunning_.store(true, std::memory_order_release);
    try {
        stressThread_ = std::thread(&ComputeService::stressMain, this);
    } catch (const std::exception& error) {
        stressRunning_.store(false, std::memory_order_release);
        stopRequested_.store(true, std::memory_order_release);
        LOGE("Failed to start GPU stress thread: %s", error.what());
    } catch (...) {
        stressRunning_.store(false, std::memory_order_release);
        stopRequested_.store(true, std::memory_order_release);
        LOGE("Failed to start GPU stress thread");
    }
}

void ComputeService::stopStress() {
    // Cancellation is cooperative: the executor observes the flag between
    // dispatch chunks. After join, the worker can no longer access VulkanContext.
    stopRequested_.store(true, std::memory_order_release);

    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock(stressMutex_);
        if (stressThread_.joinable()) threadToJoin = std::move(stressThread_);
    }
    if (threadToJoin.joinable()) threadToJoin.join();
    stressRunning_.store(false, std::memory_order_release);
}

void ComputeService::stressMain() {
    LOGI("GPU stress started");
    // Hold gpuMutex_ for the entire stress session to prevent benchmark or cleanup
    // operations from running between consecutive GEMM iterations.
    {
        std::unique_lock<std::mutex> gpuLock(gpuMutex_);
        if (!stopRequested_.load(std::memory_order_acquire)) {
            VulkanContext* context = ensureContext();
            if (context != nullptr) {
                auto executor = GemmExecutor::create(*context, kStressConfig);
                if (executor) {
                    const auto shouldStop = [this] {
                        return stopRequested_.load(std::memory_order_acquire);
                    };
                    // Create the pipeline, descriptors, and buffers once and reuse
                    // them; each iteration only repeats command dispatch.
                    while (!shouldStop()) {
                        if (executor->run({}, shouldStop, false) < 0) break;
                    }
                }
            }
        }
    }
    stressRunning_.store(false, std::memory_order_release);
    LOGI("GPU stress stopped");
}

void ComputeService::cleanup() {
    // Stop the worker first, then destroy the context while holding the GPU mutex
    // to prevent stressMain() from using a freed logical device.
    stopStress();
    std::lock_guard<std::mutex> gpuLock(gpuMutex_);
    context_.reset();
}

}  // namespace materialbench::vulkan
