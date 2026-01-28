#include "utils.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sys/syscall.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <vulkan/vulkan.h>

JavaVM* g_vm = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

int get_biggest_core() {
    int best_core = 0;
    long best_freq = -1;
    for (int cpu = 0;; cpu++) {
        std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/cpuinfo_max_freq";
        std::ifstream f(path);
        if (!f.is_open()) break;
        long freq;
        f >> freq;
        if (freq > best_freq) {
            best_freq = freq;
            best_core = cpu;
        }
    }
    return best_core;
}

std::vector<int> get_performance_cores() {
    struct Core {
        int id;
        long max_freq;
    };

    std::vector<Core> cores;
    long min_max_freq = -1;

    for (int cpu = 0;; cpu++) {
        std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/cpuinfo_max_freq";
        std::ifstream f(path);
        if (!f.is_open()) break;

        long freq;
        f >> freq;
        cores.push_back({cpu, freq});

        if (min_max_freq == -1 || freq < min_max_freq) {
            min_max_freq = freq;
        }
    }

    std::vector<int> result;
    for (const auto& core : cores) {
        if (core.max_freq > min_max_freq) {
            result.push_back(core.id);
        }
    }

    if (result.empty()) {
        for (const auto& core : cores) result.push_back(core.id);
    }

    return result;
}

void pin_to_core(int core_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    sched_setaffinity(0, sizeof(cpu_set_t), &set);
}

void update_progress(JNIEnv* env, jobject activity, jmethodID methodId, float progress) {
    if (env && activity && methodId) {
        env->CallVoidMethod(activity, methodId, progress);
    }
}

std::string get_files_dir_path(JNIEnv* env, jobject activity) {
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getFilesDirMethod = env->GetMethodID(activityClass, "getFilesDir", "()Ljava/io/File;");
    jobject filesDir = env->CallObjectMethod(activity, getFilesDirMethod);

    jclass fileClass = env->FindClass("java/io/File");
    jmethodID getPathMethod = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
    auto pathStr = (jstring)env->CallObjectMethod(filesDir, getPathMethod);

    const char* pathChars = env->GetStringUTFChars(pathStr, nullptr);
    std::string path = std::string(pathChars);
    env->ReleaseStringUTFChars(pathStr, pathChars);

    return path;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_komarudude_materialbench_ui_BenchActivity_hasVulkanRt(
        JNIEnv *env, jobject thiz) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VKRTChecker";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 0;

    VkInstance instance;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    bool rtSupported = false;
    const char *targetExtension = "VK_KHR_ray_query";

    for (const auto &device: devices) {
        uint32_t extensionCount = 0;

        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        if (extensionCount == 0) continue;

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);

        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                                 availableExtensions.data()) != VK_SUCCESS) {
            continue;
        }

        for (const auto &extension: availableExtensions) {
            if (std::strcmp(targetExtension, extension.extensionName) == 0) {
                rtSupported = true;
                break;
            }
        }

        if (rtSupported) {
            break;
        }
    }

    vkDestroyInstance(instance, nullptr);
    return rtSupported;
}
}