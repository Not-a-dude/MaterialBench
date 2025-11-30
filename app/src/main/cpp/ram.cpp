#include <jni.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sys/syscall.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include "utils.h"

// Function to prevent optimization
template <class T>
__attribute__((always_inline)) inline void do_not_optimize(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRamSequentialWriteBenchmark(
        JNIEnv* env, jobject thiz, jobject activity) {

    const size_t buffer_size = 768 * 1024 * 1024; // 768 MB
    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    volatile char* buffer = new (std::nothrow) char[buffer_size];
    if (!buffer) return -1;

    const int total_progress_updates = 100;
    size_t progress_step = buffer_size / total_progress_updates;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < buffer_size; i++) {
        buffer[i] = static_cast<char>(i & 0xFF);
        do_not_optimize(buffer[i]);

        if (i % progress_step == 0) {
            float progress = static_cast<float>(i) / static_cast<float>(buffer_size);
            update_progress(env, activity, updateProgressMethod, progress);
        }
    }

    asm volatile("" : : : "memory");
    std::atomic_thread_fence(std::memory_order_seq_cst);

    update_progress(env, activity, updateProgressMethod, 1.0f);
    auto end = std::chrono::high_resolution_clock::now();

    delete[] buffer;
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRamSequentialReadBenchmark(
        JNIEnv* env, jobject thiz, jobject activity) {

    const size_t buffer_size = 768 * 1024 * 1024; // 768 MB
    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    char* non_volatile_buffer = new (std::nothrow) char[buffer_size];
    if (!non_volatile_buffer) return -1;

    for (size_t i = 0; i < buffer_size; i++) {
        non_volatile_buffer[i] = static_cast<char>(i & 0xFF);
    }

    volatile char* buffer = static_cast<volatile char*>(non_volatile_buffer);
    volatile char sum = 0;

    const int total_progress_updates = 100;
    size_t progress_step = buffer_size / total_progress_updates;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < buffer_size; i++) {
        char value = buffer[i];
        sum += value;
        do_not_optimize(value);

        if (i % progress_step == 0) {
            float progress = static_cast<float>(i) / static_cast<float>(buffer_size);
            update_progress(env, activity, updateProgressMethod, progress);
        }
    }

    asm volatile("" : : : "memory");
    std::atomic_thread_fence(std::memory_order_seq_cst);
    do_not_optimize(sum);

    update_progress(env, activity, updateProgressMethod, 1.0f);
    auto end = std::chrono::high_resolution_clock::now();

    delete[] non_volatile_buffer;
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

}