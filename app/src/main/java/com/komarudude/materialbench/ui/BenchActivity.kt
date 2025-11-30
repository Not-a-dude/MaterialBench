package com.komarudude.materialbench.ui

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.annotation.Keep
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.runtime.*
import androidx.compose.ui.res.stringResource
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import android.content.pm.PackageManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import com.komarudude.materialbench.ui.theme.MaterialBenchTheme
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.view.WindowManager
import android.util.Log
import android.widget.Toast
import com.komarudude.materialbench.BenchScores
import com.komarudude.materialbench.utils.MobileNetV4Classifier
import com.komarudude.materialbench.R
import com.komarudude.materialbench.utils.RetrofitClient
import com.komarudude.materialbench.utils.ScoreRequest
import com.komarudude.materialbench.utils.loadAndPrepareImage
import kotlinx.coroutines.channels.Channel

private const val LITE_RT_SCORE_SCALE = 1_000_000_0

class BenchActivity : ComponentActivity() {
    var onProgressUpdate: ((Float) -> Unit)? = null
    lateinit var classifier: MobileNetV4Classifier
    @Keep
    fun updateBenchmarkProgress(progress: Float) {
        onProgressUpdate?.invoke(progress)
    }


    external fun nativeRunCpuMathSingleCoreBenchmark(activity: BenchActivity): Long
    external fun nativeRunCpuMathMultiCoreBenchmark(activity: BenchActivity): Long
    external fun nativeRunRamSequentialWriteBenchmark(activity: BenchActivity): Long
    external fun nativeRunRamSequentialReadBenchmark(activity: BenchActivity): Long
    external fun nativeRunRomMixedRandomBenchmark(activity: BenchActivity): Long
    external fun nativeRunRomSequentialWriteBenchmark(activity: BenchActivity): Long
    external fun nativeRunRomSequentialReadBenchmark(activity: BenchActivity): Long
    external fun nativeRunCpuCryptoSingleCoreBenchmark(activity: BenchActivity): Long
    external fun nativeRunCpuCryptoMultiCoreBenchmark(activity: BenchActivity): Long
    external fun nativeRunVulkanGEMMBenchmark(activity: BenchActivity): Long
    external fun hasVulkanRt(): Boolean
    external fun nativeBenchCleanup()

