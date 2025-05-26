// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
#pragma once

// Add these definitions if not already present

// Directory name constants
#define SUDACHI_DIR "sudachi"

// Enum for LegacyPath
enum class LegacyPath {
    SumiDir,
    SumiConfigDir,
    SumiCacheDir,
    SudachiDir,
    SudachiConfigDir,
    SudachiCacheDir,
    YuzuDir,
    YuzuConfigDir,
    YuzuCacheDir,
    SuyuDir,
    SuyuConfigDir,
    SuyuCacheDir,
    CitronDir,
    CitronConfigDir,
    CitronCacheDir,
    EdenDir,
    EdenConfigDir,
    EdenCacheDir,
};
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// sumi data directories

#define SUMI_DIR "sumi"
#define PORTABLE_DIR "user"

// Sub-directories contained within a sumi data directory

#define AMIIBO_DIR "amiibo"
#define CACHE_DIR "cache"
#define CONFIG_DIR "config"
#define CRASH_DUMPS_DIR "crash_dumps"
#define DUMP_DIR "dump"
#define KEYS_DIR "keys"
#define LOAD_DIR "load"
#define LOG_DIR "log"
#define NAND_DIR "nand"
#define PLAY_TIME_DIR "play_time"
#define SCREENSHOTS_DIR "screenshots"
#define SDMC_DIR "sdmc"
#define SHADER_DIR "shader"
#define TAS_DIR "tas"
#define ICONS_DIR "icons"

// sumi-specific files

#define LOG_FILE "sumi_log.txt"
