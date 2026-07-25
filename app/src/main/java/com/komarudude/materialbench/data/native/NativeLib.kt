package com.komarudude.materialbench.data.native

import androidx.annotation.Keep

interface BenchmarkProgressCallback {
    @Keep
    fun onProgressUpdate(progress: Float)
}

interface StressDataCallback {
    @Keep
    fun onStressData(timestamp: Long, cpuPerf: Int?, gpuMflops: Int?)
}

object NativeLib {
    init {
        try {
            System.loadLibrary("materialbench")
        } catch (_: UnsatisfiedLinkError) {
            // Ignore in Preview
        }
    }

    external fun nativeRunCpuMathSingleCoreBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunCpuMathMultiCoreBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunCpuVectorMathBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunRamSequentialWriteBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunRamSequentialReadBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunRomMixedRandomBenchmark(filesDir: String, callback: BenchmarkProgressCallback): Long
    external fun nativeRunRomSequentialWriteBenchmark(filesDir: String, callback: BenchmarkProgressCallback): Long
    external fun nativeRunRomSequentialReadBenchmark(filesDir: String, callback: BenchmarkProgressCallback): Long
    external fun nativeRunCpuCryptoBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunCpuCompressBenchmark(assetManager: android.content.res.AssetManager, callback: BenchmarkProgressCallback): Long
    external fun nativeRunVulkanGEMMBenchmark(callback: BenchmarkProgressCallback): Long
    external fun hasVulkanRt(): Boolean
    external fun nativeStartCpuStress(callback: StressDataCallback)
    external fun nativeStopCpuStress()
    external fun nativeStartGpuStress(callback: StressDataCallback)
    external fun nativeStopGpuStress()
    external fun nativeCleanup()
    external fun nativeBenchCleanup()
}