    companion object {
        init {
            try {
                System.loadLibrary("materialbench")
            } catch (_: UnsatisfiedLinkError) {
                // Ignore in Preview
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appContext = applicationContext
        classifier = MobileNetV4Classifier(
            appContext,
            "mobilenetv4_conv_large.e600_r384_in1k_float16.tflite"
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        enableEdgeToEdge()
        setContent {
            MaterialBenchTheme {
                Scaffold(
                    modifier = Modifier.fillMaxSize()
                ) { innerPadding ->
                    BenchScreen(
                        modifier = Modifier.padding(innerPadding),
                        onBackToMenu = {
                            finish()
                        },
                        activity = this
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        classifier.close()
        nativeBenchCleanup()
    }
}

// Test category (for displays in 4 cards)
enum class TestCategory { CPU, GPU, MEM, AI }

// One subphase (step)
data class TestStep(val id: String, val label: String, val category: TestCategory)

@OptIn(
    ExperimentalMaterial3ExpressiveApi::class,
    ExperimentalMaterial3Api::class
)
@Composable
fun BenchScreen(modifier: Modifier = Modifier, onBackToMenu: () -> Unit, activity: BenchActivity) {
    val context = LocalContext.current
    val resultChannel = remember { Channel<Int>(Channel.CONFLATED) }
    val ueLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val score = if (result.resultCode == Activity.RESULT_OK) {
            result.data?.getIntExtra("ue_frames", 0) ?: 0
        } else {
            0
        }
        resultChannel.trySend(score)
    }

    val isSupportVulkanRT = activity.hasVulkanRt()

    val overallProgressTitle = stringResource(R.string.overall_progress_title)
    val runningString = stringResource(R.string.running)
    val finishedString = stringResource(R.string.finished)
    val backToMenuString = stringResource(R.string.back_to_menu)
    val cpuMathSingleString = stringResource(R.string.cpu_math_single)
    val cpuMathMultiString = stringResource(R.string.cpu_math_multi)
    val ramSeqWrite = stringResource(R.string.ram_seq_write)
    val ramSeqRead = stringResource(R.string.ram_seq_read)
    val romRandOps = stringResource(R.string.rom_rand_ops)
    val romSeqWrite = stringResource(R.string.rom_seq_write)
    val romSeqRead = stringResource(R.string.rom_seq_read)
    val cpuCryptoSingle = stringResource(R.string.cpu_crypto_single)
    val cpuCryptoMulti = stringResource(R.string.cpu_crypto_multi)
    val gpuVulkanComputeGemm = stringResource(R.string.vulkan_compute_gemm)
    val gpuRT = stringResource(R.string.gpu_rt)
    val aiLiteRT = stringResource(R.string.ai_litert)
    val testSteps = remember {
        listOf(
            // CPU
            TestStep("cpu_math_single", cpuMathSingleString, TestCategory.CPU),
            TestStep("cpu_math_multi", cpuMathMultiString, TestCategory.CPU),
            TestStep("cpu_crypto_single", cpuCryptoSingle, TestCategory.CPU),
            TestStep("cpu_crypto_multi", cpuCryptoMulti, TestCategory.CPU),

            // GPU
            TestStep("gpu_gemm", gpuVulkanComputeGemm, TestCategory.GPU),
            TestStep("gpu_rt", gpuRT, TestCategory.GPU),

            // MEM
            TestStep("ram_seq_write", ramSeqWrite, TestCategory.MEM),
            TestStep("ram_seq_read", ramSeqRead, TestCategory.MEM),
            TestStep("rom_rand_ops", romRandOps, TestCategory.MEM),
            TestStep("rom_seq_write", romSeqWrite, TestCategory.MEM),
            TestStep("rom_seq_read", romSeqRead, TestCategory.MEM),

            // AI
            TestStep("ai_litert", aiLiteRT, TestCategory.AI)
        )
    }

    val pm: PackageManager = context.packageManager
    val hasVulkanCompute = pm.hasSystemFeature("android.hardware.vulkan.compute")

    val totalSteps = testSteps.size

    var currentStepIndex by remember { mutableIntStateOf(-1) }
    var finished by remember { mutableStateOf(false) }

    var currentStepProgress by remember { mutableFloatStateOf(0f) }

    val progress by remember {
        derivedStateOf {
            if (currentStepIndex < 0) 0f
            else (currentStepIndex + currentStepProgress) / totalSteps.toFloat()
        }
    }

    val stepScores = remember { mutableStateMapOf<String, Int>() }

    val categoryScores by remember {
        derivedStateOf {
            TestCategory.entries.associateWith { cat ->
                val scores = testSteps.filter { it.category == cat }.mapNotNull { stepScores[it.id] }
                scores.sum()
            }
        }
    }

    val categoryProgressPercent by remember {
        derivedStateOf {
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
    }

    LaunchedEffect(Unit) {
        activity.onProgressUpdate = { newProgress ->
            currentStepProgress = newProgress
        }

        if (!activity.hasVulkanRt() && hasVulkanCompute) {
            Toast.makeText(context, R.string.device_incomplete_feature, Toast.LENGTH_LONG).show()
            Log.w("MaterialBench", context.getString(R.string.device_incomplete_feature))
        }

        delay(600)
        for (i in testSteps.indices) {
            currentStepIndex = i
            val step = testSteps[i]
            currentStepProgress = 0f // Reset step progress before starting new

            var generatedScore: Int

            try {
                generatedScore = when (step.id) {
                    "cpu_math_single" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunCpuMathSingleCoreBenchmark(activity) },
                            scale = 100_000_000
                        )
                    }
                    "cpu_math_multi" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunCpuMathMultiCoreBenchmark(activity) },
                            scale = 100_000_000
                        )
                    }
                    "cpu_crypto_single" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunCpuCryptoSingleCoreBenchmark(activity) },
                            scale = 100_000_000
                        )
                    }
                    "cpu_crypto_multi" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunCpuCryptoMultiCoreBenchmark(activity) },
                            scale = 100_000_000
                        )
                    }
                    "ram_seq_write" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunRamSequentialWriteBenchmark(activity) },
                            scale = 10_000_000
                        )
                    }
                    "ram_seq_read" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunRamSequentialReadBenchmark(activity) },
                            scale = 10_000_000
                        )
                    }
                    "rom_rand_ops" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunRomMixedRandomBenchmark(activity) },
                            scale = 10_000_000
                        )
                    }
                    "rom_seq_write" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunRomSequentialWriteBenchmark(activity) },
                            scale = 10_000_000
                        )
                    }
                    "rom_seq_read" -> {
                        runNativeBenchmark(
                            call = { activity.nativeRunRomSequentialReadBenchmark(activity) },
                            scale = 10_000_000
                        )
                    }
                    "gpu_gemm" -> {
                        if (hasVulkanCompute) {
                            runNativeBenchmark(
                                call = { activity.nativeRunVulkanGEMMBenchmark(activity) },
                                scale = 100_000_000
                            )
                        } else {
                            0
                        }
                    }
                    "gpu_rt" -> {
                        if (isSupportVulkanRT) {
                            val uePackageName = "com.komarudude.materialbench.rttest" // Замените если форкаете и у вас своё имя пакета
                            val ueActivityName = "com.epicgames.unreal.GameActivity"
                            Log.i("MB_RT", "Starting benchmark activity...")

                            val intent = Intent().apply {
                                setClassName(uePackageName, ueActivityName)
                            }
                            ueLauncher.launch(intent)

                            val score = resultChannel.receive()
                            Log.i("MB_RT", "Benchmark activity closed. Score: $score")
                            score * 10
                        } else {
                            Toast.makeText(context, R.string.device_not_supported, Toast.LENGTH_SHORT).show()
                            Log.w("MB_RT", "Device not support RT test")
                            0
                        }
                    }
                    "ai_litert" -> {
                        runLiteRtBenchmark(activity)
                    }
                    else -> {
                        val simulatedStepDuration = 1000L
                        val score = 0

                        // Simulate progress with regular updates
                        val numProgressUpdates = 100
                        for (progressTick in 1..numProgressUpdates) {
                            delay(simulatedStepDuration / numProgressUpdates)
                            currentStepProgress = progressTick.toFloat() / numProgressUpdates.toFloat()
                        }
                        score
                    }
                }
            } catch (_: Exception) {
                generatedScore = (1)
            }

            stepScores[step.id] = generatedScore
            withContext(Dispatchers.IO) {
                BenchScores.saveScore(activity, step.id, generatedScore)
            }
            currentStepProgress = 1f // Noting step as completed
        }

        currentStepIndex = -2
        finished = true

        val cpuScore = categoryScores[TestCategory.CPU] ?: 0
        val gpuScore = categoryScores[TestCategory.GPU] ?: 0
        val memScore = categoryScores[TestCategory.MEM] ?: 0
        val aiScore = categoryScores[TestCategory.AI] ?: 0
        val overallScore = cpuScore + gpuScore + memScore + aiScore

        withContext(Dispatchers.IO) {
            BenchScores.saveScore(activity, "CPU Benchmark", cpuScore)
            BenchScores.saveScore(activity, "GPU Benchmark", gpuScore)
            BenchScores.saveScore(activity, "Memory Test", memScore)
            BenchScores.saveScore(activity, "AI Test", aiScore)
            BenchScores.saveScore(activity, "overall_score", overallScore)
        }

        val service = RetrofitClient.apiService
        val versionCode = activity.packageManager.getPackageInfo(activity.packageName, 0).longVersionCode

        try {
            val response = service.submit(ScoreRequest(overallScore, versionCode))
            Log.d("BenchScreen", "Score submitted successfully: ${response.message}")
        } catch (e: Exception) {
            Log.e("BenchScreen", "Error submitting score: ${e.message}")
        }

        activity.onProgressUpdate = null
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Card(
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier
                .fillMaxWidth(),
            elevation = CardDefaults.cardElevation(defaultElevation = 8.dp)
        ) {
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                val animatedProgress by animateFloatAsState(targetValue = progress, label = "mainProgress")

                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier.size(150.dp)
                ) {
                    CircularWavyProgressIndicator(
                        progress = { animatedProgress },
                        modifier = Modifier.size(150.dp),
                        color = MaterialTheme.colorScheme.primary,
                        trackColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f)
                    )
                    Text(
                        text = "${(animatedProgress * 100).toInt()}%",
                        style = MaterialTheme.typography.headlineMedium
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    text = overallProgressTitle,
                    style = MaterialTheme.typography.titleMedium
                )

                if (currentStepIndex >= 0 && currentStepIndex < testSteps.size) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(text = "$runningString ${testSteps[currentStepIndex].label}")
                } else if (finished) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(text = finishedString)
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            StatCard(
                label = "CPU",
                percent = categoryProgressPercent[TestCategory.CPU] ?: 0,
                subLabel = if (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.CPU && !finished)
                    "$runningString ${testSteps[currentStepIndex].label}"
                else
                    "${categoryScores[TestCategory.CPU] ?: 0}",
                showLoading = (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.CPU && !finished),
                modifier = Modifier.fillMaxWidth()
            )

            StatCard(
                label = "GPU",
                percent = categoryProgressPercent[TestCategory.GPU] ?: 0,
                subLabel = if (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.GPU && !finished)
                    "$runningString ${testSteps[currentStepIndex].label}"
                else
                    "${categoryScores[TestCategory.GPU] ?: 0}",
                showLoading = (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.GPU && !finished),
                modifier = Modifier.fillMaxWidth()
            )

            StatCard(
                label = "Memory",
                percent = categoryProgressPercent[TestCategory.MEM] ?: 0,
                subLabel = if (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.MEM && !finished)
                    "$runningString ${testSteps[currentStepIndex].label}"
                else
                    "${categoryScores[TestCategory.MEM] ?: 0}",
                showLoading = (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.MEM && !finished),
                modifier = Modifier.fillMaxWidth()
            )

            StatCard(
                label = "AI",
                percent = categoryProgressPercent[TestCategory.AI] ?: 0,
                subLabel = if (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.AI && !finished)
                    "$runningString ${testSteps[currentStepIndex].label}"
                else
                    "${categoryScores[TestCategory.AI] ?: 0}",
                showLoading = (currentStepIndex >= 0 && testSteps[currentStepIndex].category == TestCategory.AI && !finished),
                modifier = Modifier.fillMaxWidth()
            )
        }

        if (finished) {
            Spacer(modifier = Modifier.height(16.dp))
            Button(onClick = onBackToMenu) {
                Text(backToMenuString)
            }
        }

        Spacer(modifier = Modifier.height(12.dp))
    }
}

