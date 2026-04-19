package com.komarudude.materialbench.ui

import android.app.ActivityManager
import android.content.Context
import android.os.Bundle
import android.view.WindowManager
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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.ArrowDropUp
import androidx.compose.material.icons.filled.DeviceThermostat
import androidx.compose.material.icons.filled.LocalFireDepartment
import androidx.compose.material3.*
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
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
import android.os.StatFs
import androidx.activity.compose.BackHandler
import android.content.pm.PackageInfo
import com.komarudude.materialbench.R
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffold
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteType
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffoldDefaults
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import com.komarudude.materialbench.data.native.NativeLib.nativeCleanup
import com.komarudude.materialbench.ui.screens.bench.BenchScreen
import com.komarudude.materialbench.ui.screens.bench.BenchViewModel
import com.komarudude.materialbench.ui.screens.stress.StressScreen
import com.komarudude.materialbench.ui.screens.stress.StressViewModel
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.compose.currentBackStackEntryAsState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(
    overallScore: String,
    benchmarks: List<Benchmark>,
    percentileRank: String?,
    packageInfo: PackageInfo,
    onRunBenchmark: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()

    Scaffold(
        modifier = modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            LargeTopAppBar(
                title = {
                    Text(
                        text = stringResource(id = R.string.material_bench_title),
                        fontWeight = FontWeight.Bold
                    )
                },
                scrollBehavior = scrollBehavior,
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                    scrolledContainerColor = MaterialTheme.colorScheme.surfaceContainer
                )
            )
        },
        floatingActionButton = {
            val context = LocalContext.current
            val stressViewModel: StressViewModel = viewModel()
            ExtendedFloatingActionButton(
                onClick = handler@{
                    val stat = StatFs(context.filesDir.absolutePath)
                    val gigabytesAvailable = stat.availableBytes.toDouble() / (1024 * 1024 * 1024)
                    val memAvailable = getSystemAvailableMemory(context)

                    if (stressViewModel.isStressRunning) {
                        Toast.makeText(context, context.getString(R.string.stress_running), Toast.LENGTH_SHORT).show()
                        return@handler
                    }
                    if (memAvailable < 1536) {
                        Toast.makeText(context, context.getString(R.string.need_more_ram), Toast.LENGTH_LONG).show()
                        return@handler
                    }
                    if (gigabytesAvailable < 1.5) {
                        Toast.makeText(context, context.getString(R.string.need_more_rom), Toast.LENGTH_LONG).show()
                        return@handler
                    }
                    onRunBenchmark()
                },
                icon = { Icon(Icons.Default.LocalFireDepartment, null) },
                text = { Text(stringResource(id = R.string.run_benchmark)) },
                expanded = true
            )
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                Spacer(modifier = Modifier.height(8.dp))
                OverallScoreCard(score = overallScore, percentileRank = percentileRank)
            }

            items(benchmarks.size) { index ->
                BenchmarkCard(benchmark = benchmarks[index])
            }

            item {
                val versionText = stringResource(R.string.bench_version, packageInfo.versionName?: "", packageInfo.longVersionCode.toInt())
                Text(
                    text = versionText,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f),
                    fontStyle = FontStyle.Italic,
                    textAlign = TextAlign.Center,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 16.dp)
                )
            }

            item {
                Spacer(modifier = Modifier.height(96.dp)) // Место под FAB
            }
        }
    }
}


enum class Destination(
    val route: String,
    val label: String,
    val icon: ImageVector,
    val contentDescription: String,
    val showInNav: Boolean = true
) {
    MAIN("main", "Main", Icons.Default.LocalFireDepartment, "Main page"),
    STRESS("stress", "Stress", Icons.Default.DeviceThermostat, "Stress test page"),
    BENCHMARK("benchmark", "Benchmark", Icons.Default.LocalFireDepartment, "Benchmark page", false)
}

