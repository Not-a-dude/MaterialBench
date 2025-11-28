package com.komarudude.materialbench.ui

import java.text.DecimalFormat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

import android.app.ActivityManager
import android.content.Intent
import android.content.Context
import android.os.Bundle
import android.os.BatteryManager
import android.os.Build
import android.content.IntentFilter
import android.util.Log
import android.widget.Toast
import androidx.core.content.getSystemService
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.ArrowDropUp
import androidx.compose.material.icons.filled.DeviceThermostat
import androidx.compose.material.icons.filled.LocalFireDepartment
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.State
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.font.FontStyle
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.komarudude.materialbench.ui.theme.MaterialBenchTheme
import android.content.pm.PackageManager
import android.os.StatFs
import androidx.activity.compose.BackHandler
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.sp
import androidx.compose.ui.graphics.toArgb
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewModelScope
import android.content.pm.PackageInfo
import com.komarudude.materialbench.BenchScores
import com.komarudude.materialbench.R
import com.komarudude.materialbench.utils.RetrofitClient
import com.komarudude.materialbench.utils.ScoreRequest

import com.patrykandpatrick.vico.compose.cartesian.rememberVicoZoomState
import com.patrykandpatrick.vico.compose.cartesian.CartesianChartHost
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberBottom
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberStart
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLineCartesianLayer
import com.patrykandpatrick.vico.compose.cartesian.rememberCartesianChart
import com.patrykandpatrick.vico.compose.cartesian.marker.rememberDefaultCartesianMarker
import com.patrykandpatrick.vico.core.cartesian.CartesianMeasuringContext
import com.patrykandpatrick.vico.compose.common.component.rememberTextComponent
import com.patrykandpatrick.vico.compose.common.component.rememberShapeComponent
import com.patrykandpatrick.vico.core.cartesian.axis.HorizontalAxis
import com.patrykandpatrick.vico.core.cartesian.axis.VerticalAxis
import com.patrykandpatrick.vico.core.cartesian.data.CartesianValueFormatter
import com.patrykandpatrick.vico.core.cartesian.marker.DefaultCartesianMarker
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.common.Fill
import com.patrykandpatrick.vico.core.cartesian.Zoom
import com.patrykandpatrick.vico.core.common.data.ExtraStore
import com.patrykandpatrick.vico.core.cartesian.data.CartesianLayerRangeProvider

private val StartAxisValueFormatter = CartesianValueFormatter.decimal(DecimalFormat("#.## °C"))

var isStressRunning by mutableStateOf(false)
private const val highTemperatureThreshold = 45.0f
private const val lowTemperatureThreshold = 17.0f

private val MarkerValueFormatter =
    DefaultCartesianMarker.ValueFormatter.default(DecimalFormat("#.## °C"))

@Composable
private fun TemperatureChart(
    modelProducer: CartesianChartModelProducer,
    bottomAxisValueFormatter: CartesianValueFormatter,
    modifier: Modifier = Modifier,
) {
    val labelBackground = rememberShapeComponent(
        fill = Fill(MaterialTheme.colorScheme.surface.toArgb())
    )

    val labelComponent = rememberTextComponent(
        color = Color.White,
        textSize = 12.sp,
        background = labelBackground
    )

    val zoomState = rememberVicoZoomState(initialZoom = Zoom.Content)

    val rangeProvider = object : CartesianLayerRangeProvider {
        override fun getMinY(minY: Double, maxY: Double, extraStore: ExtraStore): Double = 18.0
        override fun getMaxY(minY: Double, maxY: Double, extraStore: ExtraStore): Double = 46.0
    }

    CartesianChartHost(
        chart = rememberCartesianChart(
            rememberLineCartesianLayer(
                rangeProvider = rangeProvider
            ),
            startAxis = VerticalAxis.rememberStart(valueFormatter = StartAxisValueFormatter),
            bottomAxis = HorizontalAxis.rememberBottom(
                guideline = null,
                valueFormatter = bottomAxisValueFormatter
            ),
            marker = rememberDefaultCartesianMarker(
                label = labelComponent,
                valueFormatter = MarkerValueFormatter
            ),
        ),
        modelProducer = modelProducer,
        zoomState = zoomState,
        modifier = modifier.height(220.dp),
    )
}

