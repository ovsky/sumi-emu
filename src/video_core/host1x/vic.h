// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "common/common_types.h"
#include "common/scratch_buffer.h"
#include "video_core/cdma_pusher.h"

namespace Tegra::Host1x {
class Host1x;
class Nvdec;

struct Pixel {
    u16 r;
    u16 g;
    u16 b;
    u16 a;
};

// One underscore represents separate pixels.
// Double underscore represents separate planes.
// _N represents chroma subsampling, not a separate pixel.
enum class VideoPixelFormat : u32 {
    A8 = 0,
    L8 = 1,
    A4L4 = 2,
    L4A4 = 3,
    R8 = 4,
    A8L8 = 5,
    L8A8 = 6,
    R8G8 = 7,
    G8R8 = 8,
    B5G6R5 = 9,
    R5G6B5 = 10,
    B6G5R5 = 11,
    R5G5B6 = 12,
    A1B5G5R5 = 13,
    A1R5G5B5 = 14,
    B5G5R5A1 = 15,
    R5G5B5A1 = 16,
    A5B5G5R1 = 17,
    A5R1G5B5 = 18,
    B5G5R1A5 = 19,
    R1G5B5A5 = 20,
    X1B5G5R5 = 21,
    X1R5G5B5 = 22,
    B5G5R5X1 = 23,
    R5G5B5X1 = 24,
    A4B4G5R4 = 25,
    A4R4G4B4 = 26,
    B4G4R4A4 = 27,
    R4G4B4A4 = 28,
    B8G8R8 = 29,
    R8G8B8 = 30,
    A8B8G8R8 = 31,
    A8R8G8B8 = 32,
    B8G8R8A8 = 33,
    R8G8B8A8 = 34,
    X8B8G8R8 = 35,
    X8R8G8B8 = 36,
    B8G8R8X8 = 37,
    R8G8B8X8 = 38,
    A8B10G10R10 = 39,
    A2R10G10B10 = 40,
    B10G10R10A2 = 41,
    R10G10B10A2 = 42,
    A4P4 = 43,
    P4A4 = 44,
    P8A8 = 45,
    A8P8 = 46,
    P8 = 47,
    P1 = 48,
    U8V8 = 49,
    V8U8 = 50,
    A8Y8U8V8 = 51,
    V8U8Y8A8 = 52,
    Y8U8V8 = 53,
    Y8V8U8 = 54,
    U8V8Y8 = 55,
    V8U8Y8 = 56,
    Y8U8_Y8V8 = 57,
    Y8V8_Y8U8 = 58,
    U8Y8_V8Y8 = 59,
    V8Y8_U8Y8 = 60,
    Y8__U8V8_N444 = 61,
    Y8__V8U8_N444 = 62,
    Y8__U8V8_N422 = 63,
    Y8__V8U8_N422 = 64,
    Y8__U8V8_N422R = 65,
    Y8__V8U8_N422R = 66,
    Y8__U8V8_N420 = 67,
    Y8__V8U8_N420 = 68,
    Y8__U8__V8_N444 = 69,
    Y8__U8__V8_N422 = 70,
    Y8__U8__V8_N422R = 71,
    Y8__U8__V8_N420 = 72,
    U8 = 73,
    V8 = 74,
};

struct Offset {
    constexpr u32 Address() const noexcept {
        return offset << 8;
    }

private:
    u32 offset;
};
static_assert(std::is_trivial_v<Offset>, "Offset must be trivial");
static_assert(sizeof(Offset) == 0x4, "Offset has the wrong size!");

struct PlaneOffsets {
    Offset luma;
    Offset chroma_u;
    Offset chroma_v;
};
static_assert(sizeof(PlaneOffsets) == 0xC, "PlaneOffsets has the wrong size!");

enum SurfaceIndex : u32 {
    Current = 0,
    Previous = 1,
    Next = 2,
    NextNoiseReduced = 3,
    CurrentMotion = 4,
    PreviousMotion = 5,
    PreviousPreviousMotion = 6,
    CombinedMotion = 7,
};

enum class DXVAHD_ALPHA_FILL_MODE : u32 {
    OPAQUE = 0,
    BACKGROUND = 1,
    DESTINATION = 2,
    SOURCE_STREAM = 3,
    COMPOSITED = 4,
    SOURCE_ALPHA = 5,
};

enum class DXVAHD_FRAME_FORMAT : u64 {
    PROGRESSIVE = 0,
    INTERLACED_TOP_FIELD_FIRST = 1,
    INTERLACED_BOTTOM_FIELD_FIRST = 2,
    TOP_FIELD = 3,
    BOTTOM_FIELD = 4,
    SUBPIC_PROGRESSIVE = 5,
    SUBPIC_INTERLACED_TOP_FIELD_FIRST = 6,
    SUBPIC_INTERLACED_BOTTOM_FIELD_FIRST = 7,
    SUBPIC_TOP_FIELD = 8,
    SUBPIC_BOTTOM_FIELD = 9,
    TOP_FIELD_CHROMA_BOTTOM = 10,
    BOTTOM_FIELD_CHROMA_TOP = 11,
    SUBPIC_TOP_FIELD_CHROMA_BOTTOM = 12,
    SUBPIC_BOTTOM_FIELD_CHROMA_TOP = 13,
};

enum class DXVAHD_DEINTERLACE_MODE_PRIVATE : u64 {
    WEAVE = 0,
    BOB_FIELD = 1,
    BOB = 2,
    NEWBOB = 3,
    DISI1 = 4,
    WEAVE_LUMA_BOB_FIELD_CHROMA = 5,
    MAX = 0xF,
};

enum class BLK_KIND {
    PITCH = 0,
    GENERIC_16Bx2 = 1,
    // These are unsupported in the vic
    BL_NAIVE = 2,
    BL_KEPLER_XBAR_RAW = 3,
    VP2_TILED = 15,
};

enum class BLEND_SRCFACTC : u32 {
    K1 = 0,
    K1_TIMES_DST = 1,
    NEG_K1_TIMES_DST = 2,
    K1_TIMES_SRC = 3,
    ZERO = 4,
};

enum class BLEND_DSTFACTC : u32 {
    K1 = 0,
    K2 = 1,
    K1_TIMES_DST = 2,
    NEG_K1_TIMES_DST = 3,
    NEG_K1_TIMES_SRC = 4,
    ZERO = 5,
    ONE = 6,
};

enum class BLEND_SRCFACTA : u32 {
    K1 = 0,
    K2 = 1,
    NEG_K1_TIMES_DST = 2,
    ZERO = 3,
    MAX = 7,
};

enum class BLEND_DSTFACTA : u32 {
    K2 = 0,
    NEG_K1_TIMES_SRC = 1,
    ZERO = 2,
    ONE = 3,
    MAX = 7,
};

struct PipeConfig {
    union {
        BitField<0, 11, u32> downsample_horiz;
        BitField<11, 5, u32> reserved0;
        BitField<16, 11, u32> downsample_vert;
        BitField<27, 5, u32> reserved1;
    };
    u32 reserved2;
    u32 reserved3;
    u32 reserved4;
};
static_assert(sizeof(PipeConfig) == 0x10, "PipeConfig has the wrong size!");

struct OutputConfig {
    union {
        BitField<0, 3, DXVAHD_ALPHA_FILL_MODE> alpha_fill_mode;
        BitField<3, 3, u64> alpha_fill_slot;
        BitField<6, 10, u64> background_a;
        BitField<16, 10, u64> background_r;
        BitField<26, 10, u64> background_g;
        BitField<36, 10, u64> background_b;
        BitField<46, 2, u64> regamma_mode;
        BitField<48, 1, u64> output_flip_x;
        BitField<49, 1, u64> output_flip_y;
        BitField<50, 1, u64> output_transpose;
        BitField<51, 1, u64> reserved1;
        BitField<52, 12, u64> reserved2;
    };
    union {
        BitField<0, 14, u32> target_rect_left;
        BitField<14, 2, u32> reserved3;
        BitField<16, 14, u32> target_rect_right;
        BitField<30, 2, u32> reserved4;
    };
    union {
        BitField<0, 14, u32> target_rect_top;
        BitField<14, 2, u32> reserved5;
        BitField<16, 14, u32> target_rect_bottom;
        BitField<30, 2, u32> reserved6;
    };
};
static_assert(sizeof(OutputConfig) == 0x10, "OutputConfig has the wrong size!");

struct OutputSurfaceConfig {
    union {
        BitField<0, 7, VideoPixelFormat> out_pixel_format;
        BitField<7, 2, u32> out_chroma_loc_horiz;
        BitField<9, 2, u32> out_chroma_loc_vert;
        BitField<11, 4, BLK_KIND> out_block_kind;
        BitField<15, 4, u32> out_block_height; // in gobs, log2
        BitField<19, 3, u32> reserved0;
        BitField<22, 10, u32> reserved1;
    };
    union {
        BitField<0, 14, u32> out_surface_width;   // - 1
        BitField<14, 14, u32> out_surface_height; // - 1
        BitField<28, 4, u32> reserved2;
    };
    union {
        BitField<0, 14, u32> out_luma_width;   // - 1
        BitField<14, 14, u32> out_luma_height; // - 1
        BitField<28, 4, u32> reserved3;
    };
    union {
        BitField<0, 14, u32> out_chroma_width;   // - 1
        BitField<14, 14, u32> out_chroma_height; // - 1
        BitField<28, 4, u32> reserved4;
    };
};
static_assert(sizeof(OutputSurfaceConfig) == 0x10, "OutputSurfaceConfig has the wrong size!");

struct MatrixStruct {
    union {
        BitField<0, 20, s64> matrix_coeff00;  // (0,0) of 4x3 conversion matrix
        BitField<20, 20, s64> matrix_coeff10; // (1,0) of 4x3 conversion matrix
        BitField<40, 20, s64> matrix_coeff20; // (2,0) of 4x3 conversion matrix
        BitField<60, 4, u64> matrix_r_shift;
    };
    union {
        BitField<0, 20, s64> matrix_coeff01;  // (0,1) of 4x3 conversion matrix
        BitField<20, 20, s64> matrix_coeff11; // (1,1) of 4x3 conversion matrix
        BitField<40, 20, s64> matrix_coeff21; // (2,1) of 4x3 conversion matrix
        BitField<60, 3, u64> reserved0;
        BitField<63, 1, u64> matrix_enable;
    };
    union {
        BitField<0, 20, s64> matrix_coeff02;  // (0,2) of 4x3 conversion matrix
        BitField<20, 20, s64> matrix_coeff12; // (1,2) of 4x3 conversion matrix
        BitField<40, 20, s64> matrix_coeff22; // (2,2) of 4x3 conversion matrix
        BitField<60, 4, u64> reserved1;
    };
    union {
        BitField<0, 20, s64> matrix_coeff03;  // (0,3) of 4x3 conversion matrix
        BitField<20, 20, s64> matrix_coeff13; // (1,3) of 4x3 conversion matrix
        BitField<40, 20, s64> matrix_coeff23; // (2,3) of 4x3 conversion matrix
        BitField<60, 4, u64> reserved2;
    };
};
static_assert(sizeof(MatrixStruct) == 0x20, "MatrixStruct has the wrong size!");

struct ClearRectStruct {
    union {
        BitField<0, 14, u32> clear_rect0_left;
        BitField<14, 2, u32> reserved0;
        BitField<16, 14, u32> clear_rect0_right;
        BitField<30, 2, u32> reserved1;
    };
    union {
        BitField<0, 14, u32> clear_rect0_top;
        BitField<14, 2, u32> reserved2;
        BitField<16, 14, u32> clear_rect0_bottom;
        BitField<30, 2, u32> reserved3;
    };
    union {
        BitField<0, 14, u32> clear_rect1_left;
        BitField<14, 2, u32> reserved4;
        BitField<16, 14, u32> clear_rect1_right;
        BitField<30, 2, u32> reserved5;
    };
    union {
        BitField<0, 14, u32> clear_rect1_top;
        BitField<14, 2, u32> reserved6;
        BitField<16, 14, u32> clear_rect1_bottom;
        BitField<30, 2, u32> reserved7;
    };
};
static_assert(sizeof(ClearRectStruct) == 0x10, "ClearRectStruct has the wrong size!");

struct SlotConfig {
    union {
        BitField<0, 1, u64> slot_enable;
        BitField<1, 1, u64> denoise;
        BitField<2, 1, u64> advanced_denoise;
        BitField<3, 1, u64> cadence_detect;
        BitField<4, 1, u64> motion_map;
        BitField<5, 1, u64> motion_map_capture;
        BitField<6, 1, u64> is_even;
        BitField<7, 1, u64> chroma_even;
        // fetch control struct
        BitField<8, 1, u64> current_field_enable;
        BitField<9, 1, u64> prev_field_enable;
        BitField<10, 1, u64> next_field_enable;
        BitField<11, 1, u64> next_nr_field_enable; // noise reduction
        BitField<12, 1, u64> current_motion_field_enable;
        BitField<13, 1, u64> prev_motion_field_enable;
        BitField<14, 1, u64> prev_prev_motion_field_enable;
        BitField<15, 1, u64> combined_motion_field_enable;

