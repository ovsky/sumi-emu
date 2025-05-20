// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <common/settings_common.h>
#include "common/common_types.h"
#include "common/settings_setting.h"

namespace AndroidSettings {

struct GameDir {
    std::string path;
    bool deep_scan = false;
};

struct OverlayControlData {
    std::string id;
    bool enabled;
    std::pair<double, double> landscape_position;
    std::pair<double, double> portrait_position;
    std::pair<double, double> foldable_position;
};

struct Values {
    Settings::Linkage linkage;

    // Path settings
    std::vector<GameDir> game_dirs;

    // Android
    Settings::Setting<bool> picture_in_picture{linkage, false, "picture_in_picture",
                                               Settings::Category::Android};
    Settings::Setting<s32> screen_layout{linkage,
                                         5,
                                         "screen_layout",
                                         Settings::Category::Android,
                                         Settings::Specialization::Default,
                                         true,
                                         true};
    Settings::Setting<s32> vertical_alignment{linkage,
                                              0,
                                              "vertical_alignment",
                                              Settings::Category::Android,
                                              Settings::Specialization::Default,
                                              true,
                                              true};

    Settings::SwitchableSetting<std::string, false> driver_path{linkage, "", "driver_path",
                                                                Settings::Category::GpuDriver};

    // LRU Cache
    Settings::SwitchableSetting<bool> use_lru_cache{linkage, true, "use_lru_cache",
                                             Settings::Category::System};

    Settings::Setting<s32> theme{linkage, 0, "theme", Settings::Category::Android};
    Settings::Setting<s32> theme_mode{linkage, -1, "theme_mode", Settings::Category::Android};
    Settings::Setting<bool> black_backgrounds{linkage, false, "black_backgrounds",
                                              Settings::Category::Android};

    // Input/performance overlay settings
    std::vector<OverlayControlData> overlay_control_data;
    Settings::Setting<s32> overlay_scale{linkage, 50, "control_scale", Settings::Category::Overlay};
    Settings::Setting<s32> overlay_opacity{linkage, 100, "control_opacity",
                                           Settings::Category::Overlay};

    Settings::Setting<bool> joystick_rel_center{linkage, true, "joystick_rel_center",
                                                Settings::Category::Overlay};
    Settings::Setting<bool> dpad_slide{linkage, true, "dpad_slide", Settings::Category::Overlay};
    Settings::Setting<bool> haptic_feedback{linkage, true, "haptic_feedback",
                                            Settings::Category::Overlay};
    Settings::Setting<bool> show_performance_overlay{linkage, true, "show_performance_overlay",
                                                     Settings::Category::Overlay, Settings::Specialization::Paired, true , true};
    Settings::Setting<bool> overlay_background{linkage, false, "overlay_background",
                                               Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<s32> perf_overlay_position{linkage, 0, "perf_overlay_position",
                                                 Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_fps{linkage, true, "show_fps",
                                     Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_frame_time{linkage, false, "show_frame_time",
                                           Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_app_ram_usage{linkage, false, "show_app_ram_usage",
                                               Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_system_ram_usage{linkage, false, "show_system_ram_usage",
                                                  Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_bat_temperature{linkage, false, "show_bat_temperature",
                                                 Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_shaders_building{linkage, true, "show_shaders_building",
                                                 Settings::Category::Overlay, Settings::Specialization::Default, true , true, &show_performance_overlay};
    Settings::Setting<bool> show_input_overlay{linkage, true, "show_input_overlay",
                                               Settings::Category::Overlay};
    Settings::Setting<bool> touchscreen{linkage, true, "touchscreen", Settings::Category::Overlay};
    Settings::Setting<s32> lock_drawer{linkage, false, "lock_drawer", Settings::Category::Overlay};

    Settings::Setting<bool> dont_show_eden_veil_warning{linkage, false, "dont_show_eden_veil_warning", Settings::Category::Miscellaneous};
};

extern Values values;

} // namespace AndroidSettings
