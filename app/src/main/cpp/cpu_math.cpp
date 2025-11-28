#include <jni.h>
#include <thread>
#include <vector>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <sched.h>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include "utils.h"

std::atomic<long long> current_iterations_done(0);
std::atomic<bool> stop_cpu_stress(false);

inline double heavy_math(double i) {
    const double PI = 3.14159265358979323846;

    double arg = i + 1.0;
    double s = std::sin(i);
    double c = std::cos(i);
    double t = std::tan(arg);
    double l = std::log(arg);
    double r = std::sqrt(arg);
    double p = std::pow(s + c, PI);
    double f = std::fmod(l * r, p + PI);
    double a = std::hypot((i + arg / 2.0), PI);
    double floor_r = std::floor(r);
    double ceil_l = std::ceil(l);
    double result_a = (s * c / t) + l + r;
    double result_b = (p * f + a) / (floor_r + ceil_l);
    double result_c = (s * r) - (c * l);
    double result_final = std::log10(result_a + result_b + result_c);

    return result_final;
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunCpuMathSingleCoreBenchmark(
        JNIEnv *env, jobject thiz, jobject activity) {
    (void)thiz;
    if (activity == nullptr) return 0; // Added null check
    jobject activity_global_ref = env->NewGlobalRef(activity);
    jclass activityClass_local = env->GetObjectClass(activity_global_ref);
    auto activity_class_global_ref = (jclass)env->NewGlobalRef(activityClass_local);
    env->DeleteLocalRef(activityClass_local); // Added this line
    jmethodID updateProgressMethod = env->GetMethodID(activity_class_global_ref, "updateBenchmarkProgress", "(F)V");

    const long long total_iterations = 70000000LL;
    current_iterations_done.store(0, std::memory_order_relaxed);

    std::thread reporter_thread([activity_global_ref, activity_class_global_ref, updateProgressMethod, total_iterations]() {
        JNIEnv* thread_env = nullptr;
        g_vm->AttachCurrentThread(&thread_env, nullptr);

        if (setpriority(PRIO_PROCESS, 0, 0) != 0) {
            LOGE("Failed to set thread priority");
        }

        const std::chrono::milliseconds update_interval(50);

        while (current_iterations_done.load(std::memory_order_relaxed) < total_iterations) {
            long long done = current_iterations_done.load(std::memory_order_relaxed);
            auto progress = static_cast<float>(static_cast<long double>(done) / total_iterations);

            if (thread_env && activity_global_ref && updateProgressMethod) {
                thread_env->CallVoidMethod(activity_global_ref, updateProgressMethod, progress);
            }

            std::this_thread::sleep_for(update_interval);
        }

        if (thread_env && activity_global_ref && updateProgressMethod) {
            thread_env->CallVoidMethod(activity_global_ref, updateProgressMethod, 1.0f);
        }

        g_vm->DetachCurrentThread();
    });

    auto start = std::chrono::high_resolution_clock::now();

    if (setpriority(PRIO_PROCESS, 0, -10) != 0) {
        LOGE("Failed to set thread priority");
    }

    volatile double result = 0;
    for (auto i = 0; i < total_iterations; i++) {
        result += heavy_math(static_cast<double>(i));

        current_iterations_done.fetch_add(1, std::memory_order_relaxed);
    }

    reporter_thread.join();

    env->DeleteGlobalRef(activity_global_ref);
    env->DeleteGlobalRef(activity_class_global_ref);

    auto end = std::chrono::high_resolution_clock::now();
    (void)result;
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

jlong run_multicore_benchmark(
        JNIEnv *env, jobject activity, long long total_iterations) {

    if (total_iterations <= 0) return 0;
    if (activity == nullptr) return 0;

    unsigned int num_cores = std::max(1u, std::thread::hardware_concurrency());
    const long long num_tasks = 100LL;
    const long long task_size = total_iterations / num_tasks;
    const long long remainder = total_iterations % num_tasks;

    std::atomic<int> next_task{0};
    std::atomic<long long> completed_iterations{0};

    jobject activity_global_ref = env->NewGlobalRef(activity);
    jclass activity_class = env->GetObjectClass(activity_global_ref);
    auto activity_class_global_ref = (jclass)env->NewGlobalRef(activity_class);
    env->DeleteLocalRef(activity_class); // Added this line
    jmethodID update_progress_method_id = env->GetMethodID(activity_class_global_ref, "updateBenchmarkProgress", "(F)V");

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_cores);

    for (auto t = 0; t < num_cores; ++t) {
        threads.emplace_back([&, activity_global_ref, activity_class_global_ref, update_progress_method_id, t]() {
            JNIEnv* thread_env = nullptr;
            g_vm->AttachCurrentThread(&thread_env, nullptr);

            if (setpriority(PRIO_PROCESS, 0, -10) != 0) {
                LOGE("Failed to set thread priority");
            }

            while (true) {
                long long task_index = next_task.fetch_add(1, std::memory_order_relaxed);
                if (task_index >= num_tasks) break;

                long long task_start = task_index * task_size + std::min(task_index, remainder);
                long long task_end = task_start + task_size + (task_index < remainder ? 1 : 0);

                volatile double result_thread = 0;
                for (auto i = task_start; i < task_end; ++i) {
                    result_thread += heavy_math(static_cast<double>(i));
                }

                long long completed = completed_iterations.fetch_add(task_end - task_start,
                                                                     std::memory_order_relaxed) + (task_end - task_start);
                auto progress = static_cast<float>(static_cast<long double>(completed) / total_iterations);

                if (thread_env && activity_global_ref && update_progress_method_id) {
                    thread_env->CallVoidMethod(activity_global_ref, update_progress_method_id, progress);
                }
            }

            g_vm->DetachCurrentThread();
        });
    }

    for (auto &th : threads) th.join();

    if (env && activity_global_ref && update_progress_method_id) {
        env->CallVoidMethod(activity_global_ref, update_progress_method_id, 1.0f);
    }
    env->DeleteGlobalRef(activity_global_ref);
    env->DeleteGlobalRef(activity_class_global_ref);

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunCpuMathMultiCoreBenchmark(
        JNIEnv *env, jobject thiz, jobject activity) {
    (void)thiz;
    const long long DEFAULT_ITERATIONS = 70000000LL;
    return run_multicore_benchmark(env, activity, DEFAULT_ITERATIONS);
}

void cpu_stress_task() {
    volatile long double result = 0;
    while (!stop_cpu_stress.load(std::memory_order_relaxed)) {
        for (auto i = 0; i < 100000; ++i) {
            result += heavy_math(static_cast<double>(i));
            if (stop_cpu_stress.load(std::memory_order_relaxed)) return;
        }
    }
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_ui_MainActivity_nativeStartCpuStress(
        JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    stop_cpu_stress.store(false, std::memory_order_relaxed);
    unsigned int num_cores = std::max(1u, std::thread::hardware_concurrency());
    for (auto t = 0; t < num_cores; ++t) {
        std::thread(cpu_stress_task).detach();
    }
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_ui_MainActivity_nativeStopCpuStress(
        JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    stop_cpu_stress.store(true, std::memory_order_relaxed);
}

}
