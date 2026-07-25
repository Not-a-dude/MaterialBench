#include <jni.h>
#include <thread>
#include <vector>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <memory>
#include <cstdint>
#include <sched.h>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include "utils.h"

std::atomic<long long> current_iterations_done(0);
std::atomic<bool> stop_cpu_stress(true);

struct alignas(64) CpuStressWorkerStats {
    std::atomic<uint64_t> completed_rounds{0};
    std::atomic<uint64_t> checksum{0};
    int core_id = -1;
};

static constexpr uint64_t CPU_STRESS_ROUNDS_PER_BATCH = 256;
static constexpr double CPU_STRESS_ROUNDS_PER_POINT = 1000000.0;

static jobject cpu_stress_callback_global = nullptr;
static jclass integer_class_global = nullptr;
static jmethodID integer_value_of_mid = nullptr;
static jmethodID on_stress_data_mid = nullptr;
static std::thread cpu_stress_reporter_thread;
static std::vector<std::thread> cpu_stress_worker_threads;
static std::unique_ptr<CpuStressWorkerStats[]> cpu_stress_worker_stats;
static size_t cpu_stress_worker_count = 0;
static std::atomic<size_t> cpu_stress_workers_ready{0};
static std::atomic<bool> cpu_stress_reporter_ready{false};
static std::atomic<bool> cpu_stress_start{false};
static std::chrono::steady_clock::time_point cpu_stress_start_time;
static std::mutex cpu_stress_mutex;

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

