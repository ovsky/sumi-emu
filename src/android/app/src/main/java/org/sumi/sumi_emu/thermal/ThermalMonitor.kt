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
import android.app.GameState
import android.net.Uri
import androidx.appcompat.app.AppCompatActivity
import org.sumi.sumi_emu.features.settings.ui.SettingsFragmentPresenter
import org.sumi.sumi_emu.features.settings.ui.SettingsViewModel
import org.sumi.sumi_emu.model.AddonViewModel
import org.sumi.sumi_emu.utils.NativeConfig
import org.sumi.sumi_emu.model.Game
import org.sumi.sumi_emu.model.GamesViewModel
import org.sumi.sumi_emu.ui.GamesFragment
import org.sumi.sumi_emu.utils.GameHelper
import org.sumi.sumi_emu.fragments.EmulationFragment
import org.sumi.sumi_emu.fragments.GamePropertiesFragment
import org.sumi.sumi_emu.fragments.GameInfoFragment
import org.sumi.sumi_emu.utils.FileUtil
import org.sumi.sumi_emu.NativeLibrary

public class ThermalMonitor(private val context: Context) : AppCompatActivity() {

    private var currentTemperature = 0

    // Configurable thresholds (in °C)
    private val warningThreshold = 100 // Begin throttling warning at this temperature
    private val initialThreshold = 105 // Start throttling at this temperature
    private val criticalThreshold = 130 // Stop emulation at this temperature
    private val restoreThreshold = 100 // Restore normal operation at this temperature

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

    // Constructor
    public fun StartThermalMonitor()
    {
        return; // Temporarily disabled

        val thermalMonitor = ThermalMonitor(context)

        thermalMonitor.setInitialProcessorState()
        thermalMonitor.startThermalMonitoring()
        setAllThreadsPriority(Process.THREAD_PRIORITY_FOREGROUND)
    }

    // Start the thermal monitoring loop process
    private fun startThermalMonitoring() {

        return; // Temporarily disabled

        Handler(Looper.getMainLooper()).postDelayed(object : Runnable {
            override fun run() {

                val temperature = getBatteryTemperature(context)
                val fahrenheit = (temperature * 9f / 5f) + 32f

                currentTemperature = temperature.roundToInt()

                if (fahrenheit >= criticalThreshold) {
                    if (!criticalAlertShown)
                    {
                        NativeLibrary.pauseEmulation();
                        showCriticalTemperatureAlert(context)
                        criticalAlertShown = true
                    }
                }
                else if (fahrenheit >= warningThreshold)
                {
                    if (!throttleAlertShown)
                    {
                        //slowDownAllThreads() // Disabled for now
                        showStartThrottlingAlert()
                        throttleAlertShown = true
                    }
                }else if (fahrenheit >= initialThreshold)
                {
                    ThrottleGpuSpeedLimit(context);

                    if (!isThrottling)
                    {
                        isThrottling = true
                        setAllThreadsPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    }
                }
                else if (fahrenheit >= restoreThreshold && isThrottling)
                {
                    ResetGpuSpeedLimit(context)

                    isThrottling = false
                    throttleAlertShown = false
                    criticalAlertShown = false

                    AndroidLog.d("ThermalMonitor", "Restoring normal operation")
                }
            }
        }, thermalCheckInterval)
    }

    // Set initial processor state
    public fun setInitialProcessorState() {
        AndroidLog.d("ThermalMonitor", "Setting initial processor state to foreground")
    }

    // Start the frame limiter process
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

