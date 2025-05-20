// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.features.settings.model

import org.yuzu.yuzu_emu.utils.NativeConfig

enum class BooleanSetting(override val key: String) : AbstractBooleanSetting {
    AUDIO_MUTED("audio_muted"),
    CPU_DEBUG_MODE("cpu_debug_mode"),
    FAST_CPU_TIME("use_fast_cpu_time"),
    FASTMEM("cpuopt_fastmem"),
    FASTMEM_EXCLUSIVES("cpuopt_fastmem_exclusives"),
    CORE_SYNC_CORE_SPEED("sync_core_speed"),
    RENDERER_USE_SPEED_LIMIT("use_speed_limit"),
    USE_DOCKED_MODE("use_docked_mode"),
    USE_AUTO_STUB("use_auto_stub"),
    RENDERER_USE_DISK_SHADER_CACHE("use_disk_shader_cache"),
    RENDERER_FORCE_MAX_CLOCK("force_max_clock"),
    RENDERER_ASYNCHRONOUS_SHADERS("use_asynchronous_shaders"),
    RENDERER_FAST_GPU("use_fast_gpu_time"),
    RENDERER_REACTIVE_FLUSHING("use_reactive_flushing"),
    RENDERER_DEBUG("debug"),
    RENDERER_PROVOKING_VERTEX("provoking_vertex"),
    RENDERER_DESCRIPTOR_INDEXING("descriptor_indexing"),
    PICTURE_IN_PICTURE("picture_in_picture"),
    USE_CUSTOM_RTC("custom_rtc_enabled"),
    BLACK_BACKGROUNDS("black_backgrounds"),
    JOYSTICK_REL_CENTER("joystick_rel_center"),
    DPAD_SLIDE("dpad_slide"),
    HAPTIC_FEEDBACK("haptic_feedback"),
    SHOW_PERFORMANCE_OVERLAY("show_performance_overlay"),
    SHOW_INPUT_OVERLAY("show_input_overlay"),
    TOUCHSCREEN("touchscreen"),
    SHOW_THERMAL_OVERLAY("show_thermal_overlay"),
    FRAME_INTERPOLATION("frame_interpolation"),
//    FRAME_SKIPPING("frame_skipping"),
    SHOW_FPS("show_fps"),
    SHOW_FRAMETIME("show_frame_time"),
    SHOW_APP_RAM_USAGE("show_app_ram_usage"),
    SHOW_SYSTEM_RAM_USAGE("show_system_ram_usage"),
    SHOW_BAT_TEMPERATURE("show_bat_temperature"),
    SHOW_SHADERS_BUILDING("show_shaders_building"),
    OVERLAY_BACKGROUND("overlay_background"),
    DONT_SHOW_SUMI_VEIL_WARNING("dont_show_eden_veil_warning"),
    DEBUG_FLUSH_BY_LINE("flush_lines"),
    USE_LRU_CACHE("use_lru_cache"),;
//    external fun isFrameSkippingEnabled(): Boolean
    external fun isFrameInterpolationEnabled(): Boolean


    override fun getBoolean(needsGlobal: Boolean): Boolean =
        NativeConfig.getBoolean(key, needsGlobal)

    override fun setBoolean(value: Boolean) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setBoolean(key, value)
    }

    override val defaultValue: Boolean by lazy { NativeConfig.getDefaultToString(key).toBoolean() }

    override fun getValueAsString(needsGlobal: Boolean): String = getBoolean(needsGlobal).toString()

    override fun reset() = NativeConfig.setBoolean(key, defaultValue)
}