        BitField<16, 4, DXVAHD_FRAME_FORMAT> frame_format;
        BitField<20, 2, u64> filter_length_y; // 0: 1-tap, 1: 2-tap, 2: 5-tap, 3: 10-tap
        BitField<22, 2, u64> filter_length_x;
        BitField<24, 12, u64> panoramic;
        BitField<36, 22, u64> reserved1;
        BitField<58, 6, u64> detail_filter_clamp;
    };
    union {
        BitField<0, 10, u64> filter_noise;
        BitField<10, 10, u64> filter_detail;
        BitField<20, 10, u64> chroma_noise;
        BitField<30, 10, u64> chroma_detail;
        BitField<40, 4, DXVAHD_DEINTERLACE_MODE_PRIVATE> deinterlace_mode;
        BitField<44, 3, u64> motion_accumulation_weight;
        BitField<47, 11, u64> noise_iir;
        BitField<58, 4, u64> light_level;
        BitField<62, 2, u64> reserved4;
    };
    union {
        BitField<0, 10, u64> soft_clamp_low;
        BitField<10, 10, u64> soft_clamp_high;
        BitField<20, 3, u64> reserved5;
        BitField<23, 9, u64> reserved6;
        BitField<32, 10, u64> planar_alpha;
        BitField<42, 1, u64> constant_alpha;
        BitField<43, 3, u64> stereo_interleave;
        BitField<46, 1, u64> clip_enabled;
        BitField<47, 8, u64> clear_rect_mask;
        BitField<55, 2, u64> degamma_mode;
        BitField<57, 1, u64> reserved7;
        BitField<58, 1, u64> decompress_enable;
        BitField<59, 5, u64> reserved9;
    };
    union {
        BitField<0, 8, u64> decompress_ctb_count;
        BitField<8, 32, u64> decompress_zbc_count;
        BitField<40, 24, u64> reserved12;
    };
    union {
        BitField<0, 30, u64> source_rect_left;
        BitField<30, 2, u64> reserved14;
        BitField<32, 30, u64> source_rect_right;
        BitField<62, 2, u64> reserved15;
    };
    union {
        BitField<0, 30, u64> source_rect_top;
        BitField<30, 2, u64> reserved16;
        BitField<32, 30, u64> source_rect_bottom;
        BitField<62, 2, u64> reserved17;
    };
    union {
        BitField<0, 14, u64> dest_rect_left;
        BitField<14, 2, u64> reserved18;
        BitField<16, 14, u64> dest_rect_right;
        BitField<30, 2, u64> reserved19;
        BitField<32, 14, u64> dest_rect_top;
        BitField<46, 2, u64> reserved20;
        BitField<48, 14, u64> dest_rect_bottom;
        BitField<62, 2, u64> reserved21;
    };
    u32 reserved22;
    u32 reserved23;
};
static_assert(sizeof(SlotConfig) == 0x40, "SlotConfig has the wrong size!");

struct SlotSurfaceConfig {
    union {
        BitField<0, 7, VideoPixelFormat> slot_pixel_format;
        BitField<7, 2, u32> slot_chroma_loc_horiz;
        BitField<9, 2, u32> slot_chroma_loc_vert;
        BitField<11, 4, u32> slot_block_kind;
        BitField<15, 4, u32> slot_block_height;
        BitField<19, 3, u32> slot_cache_width;
        BitField<22, 10, u32> reserved0;
    };
    union {
        BitField<0, 14, u32> slot_surface_width;   //  - 1
        BitField<14, 14, u32> slot_surface_height; //  - 1
        BitField<28, 4, u32> reserved1;
    };
    union {
        BitField<0, 14, u32> slot_luma_width;   // padded, - 1
        BitField<14, 14, u32> slot_luma_height; // padded, - 1
        BitField<28, 4, u32> reserved2;
    };
    union {
        BitField<0, 14, u32> slot_chroma_width;   // padded, - 1
        BitField<14, 14, u32> slot_chroma_height; // padded, - 1
        BitField<28, 4, u32> reserved3;
    };
};
static_assert(sizeof(SlotSurfaceConfig) == 0x10, "SlotSurfaceConfig has the wrong size!");

struct LumaKeyStruct {
    union {
        BitField<0, 20, u64> luma_coeff0;  // (0) of 4x1 conversion matrix, S12.8 format
        BitField<20, 20, u64> luma_coeff1; // (1) of 4x1 conversion matrix, S12.8 format
        BitField<40, 20, u64> luma_coeff2; // (2) of 4x1 conversion matrix, S12.8 format
        BitField<60, 4, u64> luma_r_shift;
    };
    union {
        BitField<0, 20, u64> luma_coeff3; // (3) of 4x1 conversion matrix, S12.8 format
        BitField<20, 10, u64> luma_key_lower;
        BitField<30, 10, u64> luma_key_upper;
        BitField<40, 1, u64> luma_key_enabled;
        BitField<41, 2, u64> reserved0;
        BitField<43, 21, u64> reserved1;
    };
};
static_assert(sizeof(LumaKeyStruct) == 0x10, "LumaKeyStruct has the wrong size!");

struct BlendingSlotStruct {
    union {
        BitField<0, 10, u32> alpha_k1;
        BitField<10, 6, u32> reserved0;
        BitField<16, 10, u32> alpha_k2;
        BitField<26, 6, u32> reserved1;
    };
    union {
        BitField<0, 3, BLEND_SRCFACTC> src_factor_color_match_select;
        BitField<3, 1, u32> reserved2;
        BitField<4, 3, BLEND_DSTFACTC> dst_factor_color_match_select;
        BitField<7, 1, u32> reserved3;
        BitField<8, 3, BLEND_SRCFACTA> src_factor_a_match_select;
        BitField<11, 1, u32> reserved4;
        BitField<12, 3, BLEND_DSTFACTA> dst_factor_a_match_select;
        BitField<15, 1, u32> reserved5;
        BitField<16, 4, u32> reserved6;
        BitField<20, 4, u32> reserved7;
        BitField<24, 4, u32> reserved8;
        BitField<28, 4, u32> reserved9;
    };
    union {
        BitField<0, 2, u32> reserved10;
        BitField<2, 10, u32> override_r;
        BitField<12, 10, u32> override_g;
        BitField<22, 10, u32> override_b;
    };
    union {
        BitField<0, 10, u32> override_a;
        BitField<10, 2, u32> reserved11;
        BitField<12, 1, u32> use_override_r;
        BitField<13, 1, u32> use_override_g;
        BitField<14, 1, u32> use_override_b;
        BitField<15, 1, u32> use_override_a;
        BitField<16, 1, u32> mask_r;
        BitField<17, 1, u32> mask_g;
        BitField<18, 1, u32> mask_b;
        BitField<19, 1, u32> mask_a;
        BitField<20, 12, u32> reserved12;
    };
};
static_assert(sizeof(BlendingSlotStruct) == 0x10, "BlendingSlotStruct has the wrong size!");

struct SlotStruct {
    SlotConfig config;
    SlotSurfaceConfig surface_config;
    LumaKeyStruct luma_key;
    MatrixStruct color_matrix;
    MatrixStruct gamut_matrix;
    BlendingSlotStruct blending;
};
static_assert(sizeof(SlotStruct) == 0xB0, "SlotStruct has the wrong size!");

struct ConfigStruct {
    PipeConfig pipe_config;
    OutputConfig output_config;
    OutputSurfaceConfig output_surface_config;
    MatrixStruct out_color_matrix;
    std::array<ClearRectStruct, 4> clear_rects;
    std::array<SlotStruct, 8> slot_structs;
};
static_assert(offsetof(ConfigStruct, pipe_config) == 0x0, "pipe_config is in the wrong place!");
static_assert(offsetof(ConfigStruct, output_config) == 0x10,
              "output_config is in the wrong place!");
static_assert(offsetof(ConfigStruct, output_surface_config) == 0x20,
              "output_surface_config is in the wrong place!");
static_assert(offsetof(ConfigStruct, out_color_matrix) == 0x30,
              "out_color_matrix is in the wrong place!");
static_assert(offsetof(ConfigStruct, clear_rects) == 0x50, "clear_rects is in the wrong place!");
static_assert(offsetof(ConfigStruct, slot_structs) == 0x90, "slot_structs is in the wrong place!");
static_assert(sizeof(ConfigStruct) == 0x610, "ConfigStruct has the wrong size!");

struct VicRegisters {
    static constexpr std::size_t NUM_REGS = 0x446;

