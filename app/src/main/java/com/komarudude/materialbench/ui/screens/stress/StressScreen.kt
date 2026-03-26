package com.komarudude.materialbench.ui.screens.stress

import android.content.pm.PackageManager
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.LinearWavyProgressIndicator
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MultiChoiceSegmentedButtonRow
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.komarudude.materialbench.R
import com.komarudude.materialbench.ui.screens.stress.components.TemperatureChart
import com.patrykandpatrick.vico.core.cartesian.CartesianMeasuringContext
import com.patrykandpatrick.vico.core.cartesian.data.CartesianValueFormatter

@OptIn(ExperimentalMaterial3ExpressiveApi::class, ExperimentalMaterial3Api::class)
@Composable
fun StressScreen(modifier: Modifier = Modifier, viewModel: StressViewModel = viewModel()) {
    val context = LocalContext.current

    BackHandler(enabled = viewModel.isStressRunning) {
        Toast.makeText(context, context.getString(R.string.stress_running), Toast.LENGTH_SHORT).show()
    }

    val pm: PackageManager = context.packageManager
    val hasVulkanCompute = pm.hasSystemFeature("android.hardware.vulkan.compute")

    LaunchedEffect(Unit) {
        if (!hasVulkanCompute) {
            Toast.makeText(context, R.string.vulkan_compute_unsupported, Toast.LENGTH_SHORT).show()
        }
    }

    val stressStartedToast = stringResource(R.string.stress_started)
    val stressStoppedToast = stringResource(R.string.stress_stopped)

    val items = listOf("CPU", "GPU")
    var selectedIndices by rememberSaveable { mutableStateOf(setOf(0)) }

    val bottomAxisValueFormatter = CartesianValueFormatter { _: CartesianMeasuringContext, axisValue: Double, _ ->
        val totalSeconds = axisValue.toLong().coerceAtLeast(0L)
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

    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
            modifier = Modifier
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
        ) {

            Text(
                text = stringResource(R.string.select_stress_modes),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )

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
                        shape = SegmentedButtonDefaults.itemShape(
                            index = index,
                            count = items.size
                        ),
                        label = {
                            Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    )
                }
            }

            Spacer(Modifier.height(32.dp))

            TemperatureChart(modelProducer = viewModel.modelProducer, bottomAxisValueFormatter = bottomAxisValueFormatter)

            Spacer(Modifier.height(32.dp))

            if (viewModel.isStressRunning) {
                LinearWavyProgressIndicator(
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(32.dp))
            }

            Button(
                onClick = {
                    val running = viewModel.isStressRunning
                    viewModel.toggleStress(
                        cpu = selectedIndices.contains(0),
                        gpu = selectedIndices.contains(1)
                    )
                    Toast.makeText(context, if (!running) stressStartedToast else stressStoppedToast, Toast.LENGTH_SHORT).show()
                },
                modifier = Modifier.height(56.dp).fillMaxWidth(0.7f),
                shape = RoundedCornerShape(16.dp)
            ) {
                if (viewModel.isStressRunning) {
                    LoadingIndicator(
                        modifier = Modifier.size(24.dp),
                        color = MaterialTheme.colorScheme.onPrimary
                    )
                    Spacer(Modifier.width(12.dp))
                }
                Text(
                    text = if (!viewModel.isStressRunning) {
                        stringResource(R.string.stress_start)
                    } else {
                        stringResource(R.string.stress_stop)
                    },
                    style = MaterialTheme.typography.titleMedium
                )
            }
        }
    }

    if (viewModel.showHighTempDialog) {
        AlertDialog(
            onDismissRequest = { viewModel.showHighTempDialog = false },
            title = { Text(stringResource(R.string.high_bat_temp_dialog_title)) },
            text = { Text(stringResource(R.string.high_bat_temp_dialog_msg, viewModel.triggeredTemp)) },
            confirmButton = {
                TextButton(onClick = { viewModel.showHighTempDialog = false }) {
                    Text(stringResource(R.string.ok))
                }
            }
        )
    } else if (viewModel.showLowTempDialog) {
        AlertDialog(
            onDismissRequest = { viewModel.showLowTempDialog = false },
            title = { Text(stringResource(R.string.low_bat_temp_dialog_title)) },
            text = { Text(stringResource(R.string.low_bat_temp_dialog_msg, viewModel.triggeredTemp)) },
            confirmButton = {
                TextButton(onClick = { viewModel.showLowTempDialog = false }) {
                    Text(stringResource(R.string.ok))
                }
            }
        )
    }
}
