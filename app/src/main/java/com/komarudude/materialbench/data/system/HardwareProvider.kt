package com.komarudude.materialbench.data.system

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.util.Log

class HardwareProvider(context: Context) {
    private val appContext = context.applicationContext
    fun getBatteryTemp(): Float? {
        val batteryStatus: Intent? = appContext.registerReceiver(
            null,
            IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        )

        val tempDeciCelsius = batteryStatus?.getIntExtra(
            BatteryManager.EXTRA_TEMPERATURE,
            -1
        )

        return if (tempDeciCelsius != null && tempDeciCelsius != -1) {
            tempDeciCelsius / 10.0f
        } else {
            Log.w("BatteryInfo", "Температура батареи недоступна.")
            null
        }
    }
}