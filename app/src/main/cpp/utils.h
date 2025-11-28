#pragma once
#include <jni.h>
#include <string>

extern JavaVM* g_vm;

int get_biggest_core();
void pin_to_core(int core_id);
void update_progress(JNIEnv* env, jobject activity, jmethodID methodId, float progress);
std::string get_files_dir_path(JNIEnv* env, jobject activity);

#ifndef LOG_UTILS_H
#define LOG_UTILS_H

#include <android/log.h>

#define LOG_TAG "MaterialBench_NDK"

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)

#endif