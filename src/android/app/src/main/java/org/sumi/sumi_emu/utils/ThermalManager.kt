package org.sumi.sumi_emu.utils

import android.content.Context
import android.os.Process
import android.util.Log

class ThermalManager(private val context: Context) {
    private val TAG = "ThermalManager"

    init {
        Log.d(TAG, "Initializing ThermalManager")
        setInitialProcessorState()
    }

    private fun setInitialProcessorState() {
        Log.d(TAG, "Setting initial processor state to lowest priority")
        Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
    }

    companion object {
        @Volatile
        private var instance: ThermalManager? = null

        fun getInstance(context: Context): ThermalManager {
            return instance ?: synchronized(this) {
                instance ?: ThermalManager(context).also { instance = it }
            }
        }
    }
}