class MainActivity : ComponentActivity() {

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

@OptIn(ExperimentalMaterial3ExpressiveApi::class, ExperimentalMaterial3Api::class)
@Composable
fun BenchMainScreen() {
    val context = LocalContext.current
    val mainViewModel: MainViewModel = viewModel()
    val benchViewModel: BenchViewModel = viewModel()
    val stressViewModel: StressViewModel = viewModel()

    val overallScore by mainViewModel.overallScore
    val benchmarks by mainViewModel.benchmarks
    val percentileRank by mainViewModel.percentileRank

    val cpuBenchmarkTitle = stringResource(id = R.string.cpu_benchmark_title)
    val cpuBenchmarkDescription = stringResource(id = R.string.cpu_benchmark_description)
    val cpuIconText = stringResource(id = R.string.cpu_icon_text)
    val cpuMathSingleString = stringResource(R.string.cpu_math_single)
    val cpuMathMultiString = stringResource(R.string.cpu_math_multi)
    val cpuVectorMathString = stringResource(R.string.cpu_vector_math)
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
    val romRandOps = stringResource(R.string.rom_rand_ops)
    val romSeqWrite = stringResource(R.string.rom_seq_write)
    val romSeqRead = stringResource(R.string.rom_seq_read)
    val cpuCrypto = stringResource(R.string.cpu_crypto)
    val cpuCompress = stringResource(R.string.cpu_compress)
    val gpuVulkanComputeGemm = stringResource(R.string.vulkan_compute_gemm)
    val gpuRT = stringResource(R.string.gpu_rt)
    val aiLiteRTCpu = stringResource(R.string.ai_litert_cpu)
    val aiLiteRTGpu = stringResource(R.string.ai_litert_gpu)

    val lifecycleOwner = LocalLifecycleOwner.current
    var onResumeTrigger by remember { mutableIntStateOf(0) }

    val navController = rememberNavController()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route

    val startDestination = Destination.MAIN
    var selectedDestination by rememberSaveable { mutableIntStateOf(startDestination.ordinal) }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_RESUME) onResumeTrigger++ }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    LaunchedEffect(onResumeTrigger) {
        val subBenchKeys = mapOf(
            "cpu" to listOf(
                SubBenchmark(titleKey = cpuMathSingleString, scoreKey = "cpu_math_single"),
                SubBenchmark(titleKey = cpuMathMultiString, scoreKey = "cpu_math_multi"),
                SubBenchmark(titleKey = cpuCrypto, scoreKey = "cpu_crypto"),
                SubBenchmark(titleKey = cpuVectorMathString, scoreKey = "cpu_vector_math"),
                SubBenchmark(titleKey = cpuCompress, scoreKey = "cpu_compress")
            ),
            "gpu" to listOf(
                SubBenchmark(titleKey = gpuVulkanComputeGemm, scoreKey = "gpu_gemm"),
                SubBenchmark(titleKey = gpuRT, scoreKey = "gpu_rt")
            ),
            "mem" to listOf(
                SubBenchmark(titleKey = ramSeqWrite, scoreKey = "ram_seq_write"),
                SubBenchmark(titleKey = ramSeqRead, scoreKey = "ram_seq_read"),
                SubBenchmark(titleKey = romRandOps, scoreKey = "rom_rand_ops"),
                SubBenchmark(titleKey = romSeqWrite, scoreKey = "rom_seq_write"),
                SubBenchmark(titleKey = romSeqRead, scoreKey = "rom_seq_read")
            ),
            "ai" to listOf(
                SubBenchmark(titleKey = aiLiteRTCpu, scoreKey = "ai_litert_cpu"),
                SubBenchmark(titleKey = aiLiteRTGpu, scoreKey = "ai_litert_gpu")
            )
        )

        mainViewModel.loadScores(
            cpuBenchmarkTitle, cpuBenchmarkDescription, cpuIconText,
            gpuBenchmarkTitle, gpuBenchmarkDescription, gpuIconText,
            memoryTestTitle, memoryTestDescription, memIconText,
            aiTestTitle, aiTestDescription, aiIconText,
            subBenchKeys
        )
    }

    NavigationSuiteScaffold(
        layoutType = if (currentRoute == Destination.BENCHMARK.route && !benchViewModel.finished) {
            NavigationSuiteType.None
        } else {
            NavigationSuiteScaffoldDefaults.calculateFromAdaptiveInfo(currentWindowAdaptiveInfo())
        },
        navigationSuiteItems = {
            Destination.entries.filter { it.showInNav }.forEachIndexed { index, destination ->
                item(
                    selected = selectedDestination == index,
                    onClick = {
                        if (stressViewModel.isStressRunning) {
                            Toast.makeText(context, R.string.stress_running, Toast.LENGTH_SHORT).show()
                        } else {
                            navController.navigate(route = destination.route) {
                                popUpTo(navController.graph.startDestinationId) { saveState = true }
                                launchSingleTop = true
                                restoreState = true
                            }
                            selectedDestination = index
                        }
                    },
                    icon = { Icon(destination.icon, contentDescription = destination.contentDescription) },
                    label = { Text(destination.label) }
                )
            }
        }
    ) {
        NavHost(
            navController = navController,
            startDestination = startDestination.route,
            modifier = Modifier.fillMaxSize()
        ) {
            composable(Destination.MAIN.route) {
                MainScreen(
                    overallScore = overallScore,
                    benchmarks = benchmarks,
                    percentileRank = percentileRank,
                    packageInfo = mainViewModel.packageInfo,
                    onRunBenchmark = {
                        benchViewModel.reset()
                        navController.navigate(Destination.BENCHMARK.route)
                    }
                )
            }
            composable(Destination.STRESS.route) {
                StressScreen(viewModel = stressViewModel)
            }
            composable(Destination.BENCHMARK.route) {
                val activity = context as? ComponentActivity
                LaunchedEffect(Unit) {
                    activity?.window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
                DisposableEffect(Unit) {
                    onDispose {
                        activity?.window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    }
                }
                BackHandler {
                    if (benchViewModel.finished) {
                        navController.popBackStack()
                        onResumeTrigger++
                    } else {
                        Toast.makeText(context, R.string.bench_running, Toast.LENGTH_SHORT).show()
                    }
                }
                BenchScreen(
                    viewModel = benchViewModel,
                    onBackToMenu = {
                        navController.popBackStack()
                        onResumeTrigger++
                    }
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class, ExperimentalMaterial3Api::class)
@Composable
fun OverallScoreCard(score: String, percentileRank: String?) {
    val mainViewModel: MainViewModel = viewModel()
    val isFetchingRank by mainViewModel.isFetchingRank

    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.elevatedCardColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer
        )
    ) {
        Column(
            modifier = Modifier.padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = stringResource(id = R.string.overall_score_title),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
            )
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = score,
                style = MaterialTheme.typography.displayLargeEmphasized,
                color = MaterialTheme.colorScheme.onPrimaryContainer
            )
            Spacer(modifier = Modifier.height(24.dp))

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(12.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.1f))
            ) {
                if (percentileRank != null) {
                    val progress = percentileRank.removeSuffix("%").replace(',', '.').toFloat() / 100f
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(progress)
                            .fillMaxHeight()
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.primary)
                    )
                } else if (isFetchingRank) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(0.1f)
                            .fillMaxHeight()
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.primary)
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            if (percentileRank != null) {
                Text(
                    text = stringResource(R.string.top_percentile, percentileRank),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            } else {
                Text(
                    text = stringResource(id = if (isFetchingRank) R.string.wait_rank_data else R.string.no_rank_data),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
        }
    }
}

