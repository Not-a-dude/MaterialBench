#include <arm_neon.h>
#include <arm_sve.h>
#include <asm/hwcap.h>
#include <jni.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/resource.h>
#include <sys/auxv.h>
#include <cmath>
#include <vector>
#include "utils.h"

std::atomic<long long> current_iterations_done_vector(0);

__attribute__((target("sve2")))
double run_sve2(const double* a, const double* b, const double* c, const double* d, uint64_t n) {
    svfloat64_t sum_v = svdup_f64(0.0);

    for (uint64_t i = 0; i < n; i += svcntd()) {
        svbool_t pg = svwhilelt_b64(i, n);

        svfloat64_t va = svld1_f64(pg, &a[i]);
        svfloat64_t vb = svld1_f64(pg, &b[i]);
        svfloat64_t vc = svld1_f64(pg, &c[i]);
        svfloat64_t vd = svld1_f64(pg, &d[i]);

        svfloat64_t res = svmla_f64_z(pg, vc, va, vb);
        res = svdiv_f64_z(pg, svsqrt_f64_z(pg, res), svadd_f64_z(pg, vd, svdup_f64(1.1)));

        res = svmla_f64_z(pg, vb, va, res);
        res = svdiv_f64_z(pg, svsqrt_f64_z(pg, res), svadd_f64_z(pg, vc, svdup_f64(2.2)));

        svbool_t pos_mask = svcmpgt_f64(pg, res, svdup_f64(0.0));
        sum_v = svadd_f64_m(pos_mask, sum_v, res);
    }

    return svaddv_f64(svptrue_b64(), sum_v);
}

double run_neon(const double* a, const double* b, const double* c, const double* d, int n) {
    float64x2_t sum_v = vdupq_n_f64(0.0);
    int i = 0;

    for (; i <= n - 2; i += 2) {
        float64x2_t va = vld1q_f64(&a[i]);
        float64x2_t vb = vld1q_f64(&b[i]);
        float64x2_t vc = vld1q_f64(&c[i]);
        float64x2_t vd = vld1q_f64(&d[i]);

        float64x2_t res = vfmaq_f64(vc, va, vb);
        res = vdivq_f64(vsqrtq_f64(res), vaddq_f64(vd, vdupq_n_f64(1.1)));

        res = vfmaq_f64(vb, va, res);
        res = vdivq_f64(vsqrtq_f64(res), vaddq_f64(vc, vdupq_n_f64(2.2)));

        uint64x2_t pos_mask = vcgtq_f64(res, vdupq_n_f64(0.0));
        float64x2_t filtered_res = vbslq_f64(pos_mask, res, vdupq_n_f64(0.0));
        sum_v = vaddq_f64(sum_v, filtered_res);
    }

    double total = vgetq_lane_f64(sum_v, 0) + vgetq_lane_f64(sum_v, 1);

    for (; i < n; i++) {
        double res = a[i] * b[i] + c[i];
        if (res > 0) {
            res = std::sqrt(res) / (d[i] + 1.1);
            res = a[i] * res + b[i];
            if (res > 0) {
                res = std::sqrt(res) / (c[i] + 2.2);
                if (res > 0) total += res;
            }
        }
    }
    return total;
}

inline double heavy_vector_math(const double* a, const double* b, const double* c, const double* d, int n) {
    static const unsigned long hwcaps2 = getauxval(AT_HWCAP2);
    static const bool has_sve2 = (hwcaps2 & HWCAP2_SVE2);

    if (has_sve2) {
        return run_sve2(a, b, c, d, static_cast<uint64_t>(n));
    } else {
        return run_neon(a, b, c, d, n);
    }
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunCpuVectorMathBenchmark(
        JNIEnv *env, jobject thiz, jobject callback) {
    (void)thiz;
    if (callback == nullptr) return 0;

    jobject callback_global_ref = env->NewGlobalRef(callback);
    jclass callbackClass_local = env->GetObjectClass(callback_global_ref);
    auto callback_class_global_ref = (jclass)env->NewGlobalRef(callbackClass_local);
    env->DeleteLocalRef(callbackClass_local);
    jmethodID updateProgressMethod = env->GetMethodID(callback_class_global_ref, "onProgressUpdate", "(F)V");

    const int VECTOR_SIZE = 128; // Large data block to show SVE's VLA benefit
    const long long total_iterations = 14000000LL;
    current_iterations_done_vector.store(0, std::memory_order_relaxed);

    std::thread reporter_thread([callback_global_ref, updateProgressMethod]() {
        JNIEnv* thread_env = nullptr;
        g_vm->AttachCurrentThread(&thread_env, nullptr);

        int big_core = get_biggest_core();
        pin_to_core(big_core);

        const std::chrono::milliseconds update_interval(100);

        while (current_iterations_done_vector.load(std::memory_order_relaxed) < total_iterations) {
            long long done = current_iterations_done_vector.load(std::memory_order_relaxed);
            auto progress = static_cast<float>(static_cast<double>(done) / total_iterations);

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

    if (setpriority(PRIO_PROCESS, 0, -15) != 0) {
        LOGE("Failed to set thread priority for benchmark");
    }

    std::vector<double> a(VECTOR_SIZE), b(VECTOR_SIZE), c(VECTOR_SIZE), d(VECTOR_SIZE);
    for(int i = 0; i < VECTOR_SIZE; i++) {
        a[i] = 1.1 + i;
        b[i] = 2.2 + i;
        c[i] = 3.3 + i;
        d[i] = 4.4 + i;
    }

    long long local_counter = 0;
    double result = 0;
    for (long long i = 0; i < total_iterations; i++) {
        result += heavy_vector_math(a.data(), b.data(), c.data(), d.data(), VECTOR_SIZE);

        a[0] += 1e-10;
        b[VECTOR_SIZE-1] -= 1e-10;

        local_counter++;
        if (local_counter >= 10000) {
            current_iterations_done_vector.fetch_add(local_counter, std::memory_order_relaxed);
            local_counter = 0;
        }
    }

    current_iterations_done_vector.store(total_iterations, std::memory_order_relaxed);
    reporter_thread.join();

    env->DeleteGlobalRef(callback_global_ref);
    env->DeleteGlobalRef(callback_class_global_ref);

    auto end = std::chrono::high_resolution_clock::now();

    volatile double sink = result;
    (void)sink;

    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

}
