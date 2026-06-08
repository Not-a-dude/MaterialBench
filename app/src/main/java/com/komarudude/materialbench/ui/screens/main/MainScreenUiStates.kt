package com.komarudude.materialbench.ui.screens.main

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