@Composable
fun MainScreen(
    overallScore: String,
    benchmarks: List<Benchmark>,
    percentileRank: String?,
    packageInfo: PackageInfo,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            OverallScoreCard(score = overallScore, percentileRank = percentileRank)
        }

        items(benchmarks.size) { index -> // Used index here
            BenchmarkCard(benchmark = benchmarks[index])
        }

        item {
            val versionText = stringResource(R.string.bench_version, packageInfo.versionName?: "", packageInfo.longVersionCode.toInt())
            Text(versionText, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f), fontStyle = FontStyle.Italic, textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
        }

        item {
            Spacer(modifier = Modifier.height(96.dp))
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StressScreen(mainActivity: MainActivity, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val pm: PackageManager = context.packageManager
    val hasVulkanCompute = pm.hasSystemFeature("android.hardware.vulkan.compute")

    val vulkanComputeUnsupportedToast = Toast.makeText(context, R.string.vulkan_compute_unsupported, Toast.LENGTH_SHORT)
    LaunchedEffect(Unit) {
        if (!hasVulkanCompute) {
            vulkanComputeUnsupportedToast.show()
        }
    }

    val stressStartedToast = Toast.makeText(context, R.string.stress_started, Toast.LENGTH_SHORT)
    val stressStoppedToast = Toast.makeText(context, R.string.stress_stopped, Toast.LENGTH_SHORT)

    val items = listOf("CPU", "GPU Compute")

    var selectedIndices by rememberSaveable { mutableStateOf(setOf(0)) }

    val modelProducer = remember { CartesianChartModelProducer() }
    val temperatures = remember { mutableStateListOf<Float>() }

    val startBatteryTemp = getBatteryTemp(context)
    var batteryTemp by remember { mutableFloatStateOf(startBatteryTemp ?: 0f) }

    var showHighTempDialog by rememberSaveable { mutableStateOf(false) }
    var showLowTempDialog by rememberSaveable { mutableStateOf(false) }
    var triggeredTemp by rememberSaveable { mutableFloatStateOf(0f) }

    // max точек, которые хотим отображать. Можно настроить.
    val maxPoints = 240

    // Сколько секунд соответствует одной точке по оси X. Начинаем с 1s.
    var sampleIntervalSec by rememberSaveable { mutableDoubleStateOf(1.0) }

    // Функция сжатия: пока больше maxPoints — объединяем соседние пары в среднее и удваиваем интервал
    fun compressIfNeeded(list: MutableList<Float>) {
        while (list.size > maxPoints) {
            val newList = mutableListOf<Float>()
            var i = 0
            while (i < list.size) {
                if (i + 1 < list.size) {
                    newList.add((list[i] + list[i + 1]) / 2f)
                    i += 2
                } else {
                    // если нечётное — переносим последний элемент
                    newList.add(list[i])
                    i += 1
                }
            }
            list.clear()
            list.addAll(newList)
            sampleIntervalSec *= 2.0
        }
    }

    // Форматтер нижней оси: axisValue — индекс точки, умножаем на sampleIntervalSec, и форматируем в s/m/h
    val bottomAxisValueFormatter = CartesianValueFormatter { _: CartesianMeasuringContext, axisValue: Double, _ ->
        val secondsDouble = (axisValue + 1) * sampleIntervalSec
        val totalSeconds = secondsDouble.toLong().coerceAtLeast(0L)

        when {
            totalSeconds >= 3600L -> {
                val h = totalSeconds / 3600L
                val m = (totalSeconds % 3600L) / 60L
                "${h}h ${m}m"
            }
            totalSeconds >= 60L -> {
                val m = totalSeconds / 60L
                val s = totalSeconds % 60L
                if (s == 0L) "${m}m" else "${m}m ${s}s"
            }
            else -> "${totalSeconds}s"
        }
    }

    LaunchedEffect(isStressRunning) {
        if (isStressRunning) {
            // Очищаем список температур при старте нового стресс-теста
            temperatures.clear()
            sampleIntervalSec = 1.0
        }
        while (isStressRunning) {
            delay(1000L)
            batteryTemp = getBatteryTemp(context) ?: 0f
            temperatures.add(batteryTemp)

            compressIfNeeded(temperatures)

            modelProducer.runTransaction {
                lineSeries {
                    series(temperatures)
                }
            }

            if (batteryTemp >= highTemperatureThreshold && !showHighTempDialog) {
                triggeredTemp = batteryTemp
                showHighTempDialog = true

                isStressRunning = false
                mainActivity.nativeStopCpuStress()
                mainActivity.nativeStopGpuStress()

                return@LaunchedEffect
            } else if (batteryTemp <= lowTemperatureThreshold && !showLowTempDialog) {
                triggeredTemp = batteryTemp
                showLowTempDialog = true

                isStressRunning = false
                mainActivity.nativeStopCpuStress()
                mainActivity.nativeStopGpuStress()

                return@LaunchedEffect
            }
        }
    }

    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
            modifier = Modifier.padding(24.dp)
        ) {

            Text(text = stringResource(R.string.select_stress_modes))

            Spacer(Modifier.height(16.dp))

            MultiChoiceSegmentedButtonRow {
                items.forEachIndexed { index, label ->
                    SegmentedButton(
                        checked = selectedIndices.contains(index),
                        onCheckedChange = {
                            if (selectedIndices.contains(index)) {
                                if (selectedIndices.size > 1) {
                                    selectedIndices = selectedIndices - index
                                }
                            } else {
                                selectedIndices = selectedIndices + index
                            }
                        },
                        enabled = if (index == 1) hasVulkanCompute else true,
                        shape = SegmentedButtonDefaults.baseShape,
                        label = {
                            Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    )
                }
            }

            Spacer(Modifier.height(16.dp))

            val selectedModes = items.filterIndexed { index, _ -> selectedIndices.contains(index) }

            Spacer(Modifier.height(32.dp))

            TemperatureChart(modelProducer = modelProducer, bottomAxisValueFormatter = bottomAxisValueFormatter)

            Spacer(Modifier.height(32.dp))

            Button(
                onClick = {
                    if (!isStressRunning) {
                        println("Начат стресс-тест в режимах: ${selectedModes.joinToString()}")
                        stressStartedToast.show()
                        isStressRunning = true
                        if (selectedIndices.contains(0)) {
                            mainActivity.nativeStartCpuStress()
                        }
                        if (selectedIndices.contains(1)) {
                            mainActivity.nativeStartGpuStress()
                        }
                    } else {
                        println("Стресс-тест остановлен")
                        stressStoppedToast.show()
                        isStressRunning = false
                        mainActivity.nativeStopCpuStress()
                        mainActivity.nativeStopGpuStress()
                    }
                }
            ) {
                Text(if (!isStressRunning) {
                    stringResource(R.string.stress_start)
                } else {
                    stringResource(R.string.stress_stop)
                }
                )
            }
        }
    }

    if (showHighTempDialog) {
        AlertDialog(
            onDismissRequest = { /* Не позволяем закрывать кликом вне диалога, только через кнопку */ },
            title = { Text(stringResource(R.string.high_bat_temp_dialog_title)) },
            text = {
                Text(stringResource(R.string.high_bat_temp_dialog_msg, triggeredTemp))
            },
            confirmButton = {
                TextButton(onClick = { showHighTempDialog = false }) {
                    Text(stringResource(R.string.ok))
                }
            }
        )
    } else if (showLowTempDialog) {
        AlertDialog(
            onDismissRequest = { /* Не позволяем закрывать кликом вне диалога, только через кнопку */ },
            title = { Text(stringResource(R.string.low_bat_temp_dialog_title)) },
            text = {
                Text(stringResource(R.string.low_bat_temp_dialog_msg, triggeredTemp))
            },
            confirmButton = {
                TextButton(onClick = { showLowTempDialog = false }) {
                    Text(stringResource(R.string.ok))
                }
            }
        )
    }
}

