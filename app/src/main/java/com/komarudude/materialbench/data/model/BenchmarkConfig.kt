package com.komarudude.materialbench.data.model

import com.komarudude.materialbench.R

object BenchmarkConfig {
    // Test category (for displays in 4 cards)
    enum class TestCategory { CPU, GPU, MEM, AI }

    // One subphase (step)
    data class TestStep(val id: String, val label: Int, val category: TestCategory)

    val testSteps = listOf(
        // CPU
        TestStep("cpu_math_single", R.string.cpu_math_single, TestCategory.CPU),
        TestStep("cpu_math_multi", R.string.cpu_math_multi, TestCategory.CPU),
        TestStep("cpu_crypto_single", R.string.cpu_crypto_single, TestCategory.CPU),
        TestStep("cpu_crypto_multi", R.string.cpu_crypto_multi, TestCategory.CPU),
        TestStep("cpu_vector_math", R.string.cpu_vector_math, TestCategory.CPU),
        TestStep("cpu_compress", R.string.cpu_compress, TestCategory.CPU),

        // GPU
        TestStep("gpu_gemm", R.string.vulkan_compute_gemm, TestCategory.GPU),
        TestStep("gpu_rt", R.string.gpu_rt, TestCategory.GPU),

        // MEM
        TestStep("ram_seq_write", R.string.ram_seq_write, TestCategory.MEM),
        TestStep("ram_seq_read", R.string.ram_seq_read, TestCategory.MEM),
        TestStep("rom_rand_ops", R.string.rom_rand_ops, TestCategory.MEM),
        TestStep("rom_seq_write", R.string.rom_seq_write, TestCategory.MEM),
        TestStep("rom_seq_read", R.string.rom_seq_read, TestCategory.MEM),

        // AI
        TestStep("ai_litert_cpu", R.string.ai_litert_cpu, TestCategory.AI),
        TestStep("ai_litert_gpu", R.string.ai_litert_gpu, TestCategory.AI)
    )
}