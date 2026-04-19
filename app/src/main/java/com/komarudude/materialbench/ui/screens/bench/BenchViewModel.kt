package com.komarudude.materialbench.ui.screens.bench

import android.app.Application
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import android.widget.Toast
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.komarudude.materialbench.BenchScores
import com.komarudude.materialbench.R
import com.komarudude.materialbench.data.model.BenchmarkConfig.TestCategory
import com.komarudude.materialbench.data.model.BenchmarkConfig.testSteps
import com.komarudude.materialbench.data.native.BenchmarkProgressCallback
import com.komarudude.materialbench.data.native.NativeLib
import com.komarudude.materialbench.utils.IntegrityChecker
import com.komarudude.materialbench.utils.MobileNetV4Classifier
import com.komarudude.materialbench.utils.RetrofitClient
import com.komarudude.materialbench.utils.ScoreRequest
import com.komarudude.materialbench.utils.loadAndPrepareImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private const val LITE_RT_SCORE_SCALE = 1_000_000_0

class BenchViewModel(
    application: Application
) : AndroidViewModel(application) {

    private val app = getApplication<Application>()
    private val cpuClassifier = MobileNetV4Classifier(
        app,
        "mobilenetv4_conv_large.e600_r384_in1k_float16.tflite"
    )
    private val gpuClassifier = MobileNetV4Classifier(
        app,
        "mobilenetv4_conv_large.e600_r384_in1k_float16.tflite"
    )

    var currentStepIndex by mutableIntStateOf(-1)
    var currentStepProgress by mutableFloatStateOf(0f)
    var finished by mutableStateOf(false)
    val stepScores = mutableStateMapOf<String, Int>()

    var showIntegrityDialog by mutableStateOf(false)
    val resultChannel = Channel<Int>(Channel.CONFLATED)

    private val pm = app.packageManager
    private val hasVulkanCompute = pm.hasSystemFeature("android.hardware.vulkan.compute")
    private val isSupportVulkanRT = NativeLib.hasVulkanRt()

    private val callback = object : BenchmarkProgressCallback {
        override fun onProgressUpdate(progress: Float) {
            currentStepProgress = progress
        }
    }

    val progress by derivedStateOf {
        if (currentStepIndex < 0) {
            if (finished) 1f else 0f
        } else (currentStepIndex + currentStepProgress) / testSteps.size.toFloat()
    }

    val categoryScores by derivedStateOf {
        TestCategory.entries.associateWith { cat ->
            testSteps.filter { it.category == cat }.mapNotNull { stepScores[it.id] }.sum()
        }
    }

    val categoryProgressPercent by derivedStateOf {
        TestCategory.entries.associateWith { cat ->
            val stepsForCat = testSteps.filter { it.category == cat }
            val totalForCat = stepsForCat.size
            if (totalForCat == 0) 0
            else {
                val completed = stepsForCat.count { stepScores.containsKey(it.id) }
                val inProgress = if (currentStepIndex >= 0 && currentStepIndex < testSteps.size && testSteps[currentStepIndex].category == cat) currentStepProgress else 0f
                val frac = (completed.toFloat() + inProgress) / totalForCat.toFloat()
                (frac.coerceIn(0f, 1f) * 100).toInt()
            }
        }
    }

    fun reset() {
        currentStepIndex = -1
        currentStepProgress = 0f
        finished = false
        stepScores.clear()
        showIntegrityDialog = false
    }

    fun startBenchmarks(onLaunchUE: () -> Unit) {
        if (currentStepIndex != -1 || finished) return

        viewModelScope.launch(Dispatchers.Default) {
            if (!isSupportVulkanRT && hasVulkanCompute) {
                withContext(Dispatchers.Main) {
                    Toast.makeText(app, R.string.device_incomplete_feature, Toast.LENGTH_LONG).show()
                }
            }

            val filesDir = app.filesDir.absolutePath

            for (i in testSteps.indices) {
                currentStepIndex = i
                val step = testSteps[i]
                currentStepProgress = 0f

                val score = when (step.id) {
                    "cpu_math_single" -> runNativeBenchmark({
                        NativeLib.nativeRunCpuMathSingleCoreBenchmark(callback)
                    }, 100_000_000)

                    "cpu_math_multi" -> runNativeBenchmark({
                        NativeLib.nativeRunCpuMathMultiCoreBenchmark(callback)
                    }, 100_000_000)

                    "cpu_crypto_single" -> runNativeBenchmark({
                        NativeLib.nativeRunCpuCryptoSingleCoreBenchmark(callback)
                    }, 100_000_000)

                    "cpu_crypto_multi" -> runNativeBenchmark({
                        NativeLib.nativeRunCpuCryptoMultiCoreBenchmark(callback)
                    }, 100_000_000)

                    "cpu_vector_math" -> runNativeBenchmark({
                        NativeLib.nativeRunCpuVectorMathBenchmark(callback)
                    }, 100_000_000)

                    "cpu_compress" -> runNativeBenchmark({
                        NativeLib.nativeRunCpuCompressBenchmark(app.assets, callback)
                    }, 100_000_000)

                    "ram_seq_write" -> runNativeBenchmark({
                        NativeLib.nativeRunRamSequentialWriteBenchmark(callback)
                    }, 10_000_000)

                    "ram_seq_read" -> runNativeBenchmark({
                        NativeLib.nativeRunRamSequentialReadBenchmark(callback)
                    }, 10_000_000)

                    "rom_rand_ops" -> runNativeBenchmark({
                        NativeLib.nativeRunRomMixedRandomBenchmark(filesDir, callback)
                    }, 10_000_000)

                    "rom_seq_write" -> runNativeBenchmark({
                        NativeLib.nativeRunRomSequentialWriteBenchmark(filesDir, callback)
                    }, 10_000_000)

                    "rom_seq_read" -> runNativeBenchmark({
                        NativeLib.nativeRunRomSequentialReadBenchmark(filesDir, callback)
                    }, 10_000_000)

                    "gpu_gemm" -> {
                        if (hasVulkanCompute) {
                            runNativeBenchmark({ NativeLib.nativeRunVulkanGEMMBenchmark(callback) }, 100_000_000)
                        } else 0
                    }

                    "gpu_rt" -> {
                        if (isSupportVulkanRT) {
                            if (!IntegrityChecker.isCompanionTrustworthy(app)) {
                                showIntegrityDialog = true
                                0
                            } else {
                                withContext(Dispatchers.Main) {
                                    onLaunchUE()
                                }
                                val score = resultChannel.receive()
                                score * 10
                            }
                        } else 0
                    }

                    "ai_litert_cpu" -> runLiteRtBenchmark(callback, cpuClassifier)
                    "ai_litert_gpu" -> runLiteRtBenchmark(callback, gpuClassifier)
                    else -> 0
                }
                stepScores[step.id] = score
                withContext(Dispatchers.IO) {
                    BenchScores.saveScore(app, step.id, score)
                }
                currentStepProgress = 1f
            }

            finished = true
            currentStepIndex = -2

            val cpuScore = categoryScores[TestCategory.CPU] ?: 0
            val gpuScore = categoryScores[TestCategory.GPU] ?: 0
            val memScore = categoryScores[TestCategory.MEM] ?: 0
            val aiScore = categoryScores[TestCategory.AI] ?: 0
            val overallScore = cpuScore + gpuScore + memScore + aiScore

            withContext(Dispatchers.IO) {
                BenchScores.saveScore(app, "CPU Benchmark", cpuScore)
                BenchScores.saveScore(app, "GPU Benchmark", gpuScore)
                BenchScores.saveScore(app, "Memory Test", memScore)
                BenchScores.saveScore(app, "AI Test", aiScore)
                BenchScores.saveScore(app, "overall_score", overallScore)

                try {
                    val versionCode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        app.packageManager.getPackageInfo(app.packageName, PackageManager.PackageInfoFlags.of(0)).longVersionCode
                    } else {
                        @Suppress("DEPRECATION")
                        app.packageManager.getPackageInfo(app.packageName, 0).longVersionCode
                    }
                    RetrofitClient.apiService.submit(ScoreRequest(overallScore, versionCode))
                } catch (e: Exception) {
                    Log.e("BenchViewModel", "Error submitting score: ${e.message}")
                }
            }
        }
    }

    private suspend fun runNativeBenchmark(call: suspend () -> Long, scale: Int): Int {
        return try {
            val timeMillis = withContext(Dispatchers.Default) { call() }
            if (timeMillis <= 0) 0 else (scale / timeMillis).toInt()
        } catch (_: Exception) {
            0
        }
    }

    private suspend fun runLiteRtBenchmark(callback: BenchmarkProgressCallback, classifier: MobileNetV4Classifier): Int {
        classifier.initialize()
        val imagesCount = 100
        val allTestPaths = (1..imagesCount).map { "images/$it.jpg" }
        val totalRuns = allTestPaths.size

        val imageMap = withContext(Dispatchers.IO) {
            allTestPaths.mapNotNull { path ->
                loadAndPrepareImage(app, path)?.let { path to it }
            }.toMap()
        }

        if (imageMap.size != totalRuns) {
            return 0
        }

        var totalTimeNs = 0L
        try {
            for ((index, path) in allTestPaths.withIndex()) {
                val imagePixels = imageMap[path]!!
                val startTime = System.nanoTime()
                classifier.runInference(imagePixels, topKCount = 5)
                totalTimeNs += (System.nanoTime() - startTime)
                callback.onProgressUpdate((index + 1).toFloat() / totalRuns)
            }
        } catch (_: Exception) {
            return 0
        } finally {
            classifier.close()
        }

        val averageTimeMs = totalTimeNs.toDouble() / totalRuns / 1_000_000.0
        return if (averageTimeMs <= 0.0) 0 else (LITE_RT_SCORE_SCALE / averageTimeMs).toInt()
    }

    override fun onCleared() {
        super.onCleared()
        cpuClassifier.close()
        gpuClassifier.close()
    }
}
