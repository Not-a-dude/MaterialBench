package com.komarudude.materialbench.ui.screens.main

import android.app.Application
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.komarudude.materialbench.BenchScores
import com.komarudude.materialbench.utils.RetrofitClient
import com.komarudude.materialbench.utils.ScoreRequest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class BenchMainViewModel(application: Application) : AndroidViewModel(application) {
    private val app = getApplication<Application>()

    private val _overallScore = mutableStateOf("0")
    val overallScore: State<String> = _overallScore

    private val _benchmarks = mutableStateOf<List<Benchmark>>(emptyList())
    val benchmarks: State<List<Benchmark>> = _benchmarks

    private val _percentileRank = mutableStateOf<String?>(null)
    val percentileRank: State<String?> = _percentileRank

    private val _isFetchingRank = mutableStateOf(false)
    val isFetchingRank: State<Boolean> = _isFetchingRank

    val packageInfo: PackageInfo = try {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            app.packageManager.getPackageInfo(app.packageName, PackageManager.PackageInfoFlags.of(0))
        } else {
            @Suppress("DEPRECATION")
            app.packageManager.getPackageInfo(app.packageName, 0)
        }
    } catch (_: Exception) {
        PackageInfo().apply { versionName = "UNKNOWN"; longVersionCode = 1L }
    }

    fun loadScores(
        cpuTitle: String, cpuDesc: String, cpuIcon: String,
        gpuTitle: String, gpuDesc: String, gpuIcon: String,
        memTitle: String, memDesc: String, memIcon: String,
        aiTitle: String, aiDesc: String, aiIcon: String,
        subBenchKeys: Map<String, List<SubBenchmark>>
    ) {
        viewModelScope.launch {
            val scores = withContext(Dispatchers.IO) {
                val currentOverall = BenchScores.getScore(app, "overall_score")

                val cpuSub = subBenchKeys["cpu"] ?: emptyList()
                val gpuSub = subBenchKeys["gpu"] ?: emptyList()
                val memSub = subBenchKeys["mem"] ?: emptyList()
                val aiSub = subBenchKeys["ai"] ?: emptyList()

                listOf(cpuSub, gpuSub, memSub, aiSub).flatten().forEach { sub ->
                    val s = BenchScores.getScore(app, sub.scoreKey)
                    sub.score = if (s == 0) null else s.toString()
                }

                val cpuScore = BenchScores.getScore(app, "CPU Benchmark")
                val gpuScore = BenchScores.getScore(app, "GPU Benchmark")
                val memScore = BenchScores.getScore(app, "Memory Test")
                val aiScore = BenchScores.getScore(app, "AI Test")

                val benchmarkList = listOf(
                    Benchmark(cpuTitle, cpuDesc, cpuIcon, if (cpuScore == 0) null else cpuScore.toString(), {}, cpuSub),
                    Benchmark(gpuTitle, gpuDesc, gpuIcon, if (gpuScore == 0) null else gpuScore.toString(), {}, gpuSub),
                    Benchmark(memTitle, memDesc, memIcon, if (memScore == 0) null else memScore.toString(), {}, memSub),
                    Benchmark(aiTitle, aiDesc, aiIcon, if (aiScore == 0) null else aiScore.toString(), {}, aiSub)
                )

                Triple(currentOverall.toString(), benchmarkList, currentOverall)
            }

            _overallScore.value = scores.first
            _benchmarks.value = scores.second

            val rawScore = scores.third
            if (rawScore > 0) {
                fetchPercentileRank(rawScore, packageInfo.longVersionCode)
            }
        }
    }

    private fun fetchPercentileRank(score: Int, versionCode: Long) {
        _isFetchingRank.value = true
        viewModelScope.launch {
            try {
                val response = withContext(Dispatchers.IO) {
                    RetrofitClient.apiService.getRank(ScoreRequest(score, versionCode))
                }
                _percentileRank.value = "%.2f".format(response.percentile) + "%"
            } catch (e: Exception) {
                _percentileRank.value = null
                Log.e("MainViewModel", "Error fetching percentile rank: ${e.message}")
            } finally {
                _isFetchingRank.value = false
            }
        }
    }
}