enum class Destination(
    val route: String,
    val label: String,
    val icon: ImageVector,
    val contentDescription: String
) {
    MAIN("main", "Main", Icons.Default.LocalFireDepartment, "Main page"),
    STRESS("stress", "Stress", Icons.Default.DeviceThermostat, "Stress test page")
}

class MainActivity : ComponentActivity() {
    companion object {
        init {
            try {
                System.loadLibrary("materialbench")
            } catch (_: UnsatisfiedLinkError) {
                // Игнорируем в Preview
            }
        }
    }

    external fun nativeStartCpuStress()
    external fun nativeStopCpuStress()
    external fun nativeStartGpuStress()
    external fun nativeStopGpuStress()
    external fun nativeCleanup()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MaterialBenchTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    BenchMainScreen()
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        nativeCleanup()
    }
}

data class SubBenchmark(
    val titleKey: String,
    val scoreKey: String,
    var score: String? = null
)

data class Benchmark(
    val title: String,
    val description: String,
    val iconText: String,
    val score: String? = null,
    val onClick: () -> Unit,
    val subBenchmarks: List<SubBenchmark> = emptyList()
)

class BenchViewModel : ViewModel() {
    private val _percentileRank = mutableStateOf<String?>(null)
    val percentileRank: State<String?> = _percentileRank

