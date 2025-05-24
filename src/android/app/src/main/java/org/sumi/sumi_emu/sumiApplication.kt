// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.sumi.sumi_emu

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import org.sumi.sumi_emu.features.input.NativeInput
import java.io.File
import org.sumi.sumi_emu.utils.DirectoryInitialization
import org.sumi.sumi_emu.utils.DocumentsTree
import org.sumi.sumi_emu.utils.GpuDriverHelper
import org.sumi.sumi_emu.utils.Log
import org.sumi.sumi_emu.utils.ThreadManager
import org.sumi.sumi_emu.thermal.ThermalMonitor
import org.sumi.sumi_emu.utils.ThermalManager

fun Context.getPublicFilesDir(): File = getExternalFilesDir(null) ?: filesDir

class SumiApplication : Application() {
    private fun createNotificationChannels() {
        val noticeChannel = NotificationChannel(
            getString(R.string.notice_notification_channel_id),
            getString(R.string.notice_notification_channel_name),
            NotificationManager.IMPORTANCE_HIGH
        )
        noticeChannel.description = getString(R.string.notice_notification_channel_description)
        noticeChannel.setSound(null, null)

        // Register the channel with the system; you can't change the importance
        // or other notification behaviors after this
        val notificationManager = getSystemService(NotificationManager::class.java)
        notificationManager.createNotificationChannel(noticeChannel)
    }

    override fun onCreate() {
        super.onCreate()
        application = this
        documentsTree = DocumentsTree()
        DirectoryInitialization.start()
        GpuDriverHelper.initializeDriverParameters()
        NativeInput.reloadInputDevices()
        NativeLibrary.logDeviceInfo()
        Log.logDeviceInfo()

        // Initialize managers
        threadManager = ThreadManager.getInstance()
        threadManager.setLowestPriority()
        thermalMonitor = ThermalMonitor(this)
        thermalMonitor.StartThermalMonitor()
        thermalManager = ThermalManager.getInstance(this)

        createNotificationChannels()
    }

    companion object {
        var documentsTree: DocumentsTree? = null
        lateinit var application: SumiApplication
        lateinit var thermalManager: ThermalManager
        lateinit var thermalMonitor: ThermalMonitor
        lateinit var threadManager: ThreadManager

        val appContext: Context
            get() = application.applicationContext
    }
}