    union {
        struct {
            INSERT_PADDING_WORDS_NOINIT(0xC0);
            u32 execute;
            INSERT_PADDING_WORDS_NOINIT(0x3F);
            std::array<std::array<PlaneOffsets, 8>, 8> surfaces;
            u32 picture_index;
            u32 control_params;
            Offset config_struct_offset;
            Offset filter_struct_offset;
            Offset palette_offset;
            Offset hist_offset;
            u32 context_id;
            u32 fce_ucode_size;
            PlaneOffsets output_surface;
            Offset fce_ucode_offset;
            INSERT_PADDING_WORDS_NOINIT(0x4);
            std::array<u32, 8> slot_context_ids;
            std::array<Offset, 8> comp_tag_buffer_offsets;
            std::array<Offset, 8> history_buffer_offset;
            INSERT_PADDING_WORDS_NOINIT(0x25D);
            u32 pm_trigger_end;
        };
        std::array<u32, NUM_REGS> reg_array;
    };
};
static_assert(offsetof(VicRegisters, execute) == 0x300, "execute is in the wrong place!");
static_assert(offsetof(VicRegisters, surfaces) == 0x400, "surfaces is in the wrong place!");
static_assert(offsetof(VicRegisters, picture_index) == 0x700,
              "picture_index is in the wrong place!");
static_assert(offsetof(VicRegisters, control_params) == 0x704,
              "control_params is in the wrong place!");
static_assert(offsetof(VicRegisters, config_struct_offset) == 0x708,
              "config_struct_offset is in the wrong place!");
static_assert(offsetof(VicRegisters, output_surface) == 0x720,
              "output_surface is in the wrong place!");
static_assert(offsetof(VicRegisters, slot_context_ids) == 0x740,
              "slot_context_ids is in the wrong place!");
static_assert(offsetof(VicRegisters, history_buffer_offset) == 0x780,
              "history_buffer_offset is in the wrong place!");
static_assert(offsetof(VicRegisters, pm_trigger_end) == 0x1114,
              "pm_trigger_end is in the wrong place!");
static_assert(sizeof(VicRegisters) == 0x1118, "VicRegisters has the wrong size!");

class Vic final : public CDmaPusher {
public:
    enum class Method : u32 {
        Execute = offsetof(VicRegisters, execute),
        SetControlParams = offsetof(VicRegisters, control_params),
        SetConfigStructOffset = offsetof(VicRegisters, config_struct_offset),
        SetOutputSurfaceLumaOffset = offsetof(VicRegisters, output_surface.luma),
        SetOutputSurfaceChromaOffset = offsetof(VicRegisters, output_surface.chroma_u),
        SetOutputSurfaceChromaUnusedOffset = offsetof(VicRegisters, output_surface.chroma_v)
    };