    private val _isFetchingRank = mutableStateOf(false)
    val isFetchingRank: State<Boolean> = _isFetchingRank

    fun fetchPercentileRank(score: Int, versionCode: Long) {
        _isFetchingRank.value = true
        viewModelScope.launch {
            try {
                val response = withContext(Dispatchers.IO) {
                    RetrofitClient.apiService.getRank(ScoreRequest(score, versionCode))
                }
                _percentileRank.value = "%.2f".format(response.percentile) + "%"
            } catch (e: Exception) {
                _percentileRank.value = null
                Log.e("BenchViewModel", "Error fetching percentile rank: ${e.message}")
            } finally {
                _isFetchingRank.value = false
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BenchMainScreen() {
    val context = LocalContext.current
    val mainActivity = context as? MainActivity
    var overallScore by remember { mutableStateOf("0") }
    var benchmarks by remember { mutableStateOf<List<Benchmark>>(emptyList()) }

    val cpuBenchmarkTitle = stringResource(id = R.string.cpu_benchmark_title)
    val cpuBenchmarkDescription = stringResource(id = R.string.cpu_benchmark_description)
    val cpuIconText = stringResource(id = R.string.cpu_icon_text)
    val cpuMathSingleString = stringResource(R.string.cpu_math_single)
    val cpuMathMultiString = stringResource(R.string.cpu_math_multi)
    val gpuBenchmarkTitle = stringResource(id = R.string.gpu_benchmark_title)
    val gpuBenchmarkDescription = stringResource(id = R.string.gpu_benchmark_description)
    val gpuIconText = stringResource(id = R.string.gpu_icon_text)
    val memoryTestTitle = stringResource(id = R.string.memory_test_title)
    val memoryTestDescription = stringResource(id = R.string.memory_test_description)
    val memIconText = stringResource(id = R.string.mem_icon_text)
    val aiTestTitle = stringResource(id = R.string.ai_test_title)
    val aiTestDescription = stringResource(id = R.string.ai_test_description)
    val aiIconText = stringResource(id = R.string.ai_icon_text)
    val ramSeqWrite = stringResource(R.string.ram_seq_write)
    val ramSeqRead = stringResource(R.string.ram_seq_read)
    val romRandWrite = stringResource(R.string.rom_rand_write)
    val romRandRead = stringResource(R.string.rom_rand_read)
    val romSeqWrite = stringResource(R.string.rom_seq_write)
    val romSeqRead = stringResource(R.string.rom_seq_read)
    val cpuCryptoSingle = stringResource(R.string.cpu_crypto_single)
    val cpuCryptoMulti = stringResource(R.string.cpu_crypto_multi)
    val gpuVulkanComputeGemm = stringResource(R.string.vulkan_compute_gemm)
    val gpuVulkanComputeNBody = stringResource(R.string.vulkan_compute_nbody)
    val gpuRT = stringResource(R.string.gpu_rt)
    val aiLiteRT = stringResource(R.string.ai_litert)

    val lifecycleOwner = LocalLifecycleOwner.current
    var onResumeTrigger by remember { mutableIntStateOf(0) }

    val navController = rememberNavController()
    val startDestination = Destination.MAIN
    var selectedDestination by rememberSaveable { mutableIntStateOf(startDestination.ordinal) }

    val packageInfo: PackageInfo = try {
        if (context is ComponentActivity) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
            } else {
                @Suppress("DEPRECATION")
                context.packageManager.getPackageInfo(context.packageName, 0)
            }
        } else {
            // For previews or non-ComponentActivity contexts, provide a dummy
            PackageInfo().apply {
                versionName = "UNKNOWN"
                longVersionCode = 1L
            }
        }
    } catch (e: UnsupportedOperationException) {
        // Catch the specific exception that occurs in previews
        Log.e("BenchMainScreen", "getPackageInfo not implemented in subclass for preview: ${e.message}")
        PackageInfo().apply {
            versionName = "UNKNOWN"
            longVersionCode = 1L
        }
    } catch (e: Exception) {
        Log.e("BenchMainScreen", "Error getting package info: ${e.message}")
        PackageInfo().apply {
            versionName = "UNKNOWN"
        }
    }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                onResumeTrigger++
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }

    val cpuSubBenchmarks = listOf(
        SubBenchmark(titleKey = cpuMathSingleString, scoreKey = "cpu_math_single"),
        SubBenchmark(titleKey = cpuMathMultiString, scoreKey = "cpu_math_multi"),
        SubBenchmark(titleKey = cpuCryptoSingle, scoreKey = "cpu_crypto_single"),
        SubBenchmark(titleKey = cpuCryptoMulti, scoreKey = "cpu_crypto_multi"),
    )
    val gpuSubBenchmarks = listOf(
        SubBenchmark(titleKey = gpuVulkanComputeGemm, scoreKey = "gpu_gemm"),
        SubBenchmark(titleKey = gpuVulkanComputeNBody, scoreKey = "gpu_nbody"),
        SubBenchmark(titleKey = gpuRT, scoreKey = "gpu_rt"),
    )
    val memSubBenchmarks = listOf(
        SubBenchmark(titleKey = ramSeqWrite, scoreKey = "ram_seq_write"),
        SubBenchmark(titleKey = ramSeqRead, scoreKey = "ram_seq_read"),
        SubBenchmark(titleKey = romRandWrite, scoreKey = "rom_rand_write"),
        SubBenchmark(titleKey = romRandRead, scoreKey = "rom_rand_read"),
        SubBenchmark(titleKey = romSeqWrite, scoreKey = "rom_seq_write"),
        SubBenchmark(titleKey = romSeqRead, scoreKey = "rom_seq_read")
    )
    val aiSubBenchmarks = listOf(
        SubBenchmark(titleKey = aiLiteRT, scoreKey = "ai_litert")
    )

    val viewModel: BenchViewModel = viewModel()
    val percentileRank by viewModel.percentileRank

    LaunchedEffect(onResumeTrigger) {
        withContext(Dispatchers.IO) {
            val currentOverallScore = BenchScores.getScore(context, "overall_score")
            overallScore = currentOverallScore.let {
                if (it == 0) "0" else it.toString()
            }

            val cpuScore = BenchScores.getScore(context, "CPU Benchmark")
            val gpuScore = BenchScores.getScore(context, "GPU Benchmark")
            val memScore = BenchScores.getScore(context, "Memory Test")
            val aiScore = BenchScores.getScore(context, "AI Test")

            val allSubBenchmarks = listOf(cpuSubBenchmarks, gpuSubBenchmarks, memSubBenchmarks, aiSubBenchmarks).flatten()
            allSubBenchmarks.forEach { sub ->
                val scoreValue = BenchScores.getScore(context, sub.scoreKey)
                sub.score = if (scoreValue == 0) null else scoreValue.toString()
            }

            benchmarks = listOf(
                Benchmark(
                    title = cpuBenchmarkTitle,
                    description = cpuBenchmarkDescription,
                    iconText = cpuIconText,
                    score = if (cpuScore == 0) null else cpuScore.toString(),
                    onClick = { /* Запуск CPU теста */ },
                    subBenchmarks = cpuSubBenchmarks
                ),
                Benchmark(
                    title = gpuBenchmarkTitle,
                    description = gpuBenchmarkDescription,
                    iconText = gpuIconText,
                    score = if (gpuScore == 0) null else gpuScore.toString(),
                    onClick = { /* Запуск GPU теста */ },
                    subBenchmarks = gpuSubBenchmarks
                ),
                Benchmark(
                    title = memoryTestTitle,
                    description = memoryTestDescription,
                    iconText = memIconText,
                    score = if (memScore == 0) null else memScore.toString(),
                    onClick = { /* Запуск теста памяти */ },
                    subBenchmarks = memSubBenchmarks
                ),
                Benchmark(
                    title = aiTestTitle,
                    description = aiTestDescription,
                    iconText = aiIconText,
                    score = if (aiScore == 0) null else aiScore.toString(),
                    onClick = { /* Запуск теста ИИ */ },
                    subBenchmarks = aiSubBenchmarks
                )
            )

            if (currentOverallScore > 0) {
                // Ensure context is ComponentActivity before accessing packageManager
                if (context is ComponentActivity) {
                    val versionCode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0)).longVersionCode
                    } else {
                        @Suppress("DEPRECATION")
                        context.packageManager.getPackageInfo(context.packageName, 0).longVersionCode
                    }
                    viewModel.fetchPercentileRank(currentOverallScore, versionCode)
                } else {
                    // For previews or non-ComponentActivity contexts, use the mock packageInfo's versionCode
                    viewModel.fetchPercentileRank(currentOverallScore, packageInfo.longVersionCode)
                }
            }
        }
    }

    BackHandler(enabled = isStressRunning) {
        Toast.makeText(context, context.getString(R.string.stress_running), Toast.LENGTH_SHORT).show()
    }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Text(
                        text = stringResource(id = R.string.material_bench_title),
                        fontWeight = FontWeight.Bold
                    )
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = handler@{
                    // 1. Получение данных
                    val stat = StatFs(context.filesDir.absolutePath)
                    val gigabytesAvailable = stat.availableBytes.toDouble() / (1024 * 1024 * 1024)
                    val memAvailable = getSystemAvailableMemory(context)

                    if (isStressRunning) {
                        Toast.makeText(
                            context,
                            context.getString(R.string.stress_running),
                            Toast.LENGTH_SHORT
                        ).show()
                        return@handler
                    }

                    if (memAvailable < 512) {
                        val message = context.getString(R.string.need_more_ram)
                        Toast.makeText(context, message, Toast.LENGTH_LONG).show()
                        return@handler
                    }

                    if (gigabytesAvailable < 1.5) {
                        val message = context.getString(R.string.need_more_rom)
                        Toast.makeText(
                            context,
                            message,
                            Toast.LENGTH_LONG
                        ).show()
                        return@handler
                    }

                    val intent = Intent(context, BenchActivity::class.java)
                    context.startActivity(intent)
                },
                icon = {},
                text = { Text(stringResource(id = R.string.run_benchmark)) }
            )
        },
        bottomBar = {
            NavigationBar(windowInsets = NavigationBarDefaults.windowInsets) {
                Destination.entries.forEachIndexed { index, destination ->
                    NavigationBarItem(
                        selected = selectedDestination == index,
                        onClick = {
                            if (isStressRunning) {
                                Toast.makeText(context, R.string.stress_running, Toast.LENGTH_SHORT).show()
                            } else {
                                navController.navigate(route = destination.route)
                                selectedDestination = index
                            }
                        },
                        icon = {
                            Icon(
                                destination.icon,
                                contentDescription = destination.contentDescription
                            )
                        },
                        label = { Text(destination.label) }
                    )
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = startDestination.route,
            modifier = Modifier.padding(innerPadding)
        ) {
            composable(Destination.MAIN.route) {
                MainScreen(overallScore = overallScore, benchmarks = benchmarks, percentileRank = percentileRank, packageInfo = packageInfo)
            }
            composable(Destination.STRESS.route) {
                StressScreen(mainActivity = mainActivity ?: MainActivity())
            }
        }
    }
}