JNIEXPORT jlong
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunCpuMathSingleCoreBenchmark(
        JNIEnv *env, jobject _, jobject callback) {
    if (callback == nullptr) return 0;
    jobject callback_global_ref = env->NewGlobalRef(callback);
    jclass callbackClass_local = env->GetObjectClass(callback_global_ref);
    auto callback_class_global_ref = (jclass)env->NewGlobalRef(callbackClass_local);
    env->DeleteLocalRef(callbackClass_local);
    jmethodID updateProgressMethod = env->GetMethodID(callback_class_global_ref, "onProgressUpdate", "(F)V");

    const long long total_iterations = 70000000LL;
    current_iterations_done.store(0, std::memory_order_relaxed);

    std::thread reporter_thread([callback_global_ref, updateProgressMethod]() {
        JNIEnv* thread_env = nullptr;
        g_vm->AttachCurrentThread(&thread_env, nullptr);

        int big_core = get_biggest_core();
        pin_to_core(big_core);

        if (setpriority(PRIO_PROCESS, 0, 0) != 0) {
            LOGE("Failed to set thread priority");
        }

        const std::chrono::milliseconds update_interval(50);
        const long long total_iters = 70000000LL;

        while (current_iterations_done.load(std::memory_order_relaxed) < total_iters) {
            long long done = current_iterations_done.load(std::memory_order_relaxed);
            auto progress = static_cast<float>(static_cast<long double>(done) / total_iters);

            if (thread_env && callback_global_ref && updateProgressMethod) {
                thread_env->CallVoidMethod(callback_global_ref, updateProgressMethod, progress);
            }

            std::this_thread::sleep_for(update_interval);
        }

        if (thread_env && callback_global_ref && updateProgressMethod) {
            thread_env->CallVoidMethod(callback_global_ref, updateProgressMethod, 1.0f);
        }

        g_vm->DetachCurrentThread();
    });

    int big_core = get_biggest_core();
    pin_to_core(big_core);

    auto start = std::chrono::high_resolution_clock::now();

    if (setpriority(PRIO_PROCESS, 0, -10) != 0) {
        LOGE("Failed to set thread priority");
    }

    long long local_counter = 0;
    double result = 0;
    for (long long i = 0; i < total_iterations; i++) {
        result += heavy_math(static_cast<double>(i));

        local_counter++;
        if (local_counter >= 100000) {
            current_iterations_done.fetch_add(100000, std::memory_order_relaxed);
            local_counter = 0;
        }
    }

    reporter_thread.join();

    env->DeleteGlobalRef(callback_global_ref);
    env->DeleteGlobalRef(callback_class_global_ref);

    auto end = std::chrono::high_resolution_clock::now();
    volatile double final_result = result;
    (void)final_result;
    return (jlong)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

static jlong run_multicore_benchmark(
        JNIEnv *env, jobject callback, long long total_iterations) {

    if (total_iterations <= 0) return 0;
    if (callback == nullptr) return 0;

    std::vector<int> perf_cores = get_performance_cores();
    unsigned int num_threads = perf_cores.size();
    const long long num_tasks = 100LL;
    const long long task_size = total_iterations / num_tasks;
    const long long remainder = total_iterations % num_tasks;

    std::atomic<int> next_task{0};
    std::atomic<long long> completed_iterations{0};

    jobject callback_global_ref = env->NewGlobalRef(callback);
    jclass callback_class = env->GetObjectClass(callback_global_ref);
    auto callback_class_global_ref = (jclass)env->NewGlobalRef(callback_class);
    env->DeleteLocalRef(callback_class);
    jmethodID update_progress_method_id = env->GetMethodID(callback_class_global_ref, "onProgressUpdate", "(F)V");

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    std::mutex sum_mutex;
    double sum = 0;

    for (unsigned int t = 0; t < num_threads; ++t) {
        int target_core = perf_cores[t];
        threads.emplace_back([&, target_core, callback_global_ref, update_progress_method_id, total_iterations]() {
            JNIEnv* thread_env = nullptr;
            if (g_vm->AttachCurrentThread(&thread_env, nullptr) != JNI_OK) {
                return;
            }
            pin_to_core(target_core);

            if (setpriority(PRIO_PROCESS, 0, -10) != 0) {
                LOGE("Failed to set thread priority");
            }

            double local_sum = 0;
            while (true) {
                int task_index = next_task.fetch_add(1, std::memory_order_relaxed);
                if (task_index >= (int)num_tasks) break;

                long long task_start = (long long)task_index * task_size + std::min((long long)task_index, remainder);
                long long task_end = task_start + task_size + (task_index < remainder ? 1 : 0);

                double result_thread = 0;
                for (long long i = task_start; i < task_end; ++i) {
                    result_thread += heavy_math(static_cast<double>(i));
                }
                local_sum += result_thread;

                long long completed = completed_iterations.fetch_add(task_end - task_start,
                                                                     std::memory_order_relaxed) + (task_end - task_start);
                auto progress = static_cast<float>(static_cast<long double>(completed) / total_iterations);

                if (thread_env && callback_global_ref && update_progress_method_id) {
                    thread_env->CallVoidMethod(callback_global_ref, update_progress_method_id, progress);
                }
            }

            {
                std::lock_guard<std::mutex> lock(sum_mutex);
                sum += local_sum;
            }

            g_vm->DetachCurrentThread();
        });
    }

    for (auto &th : threads) th.join();

    if (env && callback_global_ref && update_progress_method_id) {
        env->CallVoidMethod(callback_global_ref, update_progress_method_id, 1.0f);
    }
    env->DeleteGlobalRef(callback_global_ref);
    env->DeleteGlobalRef(callback_class_global_ref);

    auto end = std::chrono::high_resolution_clock::now();
    LOGD("System math multicore thread result: %f", sum);
    return (jlong)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

JNIEXPORT jlong
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunCpuMathMultiCoreBenchmark(
        JNIEnv *env, jobject thiz, jobject callback) {
    (void)thiz;
    const long long DEFAULT_ITERATIONS = 70000000LL;
    return run_multicore_benchmark(env, callback, DEFAULT_ITERATIONS);
}

static inline uint64_t rotate_left_64(uint64_t value, unsigned int shift) {
    return (value << shift) | (value >> (64U - shift));
}

static inline uint64_t cpu_stress_kernel(uint64_t state, uint64_t salt) {
    uint64_t a = state ^ salt;
    uint64_t b = state + 0x9E3779B97F4A7C15ULL;
    uint64_t c = ~state ^ 0xD1B54A32D192ED03ULL;
    uint64_t d = salt + 0x94D049BB133111EBULL;

    for (uint64_t round = 0; round < CPU_STRESS_ROUNDS_PER_BATCH; ++round) {
        a = rotate_left_64(a + b, 17) * 0xBF58476D1CE4E5B9ULL;
        b = rotate_left_64(b ^ c, 31) + a;
        c = rotate_left_64(c + d, 24) ^ b;
        d = rotate_left_64(d ^ a, 13) * 0x94D049BB133111EBULL + c;
    }

    return a ^ rotate_left_64(b, 7) ^ rotate_left_64(c, 19) ^ rotate_left_64(d, 37);
}

static std::vector<int> get_available_stress_cores() {
    std::vector<int> cores;
    cpu_set_t allowed_cpus;
    CPU_ZERO(&allowed_cpus);

    if (sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus) == 0) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (CPU_ISSET(cpu, &allowed_cpus)) cores.push_back(cpu);
        }
    }

    if (cores.empty()) {
        unsigned int fallback_count = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned int cpu = 0; cpu < fallback_count; ++cpu) {
            cores.push_back(static_cast<int>(cpu));
        }
    }

    return cores;
}

static long read_current_frequency_khz(int core_id) {
    std::ifstream frequency_file(
            "/sys/devices/system/cpu/cpu" + std::to_string(core_id) +
            "/cpufreq/scaling_cur_freq");
    long frequency = -1;
    if (frequency_file.is_open()) frequency_file >> frequency;
    return frequency;
}

