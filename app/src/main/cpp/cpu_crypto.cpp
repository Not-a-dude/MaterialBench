#include <jni.h>
#include "openssl/evp.h"
#include <chrono>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include "utils.h"

void get_ctr_iv_for_block(const unsigned char* base_iv, long long block_index, unsigned char* out_iv) {
    memcpy(out_iv, base_iv, 16);
    auto counter = (unsigned long long*)(out_iv + 8);
    *counter += block_index;
}


extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunCpuCryptoSingleCoreBenchmark(JNIEnv *env, jobject /*thiz*/, jobject activity) {
    const int SIZE = 256 * 1024 * 1024;
    const int ITERATIONS = 200;

    auto *data_in = (unsigned char*)aligned_alloc(64, SIZE);
    auto *data_encrypted = (unsigned char*)aligned_alloc(64, SIZE);
    auto *data_decrypted = (unsigned char*)aligned_alloc(64, SIZE);

    if (!data_in || !data_encrypted || !data_decrypted) {
        free(data_in); free(data_encrypted); free(data_decrypted);
        return -1; // Memory allocation error
    }

    for (size_t i = 0; i < SIZE; i++) data_in[i] = (unsigned char)(i & 0xFF);
    mlock(data_in, SIZE); mlock(data_encrypted, SIZE); mlock(data_decrypted, SIZE);

    unsigned char key[32]; memset(key, 0x11, sizeof(key));
    unsigned char iv[16];  memset(iv, 0x22, sizeof(iv));

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    update_progress(env, activity, updateProgressMethod, 0.0f);
    auto total_start = std::chrono::high_resolution_clock::now();

    int big_core = get_biggest_core();
    pin_to_core(big_core);

    if (setpriority(PRIO_PROCESS, 0, -10) != 0) {
        LOGE("Failed to set thread priority");
    }

    for (int i = 0; i < ITERATIONS; ++i) {
        // Encrypt
        EVP_CIPHER_CTX *ctx_enc = EVP_CIPHER_CTX_new();
        if (!ctx_enc) { return -2; }
        if (1 != EVP_EncryptInit_ex(ctx_enc, EVP_aes_256_ctr(), nullptr, key, iv)) { return -3; }
        int encrypted_len = 0;
        if (1 != EVP_EncryptUpdate(ctx_enc, data_encrypted, &encrypted_len, data_in, SIZE)) { return -4; }
        int tmplen = 0;
        if (1 != EVP_EncryptFinal_ex(ctx_enc, data_encrypted + encrypted_len, &tmplen)) { return -5; }
        EVP_CIPHER_CTX_free(ctx_enc);

        update_progress(env, activity, updateProgressMethod, (float)(i * 2 + 1) / (ITERATIONS * 2));

        // Decrypt
        EVP_CIPHER_CTX *ctx_dec = EVP_CIPHER_CTX_new();
        if (!ctx_dec) { return -6; }
        if (1 != EVP_DecryptInit_ex(ctx_dec, EVP_aes_256_ctr(), nullptr, key, iv)) { return -7; }
        int decrypted_len = 0;
        if (1 != EVP_DecryptUpdate(ctx_dec, data_decrypted, &decrypted_len, data_encrypted, encrypted_len + tmplen)) { return -8; }
        int tmplen2 = 0;
        if (1 != EVP_DecryptFinal_ex(ctx_dec, data_decrypted + decrypted_len, &tmplen2)) { return -9; }
        EVP_CIPHER_CTX_free(ctx_dec);

        update_progress(env, activity, updateProgressMethod, (float)(i * 2 + 2) / (ITERATIONS * 2));
    }

    if (memcmp(data_in, data_decrypted, SIZE) != 0) {
        munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
        free(data_in); free(data_encrypted); free(data_decrypted);
        return -10;
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    update_progress(env, activity, updateProgressMethod, 1.0f);

    long long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    
    munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
    free(data_in); free(data_encrypted); free(data_decrypted);
    return duration_ms;
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunCpuCryptoMultiCoreBenchmark(
        JNIEnv *env, jobject thiz, jobject activity) {

    const int SIZE = 256 * 1024 * 1024;
    const int TOTAL_ITERATIONS = 200;

    std::vector<int> perf_cores = get_performance_cores();
    const unsigned int num_cores = perf_cores.size();

    // General enter buffer
    unsigned char* data_in = (unsigned char*)aligned_alloc(64, SIZE);
    if (!data_in) return -1;
    for (size_t i = 0; i < SIZE; i++) data_in[i] = (unsigned char)(i & 0xFF);

    jobject activity_global_ref = env->NewGlobalRef(activity);
    jclass activity_class = env->GetObjectClass(activity_global_ref);
    jmethodID update_progress_method_id = env->GetMethodID(activity_class, "updateBenchmarkProgress", "(F)V");

    std::atomic<int> next_iteration{0};
    std::atomic<int> progress_counter{0};
    std::atomic<bool> error_flag{false};

    auto total_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;

    for (unsigned int t = 0; t < num_cores; ++t) {
        int target_core = perf_cores[t];

        threads.emplace_back([=, &next_iteration, &progress_counter, &error_flag, &data_in]() {
            JNIEnv* thread_env;
            if (g_vm->AttachCurrentThread(&thread_env, nullptr) != JNI_OK) {
                error_flag = true; return;
            }

            pin_to_core(target_core);
            setpriority(PRIO_PROCESS, 0, -10);

            unsigned char* t_enc = (unsigned char*)aligned_alloc(64, SIZE);
            unsigned char* t_dec = (unsigned char*)aligned_alloc(64, SIZE);

            if (!t_enc || !t_dec) {
                error_flag = true;
            } else {
                unsigned char key[32]; memset(key, 0x11, 32);
                unsigned char base_iv[16]; memset(base_iv, 0x22, 16);

                while (true) {
                    int iter = next_iteration.fetch_add(1);
                    if (iter >= TOTAL_ITERATIONS || error_flag.load()) break;

                    unsigned char thread_iv[16];
                    get_ctr_iv_for_block(base_iv, (long long)iter * (SIZE / 16), thread_iv);

                    // Encrypt
                    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
                    int outlen = 0;
                    EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key, thread_iv);
                    EVP_EncryptUpdate(ctx, t_enc, &outlen, data_in, SIZE);
                    EVP_CIPHER_CTX_free(ctx);

                    // Decrypt
                    ctx = EVP_CIPHER_CTX_new();
                    EVP_DecryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key, thread_iv);
                    EVP_DecryptUpdate(ctx, t_dec, &outlen, t_enc, SIZE);
                    EVP_CIPHER_CTX_free(ctx);

                    // Check
                    if (memcmp(data_in, t_dec, SIZE) != 0) error_flag = true;

                    // Progress
                    int p = progress_counter.fetch_add(2) + 2;
                    if (iter % 5 == 0) {
                        thread_env->CallVoidMethod(activity_global_ref, update_progress_method_id, (float)p / (TOTAL_ITERATIONS * 2));
                    }
                }
            }

            // Clean before exit from thread
            if (t_enc) free(t_enc);
            if (t_dec) free(t_dec);
            g_vm->DetachCurrentThread();
        });
    }

    for (auto &th : threads) th.join();

    free(data_in);
    env->DeleteGlobalRef(activity_global_ref);

    if (error_flag) return -11;

    auto total_end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
}

}
