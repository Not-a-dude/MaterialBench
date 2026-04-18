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
float run_sve2(const float* a, const float* b, const float* c, const float* d, uint64_t n) {
    svfloat32_t sum_v = svdup_f32(0.0f);
    const svfloat32_t v_threshold = svdup_f32(1.1f);
    const svfloat32_t v_zero = svdup_f32(0.0f);

    for (uint64_t i = 0; i < n; i += svcntw()) {
        svbool_t pg = svwhilelt_b32(i, n);

        svfloat32_t va = svld1_f32(pg, &a[i]);
        svfloat32_t vb = svld1_f32(pg, &b[i]);
        svfloat32_t vc = svld1_f32(pg, &c[i]);
        svfloat32_t vd = svld1_f32(pg, &d[i]);

        svuint32_t v_idx = svcvt_u32_f32_z(pg, svabs_f32_z(pg, vb));
        v_idx = svand_n_u32_z(pg, v_idx, 0x7F);

        svfloat32_t v_gathered = svld1_gather_u32index_f32(pg, a, v_idx);

        svfloat32_t res1 = svmla_f32_z(pg, vc, va, vb);

        svuint32_t bits      = svreinterpret_u32_f32(svabs_f32_z(pg, res1));
        svuint32_t exp_field = svlsr_n_u32_z(pg, bits, 23);
        svuint32_t mant      = svand_n_u32_z(pg, bits, 0x7FFFFF);

        svbool_t is_zero   = svcmpeq_n_u32(pg, bits, 0);
        svbool_t is_normal = svcmpne_n_u32(pg, exp_field, 0);

        svint32_t exp_norm = svsub_s32_z(pg, svreinterpret_s32_u32(exp_field), svdup_s32(127));
        svint32_t clz_mant = svreinterpret_s32_u32(svclz_u32_z(pg, mant));
        svint32_t exp_sub  = svsub_s32_z(pg, svdup_s32(-118), clz_mant);

        svint32_t exp_bucket = svsel_s32(
                is_zero, svdup_s32(-1),
                svsel_s32(is_normal, exp_norm, exp_sub)
        );

        svint32_t vi_res = svcvt_s32_f32_z(pg, res1);
        svint32_t vi_vd  = svcvt_s32_f32_z(pg, vd);
        svint32_t vi_sat = svqadd_s32(vi_res, vi_vd);
        svfloat32_t v_sat = svcvt_f32_s32_z(pg, vi_sat);

        svbool_t mask1 = svcmpgt_f32(pg, v_gathered, v_threshold);
        svbool_t mask2 = svcmpne_n_s32(pg, exp_bucket, 0);
        svbool_t final_mask = svand_b_z(pg, mask1, mask2);

        svfloat32_t res2 = svsel_f32(final_mask, v_sat, svsub_f32_z(pg, res1, v_gathered));
        svfloat32_t res3 = svmla_f32_z(pg, res2, vd, va);
        svfloat32_t final_res = svmax_f32_z(pg, res3, v_zero);

        sum_v = svadd_f32_m(pg, sum_v, final_res);
    }

    return svaddv_f32(svptrue_b32(), sum_v);
}

