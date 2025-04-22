package org.sumi.sumi_emu.thermal

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import kotlin.math.roundToInt
import org.sumi.sumi_emu.features.settings.model.ShortSetting
import org.sumi.sumi_emu.features.settings.model.BooleanSetting
import android.os.Process
import android.os.SystemClock
import android.os.Handler
import android.os.Looper
import androidx.fragment.app.FragmentActivity
import kotlin.concurrent.thread
import android.util.Log as AndroidLog
import android.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import org.sumi.sumi_emu.utils.NativeConfig


public class ThermalMonitor(private val context: Context) : AppCompatActivity() {

    private var currentTemperature = 0

    // Configurable thresholds (in °C)
    private val warningThreshold = 100 // Start throttling at this temperature
    private val criticalThreshold = 130 // Stop emulation at this temperature
    private val restoreThreshold = 98 // Restore normal operation at this temperature

    // Throttle state flags
    private var throttleAlertShown = false
    private var criticalAlertShown = false
    private val thermalCheckInterval = 1000L // 1 second

    // CPU state flags
    private var processorBasicState = Process.THREAD_PRIORITY_FOREGROUND
    private var processorThrottleState = Process.THREAD_PRIORITY_BACKGROUND

    private val cpuThrottleLimitPercent = 10 // CPU limit percentage

    public var isThrottling = false
    public var isRunning = true

    public fun StartThermalMonitor()
    {
        val thermalMonitor = ThermalMonitor(context)
        thermalMonitor.setInitialProcessorState()
        thermalMonitor.startThermalMonitoring()
    }

    public fun setInitialProcessorState() {
        //setMainThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
        //setMainThreadLimiter(10050);

        //setThreadsPriority(Process.THREAD_PRIORITY_BACKGROUND, true)
        //setThreadsLimiter(cpuLimitPercent = 100, updateMainThread = true)
        AndroidLog.d("ThermalMonitor", "Setting initial processor state to foreground")

        //coolDownAllThreads();
        //startFrameLimiter(30) // Start frame limiter at 30 FPS
    }

    private fun startFrameLimiter(targetFps: Int) {
        val frameIntervalMs = 1000 / targetFps

        thread(start = true) {
            while (isRunning && isThrottling) {
                val frameStart = SystemClock.uptimeMillis()

                // Replace with your rendering code
                AndroidLog.d("ThermalMonitor" , "Rendering frame...")

                val elapsed = SystemClock.uptimeMillis() - frameStart
                val sleep = frameIntervalMs - elapsed
                if (sleep > 0) Thread.sleep(sleep)
            }
        }
    }

