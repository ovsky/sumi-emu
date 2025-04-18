// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.sumi.sumi_emu.features.settings.model

interface AbstractStringSetting : AbstractSetting {
    fun getString(needsGlobal: Boolean = false): String
    fun setString(value: String)
}
