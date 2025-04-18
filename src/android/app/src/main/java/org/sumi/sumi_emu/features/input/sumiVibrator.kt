// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.sumi.sumi_emu.features.input

import android.content.Context
import android.os.Build
import android.os.CombinedVibration
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.InputDevice
import androidx.annotation.Keep
import androidx.annotation.RequiresApi
import org.sumi.sumi_emu.SumiApplication

@Keep
@Suppress("DEPRECATION")
interface SumiVibrator {
    fun supportsVibration(): Boolean

    fun vibrate(intensity: Float)

    companion object {
        fun getControllerVibrator(device: InputDevice): SumiVibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                SumiVibratorManager(device.vibratorManager)
            } else {
                SumiVibratorManagerCompat(device.vibrator)
            }

        fun getSystemVibrator(): SumiVibrator =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = SumiApplication.appContext
                    .getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                SumiVibratorManager(vibratorManager)
            } else {
                val vibrator = SumiApplication.appContext
                    .getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
                SumiVibratorManagerCompat(vibrator)
            }

        fun getVibrationEffect(intensity: Float): VibrationEffect? {
            if (intensity > 0f) {
                return VibrationEffect.createOneShot(
                    50,
                    (255.0 * intensity).toInt().coerceIn(1, 255)
                )
            }
            return null
        }
    }
}

@RequiresApi(Build.VERSION_CODES.S)
class SumiVibratorManager(private val vibratorManager: VibratorManager) : SumiVibrator {
    override fun supportsVibration(): Boolean {
        return vibratorManager.vibratorIds.isNotEmpty()
    }

    override fun vibrate(intensity: Float) {
        val vibration = SumiVibrator.getVibrationEffect(intensity) ?: return
        vibratorManager.vibrate(CombinedVibration.createParallel(vibration))
    }
}

class SumiVibratorManagerCompat(private val vibrator: Vibrator) : SumiVibrator {
    override fun supportsVibration(): Boolean {
        return vibrator.hasVibrator()
    }

    override fun vibrate(intensity: Float) {
        val vibration = SumiVibrator.getVibrationEffect(intensity) ?: return
        vibrator.vibrate(vibration)
    }
}
