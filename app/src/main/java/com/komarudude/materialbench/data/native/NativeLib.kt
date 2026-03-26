package com.komarudude.materialbench.data.native

import androidx.annotation.Keep

interface BenchmarkProgressCallback {
    @Keep
    fun onProgressUpdate(progress: Float)
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
    external fun nativeRunRomMixedRandomBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunRomSequentialWriteBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunRomSequentialReadBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunCpuCryptoSingleCoreBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunCpuCryptoMultiCoreBenchmark(callback: BenchmarkProgressCallback): Long
    external fun nativeRunVulkanGEMMBenchmark(callback: BenchmarkProgressCallback): Long
    external fun hasVulkanRt(): Boolean
    external fun nativeStartCpuStress()
    external fun nativeStopCpuStress()
    external fun nativeStartGpuStress()
    external fun nativeStopGpuStress()
    external fun nativeCleanup()
    external fun nativeBenchCleanup()
}