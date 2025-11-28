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
#include <sys/system_properties.h>
#include "utils.h"

extern "C" {

bool create_test_file(const std::string& path, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    const size_t buffer_size = 64 * 1024;
    std::vector<char> buffer(buffer_size);

    for (size_t i = 0; i < size; i += buffer_size) {
        size_t chunk_size = std::min(buffer_size, size - i);
        for (size_t j = 0; j < chunk_size; j++) {
            buffer[j] = static_cast<char>((i + j) & 0xFF);
        }
        file.write(buffer.data(), chunk_size);
        if (!file) return false;
    }
    file.close();
    return true;
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRomSequentialWriteBenchmark(
        JNIEnv* env, jobject /*thiz*/, jobject activity) {

    const size_t file_size = 500 * 1024 * 1024; // 500 MB
    const int block_size = 4 * 1024 * 1024; // 4 MB
    const int iterations = file_size / block_size;
    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    std::string filePath = get_files_dir_path(env, activity) + "/mb_seq_write_test.bin";

    std::ofstream pre_alloc_file(filePath, std::ios::binary | std::ios::trunc);
    if (!pre_alloc_file) {
        return -1;
    }
    std::vector<char> empty_block(block_size, 0);
    for (int i = 0; i < iterations; ++i) {
        pre_alloc_file.write(empty_block.data(), block_size);
    }
    pre_alloc_file.close();

    int fd = open(filePath.c_str(), O_WRONLY);
    if (fd < 0) {
        remove(filePath.c_str());
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dist_value(0, 255);

    const int total_progress_updates = 100;
    const int64_t progress_step = iterations / total_progress_updates;

    std::vector<uint8_t> block(block_size);
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        for (int j = 0; j < block_size; j++) {
            block[j] = static_cast<uint8_t>(dist_value(gen));
        }

        if (write(fd, block.data(), block_size) == -1) {
            close(fd);
            remove(filePath.c_str());
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

    remove(filePath.c_str());
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRomSequentialReadBenchmark(
        JNIEnv* env, jobject /*thiz*/, jobject activity) {

    const size_t file_size = 500 * 1024 * 1024; // 500 MB
    const int block_size = 4 * 1024 * 1024; // 4 MB
    const int iterations = file_size / block_size;
    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    std::string filePath = get_files_dir_path(env, activity) + "/mb_seq_read_test.bin";

    if (!create_test_file(filePath, file_size)) {
        return -1;
    }

    int fd_write = open(filePath.c_str(), O_WRONLY);
    if (fd_write >= 0) {
        fdatasync(fd_write);
        close(fd_write);
    }

    int fd = -1;
    void* aligned_block_ptr = nullptr;
    uint8_t* block = nullptr;

    fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGI("Failed to open file in O_RDONLY mode either");
        remove(filePath.c_str());
        return -1;
    }

    if (posix_memalign(&aligned_block_ptr, 4096, block_size) != 0) {
        LOGI("Failed to allocate aligned memory");
        close(fd);
        remove(filePath.c_str());
        return -1;
    }
    block = static_cast<uint8_t*>(aligned_block_ptr);

    const int total_progress_updates = 100;
    const int64_t progress_step = iterations / total_progress_updates;

    // Make checksum volatile to prevent optimization
    volatile uint64_t checksum = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        ssize_t bytes_read = read(fd, block, block_size);
        if (bytes_read == -1) {
            LOGI("Read error ");
            close(fd);
            free(aligned_block_ptr);
            remove(filePath.c_str());
            return -1;
        }

        // Explicitly use the read data
        for (int j = 0; j < block_size; j++) {
            checksum += block[j];
        }

        // Compiler barrier
        asm volatile("" : "+r" (checksum) : : "memory");

        if (i % progress_step == 0 && i > 0) {
            float progress = static_cast<float>(i) / iterations;
            update_progress(env, activity, updateProgressMethod, progress);
        }
    }

    // Another barrier and explicit use of checksum
    asm volatile("" : "+r" (checksum) : : "memory");

    // Explicitly use the final checksum value
    if (checksum == 0) {
        LOGI("Checksum is zero - this should never happen");
    }

    update_progress(env, activity, updateProgressMethod, 1.0f);
    auto end = std::chrono::high_resolution_clock::now();
    long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    close(fd);
    free(aligned_block_ptr);
    remove(filePath.c_str());
    return duration_ms;
}

}