@Composable
fun OverallScoreCard(score: String, percentileRank: String?) {
    val viewModel: BenchViewModel = viewModel()
    val isFetchingRank by viewModel.isFetchingRank

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer
        )
    ) {
        Column(
            modifier = Modifier.padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = stringResource(id = R.string.overall_score_title),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.7f)
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = score,
                style = MaterialTheme.typography.displayLarge,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onPrimaryContainer
            )
            Spacer(modifier = Modifier.height(16.dp))
            if (percentileRank != null) {
                Text(
                    text = stringResource(R.string.top_percentile, percentileRank),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                Spacer(modifier = Modifier.height(8.dp))
                LinearProgressIndicator(
                    progress = { percentileRank.removeSuffix("%").replace(',', '.').toFloat() / 100f },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(8.dp)
                        .clip(RoundedCornerShape(4.dp)),
                    color = MaterialTheme.colorScheme.primary
                )
            } else {
                Text(
                    text = stringResource(id = if (isFetchingRank) R.string.wait_rank_data else R.string.no_rank_data),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                Spacer(modifier = Modifier.height(8.dp))
                if (isFetchingRank) {
                    LinearProgressIndicator(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp)
                            .clip(RoundedCornerShape(4.dp)),
                        color = MaterialTheme.colorScheme.primary
                    )
                } else {
                    LinearProgressIndicator(
                        progress = { 0f },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp)
                            .clip(RoundedCornerShape(4.dp)),
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
    }
}

@Composable
fun BenchmarkCard(benchmark: Benchmark) {
    var isExpanded by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Column(
            modifier = Modifier.clickable { isExpanded = !isExpanded }
        ) {
            Row(
                modifier = Modifier.padding(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(40.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.primary),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = benchmark.iconText,
                        color = MaterialTheme.colorScheme.onPrimary,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                }

                Spacer(modifier = Modifier.width(16.dp))

                Column(
                    modifier = Modifier.weight(1f)
                ) {
                    Text(
                        text = benchmark.title,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = benchmark.description,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }

                if (benchmark.score != null) {
                    Column(
                        horizontalAlignment = Alignment.End,
                        modifier = Modifier.padding(start = 8.dp)
                    ) {
                        Text(
                            text = benchmark.score,
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = stringResource(id = R.string.points),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                } else {
                    Text(
                        text = "?", // Display "?" if score is null
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                }

                if (benchmark.subBenchmarks.isNotEmpty()) {
                    IconButton(onClick = { isExpanded = !isExpanded }) {
                        Icon(
                            imageVector = if (isExpanded) Icons.Filled.ArrowDropUp else Icons.Filled.ArrowDropDown,
                            contentDescription = if (isExpanded) stringResource(R.string.collapse) else stringResource(
                                R.string.expand)
                        )
                    }
                }
            }

            AnimatedVisibility(visible = isExpanded && benchmark.subBenchmarks.isNotEmpty()) {
                Column(modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp)) {
                    benchmark.subBenchmarks.forEach { subBenchmark ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 4.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = subBenchmark.titleKey,
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Text(
                                text = subBenchmark.score ?: "?",
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.primary
                            )
                        }
                        HorizontalDivider(modifier = Modifier.padding(top = 4.dp))
                    }
                }
            }
        }
    }
}

fun getBatteryTemp(context: Context): Float? {
    val batteryStatus: Intent? = context.registerReceiver(
        null,
        IntentFilter(Intent.ACTION_BATTERY_CHANGED)
    )

    val tempDeciCelsius = batteryStatus?.getIntExtra(
        BatteryManager.EXTRA_TEMPERATURE,
        0
    )

    return if (tempDeciCelsius != null && tempDeciCelsius != -1) {
        tempDeciCelsius / 10.0f
    } else {
        Log.w("BatteryInfo", "Температура батареи недоступна.")
        null
    }
}

fun getSystemAvailableMemory(context: Context): Long {
    val activityManager: ActivityManager? = context.getSystemService()
    val memoryInfo = ActivityManager.MemoryInfo()
    activityManager?.getMemoryInfo(memoryInfo)

    return memoryInfo.availMem / (1024 * 1024)
}

@Preview(showBackground = true)
@Composable
fun BenchMainScreenPreview() {
    MaterialBenchTheme {
        BenchMainScreen()
    }
}

@Preview(showBackground = true)
@Composable
fun BenchStressScreenPreview() {
    MaterialBenchTheme {
        StressScreen(mainActivity = MainActivity())
    }
}