static void cpu_stress_task(size_t worker_index, int core_id) {
    pin_to_core(core_id);

    CpuStressWorkerStats &stats = cpu_stress_worker_stats[worker_index];
    const uint64_t salt = 0x9E3779B97F4A7C15ULL * (worker_index + 1);
    uint64_t state = 0xD1B54A32D192ED03ULL ^ salt;
    uint64_t completed_rounds = 0;

    stats.completed_rounds.store(0, std::memory_order_relaxed);
    stats.checksum.store(state, std::memory_order_relaxed);
    cpu_stress_workers_ready.fetch_add(1, std::memory_order_release);

    while (!cpu_stress_start.load(std::memory_order_acquire)) {
        if (stop_cpu_stress.load(std::memory_order_relaxed)) return;
        std::this_thread::yield();
    }

    while (!stop_cpu_stress.load(std::memory_order_relaxed)) {
        state = cpu_stress_kernel(state, salt + completed_rounds);
        completed_rounds += CPU_STRESS_ROUNDS_PER_BATCH;
        stats.checksum.store(state, std::memory_order_relaxed);
        stats.completed_rounds.store(completed_rounds, std::memory_order_release);
    }
}

JNIEXPORT void
Java_com_komarudude_materialbench_data_native_NativeLib_nativeStartCpuStress(
        JNIEnv *env, jobject thiz, jobject callback) {
    (void)thiz;
    std::lock_guard<std::mutex> lock(cpu_stress_mutex);
    if (callback == nullptr || !stop_cpu_stress.load(std::memory_order_acquire)) return;

    std::vector<int> stress_cores = get_available_stress_cores();
    cpu_stress_worker_count = stress_cores.size();
    cpu_stress_worker_stats = std::make_unique<CpuStressWorkerStats[]>(cpu_stress_worker_count);

    for (size_t worker = 0; worker < cpu_stress_worker_count; ++worker) {
        cpu_stress_worker_stats[worker].core_id = stress_cores[worker];
    }

    cpu_stress_callback_global = env->NewGlobalRef(callback);
    jclass clz = env->GetObjectClass(cpu_stress_callback_global);
    on_stress_data_mid = env->GetMethodID(
            clz, "onStressData", "(JLjava/lang/Integer;Ljava/lang/Integer;)V");
    env->DeleteLocalRef(clz);

    jclass int_clz = env->FindClass("java/lang/Integer");
    integer_class_global = (jclass)env->NewGlobalRef(int_clz);
    integer_value_of_mid = env->GetStaticMethodID(
            integer_class_global, "valueOf", "(I)Ljava/lang/Integer;");
    env->DeleteLocalRef(int_clz);

    stop_cpu_stress.store(false, std::memory_order_relaxed);
    cpu_stress_start.store(false, std::memory_order_relaxed);
    cpu_stress_workers_ready.store(0, std::memory_order_relaxed);
    cpu_stress_reporter_ready.store(false, std::memory_order_relaxed);

    cpu_stress_reporter_thread = std::thread([]() {
        JNIEnv *thread_env = nullptr;
        if (g_vm->AttachCurrentThread(&thread_env, nullptr) != JNI_OK) {
            stop_cpu_stress.store(true, std::memory_order_relaxed);
            cpu_stress_reporter_ready.store(true, std::memory_order_release);
            return;
        }

        cpu_stress_reporter_ready.store(true, std::memory_order_release);
        while (!cpu_stress_start.load(std::memory_order_acquire)) {
            if (stop_cpu_stress.load(std::memory_order_relaxed)) {
                g_vm->DetachCurrentThread();
                return;
            }
            std::this_thread::yield();
        }

        std::vector<uint64_t> last_rounds(cpu_stress_worker_count, 0);
        auto last_sample_time = cpu_stress_start_time;
        auto next_sample_time = last_sample_time + std::chrono::seconds(1);
        int sample_index = 0;

        while (!stop_cpu_stress.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_until(next_sample_time);
            if (stop_cpu_stress.load(std::memory_order_relaxed)) break;

            auto sample_time = std::chrono::steady_clock::now();
            uint64_t total_delta = 0;

            for (size_t worker = 0; worker < cpu_stress_worker_count; ++worker) {
                CpuStressWorkerStats &stats = cpu_stress_worker_stats[worker];
                uint64_t current = stats.completed_rounds.load(std::memory_order_acquire);
                uint64_t delta = current - last_rounds[worker];
                last_rounds[worker] = current;
                total_delta += delta;

                if (sample_index < 10) {
                    long frequency = read_current_frequency_khz(stats.core_id);
                    uint64_t checksum = stats.checksum.load(std::memory_order_relaxed);
                    LOGD("CPU stress worker=%zu cpu=%d delta=%llu freq_khz=%ld checksum=%016llx",
                         worker,
                         stats.core_id,
                         static_cast<unsigned long long>(delta),
                         frequency,
                         static_cast<unsigned long long>(checksum));
                }
            }

            double elapsed_seconds =
                    std::chrono::duration<double>(sample_time - last_sample_time).count();
            double rounds_per_second = elapsed_seconds > 0.0
                    ? static_cast<double>(total_delta) / elapsed_seconds
                    : 0.0;
            int points = static_cast<int>(std::llround(
                    rounds_per_second / CPU_STRESS_ROUNDS_PER_POINT));

            if (sample_index < 10) {
                LOGD("CPU stress sample=%d delta=%llu elapsed_ms=%.2f rounds_s=%.0f points=%d",
                     sample_index + 1,
                     static_cast<unsigned long long>(total_delta),
                     elapsed_seconds * 1000.0,
                     rounds_per_second,
                     points);
            }
            ++sample_index;
            last_sample_time = sample_time;

            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            jobject cpu_perf_obj = thread_env->CallStaticObjectMethod(
                    integer_class_global, integer_value_of_mid, (jint)points);
            thread_env->CallVoidMethod(
                    cpu_stress_callback_global, on_stress_data_mid,
                    (jlong)now, cpu_perf_obj, nullptr);
            thread_env->DeleteLocalRef(cpu_perf_obj);

            next_sample_time += std::chrono::seconds(1);
            if (next_sample_time <= sample_time) {
                next_sample_time = sample_time + std::chrono::seconds(1);
            }
        }

        g_vm->DetachCurrentThread();
    });

    cpu_stress_worker_threads.clear();
    cpu_stress_worker_threads.reserve(cpu_stress_worker_count);
    for (size_t worker = 0; worker < cpu_stress_worker_count; ++worker) {
        cpu_stress_worker_threads.emplace_back(
                cpu_stress_task, worker, stress_cores[worker]);
    }

    while ((!cpu_stress_reporter_ready.load(std::memory_order_acquire) ||
            cpu_stress_workers_ready.load(std::memory_order_acquire) < cpu_stress_worker_count) &&
           !stop_cpu_stress.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
    }

    if (stop_cpu_stress.load(std::memory_order_relaxed)) {
        if (cpu_stress_reporter_thread.joinable()) cpu_stress_reporter_thread.join();
        for (auto &worker : cpu_stress_worker_threads) {
            if (worker.joinable()) worker.join();
        }
        cpu_stress_worker_threads.clear();
        cpu_stress_worker_stats.reset();
        cpu_stress_worker_count = 0;
        env->DeleteGlobalRef(cpu_stress_callback_global);
        env->DeleteGlobalRef(integer_class_global);
        cpu_stress_callback_global = nullptr;
        integer_class_global = nullptr;
        return;
    }

    cpu_stress_start_time = std::chrono::steady_clock::now();
    cpu_stress_start.store(true, std::memory_order_release);
}