    explicit Vic(Host1x& host1x, s32 id, u32 syncpt, FrameQueue& frame_queue);
    ~Vic();

    /// Write to the device state.
    void ProcessMethod(u32 method, u32 arg) override;

private:
    void Execute();

    void Blend(const ConfigStruct& config, const SlotStruct& slot);

    template <bool Planar, bool Interlaced = false>
    void ReadProgressiveY8__V8U8_N420(const SlotStruct& slot, std::span<const PlaneOffsets> offsets,
                                      std::shared_ptr<const FFmpeg::Frame> frame);
    template <bool Planar, bool TopField>
    void ReadInterlacedY8__V8U8_N420(const SlotStruct& slot, std::span<const PlaneOffsets> offsets,
                                     std::shared_ptr<const FFmpeg::Frame> frame);

    template <bool Planar>
    void ReadY8__V8U8_N420(const SlotStruct& slot, std::span<const PlaneOffsets> offsets,
                           std::shared_ptr<const FFmpeg::Frame> frame);

    void WriteY8__V8U8_N420(const OutputSurfaceConfig& output_surface_config);

    template <VideoPixelFormat Format>
    void WriteABGR(const OutputSurfaceConfig& output_surface_config);

    s32 id;
    s32 nvdec_id{-1};
    u32 syncpoint;

    VicRegisters regs{};
    FrameQueue& frame_queue;

    const bool has_sse41{false};

    Common::ScratchBuffer<Pixel> output_surface;
    Common::ScratchBuffer<Pixel> slot_surface;
    Common::ScratchBuffer<u8> luma_scratch;
    Common::ScratchBuffer<u8> chroma_scratch;
    Common::ScratchBuffer<u8> swizzle_scratch;
};

} // namespace Tegra::Host1x
