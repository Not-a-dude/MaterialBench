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

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunCpuCryptoBenchmark(JNIEnv *env, jobject /*thiz*/, jobject callback) {
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

    jclass callbackClass = env->GetObjectClass(callback);
    jmethodID updateProgressMethod = env->GetMethodID(callbackClass, "onProgressUpdate", "(F)V");

    update_progress(env, callback, updateProgressMethod, 0.0f);
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

        update_progress(env, callback, updateProgressMethod, (float)(i * 2 + 1) / (ITERATIONS * 2));

        // Decrypt
        EVP_CIPHER_CTX *ctx_dec = EVP_CIPHER_CTX_new();
        if (!ctx_dec) { return -6; }
        if (1 != EVP_DecryptInit_ex(ctx_dec, EVP_aes_256_ctr(), nullptr, key, iv)) { return -7; }
        int decrypted_len = 0;
        if (1 != EVP_DecryptUpdate(ctx_dec, data_decrypted, &decrypted_len, data_encrypted, encrypted_len + tmplen)) { return -8; }
        int tmplen2 = 0;
        if (1 != EVP_DecryptFinal_ex(ctx_dec, data_decrypted + decrypted_len, &tmplen2)) { return -9; }
        EVP_CIPHER_CTX_free(ctx_dec);

        update_progress(env, callback, updateProgressMethod, (float)(i * 2 + 2) / (ITERATIONS * 2));
    }

    if (memcmp(data_in, data_decrypted, SIZE) != 0) {
        munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
        free(data_in); free(data_encrypted); free(data_decrypted);
        return -10;
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    update_progress(env, callback, updateProgressMethod, 1.0f);

    long long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    
    munlock(data_in, SIZE); munlock(data_encrypted, SIZE); munlock(data_decrypted, SIZE);
    free(data_in); free(data_encrypted); free(data_decrypted);
    return duration_ms;
}

}