JNIEXPORT void
Java_com_komarudude_materialbench_data_native_NativeLib_nativeStopCpuStress(
        JNIEnv *env, jobject thiz) {
    (void)thiz;
    std::lock_guard<std::mutex> lock(cpu_stress_mutex);
    stop_cpu_stress.store(true, std::memory_order_relaxed);

    if (cpu_stress_reporter_thread.joinable()) {
        cpu_stress_reporter_thread.join();
    }
    for (auto &worker : cpu_stress_worker_threads) {
        if (worker.joinable()) worker.join();
    }
    cpu_stress_worker_threads.clear();

    uint64_t combined_checksum = 0;
    for (size_t worker = 0; worker < cpu_stress_worker_count; ++worker) {
        combined_checksum ^= cpu_stress_worker_stats[worker].checksum.load(
                std::memory_order_relaxed);
    }
    LOGD("CPU stress stopped: checksum=%016llx",
         static_cast<unsigned long long>(combined_checksum));

    cpu_stress_worker_stats.reset();
    cpu_stress_worker_count = 0;
    cpu_stress_start.store(false, std::memory_order_relaxed);
    cpu_stress_workers_ready.store(0, std::memory_order_relaxed);
    cpu_stress_reporter_ready.store(false, std::memory_order_relaxed);

    if (cpu_stress_callback_global) {
        env->DeleteGlobalRef(cpu_stress_callback_global);
        cpu_stress_callback_global = nullptr;
    }
    if (integer_class_global) {
        env->DeleteGlobalRef(integer_class_global);
        integer_class_global = nullptr;
    }
    integer_value_of_mid = nullptr;
    on_stress_data_mid = nullptr;
}

}