@Composable
fun StatCard(label: String, percent: Int, subLabel: String, showLoading: Boolean, modifier: Modifier = Modifier) {
    Card(
        shape = RoundedCornerShape(12.dp),
        modifier = modifier,
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp)
    ) {
        Column(
            modifier = Modifier
                .padding(12.dp)
                .fillMaxWidth()
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    text = "${percent}%",
                    style = MaterialTheme.typography.titleLarge,
                    modifier = Modifier.width(64.dp)
                )

                Spacer(modifier = Modifier.width(8.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(text = label, style = MaterialTheme.typography.titleSmall)
                    Spacer(modifier = Modifier.height(6.dp))
                    if (showLoading) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(modifier = Modifier.size(24.dp))
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(text = subLabel, style = MaterialTheme.typography.bodyMedium)
                        }
                    } else {
                        Text(text = subLabel, style = MaterialTheme.typography.headlineSmall)
                    }
                }
            }
        }
    }
}

private suspend fun runNativeBenchmark(
    call: suspend () -> Long,
    scale: Int
): Int {
    return try {
        val timeMillis = withContext(Dispatchers.Default) { call() }
        when {
            timeMillis < 0 -> 0
            timeMillis == 0L -> Int.MAX_VALUE
            else -> (scale / timeMillis).toInt()
        }
    } catch (_: Exception) {
        0
    }
}

