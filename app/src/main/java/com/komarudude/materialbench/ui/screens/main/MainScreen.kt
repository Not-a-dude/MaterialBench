package com.komarudude.materialbench.ui.screens.main

import android.content.pm.PackageInfo
import android.os.StatFs
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.LocalFireDepartment
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.komarudude.materialbench.R
import com.komarudude.materialbench.ui.screens.main.components.BenchmarkCard
import com.komarudude.materialbench.ui.screens.main.components.OverallScoreCard
import com.komarudude.materialbench.ui.screens.stress.StressViewModel

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

                    if (stressViewModel.isStressRunning) {
                        Toast.makeText(context, context.getString(R.string.stress_running), Toast.LENGTH_SHORT).show()
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