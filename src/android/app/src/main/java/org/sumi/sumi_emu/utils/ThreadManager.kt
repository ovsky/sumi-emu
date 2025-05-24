package org.sumi.sumi_emu.utils

import android.os.Process
import android.util.Log

object ThreadManager {
    private const val TAG = "ThreadManager"

    fun setLowestPriority() {
        try {
            // Set the current thread to lowest priority
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)

            // Get all threads in the process
            val rootGroup = Thread.currentThread().threadGroup
            val threads = enumerateAllThreads(rootGroup)

            Log.d(TAG, "Found ${threads.size} threads")

            // Set all threads to lowest priority except system threads
            for (thread in threads) {
                try {
                    if (thread != null && thread.isAlive) {
                        // Skip system-critical threads
                        if (!thread.name.startsWith("Finalizer") && !thread.name.startsWith("GC")) {
                            Log.d(TAG, "Setting thread ${thread.name} to lowest priority")
                            Process.setThreadPriority(thread.id.toInt(), Process.THREAD_PRIORITY_BACKGROUND)
                        }
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to adjust thread '${thread.name}': ${e.message}")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to set thread priorities: ${e.message}")
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
}