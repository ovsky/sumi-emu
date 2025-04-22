package org.sumi.sumi_emu.thermal

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import kotlin.math.roundToInt
import kotlin.math.max

class ThermalMonitor(private val context: Context) {

    public var lastCheckedTemperature: Int = 0;

    private var currentTemperature = 0
    public var throttleFactor = 1.0f

    // Configurable thresholds (in °C)
    private val warningThreshold = 95
    private val criticalThreshold = 120 // Stop emulation at this temperature
    //
    fun update(): Float {
        currentTemperature = getCurrentTemperature()
        throttleFactor = calculateThrottle()
        return throttleFactor
    }

    // private fun getCurrentTemperature(): Int {
    //     return try {
    //         val bm = context.getSystemService(Context.BATTERY_SERVICE) as BatteryManager
    //         if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
    //         //    bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_STATUS) / 10
    //         } else {
    //             30 // Safe default for unsupported API levels
    //         }
    //     } catch (e: Exception) {
    //         30 // Safe default if reading fails
    //     }
    // }

    private fun getBatteryTemperature(context: Context): Float {
        try {
            val batteryIntent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            // Temperature in tenths of a degree Celsius
            val temperature = batteryIntent?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0
            // Convert to degrees Celsius
            return temperature / 10.0f
        } catch (e: Exception) {
            // Log.error("[ThermalMonitor] Failed to get battery temperature: ${e.message}")
            return 0.0f
        }
    }

    public fun getCurrentTemperature(): Int {

        val temperature = getBatteryTemperature(context)
        val fahrenheit = (temperature * 9f / 5f) + 32f

        lastCheckedTemperature = fahrenheit.roundToInt();
        return fahrenheit.roundToInt();

        // val currentTemp = getCurrentTemperatureFull(this.context);
        // lastCheckedTemperature = currentTemp?.roundToInt() ?: 30;

        // return currentTemp?.roundToInt() ?: 30;
    }

    // public fun getCurrentTemperature(): Int {

    //     val currentTemp = getCurrentTemperatureFull(this.context);
    //     lastCheckedTemperature = currentTemp?.roundToInt() ?: 30;

    //     return currentTemp?.roundToInt() ?: 30;
    // }

    // public fun getCurrentTemperatureFull(context: Context): Float? {
    //     // Create an IntentFilter for battery change events.
    //     val intentFilter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)

    //     // Register a null receiver to get the sticky broadcast intent.
    //     // This gives us the last known battery status immediately.
    //     val batteryStatus: Intent? = context.registerReceiver(null, intentFilter)

    //     // Use the safe call operator ?. and let to execute only if batteryStatus is not null.
    //     return batteryStatus?.let { intent ->
    //         // Get the temperature value from the intent extras.
    //         // BatteryManager.EXTRA_TEMPERATURE provides the value in tenths of degrees Celsius.
    //         // Provide a default value (e.g., -1) in case the extra is missing.
    //         val temperatureInt = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1)

    //         // Check if a valid temperature was retrieved (i.e., not the default fallback).
    //         if (temperatureInt != -1) {
    //             // Convert the value from tenths of degrees Celsius to degrees Celsius.
    //             temperatureInt / 10.0f
    //         } else {
    //             // Return null if the temperature extra wasn't found.
    //             null
    //         }
    //     } // If batteryStatus was null in the first place, let returns null.
    // }

    /// Calculates the throttle factor based on the current temperature
    /// The throttle factor is a value between 0.0 (full stop) and 1.0 (no throttling)
    /// The throttle factor is calculated based on the current temperature and the defined thresholds.
    private fun calculateThrottle(): Float {
        return when {
            currentTemperature >= criticalThreshold -> 0f // Full stop
            currentTemperature >= warningThreshold -> {
                val tempRange = criticalThreshold - warningThreshold
                val tempDelta = currentTemperature - warningThreshold
                max(0.1f, 1.0f - (tempDelta.toFloat() / tempRange * 0.9f))
            }
            else -> 1.0f // No throttling
        }
    }
}