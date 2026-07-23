#include <jni.h>

#include "vulkan/compute_service.h"

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeRunVulkanGEMMBenchmark(
    JNIEnv* env, jobject /* thiz */, jobject callback) {
    jclass callbackClass = env->GetObjectClass(callback);
    jmethodID updateProgressMethod = env->GetMethodID(
        callbackClass, "onProgressUpdate", "(F)V");
    env->DeleteLocalRef(callbackClass);

    return static_cast<jlong>(
        materialbench::vulkan::ComputeService::instance().runBenchmark(
            [env, callback, updateProgressMethod](float progress) {
                env->CallVoidMethod(callback, updateProgressMethod, progress);
                if (env->ExceptionCheck()) env->ExceptionClear();
            }));
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeStartGpuStress(
    JNIEnv* /* env */, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().startStress();
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeStopGpuStress(
    JNIEnv* /* env */, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().stopStress();
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeCleanup(
    JNIEnv* /* env */, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().cleanup();
}

JNIEXPORT void JNICALL
Java_com_komarudude_materialbench_data_native_NativeLib_nativeBenchCleanup(
    JNIEnv* /* env */, jobject /* thiz */) {
    materialbench::vulkan::ComputeService::instance().cleanup();
}

}  // extern "C"
