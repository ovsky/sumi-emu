package org.sumi.sumi_emu.thermal

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import kotlin.math.roundToInt
import kotlin.math.max
import android.util.Log
import org.sumi.sumi_emu.NativeLibrary
import android.os.Process
import android.os.SystemClock
//import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.FragmentActivity
import androidx.core.content.ContextCompat
import kotlinx.coroutines.currentCoroutineContext
import kotlin.concurrent.thread
import kotlin.contracts.contract
import android.util.Log as AndroidLog
import android.app.AlertDialog

// class ThermalMonitor(private val context: Context) : Fragment(), SurfaceHolder.Callback {
public class ThermalMonitor(private val context: Context) {

    private var currentTemperature = 0

    // Configurable thresholds (in °C)
    private val warningThreshold = 100 // Start throttling at this temperature
    private val criticalThreshold = 130 // Stop emulation at this temperature
    private val restoreThreshold = 98 // Restore normal operation at this temperature

    // Throttle state flags
    private var throttleAlertShown = false
    private var criticalAlertShown = false
    private val thermalCheckInterval = 5000L // 5 second

    // CPU state flags
    private var processorBasicState = Process.THREAD_PRIORITY_FOREGROUND
    private var processorThrottleState = Process.THREAD_PRIORITY_BACKGROUND

    // CPU performance limits
    private val cpuThrottleLimitPercent = 10 // CPU limit percentage
    // @Volatile
companion object {
    @Volatile
    @JvmStatic
    public var isThrottling = false

    @JvmStatic
    public fun StartThermalMonitor(context: Context)
    {
        AndroidLog.d("ThermalMonitor", "Starting thermal monitor...")

        val thermalMonitor = ThermalMonitor(context)
        thermalMonitor.setInitialProcessorState()
        thermalMonitor.startMonitoring(context)
    }
}
@Volatile
public var isRunning = true

    public fun setInitialProcessorState() {
        setThreadsPriority(Process.THREAD_PRIORITY_FOREGROUND, true)
        setThreadsLimiter(cpuLimitPercent = 100, updateMainThread = true)
        AndroidLog.d("ThermalMonitor", "Setting initial processor state to foreground")
    }

