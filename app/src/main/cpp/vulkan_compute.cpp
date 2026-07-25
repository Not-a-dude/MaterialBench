#include <jni.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cmath>
#include <limits>

#include "vulkan/compute_service.h"
#include "utils.h"

extern "C" {

static jobject gpu_stress_callback_global = nullptr;
static jclass gpu_integer_class_global = nullptr;
static jmethodID gpu_integer_value_of_mid = nullptr;
static jmethodID gpu_on_stress_data_mid = nullptr;
static std::thread gpu_stress_reporter_thread;
static std::mutex gpu_stress_reporter_mutex;
static std::atomic<bool> stop_gpu_reporter{false};

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunVulkanGEMMBenchmark(
    JNIEnv* env, jobject /* thiz */, jobject callback) {
    jclass callbackClass = env->GetObjectClass(callback);
    jmethodID updateProgressMethod = env->GetMethodID(
        callbackClass, "onProgressUpdate", "(F)V");
    env->DeleteLocalRef(callbackClass);

    return static_cast<jlong>(
        materialbench::vulkan::ComputeService::instance().runBenchmark(
            [env, callback, updateProgressMethod](float progress) {
                env->CallVoidMethod(callback, updateProgressMethod, progress);
                if (env->ExceptionCheck()) env->ExceptionClear();
            }));
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeStartGpuStress(
    JNIEnv* env, jobject /* thiz */, jobject callback) {
    std::lock_guard<std::mutex> lock(gpu_stress_reporter_mutex);

    if (gpu_stress_callback_global != nullptr) return;

    if (callback) {
        gpu_stress_callback_global = env->NewGlobalRef(callback);
        jclass clz = env->GetObjectClass(gpu_stress_callback_global);
        gpu_on_stress_data_mid = env->GetMethodID(clz, "onStressData", "(JLjava/lang/Integer;Ljava/lang/Integer;)V");
        env->DeleteLocalRef(clz);

        jclass int_clz = env->FindClass("java/lang/Integer");
        gpu_integer_class_global = (jclass)env->NewGlobalRef(int_clz);
        gpu_integer_value_of_mid = env->GetStaticMethodID(gpu_integer_class_global, "valueOf", "(I)Ljava/lang/Integer;");
        env->DeleteLocalRef(int_clz);
    }

    materialbench::vulkan::ComputeService::instance().startStress();

    if (gpu_stress_callback_global) {
        stop_gpu_reporter.store(false, std::memory_order_relaxed);
        gpu_stress_reporter_thread = std::thread([]() {
            JNIEnv* thread_env = nullptr;
            if (g_vm->AttachCurrentThread(&thread_env, nullptr) != JNI_OK) return;

            auto& service = materialbench::vulkan::ComputeService::instance();
            auto last_sample = service.getStressPerformance();
            auto next_sample_time = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(1);

            while (!stop_gpu_reporter.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_until(next_sample_time);
                if (stop_gpu_reporter.load(std::memory_order_relaxed)) break;

                const auto sample_time = std::chrono::steady_clock::now();
                const auto current = service.getStressPerformance();
                const uint64_t delta_flops = current.completedFlops - last_sample.completedFlops;
                const uint64_t delta_nanoseconds =
                        current.elapsedNanoseconds - last_sample.elapsedNanoseconds;
                last_sample = current;

                if (delta_flops > 0 && delta_nanoseconds > 0) {
                    // Standard GEMM throughput: FMA counts as two floating-point
                    // operations. Reporting MFLOP/s keeps the value integral without
                    // introducing an arbitrary score multiplier.
                    long double mflops_value =
                            static_cast<long double>(delta_flops) * 1000.0L /
                            static_cast<long double>(delta_nanoseconds);
                    const auto max_jint =
                            static_cast<long double>(std::numeric_limits<jint>::max());
                    if (mflops_value > max_jint) mflops_value = max_jint;
                    const jint gpu_mflops = static_cast<jint>(std::llround(mflops_value));

                    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                    jobject gpu_perf_obj = thread_env->CallStaticObjectMethod(
                            gpu_integer_class_global, gpu_integer_value_of_mid, gpu_mflops);
                    thread_env->CallVoidMethod(
                            gpu_stress_callback_global, gpu_on_stress_data_mid,
                            static_cast<jlong>(now), nullptr, gpu_perf_obj);
                    thread_env->DeleteLocalRef(gpu_perf_obj);
                }

                next_sample_time += std::chrono::seconds(1);
                if (next_sample_time <= sample_time) {
                    next_sample_time = sample_time + std::chrono::seconds(1);
                }
            }

            g_vm->DetachCurrentThread();
        });
    }
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeStopGpuStress(
    JNIEnv* env, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().stopStress();

    std::lock_guard<std::mutex> lock(gpu_stress_reporter_mutex);
    stop_gpu_reporter.store(true, std::memory_order_relaxed);

    if (gpu_stress_reporter_thread.joinable()) {
        gpu_stress_reporter_thread.join();
    }

    if (gpu_stress_callback_global) {
        env->DeleteGlobalRef(gpu_stress_callback_global);
        gpu_stress_callback_global = nullptr;
    }
    if (gpu_integer_class_global) {
        env->DeleteGlobalRef(gpu_integer_class_global);
        gpu_integer_class_global = nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeCleanup(
    JNIEnv* /* env */, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().cleanup();
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeBenchCleanup(
    JNIEnv* /* env */, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().cleanup();
}

}  // extern "C"
