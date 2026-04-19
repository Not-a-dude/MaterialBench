#include <zstd.h>
#include <jni.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/resource.h>
#include "utils.h"

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunCpuCompressBenchmark(
        JNIEnv *env, jobject thiz, jobject asset_manager, jobject callback) {

    if (asset_manager == nullptr || callback == nullptr) return 0;

    AAssetManager* mgr = AAssetManager_fromJava(env, asset_manager);
    AAsset* asset = AAssetManager_open(mgr, "enwik_128.bin", AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open enwik_128.bin from assets");
        return 0;
    }

    off_t src_size = AAsset_getLength(asset);
    std::vector<uint8_t> src_buffer(src_size);
    int read_result = AAsset_read(asset, src_buffer.data(), src_size);
    AAsset_close(asset);

    if (read_result < 0) {
        LOGE("Failed to read enwik_128.bin");
        return 0;
    }

    jclass callback_class = env->GetObjectClass(callback);
    jmethodID update_progress_method = env->GetMethodID(callback_class, "onProgressUpdate", "(F)V");

    size_t const dst_capacity = ZSTD_compressBound(src_size);
    std::vector<uint8_t> dst_buffer(dst_capacity);
    ZSTD_CCtx* const cctx = ZSTD_createCCtx();

    int big_core = get_biggest_core();
    pin_to_core(big_core);
    setpriority(PRIO_PROCESS, 0, -10);

    const std::vector<int> levels = {1, 3, 5, 8};
    const size_t total_steps = levels.size();
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < total_steps; i++) {
        int current_level = levels[i];
        size_t const c_size = ZSTD_compressCCtx(
                cctx,
                dst_buffer.data(), dst_capacity,
                src_buffer.data(), src_size,
                current_level
        );

        if (ZSTD_isError(c_size)) {
            LOGE("ZSTD error at level %d: %s", current_level, ZSTD_getErrorName(c_size));
            break;
        }

        update_progress(env, callback, update_progress_method, static_cast<float>(i + 1) / static_cast<float>(total_steps));
    }

    auto end = std::chrono::high_resolution_clock::now();

    update_progress(env, callback, update_progress_method, 1.0f);

    ZSTD_freeCCtx(cctx);

    return (jlong)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

}