    public fun startMonitoring(context : Context) {

        AndroidLog.d("ThermalMonitor", "Cooler + Starting thermal monitor...")

        isRunning = true

        val handler = android.os.Handler(android.os.Looper.getMainLooper())
        val runnable = object : Runnable
        {
            override fun run() {

                val temperature = getBatteryTemperature(context)
                val fahrenheit = (temperature * 9f / 5f) + 32f

                currentTemperature = temperature.roundToInt()

                if (fahrenheit >= criticalThreshold) {
                    if (!criticalAlertShown)
                    {
                        showCriticalTemperatureAlert(context)
                        criticalAlertShown = true
                    }
                }
                else if (fahrenheit >= warningThreshold)
                {
                    if (!throttleAlertShown)
                    {
                        showStartThrottlingAlert(context)

                        setThreadsPriority(Process.THREAD_PRIORITY_BACKGROUND, true)
                        setThreadsLimiter(cpuLimitPercent = cpuThrottleLimitPercent, updateMainThread = true)

                        throttleAlertShown = true
                        isThrottling = true
                    }
                }
                else if (fahrenheit <= restoreThreshold)
                {
                    if (throttleAlertShown)
                    {
                        setThreadsPriority(Process.THREAD_PRIORITY_FOREGROUND, true)
                        setThreadsLimiter(cpuLimitPercent = 100, updateMainThread = true)
                        isThrottling = false
                        throttleAlertShown = false
                    }
                }
                handler.postDelayed(this, thermalCheckInterval)
            }
        }

        handler.post(runnable)
        }
    }

    private fun setThreadsPriority(threadPriotity: Int = Process.THREAD_PRIORITY_BACKGROUND, updateMainThread: Boolean = false) {

        val rootGroup = getRootThreadGroup()
        val threads = enumerateAllThreads(rootGroup)

        AndroidLog.d("ThermalMonitor", "Found ${threads.size} threads")

        for (t in threads) {
            if (t.isAlive && (t != Thread.currentThread() || updateMainThread))
            {
                AndroidLog.d("ThermalMonitor", "Setting thread '${t.name}' to background priority")
                android.os.Process.setThreadPriority(t.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
            }
        }
    }

    private fun setThreadsLimiter(cpuLimitPercent: Int = 30, updateMainThread: Boolean = false) {

        val rootGroup = getRootThreadGroup()
        val threads = enumerateAllThreads(rootGroup)

        AndroidLog.d("ThermalMonitor", "Found ${threads.size} threads")
        AndroidLog.d("ThermalMonitor", "Setting CPU limit to $cpuLimitPercent%")


        for (t in threads) {
            if (ThermalMonitor.isThrottling && t.isAlive && (t != Thread.currentThread() || updateMainThread))
            {
                AndroidLog.d("ThermalMonitor", "Setting thread '${t.name}' to  $cpuLimitPercent% CPU limit")

                // if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC") && !t.name.startsWith("main")) {
                //     AndroidLog.d("Cooler", "Setting thread '${t.name}' to background priority")
                //     android.os.Process.setThreadPriority(t.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
                // }

                val cycleMs = 100L
                val workMs = (cycleMs * cpuLimitPercent) / 100
                val sleepMs = cycleMs - workMs

                while (t.isAlive) {
                    val start = SystemClock.uptimeMillis()

                    // Simulate work with easy task to keep CPU busy
                    while (SystemClock.uptimeMillis() - start < workMs) {
                        Math.round(123456.789f)
                    }

                    try {
                        Thread.sleep(sleepMs)
                    } catch (e: InterruptedException) {
                        break
                    }
                }
            }
        }
    }

    private fun getRootThreadGroup(): ThreadGroup {
        var rootGroup = Thread.currentThread().threadGroup
        var parent: ThreadGroup?
        while (rootGroup?.parent.also { parent = it } != null) {
            rootGroup = parent
        }
        return rootGroup!!
    }

    private fun enumerateAllThreads(group: ThreadGroup): List<Thread> {
        var groupThreads = arrayOfNulls<Thread>(group.activeCount() * 2)
        val count = group.enumerate(groupThreads, true)
        return groupThreads.filterNotNull().take(count)
    }

    private fun setMainThreadPriority(threadPriority: Int) {
        try {
            AndroidLog.d("ThermalMonitor", "Setting main thread priority to $threadPriority")
            Process.setThreadPriority(Process.myTid(), threadPriority)
        } catch (e: Exception) {
            AndroidLog.e("ThermalMonitor", "Failed to set main thread priority: ${e.message}")
        }
    }

    fun onDestroy() {
        ThermalMonitor.isThrottling = false
    }

    private fun showStartThrottlingAlert(context: Context) {
        // (context as? FragmentActivity)?.runOnUiThread {
            AlertDialog.Builder(context)
                .setTitle("Thermal Throttling")
                .setMessage("Device is getting hot!\nEmulation will be throttled to prevent overheating.")
                // .setPositiveButton("OK") { _, _ -> ThermalMonitor.throttleAlertShown = true }
                // .setPositiveButton("OK") { dialog, _ -> dialog.dismiss() }
                .setPositiveButton("OK") { _, _ -> (context as? Activity)?.finish() }
                .setCancelable(false)
                .show()
        // }
    }

    private fun showCriticalTemperatureAlert(context: Context) {
        // context.runOnUiThread {
            AlertDialog.Builder(context)
                .setTitle("Critical Temperature")
                .setMessage("Device is way too hot!\nEmulation stopped for your safety. Try another CPU/GPU settings.")
                .setPositiveButton("OK") { _, _ -> (context as? Activity)?.finish() }
                .setCancelable(false)
                .show()
        // }
    }

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