    private fun slowDownAllThreads() {
        val rootGroup = getRootThreadGroup()
        val threads = enumerateAllThreads(rootGroup)

        while (isThrottling)
        {
            for (t in threads) {
                try {
    //                if (t != null && t.isAlive && t != Thread.currentThread()) {
                    if (t != null && t.isAlive) {
                        // Skip system threads (filter by name or group as needed)
    //                    if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC") && !t.name.startsWith("main")) {
                        if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC")) {                        // Log.debug("Cooler + Setting thread '${t.name}' to background priority")
                            // android.os.Process.setThreadPriority(t.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
                            Thread.sleep(30) // Wait for 30 milliseconds before processing the next thread
                        }
                    }
                } catch (e: Exception) {
                    AndroidLog.d("ThermalMonitor",  "Failed to adjust thread '${t.name}': ${e.message}")
                }
            }
        }
    }

    private fun coolDownAllThreads(threadPriority: Int = Process.THREAD_PRIORITY_BACKGROUND) {
        val rootGroup = getRootThreadGroup()
        val threads = enumerateAllThreads(rootGroup)

        AndroidLog.d("ThermalMonitor", "Found ${threads.size} threads")

        for (t in threads) {
            try {
                // if (t != null && t.isAlive && t != Thread.currentThread()) {
                if (t != null && t.isAlive) {
                    // Skip system threads (filter by name or group as needed)
                    // if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC") && !t.name.startsWith("main")) {
                    if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC")) {
                        AndroidLog.d("Cooler", "Setting thread ${t.name} to background priority")
                        android.os.Process.setThreadPriority(t.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
                    }
                }
            } catch (e: Exception) {
                AndroidLog.d("Cooler", "Failed to adjust thread '${t.name}': ${e.message}")
            }
        }
    }

    private fun startThermalMonitoring() {

    Handler(Looper.getMainLooper()).postDelayed(object : Runnable {
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
                ThrottleGpuSpeedLimit();

                if (!throttleAlertShown)
                {
                    showStartThrottlingAlert()

                    slowDownAllThreads()
//                    startFrameLimiter(targetFps = 2) // Start frame limiter at 10 FPS
                    coolDownAllThreads()

                    throttleAlertShown = true
                    isThrottling = true
                }
            }
            else if (fahrenheit >= restoreThreshold)
            {
                if (throttleAlertShown)
                {
                    ResetGpuSpeedLimit()
                    showCriticalTemperatureAlert()
                    // setThreadsPriority(Process.THREAD_PRIORITY_FOREGROUND, true)
                    // setThreadsLimiter(cpuLimitPercent = 100, updateMainThread = true)
                    isThrottling = false
                    throttleAlertShown = false
                }
            }
        }
    }, thermalCheckInterval)
}

    private fun showStartThrottlingAlert() {
        (context as? Activity)?.runOnUiThread {
            AlertDialog.Builder(context)
                .setTitle("Thermal Throttling")
                .setMessage("Device is getting hot!\nEmulation will be throttled to prevent overheating.")
                .setPositiveButton("OK") { _, _ -> throttleAlertShown = true }
                .setCancelable(false)
                .show()
        }
    }

    private fun showCriticalTemperatureAlert()
    {
        (context as? Activity)?.runOnUiThread {
            AlertDialog.Builder(context)
                .setTitle("Critical Temperature")
                .setMessage("Device is way too hot!\nEmulation stopped for your safety. Try another CPU/GPU settings.")
                .setPositiveButton("OK") { _, _ -> (context as? Activity)?.finish() }
                .setCancelable(false)
                .show()
            }
        }

        override fun onDestroy() {
            isThrottling = false
            isRunning = false

            ResetGpuSpeedLimit()

            super.onDestroy()
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

        private fun showStartThrottlingAlert(context: Context) {
             (context as? FragmentActivity)?.runOnUiThread {
                AlertDialog.Builder(context)
                    .setTitle("Thermal Throttling")
                    .setMessage("Device is getting hot!\nEmulation will be throttled to prevent overheating.")
                    .setPositiveButton("OK") { _, _ -> (context as? Activity)?.finish() }
                    .setCancelable(false)
                    .show()
             }
        }

        private fun showCriticalTemperatureAlert(context: Context) {
            (context as? FragmentActivity)?.runOnUiThread {
                AlertDialog.Builder(context)
                    .setTitle("Critical Temperature")
                    .setMessage("Device is way too hot!\nEmulation stopped for your safety. Try another CPU/GPU settings.")
                    .setPositiveButton("OK") { _, _ -> (context as? Activity)?.finish() }
                    .setCancelable(false)
                    .show()
             }
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

    companion object {
        @JvmStatic

        var originalGpuSpeedLimit = 0;
        var originalGpuSpeedLimitUsage = false;

        public fun SetGpuSpeedLimit(value: Int, useLimiter : Boolean = true) {

            if (originalGpuSpeedLimit == 0)
            {
                originalGpuSpeedLimitUsage = BooleanSetting.RENDERER_USE_SPEED_LIMIT.getBoolean()
                originalGpuSpeedLimit = ShortSetting.RENDERER_SPEED_LIMIT.getShort().toInt()
            }

            ShortSetting.RENDERER_SPEED_LIMIT.setShort(value.toShort())
            BooleanSetting.RENDERER_USE_SPEED_LIMIT.setBoolean(useLimiter)
            NativeConfig.setBoolean("use_speed_limit", useLimiter)
            NativeConfig.setShort("speed_limit", value.toShort())
            NativeConfig.saveGlobalConfig()
        }

        public fun ThrottleGpuSpeedLimit() {
            val throttleGpuMaxLimit = 120;
            val throttleGpuMinLimit = 50;
            val throttleGpuStep = 20;

            var currentGpuSpeedLimit = ShortSetting.RENDERER_SPEED_LIMIT
                .getShort()
                .toInt()
                .coerceIn(throttleGpuMinLimit, throttleGpuMaxLimit)

            if (currentGpuSpeedLimit >= throttleGpuMinLimit + throttleGpuStep) {
                currentGpuSpeedLimit -= throttleGpuStep;
            }

            SetGpuSpeedLimit(currentGpuSpeedLimit);
        }

        public fun ResetGpuSpeedLimit()
        {
            SetGpuSpeedLimit(originalGpuSpeedLimit, originalGpuSpeedLimitUsage)
        }
    }
}