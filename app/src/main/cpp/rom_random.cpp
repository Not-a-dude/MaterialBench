#include <jni.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>
#include <errno.h>
#include "utils.h"

extern "C" {

bool create_random_test_file(const std::string& path, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    const size_t buffer_size = 64 * 1024;
    std::vector<char> buffer(buffer_size);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dist_value(0, 255);

    for (size_t i = 0; i < size; i += buffer_size) {
        size_t chunk_size = std::min(buffer_size, size - i);
        for (size_t j = 0; j < chunk_size; j++) {
            buffer[j] = static_cast<char>(dist_value(gen));
        }
        file.write(buffer.data(), chunk_size);
        if (!file) return false;
    }

    return true;
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRomRandomWriteBenchmark(
        JNIEnv* env, jobject /*thiz*/, jobject activity) {

    const size_t file_size = 500 * 1024 * 1024; // 500 MB
    const int block_size = 4 * 1024; // 4 KB
    const int iterations = file_size / block_size;
    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    std::string filePath = get_files_dir_path(env, activity) + "/mb_random_write_test.bin";

    std::ofstream pre_alloc_file(filePath, std::ios::binary | std::ios::trunc);
    if (!pre_alloc_file) {
        return -1;
    }
    std::vector<char> empty_block(block_size, 0);
    for (int i = 0; i < iterations; ++i) {
        pre_alloc_file.write(empty_block.data(), block_size);
    }
    pre_alloc_file.close();

    int fd = -1;
    uint8_t* block = nullptr;

    fd = open(filePath.c_str(), O_RDWR);
    if (fd < 0) {
        LOGI("Failed to open file in O_RDWR mode (errno: %d)", errno);
        remove(filePath.c_str());
        return -1;
    }

    block = new uint8_t[block_size];
    if (!block) {
        LOGI("Failed to allocate regular memory");
        close(fd);
        remove(filePath.c_str());
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist_offset(0, iterations - 1);
    std::uniform_int_distribution<uint16_t> dist_value(0, 255);

    const int total_progress_updates = 100;
    const int64_t progress_step = iterations / total_progress_updates;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        size_t offset = dist_offset(gen) * block_size;

        for (int j = 0; j < block_size; j++) {
            block[j] = static_cast<uint8_t>(dist_value(gen));
        }

        if (pwrite(fd, block, block_size, offset) == -1) {
            LOGI("Write error (errno: %d)", errno);
            close(fd);
            remove(filePath.c_str());
            delete[] block;
            return -1;
        }

        if (i % progress_step == 0 && i > 0) {
            float progress = static_cast<float>(i) / iterations;
            update_progress(env, activity, updateProgressMethod, progress);
        }
    }

    fdatasync(fd);
    asm volatile("" : : : "memory");
    update_progress(env, activity, updateProgressMethod, 1.0f);
    auto end = std::chrono::high_resolution_clock::now();
    long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();


    close(fd);
    remove(filePath.c_str());
    // Releasing allocated memory
    delete[] block;
    return duration_ms;
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRomRandomReadBenchmark(
        JNIEnv* env, jobject /*thiz*/, jobject activity) {

    const size_t file_size = 500 * 1024 * 1024; // 500 MB
    const int block_size = 4 * 1024; // 4 KB
    const int iterations = file_size / block_size;
    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    std::string filePath = get_files_dir_path(env, activity) + "/mb_random_read_test.bin";

    if (!create_random_test_file(filePath, file_size)) {
        return -1;
    }

    int fd_write = open(filePath.c_str(), O_WRONLY);
    if (fd_write >= 0) {
        fdatasync(fd_write);
        close(fd_write);
    }

    int fd = -1;
    uint8_t* block = nullptr;

    fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGI("Failed to open file in O_RDONLY mode (errno: %d)", errno);
        remove(filePath.c_str());
        return -1;
    }

    block = new uint8_t[block_size];
    if (!block) {
        LOGI("Failed to allocate regular memory");
        close(fd);
        remove(filePath.c_str());
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist_offset(0, iterations - 1);

    const int total_progress_updates = 100;
    const int64_t progress_step = iterations / total_progress_updates;

    uint64_t checksum = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        size_t offset = dist_offset(gen) * block_size;

        if (pread(fd, block, block_size, offset) == -1) {
            LOGI("Read error (errno: %d)", errno);
            close(fd);
            remove(filePath.c_str());
            delete[] block;
            return -1;
        }

        for (int j = 0; j < block_size; j++) {
            checksum += block[j];
        }

        if (i % progress_step == 0 && i > 0) {
            float progress = static_cast<float>(i) / iterations;
            update_progress(env, activity, updateProgressMethod, progress);
        }
    }

    asm volatile("" : : : "memory");
    update_progress(env, activity, updateProgressMethod, 1.0f);
    auto end = std::chrono::high_resolution_clock::now();
    long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    (void)checksum;
    close(fd);
    remove(filePath.c_str());
    // Releasing allocated memory
    delete[] block;
    return duration_ms;
}

}