private suspend fun runLiteRtBenchmark(activity: BenchActivity): Int {
    activity.classifier.initialize()

    val imagesCount = 100
    val allTestPaths = (1..imagesCount)
        .map { "images/$it.jpg" }
        .toMutableList()
    val totalRuns = allTestPaths.size

    val rawImageMap: Map<String, ByteArray?> = withContext(Dispatchers.IO) {
        allTestPaths.associateWith { path -> loadAndPrepareImage(activity, path) }
    }

    val imageMap: Map<String, ByteArray> = rawImageMap.mapNotNull { (k, v) ->
        v?.let { k to it }
    }.toMap()

    if (imageMap.size != allTestPaths.size) {
        val missing = allTestPaths.filter { !imageMap.containsKey(it) }
        Log.e("MaterialBench", "Error: Failed to load ${missing.size} images: $missing")
        activity.classifier.close()
        return 0
    }

    var totalTimeNs = 0L
    val uncertThreshold = 0.30f

    try {
        for ((index, path) in allTestPaths.withIndex()) {
            val imagePixels = imageMap[path]!!

            val startTime = System.nanoTime()
            val result = withContext(Dispatchers.Default) {
                activity.classifier.runInference(imagePixels, topKCount = 5)
            }
            val timeNs = System.nanoTime() - startTime
            totalTimeNs += timeNs
            val timeMs = timeNs / 1_000_000.0

            val uncertainTag = if (result.confidence < uncertThreshold) "UNCERTAIN" else ""
            Log.d(
                "MaterialBench",
                "LiteRT RUN ${index + 1}/${totalRuns}: Path=$path, " +
                        "Time=${"%.3f".format(timeMs)}ms, Top1=${result.className} (${ "%.4f".format(result.confidence)}) $uncertainTag"
            )
            Log.d("MaterialBench", "  Top-5:")
            result.topK.forEach { e ->
                Log.d("MaterialBench", "    ${e.className}: ${"%.4f".format(e.prob)}")
            }

            activity.onProgressUpdate?.invoke(
                (index + 1).toFloat() / totalRuns.toFloat()
            )
        }
    } catch (e: Exception) {
        Log.e("MaterialBench", "LiteRT benchmark completed with an error: ${e.message}")
        e.printStackTrace()
        return 0
    } finally {
        activity.classifier.close()
    }

    val averageTimeMs = if (totalRuns > 0) totalTimeNs.toDouble() / totalRuns.toDouble() / 1_000_000.0 else 0.0
    val score = if (averageTimeMs <= 0.0) 0 else (LITE_RT_SCORE_SCALE / averageTimeMs).toInt()

    Log.i("MaterialBench", "LiteRT benchmark finished — avg time = ${"%.3f".format(averageTimeMs)} ms, score = $score")
    return score
}


@Preview(showBackground = true)
@Composable
fun BenchPreview() {
    MaterialBenchTheme {
        val context = LocalContext.current
        BenchScreen(onBackToMenu = {}, activity = context as? BenchActivity ?: BenchActivity())
    }
}
