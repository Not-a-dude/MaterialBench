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
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunCpuCryptoMultiCoreBenchmark(JNIEnv *env, jobject /*thiz*/, jobject activity) {
    const int SIZE = 256 * 1024 * 1024;
    const int ITERATIONS = 200;
    const unsigned int num_cores = std::thread::hardware_concurrency();

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
    unsigned char base_iv[16];  memset(base_iv, 0x22, sizeof(base_iv));

    jobject activity_global_ref = env->NewGlobalRef(activity);
    jclass activity_class = env->GetObjectClass(activity_global_ref);
    jmethodID update_progress_method_id = env->GetMethodID(activity_class, "updateBenchmarkProgress", "(F)V");

    std::atomic<int> progress_counter{0};
    const int total_progress_steps = ITERATIONS * num_cores * 2; // Each thread does Encrypt + Decrypt per iteration
    std::atomic<bool> error_flag{false};

    update_progress(env, activity, update_progress_method_id, 0.0f);
    auto total_start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_cores);

    for (unsigned int t = 0; t < num_cores; ++t) {
        threads.emplace_back([=, &progress_counter, &error_flag]() {
            JNIEnv* thread_env;
            // Attach JNI once per thread
            if (g_vm->AttachCurrentThread(&thread_env, nullptr) != JNI_OK) {
                error_flag = true;
                return;
            }

            if (setpriority(PRIO_PROCESS, 0, -10) != 0) {
                LOGE("Failed to set thread priority");
            }

            const int chunk_size = SIZE / num_cores;
            const int offset = t * chunk_size;
            const bool is_last_thread = (t == num_cores - 1);
            const int current_chunk_size = is_last_thread ? (SIZE - offset) : chunk_size;

            for (int iter = 0; iter < ITERATIONS; ++iter) {
                if (error_flag.load()) break;

                unsigned char thread_iv[16];
                get_ctr_iv_for_block(base_iv, (offset / 16) + (long long)iter * (SIZE / 16), thread_iv);

                // Encrypt
                EVP_CIPHER_CTX *ctx_enc = EVP_CIPHER_CTX_new();
                if (!ctx_enc) { error_flag = true; break; }
                if (1 != EVP_EncryptInit_ex(ctx_enc, EVP_aes_256_ctr(), nullptr, key, thread_iv)) { error_flag = true; EVP_CIPHER_CTX_free(ctx_enc); break; }
                int encrypted_len = 0;
                if (1 != EVP_EncryptUpdate(ctx_enc, data_encrypted + offset, &encrypted_len, data_in + offset, current_chunk_size)) { error_flag = true; EVP_CIPHER_CTX_free(ctx_enc); break; }
                int tmplen = 0;
                if (1 != EVP_EncryptFinal_ex(ctx_enc, data_encrypted + offset + encrypted_len, &tmplen)) { error_flag = true; EVP_CIPHER_CTX_free(ctx_enc); break; }
                EVP_CIPHER_CTX_free(ctx_enc);

                progress_counter.fetch_add(1, std::memory_order_relaxed);
                thread_env->CallVoidMethod(activity_global_ref, update_progress_method_id, (float)progress_counter.load() / total_progress_steps);


                // Decrypt
                EVP_CIPHER_CTX *ctx_dec = EVP_CIPHER_CTX_new();
                if (!ctx_dec) { error_flag = true; break; }
                if (1 != EVP_DecryptInit_ex(ctx_dec, EVP_aes_256_ctr(), nullptr, key, thread_iv)) { error_flag = true; EVP_CIPHER_CTX_free(ctx_dec); break; }
                int decrypted_len = 0;
                if (1 != EVP_DecryptUpdate(ctx_dec, data_decrypted + offset, &decrypted_len, data_encrypted + offset, encrypted_len + tmplen)) { error_flag = true; EVP_CIPHER_CTX_free(ctx_dec); break; }
                int tmplen2 = 0;
                if (1 != EVP_DecryptFinal_ex(ctx_dec, data_decrypted + offset + decrypted_len, &tmplen2)) { error_flag = true; EVP_CIPHER_CTX_free(ctx_dec); break; }
                EVP_CIPHER_CTX_free(ctx_dec);

                progress_counter.fetch_add(1, std::memory_order_relaxed);
                thread_env->CallVoidMethod(activity_global_ref, update_progress_method_id, (float)progress_counter.load() / total_progress_steps);
            }
            
            // Detach JNI once per thread
            g_vm->DetachCurrentThread();
        });
    }

    for (auto &th : threads) th.join();

    if (error_flag) {
        env->DeleteGlobalRef(activity_global_ref);
        munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
        free(data_in); free(data_encrypted); free(data_decrypted);
        return -11; 
    }

    if (memcmp(data_in, data_decrypted, SIZE) != 0) {
        env->DeleteGlobalRef(activity_global_ref);
        munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
        free(data_in); free(data_encrypted); free(data_decrypted);
        return -10;
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    update_progress(env, activity, update_progress_method_id, 1.0f);

    long long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    
    env->DeleteGlobalRef(activity_global_ref);
    munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
    free(data_in); free(data_encrypted); free(data_decrypted);
    return duration_ms;
}

}
