// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Tegra::Host1x::NvdecCommon {

enum class VideoCodec : u64 {
    None = 0x0,
    H264 = 0x3,
    VP8 = 0x5,
    H265 = 0x7,
    VP9 = 0x9,
};

struct Offset {
    constexpr u64 Address() const noexcept {
        return offset << 8;
    }

private:
    u64 offset;
};
static_assert(std::is_trivial_v<Offset>, "Offset must be trivial");
static_assert(sizeof(Offset) == 0x8, "Offset has the wrong size!");

// NVDEC should use a 32-bit address space, but is mapped to 64-bit,
// doubling the sizes here is compensating for that.
struct NvdecRegisters {
    static constexpr std::size_t NUM_REGS = 0x178;

    union {
        struct {
            INSERT_PADDING_WORDS_NOINIT(256); ///< 0x0000
            VideoCodec set_codec_id;          ///< 0x0400
            INSERT_PADDING_WORDS_NOINIT(126); ///< 0x0408
            u64 execute;                      ///< 0x0600
            INSERT_PADDING_WORDS_NOINIT(126); ///< 0x0608
            struct {                          ///< 0x0800
                union {
                    BitField<0, 3, VideoCodec> codec;
                    BitField<4, 1, u64> gp_timer_on;
                    BitField<13, 1, u64> mb_timer_on;
                    BitField<14, 1, u64> intra_frame_pslc;
                    BitField<17, 1, u64> all_intra_frame;
                };
            } control_params;
            Offset picture_info_offset;                    ///< 0x0808
            Offset frame_bitstream_offset;                 ///< 0x0810
            u64 frame_number;                              ///< 0x0818
            Offset h264_slice_data_offsets;                ///< 0x0820
            Offset h264_mv_dump_offset;                    ///< 0x0828
            INSERT_PADDING_WORDS_NOINIT(6);                ///< 0x0830
            Offset frame_stats_offset;                     ///< 0x0848
            Offset h264_last_surface_luma_offset;          ///< 0x0850
            Offset h264_last_surface_chroma_offset;        ///< 0x0858
            std::array<Offset, 17> surface_luma_offsets;   ///< 0x0860
            std::array<Offset, 17> surface_chroma_offsets; ///< 0x08E8
            Offset pic_scratch_buf_offset;                 ///< 0x0970
            Offset external_mvbuffer_offset;               ///< 0x0978
            INSERT_PADDING_WORDS_NOINIT(32);               ///< 0x0980
            Offset h264_mbhist_buffer_offset;              ///< 0x0A00
            INSERT_PADDING_WORDS_NOINIT(30);               ///< 0x0A08
            Offset vp8_prob_data_offset;                   ///< 0x0A80
            Offset vp8_header_partition_buf_offset;        ///< 0x0A88
            INSERT_PADDING_WORDS_NOINIT(28);               ///< 0x0A90
            Offset hvec_scalist_list_offset;               ///< 0x0B00
            Offset hvec_tile_sizes_offset;                 ///< 0x0B08
            Offset hvec_filter_buffer_offset;              ///< 0x0B10
            Offset hvec_sao_buffer_offset;                 ///< 0x0B18
            Offset hvec_slice_info_buffer_offset;          ///< 0x0B20
            Offset hvec_slice_group_index_offset;          ///< 0x0B28
            INSERT_PADDING_WORDS_NOINIT(20);               ///< 0x0B30
            Offset vp9_prob_tab_buffer_offset;             ///< 0x0B80
            Offset vp9_ctx_counter_buffer_offset;          ///< 0x0B88
            Offset vp9_segment_read_buffer_offset;         ///< 0x0B90
            Offset vp9_segment_write_buffer_offset;        ///< 0x0B98
            Offset vp9_tile_size_buffer_offset;            ///< 0x0BA0
            Offset vp9_col_mvwrite_buffer_offset;          ///< 0x0BA8
            Offset vp9_col_mvread_buffer_offset;           ///< 0x0BB0
            Offset vp9_filter_buffer_offset;               ///< 0x0BB8
        };
        std::array<u64, NUM_REGS> reg_array;
    };
};
static_assert(sizeof(NvdecRegisters) == (0xBC0), "NvdecRegisters is incorrect size");

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(NvdecRegisters, field_name) == position * sizeof(u64),                  \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(set_codec_id, 0x80);
ASSERT_REG_POSITION(execute, 0xC0);
ASSERT_REG_POSITION(control_params, 0x100);
ASSERT_REG_POSITION(picture_info_offset, 0x101);
ASSERT_REG_POSITION(frame_bitstream_offset, 0x102);
ASSERT_REG_POSITION(frame_number, 0x103);
ASSERT_REG_POSITION(h264_slice_data_offsets, 0x104);
ASSERT_REG_POSITION(frame_stats_offset, 0x109);
ASSERT_REG_POSITION(h264_last_surface_luma_offset, 0x10A);
ASSERT_REG_POSITION(h264_last_surface_chroma_offset, 0x10B);
ASSERT_REG_POSITION(surface_luma_offsets, 0x10C);
ASSERT_REG_POSITION(surface_chroma_offsets, 0x11D);
ASSERT_REG_POSITION(vp8_prob_data_offset, 0x150);
ASSERT_REG_POSITION(vp8_header_partition_buf_offset, 0x151);
ASSERT_REG_POSITION(vp9_prob_tab_buffer_offset, 0x170);
ASSERT_REG_POSITION(vp9_ctx_counter_buffer_offset, 0x171);
ASSERT_REG_POSITION(vp9_segment_read_buffer_offset, 0x172);
ASSERT_REG_POSITION(vp9_segment_write_buffer_offset, 0x173);
ASSERT_REG_POSITION(vp9_col_mvwrite_buffer_offset, 0x175);
ASSERT_REG_POSITION(vp9_col_mvread_buffer_offset, 0x176);

#undef ASSERT_REG_POSITION

} // namespace Tegra::Host1x::NvdecCommon
