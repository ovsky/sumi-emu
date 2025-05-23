// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.sumi.sumi_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.sumi.sumi_emu.utils.ParamPackage
import org.sumi.sumi_emu.features.input.NativeInput
import org.sumi.sumi_emu.features.input.model.InputType
import org.sumi.sumi_emu.features.input.model.NativeButton

class ButtonInputSetting(
    override val playerIndex: Int,
    val nativeButton: NativeButton,
    @StringRes titleId: Int = 0,
    titleString: String = ""
) : InputSetting(titleId, titleString) {
    override val type = TYPE_INPUT
    override val inputType = InputType.Button

    override fun getSelectedValue(): String {
        val params = NativeInput.getButtonParam(playerIndex, nativeButton)
        val button = buttonToText(params)
        return getDisplayString(params, button)
    }

    override fun setSelectedValue(param: ParamPackage) =
        NativeInput.setButtonParam(playerIndex, nativeButton, param)
}
