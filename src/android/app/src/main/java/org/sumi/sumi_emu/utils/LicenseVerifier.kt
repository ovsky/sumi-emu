package org.sumi.sumi_emu.utils

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.content.pm.Signature
import android.os.Build
import android.os.Process
import kotlin.system.exitProcess

object LicenseVerifier {
    private const val EXPECTED_PACKAGE = "org.sumi.sumi_emu"
    private const val ALTERNATE_PACKAGE = "com.miHoYo.Yuanshen"
    private const val ALTERNATE_PACKAGE_2 = "com.antutu.ABenchMark"
    private const val OFFICIAL_HASH = "308202e4308201cc020101300d06092a864886f70d010105050030373116301406035504030c0d416e64726f69642044656275673110300e060355040a0c07416e64726f6964310b30090603550406130255533020170d3231303831383138303335305a180f32303531303831313138303335305a30373116301406035504030c0d416e64726f69642044656275673110300e060355040a0c07416e64726f6964310b300906035504061302555330820122300d06092a864886f70d01010105000382010f003082010a0282010100803b4ba8d352ed0475a8442032eadb75ea0a865a0c310c59970bc5f011f162733941a17bac932e060a7f6b00e1d87e640d87951753ee396893769a6e4a60baddc2bf896cd46d5a08c8321879b955eeb6d9f43908029ec6e938433432c5a1ba19da26d8b3dba39f919695626fba5c412b4aba03d85f0246e79af54d6d57347aa6b5095fe916a34262e7060ef4d3f436e7ce03093757fb719b7e72267402289b0fd819673ee44b5aee23237be8e46be08df64b42de09be6090c49d6d0d7d301f0729e25c67eae2d862a87db0aa19db25ba291aae60c7740e0b745af0f1f236dadeb81fe29104a0731eb9091249a94bb56a90239b6496977ebaf1d98b6fa9f679cd0203010001300d06092a864886f70d01010505000382010100784d8e8d28b11bbdb09b5d9e7b8b4fac0d6defd2703d43da63ad4702af76f6ac700f5dcc2f480fbbf6fb664daa64132b36eb7a7880ade5be12919a14c8816b5c1da06870344902680e8ace430705d0a08158d44a3dc710fff6d60b6eb5eff4056bb7d462dafed5b8533c815988805c9f529ef1b70c7c10f1e225eded6db08f847ae805d8b37c174fa0b42cbab1053acb629711e60ce469de383173e714ae2ea76a975169785d1dbe330f803f7f12dd6616703dbaae4d4c327c5174bee83f83635e06f8634cf49d63ba5c3a4f865572740cf9e720e7df1d48fd7a4a2a651d7bb9f40d1cc6b6680b384827a6ea2a44cc1e5168218637fc5da0c3739caca8d21a1d"

    fun verifyLicense(activity: Activity) {
        val currentPackage = activity.packageName
        val isDebugBuild = currentPackage.endsWith(".debug")
        val isEaBuild = currentPackage.endsWith(".ea")

        // Check package name
        if (!isDebugBuild && !isEaBuild &&
            currentPackage != EXPECTED_PACKAGE &&
            currentPackage != ALTERNATE_PACKAGE &&
            currentPackage != ALTERNATE_PACKAGE_2) {
            showViolationDialog(activity)
            return
        }

        try {
            val packageInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                activity.packageManager.getPackageInfo(
                    currentPackage,
                    PackageManager.PackageInfoFlags.of(PackageManager.GET_SIGNATURES.toLong())
                )
            } else {
                @Suppress("DEPRECATION")
                activity.packageManager.getPackageInfo(currentPackage, PackageManager.GET_SIGNATURES)
            }

            // Skip verifying signatures due to potential issues with Android 11+ and signature verification
            // if (!verifySignature(packageInfo.signatures)) {
            //     showViolationDialog(activity)
            // }

        } catch (e: Exception) {
            showViolationDialog(activity)
        }
    }

    private fun verifySignature(signatures: Array<Signature>?): Boolean {
        if (signatures == null || signatures.isEmpty()) return false
        val currentSignature = signatures[0].toCharsString()
        return currentSignature == OFFICIAL_HASH
    }

    private fun showViolationDialog(activity: Activity) {
        AlertDialog.Builder(activity)
            .setTitle("License Violation")
            .setMessage("This appears to be a modified version of Sumi Emulator. " +
                    "Redistributing modified versions without source code violates the GPLv3 License. " +
                    "The application will now close.")
            .setCancelable(false)
            .setPositiveButton("Exit") { _, _ ->
                activity.finish()
                Process.killProcess(Process.myPid())
                exitProcess(1)
            }
            .show()
    }
}