package com.komarudude.materialbench.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DeviceThermostat
import androidx.compose.material.icons.filled.LocalFireDepartment
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.tooling.preview.Preview
import com.komarudude.materialbench.ui.theme.MaterialBenchTheme
import com.komarudude.materialbench.data.native.NativeLib.nativeCleanup
import com.komarudude.materialbench.ui.screens.main.BenchMainScreen

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

@Preview(showBackground = true)
@Composable
fun BenchMainScreenPreview() {
    MaterialBenchTheme {
        // BenchMainScreen()
    }
}
