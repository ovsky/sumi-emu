package org.sumi.sumi_emu.utils

import android.content.Context
import android.content.SharedPreferences
import android.os.Process
import android.util.Log
import java.io.BufferedReader
import java.io.FileReader
import java.util.concurrent.atomic.AtomicBoolean

class CoreManager private constructor(private val context: Context) {
    companion object {
        private const val TAG = "CoreManager"
        private const val LITTLE_CORES_START = 4 // Assuming cores 4+ are LITTLE cores
        private const val BIG_CORES_END = 3 // Assuming cores 0-3 are BIG cores

        @Volatile
        private var instance: CoreManager? = null

        fun getInstance(context: Context): CoreManager {
            return instance ?: synchronized(this) {
                instance ?: CoreManager(context.applicationContext).also { instance = it }
            }
        }
    }

    private val prefs: SharedPreferences = context.getSharedPreferences("core_settings", Context.MODE_PRIVATE)
    private val isInitialized = AtomicBoolean(false)
    private val availableCores: IntArray by lazy { getAvailableCores() }
    private val littleCores: IntArray by lazy { availableCores.filter { it >= LITTLE_CORES_START }.toIntArray() }
    private val bigCores: IntArray by lazy { availableCores.filter { it <= BIG_CORES_END }.toIntArray() }

    fun applyCoreSettings() {
        if (isInitialized.get()) return
        isInitialized.set(true)

        try {
            val cpuMode = prefs.getString("cpu_core_mode", "all") ?: "all"
            val maxCpuCores = prefs.getInt("max_cpu_cores", availableCores.size)
            val gpuMode = prefs.getString("gpu_core_mode", "all") ?: "all"
            val maxGpuCores = prefs.getInt("max_gpu_cores", 4)

            applyCpuSettings(cpuMode, maxCpuCores)
            applyGpuSettings(gpuMode, maxGpuCores)
        } catch (e: Exception) {
            Log.e(TAG, "Error applying core settings: ${e.message}")
        }
    }

    private fun applyCpuSettings(mode: String, maxCores: Int) {
        val targetCores = when (mode) {
            "big" -> bigCores
            "little" -> littleCores
            "custom" -> availableCores.take(maxCores).toIntArray()
            else -> availableCores
        }

        try {
            val rootGroup = getRootThreadGroup()
            val threads = enumerateAllThreads(rootGroup)

            threads.forEach { thread ->
                if (shouldProcessThread(thread)) {
                    try {
                        val targetCore = targetCores[thread.id % targetCores.size]
                        Process.setThreadAffinity(thread.id, intArrayOf(targetCore))
                        Log.d(TAG, "Set thread ${thread.name} to core $targetCore")
                    } catch (e: Exception) {
                        Log.e(TAG, "Error setting thread affinity: ${e.message}")
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error applying CPU settings: ${e.message}")
        }
    }

    private fun applyGpuSettings(mode: String, maxCores: Int) {
        // GPU core management implementation will depend on your specific GPU architecture
        // This is a placeholder for GPU-specific implementation
        when (mode) {
            "performance" -> {
                // Set GPU to high performance mode
                setGpuPerformanceMode()
            }
            "powersave" -> {
                // Set GPU to power saving mode
                setGpuPowerSaveMode()
            }
            "custom" -> {
                // Set custom GPU core count
                setGpuCoreCount(maxCores)
            }
        }
    }

    private fun setGpuPerformanceMode() {
        // Implement GPU performance mode setting
        // This will depend on your specific GPU architecture
    }

    private fun setGpuPowerSaveMode() {
        // Implement GPU power save mode setting
        // This will depend on your specific GPU architecture
    }

    private fun setGpuCoreCount(count: Int) {
        // Implement custom GPU core count setting
        // This will depend on your specific GPU architecture
    }

    private fun getAvailableCores(): IntArray {
        return try {
            val cores = mutableListOf<Int>()
            val reader = BufferedReader(FileReader("/sys/devices/system/cpu/online"))
            val line = reader.readLine()
            reader.close()

            line?.split(",")?.forEach { range ->
                if (range.contains("-")) {
                    val (start, end) = range.split("-").map { it.trim().toInt() }
                    cores.addAll(start..end)
                } else {
                    cores.add(range.trim().toInt())
                }
            }
            cores.toIntArray()
        } catch (e: Exception) {
            Log.e(TAG, "Error reading available cores: ${e.message}")
            intArrayOf()
        }
    }

    private fun getRootThreadGroup(): ThreadGroup {
        var rootGroup = Thread.currentThread().threadGroup
        while (rootGroup.parent != null) {
            rootGroup = rootGroup.parent
        }
        return rootGroup
    }

    private fun enumerateAllThreads(group: ThreadGroup): List<Thread> {
        val threads = mutableListOf<Thread>()
        val threadCount = group.activeCount()
        val threadArray = Array(threadCount) { Thread() }
        group.enumerate(threadArray)
        threads.addAll(threadArray.filter { it.id != 0 })

        val groupCount = group.activeGroupCount()
        val groupArray = Array(groupCount) { ThreadGroup("") }
        group.enumerate(groupArray)
        groupArray.forEach { subGroup ->
            threads.addAll(enumerateAllThreads(subGroup))
        }

        return threads
    }

    private fun shouldProcessThread(thread: Thread): Boolean {
        val name = thread.name.lowercase()
        return !name.contains("finalizer") &&
               !name.contains("gc") &&
               !name.contains("system") &&
               thread.id != Process.myTid()
    }
}