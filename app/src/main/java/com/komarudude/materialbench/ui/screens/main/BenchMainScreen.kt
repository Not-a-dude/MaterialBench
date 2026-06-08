package com.komarudude.materialbench.ui.screens.main

import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffold
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffoldDefaults
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteType
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.komarudude.materialbench.R
import com.komarudude.materialbench.ui.Destination
import com.komarudude.materialbench.ui.screens.bench.BenchScreen
import com.komarudude.materialbench.ui.screens.bench.BenchViewModel
import com.komarudude.materialbench.ui.screens.stress.StressScreen
import com.komarudude.materialbench.ui.screens.stress.StressViewModel

@OptIn(ExperimentalMaterial3ExpressiveApi::class, ExperimentalMaterial3Api::class)
@Composable
fun BenchMainScreen() {
    val context = LocalContext.current
    val mainViewModel: BenchMainViewModel = viewModel()
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

    val lifecycleOwner = LocalLifecycleOwner.current
    var onResumeTrigger by remember { mutableIntStateOf(0) }

    val navController = rememberNavController()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route

    val startDestination = Destination.MAIN
    var selectedDestination by rememberSaveable { mutableIntStateOf(startDestination.ordinal) }

    DisposableEffect(lifecycleOwner) {
        val observer =
            LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_RESUME) onResumeTrigger++ }
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
                SubBenchmark(titleKey = aiLiteRTCpu, scoreKey = "ai_litert_cpu")
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