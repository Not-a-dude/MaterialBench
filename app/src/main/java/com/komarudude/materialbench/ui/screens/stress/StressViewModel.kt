package com.komarudude.materialbench.ui.screens.stress

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.komarudude.materialbench.data.native.NativeLib
import com.komarudude.materialbench.data.native.StressDataCallback
import com.komarudude.materialbench.data.system.HardwareProvider
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.milliseconds

data class StressPoint(val timeSec: Float, val temp: Float, val cpuPoints: Int, val gpuMflops: Int)

class StressViewModel(application: Application) : AndroidViewModel(application), StressDataCallback {
    private val hardwareProvider = HardwareProvider(application)
    
    var isStressRunning by mutableStateOf(false)
        private set

    val modelProducer = CartesianChartModelProducer()
    
    private val points = mutableStateListOf<StressPoint>()
    private var startTime = 0L
    private var monitorJob: Job? = null
    
    var batteryTemp by mutableFloatStateOf(hardwareProvider.getBatteryTemp() ?: 0f)
        private set

    var currentCpuPoints by mutableIntStateOf(0)
        private set
    var currentGpuMflops by mutableIntStateOf(0)
        private set

    @Volatile
    private var hasCpuSample = false

    @Volatile
    private var hasGpuSample = false

    var showHighTempDialog by mutableStateOf(false)
    var showLowTempDialog by mutableStateOf(false)
    var triggeredTemp by mutableFloatStateOf(0f)

    private val highTemperatureThreshold = 45.0f
    private val lowTemperatureThreshold = 17.0f

    override fun onStressData(timestamp: Long, cpuPerf: Int?, gpuMflops: Int?) {
        cpuPerf?.let {
            currentCpuPoints = it
            hasCpuSample = true
        }
        gpuMflops?.let {
            currentGpuMflops = it
            hasGpuSample = true
        }
    }

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
        currentCpuPoints = 0
        currentGpuMflops = 0
        hasCpuSample = false
        hasGpuSample = false
        startTime = System.currentTimeMillis()
        
        if (cpu) NativeLib.nativeStartCpuStress(this)
        if (gpu) NativeLib.nativeStartGpuStress(this)

        monitorJob = viewModelScope.launch {
            while (isStressRunning) {
                val currentTemp = hardwareProvider.getBatteryTemp() ?: 0f
                batteryTemp = currentTemp
                val currentSec = ((System.currentTimeMillis() - startTime) / 1000L).toFloat()
                
                val selectedSamplesReady = (!cpu || hasCpuSample) && (!gpu || hasGpuSample)
                if (selectedSamplesReady) {
                    points.add(StressPoint(currentSec, currentTemp, currentCpuPoints, currentGpuMflops))

                    modelProducer.runTransaction {
                        lineSeries {
                            series(x = points.map { it.timeSec }, y = points.map { it.temp })
                        }
                        lineSeries {
                            if (cpu) {
                                series(x = points.map { it.timeSec }, y = points.map { it.cpuPoints.toFloat() })
                            }
                            if (gpu) {
                                series(x = points.map { it.timeSec }, y = points.map { it.gpuMflops.toFloat() })
                            }
                        }
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
                delay(1000L.milliseconds)
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
