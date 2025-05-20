// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include "video_core/host1x/codecs/vp8.h"
#include "video_core/host1x/host1x.h"
#include "video_core/memory_manager.h"

namespace Tegra::Decoders {
VP8::VP8(Host1x::Host1x& host1x_, const Host1x::NvdecCommon::NvdecRegisters& regs_, s32 id_,
         Host1x::FrameQueue& frame_queue_)
    : Decoder{host1x_, id_, regs_, frame_queue_} {
    codec = Host1x::NvdecCommon::VideoCodec::VP8;
    initialized = decode_api.Initialize(codec);
}

VP8::~VP8() = default;

std::tuple<u64, u64> VP8::GetProgressiveOffsets() {
    auto luma{regs.surface_luma_offsets[static_cast<u32>(Vp8SurfaceIndex::Current)].Address()};
    auto chroma{regs.surface_chroma_offsets[static_cast<u32>(Vp8SurfaceIndex::Current)].Address()};
    return {luma, chroma};
}

std::tuple<u64, u64, u64, u64> VP8::GetInterlacedOffsets() {
    auto luma_top{regs.surface_luma_offsets[static_cast<u32>(Vp8SurfaceIndex::Current)].Address()};
    auto luma_bottom{
        regs.surface_luma_offsets[static_cast<u32>(Vp8SurfaceIndex::Current)].Address()};
    auto chroma_top{
        regs.surface_chroma_offsets[static_cast<u32>(Vp8SurfaceIndex::Current)].Address()};
    auto chroma_bottom{
        regs.surface_chroma_offsets[static_cast<u32>(Vp8SurfaceIndex::Current)].Address()};
    return {luma_top, luma_bottom, chroma_top, chroma_bottom};
}

std::span<const u8> VP8::ComposeFrame() {
    memory_manager.ReadBlock(regs.picture_info_offset.Address(), &current_context,
                             sizeof(VP8PictureInfo));

    const bool is_key_frame = current_context.key_frame == 1u;
    const auto bitstream_size = static_cast<size_t>(current_context.vld_buffer_size);
    const size_t header_size = is_key_frame ? 10u : 3u;
    frame_scratch.resize(header_size + bitstream_size);

    // Based on page 30 of the VP8 specification.
    // https://datatracker.ietf.org/doc/rfc6386/
    frame_scratch[0] = is_key_frame ? 0u : 1u; // 1-bit frame type (0: keyframe, 1: interframes).
    frame_scratch[0] |=
        static_cast<u8>((current_context.version & 7u) << 1u); // 3-bit version number
    frame_scratch[0] |= static_cast<u8>(1u << 4u);             // 1-bit show_frame flag

    // The next 19-bits are the first partition size
    frame_scratch[0] |= static_cast<u8>((current_context.first_part_size & 7u) << 5u);
    frame_scratch[1] = static_cast<u8>((current_context.first_part_size & 0x7f8u) >> 3u);
    frame_scratch[2] = static_cast<u8>((current_context.first_part_size & 0x7f800u) >> 11u);

    if (is_key_frame) {
        frame_scratch[3] = 0x9du;
        frame_scratch[4] = 0x01u;
        frame_scratch[5] = 0x2au;
        // TODO(ameerj): Horizontal/Vertical Scale
        // 16 bits: (2 bits Horizontal Scale << 14) | Width (14 bits)
        frame_scratch[6] = static_cast<u8>(current_context.frame_width & 0xff);
        frame_scratch[7] = static_cast<u8>(((current_context.frame_width >> 8) & 0x3f));
        // 16 bits:(2 bits Vertical Scale << 14) | Height (14 bits)
        frame_scratch[8] = static_cast<u8>(current_context.frame_height & 0xff);
        frame_scratch[9] = static_cast<u8>(((current_context.frame_height >> 8) & 0x3f));
    }
    const u64 bitstream_offset = regs.frame_bitstream_offset.Address();
    memory_manager.ReadBlock(bitstream_offset, frame_scratch.data() + header_size, bitstream_size);

    return frame_scratch;
}

} // namespace Tegra::Decoders