    // Slow down all threads uaing a sleep method = currently disabled
    private fun slowDownAllThreads() {

        val rootGroup = getRootThreadGroup()
        val threads = enumerateAllThreads(rootGroup)

        while (isThrottling)
        {
            for (t in threads) {
                try {
                   if (t != null && t.isAlive && ((t as Thread) != Looper.getMainLooper())) {
                    // if (t != null && t.isAlive) {
                        // Skip system threads (filter by name or group as needed)
                    //    if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC") && !t.name.startsWith("main")) {
                        if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC")) {                        // Log.debug("Cooler + Setting thread '${t.name}' to background priority")
                            // android.os.Process.setThreadPriority(t.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
                            Thread.sleep(5) // Wait for 5 milliseconds before processing the next thread
                        }
                    }
                } catch (e: Exception) {
                    AndroidLog.d("ThermalMonitor",  "Failed to adjust thread '${t.name}': ${e.message}")
                }
            }
        }
    }

    // Limit the GPU speed by setting the thread priority
    private fun setAllThreadsPriority(threadPriority: Int = Process.THREAD_PRIORITY_BACKGROUND) {
        val rootGroup = getRootThreadGroup()
        val threads = enumerateAllThreads(rootGroup)

        AndroidLog.d("ThermalMonitor", "Found ${threads.size} threads")

        for (t in threads) {
            try {
                if (t != null && t.isAlive) {
                    if (!t.name.startsWith("Finalizer") && !t.name.startsWith("GC")) {
                        AndroidLog.d("ThermalMonitor", "Setting thread ${t.name} to background priority")
                        android.os.Process.setThreadPriority(t.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
                    }
                }
            } catch (e: Exception) {
                AndroidLog.d("Cooler", "Failed to adjust thread '${t.name}': ${e.message}")
            }
        }
    }


// UI Alerts

    // Show alert dialog for starting throttling
    // This alert is shown when the device temperature reaches the warning threshold
    // and the user has not yet been notified about throttling.
    private fun showStartThrottlingAlert() {
        (context as? Activity)?.runOnUiThread {
            AlertDialog.Builder(context)
                .setTitle("Thermal Throttling")
                .setMessage("Device is getting hot!\nEmulation will be throttled to prevent overheating but probably still fine.\nINFO: If you want to ignore the thermal throttling, please go to the current game settings and update emulation speed.")
                .setPositiveButton("OK") { _, _ -> throttleAlertShown = true }
                .setCancelable(false)
                .show()
        }
    }

    // Show alert dialog for critical temperature
    // This alert is shown when the device temperature reaches the critical threshold
    // and the user has not yet been notified about the critical temperature.
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

// UI Alerts

// Basic Methods
        // Restore the original thermal state
        override fun onDestroy() {
            ResetGpuSpeedLimit(context)

            isThrottling = false
            isRunning = false

            throttleAlertShown = false
            criticalAlertShown = false

            super.onDestroy()
        }

// Basic Methods

// Minor Methods

        // Get the root thread group
        private fun getRootThreadGroup(): ThreadGroup {
            var rootGroup = Thread.currentThread().threadGroup
            var parent: ThreadGroup?
            while (rootGroup?.parent.also { parent = it } != null) {
                rootGroup = parent
            }
            return rootGroup!!
        }

        // Enumerate all threads in the thread group
        private fun enumerateAllThreads(group: ThreadGroup): List<Thread> {
            var groupThreads = arrayOfNulls<Thread>(group.activeCount() * 2)
            val count = group.enumerate(groupThreads, true)
            return groupThreads.filterNotNull().take(count)
        }

        // Get the current battery temperature
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

// Minor Methods

// Companion Methods - GPU Speed Limit

    companion object {
        @JvmStatic

        var originalGpuSpeedLimit = 0;
        var originalGpuSpeedLimitUsage = false;

        // Set the GPU speed limit
        public fun SetGpuSpeedLimit(value: Int, useLimiter : Boolean = true, context: Context) {

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
            NativeConfig.savePerGameConfig()
        }

        // Throttle the GPU speed limit
        public fun ThrottleGpuSpeedLimit(context: Context) {
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

            SetGpuSpeedLimit(currentGpuSpeedLimit, true, context);
        }

        public fun ResetGpuSpeedLimit(context : Context)
        {
            SetGpuSpeedLimit(originalGpuSpeedLimit, originalGpuSpeedLimitUsage, context);
        }
    }

//endregion Companion Methods - GPU Speed Limit

}