@Composable
fun BenchmarkCard(benchmark: Benchmark) {
    var isExpanded by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.medium,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainer
        )
    ) {
        Column(
            modifier = Modifier
                .clickable { isExpanded = !isExpanded }
                .padding(8.dp) // Внутренний паддинг
        ) {
            Row(
                modifier = Modifier.padding(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(48.dp)
                        .clip(MaterialTheme.shapes.small)
                        .background(MaterialTheme.colorScheme.secondaryContainer),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = benchmark.iconText,
                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                }

                Spacer(modifier = Modifier.width(16.dp))

                Column(
                    modifier = Modifier.weight(1f)
                ) {
                    Text(
                        text = benchmark.title,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        text = benchmark.description,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }

                Spacer(modifier = Modifier.width(8.dp))

                Column(horizontalAlignment = Alignment.End) {
                    Text(
                        text = benchmark.score ?: "?",
                        style = MaterialTheme.typography.headlineSmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Bold
                    )
                    if (benchmark.subBenchmarks.isNotEmpty()) {
                        Icon(
                            imageVector = if (isExpanded) Icons.Filled.ArrowDropUp else Icons.Filled.ArrowDropDown,
                            contentDescription = null,
                            modifier = Modifier.size(24.dp),
                            tint = MaterialTheme.colorScheme.outline
                        )
                    }
                }
            }

            AnimatedVisibility(visible = isExpanded && benchmark.subBenchmarks.isNotEmpty()) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp)
                        .padding(bottom = 16.dp)
                        .clip(MaterialTheme.shapes.small)
                        .background(MaterialTheme.colorScheme.surfaceContainerHigh) // Чуть светлее фон для подпунктов
                        .padding(12.dp)
                ) {
                    benchmark.subBenchmarks.forEachIndexed { index, subBenchmark ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 6.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = subBenchmark.titleKey,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                text = subBenchmark.score ?: "?",
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.primary
                            )
                        }
                        if (index < benchmark.subBenchmarks.size - 1) {
                            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
                        }
                    }
                }
            }
        }
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
        // BenchMainScreen()
    }
}
