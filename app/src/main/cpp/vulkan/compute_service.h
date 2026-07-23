#pragma once

#include "gemm_executor.h"
#include "vulkan_context.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace materialbench::vulkan {

// Thread-safe facade over the Vulkan compute subsystem. Lazily creates one
// VulkanContext and prevents concurrent benchmark/stress access to the queue.
class ComputeService final {
public:
    static ComputeService& instance();
    ~ComputeService();

    ComputeService(const ComputeService&) = delete;
    ComputeService& operator=(const ComputeService&) = delete;

    // Executes one large GEMM with result validation and progress reporting.
    int64_t runBenchmark(const GemmExecutor::ProgressCallback& progress);

    // Controls the background GEMM loop. Repeated start/stop calls are safe.
    void startStress();
    void stopStress();
    void cleanup();

private:
    ComputeService() = default;

    VulkanContext* ensureContext();
    void stressMain();

    // Serializes queue operations and destruction of context_. Vulkan requires
    // external synchronization for concurrent access to the same queue.
    std::mutex gpuMutex_;
    std::unique_ptr<VulkanContext> context_;

    // Protects ownership of std::thread. Atomic flags let the worker check for a
    // stop request without holding the mutex.
    std::mutex stressMutex_;
    std::thread stressThread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> stressRunning_{false};
};

}  // namespace materialbench::vulkan
