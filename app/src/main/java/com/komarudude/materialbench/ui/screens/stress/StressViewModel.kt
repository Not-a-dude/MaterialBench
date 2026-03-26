package com.komarudude.materialbench.ui.screens.stress

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.komarudude.materialbench.data.native.NativeLib
import com.komarudude.materialbench.data.system.HardwareProvider
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

data class StressPoint(val timeSec: Float, val temp: Float)

class StressViewModel(application: Application) : AndroidViewModel(application) {
    private val hardwareProvider = HardwareProvider(application)
    
    var isStressRunning by mutableStateOf(false)
        private set

    val modelProducer = CartesianChartModelProducer()
    private val points = mutableStateListOf<StressPoint>()
    private var startTime = 0L
    private var monitorJob: Job? = null
    
    var batteryTemp by mutableFloatStateOf(hardwareProvider.getBatteryTemp() ?: 0f)
        private set

    var showHighTempDialog by mutableStateOf(false)
    var showLowTempDialog by mutableStateOf(false)
    var triggeredTemp by mutableFloatStateOf(0f)

    private val highTemperatureThreshold = 45.0f
    private val lowTemperatureThreshold = 17.0f

    fun toggleStress(cpu: Boolean, gpu: Boolean) {
        if (isStressRunning) {
            stopStress()
        } else {
            startStress(cpu, gpu)
        }
    }

    private fun startStress(cpu: Boolean, gpu: Boolean) {
        isStressRunning = true
        points.clear()
        startTime = System.currentTimeMillis()
        
        if (cpu) NativeLib.nativeStartCpuStress()
        if (gpu) NativeLib.nativeStartGpuStress()

        monitorJob = viewModelScope.launch {
            while (isStressRunning) {
                val currentTemp = hardwareProvider.getBatteryTemp() ?: 0f
                batteryTemp = currentTemp
                val currentSec = ((System.currentTimeMillis() - startTime) / 1000L).toFloat()
                points.add(StressPoint(currentSec, currentTemp))

                modelProducer.runTransaction {
                    lineSeries {
                        series(x = points.map { it.timeSec }, y = points.map { it.temp })
                    }
                }

                if (currentTemp >= highTemperatureThreshold) {
                    triggeredTemp = currentTemp
                    showHighTempDialog = true
                    stopStress()
                } else if (currentTemp <= lowTemperatureThreshold) {
                    triggeredTemp = currentTemp
                    showLowTempDialog = true
                    stopStress()
                }
                delay(1000L)
            }
        }
    }

    fun stopStress() {
        isStressRunning = false
        monitorJob?.cancel()
        NativeLib.nativeStopCpuStress()
        NativeLib.nativeStopGpuStress()
    }

    override fun onCleared() {
        super.onCleared()
        stopStress()
    }
}
