#include <jni.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "utils.h"

// Create file with random data
bool create_random_test_file(const std::string& path, size_t size) {
    const size_t buffer_size = 64 * 1024;
    std::vector<uint8_t> buffer(buffer_size);

    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        LOGI("create_random_test_file: open failed errno=%d", errno);
        return false;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dist_value(0, 255);

    size_t written_total = 0;
    while (written_total < size) {
        size_t chunk_size = std::min(buffer_size, size - written_total);
        for (size_t j = 0; j < chunk_size; ++j) {
            buffer[j] = static_cast<uint8_t>(dist_value(gen));
        }

        ssize_t w = write(fd, buffer.data(), chunk_size);
        if (w < 0) {
            LOGI("create_random_test_file: write failed errno=%d", errno);
            close(fd);
            return false;
        }
        written_total += static_cast<size_t>(w);

        if (fsync(fd) != 0) {
            LOGW("create_random_test_file: fsync failed errno=%d", errno);
        }
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    }

    close(fd);
    return true;
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_nativeRunRomMixedRandomBenchmark(
        JNIEnv* env, jobject /*thiz*/, jobject activity) {

    const size_t file_size = 500ULL * 1024ULL * 1024ULL; // 500 MB
    const int block_size = 64 * 1024; // 64 KB
    const int64_t iterations = static_cast<int64_t>(file_size / block_size);

    int big_core = get_biggest_core();
    pin_to_core(big_core);

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID updateProgressMethod = env->GetMethodID(activityClass, "updateBenchmarkProgress", "(F)V");

    std::string filePath = get_files_dir_path(env, activity) + "/mb_mixed_rw_test.bin";

    // Create file with random data
    if (!create_random_test_file(filePath, file_size)) {
        return -1;
    }

    // Open file for write/read
    int fd = open(filePath.c_str(), O_RDWR);
    if (fd < 0) {
        LOGI("Failed to open file in O_RDWR mode (errno: %d)", errno);
        remove(filePath.c_str());
        return -1;
    }

    auto* block = new (std::nothrow) uint8_t[block_size];
    if (!block) {
        LOGI("Failed to allocate regular memory");
        close(fd);
        remove(filePath.c_str());
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int64_t> dist_offset(0, iterations - 1);
    std::uniform_int_distribution<uint16_t> dist_value(0, 255);

    const int total_progress_updates = 100;
    const int64_t progress_step = std::max<int64_t>(1, iterations / total_progress_updates);

    volatile uint64_t checksum = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (int64_t i = 0; i < iterations; ++i) {
        // Every time choose random block for read
        int64_t read_block = dist_offset(gen);
        off_t read_offset = read_block * static_cast<off_t>(block_size);
        ssize_t r = pread(fd, block, block_size, read_offset);
        if (r == -1 || r != block_size) {
            LOGI("Read error (errno: %d) read=%zd expected=%d", errno, r, block_size);
            close(fd);
            remove(filePath.c_str());
            delete[] block;
            return -1;
        }

        for (int j = 0; j < block_size; ++j) {
            checksum += block[j];
        }

        // Choose another random block for write
        int64_t write_block = dist_offset(gen);
        off_t write_offset = write_block * static_cast<off_t>(block_size);
        for (int j = 0; j < block_size; ++j) {
            block[j] = static_cast<uint8_t>(dist_value(gen));
        }

        ssize_t w = pwrite(fd, block, block_size, write_offset);
        if (w == -1 || w != block_size) {
            LOGI("Write error (errno: %d) wrote=%zd expected=%d", errno, w, block_size);
            close(fd);
            remove(filePath.c_str());
            delete[] block;
            return -1;
        }

        if ((i % progress_step) == 0 && i > 0) {
            float progress = static_cast<float>(i) / static_cast<float>(iterations);
            update_progress(env, activity, updateProgressMethod, progress);
        }
    }

    // Force sync data on disk
    if (fdatasync(fd) != 0) {
        LOGI("fdatasync failed (errno: %d)", errno);
    }

    asm volatile("" : : "r"(checksum) : "memory");
    update_progress(env, activity, updateProgressMethod, 1.0f);

    auto end = std::chrono::high_resolution_clock::now();
    long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    LOGV("Mixed RW Checksum: %" PRIu64, checksum);
    close(fd);
    remove(filePath.c_str());
    delete[] block;
    return duration_ms;
}

}
