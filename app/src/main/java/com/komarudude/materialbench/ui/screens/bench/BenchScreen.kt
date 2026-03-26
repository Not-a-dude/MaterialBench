package com.komarudude.materialbench.ui.screens.bench

import android.app.Activity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DeveloperBoardOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularWavyProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.komarudude.materialbench.R
import com.komarudude.materialbench.data.model.BenchmarkConfig
import com.komarudude.materialbench.data.model.BenchmarkConfig.testSteps
import com.komarudude.materialbench.ui.screens.bench.components.StatCard

@OptIn(
    ExperimentalMaterial3ExpressiveApi::class,
    ExperimentalMaterial3Api::class
)
@Composable
fun BenchScreen(
    modifier: Modifier = Modifier,
    viewModel: BenchViewModel = viewModel(),
    onBackToMenu: () -> Unit
) {
    val context = LocalContext.current
    val ueLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val score = if (result.resultCode == Activity.RESULT_OK) {
            result.data?.getIntExtra("ue_frames", 0) ?: 0
        } else {
            0
        }
        viewModel.resultChannel.trySend(score)
    }

    LaunchedEffect(Unit) {
        viewModel.startBenchmarks(onLaunchUE = {
            val intent = context.packageManager.getLaunchIntentForPackage("com.komarudude.materialbench.rttest")
            if (intent != null) {
                ueLauncher.launch(intent)
            } else {
                viewModel.resultChannel.trySend(0)
            }
        })
    }

    val overallProgressTitle = stringResource(R.string.overall_progress_title)
    val runningString = stringResource(R.string.running)
    val finishedString = stringResource(R.string.finished)
    val backToMenuString = stringResource(R.string.back_to_menu)

    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        ElevatedCard(
            shape = RoundedCornerShape(24.dp),
            modifier = Modifier
                .fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainerHigh
            )
        ) {
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                val animatedProgress by animateFloatAsState(targetValue = viewModel.progress, label = "mainProgress")

                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier.size(180.dp)
                ) {
                    CircularWavyProgressIndicator(
                        progress = { animatedProgress },
                        modifier = Modifier.size(180.dp),
                        color = MaterialTheme.colorScheme.primary,
                        trackColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.1f)
                    )
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text = "${(animatedProgress * 100).toInt()}%",
                            style = MaterialTheme.typography.displaySmall,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = if (viewModel.finished) finishedString else runningString,
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

                Spacer(modifier = Modifier.height(24.dp))
                Text(
                    text = overallProgressTitle,
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.SemiBold
                )

                if (viewModel.currentStepIndex >= 0 && viewModel.currentStepIndex < testSteps.size) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = stringResource(testSteps[viewModel.currentStepIndex].label),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            BenchmarkConfig.TestCategory.entries.forEach { cat ->
                val isCurrentCategory = viewModel.currentStepIndex >= 0 &&
                        viewModel.currentStepIndex < testSteps.size &&
                        testSteps[viewModel.currentStepIndex].category == cat

                StatCard(
                    label = cat.name,
                    percent = viewModel.categoryProgressPercent[cat] ?: 0,
                    subLabel = if (isCurrentCategory && !viewModel.finished)
                        stringResource(testSteps[viewModel.currentStepIndex].label)
                    else
                        "${viewModel.categoryScores[cat] ?: 0}",
                    showLoading = isCurrentCategory && !viewModel.finished,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }

        if (viewModel.finished) {
            Spacer(modifier = Modifier.height(24.dp))
            Button(
                onClick = onBackToMenu,
                modifier = Modifier.fillMaxWidth().height(56.dp),
                shape = RoundedCornerShape(16.dp)
            ) {
                Text(backToMenuString, style = MaterialTheme.typography.titleMedium)
            }
        }

        Spacer(modifier = Modifier.height(12.dp))
    }

    if (viewModel.showIntegrityDialog) {
        AlertDialog(
            onDismissRequest = { viewModel.showIntegrityDialog = false },
            title = { Text(text = stringResource(id = R.string.integrity_error_title)) },
            text = { Text(text = stringResource(id = R.string.integrity_error_text)) },
            icon = { Icon(imageVector = Icons.Default.DeveloperBoardOff, contentDescription = null) },
            confirmButton = {
                TextButton(onClick = { viewModel.showIntegrityDialog = false }) {
                    Text("ОК")
                }
            }
        )
    }
}
