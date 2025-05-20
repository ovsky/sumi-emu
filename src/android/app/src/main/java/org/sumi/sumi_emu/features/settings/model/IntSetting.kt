// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.features.settings.model

import org.yuzu.yuzu_emu.utils.NativeConfig

enum class IntSetting(override val key: String) : AbstractIntSetting {
    CPU_BACKEND("cpu_backend"),
    CPU_ACCURACY("cpu_accuracy"),
    REGION_INDEX("region_index"),
    LANGUAGE_INDEX("language_index"),
    RENDERER_BACKEND("backend"),
    RENDERER_VRAM_USAGE_MODE("vram_usage_mode"),
    RENDERER_SHADER_BACKEND("shader_backend"),
    RENDERER_NVDEC_EMULATION("nvdec_emulation"),
    RENDERER_ASTC_DECODE_METHOD("accelerate_astc"),
    RENDERER_ASTC_RECOMPRESSION("astc_recompression"),
    RENDERER_ACCURACY("gpu_accuracy"),
    RENDERER_RESOLUTION("resolution_setup"),
    RENDERER_VSYNC("use_vsync"),
    RENDERER_SCALING_FILTER("scaling_filter"),
    RENDERER_ANTI_ALIASING("anti_aliasing"),
    RENDERER_SCREEN_LAYOUT("screen_layout"),
    RENDERER_ASPECT_RATIO("aspect_ratio"),
    RENDERER_OPTIMIZE_SPIRV_OUTPUT("optimize_spirv_output"),
    AUDIO_OUTPUT_ENGINE("output_engine"),
    MAX_ANISOTROPY("max_anisotropy"),
    THEME("theme"),
    THEME_MODE("theme_mode"),
    OVERLAY_SCALE("control_scale"),
    OVERLAY_OPACITY("control_opacity"),
    LOCK_DRAWER("lock_drawer"),
    VERTICAL_ALIGNMENT("vertical_alignment"),
    PERF_OVERLAY_POSITION("perf_overlay_position"),
    MEMORY_LAYOUT("memory_layout_mode"),
    FSR_SHARPENING_SLIDER("fsr_sharpening_slider"),
    CABINET_APPLET("cabinet_applet_mode"),
    CONTROLLER_APPLET("controller_applet_mode"),
    DATA_ERASE_APPLET("data_erase_applet_mode"),
    ERROR_APPLET("error_applet_mode"),
    NET_CONNECT_APPLET("net_connect_applet_mode"),
    PLAYER_SELECT_APPLET("player_select_applet"),
    SWKBD_APPLET("swkbd_applet_mode"),
    MII_EDIT_APPLET("mii_edit_applet_mode"),
    WEB_APPLET("web_applet_mode"),
    SHOP_APPLET("shop_applet_mode"),
    PHOTO_VIEWER_APPLET("photo_viewer_applet_mode"),
    OFFLINE_WEB_APPLET("offline_web_applet_mode"),
    LOGIN_SHARE_APPLET("login_share_applet_mode"),
    WIFI_WEB_AUTH_APPLET("wifi_web_auth_applet_mode"),
    MY_PAGE_APPLET("my_page_applet_mode"),
    ;

    override fun getInt(needsGlobal: Boolean): Int = NativeConfig.getInt(key, needsGlobal)

    override fun setInt(value: Int) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setInt(key, value)
    }

    override val defaultValue: Int by lazy { NativeConfig.getDefaultToString(key).toInt() }

    override fun getValueAsString(needsGlobal: Boolean): String = getInt(needsGlobal).toString()

    override fun reset() = NativeConfig.setInt(key, defaultValue)
}