float run_neon(const float* a, const float* b, const float* c, const float* d, int n) {
    float32x4_t sum_v = vdupq_n_f32(0.0f);
    const float32x4_t v_threshold = vdupq_n_f32(1.1f);
    const float32x4_t v_zero = vdupq_n_f32(0.0f);

    int i = 0;
    for (; i <= n - 4; i += 4) {
        float32x4_t va = vld1q_f32(&a[i]);
        float32x4_t vb = vld1q_f32(&b[i]);
        float32x4_t vc = vld1q_f32(&c[i]);
        float32x4_t vd = vld1q_f32(&d[i]);

        uint32_t idx[4];
        vst1q_u32(idx, vreinterpretq_u32_f32(vabsq_f32(vb)));

        float g_vals[4];
        g_vals[0] = a[idx[0] & 0x7F];
        g_vals[1] = a[idx[1] & 0x7F];
        g_vals[2] = a[idx[2] & 0x7F];
        g_vals[3] = a[idx[3] & 0x7F];

        float32x4_t v_gathered = vld1q_f32(g_vals);

        float32x4_t res1 = vfmaq_f32(vc, va, vb);

        uint32x4_t bits      = vreinterpretq_u32_f32(vabsq_f32(res1));
        uint32x4_t exp_field = vshrq_n_u32(bits, 23);
        uint32x4_t mant      = vandq_u32(bits, vdupq_n_u32(0x7FFFFF));

        uint32x4_t is_zero   = vceqq_u32(bits, vdupq_n_u32(0));
        uint32x4_t is_normal = vcgtq_u32(exp_field, vdupq_n_u32(0));

        int32x4_t exp_norm = vsubq_s32(vreinterpretq_s32_u32(exp_field), vdupq_n_s32(127));
        int32x4_t clz_mant = vreinterpretq_s32_u32(vclzq_u32(mant));
        int32x4_t exp_sub  = vsubq_s32(vdupq_n_s32(-118), clz_mant);

        int32x4_t exp_bucket = vbslq_s32(
                is_zero, vdupq_n_s32(-1),
                vbslq_s32(is_normal, exp_norm, exp_sub)
        );

        int32x4_t vi_res = vcvtq_s32_f32(res1);
        int32x4_t vi_vd  = vcvtq_s32_f32(vd);
        int32x4_t vi_sat = vqaddq_s32(vi_res, vi_vd);
        float32x4_t v_sat = vcvtq_f32_s32(vi_sat);

        uint32x4_t mask1 = vcgtq_f32(v_gathered, v_threshold);
        uint32x4_t mask2 = vmvnq_u32(vceqq_s32(exp_bucket, vdupq_n_s32(0)));
        uint32x4_t final_mask = vandq_u32(mask1, mask2);

        float32x4_t res2 = vbslq_f32(final_mask, v_sat, vsubq_f32(res1, v_gathered));
        float32x4_t res3 = vfmaq_f32(res2, vd, va);
        float32x4_t final_res = vmaxq_f32(res3, v_zero);

        sum_v = vaddq_f32(sum_v, final_res);
    }

    float total = vgetq_lane_f32(sum_v, 0) + vgetq_lane_f32(sum_v, 1) +
                  vgetq_lane_f32(sum_v, 2) + vgetq_lane_f32(sum_v, 3);

    for (; i < n; i++) {
        uint32_t idx = (uint32_t)std::fabs(b[i]) & 0x7F;
        float gathered = a[idx];

        float res1 = std::fma(a[i], b[i], c[i]);

        uint32_t bits;
        std::memcpy(&bits, &res1, sizeof(bits));
        bits &= 0x7FFFFFFFu;

        int exp_bucket;
        if (bits == 0) {
            exp_bucket = -1;
        } else {
            uint32_t exp_field = bits >> 23;
            if (exp_field != 0) {
                exp_bucket = (int)exp_field - 127;
            } else {
                uint32_t mant = bits & 0x7FFFFFu;
                exp_bucket = -118 - (int)__builtin_clz(mant);
            }
        }

        int32_t vi_res = (int32_t)res1;
        int32_t vi_d   = (int32_t)d[i];
        int32_t vi_sat = (int32_t)std::clamp<int64_t>((int64_t)vi_res + (int64_t)vi_d, INT32_MIN, INT32_MAX);
        float v_sat = (float)vi_sat;

        float res2 = (gathered > 1.1f && exp_bucket != 0) ? v_sat : (res1 - gathered);
        float res3 = res2 + d[i] * a[i];

        if (res3 > 0.0f) total += res3;
    }

    return total;
}

inline float heavy_vector_math(const float* a, const float* b, const float* c, const float* d, int n) {
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

    const int VECTOR_SIZE = 128;
    const long long total_iterations = 25000000LL;
    current_iterations_done_vector.store(0, std::memory_order_relaxed);

    std::thread reporter_thread([callback_global_ref, updateProgressMethod, total_iterations]() {
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

    std::vector<float> a(VECTOR_SIZE), b(VECTOR_SIZE), c(VECTOR_SIZE), d(VECTOR_SIZE);
    for(int i = 0; i < VECTOR_SIZE; i++) {
        auto fi = static_cast<float>(i);
        a[i] = 1.1f + fi;
        b[i] = 2.2f + fi;
        c[i] = 3.3f + fi;
        d[i] = 4.4f + fi;
    }

    long long local_counter = 0;
    float result = 0;
    for (long long i = 0; i < total_iterations; i++) {
        result += heavy_vector_math(a.data(), b.data(), c.data(), d.data(), VECTOR_SIZE);

        a[0] += 1e-7f;
        b[VECTOR_SIZE-1] -= 1e-7f;

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

    volatile float sink = result;
    (void)sink;

    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

}
