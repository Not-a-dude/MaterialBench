package com.komarudude.materialbench

import android.content.Context

object BenchScores {
    private const val PREFS_NAME = "BenchScores"

    fun saveScore(context: Context, category: String, score: Int) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putInt(category, score).commit()
    }

    fun getScore(context: Context, category: String): Int {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getInt(category, 0)
    }
}
